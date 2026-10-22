# Validation and next work

## Slicer Live r8.4: supplied QAROS dev.3008 SDK, 2026-10-22

The user supplied the full `QAROS.zip` installation. Its changelog identifies
`0.9.0-dev.3008+65dc9aeb`; the unmodified header is pinned as `hub-55e6e3f4`.
The current Windows update ZIP is `downloads/qarvtk-live-dev3008.zip`.

Completed locally:

- The supplied ZIP passed CRC checks. The header's 199 dynamically loaded
  symbols were compared with the exports of its `bin/qar-streaming-c.dll`;
  none is missing. `peer_spec_get_presence` is present and the removed
  `runtime_destroy` export is absent. This is a static compatibility check,
  not a Windows runtime launch.
- The Windows x64 native bridge compiled against all four pinned headers
  with MinGW-w64 GCC 13.2.0-posix. The shipped build uses dev.3008 and reports
  ABI 5, `target_discovery=3`, `multi_target=1` and the retained panel features.
  Only standard Windows DLL imports remain; the C++ runtime is linked statically.
- All 13 CTest suites passed, including the new presence fixture, existing
  startup/cancellation/ownership tests, 17 Python host-runtime cases and panel
  sizing with the new header. Presence cases cover online/unknown/future values,
  offline peers with stale Running state, leaving/retired peers, category
  filtering, stale snapshot races and a receiver returning under the same id.
  The dev.2927 fallback is tested separately with its own header.
- The package CRC, SDK fingerprint, native capability metadata, archive SHA256
  and all ten entries were checked. The packaged DLL and Python files match
  the current source/build. No QAROS runtime DLL is redistributed.
- The updater passed fresh installation, old-module removal, preservation of
  unrelated content, backup, injected-failure rollback and SDK-mismatch rejection
  under PowerShell 7.4.6 on Linux. Native Windows DLL loading is explicitly
  skipped there; the Windows CI fixture covers it when a runner is available.
- Changed Python files passed compilation checks. The panel crop/shaders,
  placement math and frame-copy code are unchanged from r8.3.

Not exercised here: Windows SDK onboarding, NVIDIA transport, Slicer Qt UI or
physical HoloLens output. The required field check is r8.4 with the complete
dev.3008 installation and matching headset player: Visualizer-only,
headset-only, combined two-volume streaming with panels, then repeated Stop/Start.
On 2026-10-22 Jan reported that Lukas confirmed the GUI fault is in QAROS and
is working on the fix. This package retains normal panel startup; it does not
contain that upstream fix. Receiver restart failures need a separate field check.

The selected target remains pinned until Stop, as before. Presence changes
selection and diagnostics; it does not add automatic peer replacement or change
the SDK sender's existing automatic reconnect behavior.

## Slicer Live r8.3: stable orientation and compact panels, 2026-10-22

The desktop camera no longer initializes the model's orientation. Fixed RAS
axes apply before and during streaming; actual data transforms remain live.
R8.1/r8.2's 14-inch calculation was present in both main and the ZIP. The native
registration path only passed that size through `get_or_create` init, which
does not necessarily update a retained panel. R8.3 explicitly applies size
after registration. This closes a demonstrated API-contract gap; it does not
establish the cause of the user's physical-size observation without a live run.

Panels now crop the source texture to geometry-derived image bounds. Data names
are in `display_name`. Source corner annotations are hidden for the lifetime of
the preview and restored on close, including errors; no persistent setting is
changed. The first capture waits for Slicer's normal clean frame. The crop uses
no brightness threshold, readback or substitute renderer. Nonlinear transforms
and lightbox layouts retain the full viewport to avoid cutting valid data.

Local validation:

- All 12 CTest suites pass, including a new SDK fixture returning a retained
  panel with stale size, explicit size application and registration/resize failures.
- Seven panel tests pass: geometry crop, DPI scaling, transformed bounds, safe
  fallback, data title, corner-label lifetime, physical diagonal across model
  sizes/aspects, compact initial rows and the existing scheduler/cleanup cases.
- VTK placement tests cover opposite and oblique desktop views, orbit/roll,
  restarts, data transforms, scale, centering and SDK stereo/head poses.
- The production C++ GPU copy runs on Mesa EGL/llvmpipe: 16 rectangle/full-frame
  copies cover 2D/array targets, SDK flips, black pixels, interpolation at crop
  edges and rejection of invalid bounds. A further check preserves letterboxing.
  Native GPU regression tests and the real-Slicer smoke fixture are updated.
- The Windows x64 DLL is cross-built with MinGW-w64 GCC 13.2.0-posix against the
  unchanged dev.2927 header. Core tests include Python transport/diagnostics;
  packaging tests and Python compilation pass.
- ZIP CRC, payload hashes and source/binary identity checks pass. The isolated
  PowerShell updater fixture passes fresh install, upgrade/backup, removal of
  retired files, rollback and rejection of a mismatched SDK on Linux.

Real Slicer GUI/Qt rendering, native DLL loading on Windows, QAROS registration,
NVIDIA encoding and physical headset appearance/size are not exercised locally.
The field check is: install with Slicer closed, confirm **Live plugin r8.3**,
restart after different camera orbits, check a square animated image and 14-inch
diagonal, manually resize, then Stop and verify slice annotations return.
Remaining GUI crashes on receiver reuse are not claimed resolved.

## Slicer Live r8.2: retire the separate renderer, 2026-10-22

QarosLive is now the only Slicer module in source registration and generated
packages. QarosVTK, its Slicer scene exporter and its obsolete smoke test are
removed. Old combined starter archives and their publish/store definitions are
retained as historical artifacts. Publishing jobs and the combined packager are
disabled; the active installer never extracts those archives. The standalone ParaView prototype
and shared rendering test code are outside this Slicer change.

Local validation:

- Two packaging tests pass: a fresh ZIP and repeated upgrades remove the old
  module, export helpers and import caches while preserving unrelated files.
- PowerShell 7.4.6 on Linux passes the isolated updater fixture: fresh install,
  payload hashes, both managed install paths, backups, removal, rollback after
  an injected failure during retirement, and rejection of a mismatched SDK.
- Existing host runtime (15 tests), diagnostics (4 pass, 1 Windows-only skip),
  view/placement math and panel (3 tests) checks pass. Python compilation and
  diff whitespace checks pass.
- The r8.2 ZIP contains only QarosLive and its support files. All payload hashes
  match. The native host DLL is byte-identical to the shipped r8.1 DLL.

Windows PowerShell 5.1/native DLL loading and actual Slicer GUI/GPU streaming
were not rerun locally for this cleanup; the existing Windows and Slicer CI
jobs retain those checks, with live-module discovery replacing exporter smoke.
ANARI integration is unchanged and no new ANARI capture support is claimed.

## Installed Hub Live r8.1: 14-inch panels, 2026-10-21

