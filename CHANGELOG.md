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
  input counters for troubleshooting headset gestures. Live input on the
  tested QAROS 0.10 runtime remains unresolved; see the
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

- Slicer Live r8.12 explicitly attaches the current video stream and restores
  visibility when it reuses a GUI panel. A retained panel could otherwise show
  black content after an interrupted Slicer session while the 3D volume kept
  streaming. The change applies to slice previews and Qt controls at startup;
  the render and capture loops are unchanged.
