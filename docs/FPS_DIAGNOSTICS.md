# Live Slicer FPS investigation, 2026-10-15

## r14 field results: resolution, fullscreen and GPU temperature

Archive `QAROS-VTK-diagnostics-20261015-174009-305.zip` contains three
consecutive sessions, in chronological order **3, 2, 1**. The plugin is
`760df92fe350c36cddc6a10e07fe143534cb1e86`; host DLL SHA-256 is still
`821fdf85a940e12bf47ca8a7adcebb7444708aa62b72a751f86d9f13e3690704`.

| Run (log suffix) | Actual render per eye | Desktop view | Completed stereo frames | Mean source FPS | Both-eye Render wall time |
|---|---|---|---:|---:|---:|
| Full resolution (3) | 1920 × 1080 | 726 × 451 | 780 | 10.78 | 49.18 ms |
| Half resolution (2) | 960 × 540 | 726 × 451 | 1,820 | 14.94 | 36.63 ms |
| Half resolution, restarted fullscreen (1) | 960 × 540 | 1453 × 933 | 1,896 | 14.34 | 40.98 ms |

FPS is total completed frames divided by the sum of recorded `elapsed_s`,
not an unweighted mean of one-second FPS values. Render times are weighted
by frame count. Startup connection time is outside the frame windows.
Every frame retains two host renders, zero additional depth passes and zero
desktop restoration renders. Stream color remains 1920 × 1080 per eye and
stream depth 960 × 540. Fullscreen does **not** override the selected render size.

These are not controlled performance comparisons: SDK eye poses change in
all three runs. The eye midpoint starts about 1835 mm from the app-content
origin and ends at about 901, 950 and 677 mm respectively. The final run also
starts with a different Slicer viewing direction. Volume dimensions, physical
matrix, transfer-function/input modification times and sampling settings stay
unchanged. Moving the camera closer changes the image and ray-casting work.

NVML succeeds on the RTX 2070 Max-Q with no query errors. Temperature reaches
78 °C in the first run, 86 °C in the second, and 87 °C in the third. The latter
two record `0x20` (software thermal slowdown); the third also has one `0x24`
sample (thermal slowdown plus software power cap). During the final run,
active GPU clocks fall from around 1750 MHz to roughly 1575–1620 MHz. This is
direct evidence of thermal clock limiting, but does not assign all of the
render-time increase to temperature. Camera workload changes at the same time.
The counters describe the entire device, including the viewer.

The final run's unchanged-eye interval is mostly 14–15 FPS, including about a
minute with Slicer foreground, and about 13.6 FPS in the last partial interval.
The data do not show a cumulative slowdown after every focus switch. In all
three runs initial SDK readiness waiting is around 45–50 ms; it falls to about
13–15 ms near the first SDK camera movement while Visualizer remains foreground.
That is a correlation, not evidence of a particular internal SDK pacing rule.
Copy calls remain around 3–4 ms. Windows thread CPU timing is quantized and
can include driver spinning, so it cannot establish a CPU bottleneck alone.
Background NVML sampling itself takes roughly 14–18 ms median and occasionally
over 100 ms; its driver overhead has not been isolated with an on/off experiment.