The user reported that r8 works, including headset streaming. This is a
functional field report, not a measured performance or long-duration result.
R8.1 replaces the 45 cm panel bounding square with a consistent 14-inch
diagonal. Initial aspect ratio is preserved and model scale does not affect
panel size. Size is set only at creation; QAROS user resizing is not overwritten.

The existing three panel tests and Python compilation pass. A creation smoke
check covers landscape, portrait, square and two pixel resolutions, verifying
the 35.56 cm diagonal and that normal updates do not recreate the panel.
ZIP payload/hash checks and isolated PowerShell install/backup/SDK rejection
pass. The shipped native DLL is byte-identical to r8; no native code changed.
The new default's appearance on the headset still needs a user check.

## Installed Hub Live r8: physical scale and target discovery, 2026-10-21

The manual panel startup mode and native video gate are removed. Centering
now preserves the default 1 mm = 1 mm scale; an explicit 1/8x–8x multiplier
applies equally to all workspaces at Start. The workspace fits rotated bounds.
Headset/combined modes require the current DLL's target-discovery capability.
The pinned SDK's Unknown/Initializing targets can negotiate a sender instead
of waiting exclusively for Running; terminal states remain excluded.

Local verification:

- 11/11 CTest suites pass, including target discovery, independent target
  startup/frame routing, cancellation and Python transport lifecycle tests.
- 3/3 remaining panel scheduler tests pass; obsolete manual-probe tests removed.
- VTK 9.3.1 placement math passes: 1:1 in centered/uncentered and oblique views,
  physical multipliers, clipping distances, invalid scale values and flat bounds.
- Windows x64 cross-builds pass against all three pinned SDK headers. The Live
  package uses the unchanged dev.2927 header; no newer SDK functions are loaded.
- ZIP CRC and payload hashes plus the isolated PowerShell installation,
  backup and SDK-mismatch rejection checks pass. Python sources compile.

Full Slicer/Qt rendering was not run in this environment. The attempted EGL
multi-volume smoke test could not start because its VTK 9.3.1 build lacks
`vtkEGLRenderWindow`. Native Windows DLL loading, encoder output, HoloLens
display/physical size and comparative performance remain unverified here.
No current log establishes the cause of the user's blank headset. The target
selection adjustment and new discovery messages are not proof of resolution.
Remaining QAROS GUI crashes on Stop/Start are not claimed fixed by r8.

## Live r17: read-only slice/video GUI panels, 2026-10-15

Visible Slicer slice windows feed their completed GPU color textures to separate
mono SDK video senders and GUI panels. EndEvent revisions and bounded staging
retain the latest frame under backpressure; no queued video history accumulates.
There are no extra renders or CPU readbacks in the production preview path.
Capture is capped at 768 pixels on the longer side and 5/10/15/30 FPS per panel,
default 15. Panels sit below the workspace, preserve aspect ratio, hide with
their source views and close on Stop. They have no interaction or Qt controls.
Panel SDK work has a separate worker; failures do not stop stereo streaming.
GPU copies, synchronization and additional encoders still have a cost.

Accepted production source: `c471ee4e8da86fe5f2ff84a4450b91fd27b44b63`.
[Build/test 38785407260](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38785407260)
passed, including the mono GPU copy test for 2D/array targets, SDK flips,
letterboxing, exact orientation and GL state restoration. Local scheduler tests
cover throttling, skipped busy captures, newest-frame replacement, hidden views,
context replacement and cleanup. Runtime tests keep stereo alive after preview
failure. Windows native
[38785407270](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38785407270)
passed all ten host/core tests, including DLL loading, ABI 4 exports and WGL
callbacks. Placement/Eye/Frame remain 112/104/232 bytes; old ABI 3 is rejected.
Both release artifacts are from the accepted production commit.

Test-only follow-up `070219a88fde3788d7a8a33adb40a65d7a7fc751` passed
[Slicer 38785978619](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38785978619)
and [Build/test 38785978734](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38785978734).
In actual Slicer 5.12.4 / VTK 9.6.2, Red/Yellow/Green previews match their native
GPU textures, preserve desktop pixels and issue no extra renders. Animated CT
data proves video/sequence frames advance and coalesce during receiver stalls;
resumed capture matches the current native image. Layout hide/restore and Stop
remove the expected visibility/observer state. All existing stereo, CT depth,
ROI, world-bounds, fullscreen and desktop-preservation tests also pass.
The first preview test compared motion-oracle arrays across a Qt splitter resize;
the follow-up compares consecutive equal-sized frames while separately checking
the newest capture exactly against the current native image. No production code
changed in that follow-up.

These Slicer tests use Mesa and a transport fixture. New panel creation, encoder
capacity, display and placement in the actual Windows SDK/Visualizer remain
to be checked on the target PC, including the user's video dataset. Compare
the same camera/preset with previews enabled and disabled. No FPS improvement
is claimed, and R16 workspace sizing still awaits explicit field confirmation.

The assembled starter has 76 verified manifest entries and byte-identical
accepted host plugin sources. Host DLL SHA-256:
`2fd97138f9ecf3fbe781a0c8e9e261849abeefb06e1fd2dbb1f931017b5bdeb6`.
Starter SHA-256:
`159a782957174d25da8f4494e3ecc29a1f56607ef2767c11032f7e8e8f1b617a`.

## Live r16: workspace dimensions from the Slicer scene, 2026-10-15

The former fixed 1.2 m cube is replaced with the visible scene bounds rotated
into workspace axes and multiplied by SDK `app_scale`. All eight AABB corners
are used, including oblique starting cameras. Center and fit makes the longest
edge 65 cm; physical mode retains 1 mm = 1 mm and the existing world placement,
with the workspace centred around the content. Degenerate axes have a 1 mm
minimum. Actor/data transforms and the render pipeline remain unchanged.
Placement and dimensions are fixed at Start; live ROI clipping does not shrink
the source world bounds. The UI shows width × height × depth in centimetres.

Host ABI 3 adds workspace XYZ size and room centre to Placement (112 bytes).
The SDK's struct order is X/Z/Y; named assignments and an asymmetric fixture
check width/length/height independently. Older ABI 2 DLLs are rejected by the
plugin before binding creation. The package must update Python and DLL together.

Accepted source: `00f48c9a8f318f03513413792fa6e0788528acce`.
[Build/test 38771695514](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38771695514)
and [Slicer 38771694756](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38771694756)
passed. Independent matrix checks cover anterior/posterior/oblique orientation,
physical placement, distant/hidden/UseBoundsOff props and flat geometry.
The real Slicer 5.12.4 / VTK 9.6.2 test verifies 32 voxels at 3 mm spacing give
93 mm world extents, and 65 cm fitted workspace extents. Existing stereo color,
CT depth, ROI, two-render accounting, desktop preservation and layout/fullscreen
checks pass with Mesa and a transport fixture. Actual new workspace sizing in
the Windows Visualizer still needs the target-PC check. No FPS improvement is
claimed; bounds calculation adds no per-frame work.

