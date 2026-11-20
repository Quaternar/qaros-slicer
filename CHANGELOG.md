# Changelog

Changes to qaros-slicer that a user sees or must act on. Format:
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). A merge to `main` that changes
behaviour is released at once: CI moves the `Unreleased` entries under the new version (see
[README.md](README.md#versioning)). Categories, in this order, only those with entries:
`Breaking`, `Features`, `Enhancements`, `Bug fixes`, `Deprecated`, `Removed`, `Security`.

## [Unreleased]

### Features

- Slicer Live r8.11 embeds Slicer's native sequence playback and seek widgets
  in the slice controls panel, with sequence selection and Stop. Slice sliders
  follow up to eight open slice views. Qt capture is driven by widget updates;
  unchanged controls use no repeated image uploads. The Qt status includes
  input counters for troubleshooting headset gestures. Receiver-scoped input
  and native callback fixes are described in the
  [video-panel input reproduction](docs/QAROS_VIDEO_PANEL_INPUT.md).

- Slicer Live r8.10 adds a Qt **Slice controls** panel with Red, Yellow and Green
  sliders and millimetre values. AR gestures reach Qt widgets through qaros-qt,
  and values synchronize with Slicer's own slice offset sliders. Enable
  **Show Qt slice controls in glasses** together with slice image panels before
  starting a stream. The prebuilt package includes a separate bridge for
  Slicer's Qt 5.15.2.

### Enhancements

- Support the installed QAROS 0.10.0-rc.4.16 runtime in Slicer Live r8.9. The
  Windows updater installs the matching bridge and Python module, and saved
  older SDK paths migrate to the installed runtime. The QAROS submodule is
  pinned to the public header shipped with that runtime.

### Bug fixes

- Slicer Live r8.15 adds a release guard using fresh SDK fingertip data
  when the finger withdraws, suppresses further updates from that old contact,
  and allows another contact without leaving the panel. Contacts remain separate
  for each receiver and hand. Cached or unavailable tracking does not invent a
  release; sampling runs only for outstanding drags on the existing panel worker.
  Live release/latency validation remains open.

- Slicer Live r8.14 coalesces pending hover/drag positions for every slice and
  playback control while preserving press/release/cancel boundaries. Slow GUI
  work no longer requires replaying all obsolete movement samples. The native
  time slider has a larger touch area and loads its selected volume on release,
  avoiding repeated proxy-volume replacements during scrubbing.

- Slicer Live r8.13 includes the selected receivers' internal mixers in the
  controls panel audience, so restricted panels can receive gestures on the
  installed QAROS runtime. Retained panels discard obsolete audience entries.
  Dragging presses Qt at the original contact before following the current
  fingertip, preventing tiny page steps when recognition starts after movement.
  A temporary SDK pause (602) now waits for recovery instead of closing all
  panel streams permanently. Qt calls retain the Python GIL so native mouse
  events can safely invoke PythonQt slots; Stop previously raised an access
  violation and removed the controls panel. Status distinguishes SDK drag
  phase from pressed Qt sliders. Physical headset validation remains open.

- Slicer Live r8.12 explicitly attaches the current video stream and restores
  visibility when it reuses a GUI panel. A retained panel could otherwise show
  black content after an interrupted Slicer session while the 3D volume kept
  streaming. The change applies to slice previews and Qt controls at startup;
  the render and capture loops are unchanged.
