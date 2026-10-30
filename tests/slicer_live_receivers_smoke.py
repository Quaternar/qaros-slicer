"""Opt-in on-device test: real installed QAROS and two temporary Visualizers.

Run in an isolated Slicer --testing process with the installed Live module
and its main window (do not use --no-main-window).
Only receiver slots created by this script are removed. Existing receivers
are never selected, stopped or changed.
"""
import json
from pathlib import Path
import re
import sys
import time
import traceback
import uuid
import numpy as np
import slicer

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'package/windows-start/plugins/slicer'))
import QarosLive
QarosLive.QarosLiveWidget.autoConnect = False
from QarosVTKLib.qarvtk_host_runtime import HubConnection, HostTransport, VisualizerMcp, installed_sdk_path
from QarosVTKLib.qarvtk_host_view import create_host_views


def run():
    assert slicer.app.layoutManager(), 'This GPU test requires the main Slicer window'
    assert QarosLive.LIVE_VERSION == 'r8.7', 'Install r8.7 before this test'
    client = VisualizerMcp()
    hub = transport = None
    created, records = [], []
    def unpack(result):
        return result.get('structuredContent') or json.loads(result['content'][0]['text'])
    def receiver(name):
        result = unpack(client.call('viz_receiver_add', dict(name=name, start=True)))
        assert result.get('ok'), result
        created.append(result['slot_id'])
        return result['peer_id'].replace('-', '')
    def pump():
        if hub: hub.tick()
        if transport: transport.tick()
        slicer.app.processEvents()
        time.sleep(.005)
    def wait(predicate, label, timeout=35):
        end = time.perf_counter() + timeout
        while time.perf_counter() < end:
            pump()
            if predicate(): return
        raise RuntimeError(label + ': ' + (transport.status() if transport else str(hub.snapshot)))
    def frames(name):
        match = re.search(re.escape(name) + r'.*?; (\d+) frames', transport.status())
        return int(match.group(1)) if match else 0
    def record(label):
        item = dict(step=label, status=transport.status(), source_peer=hub.snapshot['source_peer'],
                    native_handle=transport.handle.value, serial=transport.diagnostics.last_serial)
        records.append(item)
        print(json.dumps(item), flush=True)
    name = 'Slicer Hot Test ' + uuid.uuid4().hex[:8]
    log_folder = root / 'build-host-dev3184/live-receivers-smoke'
    log_folder.mkdir(parents=True, exist_ok=True)
    try:
        first = receiver(name + ' A')
        hub = HubConnection(root / 'package/windows-start/Release/qarvtk-host.dll', installed_sdk_path(), log_folder / 'hub')
        wait(lambda: hub.connected and any(p['id'] == first and p['available'] for p in hub.snapshot['peers']), 'First receiver discovery')
        slicer.app.layoutManager().setLayout(slicer.vtkMRMLLayoutNode.SlicerLayoutOneUp3DView)
        view = slicer.app.layoutManager().threeDWidget(0).threeDView()
        volume = slicer.mrmlScene.AddNewNodeByClass('vtkMRMLScalarVolumeNode', 'Live receiver test')
        z, y, x = np.indices((24, 24, 24))
        data = np.where((x-11.5)**2+(y-11.5)**2+(z-11.5)**2 < 9**2, 900, 0).astype(np.int16)
        slicer.util.updateVolumeFromArray(volume, data)
        volume.SetSpacing(3., 3., 3.)
        volume.CreateDefaultDisplayNodes()
        display = slicer.modules.volumerendering.logic().CreateDefaultVolumeRenderingNodes(volume)
        display.SetVisibility(True)
        prop = display.GetVolumePropertyNode().GetVolumeProperty()
        prop.GetRGBTransferFunction().AddRGBPoint(0, 0., 0., 0.)
        prop.GetRGBTransferFunction().AddRGBPoint(900, 1., .2, .1)
        prop.GetScalarOpacity().AddPoint(0, 0.)
        prop.GetScalarOpacity().AddPoint(899, 0.)
        prop.GetScalarOpacity().AddPoint(900, .6)
        slicer.app.processEvents()
        view.resetFocalPoint()
        view.renderWindow().Render()
        hosts = create_host_views(view, separate=True)
        transport = HostTransport(root / 'package/windows-start/Release/qarvtk-host.dll', installed_sdk_path(),
                                  'selected', log_folder, hosts, connection=hub, selected_peers=[first])
        wait(lambda: frames(name + ' A') >= 3, 'First receiver frames')
        record('one receiver')
        before = frames(name + ' A')
        second = receiver(name + ' B')
        wait(lambda: any(p['id'] == second and p['available'] for p in hub.snapshot['peers']), 'Late receiver discovery')
        transport.set_receivers([first, second])
        wait(lambda: frames(name + ' A') > before and frames(name + ' B') >= 3, 'Concurrent receiver frames')
        record('late receiver added')
        before = frames(name + ' B')
        transport.set_receivers([second])
        wait(lambda: frames(name + ' B') > before + 2, 'Remaining receiver continues')
        record('first receiver removed')
        transport.set_receivers([])
        wait(lambda: 'Streaming is idle' in transport.status(), 'Empty selection idle')
        record('all receivers unchecked')
        serial = transport.diagnostics.last_serial
        end = time.perf_counter() + .3
        while time.perf_counter() < end: pump()
        assert transport.diagnostics.last_serial == serial
        transport.set_receivers([first])
        wait(lambda: frames(name + ' A') >= 3, 'Idle receiver resumes')
        record('receiver resumed')
        assert len({r['source_peer'] for r in records}) == 1
        assert len({r['native_handle'] for r in records}) == 1
    finally:
        failures = []
        try:
            if transport:
                transport.request_stop('On-device smoke completed')
                wait(lambda: not transport.running, 'Transport teardown', 30)
        except Exception as error: failures.append(str(error))
        try:
            if hub:
                hub.request_stop()
                wait(lambda: not hub.running, 'Hub teardown', 30)
        except Exception as error: failures.append(str(error))
        for slot in created:
            try:
                assert unpack(client.call('viz_receiver_remove', dict(slot_id=slot))).get('ok')
            except Exception as error: failures.append(str(error))
        (log_folder / 'result.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
        if failures: raise RuntimeError('Cleanup: ' + '; '.join(failures))


try:
    run()
except Exception:
    traceback.print_exc()
    slicer.app.exit(1)
else:
    print('PASS: actual stereo frames, hot-created second receiver, concurrent frames, remove, empty pause/resume; same source and host; test slots removed', flush=True)
    slicer.app.exit(0)