[Windows native 38771694919](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38771694919)
passed, including all ten host/core tests, real DLL ABI 3 loading and WGL sharing.
Both pinned release artifacts come from the same accepted source commit.
The starter has 75 verified manifest entries, byte-identical plugin sources,
and host DLL SHA-256 `cf9c31abaac79077d3c9130e62a9d31ce07bc2bc32599af6157ad691025b7089`.
Its embedded metadata reports Placement 112 / Eye 104 / Frame 232 bytes.
Starter SHA-256: `4a78f5c9acb2a688499eb13ff1cf7d38803b02a2acb01bbfc44f7853c3c9d219`.

## Live r14 field results and r15 layout fix, 2026-10-15

The three sessions in `QAROS-VTK-diagnostics-20261015-174009-305.zip`
average 10.78 FPS at full resolution, 14.94 at half resolution and 14.34
after restarting half resolution in fullscreen. Actual render size remains
960 × 540 in both half-resolution runs. SDK eye poses change substantially;
the later runs reach 86–87 °C and NVML reports software thermal slowdown.
These observations do not isolate focus as the cause of the slowdown.
See [FPS_DIAGNOSTICS.md](FPS_DIAGNOSTICS.md) for measured timings and limits.

All source sessions stop normally. The plugin's unconditional connection of
`layoutChanged(int)` to Stop explains a session closure when maximizing a view;
there is no viewer log to establish or rule out an additional viewer crash.
R15 keeps the session when the same visible first 3D view, renderer, camera
and OpenGL context survive. Source removal/replacement/context recreation
still requires restarting. The source/context guard runs before frame capture.
The first stop reason is retained in the module status and `STOP_REQUEST`.

Accepted production/plugin source: `46bf78ce91f60f0591125caeb380e7eee6363881`.
[Build/test 38764857884](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38764857884)
passed, including Windows runtime/control-flow tests. The test-only follow-up
`1ca772db05db8dadafbe6eeebba80fa1d694e3bf` passed
[Slicer 38765419509](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38765419509).
It uses the connected layout signal for four-up/one-up, 3D maximize/restore and
application fullscreen/normal. After each transition it verifies two renders,
unchanged desktop pixels, current native pipeline color and independent VTK
tissue depth. Hiding the source stops with a reason; a changed context stamp
is rejected. A pre/post-layout image-equality assertion was replaced by current
native color/depth comparison because layout changes can update MRML props.
The earlier API spelling error was caught by Slicer and corrected to
`IsViewVisibleInLayout` before the accepted tests/package.

The Slicer test uses Mesa and a transport fixture. The user subsequently
confirmed fullscreen/restore on the target Windows SDK/viewer installation.
Before this confirmation startup rejected the actual CPU mapper
`vtkFixedPointVolumeRayCastMapper` / `vtkMRMLCPURayCastVolumeRenderingDisplayNode`.
Calling `ChangeVolumeRenderingMethod("vtkMRMLGPURayCastVolumeRenderingDisplayNode")`
restored streaming; the user confirmed it helped. Why CPU rendering became
active is unknown, so this is not evidence of a proven r15 renderer regression.
R15 does not claim an FPS fix.
Native DLL remains the accepted r13 binary (`821fdf85…`), ABI 2. The assembled
starter has 75 verified manifest entries, byte-identical accepted plugin sources,
and SHA-256 `e1e27028e8f303f75f30d5e51aa5749d0319daef6f45a9a503ffd1141d3cd7a9`.

## Live r13 field results and r14 diagnostics, 2026-10-15

The user now confirms correct orientation. The latest r13 logs confirm two
eye renders, half-sized depth and 50% actual color render resolution throughout.
In the latest run source throughput first reached 19–19.5 FPS, then declined to
about 13 FPS while both-eye Render wall time grew from about 30 to 48 ms.
Copy costs stayed around 3–4 ms. See [FPS_DIAGNOSTICS.md](FPS_DIAGNOSTICS.md)
for exact session identification, measurement limits and the r14 experiment.
R14 changes diagnostics only and retains the accepted r13 native DLL.
Accepted diagnostic/plugin source: `760df92fe350c36cddc6a10e07fe143534cb1e86`.
[Slicer 38759400135](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38759400135)
and [build/test 38759400030](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38759400030)
passed. Windows tests include real foreground-process API calls; NVML behavior
is tested with a fixture, not a physical NVIDIA GPU. Slicer confirms the added
read-only sampling and continues to pass color/depth, ROI and desktop restoration.
Actual NVIDIA telemetry availability/overhead remains a field check.
R14 starter SHA-256: `f350d4a77458c59fe09a03a7e1ebfb0ffeac2dea8bd47d6a86a4955cac2ce91a`.
All 75 manifest entries, packaged host Python sources and the unchanged r13
host DLL hash were verified before publication.


## Live r13: SDK placement and separate depth resolution, 2026-10-15

The user reports that r12 improved source FPS but still slows Slicer, and that
Visualizer X/Z controls and the starting view appear reversed. At the time of this implementation, the last uploaded
field measurement was r11 (1920x1080 per eye); the newer field evidence above
supersedes this initial status. R12 code still rendered at the full viewer dimensions with full image
sampling. Its initial fixed RAS mapping selected a posterior view and did not
adopt the desktop camera direction.

R13 keeps native Slicer RAS coordinates throughout rendering. The initial camera
basis and bounds are represented solely by SDK `app_pose` and `app_scale`.
This includes millimetre conversion: without fit `app_scale=.001`; with fit it
is `.65 / max_extent_mm`. Incoming SDK eyes are used directly. The SDK can then
apply its own movement/scale controls without an extra hidden scene transform.
The depth layout is negotiated before begin-frame at half the color width and
height. A 1920x1080 color eye thus has 960x540 depth. Render resolution can be
100/75/50%; full color remains the default and transport color size is unchanged.
The module and PERF_HOST record actual render, color, depth and desktop sizes.
Half-sized output depth alone does not reduce full-size color ray casting.

Accepted plugin source: `bc1da84b7a51e3cd1cdb5e6957b0bd0014ef71fd`.
[Slicer 38750315676](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38750315676)
and [build/test 38750316041](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38750316041)
passed. The unchanged native code was built and tested on source
`4fd59dad46fd38588c33883dcead62d175141658` in
[Windows native 38749688548](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38749688548).

- Independent matrix contract checks cover anterior, posterior and oblique
  initial views, stereo/head rotations, +app_pose X moving content right and
  +Z toward the viewer. VTK eye position equals the SDK request exactly.
- Actual Slicer rendering verifies half-sized host textures against independently
  requested smaller eyes, and smaller fallback volume depth with full-size color.
  Existing CT depth, ROI, host shaders, MSAA and desktop restoration checks pass;
  normal capture still makes exactly two host renders after queued Qt events.
- Mesa GPU tests verify independently sized color/depth, flipped rectangles,
  color enlargement, and nearest depth at foreground/background discontinuities
  for both R32F and D32F. CPU layout checks include reordered eyes and odd sizes.
