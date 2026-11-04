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
    assert widget.newReceiverButton.enabled
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
        updates = []
        fail = False
        resolution_status = 'Test fixture'
        def request_stop(self, *args): self.stopping = True
        def set_receivers(self, ids):
            if self.fail: raise RuntimeError('Receiver still disconnecting; retry shortly')
            self.updates.append(list(ids))
        def tick(self): pass
        def status(self): return 'Test fixture'

    widget.activeReceiverIds = [peer(1)['id'], peer(2)['id']]
    widget.deviceSelection.checked = set(widget.activeReceiverIds)
    widget.transport = Stream()
    widget.update_controls()
    assert not widget.startButton.enabled and widget.stopButton.enabled
    assert all(box.enabled for box in widget.deviceRows.values())
    assert widget.newReceiverButton.enabled
    # A late headset joins while the existing two are rendering.
    connection.snapshot['peers'].append(peer(5))
    widget.deviceSelection.update(connection.snapshot)
    widget.update_device_rows()
    widget.deviceRows[peer(5)['id']].checked = True
    assert set(widget.activeReceiverIds) == {peer(1)['id'], peer(2)['id'], peer(5)['id']}
    widget.transport.fail = True
    widget.deviceRows[peer(4)['id']].checked = True
    assert not widget.deviceRows[peer(4)['id']].checked
    assert 'retry' in widget.receiverSelectionStatus.text
    widget.transport.fail = False
    widget.deviceRows[peer(1)['id']].checked = False
    assert set(widget.activeReceiverIds) == {peer(2)['id'], peer(5)['id']}
    widget.deviceRows[peer(2)['id']].checked = False
    widget.deviceRows[peer(5)['id']].checked = False
    assert widget.activeReceiverIds == []
    assert not widget.transport.stopping and connection.connected
    widget.deviceRows[peer(3)['id']].checked = True
    assert widget.activeReceiverIds == [peer(3)['id']]
    # Creation remains accessible with no available receiver.
    connection.snapshot['peers'] = []
    widget.deviceSelection.checked.clear()
    widget.deviceSelection.update(connection.snapshot)
    widget.update_device_rows(); widget.update_controls()
    assert widget.newReceiverButton.enabled
    class Creator:
        running = False
        result = None
        def start(self): self.running = True; return True
        def poll(self):
            result, self.result = self.result, None
            return result
    widget.receiverCreator = Creator()
    widget.create_receiver()
    assert not widget.newReceiverButton.enabled
    assert not widget.transport.stopping
    widget.receiverCreator.running = False
    widget.receiverCreator.result = (True, 'Created (UI test fixture)')
    widget.tick()
    assert widget.newReceiverButton.enabled and 'Created' in widget.receiverCreationStatus.text
    connection.snapshot['peers'] = [peer(3, 'visualizer', name='Visualizer')]
    widget.deviceSelection.update(connection.snapshot)
    widget.deviceSelection.checked = {peer(3)['id']}
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
    print('PASS: PythonQt hot-add/remove; multiple headsets; empty pause/resume; creation with no receiver; exact IDs; persistent Hub', flush=True)


try:
    run()
except Exception:
    traceback.print_exc()
    slicer.app.exit(1)
else:
    slicer.app.exit(0)
