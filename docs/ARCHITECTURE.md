# Architecture

## Slicer product scope (decision, 2026-10-22)

Slicer has one QAROS module: direct host streaming (`QarosLive`). The snapshot
module (`QarosVTK`) and Slicer exporter have been removed, including registration,
packaging and the active installation path. Historical combined installers
and publication definitions are retained, but their packager and jobs are
disabled. We do not maintain a replacement Slicer renderer. ANARI remains an independent renderer
route, not a new capture mode added by this cleanup. The private `QarosVTKLib`
name is retained for installation compatibility and does not register a module.

`UPDATE-LIVE-PLUGIN.cmd` supports first installation and backed-up migration.
Historical prototype details below document earlier work; they do not advertise
the retired Slicer module. Shared standalone code remains for the earlier
ParaView prototype and development tests.

## Host rendering first (decision, 2026-10-14)

The next integration must use the host application's actual rendering pipeline.
Slicer/ParaView remain responsible for their live scene, mappers, lights,
materials, volume transfer functions, clipping, shaders and plugin render passes.
QAROS supplies viewer pose/FOV and transports the resulting color and depth.
Recreating host features in a separate renderer repeats the compatibility and
maintenance problem identified with the ANARI integration.

**Status, 2026-10-15:** the experimental Slicer host-rendered path is implemented
as `QarosLive.py`, `qarvtk_host_view.py` and the VTK/Qt-free `qarvtk-host.dll`.
The r10 field test reached live streaming on the target NVIDIA/SDK installation,
but exposed a completed-frame re-acquisition race, about 10 FPS and one reported
Visualizer exit. R11 corrects lease ownership and instruments live wall times.
R12 reduces the standard Composite path to one host render per eye, preserves
desktop framebuffer attachments and suppresses CTK's redundant camera redraws.
R13 moves all placement, axis conversion and fitting into SDK `app_pose` and
`app_scale`, aligns the initial viewing direction with Slicer and negotiates
depth at half the color width/height. The user can select 100/75/50% host render
resolution independently of the negotiated stream color dimensions.
Stable end-to-end operation and the receiver exit remain to be verified; the
milestone was not yet declared complete at that date. The R9 Slicer snapshot
module was subsequently retired on 2026-10-22.

The live module uses the original view's resolved display framebuffer. On VTK
9.6's standard Composite pipeline, a scoped shader addition writes the first
contributing sample to `gl_FragDepth` during the normal color render. The original
mapper, lighting, transfer functions, crop and compatible shader replacements
remain in use. Depth writes use VTK's `GLDepthMaskOverride=1`; full-resolution
image sampling avoids the mapper's separate color-only downsampling FBO. Both
settings and the original shader/property-key objects are restored afterwards.
Custom render passes, SSAO, volume depth peeling, existing depth overrides and
conflicting shader replacements retain the supplementary RenderToImage path.
That path combines volume and opaque-surface depth on the GPU, preserving color.

For the Qt host, reusable eye textures temporarily replace the attachments of
the existing render/display framebuffers. The original desktop textures remain
untouched and are reattached with their original sizes, including after failure.
No desktop copy or full render is needed. CTK render requests from temporary
camera changes are disabled within the capture scope; its previous render-enabled
state is restored before returning to the event loop. No events are processed
while an eye camera is installed. Other windows/stereo modes retain a desktop
rerender fallback. There is no scene export or CPU image path.

The native C ABI receives texture names only inside Slicer's process. Two owned
WGL contexts share host resources: one copies frames, one is reserved for SDK
graphics callbacks. SDK session/frame control runs on a worker. A frame lease
holds SDK textures until the GUI thread has copied both eyes and planes; Stop
cancels pending SDK work and destruction waits for worker shutdown. The bridge
uses the pinned session SDK and imports no host VTK/Qt classes. Conservative
`glFinish` calls remain unchanged; removing synchronization needs a separate
ownership/fence design. R12 changes the Python host capture, not the native SDK
transport. Two actual eye renders still consume time on the host GUI/GPU thread.

