#!/usr/bin/env python3
"""Package the QAROS 0.10.0-rc.4.16 Live plugin and its Windows x64 bridge."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SDK_SHA = '231b63f8bd59e126ae199223e31c5efabf36eefbca36cd4ab5fb375991837a77'
BUILD = 'installed-hub-update-r8.14'
NATIVE_BUILD = 'installed-hub-update-r8.14'


def package(dll, qt_dll, output, compiler):
    native = Path(dll).read_bytes()
    offset = struct.unpack_from('<I', native, 0x3c)[0]
    if (native[:2] != b'MZ' or native[offset:offset + 4] != b'PE\0\0' or
            struct.unpack_from('<H', native, offset + 4)[0] != 0x8664):
        raise ValueError('Expected a Windows x64 DLL')
    if ('"header_sha256":"' + SDK_SHA + '"').encode() not in native:
        raise ValueError('DLL does not contain the QAROS 0.10.0-rc.4.16 SDK fingerprint')
    if b'"panel_probe":0' not in native:
        raise ValueError('DLL still provides the retired panel test mode')
    if b'"panel_size_on_start":1' not in native:
        raise ValueError('DLL does not apply physical panel size after registration')
    if b'"panel_image_region":1' not in native:
        raise ValueError('DLL does not provide image-only GPU panel capture')
    if b'"target_discovery":3' not in native:
        raise ValueError("DLL does not provide updated target discovery")
    if b'"multi_target":1' not in native:
        raise ValueError('DLL does not provide Visualizer + headset streaming')
    if b'"runtime_handle_only":1' not in native:
        raise ValueError('DLL does not provide runtime handle-only cleanup')
    for capability in (b'"device_selection":1', b'"persistent_connection":1', b'"live_selection":1',
                       b'"independent_receiver_cycles":1', b'"frame_abort":1', b'"render_queue_depth_per_sender":1',
                       b'"stable_app_tag":"slicer.qaros.quaternar"', b'"max_selected_targets":8'):
        if capability not in native:
            raise ValueError('DLL lacks persistent connected-receiver selection')
    if ('Host build: ' + NATIVE_BUILD + ' (ABI 5)').encode() not in native:
        raise ValueError('DLL does not match this Live update build')
    if b'"panel_input":1' not in native:
        raise ValueError('DLL does not provide copied GUI panel input')
    if b'"panel_stream_rebind":1' not in native:
        raise ValueError('DLL does not rebind retained GUI panels to the current video stream')
    if b'"panel_pipeline_audience":1' not in native or b'"panel_drag_origin":1' not in native:
        raise ValueError('DLL lacks receiver/mixer input scope or original-contact drag input')
    if b'"panel_motion_coalescing":1' not in native:
        raise ValueError('DLL does not coalesce obsolete panel motion updates')
    qt_native = Path(qt_dll).read_bytes()
    if len(qt_native) < 64 or qt_native[:2] != b'MZ':
        raise ValueError('Expected a Windows Qt controls DLL')
    qt_offset = struct.unpack_from('<I', qt_native, 0x3c)[0]
    if (qt_native[qt_offset:qt_offset + 4] != b'PE\0\0' or
            struct.unpack_from('<H', qt_native, qt_offset + 4)[0] != 0x8664 or
            b'qarvtk_qt_controls_create' not in qt_native or b'qarvtk_qt_version' not in qt_native):
        raise ValueError('Expected a Windows x64 Qt slice controls DLL')
    for symbol in (b'qarvtk_qt_controls_attach_playback', b'qarvtk_qt_controls_add_slice', b'qarvtk_qt_controls_status'):
        if symbol not in qt_native:
            raise ValueError('Qt bridge lacks native sequence controls or view selection')
    payload = {'Release/qarvtk-host.dll': native,
               'Release/qarvtk-qt-controls.dll': qt_native,
               'licenses/qaros-qt-LICENSE.txt': (ROOT / 'external/qaros-qt/LICENSE').read_bytes(),
               'plugins/slicer/QarosLive.py': (ROOT / 'integrations/slicer/QarosLive.py').read_bytes(),
               'plugins/slicer/QarosVTKLib/__init__.py': b'"""Private support package, not a Slicer module."""\n'}
    for name in ('runtime', 'view', 'gpu', 'panels', 'controls', 'diagnostics'):
        path = 'qarvtk_host_' + name + '.py'
        payload['plugins/slicer/QarosVTKLib/' + path] = (ROOT / 'integrations/slicer' / path).read_bytes()
    payload['plugins/slicer/QarosVTKLib/qarvtk_runtime.py'] = (ROOT / 'integrations/qarvtk_runtime.py').read_bytes()
    info = dict(sdk_version='0.10.0-rc.4.16+793871a8', sdk_header_sha256=SDK_SHA,
                host_abi=5, build=BUILD, release_stage='candidate',
                native_build=NATIVE_BUILD, compiler=compiler,
                slicer_modules=['QarosLive'], retired_slicer_modules=['QarosVTK'],
                slice_previews_default=True, panel_probe=0, target_discovery=3,
                default_model_scale=1.0, model_scale_range=[0.125, 8.0],
                slice_panel_diagonal_inches=14.0,
                orientation_mode='fixed_ras', panel_size_on_start=1, panel_image_region=1,
                multi_target=1, default_target_mode='selected',
                device_selection=1, persistent_connection=1, live_selection=1,
                independent_receiver_cycles=1, frame_abort=1, render_queue_depth_per_sender=1,
                gpu_copy_mode='host_context_per_receiver_fence', stop_detaches_audience=1,
                performance_fps_gain_established=False,
                performance_report='docs/SLICER_RENDER_PERFORMANCE.md',
                receiver_creation='visualizer_mcp',
                qt_slice_controls=1, qt_playback_controls=1, qt_controls_event_driven=1, qt_version='5.15.2',
                panel_stream_rebind=1, panel_pipeline_audience=1, panel_drag_origin=1,
                panel_motion_coalescing=1, qt_seek_on_release=1,
                stable_app_tag='slicer.qaros.quaternar', max_selected_targets=8,
                max_visualizer_targets=8, max_headset_targets=8,
                runtime_teardown='handle_only',
                panel_registration_snapshot='audience_only_on_start',
                runtime_included=False, live_gpu_tested=True,
                live_gpu_test_scope='ct_two_visualizers_live_selection_slice_video_qt_controls_bidirectional', live_headset_tested=False,
                files={name: hashlib.sha256(data).hexdigest() for name, data in payload.items()})
    payload['HOST_UPDATE_INFO.json'] = (json.dumps(info, indent=2) + '\n').encode()
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    archive_path = output / 'qarvtk-live-0.10.0-rc.4.16.zip'
    with zipfile.ZipFile(archive_path, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(payload.items()):
            entry = zipfile.ZipInfo(name, (2026, 11, 20, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    with zipfile.ZipFile(archive_path) as archive:
        if archive.testzip():
            raise ValueError('ZIP CRC verification failed')
    digest = hashlib.sha256(archive_path.read_bytes()).hexdigest()
    archive_path.with_suffix('.zip.sha256').write_text(digest + '  ' + archive_path.name + '\n')
    print(str(archive_path) + ': ' + digest)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--qt-dll', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'downloads')
    parser.add_argument('--compiler', required=True)
    args = parser.parse_args()
    package(args.dll, args.qt_dll, args.output, args.compiler)
