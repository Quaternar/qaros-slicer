"""Real Slicer/PythonQt receiver controls with an isolated discovery fixture."""
import json
from pathlib import Path
import sys
import traceback
import slicer

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'package/QAROS-VTK-plugins/slicer'))
sys.path.insert(0, str(root / 'tests'))
from test_host_devices import peer
import QarosLive


def run():
    QarosLive.QarosLiveWidget.autoConnect = False
    representation = slicer.modules.qaroslive.widgetRepresentation()
    widget = representation.self()

    class Connection:
        running = connected = True
        stopping = False
        snapshot = dict(connected=True, status='Connected (UI test fixture)',
                        peers=[peer(1), peer(2), peer(3, 'visualizer', name='Visualizer')])
        def tick(self): return self.snapshot
        def request_stop(self):
            self.running = self.connected = False
            self.stopping = True

    connection = Connection()
    widget.connection = connection
    widget.deviceSelection.checked.clear()
    widget.deviceSelection.update(connection.snapshot)
    widget.update_device_rows(); widget.update_controls()
    assert not widget.startButton.enabled
    assert len(widget.deviceRows) == 3
    assert widget.deviceRows[peer(1)['id']].text != widget.deviceRows[peer(2)['id']].text
    widget.deviceRows[peer(1)['id']].checked = True
    widget.deviceRows[peer(2)['id']].checked = True
    assert widget.startButton.enabled
    assert set(widget.deviceSelection.selected_ids()) == {peer(1)['id'], peer(2)['id']}

    # Both headsets go offline. The connected Visualizer can still be checked
    # and started, without selecting an absent headset or timing out blindly.
    connection.snapshot = dict(connected=True, status='Connected (UI test fixture)',
                               peers=[peer(3, 'visualizer', name='Visualizer')])
    widget.deviceSelection.update(connection.snapshot)
    widget.update_device_rows(); widget.update_controls()
    assert not widget.startButton.enabled
    assert not widget.deviceRows[peer(1)['id']].enabled
    assert not widget.deviceRows[peer(2)['id']].enabled
    widget.deviceRows[peer(3)['id']].checked = True
    assert widget.startButton.enabled
    assert widget.deviceSelection.selected_ids() == [peer(3)['id']]

    # The same two headsets return, plus a replacement with the same name.
    connection.snapshot['peers'] = [peer(4), peer(2), peer(3, 'visualizer', name='Visualizer'), peer(1)]
    widget.deviceSelection.update(connection.snapshot)
    widget.update_device_rows(); widget.update_controls()
    assert widget.deviceRows[peer(1)['id']].checked
    assert widget.deviceRows[peer(2)['id']].checked
    assert not widget.deviceRows[peer(4)['id']].checked

    class Stream:
        running = True
        stopping = False
        def request_stop(self, *args): self.stopping = True

    widget.activeReceiverIds = [peer(1)['id'], peer(2)['id']]
    widget.transport = Stream()
    widget.update_controls()
    assert not widget.startButton.enabled and widget.stopButton.enabled
    assert all(not box.enabled for box in widget.deviceRows.values())
    widget.transport.running = False
    widget.update_controls()
    assert widget.startButton.enabled and not widget.stopButton.enabled
    assert connection.connected, 'Stopping a stream disconnected the Hub'
    widget.transport = None

    representation.resize(800, 1600)
    if representation.layout(): representation.layout().activate()
    screenshot = root / 'build-host-dev3184/receiver-picker.png'
    screenshot.parent.mkdir(parents=True, exist_ok=True)
    representation.grab().save(str(screenshot))
    widget.cleanup()
    widget.tick()
    assert not widget.timer.isActive()
    print('PASS: real PythonQt receiver checkboxes; several identical-model headsets; no-headset Visualizer; exact-id reconnection; stream/connection lifetime', flush=True)


try:
    run()
except Exception:
    traceback.print_exc()
    slicer.app.exit(1)
else:
    slicer.app.exit(0)