R17 uses host ABI 4: creation retains SDK placement, workspace XYZ dimensions
and room centre (112 bytes); each frame still reports both
color/depth dimensions and the exact near/far supplied to `show_frame`.
App coordinates are the original Slicer world RAS numbers, without a per-frame
matrix, centering or scaling. In Live r8, `app_scale=.001 * size_multiplier`
maps native RAS millimetres to room metres. The default multiplier is 1,
independent of centering. A logarithmic slider selects 1/8x through 8x at Start.
`app_pose` rotates the initial Slicer camera basis onto the app-volume basis
and translates the scaled, rotated bounds centre to the volume origin.
SDK poses already account for placement and scale and are used directly by VTK.
At Start, `ComputeVisiblePropBounds` supplies the visible scene's world AABB.
All eight corners are rotated into workspace axes before calculating size,
including oblique camera orientations. Centering places the workspace centre
at `(0, 0, -1.5)` room metres without changing scale. Without centering,
the scaled world placement is retained by moving the workspace
centre to the scaled, rotated scene centre and compensating with `app_pose`.
XYZ dimensions are assigned by name to the SDK's X/Z/Y width/length/height
fields; degenerate axes have a 1 mm minimum thickness. Bounds and placement
stay fixed until restart, with no new per-frame work. ROI rendering remains
live, but clipping does not tighten the mapper bounds or workspace automatically.
Clip values are `.05/app_scale` and `20/app_scale` at Start, in app units;
the exact same values accompany the depth stream, with `depth_scale=1`.

The layout is changed before the first begin-frame, preserving per-plane flips
and using a separate texture for each view. Depth has ceil(color width/2) by
ceil(color height/2). GPU copy samples depth with nearest texels, never linearly
blending a foreground surface with background. Scaled color uses bilinear
resampling with clamped edges. The supported single-pass path still computes
depth with color at the selected host render size; only its output is reduced.
The compatibility depth pass renders at the smaller of host and depth sizes.
The UI/log distinguish render pixels, stream color/depth and desktop dimensions.

Initial depth scope is one GPU Composite volume with opaque surfaces. ROI
cropping is live. Multi-volume/MIP and volume plus translucent surfaces fail
explicitly until a valid depth policy is implemented. Color from arbitrary host
effects does not by itself guarantee meaningful depth for every custom plugin.

### First milestone: Slicer's live 3D view to Visualizer

1. Use the existing host view and its rendering pipeline for each requested eye.
   Copying its actors into another `vtkRenderer` is not sufficient: host display
   managers and custom passes must remain involved. Treat the host as the source
   of scene state instead of extending the VTP/VTM exporter.
2. Separate the reusable SDK transport/GPU copy from `qarvtk::Renderer` and GLFW
   ownership in `apps/sender.cpp`. Establish GPU access to the host-rendered
   output. The first candidate is an in-process bridge with an SDK worker context
   sharing resources with the host context. A GL texture name alone is not a
   cross-process sharing mechanism. Verify context/thread ownership before
   selecting the final bridge implementation.
3. Render on the host's render/GUI thread without reentrant rendering. Apply each
   QAROS eye pose, asymmetric projection and clipping in host coordinates; restore
   desktop camera and render state on success, stop and error. Unit conversion
   and XR placement belong at the view boundary, not in rewritten scene data.
   The SDK worker must not take over the host's GUI context.
4. Transfer host color and validated depth to SDK textures on the GPU. Reuse
   layout/origin checks and synchronization from the transport prototype where
   applicable. Do not introduce per-frame screenshots or CPU pixel readback into
   the production path.
5. Validate volume depth explicitly. A normal scene depth attachment must not be
   assumed to describe visible tissue in a ray-cast volume. Determine how to
   obtain a usable depth from the host's active volume pipeline while preserving
   its color output. One depth value cannot describe every transparent layer;
   document the policy and verify it with surfaces, cropping and empty rays.

