"""Receiver selection and persistent-connection lifecycle, without a live Hub."""
import ctypes as c
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'integrations/slicer'))
from qarvtk_host_runtime import (APP_TAG, EXPECTED_SDK, DeviceSelection, HubConnection, HostTransport)


def peer(number, category='headset', available=True, name='QAROS Player'):
    return dict(id=f'{number:032x}', category=category, available=available,
                name=name, device='Headset', presence='Online' if available else 'Offline')


class FakeHubDll:
    def __init__(self):
        self._qarvtk_info = dict(header_sha256=EXPECTED_SDK, device_selection=1,
                                persistent_connection=1, stable_app_tag=APP_TAG)
        self.connects = self.stops = self.destroys = self.stream_stops = 0
        self.result = 1
        self.snapshot = dict(connected=True, status='Connected', app_tag=APP_TAG,
                             source_peer='f' * 32, peers=[peer(1), peer(2)])

    def qarvtk_hub_connect(self, sdk, folder, result, error, size):
        self.connects += 1
        c.cast(result, c.POINTER(c.c_void_p)).contents.value = 77
        return 0

    def qarvtk_hub_poll(self, handle, buffer, size):
        buffer.value = json.dumps(self.snapshot).encode()
        return self.result

    def qarvtk_hub_stop(self, handle):
        self.stops += 1

    def qarvtk_hub_destroy(self, handle):
        self.destroys += 1
        return int(self.destroys > 1)

    def qarvtk_host_stop(self, handle):
        self.stream_stops += 1


class DeviceTests(unittest.TestCase):
    def test_several_headsets_and_visualizers_can_be_checked_together(self):
        rows = [peer(1), peer(2), peer(3, 'visualizer')]
        selection = DeviceSelection()
        selection.update(dict(connected=True, peers=rows))
        for row in rows:
            selection.select(row['id'], True)
        self.assertEqual(set(selection.selected_ids()), {row['id'] for row in rows})

    def test_no_headset_does_not_block_a_checked_visualizer(self):
        selection = DeviceSelection([peer(1)['id'], peer(2)['id']])
        selection.update(dict(connected=True, peers=[peer(1, available=False), peer(2, 'visualizer')]))
        self.assertEqual(selection.selected_ids(), [peer(2)['id']])
        with self.assertRaisesRegex(ValueError, 'connected receiver'):
            selection.select(peer(1)['id'], True)

    def test_no_automatic_selection_or_same_name_fallback(self):
        selection = DeviceSelection([peer(1)['id']])
        selection.update(dict(connected=True, peers=[peer(1), peer(2)]))
        selection.update(dict(connected=True, peers=[peer(2)]))
        self.assertEqual(selection.selected_ids(), [])
        self.assertEqual(len(selection.peers), 2)
        self.assertFalse(next(p for p in selection.peers if p['id'] == peer(1)['id'])['available'])

    def test_saved_exact_selection_survives_reordering_and_reconnection(self):
        selection = DeviceSelection([peer(2)['id']])
        for rows in ([peer(1), peer(2)], [peer(2), peer(1)], [peer(2, available=False), peer(1)]):
            selection.update(dict(connected=True, peers=rows))
        self.assertEqual(selection.selected_ids(), [])
        selection.update(dict(connected=True, peers=[peer(1), peer(2)]))
        self.assertEqual(selection.selected_ids(), [peer(2)['id']])

    def test_disconnected_hub_makes_all_rows_unavailable(self):
        selection = DeviceSelection([peer(1)['id']])
        selection.update(dict(connected=False, peers=[peer(1)]))
        self.assertEqual(selection.selected_ids(), [])

    def test_invalid_and_non_receiver_rows_are_excluded(self):
        selection = DeviceSelection(['invalid'])
        selection.update(dict(connected=True, peers=[peer(1, 'camera'), dict(peer(2), id='bad'), peer(3)]))
        self.assertEqual([p['id'] for p in selection.peers], [peer(3)['id']])
        self.assertEqual(selection.selected_ids(), [])

    def test_receiver_limit_is_total_and_does_not_limit_one_per_category(self):
        rows = [peer(i) for i in range(1, 10)]
        selection = DeviceSelection()
        selection.update(dict(connected=True, peers=rows))
        for row in rows[:8]: selection.select(row['id'], True)
        with self.assertRaisesRegex(ValueError, 'eight'):
            selection.select(rows[8]['id'], True)
        self.assertEqual(len(selection.selected_ids()), 8)

    def connection(self, dll):
        with patch('qarvtk_host_runtime.validate_sdk', return_value=EXPECTED_SDK), \
             patch('qarvtk_host_runtime.library', return_value=dll):
            return HubConnection('host.dll', 'sdk', 'logs')

    def test_repeated_discovery_reuses_one_enrollment_and_tag(self):
        dll = FakeHubDll()
        hub = self.connection(dll)
        for _ in range(10):
            snapshot = hub.tick()
            self.assertEqual(snapshot['app_tag'], 'slicer.qaros.quaternar')
            self.assertEqual(snapshot['source_peer'], 'f' * 32)
        self.assertEqual(dll.connects, 1)

    def test_stream_stop_keeps_the_hub_connection(self):
        dll = FakeHubDll()
        hub = self.connection(dll)
        hub.tick()
        stream = object.__new__(HostTransport)
        stream.handle = c.c_void_p(99)
        stream.dll, stream.connection = dll, hub
        stream.stopping, stream.panels = False, None
        stream.diagnostics = MagicMock()
        stream.request_stop('Stop button')
        self.assertEqual(dll.stream_stops, 1)
        self.assertEqual(dll.stops, 0)
        self.assertTrue(hub.connected)

    def test_disconnect_is_async_and_idempotent(self):
        dll = FakeHubDll()
        hub = self.connection(dll)
        hub.tick()
        hub.request_stop(); hub.request_stop()
        self.assertFalse(hub.connected)
        hub.tick()
        self.assertTrue(hub.running)
        hub.tick()
        self.assertFalse(hub.running)
        self.assertEqual(dll.stops, 1)

    def test_connection_error_is_visible_and_stops_only_once(self):
        dll = FakeHubDll()
        hub = self.connection(dll)
        dll.result = -1
        dll.snapshot = dict(connected=False, peers=[], status='Hub unavailable')
        self.assertEqual(hub.tick()['status'], 'Hub unavailable')
        self.assertFalse(hub.connected)
        self.assertEqual(dll.stops, 1)

    def test_old_bridge_is_rejected_before_enrollment(self):
        dll = FakeHubDll()
        dll._qarvtk_info['persistent_connection'] = 0
        with self.assertRaisesRegex(RuntimeError, 'r8.6'):
            self.connection(dll)
        self.assertEqual(dll.connects, 0)


if __name__ == '__main__':
    unittest.main()
