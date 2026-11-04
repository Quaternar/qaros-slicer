# Slicer r8.8 candidate: port of ParaView r6c, 2026-11-03

The transport optimizations are portable. The tested Slicer candidate reduces
GUI stereo preparation by roughly 40%, but this CT comparison does **not**
establish a large FPS gain or improved jitter. The user's installed r8.7 is
retained; r8.8 is supplied as a draft candidate for further evaluation.

## What transfers

| ParaView change | Slicer result |
| --- | --- |
| Avoid repeated pipeline discovery | ParaView-specific; Slicer does not do that discovery on every timer tick. |
| Preserve desktop framebuffer attachments instead of a third render | Already present in Slicer; confirmed again on VTK 9.6.2. |
| Independent cancellable SDK begin/show cycle for every sender | Ported. A receiver waiting for its next pose or SDK show does not own the GUI renderer. |
| Bounded FIFO with one pending frame per receiver/volume route | Ported. Exactly one job is acquired by Slicer's GUI renderer at a time. |
| Copy in the original GL context; one GPU fence per stereo frame | Ported. This replaces eight normal per-plane `glFinish` calls; a receiver completion worker waits before SDK show or texture retirement. |
| Safe cleanup after a partial GPU copy | Ported, including explicit frame abort after restoring the VTK state; verified by fault injection. |
| Retain an empty workspace across Stop/Start | Ported with explicit audience detach and reapplication of the current placement, scale and name. Disconnect closes retained workspaces. |

No new scene renderer is introduced. The existing Slicer view, VTK mappers,
transfer functions, ROI, shaders, physical scale and stereo eye poses remain
the source of the pixels. Independent receiver workers do not execute VTK
rendering concurrently.

## Newer VTK and depth

Slicer 5.12.4 on this machine uses **VTK 9.6.2**. Its existing standard GPU
Composite path already adds first-contributing-sample depth to the normal
color render. It uses two scene renders per stereo frame, with zero extra
volume depth renders and no desktop restoration render. This depth hook was
already in r8.7; it is not a new r8.8 speedup.

Custom/incompatible depth hooks retain the supplementary native RenderToImage
path. A shared scene containing multiple volumes still requires its validated
supplementary depth render to preserve the native color compositing. Separate
per-volume workspaces retain the single-volume fast path. The version guard
has not been widened to untested VTK releases.

The installed ParaView 5.12.1 uses VTK 9.3. Its missing configurable framebuffer
depth-size accessor required a VTK 9.3 fallback. Slicer already has that API,
so that older-version workaround is not copied into Slicer.

## Controlled A/B/A comparison

Three separate actual Slicer processes used temporary settings, the same two
dedicated Visualizer slots and the same synthetic 96³ CT with 1 mm spacing,
GPU Composite rendering and identical transfer functions. Each process
measured one receiver, then two, after a 5 s warmup, for 30 s per phase.
All phases used 100% rendering, 1920 × 1080 color per eye and 960 × 540 depth.
The original DLL is r8.7; the candidate DLL contains the ported r6c transport.

| Variant | Receivers | GUI stereo preparations/s | GUI preparation mean | Color + depth copy calls mean | Aggregate GUI completion interval p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| r8.7 before | 1 | 22.22 | 15.83 ms | 7.33 ms | 66.68 ms |
| r8.8 candidate | 1 | 20.33 | 9.61 ms | 0.72 ms | 75.01 ms |
| r8.7 restored | 1 | 20.03 | 15.93 ms | 7.60 ms | 70.97 ms |
| r8.7 before | 2 | 32.50 | 16.66 ms | 7.50 ms | 53.39 ms |
| r8.8 candidate | 2 | 33.76 | 9.29 ms | 0.67 ms | 57.84 ms |
| r8.7 restored | 2 | 31.62 | 16.05 ms | 7.60 ms | 54.53 ms |

Two-receiver FPS is the **total across both receivers**, not FPS per headset.
These measurements end at GUI preparation/native finish; GPU fence waits and
SDK show continue on receiver workers. They are not end-to-end display latency.
The queue remains bounded, so this is not an unbounded pre-render benchmark.
The p99 column also describes aggregate GUI completions, not either receiver's
display cadence. Native `JITTER_NATIVE` samples separately record sender
completion, begin, GUI, GPU wait and show durations.

The candidate spends much less time blocking the GUI during texture copies.
The actual VTK color renders remain around 6 ms for both eyes. One-receiver
FPS lies within the original before/after range; two-receiver throughput is
only modestly higher. The p99 comparison does not show a jitter improvement.
Shorter GUI preparation therefore must not be advertised as the large
ParaView speedup recurring in Slicer. This is one synthetic scene and short
windows on a shared GPU, not a benchmark of all user datasets or headsets.

Aggregated results without receiver ids or private logs are in
[`benchmarks/slicer-r8-8-20261103.json`](benchmarks/slicer-r8-8-20261103.json).
Raw local test logs are under `build-host-perf-r8-8/`, outside version control.

## Correctness checks

- MSVC Release build and **22/22 CTest suites passed**, including 240 real
  stereo GPU copies with two completion workers, immediate overwrite of the
  source textures, exact color/depth checks and preservation of host GL state.
- Both native translation units passed syntax checks against all four older
  pinned SDKs. These are compile checks, not old-runtime executions.
- Actual Slicer/VTK rendering passed native color and independent depth
  comparisons, host-only surface and volume shaders, occlusion, live ROI,
  offscreen rays, MSAA, eye resize, desktop color/depth restoration, layout
  changes and failure cleanup.
- Multiple-volume tests passed overlapping/transformed volumes, reversed draw
  order, half-resolution combined depth, opaque occluders, live ROI, hidden or
  offscreen volumes and separate per-volume workspace isolation.
- Real Red/Yellow/Green slice preview tests passed image bounds, live changing
  frames, coalescing under backpressure, annotations, layout visibility and
  cleanup without additional scene renders. These preview tests use a
  transport fixture rather than testing actual SDK panel delivery.
- The candidate streamed the CT to two actual Visualizers, hot-added the
  second, paused/resumed an empty audience, and completed three further
  start/stop cycles with the same source identity. A failure immediately
  after the first real GPU color copy drained cleanly; the next Start rendered
  successfully. Only the two dedicated test receiver slots were removed.

Earlier GPU tests had fixed tissue/occluder pixel-count assumptions unsuitable
for reduced-resolution or cropped fixtures. The test now checks nonempty depth
and compares its values against independent native VTK references; the
occluder is large enough for its 192 × 160 test image. The production depth
algorithm is unchanged.

Physical multi-headset transfer, persistent long-session performance and
receiver/display jitter still require device-specific evaluation. No automatic
resolution reduction or default FPS cap was introduced.