Do not load r9's independently built VTK/Qt libraries into Slicer or ParaView.
Any C++ extension using their VTK/Qt types must target the matching host build.
The implemented transport-only bridge exposes a narrow native interface without
linking another VTK. Real Windows Slicer/SDK shared-context integration still
needs on-device validation.

### Host-rendering acceptance

- On the target Windows Slicer installation, stream a live CT/MR volume and an
  opaque model/segment to Visualizer from the host's render path. Verify stereo
  pose/FOV, depth orientation, scale and surface/volume occlusion.
- Change transfer functions, visibility, lighting and ROI cropping in Slicer.
  The streamed view must follow the live host rendering without scene export or
  new per-feature serialization. Include a host-only custom render effect in
  the comparison so that a reconstructed scene cannot pass as host rendering.
- Compare the host and streamed source at the same camera, FOV and resolution.
  Preserve the desktop view and normal interaction after frames, stop and
  failure. Check resource cleanup when the host view closes. Test-only readback
  may be used as an oracle; it is not a production capture path or an FPS test.
- Verify the real Windows SDK/GPU path. A synthetic renderer test or a passed
  Linux host smoke test does not complete this milestone.

The user has requested read-only slice previews next; R17 implements this
alongside continued host-rendering acceptance. Then adapt the shared transport
to ParaView's own view/render lifecycle and connect the existing Qt streaming
and input capabilities when their integration contract is available. An optional
public UI extension interface should avoid maintaining a replica UI per plugin.
FPS investigation remains a separate task.

### R17: live slice preview panels

`qarvtk_host_panels.py` observes EndEvent on visible Slicer slice render windows
and reads their completed display-FBO color texture. It never calls Render or
reads pixels to CPU. The source can be a static slice, changing annotations,
video or a playing sequence: each newer rendered image replaces the pending
revision. Capture is limited to 5/10/15/30 FPS per view (15 by default), with
at most one capture per Qt timer turn and fair rotation between views. There
is no frame queue. A busy native staging lock skips capture; the next attempt
uses the latest texture. The preview checkbox and FPS choice apply at Start.

Each visible slice has a mono RGBA video sender and SDK GUI stream panel,
independent of the stereo/depth sender. The native panel worker uses
try_begin_frame and sends the newest owned GPU copy. A static copy is resent
every two seconds for reconnects without recapturing Slicer. A panel failure
is reported separately and does not terminate stereo streaming. No input
subscriptions, event forwarding, Qt toolbars or controls are part of R17.

Slice windows need not share the 3D view's OpenGL context. Each source owns a
shared copy context and a separate SDK graphics-callback context. An exclusive
GPU mutex controls copy-context migration between GUI capture and the worker;
GUI capture uses try_lock rather than waiting for the sender. GPU completion
still uses conservative glFinish, so copies and additional encoders have a
real cost. All SDK panel destruction happens on the panel worker. The session
joins that worker before destroying the SDK session/library; GL resources are
released on the GUI thread after native shutdown.

An owned texture has at most 768 pixels on its longer side, even dimensions,
and the source aspect ratio at creation. Later source resizing fits inside it
with black padding. The first copy converts VTK's bottom-left origin to the
SDK's top-left convention; upload does not flip it again and respects returned
view flips. Up to eight named slice views occupy a three-column grid below the
workspace. Since r8.1 each starts at a 14-inch (0.3556 m) diagonal, preserving
its video aspect ratio, independent of model scale or capture resolution.
Size is supplied only at creation so user resizing in QAROS persists during
streaming. A recreated panel starts at the default again.
Red, Yellow and Green come first.

Layout-hidden slices hide their panels and resume with current content when
visible again. Window/context replacement retires the old source. Up to 16
source creations per session bound retained contexts; reaching that limit asks
for a stream restart. Stop removes all observers and closes streams/panels.
Diagnostics report capture time, busy skips, replaced revisions and native
source sizes. Real Slicer/Mesa and GPU-copy tests cover source correctness,
video freshness, orientation and lifecycle; Windows SDK/Visualizer behavior
and the cost on the target NVIDIA GPU remain field checks.

