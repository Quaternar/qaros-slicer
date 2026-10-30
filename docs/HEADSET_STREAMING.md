# Slicer Live r8.7: change receivers during streaming

Open QAROS Live Renderer and allow its connection in the Hub if requested.
The plugin keeps one connection with the fixed app tag
`slicer.qaros.quaternar` while the module is in use.

1. Connect headsets and start any Visualizer receivers in QAROS. Keep QAROS
   and headset players on the same version. **New Visualizer receiver** creates
   and starts one from Slicer, including when the list is empty. Visualizer
   must be running with Settings > General > Enable MCP endpoint enabled.
   A created receiver is offered in the list for explicit selection.
2. Check the intended devices in **Stream to**. Several headsets of the same
   type and several Visualizer receivers can be selected together, up to
   eight receivers total. Identical labels receive a short peer-id suffix.
3. Select at least one connected receiver. **Start live streaming** stays
   disabled while there is no connected selection. A missing headset does
   not prevent streaming to a checked connected Visualizer.
4. Start streaming. Each selected receiver gets its own stereo view of each
   source workspace; image-only slice panels are sent to those same receivers.
5. Change the checkboxes while streaming to add or remove receivers. Other
   selected receivers continue; unchecked targets retire on a worker. Uncheck
   all to pause delivery, then check a connected receiver to resume.
   **Stop** closes all senders and
   workspaces but keeps the Hub connection and app tag. **Disconnect** closes
   that connection. Do not add a new source for every Start.

Check marks follow exact peer ids and survive list reordering. Disconnected
receivers remain visible when previously selected. An offline target already
requested by a running stream can be unchecked; a new offline target cannot
be selected. A newly created receiver with the same display name is a different
peer and is not selected automatically. If a streaming receiver disconnects,
the SDK reconnects to that same peer; another checked receiver can continue.
If a receiver is replaced, uncheck the old one and check its replacement.

With N selected receivers and V source volumes the plugin creates N*V senders.
Each sender owns an independent SDK WGL context. One frame lease and the
existing fair scheduler serialize use of Slicer's GUI renderer, so adding
headsets increases the rendering work. No performance target is implied.

To test, check two headsets, verify the independent views on both, then check
an additional Visualizer without stopping. Uncheck one target and verify the
other continues. Repeat Stop/Start and confirm that the source tag
remains the same. Disconnect one headset and verify the list and the other
receiver. A physical multi-headset test is distinct from the automated
selection and routing tests recorded in [VALIDATION.md](VALIDATION.md).
