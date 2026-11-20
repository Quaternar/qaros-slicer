# Video-panel input reproduction

Observed on 2026-11-20 with QAROS `0.10.0-rc.4.16+793871a8`, Slicer
5.12.4 / Qt 5.15.2 and the matching public C SDK at `d90dd6a3`.

The streamed Qt controls are visible and update when changed in Slicer.
The Visualizer's simulated hand shows a contact ring on the content, but
the source receives no panel gestures, so sliders and playback controls
cannot respond to that contact.

## Steps and evidence

1. Create a mono RGBA video sender and a GUI panel using
   `QarGuiPanelStreamIdExt`. On reuse, explicitly bind the current sender
   with `qar_gui_panels_set_stream`; confirm decoded pixels are visible.
2. Subscribe with `qar_gui_panels_subscribe_all_gesture_events`, keeping
   its cancellation token and callback context alive. Registration succeeds.
3. In a Visualizer receiver, enable the simulated hand with H and use
   Click / drag. Hold over a slider for four seconds, then release. The
   content contact ring is visible, but the source callback is never called.
4. A 35-second `viz_target_poses(include_joints=true)` measurement recorded
   401 samples. The index fingertip reached 15.1 mm behind the panel plane
   for 34 samples; reach alone does not explain the missing input.
5. `viz_hand_targets(kinds=["gui_panel"])` reports no events for the Qt
   video panel. The source counts callbacks before parsing or audience
   filtering: received, accepted, filtered and invalid all remain zero.
6. Temporarily hide the video panel and place a small website panel at the
   same pose and physical size. The same simulated hand produces hover
   started/updated events for that website panel. Restore the video panel:
   no events are reported for it.

The Qt production input path separately passes slider dragging and button
activation with copied SDK gestures. This isolates the missing live events
from the widget's ability to respond, without claiming an internal runtime
cause. Website hover is the control observation; website dragging was not
validated by this check. A blue contact ring alone is not proof that a GUI
gesture was published.

A temporary comparison also used CPU video ingress for the controls, as
the official qaros-qt backend does, instead of this host's OpenGL ingress.
Decoded controls were visible, but the callback and GUI-panel telemetry
still received zero events during the simulated-hand drag. The CPU variant
was removed. This does not exclude a registration or integration problem;
the sender's graphics ingress alone did not resolve the observation.

A second OpenGL check created the controls at a previously unused panel
index, with a newly initialized audience containing only the active test
receiver. The controls were decoded, but neither the subscription counter
nor GUI-panel telemetry reported gestures during the drag. Reusing the old
panel identity is therefore not required for the observed failure.

## Separate restart fix

The black-panel issue after an interrupted source is addressed in Slicer
Live r8.12. `get_or_create` can retain an old video stream: explicitly
rebinding the current stream restored all four decoded panel images with
their existing identities. That image fix did not resolve this input issue.

No MCP polling or synthetic touch injection has been added to the plugin.
MCP was used only for this diagnosis. The next required check is why the
runtime emits no GUI gestures for the video content despite the contact,
then a real hand test in the headset. Keep the integration on its test
branch until that check succeeds.
