# QAROS report review: mixer startup exits and historical panel dumps

The 2026-10-20 report contains **different failure modes**. The current alert
is a loop of mixer exits with status `1`, not a recorded WebCore access
violation. Its historical dumps independently confirm yesterday's panel crash,
including the delayed, video-disabled manual test. Neither underlying cause is
fully identified by the available logs and release dumps.

Source: `QAROS-crash-report-20261020-143646.zip`, 19,175,334 bytes, SHA256
`9c15b749cd284cb6140f2a09478f2971f983f8c8f768ce167985a2e89dfce808`.
The archive has 42 entries and identifies QAROS Hub
`0.9.0-dev.2927+f291e404.demo`. It contains five October 19 GUI minidumps,
but no October 20 minidump, matching executable/DLL files, or PDB files.
Raw logs and dumps are not committed to this repository.

Times below are local Europe/Prague (UTC+02:00). Archive directory timestamps
ending in `Z` are UTC. Paths are relative to the report ZIP. For brevity:

- `L` = `launcher/10cb92b7-9a3e-455f-be37-94078af561c7/`.
- `M` = `mixer_db5c70a7-c7aa-4592-af28-de46efebf41b_stdout.log`.
- `V` = `streaming_viz_6c30dc43-3e50-5e29-b2d3-5ba4f8e13053_stdout.log`.

## Current failure: October 20, 14:35–14:36

`report.txt` says a mixer was restarted ten times within ten minutes. The
underlying log is more precise: `L/launcher_stdout.log` records **57 starts
and 56 completed exit observations in about 60 seconds**. All 56 exits have
`graceful=true crashed=false exitStatus=1`. Here `graceful=true` describes
the process lifecycle classification; status `1` is still an unsuccessful exit.

| Local time | Evidence | Location |
| --- | --- | --- |
| 14:35:45.950128 | First mixer process starts, PID 18960, using the installed `qar-stereokit-gui.exe`. | `L/launcher_stdout.log:2362` |
| 14:35:47.030705 | Last startup milestone: `joining session via onboarding (target/mixer path)`. Next lines say the service is no longer running and main leaves with exit code 1. | `L/M:3–6` |
| 14:35:47.738331 | First observed mixer stop, status 1, `crashed=false`. | `L/launcher_stdout.log:2495` |
| 14:35:55.921411 | Visualizer cannot obtain its stream: code 507, no peer acknowledges within 10 seconds across two attempts. | `L/V:1539` |
| 14:35:56.991308 | Watchdog reports `restart-storm`, ten restarts in its ten-minute window. Restarts continue. | `L/launcher_stdout.log:2635` |
| 14:36:46.132062 / .141104 | Last recorded stop, then the 57th start. PID 18960 is reused; this is not the first process continuing. | `L/launcher_stdout.log:3316,3319` |

The mixer stdout contains 57 copies of the startup/onboarding message and 57
main-exit messages with code 1; the last exit has not yet appeared in the
launcher lifecycle capture. Mixer stderr is empty. There is no
`Runtime: JoinOrOnboard`, session-initialized message, `BrowserControl` message,
or explicit error explaining the failed initialization in this current mixer
log. The internal mixer's `isSourceApp=false isTargetApp=false` flags do not
describe Slicer's source-app registration.

The five Visualizer receiver timeouts are consistent with an unavailable mixer;
this is an inference from the lifecycle, not proof that receiver creation
caused the loop. The Hub service stdout repeats the launcher's messages and
must not be counted as additional restarts.

### Comparison with the earlier successful startup

`L/previous-runs/20261020T123536Z/M` records the same mixer starting at 12:33:10:

1. At 12:33:11.130474 it logs the same target/mixer onboarding milestone
   (line 4), followed by two GPU enumeration messages (lines 5–6).
2. `Runtime: JoinOrOnboard` appears at 12:33:11.669569 (line 7).
3. Certificate renewal succeeds, onboarding completes, and the session is
   initialized at 12:33:11.853630 in 723 ms (lines 8–14, 114, 116).
4. Rendering continues through the last mixer log entry at 14:28:17.482280.

The current failing startup never emits even the first two GPU messages from
that sequence. This narrows the observable divergence to **early initialization
after the target/mixer onboarding branch is selected**, before session success
is logged. It does not establish that the enrollment server rejected a request,
that certificates expired, or that GPU initialization failed. The same day's
successful renewal is evidence against treating renewal warnings alone as a
diagnosis.

