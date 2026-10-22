# Slicer Live r8.4: Visualizer and HoloLens

Connecting HoloLens to QAROS does not itself stream Slicer content. Slicer
creates a rendering sender for that target peer and each app volume.
One Visualizer receiver and one headset can receive independent stereo views
of the same workspaces. Image-only slice panels start with a 14-inch diagonal,
independent of model scale; resize them in QAROS as needed. R8.3 applies size
after registration, crops surrounding margins on the GPU and moves data names
to the title. Slicer corner labels are hidden while streaming and restored on Stop.

## Install and test

1. Close Slicer. Fetch/Pull `main` with r8.4 and run `UPDATE-LIVE-PLUGIN.cmd`. See the
   [installation guide](QAROS_DEV3008_UPDATE.md). Use matching QAROS dev.3008 and
   headset players; complete a new pairing or app approval if the Hub asks.
2. Connect HoloLens in QAROS to the same Hub session used by Slicer. For the
   first test, choose **Headset only** and do not create a Visualizer receiver.
3. Open the two-volume scene in Slicer. Leave **Separate QAROS workspace for
   each volume**, centering and slice previews enabled. Set **Model size** to
   **1x (1:1)**. Click **Start live streaming**, approving Slicer if requested.
4. The status should show **Headset**, two connected volumes and an increasing
   frame count. Two volumes in this mode create two senders. There should be
   no Visualizer target or Visualizer rendering sender in the log.
5. Check both volumes and visible slice panels in HoloLens. Move your head:
   perspective must follow it. Change a transfer function or ROI in Slicer;
   the change should appear in the glasses. Check the physical dimensions
   against the **Workspace** readout and the source data's millimetre spacing.
6. Stop, change Model size to 0.5x or 2x and Start. Dimensions should halve or
   double; 1:1 resets the slider. Centering alone must never change size.
7. Compare **Headset only** with **Visualizer + headset**, using the same
   scene, scale, panel settings and render resolution. The combined mode
   creates four senders for two volumes. Compare measured FPS/timings; no
   speedup is assumed before measurement. The ordinary Slicer desktop view
   still exists in both modes.
8. Test a late headset: start combined mode with just the Visualizer receiver,
   then connect HoloLens. Visualizer frames should continue during headset
   negotiation, and existing panels should become visible in the glasses.
9. Repeat Stop/Start three times with the same receivers. QAROS and target
   applications must remain running. Jan reported on 2026-10-22 that Lukas
   confirmed the GUI fault is in QAROS and is fixing it. This plugin update
   retains normal panels; it does not contain that fix. Verify connection
   behavior after Stop/Start separately.

## If the headset stays blank

The log folder is displayed in the module. In `host-bridge.log`, check:

- `Host build: installed-hub-update-r8.4` and `Target mode: headset` confirm
  the intended DLL and mode.
- `PEER_OBSERVED` reports target/headset peers, their role, category, presence
  and eligibility. If no headset target appears, check the Hub session and
  that its target app is connected, not only the physical device pairing.
- `TARGET_SELECTED type=Headset` means a headset was selected.
- `SENDER_CONNECT ... target=Headset ... app_volume=...` starts negotiation.
  `WORKSPACE_FAILED` gives a failure for that volume. No usable sender causes
  startup to end with an error; a discovered device alone is not success.
- `FIRST_FRAME` and growing frame counts confirm SDK submission. They do not
  independently prove the image is visible in the physical glasses.

Use a current attempt's log: previous sessions cannot establish why a new
attempt is blank. Do not reset all Hub data to collect this distinction.

## Selection and implementation

A target must advertise role `TARGET` and the requested category (`HEADSET`
or `VISUALIZER`). On dev.3008, r8.4 accepts `Online` and `Unknown` presence;
`Offline`, `Leaving` and `Retired` are excluded. Unrecognized future values
count as `Unknown`, per the SDK contract. AppState is not used for selection.
The legacy dev.2927 build retains its AppState fallback; it never loads
the new presence function. Multiple available targets of one category are reported
as ambiguous. Keep one of that category connected. A chosen peer stays pinned
until Stop; restart streaming after replacing a receiver.

The current official
[Vulkan source example](https://github.com/Quaternar/qaros/blob/83d808c9e89aa4596803a260c0c8d583b5520fe7/qar-streaming-c/examples/vulkan_cube/main.cpp)
subscribes before enumerating peers, selects targets and passes both
`QarRenderSenderInit.peer_id` and `app_volume_id`. The supplied dev.3008 header
also specifies presence for discovery and deprecates AppState as a readiness
signal. R8.4 follows that contract while leaving sender negotiation responsible
for connection. It does not establish resolution of earlier connection failures
or include the GUI fix being developed in QAROS.

Per-target startup is independent of frame delivery to another target. Each
sender has its own graphics context, camera pose and FOV. Headset-only uses
slot zero for the headset, never a Visualizer fallback. Panels gain a late
target after its senders finish connecting; their audience is never emptied
because the SDK treats an empty list as everyone. Panel interaction is not
implemented. Startup waits up to two minutes if no usable sender appears.

ABI 5 remains unchanged. Both headset and combined modes require metadata
`multi_target=1` and `target_discovery=3` for dev.3008 (`2` for dev.2927);
the loader also requires r7's
`runtime_handle_only=1` cleanup. Local selection, routing, cancellation,
physical-placement and installer tests cannot replace a Windows/NVIDIA/
HoloLens run. On 2026-10-21 the user confirmed r8 works, including the headset.
No measured performance comparison was supplied. R8.4 rebuilds the DLL for
the new SDK and retains r8.3's panel sizing/cropping. Its behavior in Windows,
appearance and physical size in the headset still need a user check.
