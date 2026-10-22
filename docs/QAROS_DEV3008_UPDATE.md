# Slicer Live r8.4 with QAROS dev.3008

This update targets the complete **QAROS 0.9.0-dev.3008+65dc9aeb** installation
supplied in `QAROS.zip` on 2026-10-22. Its changelog identifies the build.
The default installation is `C:\Program Files\Quaternar\QAROS`; the updater
reads the registered installation path first.

## Install

1. Stop streaming and close Slicer completely.
2. Fetch/Pull `main`, then run `UPDATE-LIVE-PLUGIN.cmd`.
3. Open Slicer and check **Live plugin r8.4** in QAROS Live Renderer.
   Hover over the version to check the loaded Python module path.
4. Use the installed QAROS and headset player from the same version. The new
   QAROS changelog describes security-store and certificate migrations that
   can require approving Slicer or pairing the headset again; follow the Hub
   prompt if it appears.
5. Start one Visualizer receiver and/or connect a headset, select **Stream to**,
   then start streaming. Existing Slicer module paths remain valid.

The updater installs `downloads/qarvtk-live-dev3008.zip`, checks its checksum,
SDK fingerprint and every payload file, and backs up the previous plugin in
`package/live-backup-*`. It also supports a fresh installation. It does not
install QAROS or copy runtime DLLs into Slicer. Keep the QAROS installation
intact; copying just a header between versions is not an upgrade.

## SDK changes used here

- The native bridge is rebuilt against the exact supplied header. New SDK
  functions are never loaded by a bridge compiled for dev.2927.
- Target discovery uses `qar_peer_spec_get_presence`: `Online` and `Unknown`
  peers are eligible; `Offline`, `Leaving` and `Retired` are excluded. Unknown
  future values are treated as `Unknown`, as the header requires. A stale
  `Running` application state no longer selects an offline receiver.
- Role and category still have to match. A Visualizer control peer, camera
  or observer is not a headset or ordinary Visualizer receiver. More than one
  eligible target of the requested category remains an explicit ambiguity.
- `qar_runtime_destroy` was removed from the SDK and its DLL exports. Cleanup
  already uses `qar_runtime_handle_destroy`; this build's dynamic loader no
  longer asks the runtime for the removed symbol.
- Local-app Hub enrollment, ASCII common names, one render sender per target
  and app volume, and handle ownership retain the existing implementation.
  These were compared with the official
  [Vulkan example at 83d808c9](https://github.com/Quaternar/qaros/blob/83d808c9e89aa4596803a260c0c8d583b5520fe7/qar-streaming-c/examples/vulkan_cube/main.cpp).

Discovery is not a receiver-readiness signal: sender negotiation still decides
whether a stream can be created. A selected peer remains pinned until Stop;
restart streaming after replacing a receiver. This update does not add automatic
switching to another peer. On 2026-10-22 Jan reported that Lukas confirmed the
GUI fault is in QAROS and is working on its fix. The plugin keeps panels enabled
normally; r8.4 is the SDK compatibility update, not that upstream GUI fix.
The separate Stop/Start connection behavior still needs a field check.

The existing fixed RAS orientation, 1:1 model scale and image-only 14-inch
panels are retained. Data names remain in panel titles; corner annotations are
hidden in Slicer while streaming and restored on Stop.

## Identifying the build

- Plugin: `r8.4`
- Native log: `Host build: installed-hub-update-r8.4 (ABI 5)`
- Native capabilities: `target_discovery=3`, `multi_target=1`,
  `runtime_handle_only=1`, `panel_size_on_start=1`, `panel_image_region=1`
- `PEER_OBSERVED` logs `presence=Online/Unknown/Offline/Leaving/Retired` and
  eligibility. Older supported compile fixtures still use their AppState API.
- Header SHA256:
  `55e6e3f48d940ca29331f0528fe475c1b7230fb19a59c31d5525c6e90b097f6c`
- Supplied `bin/qar-streaming-c.dll` SHA256, for provenance only:
  `2668fb59444e5ca4d0cca317c1f5068d13192a40668d81cb34dc52b50aae8609`

The runtime DLL is not redistributed. The bridge ABI remains 5; a new native
DLL is nevertheless required because the SDK's dynamic symbol table changed.
See [validation](VALIDATION.md) for what was tested locally and what still
requires Windows, Slicer and the headset.