- Windows host ABI 2, WGL sharing and lifetime tests passed. The packaged plugin
  and DLL must be updated together; Slicer must be closed during installation.

R13 starter SHA-256: `679deb687d8edc209a11d92082bfa230bf91146ee01e9c062bf5eb90b478f6b6`.
Host DLL SHA-256: `821fdf85a940e12bf47ca8a7adcebb7444708aa62b72a751f86d9f13e3690704`.
All 74 manifest entries and the packaged host Python sources were verified.

No real SDK runtime/Visualizer runs in these CI tests. Subsequent field logs confirm the negotiated layout
and actual render sizes; the user confirms orientation. These CI checks alone
do not establish target-device FPS or separately verify every X/Z gesture.

## Live r11 performance report and r12 optimization, 2026-10-15

`QAROS-VTK-diagnostics-20261015-135746-589.zip` contains a verified r11 run
with 484 stereo frames, 1920x1080 per eye, a normal stop and no duplicate-frame
error in that run. Older r10 logs in the same archive are separate runs.
Weighted stage means: color renders 57.86 ms, supplementary depth renders
49.17 ms, desktop restoration 28.70 ms, Python frame wall time 141.29 ms.
The measured stream rate was 5.78 new stereo frames/s. SDK readiness waiting
averaged 22.82 ms on the worker. Native explicit host/copy synchronization was
2.63 ms, already included in the Python copy calls; it must not be added again.
This does not measure GPU waits internal to VTK separately. The user's observed
7 FPS in the source Slicer is consistent with the long synchronous GUI workload.

R12 changes the host plugin, retaining the accepted r11 native DLL and SDK:

- A scoped addition to the actual volume shader writes first-contributing-sample
  depth alongside normal host color. No second volume render is needed on the
  supported standard VTK 9.6 Composite pipeline.
- The render/display framebuffers borrow reusable eye attachments; the original
  desktop textures are reattached without rerendering, copying or CPU readback.
- CTK's automatic camera redraw is disabled only during capture and restored
  afterwards. This prevents a hidden synchronous extra render on camera
  restoration. The normal event loop and subsequent user edits remain enabled.
- Conflicting shaders, custom passes, SSAO, shadows and actual translucent
  geometry retain the supplementary depth path. Slicer's enabled peeling flags
  alone do not imply an active peeling pass when all polygonal props are opaque.

[Slicer acceptance 38740811539](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38740811539)
passed on source `5e8cc3bcf8e93a51f088bc7203fd2850700da976`, using the official
Slicer 5.12.4 / VTK 9.6.2 binary under Xvfb/Mesa:

- Exactly two render-window StartEvents, including after queued Qt events.
- Color within two byte levels of the unmodified host rendering, including
  custom surface and volume shaders; first-hit depth agrees with VTK's
  independent RenderToImage result within 2e-5 of normalized depth.
- Opaque surface occlusion, live ROI cropping and empty/offscreen rays.
- Exact preservation of desktop color/depth and original texture objects.
- Changed eye sizes, MSAA 0/4, consumer failure and restoration of the current
  MRML shader/property-key objects, sampling, camera, size and render enablement.
- Compatibility fallback for an existing depth override or conflicting shader.

[Build/test 38740812647](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38740812647)
passed Linux GPU/core tests, Windows core/runtime tests and ParaView packaging
on the same source commit. The native DLL is unchanged from accepted r11 build
38729343448 (SHA-256 `805c01781dec30d1e3d2aac9a8401df50e09a2483b1c256ee967920a3568e50e`).
R12 ZIP SHA-256: `677c698c2672b88c95cff06ab1ccfc07a13cbccb3e4ec57685fdf2fe8929f128`.
All packaged file hashes and the packaged host Python files were checked locally.

The standard capture requests two host renders instead of five. Requested
eye resolution is unchanged. Full-resolution image sampling is used while
writing volume depth; the original sampling settings are restored afterwards.
Native synchronization is unchanged. Two eye renders still occupy the GUI/GPU
thread, so this is not a claim of zero source slowdown, 60 FPS, or a proven
speedup on the target NVIDIA laptop. That requires a new field run.

## Live r10 field report and r11 correction, 2026-10-15

`QAROS-VTK-diagnostics-20261015-133345-549.zip` confirms the published r10 host
DLL and SDK header on NVIDIA RTX 2070 Max-Q. The user reports about 10 FPS and
one Visualizer exit. Live session 13:29 logged 240 submitted stereo frames then
`ERROR: Duplicate host frame plane`; session 13:30 reached at least 600 frames.
There is no matching Windows crash event in the collection. The old sender logs
are from other sessions and must not be used to explain live-host performance.

The bridge left a finished frame posted until the SDK worker woke up. If the GUI
polled first, it could lease that same frame again, with its plane mask already
complete. R11 moves ownership into a mutex-protected lease state: submitted and
consumed frames cannot be acquired. A portable regression test forces this exact
finish/re-poll-before-consume order 1000 times, and checks stale/partial/duplicate
planes and cancellation. A Python guard also blocks nested timer events from
overwriting the live frame or destroying the transport inside host rendering.
This establishes a bridge defect and correction, not the cause of the Visualizer
process exit, which still requires verification on the target installation.

R11 records five-second wall-time averages in `host-bridge.log` and
`host-python.log`, both collected by COLLECT-LOGS. Python separates normal color
rendering, supplemental depth rendering, copy calls and desktop restoration.
Native metrics separate SDK readiness, metadata, GUI handoff, SDK show and the
existing GL synchronization/copy calls. GPU work is asynchronous: render CPU
calls can defer GPU cost into later synchronization. Native copy/sync fields are
subsets of Python copy calls and native GUI wait; do not add overlapping fields.
The instrumentation adds no pixel readback or GPU wait and makes no FPS claim.

R11 release verification, source `cf96875df48a247fd693b4ad4207b40b52dd59d7`:

- [Windows native 38729343448](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38729343448): both SDK builds; all eight host-only CTest cases, including the deterministic lease regression, Python nested-timer/stop cases, ABI and WGL sharing.
- [Slicer 38729343478](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38729343478): actual Slicer 5.12.4 stereo/color/depth/ROI/restoration acceptance and non-overlapping Python stage measurements.
- [Build 38729343505](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38729343505): all nine Linux CTest cases, Windows core/collector checks, both SDK compile paths and packaged Python plugins.
- DLL SHA-256: `805c01781dec30d1e3d2aac9a8401df50e09a2483b1c256ee967920a3568e50e`.
- ZIP SHA-256: `baa8f803914c62c366e3efe4f1d496e314d0ceb518f65323226b1fae131feb76`; 73 internal manifest entries verified.
- These checks do not establish improved target-PC FPS or prove the cause of the receiver exit. Next on-device check: 30 seconds of live CT, normal Stop, then collect both live logs.

## Direct host rendering: r10, 2026-10-15

