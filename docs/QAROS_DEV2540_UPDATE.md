> Historical SDK migration record. For the current Slicer installation use
> [INSTALLATION.md](INSTALLATION.md). The separate-renderer Slicer module and
> the r17 starter were retired on 2026-10-22.

# Slicer Live update for QAROS 0.9.0-dev.2540+1ed038e4

Historical dev.2540 notes. The actual installed QAROS supplied afterwards is
dev.2927 at `C:\Program Files\Quaternar\QAROS`; use
[QAROS_DEV2927_UPDATE.md](QAROS_DEV2927_UPDATE.md) and the current updater for it.
The archived `qarvtk-live-dev2540.zip` remains available for the older SDK.
The steps below describe the updater at commit `2526ae4`, not the current one.

This update starts from main `65914b6` with independent workspaces. It replaces
the Live bridge and Python module together. QAROS itself stays in its complete
installation folder, normally `D:\QAROS-0.9.0-dev.2540+1ed038e4`.

## Install on the existing Windows checkout

1. Close Slicer. In GitHub Desktop fetch and select `codex/slicer-qaros-dev2540`.
2. Run `UPDATE-LIVE-PLUGIN.cmd` in the `QAROS_VTK` checkout.
3. Open Slicer and **QAROS Live Renderer**. Check the SDK folder above and start
   live streaming. Separate workspaces and slice previews remain available.

The updater verifies the exact SDK header, runtime/launcher presence, archive
checksum and payload checksums before copying. It saves replaced files under
`package/live-backup-<timestamp>` and restores them if copying fails. It does
not edit Slicer's settings or the SDK. The Live module selects the new installed
SDK on first opening; it now saves its SDK choice separately from the older
standalone QAROS Streaming module.

For a different SDK location, run:

```powershell
.\UPDATE-LIVE-PLUGIN.cmd -SdkRoot 'E:\QAROS-0.9.0-dev.2540+1ed038e4'
```

Then select that root in the Live module. The root must contain both `include`
and `bin`. Do not select `bin` itself or copy the new header into `D:\install`.

`START-PREVIEW.cmd` still extracts the historical r17 package, so running it
after this update restores the old Live plugin. Reapply this updater afterwards.
To restore the immediately preceding multi-volume plugin, close Slicer and copy
the contents of the reported backup folder over `package/windows-start`, then
select the previous SDK and matching host DLL. Keep the backup until the new
runtime has passed the real GPU test.

## Why a rebuild is required

The supplied header SHA256 is
`c709c479cc6a1acc0040fb61a071cc7bc16e65bf5acb90d624d9d6b0a6fcd3c1`.
It still uses the session invite API, but the old `QarStreamParamsOpenGl`
has been replaced by platform-specific structures. Windows now uses
`QarStreamParamsOpenGlWgl` with nested `QarOpenGlContextCallbacks` and no
`unbind` field. Both volume and slice-video senders use the new structure;
restoring an empty binding still explicitly calls `wglMakeCurrent(NULL, NULL)`.
The old and new SDK layouts are compiled separately, never cast or patched.

The new SDK also replaces gesture APIs and changes default gesture mappings.
The plugin does not subscribe to these gesture APIs; interactions continue to
use the SDK defaults. Slice panels remain preview-only. The dynamic loader is
compiled against the complete new symbol table, including the renamed symbols.
The bridge ABI between Python and our DLL remains version 5.

## Build and verification

`scripts/build_host_cross.sh` defaults to the exact new header. It can still
build for the previous header when `QAROS_SDK_ROOT=ci/sdk-headers/session-022c40b6`
is explicitly set. CMake also supports both exact headers. The Python boundary
accepts either supported pair but rejects mixing a DLL and a different SDK.

The downloadable update is built with MinGW-w64 GCC 13 (POSIX), with its compiler
runtime linked statically. It includes no QAROS, VTK or Qt runtime binaries.
The `Live host SDK compatibility` Windows CI builds both SDK variants with MSVC,
checks callback context restoration, runs the DLL ABI tests, and exercises the
PowerShell updater and the shipped cross-compiled DLL.

Compilation and tests without a QAROS runtime do not verify NVIDIA interop,
viewer negotiation or frame rates. The remaining Windows smoke is: stream one
volume, then two separate workspaces, check slice previews, stop and start again.
