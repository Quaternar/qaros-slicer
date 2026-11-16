# qaros-slicer

Your [3D Slicer](https://www.slicer.org) scene in the room, in AR glasses, streamed live.

`qaros-slicer` is a Slicer module, **QAROS → QAROS Live Renderer** (`QarosLive.py`), and a small native host bridge (`qarvtk-host.dll`). Together they stream Slicer's own 3D view to [QAROS](https://www.quaternar.com/), QuaternAR's AR streaming runtime. The image comes from Slicer's own renderer: QAROS supplies the eye poses and carries stereo color, depth and slice previews. The scene is not exported to a second renderer, and the bridge adds no VTK or Qt of its own to Slicer.

- **Live stereo 3D.** Each receiver gets its own stereo stream of the first 3D view, as color plus depth. Transfer functions, ROI cropping and real data transforms stay live.
- **One workspace per volume.** Each visible volume can have its own movable QAROS workspace. Turn off **Separate QAROS workspace for each volume** to stream the whole scene as one.
- **Slice views as panels.** Visible slice views open as image-only GUI panels next to the model.
- **Several receivers.** Stream to several headsets and Visualizers at once, up to eight, and add or remove them while streaming.

The old QAROS Streaming module (`QarosVTK.py`) was removed on 2026-10-22. ANARI remains a separate alternative: this package does not install it, and Live capture does not support it.

## Requirements

- Windows x64 with 3D Slicer. The reference version is Slicer 5.12.4 (VTK 9.6.2).
- A Slicer 3D view that uses the supported host pipeline: VTK GPU ray casting with Composite blending for CT and MR, and no SSAO, shadows or custom render pass.
- A QAROS build whose C API header matches the bridge exactly. The bridge checks the SHA-256 of `include/qar_streaming.h` in the QAROS folder:

| Bridge built from | QAROS it runs with |
| --- | --- |
| `main` with the `external/qaros` submodule | QAROS 0.10.0 (`staging/main-with-headset`), header `b5528bc4` |
| `downloads/qarvtk-live-dev3184.zip`, installed by `UPDATE-LIVE-PLUGIN.cmd` | QAROS 0.9.0-dev.3184+2ccfb349, header `f3bbddfd` |

- QAROS and the headset player apps must run the same version. After an update, the Hub may ask you to approve the app or pair the device again.
- To build: Visual Studio 2022 or later with the C++ workload, CMake 3.20 or later, Python 3 for the tests. You do not need to build Slicer, VTK, Qt or vcpkg.

Detailed steps (Czech): [docs/INSTALLATION.md](docs/INSTALLATION.md).

## Install

### Prebuilt package (QAROS 0.9.0-dev.3184)

1. Stop streaming and close Slicer.
2. Update this checkout (`git pull`, or **Fetch origin → Pull origin** in GitHub Desktop).
3. Run **`UPDATE-LIVE-PLUGIN.cmd`**. It also works for the first install.
4. On the first install, add the folder `package/windows-start/plugins/slicer` from this checkout to **Edit → Application Settings → Modules → Additional module paths**, then restart Slicer.
5. Open **QAROS → QAROS Live Renderer**. The module must show **Live plugin r8.8**.

What the updater does:

- It installs package **r8.8** for the complete QAROS **0.9.0-dev.3184+2ccfb349**. It finds QAROS through the Windows registry, usually `C:\Program Files\Quaternar\QAROS`.
- It checks the SDK and the ZIP contents and backs up the original files.
- It removes the old module, its export helpers and the Python caches from the folders it manages. It does not search other folders: if you copied a plugin by hand somewhere else, remove that path from Additional module paths.
- The path `package/windows-start` stays, so existing Slicer settings keep working. The old `START-PREVIEW.cmd` only forwards to this installer. The cube preview and the standalone sender are not needed for Slicer.
- A pull alone does not update the installed files: run the installer and restart Slicer.

The package does not run with QAROS 0.10.0; for that, build from source (below).

More: [Windows with GitHub Desktop](docs/WINDOWS_DESKTOP.md) · [short guide](docs/WINDOWS_LIVE_QUICKSTART.txt).

### From source (QAROS 0.10.0)

1. Build the bridge (see [Build](#build)).
2. Package the module: `py -3 scripts/package_plugins.py`. It writes `package/QAROS-VTK-plugins/slicer`.
3. Add that folder to Slicer's **Additional module paths** and restart Slicer.
4. In **QAROS Live Renderer**, set **Host bridge DLL** to `build-host\Release\qarvtk-host.dll` and **QAROS SDK folder** to the QAROS folder that holds `include\qar_streaming.h` and `bin\qar-streaming-c.dll`.

## Usage

With QAROS running on the same PC:

1. Load data and show a volume in Slicer's first 3D view.
2. Open **QAROS → QAROS Live Renderer**. The module connects to the Hub with the app tag `slicer.qaros.quaternar`. On the first connection, QAROS asks a person at the PC to allow Slicer. Answer within two minutes, or the Hub refuses the app and you connect again.
3. Under **Stream to**, check one or more connected receivers, then press **Start**.

- **Stream to** lists the receivers with their connection state. Start receivers and connect headsets in QAROS first.
  - Up to eight in total, including several headsets of the same type and several Visualizers.
  - A disconnected receiver cannot be checked and does not block the other checked receivers.
  - Checkboxes work while streaming: unchecking all pauses delivery, checking one resumes it.
- **New Visualizer receiver** creates and starts a new receiver, even when the list is empty. The QAROS Visualizer must be running with its MCP endpoint enabled in **Settings → General**. Check the new receiver in the list yourself.
- **Scale.** The default is **1:1** (1 Slicer mm = 1 physical mm). Centering keeps the size. **Model size (at Start)** goes from 1/8× to 8×. Render resolution is set separately (100%, 75%, 50%).
- **Orientation.** The Slicer camera does not rotate the data in QAROS, not even at Start. Rotate the model with its workspace in QAROS.
- **Slice panels** start with a **14-inch** diagonal and the aspect ratio of the actual image area. GPU cropping removes the empty margin around the image and keeps black parts of the data. The data names go to the window title. Slice corner annotations are hidden in Slicer while streaming and come back after Stop. Resize a panel in QAROS.
- **Stop** ends the render streams and keeps the Hub connection and the app tag. **Disconnect** also closes the connection. Wait for Stop to finish before the next Start.
- Logs: the module shows its log folder; `COLLECT-LOGS.cmd` zips the logs and related Windows crash reports.

More: [headsets](docs/HEADSET_STREAMING.md) · [multiple volumes](docs/MULTIPLE_VOLUMES.md) · [FPS diagnostics](docs/FPS_DIAGNOSTICS.md) · [render performance](docs/SLICER_RENDER_PERFORMANCE.md).

## Build

```powershell
git clone --recurse-submodules https://github.com/Quaternar/qaros-slicer; cd qaros-slicer
cmake -S . -B build-host -A x64 -DQARVTK_BUILD_RENDERER=OFF -DQARVTK_BUILD_HOST_BRIDGE=ON
cmake --build build-host --config Release --parallel 4
ctest --test-dir build-host -C Release --output-on-failure
```

- The result is `build-host\Release\qarvtk-host.dll`.
- The C API header comes from the `external/qaros` submodule ([Quaternar/qaros](https://github.com/Quaternar/qaros), branch `staging/main-with-headset`, QAROS 0.10.0). In an existing clone, run `git submodule update --init`.
- `-DQAROS_SDK_ROOT=<folder>` builds against another header instead: a QAROS folder with `include/` and `bin/`, or one of the pinned headers in `ci/sdk-headers/`. With a `bin/qar-streaming-c.dll` there, the tests also load that DLL.
- The full header hash selects the API. Unknown headers are rejected, and each DLL embeds the hash it was built for. See [SDK compatibility](docs/SDK_COMPATIBILITY.md).
- If CMake finds no real Python, add `-DPython3_EXECUTABLE=<path>`.

The CMake extension in `integrations/slicer` registers only `QarosLive`. More: [local Windows build](docs/LOCAL_WINDOWS_BUILD.md) (Czech) · [Windows development](docs/WINDOWS.md).

## How it works

| Directory | What it holds |
| --- | --- |
| [`integrations/slicer`](integrations/slicer) | The Slicer module `QarosLive` and its private libraries: host view, slice panels, GPU depth, runtime and diagnostics |
| [`src`](src), [`include/qarvtk`](include/qarvtk), [`apps`](apps) | The VTK-free host bridge (`qarvtk-host.dll`, ABI 5), the shared core, and the standalone preview and sender |
| [`external/qaros`](https://github.com/Quaternar/qaros) | Git submodule: the QAROS C API header `qar-streaming-c/include/qar_streaming.h` |
| [`ci/sdk-headers`](ci/sdk-headers) | The exact headers of older QAROS builds, for builds and tests only |
| [`downloads`](downloads) | The prebuilt Live packages the updater installs, and historical ZIPs |
| [`scripts`](scripts) | Packaging, the Windows updater and the log collector |
| [`tests`](tests) | C++ and Python tests; the Slicer smoke tests run in CI |
| [`docs`](docs) | Architecture, validation history, SDK compatibility, update guides |

- The repository holds no SDK runtime DLLs, invites or pairing identities.
- Do not copy the headers in `ci/sdk-headers/` into an installed QAROS.
- Old combined install ZIPs remain only as historical artifacts. The current installer does not use them, and publishing or repackaging them is disabled because they contain the removed module. For Slicer, use only the current Live package.
- The standalone VTK backend serves the remaining ParaView prototype and development tests. It is not part of the Slicer install.

Details: [architecture and decisions](docs/ARCHITECTURE.md) · [validation history](docs/VALIDATION.md).

## ParaView

- A direct ParaView Live plugin is in progress on branch `codex/paraview-live`, [draft PR #4](https://github.com/HonzaHovora/QAROS_VTK/pull/4) in the source repository. It still needs testing on Windows with QAROS.
- The earlier ParaView export prototype and the shared development tools stay separate from the Slicer install. See [host integrations](docs/HOST_INTEGRATIONS.md).

## Known issues

- **GUI window texts.** Reported 2026-10-22: the texts on the QAROS GUI windows do not work. Not yet diagnosed.
- **QAROS GUI crash when panels open.** Confirmed on the QAROS side (`qar-stereokit-gui`, `WebCore.dll`); the fix comes from QAROS, not from this plugin.
- **Reconnecting after Stop/Start** still needs checking on current QAROS builds.

Details and where to start in the code: [HANDOVER.md](HANDOVER.md) (Czech).

## Versioning

This repository follows the shared QAROS versioning scheme: [ADR-0001: One versioning scheme for every QAROS repository](https://github.com/Quaternar/qaros/blob/main/docs/adr/0001-one-versioning-scheme.md), with its rules for app repositories, [ADR-0002: QAROS apps release on every merge that changes behaviour](https://github.com/Quaternar/qaros/blob/main/docs/adr/0002-app-release-on-merge.md).

- [`VERSION`](VERSION) holds the version of the last release, `0.0.0` before the first one. CMake reads it for `project()`. Never edit it by hand.
- Every merge to `main` that changes behaviour is a release. CI (`.github/workflows/app-release.yml`) writes the new version into `VERSION`, moves the `Unreleased` entries of [CHANGELOG.md](CHANGELOG.md) under it, commits `chore(release): vX.Y.Z`, tags `vX.Y.Z` and publishes a GitHub release with that changelog section.
- The pull request decides the bump. A label wins; without one, the title, a [Conventional Commit](https://www.conventionalcommits.org/), decides:

  | Label | Title | Bump |
  |---|---|---|
  | `release:major` | | major |
  | | `feat!: ...` or `BREAKING` in the title | major, minor below 1.0.0 |
  | `release:minor` | `feat: ...` | minor |
  | `release:patch` | `fix: ...`, `perf: ...`, `revert: ...` | patch |
  | `release:none` | `docs`, `ci`, `chore`, `test`, `refactor`, `style`, `build` | no release |

- The **Release bump** check on each pull request fails when neither defines a bump, and shows the version merging releases.
- Add the changelog entry under `Unreleased` in the pull request. The release is at least what its entries need: `Breaking` or `Removed` a breaking bump, `Features` or `Enhancements` a minor, `Bug fixes` a patch.
- To merge without releasing, label the pull request `release:none`; the next releasing merge ships it.
- The Slicer module keeps its own build label (`LIVE_VERSION`, `r8.8` in `integrations/slicer/QarosLive.py`) for now.

## License

MIT, see [LICENSE](LICENSE). The OpenGL declarations in `include/qarvtk/host_gl.hpp`, taken from the Khronos OpenGL Registry, are MIT licensed by The Khronos Group, see `third_party/KHRONOS-LICENSE.txt`.