The user confirmed that the r9 volume test works on the target installation.
The next test is the separate **QAROS Live Renderer** module, using the actual
Slicer view and GPU textures. This is not an additional snapshot export mode.

Verified release inputs:

- [Windows run 38717231877](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38717231877), source `9d1b6126aeaf4d897d68c9e3608f6dedfc6621b7`: modern/session sender builds, VTK-free host DLL, ABI test and shared WGL texture test.
- [Slicer run 38718347233](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38718347233), source `76db41975b58efab265dba5d85ba05dbf3072ea3`: complete live-host acceptance below, including custom shader and offscreen-volume clearing.
- [Build run 38718347268](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38718347268), same source: CTest/GPU oracles including split color/depth copying, ParaView compatibility and packaged plugins.
- Host DLL SHA-256: `32ed3995868f303d1ce7e0f9ed24d7c147ce9befb353572f776049243b835d5e`. Its PE import table contains only OpenGL/Win32 and Microsoft C/C++ runtime libraries, no VTK/Qt/GLFW.
- Assembled r10 ZIP SHA-256: `cf089ec7aca2e2ae0e7ad8fb34307dc8d3201b0a266fd8a4af0e3e2ec7c558a8`.

Slicer acceptance uses the official 5.12.4 binary (VTK 9.6.2) under Xvfb/Mesa.
It checks stereo, matching host color, tissue depth, a renderer-only actor with
a custom shader, ROI cropping, and restoration of the camera/mapper/cullers and
window dimensions after success and an injected consumer failure. Pixel readback
is confined to this test. Windows separately builds the transport with the
standalone renderer disabled, tests ABI/SDK metadata and missing-context error
recovery, and verifies shared WGL texture contents and context restoration.

These component checks cannot validate the live Slicer → QAROS runtime →
Visualizer route on NVIDIA. That remains the on-device acceptance gate. This
version adds a supplementary volume-depth pass and conservative synchronization;
it makes no FPS improvement claim. The original r8 pacing investigation remains
deferred. The published release metadata identifies the exact tested artifacts.

## Lit scenes and CT/MR volumes: r9, 2026-10-14

Source commit `e92a9f964e42ec596b4bcf9edac6f20cb97a8eea` passed:

- [Windows native run 38600700083](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38600700083): modern and session SDK builds, core/peer/frame-wait/WGL checks and exact session header fingerprint, plugin protocol 2. Windows runners do not execute the GPU rendering tests.
- [Build and test run 38600700191](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38600700191): all seven CTest cases, including VTK/Mesa volume rendering, the existing texture-copy and orientation tests, both Linux SDK branches, Python XML interchange, ParaView pipeline/plugin lifecycle and Windows core/process checks.
- [Slicer 5.12.4 run 38600700041](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38600700041): actual packaged module discovery, transformed models/segments, scalar CT export with physical spacing and world matrix, visible-scene selection, unsupported ROI rejection and Start/Update/Stop.

The new GPU oracle checks transfer-function color and updates, tissue depth rather
than bounding-box depth, transparent/far background, out-of-view clearing, stereo
disparity, resize, opaque surface occlusion, failed-reload retention and curved
surface lighting. Readback is test-only. The first test revision exposed a VTK
appended-XML rewrite issue in its fixture; the fixture now uses the same inline
binary XML mode as the production exporter, and the complete test passes.

The Windows starter includes vtkRenderingVolume and vtkRenderingVolumeOpenGL2
9.3 DLLs, the volume test executable and the matching new Slicer scene module.
Assembled ZIP SHA-256: `12f3001e2209f4876adc3607d5e459aa4b2e9fed796dd52acc1f473253223335`.
Session sender SHA-256: `415af46214722e4442364ec6306a3114dd4a494664dc85eb7f550283643bf80f`.

This is not an FPS fix. QAROS frame pacing, WGL context handling, SDK layout and
color/depth origin conversion from r8 are retained. The user confirmed the r9
volume test on 2026-10-15; operation in two headsets is unverified. Scope: Slicer,
one scalar volume, Composite mode, no ROI cropping, linear volume transforms,
opaque surfaces when combined with a volume. ParaView remains surface-only.
Center/fit is explicit and defaults to 0.65 m; disable it for physical size.

## On-device r8 result: SDK readiness dominates, 2026-10-14

`QAROS-VTK-diagnostics-20261014-172918-472.zip` verifies the exact r8 sender
SHA-256 `2e35c170c9e0f663db7bb3bc8479f38ee490d97ed4a63046757f89e28bdc1321`
and the unchanged session SDK header. The sender submitted 1212 stereo frames
and stopped normally. A first-pose 801 retry occurred during startup and
recovered; the measured streaming windows contain no error retries or scene
reloads. The collector contains no matching Windows crash records.

Eleven complete windows contain 1125 frames over approximately 55.35 seconds:
**20.33 source stereo FPS**, at 1920 x 1080 per eye. Weighted wall times:

| Sender measurement | Mean ms / stereo frame |
| --- | ---: |
| Whole frame cycle | 49.20 |
| Begin-frame request to ready (overlapping total) | 42.57 |
| Pending-poll sleep (part of the readiness wait) | 40.47 |
| Housekeeping (mostly while polling) | 2.11 |
| VTK rendering, both eyes | 4.01 |
| GPU texture copy | 0.64 |
| glFinish | 0.42 |
| SDK submission | 1.44 |
| All nonblocking begin-frame calls | 0.08 |
| OpenGL texture access | 0.01 |
| Layout and pose access | 0.01 |
| Frame metadata release | 0.00 (rounded) |
| Unattributed time | 0.02 |

The request-to-ready total overlaps polling/sleep/housekeeping, so these rows
must not all be added together. It represents about 86.5% of the frame cycle.
There are 14.50 pending polls per frame. Each requested 2 ms sleep actually takes
2.79 ms on average, with an 8.86 ms maximum over the run. This is not a single
40 ms oversleep. The supplied C header says try_begin_frame starts one async wait
and subsequent calls poll its completion. Repeated pending results therefore
point to readiness pacing/backpressure, rather than 40 ms of frame-texture access
or rendering. Faster polling may reduce readiness-detection latency, but this
measurement does not prove that removing sleep removes the SDK's wait.

The immediate bottleneck is now localized to SDK frame readiness. It does **not**
yet identify which internal condition holds it up: render-request pacing, transfer
buffer availability, downstream mixer/receiver GPU work, or synchronization.
The sender-thread timings do not measure asynchronous SDK workers. In particular,
fast application GPU copies do not prove all internal transfers/conversions are
fast. Slicer only exports geometry to the separate sender; no repeated exports
occurred in this run.

Next evidence must come from the actual QAROS stream route: source -> mixer and
mixer -> Visualizer FPS, source render-request wait versus frame hold, receiver
frame/pose age and mixer timing. The public SDK documentation describes Stream
Graph / warping timing views, but this installed session runtime's exact UI has
not been observed. Obtain a screenshot of the running Visualizer to locate the
available timing view before giving version-specific click instructions. No r9
binary change or claimed 60 FPS fix follows from this log alone.