The source-application registry warning at 14:36:43.116765 says no stored
snapshot/mutation arrived and no configured source application is launched
(`L/launcher_stdout.log:3294`). **The same warning already occurs at
12:34:07.840701 in the earlier run with a functioning mixer**
(`L/previous-runs/20261020T123536Z/launcher_stdout.log:3729`). It is a separate
configuration observation, not an established explanation for the restart
loop. Stale peer-spec and query-route warnings also occur in the earlier run.
These findings do not justify deleting the user's persisted QAROS state.

## Historical panel crash: five matching minidumps

All five GUI dumps in `L/previous-runs/20261020T103302Z/` were opened with
LLDB 20. They consistently identify exception `0xC0000005` at
`0x00007FFA76DB26E0`, module base `0x00007FFA74010000`, hence
**`WebCore.dll +0x2DA26E0`**. In every dump `R14=4`, `RAX=4`, and `RSI=0`.
The captured faulting instruction reads one byte through `R14`
(`movzx ecx, byte ptr [r14]`): a read of the invalid address `0x4`.

| October 19 local time | GUI PID | Dump filename |
| --- | --- | --- |
| 17:16:38 | 14448 | `20261019-171638_qar-stereokit-gui_14448.dmp` |
| 17:23:04 | 11748 | `20261019-172304_qar-stereokit-gui_11748.dmp` |
| 17:33:40 | 30356 | `20261019-173340_qar-stereokit-gui_30356.dmp` |
| 18:17:03 | 20104 | `20261019-181703_qar-stereokit-gui_20104.dmp` |
| 18:34:47 | 25588 | `20261019-183447_qar-stereokit-gui_25588.dmp` |

The last dump is the newly available binary evidence for the
[delayed manual panel reproduction](PANEL_STARTUP_CRASH_20261019.md).
It is 1,369,655 bytes, SHA256
`c3472978022b92a38fc2109ac8d8365846dbc95412296d9c8bf6a62452c2e8cb`.
That run created only Red after about 30 seconds of two-volume streaming and
never enabled panel video or called panel begin/access/show-frame APIs.
Sender creation and local staging were still part of the test.

The debugger recovered only frame 0 in each dump. No validated caller stack
or internal object type is established. The repeated instruction/register
signature confirms the same fault site, not necessarily the same caller or
root cause. The dump records WebCore debug identifier
`66EA0355-3E61-4557-9EC9-AA7F70B88FE5`, age 4; this can help the developer
match the dependency's symbols. Exact matching module binaries may permit
further unwinding; matching symbols or a symbolized reproduction are needed
to identify named callers and source lines.

## Separate older launcher failures

Five supervisor reports under `qaros-hub-service/crashes/`, dated October 19
18:46:24 through 18:47:00, concern `qar-runtime-launcher`. Their actual exit
status is **`0xC0000142` (a DLL failed to initialize)**. The generic explanatory
paragraph also mentions fail-fast code `0xC0000409`; that is not their recorded
exit status. No corresponding launcher dump is included. These are neither
the WebCore read violation nor the current mixer status-1 exit loop.

## Concrete next work in the QAROS implementation

- **Current mixer startup:** inspect the path between the target/mixer
  onboarding log and the first runtime/GPU initialization logs. Preserve and
  log the actual returned failure, exception or cancellation reason before
  the service transitions to stopped and main returns 1. Compare the child
  configuration/lifetime with the successful 12:33 launch. The current report
  lacks this diagnostic; it cannot identify the failing operation.
- **Panel crash:** use the 18:34:47 dump and exact matching module files, or
  reproduce r4 manual step 1 under the matching symbolized QAROS build.
  Inspect dynamic panel construction/content dispatch and WebCore view/control
  lifetime before the first `BrowserControl` stream-connection message.
  Existing common-name and local-app onboarding checks are recorded in the
  [Vulkan example comparison](PANEL_STARTUP_CRASH_20261019.md#comparison-with-the-official-vulkan-source-after-developer-feedback).
- **Failure reporting:** distinguish nonzero normal process exits, access
  violations and DLL initialization failures; the current umbrella message
  obscures materially different evidence.

This review changes documentation only. It provides no new plugin binary,
requires no reinstall, and does not claim a crash fix. The QAROS GUI/runtime
implementation and matching module/symbol files are not available in this
workspace.
