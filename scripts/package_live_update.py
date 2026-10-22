#!/usr/bin/env python3
"""Package the installed dev.3008 Live plugin and its Windows x64 bridge."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SDK_SHA = '55e6e3f48d940ca29331f0528fe475c1b7230fb19a59c31d5525c6e90b097f6c'
BUILD = 'installed-hub-update-r8.4'
NATIVE_BUILD = 'installed-hub-update-r8.4'


def package(dll, output, compiler):
    native = Path(dll).read_bytes()
    offset = struct.unpack_from('<I', native, 0x3c)[0]
    if (native[:2] != b'MZ' or native[offset:offset + 4] != b'PE\0\0' or
            struct.unpack_from('<H', native, offset + 4)[0] != 0x8664):
        raise ValueError('Expected a Windows x64 DLL')
    if ('"header_sha256":"' + SDK_SHA + '"').encode() not in native:
        raise ValueError('DLL does not contain the exact dev.3008 SDK fingerprint')
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
    if ('Host build: ' + NATIVE_BUILD + ' (ABI 5)').encode() not in native:
        raise ValueError('DLL does not match this Live update build')
    payload = {'Release/qarvtk-host.dll': native,
               'plugins/slicer/QarosLive.py': (ROOT / 'integrations/slicer/QarosLive.py').read_bytes(),
               'plugins/slicer/QarosVTKLib/__init__.py': b'"""Private support package, not a Slicer module."""\n'}
    for name in ('runtime', 'view', 'gpu', 'panels', 'diagnostics'):
        path = 'qarvtk_host_' + name + '.py'
        payload['plugins/slicer/QarosVTKLib/' + path] = (ROOT / 'integrations/slicer' / path).read_bytes()
    payload['plugins/slicer/QarosVTKLib/qarvtk_runtime.py'] = (ROOT / 'integrations/qarvtk_runtime.py').read_bytes()
    info = dict(sdk_version='0.9.0-dev.3008+65dc9aeb', sdk_header_sha256=SDK_SHA,
                host_abi=5, build=BUILD,
                native_build=NATIVE_BUILD, compiler=compiler,
                slicer_modules=['QarosLive'], retired_slicer_modules=['QarosVTK'],
                slice_previews_default=True, panel_probe=0, target_discovery=3,
                default_model_scale=1.0, model_scale_range=[0.125, 8.0],
                slice_panel_diagonal_inches=14.0,
                orientation_mode='fixed_ras', panel_size_on_start=1, panel_image_region=1,
                multi_target=1, default_target_mode='both',
                max_visualizer_targets=1, max_headset_targets=1,
                runtime_teardown='handle_only',
                panel_registration_snapshot='none',
                runtime_included=False, live_gpu_tested=False,
                files={name: hashlib.sha256(data).hexdigest() for name, data in payload.items()})
    payload['HOST_UPDATE_INFO.json'] = (json.dumps(info, indent=2) + '\n').encode()
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    archive_path = output / 'qarvtk-live-dev3008.zip'
    with zipfile.ZipFile(archive_path, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(payload.items()):
            entry = zipfile.ZipInfo(name, (2026, 10, 22, 0, 0, 0))
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
    parser.add_argument('--output', type=Path, default=ROOT / 'downloads')
    parser.add_argument('--compiler', required=True)
    args = parser.parse_args()
    package(args.dll, args.output, args.compiler)