## Current r9 data and frame paths

Host path: a Slicer scripted module or a ParaView Python pipeline plugin controls
one child sender through `integrations/qarvtk_runtime.py`. The plugins and backend
are separate installable components from the start.

Scene path: Slicer exports lit models/segments and one scalar CT/MR volume as
an atomic `.vtm` manifest with immutable VTP/VTI payloads. Surface transforms
are baked in world RAS mm; volume physical spacing and its image-to-world
matrix are retained. Colors, opacity and transfer-function nodes are explicit
field data. ParaView retains its surface-only `.vtp` path. CPU/file work happens
on publication/reload, never per video frame.

Frame path: QAROS begin-frame → SDK camera pose/FOV for each eye → VTK color/depth
FBO → two shader copies per eye to SDK-owned textures → `glFinish` → show-frame.
No video pixels are read back to CPU by production code. Tests read texture data
only to compare it with known geometry. This is GPU-to-GPU copying, **not** zero-copy.

`glFinish` is deliberately conservative for the first shared-context prototype.
It blocks for completion and must be profiled with the real encoder; fences or
SDK-supported external synchronization are a later optimization. A GLFW preview
blit is optional and not part of the stream.

## Coordinates and depth

- Content coordinates: right-handed, metres, +Y up, camera forward −Z.
- SDK quaternion order: x, y, z, w. Each eye uses its own pose and asymmetric FOV.
- SDK poses are already in app content space. Do not reapply volume transforms.
- VTK explicit projection uses OpenGL NDC depth −1…+1. Texture depth is 0…1,
  conventional near-to-far, and is sent unchanged with matching near/far.
- Current clipping: 0.05–20 app metres, `depth_scale=1`. No reverse-Z.
- Slicer: export world RAS mm, then `(x,y,z) → 0.001 × (x,z,−y)` in the renderer.
  This is a proper axis rotation preserving handedness. Parent MRML transforms
  are baked before unit/axis conversion.
- ParaView plugin: preserve pipeline coordinates; set Metres Per Unit explicitly.
  Use an upstream Transform filter. Display-only Position/Scale/Orientation are
  not included by the plugin. The older `export_active` convenience helper also
  bakes the active view's display transform. The sender assumes Y-up unless
  `--ras` is selected.

Slicer defaults to the explicitly labelled Center and fit option: one common
transform centers all scene objects and maps the longest bound to 0.65 m.
Disabling it preserves physical size and world position. Original `.vtp` imports
keep their original position and command-line unit scale.

## Texture contract

The sender requests separated LEFT color/depth and RIGHT color/depth textures.
It consumes returned metadata rather than assuming texture/view order. Layout
validation rejects missing/duplicate eyes or planes, bounds violations, overlap,
zero handles, oversized textures and array layers. Reversed rectangle corners
are treated as flips. Color and depth dimensions may differ; sampling covers
each destination rectangle.

The sender negotiates RGBA8 and D32_FLOAT and accepts R32_FLOAT depth if reported.
The GL copy validates actual shared texture handles, dimensions and internal
formats. The lower-level copier handles native depth or R32F color attachments;
MSAA is disabled. Layered textures, external-memory import from Vulkan, and
multisampled targets are not supported.

Copy operations restore the GL state they modify. VTK's normal framebuffer and
shader state remain intact. The output background is transparent with depth 1.
Surfaces use camera-relative key/fill/back lights and Phong normals. The volume
uses vtkOpenGLGPUVolumeRayCastMapper in RenderToImage mode. Its color and
first nonzero-opacity sample depth are GPU textures; GlComposite combines them
with the surface framebuffer without CPU readback. Opaque surface depth terminates
rays. Empty rays have depth 1. Culling is disabled so each eye's offscreen image
is cleared even when a volume is outside the view.

