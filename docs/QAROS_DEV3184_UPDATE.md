# Slicer Live r8.5 with QAROS dev.3184

This update targets the complete installed **QAROS 0.9.0-dev.3184+2ccfb349**
on Windows x64, inspected on 2026-10-30. The registry and installed changelog
report this version. The exact header is pinned in
`ci/sdk-headers/hub-f3bbddfd/include/qar_streaming.h` with SHA-256
`f3bbddfdd4106a87bdcb114dc3a456b189b3f6a009a535e58592737d456a4f52`.

## Install

1. Stop streaming and close Slicer completely.
2. Update the checkout and run `UPDATE-LIVE-PLUGIN.cmd`.
3. Open Slicer and check **Live plugin r8.5** in QAROS Live Renderer.
4. Keep QAROS and headset players on the same version. This QAROS release
   changes app approvals and headset pairing; complete the Hub's prompts
   if requested. The plugin does not delete or replace QAROS identities.
5. Start a Visualizer receiver and/or connect a headset, choose **Stream to**,
   and start streaming. Existing Additional module paths remain valid.

The updater installs `downloads/qarvtk-live-dev3184.zip`. It verifies the
SDK fingerprint, archive checksum, ABI and payload hashes before replacing
files, and saves the previous plugin under `package/live-backup-*`.
It does not replace QAROS runtime files or copy them into Slicer.

## Compatibility change

The previous r8.4 plugin was pinned to the dev.3008 header and rejected this
new SDK. Both the Python fingerprint registry and native DLL now support the
dev.3184 header; the shipped DLL is compiled against these exact bytes.
Unknown headers and mismatched DLL/header combinations remain rejected.

The new header adds local app tags and session lifecycle information and
changes the co-process invite API. This plugin uses local-app enrollment
directly, not co-process invites or application-minted pairing codes, so no
new invite path is introduced. Peer presence discovery, volume/panel common
names, stereo OpenGL transport, physical size and handle-only teardown retain
their existing behavior. Source compatibility with the four older pinned
headers is retained; every SDK still requires its matching native DLL.

The update does not claim to repair the previously reported QAROS GUI faults.
See [VALIDATION.md](VALIDATION.md) for actual build and runtime checks.
