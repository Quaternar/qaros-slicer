"""Opt-in real Slicer CT benchmark; caller provides its own test receivers.

Set QARVTK_PERF_CONFIG to a JSON file with dll, receivers (two exact ids),
output, warmup_s and sample_s. Uses temporary Slicer settings (--testing).
Does not install a plugin, change receiver slots or touch the user's scene.
"""
import hashlib
import json
import os
from pathlib import Path
import sys
import time
import traceback
import numpy as np
import slicer
import vtk

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root))
from integrations.slicer.qarvtk_host_runtime import HostTransport, HubConnection, installed_sdk_path
from integrations.slicer.qarvtk_host_view import create_host_views


def run():
    config = json.loads(Path(os.environ['QARVTK_PERF_CONFIG']).read_text(encoding='utf-8'))
    output = Path(config['output'])
    output.mkdir(parents=True, exist_ok=True)
    hub = transport = None
    hosts = []
    samples, phases = [], []
    measuring = False
    def pump():
        if hub: hub.tick()
        if transport: transport.tick()
        slicer.app.processEvents()
        time.sleep(.001)
    def wait(predicate, label, seconds=40):
        deadline = time.perf_counter() + seconds
        while time.perf_counter() < deadline:
            pump()
            if predicate(): return
        raise RuntimeError(label + ': ' + (transport.status() if transport else str(hub.snapshot)))
    def duration(seconds):
        deadline = time.perf_counter() + seconds
        while time.perf_counter() < deadline: pump()
    try:
        assert slicer.app.layoutManager(), 'Use the full Slicer main window'
        peers = config['receivers']
        assert len(peers) == 2 and peers[0] != peers[1]
        hub = HubConnection(config['dll'], installed_sdk_path(), output / 'hub')
        wait(lambda: hub.connected and set(peers).issubset({p['id'] for p in hub.snapshot['peers'] if p['available']}), 'Test receiver discovery')
        slicer.app.layoutManager().setLayout(slicer.vtkMRMLLayoutNode.SlicerLayoutOneUp3DView)
        view = slicer.app.layoutManager().threeDWidget(0).threeDView()
        volume = slicer.mrmlScene.AddNewNodeByClass('vtkMRMLScalarVolumeNode', 'Controlled CT')
        z, y, x = np.indices((96, 96, 96), dtype=np.float32)
        radius = (x-47.5)**2 + (y-47.5)**2 + (z-47.5)**2
        values = np.where(radius < 38**2, 400 + x*4 + z*2, 0).astype(np.int16)
        slicer.util.updateVolumeFromArray(volume, values)
        volume.SetSpacing(1., 1., 1.)
        volume.CreateDefaultDisplayNodes()
        display = slicer.modules.volumerendering.logic().CreateDefaultVolumeRenderingNodes(volume)
        display.SetVisibility(True)
        prop = display.GetVolumePropertyNode().GetVolumeProperty()
        color, opacity = prop.GetRGBTransferFunction(), prop.GetScalarOpacity()
        color.RemoveAllPoints(); color.AddRGBPoint(0, 0., 0., 0.); color.AddRGBPoint(400, .6, .2, .1); color.AddRGBPoint(1000, 1., 1., .8)
        opacity.RemoveAllPoints(); opacity.AddPoint(0, 0.); opacity.AddPoint(399, 0.); opacity.AddPoint(1000, .6)
        prop.ShadeOn()
        slicer.app.processEvents(); view.resetFocalPoint(); view.renderWindow().Render()
        hosts = create_host_views(view, separate=True)
        transport = HostTransport(config['dll'], installed_sdk_path(), 'selected', output, hosts,
                                  connection=hub, selected_peers=[peers[0]])
        original_record = transport.diagnostics.record
        def record(serial, timings, eyes, started, *args, **kwargs):
            if measuring:
                samples.append(dict(serial=int(serial), start=started, end=time.perf_counter(),
                                    timings=dict(timings), eye_sizes=[list(e['size']) for e in eyes]))
            original_record(serial, timings, eyes, started, *args, **kwargs)
        transport.diagnostics.record = record
        for count in (1, 2):
            if count == 2: transport.set_receivers(peers)
            wait(lambda: transport.status().count('1/1 volumes') == count, 'Sender readiness')
            duration(float(config.get('warmup_s', 5)))
            samples.clear()
            started = time.perf_counter()
            measuring = True
            duration(float(config.get('sample_s', 30)))
            measuring = False
            elapsed = time.perf_counter() - started
            assert len(samples) >= 20, 'Too few frames for a useful measurement'
            intervals = np.diff([s['end'] for s in samples])*1000
            timing_keys = set().union(*(s['timings'] for s in samples))
            summary = dict(receivers=count, frames=len(samples), seconds=elapsed,
                           completed_gui_stereo_fps=len(samples)/elapsed,
                           interval_median_ms=float(np.median(intervals)),
                           interval_p95_ms=float(np.percentile(intervals, 95)),
                           interval_p99_ms=float(np.percentile(intervals, 99)),
                           interval_max_ms=float(np.max(intervals)),
                           mean_ms={key: float(np.mean([s['timings'].get(key, 0.) for s in samples])) for key in timing_keys},
                           eye_sizes=samples[-1]['eye_sizes'], source_peer=hub.snapshot['source_peer'],
                           host_handle=transport.handle.value,
                           render_sizes=[list(size) for size in transport.host.last_render_sizes],
                           samples=list(samples))
            assert summary['mean_ms']['host_render_calls'] == 2
            assert summary['mean_ms']['volume_depth_passes'] == 0
            phases.append(summary)
            print('BENCH ' + json.dumps({key: value for key, value in summary.items() if key != 'samples'}), flush=True)
        report = dict(status='PASS', slicer=slicer.app.applicationVersion, vtk=vtk.vtkVersion.GetVTKVersion(),
                      dll=str(config['dll']), dll_sha256=hashlib.sha256(Path(config['dll']).read_bytes()).hexdigest(),
                      capabilities=transport.dll._qarvtk_info, dataset=dict(dimensions=[96,96,96], spacing_mm=[1.,1.,1.]),
                      phases=phases)
        if config.get('exercise_cycles'):
            assert transport.dll._qarvtk_info.get('frame_abort') == 1
            source = hub.snapshot['source_peer']
            transport.set_receivers([])
            wait(lambda: 'Streaming is idle' in transport.status(), 'Empty pause')
            serial = transport.diagnostics.last_serial
            duration(.3)
            assert transport.diagnostics.last_serial == serial
            transport.set_receivers([peers[0]])
            wait(lambda: transport.diagnostics.last_serial > serial+2, 'Pause resume')
            transport.request_stop('Cycle regression')
            wait(lambda: not transport.running, 'Normal Stop')
            cycles = []
            for cycle in range(3):
                hosts = create_host_views(view, separate=True)
                transport = HostTransport(config['dll'], installed_sdk_path(), 'selected', output / ('cycle-'+str(cycle)),
                                          hosts, connection=hub, selected_peers=peers)
                wait(lambda: transport.diagnostics.last_serial >= 5, 'Restart frames')
                assert hub.snapshot['source_peer'] == source
                if cycle == 1:
                    color = transport.color
                    def broken(*args):
                        color(*args)
                        raise RuntimeError('injected failure after first GPU color copy')
                    transport.color = broken
                    wait_until = time.perf_counter()+15
                    try:
                        while time.perf_counter() < wait_until: pump()
                        raise AssertionError('GPU failure was not injected')
                    except RuntimeError as error:
                        assert 'injected failure' in str(error), str(error)
                    assert transport.stopping
                else:
                    transport.request_stop('Cycle completed')
                wait(lambda: not transport.running, 'Restart/failure teardown')
                cycles.append(dict(cycle=cycle, source_peer=hub.snapshot['source_peer'],
                                   frames=transport.diagnostics.last_serial, injected_failure=cycle == 1))
            report['lifecycle'] = dict(pause_resume=True, restart_and_failure_recovery=cycles)
            print('PASS: same-source pause/resume, Stop/Start and recovery after a partial GPU copy failure', flush=True)
        (output / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    finally:
        if transport:
            transport.request_stop('Controlled benchmark completed')
            wait(lambda: not transport.running, 'Render teardown', 30)
        else:
            for host in hosts: host.close()
        if hub:
            hub.request_stop()
            wait(lambda: not hub.running, 'Hub teardown', 30)


try:
    run()
except Exception:
    traceback.print_exc()
    slicer.app.exit(1)
else:
    print('PASS: controlled CT stereo benchmark, one/two receivers, VTK single-pass depth and completed cleanup', flush=True)
    slicer.app.exit(0)