## Confirmed orientation and source throughput: 2026-10-14, r8 diagnostics

The user confirmed that r7's transformations are now correct and that Visualizer
shows about 44 FPS. The supplied `QAROS-VTK-diagnostics-20261014-170715-742.zip`
contains the exact r7 sender EXE (SHA-256
`2f5b74e79c6dd34ec7a4ca3016168e1973b7c0065a50bbdb6d1446801821de13`).
It completed 1202 stereo frames and stopped normally. Its 12 complete five-second
measurement windows contain 1188 frames over about 60.28 seconds: **19.71 source
stereo FPS**, at 1920 x 1080 per eye. Viewer FPS is a separate measurement; the
log does not establish that source throughput improved from r6.

Weighted CPU wall times per stereo frame are approximately 4.59 ms for VTK,
0.83 ms for GPU copy, 0.40 ms for glFinish, 1.40 ms for SDK submission and 0.11 ms
for all begin-frame polls. The remaining 43.42 ms is not attributed in r7. There
are about 14.66 pending polls per frame, each followed by a nominal 2 ms sleep.
This does not prove either a slow renderer or a fixed SDK frame-rate cap. Actual
sleep duration, request pacing, frame texture acquisition, scene checks and
metadata destruction must be distinguished before changing the rendering path.
The Slicer plugin publishes geometry to a separate sender process; Slicer's own
viewport is not captured each frame.

R8 extends the existing five-second measurements. Exclusive `PERF` totals now
include actual pending-poll sleep, error-retry sleep, lifecycle/event/file checks,
OpenGL texture acquisition, layout/pose access, frame-info destruction and
optional preview presentation. `PACING` adds the wall time from the first
begin-frame poll through successful readiness, the actual mean/max pending-poll
sleep, maximum frame-texture acquisition, retry count and scene reload count.
**Acquisition wall time overlaps the exclusive phase totals; do not add it to
PERF.** All times are wall time on the sender thread, not GPU timestamps or a
measurement of asynchronous encoding or compositor work.

Frame metadata is released immediately after successful submission so its cost
belongs to the same measurement window. Poll/backoff durations, rendering,
texture conversion, GPU synchronization and startup behavior remain the same.
This is a diagnostic build, not a verified 60 FPS optimization. The next on-device
run must identify which waiting phase dominates; two-headset throughput remains
unmeasured.

Code commit `4920d06ed70d8f237fc27ed3db3b635c1ff09c3f` passed both Windows SDK
builds, core/peer/frame-wait/WGL tests and exact header fingerprint verification in
[run 38588060847](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38588060847).
[Run 38588060990](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38588060990)
passed Linux and Windows core/process checks, both Linux SDK compile branches,
surface interchange, ParaView and both VTK/Mesa GPU tests (including orientation).
An isolated C++17 formatter/accounting probe also checked seeded phase totals;
its numbers are synthetic and are not a performance result.

## Vertical raster orientation and performance measurement: 2026-10-14, r7

The user confirmed that r6 streams a visible cube into Visualizer on the NVIDIA
PC. Horizontal camera motion is correct, but vertical motion makes the cube move
within its volume in the opposite direction. Reported viewer performance is
approximately 36 FPS; the requested target is at least 60 FPS, eventually with
two headsets. This is the first confirmed end-to-end Visualizer display, not a
headset or two-client throughput measurement.

The SDK coordinate guide specifies right-handed, +Y-up, -Z-forward view poses in
app content space. Our camera follows that convention. The official Vulkan cube
renderer uses a negative-height viewport with the same projection signs, placing
the upper image row at texture row zero. VTK's OpenGL framebuffer starts at the
bottom. Our copy composed the SDK view flips without converting this source
origin, vertically mirroring both image and depth relative to the view metadata.

R7 composes the bottom-to-top raster conversion with each view's existing flips
for both color and depth. Pose, projection, horizontal orientation and scene
coordinates are unchanged. First-frame diagnostics also report the view rectangle
and origin conversion. The existing GPU test now uses an elevated camera; a new
GPU test compares color and depth against an independent ray/box intersection
oracle for pitch, yaw, roll, translations, stereo, asymmetric FOV, padded view
rectangles, independently flipped planes, 2D/array destinations and D32F/R32F.
The local GL 3.3 copy test fails on r6's row order and passes with r7.

Every five seconds after the first frame, the sender records `PERF stereo_fps`,
average frame time, SDK begin-frame time, VTK rendering, GPU copy calls, blocking
`glFinish`, SDK submission and remaining time, plus idle polls and eye resolution.
These are CPU wall times per complete stereo frame, including driver stalls at
the measured calls; asynchronous encoding and receiver display are not measured.
The synchronization baseline remains in place until timings justify a change.
No 60-FPS or two-headset result is claimed. A 30-60 second on-device run will
identify which part exceeds the 16.67 ms frame budget.

Code commit `c50e71d2ca8c463f9c4d6624b3ab40c02147c25b` passed both Windows SDK
builds, core/peer/frame-wait/WGL tests and exact header verification in
[run 38584695970](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38584695970).
[Run 38584696083](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38584696083)
passed Linux core/process tests, GPU copy (2.49 s), GPU orientation (0.25 s),
both SDK compile branches, surface interchange, Windows core and ParaView tests.
Those test durations are CI wall times, not NVIDIA streaming performance.

