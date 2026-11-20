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
import qt

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'package/windows-start/plugins/slicer'))
import QarosLive
QarosLive.QarosLiveWidget.autoConnect = False
from QarosVTKLib.qarvtk_host_runtime import HubConnection, HostTransport, VisualizerMcp, installed_sdk_path
from QarosVTKLib.qarvtk_host_view import create_host_views, workspace_envelope
from QarosVTKLib.qarvtk_host_panels import SlicePanels


def run():
    assert slicer.app.layoutManager(), 'This GPU test requires the main Slicer window'
    assert QarosLive.LIVE_VERSION == 'r8.11', 'Install r8.11 before this test'
    client = VisualizerMcp()
    pause_policy = client.call('viz_state', {})['structuredContent']['stream_pause_policy']
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
        detail = transport.status() if transport else str(hub.snapshot)
        if transport and transport.panels:
            detail += ' panels=' + str({name: transport.panel_status(source.handle)
                                       for name, source in transport.panels.sources.items()})
        raise RuntimeError(label + ': ' + detail)
    def frames(name):
        match = re.search(re.escape(name) + r'.*?; (\d+) frames', transport.status())
        return int(match.group(1)) if match else 0
    def record(label):
        item = dict(step=label, status=transport.status(), source_peer=hub.snapshot['source_peer'],
                    native_handle=transport.handle.value, serial=transport.diagnostics.last_serial)
        if transport.panels:
            item['panels'] = {name: dict(visible=source.visible, status=transport.panel_status(source.handle))
                              for name, source in transport.panels.sources.items()}
            item['panel_errors'] = dict(transport.panels.errors)
        records.append(item)
        print(json.dumps(item), flush=True)
    name = 'Slicer Hot Test ' + uuid.uuid4().hex[:8]
    log_folder = root / 'build-host-dev3184/live-receivers-smoke'
    log_folder.mkdir(parents=True, exist_ok=True)
    try:
        client.call('viz_stream_pause_policy', dict(policy='never'))
        first = receiver(name + ' A')
        hub = HubConnection(root / 'package/windows-start/Release/qarvtk-host.dll', installed_sdk_path(), log_folder / 'hub')
        wait(lambda: hub.connected and any(p['id'] == first and p['available'] for p in hub.snapshot['peers']), 'First receiver discovery')
        slicer.app.layoutManager().setLayout(slicer.vtkMRMLLayoutNode.SlicerLayoutFourUpView)
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
        slicer.util.setSliceViewerLayers(background=volume)
        slicer.util.resetSliceViews()
        position, size = workspace_envelope(hosts)
        transport.panels = SlicePanels(slicer.app.layoutManager(), transport, position, size, controls=True)
        controls = transport.panels.controls
        wait(lambda: len(transport.panels.sources) == 3, 'Three slice previews')
        panel = next(w for w in qt.QApplication.topLevelWidgets() if w.objectName == 'qarvtkSliceControls')
        native_sliders = [panel.findChild(qt.QSlider, 'sliceSlider' + str(i)) for i in range(3)]
        for i, native in enumerate(native_sliders):
            original = controls.slider(i)
            requested = original.minimum + (original.maximum-original.minimum)*.35
            original.value = requested
            pump()
            expected = round((original.value-original.minimum)/(original.maximum-original.minimum)*10000)
            assert abs(native.value-expected) <= 1, (i, native.value, expected)
            native.setSliderDown(True)
            native.value = 6500
            native.setSliderDown(False)
            pump()
            requested = original.minimum + (original.maximum-original.minimum)*.65
            assert abs(original.value-requested) < .01, (i, original.value, requested)
        wait(lambda: all(transport.panel_status(s.handle).startswith('Live ') for s in transport.panels.sources.values()),
             'Live slice video panels')
        assert not transport.panels.errors, transport.panels.errors
        panel.grab().save(str(log_folder / 'qt-slice-controls.png'))
        record('Qt slice controls synchronized in both directions')
        # Native sequence widgets control the same proxy volume already being streamed.
        sequence = slicer.mrmlScene.AddNewNodeByClass('vtkMRMLSequenceNode', 'Playback test')
        for index in range(4):
            slicer.util.updateVolumeFromArray(volume, np.roll(data, index, axis=0))
            sequence.SetDataNodeAtValue(volume, str(index))
        browser = slicer.mrmlScene.AddNewNodeByClass('vtkMRMLSequenceBrowserNode', 'Playback test')
        browser.SetAndObserveMasterSequenceNodeID(sequence.GetID())
        browser.AddProxyNode(volume, sequence, False)
        controls.playback.selector.setCurrentNode(browser)
        wait(lambda: controls.playback.available and panel.height > 252, 'Native sequence controls appear')
        controls.playback.seek.setSelectedItemNumber(2)
        assert browser.GetSelectedItemNumber() == 2
        browser.SetSelectedItemNumber(1)
        pump()
        seek_slider = controls.playback.seek.findChild(qt.QSlider, 'slider_IndexValue')
        assert seek_slider.value == 1, seek_slider.value
        controls.playback.play.onVcrLast()
        assert browser.GetSelectedItemNumber() == 3
        controls.playback.play.onVcrFirst()
        assert browser.GetSelectedItemNumber() == 0
        browser.SetPlaybackRateFps(5.)
        play_button = controls.playback.play.findChild(qt.QPushButton, 'pushButton_VcrPlayPause')
        play_button.click()
        wait(lambda: browser.GetPlaybackActive() and browser.GetSelectedItemNumber() > 0, 'Native sequence playback')
        play_button.click()
        assert not browser.GetPlaybackActive()
        controls.playback.stop.click()
        assert browser.GetSelectedItemNumber() == 0 and not browser.GetPlaybackActive()
        panel.grab().save(str(log_folder / 'qt-sequence-controls.png'))
        record('Native sequence play, pause, first, last, seek and Stop passed')
        layout = slicer.app.layoutManager()
        extra_layout = '''<layout type="horizontal"><item>
          <view class="vtkMRMLViewNode" singletontag="1"><property name="viewlabel" action="default">1</property></view>
          </item><item><layout type="vertical">'''
        for view_name, color in (('Red', '#F34A33'), ('Yellow', '#EDD54C'), ('Green', '#6EB04B'), ('Red2', '#F34A33')):
            extra_layout += '<item><view class="vtkMRMLSliceNode" singletontag="' + view_name + '">' + \
                '<property name="orientation" action="default">Axial</property>' + \
                '<property name="viewlabel" action="default">' + view_name + '</property>' + \
                '<property name="viewcolor" action="default">' + color + '</property></view></item>'
        extra_layout += '</layout></item></layout>'
        layout.layoutLogic().GetLayoutNode().AddLayoutDescription(501, extra_layout)
        layout.setLayout(501)
        assert layout.threeDWidget(0).threeDView() == view, 'Layout replaced the active 3D source'
        layout.sliceWidget('Red2').sliceLogic().GetSliceCompositeNode().SetBackgroundVolumeID(volume.GetID())
        slicer.util.resetSliceViews()
        wait(lambda: 'Red2' in controls.names and 'Red2' in transport.panels.sources, 'Additional slice view controls')
        index = controls.names.index('Red2')
        extra_slider = panel.findChild(qt.QSlider, 'sliceSlider' + str(index))
        extra_slider.value = 7000
        pump()
        original = controls.slider(index)
        expected = original.minimum + (original.maximum-original.minimum)*.7
        assert abs(original.value-expected) < .02
        wait(lambda: all(transport.panel_status(s.handle).startswith('Live ')
                        for s in transport.panels.sources.values()), 'Four slice video panels')
        record('Additional Red2 slice view synchronized')
        transport.set_receivers([])
        wait(lambda: 'Streaming is idle' in transport.status(), 'Qt controls pause with empty selection')
        transport.set_receivers([first])
        wait(lambda: frames(name + ' A') >= 3 and all(transport.panel_status(s.handle).startswith('Live ')
             for s in transport.panels.sources.values()), 'Slice panels resume after empty selection')
        assert transport.panels.controls is controls
        record('Qt controls and slice panels resumed')
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
        try:
            client.call('viz_stream_pause_policy', dict(policy=pause_policy))
        except Exception as error: failures.append(str(error))
        (log_folder / 'result.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
        if failures: raise RuntimeError('Cleanup: ' + '; '.join(failures))


try:
    run()
except Exception:
    traceback.print_exc()
    slicer.app.exit(1)
else:
    print('PASS: stereo streams, live receiver selection, Qt slice controls in both directions and slice video; test slots removed', flush=True)
    slicer.app.exit(0)
