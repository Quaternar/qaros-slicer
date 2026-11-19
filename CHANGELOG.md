# Changelog

Changes to qaros-slicer that a user sees or must act on. Format:
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). A merge to `main` that changes
behaviour is released at once: CI moves the `Unreleased` entries under the new version (see
[README.md](README.md#versioning)). Categories, in this order, only those with entries:
`Breaking`, `Features`, `Enhancements`, `Bug fixes`, `Deprecated`, `Removed`, `Security`.

## [Unreleased]

### Enhancements

- Support the installed QAROS 0.10.0-rc.4.16 runtime in Slicer Live r8.9. The
  Windows updater installs the matching bridge and Python module, and saved
  older SDK paths migrate to the installed runtime. The QAROS submodule is
  pinned to the public header shipped with that runtime.