NVML definitions: [NVIDIA header](https://github.com/NVIDIA/go-nvml/blob/main/pkg/nvml/nvml.h)
and [API reference](https://docs.nvidia.com/deploy/pdf/NVML_API_Reference_Guide.pdf).

### Fullscreen stop and r15 correction

The half-resolution run stops at **15:37:16.744 UTC** with `Stop requested`
and normal SDK worker shutdown. All three runs end normally on the source
side. The archive has no viewer log or recorded Windows crash event; a
separate viewer crash cannot be proved or excluded from this evidence.

The plugin connected `layoutChanged(int)` directly to `stop_stream`. Slicer
5.12.4 emits that signal when maximizing a view as well as changing layouts.
This is a concrete source-side reason for session closure matching the reported
sequence. R15 retains streaming when the same first 3D view, renderer, camera
and OpenGL context survive. Hiding/replacing that source, closing the scene,
or recreating its context still stops safely. The context/source guard also
runs before each eye-pair capture; the native WGL context guard remains active.
Each stop now records its first reason in `STOP_REQUEST` and the module status.
Continued layout changes record `LAYOUT_CHANGED`.

The regression test uses the connected Slicer layout signal, maximizes and
restores the 3D view, changes between four-up and one-up layouts, and toggles
application fullscreen. It checks stereo color/depth, preserved desktop pixels
and two renders per frame after each transition. Removing the 3D source must
stop explicitly. These tests use Slicer with Mesa and a transport fixture;
Windows WGL/context sharing and actual viewer behavior during fullscreen still
require a target-PC check. R15 is a layout/lifecycle fix, not a claimed FPS fix.

For a further performance comparison, use the same SDK camera and volume
preset throughout and distinguish a cold GPU from sustained 86 °C operation.
No screenshot capture is needed.

## Earlier r13 report

The user confirmed correct orientation with r13. Two current field reports use
the accepted r13 DLL (`821fdf85…`) and the same initial app placement.

`QAROS-VTK-diagnostics-20261015-165856-906.zip`, latest session
`20261015-165429-435776`, confirms the requested 50% render scale:

| Quantity | Value throughout this session |
|---|---|
| Actual eye render | 960 × 540 per eye |
| Stream color | 1920 × 1080 per eye, GPU upscaled |
| Stream depth | 960 × 540 per eye |
| Desktop view | 726 × 451 |
| Renders / extra depth / desktop restore renders | 2 / 0 / 0 |

After an initial SDK readiness wait of about 45–47 ms, readiness waiting falls
to about 12–13 ms and the stream reaches 19–19.5 completed stereo frames/s.
Both eye renders initially take about 30 ms in total. Over the next minutes
this rises to about 48 ms; the final full minute averages 13.39 stereo frames/s,
48.42 ms color rendering and 53.93 ms Python frame wall time. Copy calls remain
around 3–4 ms; SDK waiting at the end is about 14–17 ms. The session stops
normally after 3,742 stereo frames, without a reported bridge error.

The slowdown is real and occurs predominantly inside the host rendering calls.
Resolution and explicit render count do not increase. These wall-clock calls
include driver waits and GPU work, so they are not isolated GPU execution times.
The old five-second logs do not capture foreground changes, hardware clocks,
temperatures, within-window cadence or SDK eye poses. They cannot establish
whether focus, scene/view changes, shared GPU load, power/thermal limits or
another source of waiting causes the reported 11/15 FPS oscillation.

## r14 diagnostic experiment

Rendering, SDK transforms/layout and native DLL are unchanged. No performance
fix is claimed. The Python log now records one-second summaries and a bounded
sequence of frame samples; old timing/resolution keys remain available.
`SAMPLE_COLUMNS` describes each sample:

| Column | Meaning |
|---|---|
| serial | Stereo frame serial |
| start_monotonic_s | Start time on the process monotonic clock |
| start_interval_ms | Time since previous frame start, including across log windows |
| color_render_ms | Wall time inside both color Render calls |
| host_render_thread_cpu_ms | Render-thread CPU time for all host renders; includes driver CPU work/spinning, excludes sleeping waits; not GPU time |
| foreground_pid | Foreground process at frame start, null if unavailable |
| sdk_eye_changed | Whether SDK position/orientation/FOV changed since the previous frame (rounded to six decimals) |

One-second records also include last SDK eye poses/FOV and a read-only snapshot
of volume sampling, crop, input dimensions/spacing, property/input modification
times and object matrix. The snapshot is taken after restoring desktop state.
During the single-pass eye renders AutoAdjustSampleDistances is temporarily off
and ImageSampleDistance is 1, as in r12/r13; the snapshot shows restored values.
At most 512 per-frame samples are retained per window; `samples_omitted` reports
any overflow, while means/counts still cover every frame.

`host-system.log` samples optional NVIDIA NVML counters once per second on a
separate thread: clocks, temperature, power, performance state, clock-event
reason mask, utilization and device memory. It also records foreground PID and
executable basename (no window titles), cumulative process CPU seconds, a
monotonic timestamp and the sampler's own elapsed time. GPU figures describe
the entire device, not Slicer's share. Unsupported values are null, with NVML
error codes. Missing NVML does not prevent streaming or focus logging. The
monitor's start/stop never waits for a driver query on the render thread.

NVML reference: https://docs.nvidia.com/deploy/nvml-api/latest/index.html

`COLLECT-LOGS.cmd` includes the matching `host-system-N.log` files alongside
host Python/native logs. No screenshots, pixel readback, GPU timing queries,
new synchronization, driver-setting changes or external monitoring processes
are introduced. Diagnostic overhead is not assumed to be zero; CPU wall times
and the sampler's own duration are retained for assessment.

For the next run keep render scale at 50%, use one unchanged scene/view and
keep both windows visible if possible. Leave Visualizer active for 20–30 s,
Slicer active for 20–30 s, then repeat. Stop after 2–3 minutes, wait for Start
to become available and collect logs. Avoid rotating/zooming or changing the
preset during this specific run, so view cost and focus can be distinguished.

Tests cover optional/unavailable NVML, 64-bit handles/counters, balanced NVML
shutdown after partial initialization, nonblocking stop during a deliberately
stalled GPU query, frame cadence across log boundaries and the Windows focus
API. The official Slicer smoke test also checks the diagnostic snapshot against
its CT fixture and continues checking color/depth and state preservation.
Actual NVIDIA telemetry and its overhead still require the target-PC run.