One depth sample approximates a translucent volume; it cannot encode every
contributing tissue layer. One scalar volume with opaque surfaces is supported;
translucent surface/volume combinations are rejected by the exporter. ROI/MIP,
multiple volumes, tone mapping and color-space calibration are not supported
by r9. The next integration must assess these through the host-rendered path.

## Plugin boundary and process protocol

ParaView and Slicer package different VTK/Qt versions. Loading an independently
built VTK library into either process risks ABI and graphics-context conflicts.
The current Python host plugins export a scene and control the child sender;
they do not stream the host renderer's output. They load neither the SDK nor
another VTK into the host. The C++ `Renderer::set_polydata` entry point feeds the
standalone renderer and is not a host-rendering integration. It is also not
ABI-safe across unrelated VTK builds.

Protocol 2 adds scene/volume snapshots; protocol 1 remains readable for the
legacy ParaView/VTP route. The process contract consists of:

- `--sdk-info`: JSON with exact header SHA-256, `modern_api` and `plugin_protocol`.
  The plugin checks these and the presence of the platform runtime before launch.
- A private working directory per instance, atomically updated `scene.vtp` or `scene.vtm`, and
  `--watch`. Only scene updates use CPU/files; every video frame stays on GPU.
- `--stop-file`: the owner requests cooperative shutdown by creating its own file.
  `--parent-pid` terminates the render/wait loop if the host disappears. Windows
  holds a process handle; Linux checks whether the parent PID still exists.
- Pairing code on stdin, a persistent identity outside the checkout, and a log.
  The Slicer UI clears the code after launch. ParaView reuses prior pairing so
  that no code is stored as a pipeline property in a saved state.
- Explicit Start commands only: loading a Slicer scene or a ParaView state never
  starts streaming automatically. `running` reports process liveness, not a
  confirmed viewer connection.

SDK calls can block until their configured 60-second timeout. Slicer's Stop
requests shutdown without blocking the UI; polling enforces a 65-second deadline.
Cleanup/ParaView Stop waits up to three seconds before killing an unresponsive
child. That fallback cannot guarantee SDK-side cleanup. Private geometry is
deleted on cleanup, logs are retained for diagnosis. A host crash can leave a
temporary VTP/VTI generations; no patient data belongs in the repository.

Slicer settings hold paths and host name, not pairing codes. Its optional one-second
publication is intentionally opt-in and skips unchanged surfaces. Volumes update
only on Start/Update scene. Immutable generations are kept until cleanup to avoid
racing an in-progress reader; repeated manual CT updates use additional disk.
All generation folders are removed when the owning backend closes.
ParaView publishes on pipeline updates, supports only the local built-in session,
and rejects pvserver/MPI execution before writing a scene or starting a process.

`scripts/package_plugins.py` copies an explicit allowlist into self-contained
host folders. It bundles neither the SDK nor backend binaries. The Slicer CMake
extension entry point supports the standard developer packaging route; the ZIP
is a portable manual-install bundle, not an Extensions Manager package.

## Next integration stages

1. Complete the host-rendered Slicer milestone and acceptance checks above.
2. Validate R17 live slice/video panels against the Windows SDK and Visualizer.
3. Integrate ParaView's actual renderer using a matching host adapter and the
   shared transport boundary. Verify live pipeline updates and render effects.
4. Connect original Qt controls/input and an optional extension interface.
5. Add per-viewer cameras and picking/gestures; profile synchronization with the
   real encoder before replacing conservative waits.

Further per-feature scene-export reconstruction is no longer the integration
roadmap. R9's snapshot format and standalone renderer remain useful as a
transport/debugging baseline; their documented limitations describe the current
implementation, not the desired host-rendering feature boundary.

OpenGL is the shortest path for the existing VTK OpenGL2 backend. QAROS's existing
Vulkan support can be used by another renderer later; this repo does not add a
second backend or claim a working Vulkan/VTK path.