Primary references inspected: QAROS
[coordinate systems](https://github.com/Quaternar/qaros/blob/main/docs/website/docs/developer-guide/coordinate-systems.md)
and [Vulkan cube renderer](https://github.com/Quaternar/qaros/blob/main/qar-streaming-c/examples/vulkan_cube/renderer.cpp).

## Destination framebuffer: 2026-10-14, r6

The r5 diagnostics reach a successful begin-frame, four textures/four views and
the first eye's pose/FOV. The sender then exits at our generic `Destination
framebuffer is incomplete` check, before submitting a frame. `ChannelIsClosed`
warnings occur before this point without preventing negotiation. The r5 log
does not record the failing plane, actual GL target or framebuffer status, so
it cannot establish the exact on-device framebuffer cause.

The copy code incorrectly assumed that `array_layers == 1` implies
`GL_TEXTURE_2D`. The SDK C structure supplies no GL target. A one-layer
`GL_TEXTURE_2D_ARRAY` fails the old unchecked bind; queries then inspect the
previously bound 2D texture, and the attachment subsequently fails. A local
Mesa/EGL regression using the actual unmodified r5 `GlCopy` reproduced the exact
generic error with an array destination and a same-sized stale 2D binding.

The r6 copy distinguishes ordinary 2D and single-layer array destinations,
attaches the requested array layer with `glFramebufferTextureLayer`, checks GL
errors at each stage and restores both texture bindings. It keeps the existing
single-layer layout restriction and GPU-only production path. Attachments are
detached on failure too. The first frame reports each destination's eye, plane,
object, target, format, dimensions and mip range; errors include the named and
numeric framebuffer status and GL error. It does not change SDK texture storage.

The local actual-code GPU regression passed 2D and one-layer array destinations
with D32F and R32F depth, exact color/depth comparison, flips and state restoration.
The repository GPU test now covers both targets at 128 x 128 and 1920 x 1080 in
shared contexts, retaining its analytic depth, alpha and stereo assertions.
These are runtime-free graphics tests; the specific NVIDIA/QAROS texture and
end-to-end display still require an on-device attempt.

Code commit `d37a2617c4998e0fa1f73423445b76c97d814616` passed both Windows SDK
builds, core/peer/frame-wait/WGL tests and exact header fingerprint verification in
[run 38581274978](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38581274978).
[Run 38581275009](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38581275009)
passed the expanded VTK/Mesa GPU test (4.35 seconds), Linux and Windows
core/process tests, both Linux SDK branches, surface interchange and ParaView.
The local isolated copy regression also passed with the Mesa GL 3.3 override.

## Initial viewer pose wait: 2026-10-14, r5

The r4 log confirms the installed EXE fingerprint, successful OpenGL setup and
connected transfer. Its first begin-frame attempt then failed with 803 wrapping
`RenderingProducerMissingPoseData` (801), about 1.6 seconds after sender creation.
No frame was submitted. The earlier paused-transfer failure did not recur in this
attempt. This alone does not establish that pose data will arrive on a later call.

The declared view order (left color/depth, right color/depth, distinct textures)
matches the official QAROS `vulkan_cube/main.cpp` example; we do not change it.
The r5 compatibility policy retries this exact 803/801 diagnostic only before
the first submitted frame, with 100 ms backoff inside the existing 60-second
default deadline. It reports the wait every five seconds and preserves the
missing-pose condition in a timeout error. Pose loss after streaming has started
and unrelated errors remain fatal. No synthetic pose or FOV is substituted.

First-frame phase messages distinguish a successful begin-frame, validated
texture layout, fetched pose/FOV, rendering/copy and actual submission. This
allows the next on-device attempt to tell a delayed initial pose from a persistent
receiver/runtime problem. The existing regression test now covers both captured
startup errors, the post-start pose-loss restriction and unrelated causes.

Code commit `185b217cde955fb4ade99fa49e0f35f1bab9caac` passed both Windows SDK
builds, core/peer/frame-wait/WGL tests and exact header fingerprint verification in
[run 38577206824](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38577206824).
[Run 38577206477](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38577206477)
passed Windows core/process tests, Linux core/peer/frame-wait/process/Mesa GPU
tests, both Linux SDK builds, surface interchange and ParaView host tests.
These checks do not determine whether the user's runtime will supply pose data.

## Primary-transfer startup pause: 2026-10-14, r4

The r3 diagnostics confirm the WGL fix on the user's RTX 2070 Max-Q: the worker
probe passed, the SDK invoked bind/resolve/capture/restore callbacks, OpenGL
sender creation completed, and the SDK reported the transfer connected. The
first begin-frame then returned 803 wrapping internal status 602,
`MediaStreamingStreamIsPaused`. Our sender treated it as fatal, destroyed the
session and submitted no frames.

The r4 legacy sender follows the working BANC bridge: allow an interruptible
1.2-second negotiation interval, then retry a paused primary transfer with a
100 ms backoff. The original no-frame timeout and Stop handling still apply.
The supplied C header omits status 602 and provides no cause traversal API, so
the compatibility helper requires top-level 803, a diagnostic handle, and the
exact nested `Code: 602 Desc: MediaStreamingStreamIsPaused` diagnostic. Other
begin-frame failures still terminate with their original error. The unmodified
SDK header and r3 WGL callbacks are retained.

The `legacy-frame-wait` regression uses the captured diagnostic through the C
API's result-message function pointer. It also checks success, closed streams,
missing diagnostics and unrelated 803 causes. This tests our retry decision,
not a live runtime. Actual first-frame submission/display still needs the user's
Windows/NVIDIA test.

Code commit `7a065af10cdccc4354941df60c6687f6d2ff2c44` passed both Windows SDK
builds, core/peer/frame-wait/WGL tests and exact header fingerprint verification in
[run 38574633107](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38574633107).
[Run 38574633198](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38574633198)
passed Windows core/process tests, Linux core/peer/frame-wait/process/Mesa GPU
tests, both Linux SDK builds, surface interchange and ParaView host tests.

## WGL context callbacks: 2026-10-14, r3

The r2 diagnostics from the user's Windows PC confirmed that the exact invited
Visualizer was observed in UNKNOWN state and its volume was created. The runtime
then received a 1920 x 1080 stream request, but `qar_render_sender_create` failed:
`BackendTransformNotSupported` during OpenGL/Vulkan setup, with the nested error
`Could not load OpenGL function: glGetStringi`. The sender caught this error and
closed its volume/session. No frame was submitted. The collector found no Windows
Application crash event. That absence alone is not proof that no viewer crashed.

Commit `3b560c184f34fa124cc204c11dfdc03e21f1b13e` supplies the exact legacy
header's optional WGL callbacks: bind, unbind, capture/restore and function
resolution. The resolver uses `wglGetProcAddress` with the supplied context
current, and `opengl32.dll` exports for OpenGL 1.1 functions. It never relies on
GLFW's current-window state on a runtime-owned thread. Callback data and handles
outlive the SDK. Before session startup a separate worker checks GL_VERSION,
glGetStringi availability and Win32 import entry points, then restores its binding.
After sender creation the log records whether the SDK actually used these callbacks.

The Windows `wgl-callback` test uses real basic WGL contexts, without QAROS or VTK,
to exercise worker binding, export resolution, rejection of another context and
restoration of both an existing and empty binding. It does not require OpenGL 3.3
or claim NVIDIA/Vulkan interoperability. The r3 live SDK result remains to be
verified on the user's PC; failure may require a runtime loader fix.

Windows native [run 38572186047](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38572186047)
passed both SDK builds, core/legacy-peer/WGL callback tests and exact SDK fingerprint
verification. [Run 38572186585](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38572186585)
also passed Windows core/process/WGL tests, Linux core/peer/process/Mesa GPU tests,
both Linux SDK compile branches, surface interchange and ParaView host tests.

## Legacy Visualizer startup: 2026-10-14

The supplied session SDK header has SHA-256
`022c40b670f87a066664c37daeb06da525db37fffdf5810e72cff8a5b81e677d`.
The local Windows preview worked on the user's NVIDIA RTX 2070 Max-Q. The first
streaming attempt opened a blank Visualizer and ended with `Invited viewer did
not reach RUNNING before timeout`, before the OpenGL sender was created.
Its log also reported a session without persistent storage.

Commit `f290dca3784b7af499f0ed2af5a70b3124f86071` follows the startup sequence
already used in the BANC bridge: subscribe to peer updates before invitation,
accept an observation of the exact invited id even when its state is UNKNOWN,
wait two seconds for receiver subscriptions, and create the volume with its
initial viewer assigned. Other peers cannot satisfy this wait. A shutting-down
viewer aborts startup. Legacy session teardown now leaves the session explicitly.

The `legacy-peer` regression test covers early UNKNOWN observations, unrelated
RUNNING peers and later state changes without loading QAROS. Runtime-free tests
and compilation cannot verify Visualizer display or NVIDIA frame transport.
The updated starter still requires a real streaming test on the user's PC.

Both SDK branches compiled successfully on Windows in
[run 38568711354](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38568711354).
The session EXE reported the exact header hash above and `modern_api: 0`.
Core and legacy-peer tests passed there. In
[run 38568710889](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/38568710889),
Linux core/peer/process/GPU tests, preview smoke, both SDK compile branches,
surface interchange, Windows core/process tests and ParaView host tests passed.

## Evidence recorded on 2026-10-09

| Check | Result / scope |
| --- | --- |
| Core math/layout on Linux | PASS locally and in CI |
| Core math/layout on Windows | PASS in CI (MSVC) |
| Plugin process controller on Linux and Windows | PASS: real child process fixture, stdin pairing, identity reuse, Stop/restart, per-instance isolation, SDK mismatch rejection |
| Native VTK/OpenGL build on Ubuntu 24.04 | PASS in CI |
| GPU copy test on Mesa/Xvfb | PASS: D32F and R32F, 128/256 px, two shared contexts, stereo disparity, flips, alpha, depth, state restoration |
| Preview | PASS: five-frame hidden run in CI |
| Modern SDK sender | PASS: Linux compile/link and `--sdk-info`, no runtime loaded |
| Legacy 2540 SDK sender | PASS: Linux C++ syntax check against actual package header; no link/runtime claim |
| Surface interchange | PASS: real VTK round-trip, baked world transform, triangulation, input unchanged, invalid export preserves previous file |
| ParaView host smoke | PASS in ParaView 5.11.2 on Ubuntu: export, display transform and original active source |
| Windows native VTK/sender | PASS: Release build and core tests in run 37986750101, commit d16e50cb (2026-10-09). No Windows GPU/Hub/headset test |
| Packaged Slicer plugin | PASS in official Slicer 5.12.4 on Ubuntu: module discovery, model/segment transforms, Start/Update/Stop with process fixture, cleanup without module-load/shutdown errors |
| Packaged ParaView plugin | PASS in official ParaView 5.12.1: real plugin loader, pipeline update, Start/Stop/restart with process fixture; locally and in Linux CI |
| Slicer CMake extension/CPack route | Entry point prepared; not built against a Slicer development tree. Tested installation is the packaged scripted module via Additional module paths |
| Hub / encoder / headset | Not run: no matching live runtime and supported NVIDIA device in this environment |

The first complete renderer test is [run 37949911874](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/37949911874).
The sender/interchange Linux build is [run 37951256265](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/37951256265).
The ParaView test is [run 37952481684](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/37952481684).
The Slicer test is [run 37952928419](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/37952928419).
Packaged plugin code revision **8e49a56** passed the Slicer test in
[run 37987779396](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/37987779396)
and the ParaView plugin job in
[run 37987779352](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/37987779352).
That ParaView job also uploaded the portable Python plugin ZIP. The process
fixture used by these tests is deliberately a test double; it neither loads
QAROS nor proves pairing, video transport, encoding or headset output.

The first packaged-plugin attempt exposed Ubuntu 24.04's ParaView 5.11.2 /
Python 3.12 loader incompatibility (`inspect.getargspec`). CI and the install
guide now use the official ParaView 5.12.1 bundle with its matching Python 3.10.
The official Linux archive is pinned by SHA-256:
`cd8d4b4dada44c8f36dc17e676cc5d8ae6b0a3c0449da9092e9df8bb8b5d48e7`.

The full Windows build is [run 37952481497](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/37952481497);
it was cancelled after the 60-minute limit while vcpkg was compiling Debug VTK.
The workflow now builds Release only, with a 180-minute cold-build limit.
Runs [37985668990](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/37985668990)
and [37986750101](https://github.com/HonzaHovora/QAROS_VTK/actions/runs/37986750101) both completed successfully. The 2026-10-14 Windows starter uses the latter run's unchanged binaries.
See [all runs](https://github.com/HonzaHovora/QAROS_VTK/actions) for the current commit;
these historic passes must not be read as proof for later untested changes.

Inspected SDK header fingerprints:

- Modern pinned public header: `00f96316f50ad9c7e1e94daaf9a229e704fffb1acf89aa2a585af32dde000069`
- Package 2540 header: `c709c479cc6a1acc0040fb61a071cc7bc16e65bf5acb90d624d9d6b0a6fcd3c1`

The packaged Windows build reports header SHA-256
`eed88c1e4a6c8038a96038e1850deee3241e3f3c460fd24a10c3706f1df599d9`.
For that binary, use its `--sdk-info` / bundled `qarvtk-sdk.json` rather than a
fingerprint from a different build. Exact header byte matching is required.

## Reproduce

```bash
cmake --preset core
cmake --build --preset core
ctest --preset core
python tests/test_bridge.py             # Python with VTK
xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 bash scripts/build-linux.sh
pvpython tests/paraview_smoke.py         # ParaView's own interpreter
# Packaged plugin host tests: see HOST_INTEGRATIONS.md
```

For a local SDK compile, use `bash scripts/build-linux.sh /path/to/sdk`; actual
streaming on Linux additionally requires a native QAROS runtime and supported
hardware. Windows commands are in [WINDOWS.md](WINDOWS.md).

## Definition of the next milestone

One Windows/NVIDIA machine runs a build against its installed SDK; a compatible
QAROS viewer receives a correctly scaled stereo cube with valid depth. A Slicer
model or segment then replaces the cube, preserving RAS orientation and mm-to-m
scale. Updating the surface from the plugin changes it; shutdown/reconnect behaves as documented. Record
screenshots and logs with version/GPU details. Only then call the end-to-end
prototype validated.

## Known constraints

- One viewer, one opaque surface, fixed green color, no text/overlays.
- Installed plugins control the sender; geometry exchange remains a file snapshot, not in-process host rendering.
- No CT/MR volume rendering, transfer functions, picking or bidirectional gestures.
- Fixed clip range and fixed initial app-volume placement; no auto-fit.
- Single-sample 2D textures; no array layers, Vulkan import, reverse-Z or HDR.
- Conservative `glFinish`, no performance/latency claim or clinical validation.
- No Wayland/EGL sender path or macOS sender implementation.
- Portable Python plugin folders/ZIP are provided; no official catalog release,
  automatic update or QAROS runtime redistribution. Backend builds are separate.

The file/CPU scene exchange is intentional for this first stage; the per-frame
pixel path stays on the GPU. A future in-process adapter must match its host's
exact VTK/Qt ABI and adopt the host's render-thread lifecycle.


