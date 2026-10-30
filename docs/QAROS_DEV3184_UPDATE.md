# Slicer Live r8.7 with QAROS dev.3184

This update targets the complete installed **QAROS 0.9.0-dev.3184+2ccfb349**
on Windows x64, inspected on 2026-10-30. The registry and installed changelog
report this version. The exact header is pinned in
`ci/sdk-headers/hub-f3bbddfd/include/qar_streaming.h` with SHA-256
`f3bbddfdd4106a87bdcb114dc3a456b189b3f6a009a535e58592737d456a4f52`.

## Install

1. Stop streaming and close Slicer completely.
2. Update the checkout and run `UPDATE-LIVE-PLUGIN.cmd`.
3. Open Slicer and check **Live plugin r8.7** in QAROS Live Renderer.
4. Keep QAROS and headset players on the same version. This QAROS release
   changes app approvals and headset pairing; complete the Hub's prompts
   if requested. The plugin does not delete or replace QAROS identities.
5. Start Visualizer receivers and/or connect headsets. Check the specific
   connected receivers under **Stream to**, then start streaming. Several
   headsets of the same model can be checked together, up to eight total
   receivers. Offline receivers are disabled; a connected Visualizer can be
   used without a headset. Existing Additional module paths remain valid.

## Persistent connection and live selection in r8.7

Opening the module connects to the Hub once, with the explicit application tag
`slicer.qaros.quaternar`. The tag does not contain a date, PID, random id or
plugin version. **Start/Stop** reuses that connection and does not enroll a
new source. **Disconnect**, module cleanup or application exit closes it.
Old entries already present in QAROS are not deleted by this update.

The receiver list refreshes from the SDK subscription. Check marks are saved
by peer id, not by row position or display name. A replacement receiver is
never selected automatically because it has the same name or category.
Change the checkboxes while streaming to add or remove targets; existing
senders retain their routes and graphics contexts. Negotiation and retirement
run on workers. Unchecking all keeps the source idle until another receiver
is checked. An empty selection closes the SDK slice panels instead of passing
an empty audience (which the SDK would interpret as everyone).
A disconnected target that was
part of a running stream keeps its own SDK reconnect path; other selected
receivers are not replaced or redirected.

**New Visualizer receiver** creates and starts a receiver through Visualizer's
supported loopback MCP endpoint, even with no available receiver and during
streaming. It does not automatically select the new receiver. Visualizer must
be running and Settings > General > Enable MCP endpoint must be enabled. The
plugin respects that setting and reads its configured local port (default
17872). Creation runs off the GUI thread and is never retried automatically:
after a timeout, check the Visualizer list before creating another receiver.

Each selected receiver has its own stereo sender and shared WGL context per
source volume. The renderer still leases at most one frame at a time across
all receiver/volume routes. Slice panels are visible only to selected receivers
whose stream setup has completed.

The updater installs `downloads/qarvtk-live-dev3184.zip`. It verifies the
SDK fingerprint, archive checksum, ABI and payload hashes before replacing
files, and saves the previous plugin under `package/live-backup-*`.
It does not replace QAROS runtime files or copy them into Slicer.

## Compatibility change

The previous r8.4 plugin was pinned to the dev.3008 header and rejected this
new SDK. Both the Python fingerprint registry and native DLL now support the
dev.3184 header; the shipped DLL is compiled against these exact bytes.
Unknown headers and mismatched DLL/header combinations remain rejected.

The new header adds local app tags and session lifecycle information and
changes the co-process invite API. This plugin uses local-app enrollment
directly, not co-process invites or application-minted pairing codes, so no
new invite path is introduced. Peer presence discovery, volume/panel common
names, stereo OpenGL transport, physical size and handle-only teardown retain
their existing behavior. Source compatibility with the four older pinned
headers is retained; every SDK still requires its matching native DLL.

The update does not claim to repair the previously reported QAROS GUI faults.
See [VALIDATION.md](VALIDATION.md) for actual build and runtime checks.
