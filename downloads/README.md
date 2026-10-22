# Slicer downloads

Use `qarvtk-live-dev3008.zip` through `UPDATE-LIVE-PLUGIN.cmd` in the repository
root. The current r8.4 installer supports fresh installation and migration from
the old Slicer exporter. It requires the complete installed QAROS dev.3008 SDK.

The current ZIP contains only QarosLive, its private Python support and
qarvtk-host.dll. It contains no standalone renderer or QarosVTK module.

The older combined `QAROS_VTK_Windows_*` archives are retained as historical
artifacts only. They contain the retired QarosVTK module: do not use them to
install Slicer. START-PREVIEW.cmd no longer extracts them. Their publish/store
jobs and combined packager are disabled so they cannot reintroduce the old
installation path. Older Live SDK archives are also retained for provenance;
they are not the installer for the current QAROS version.
