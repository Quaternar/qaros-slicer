# Host integrations

## Slicer: direct rendering only

`integrations/slicer/QarosLive.py` is the only registered Slicer module.
It uses Slicer's live view and a VTK/Qt-free `qarvtk-host.dll` transport.
The private `QarosVTKLib` directory is a support package, not a second module.

The separate-renderer Slicer module (`QarosVTK.py`) and its scene-export helper
were retired on 2026-10-22. They are absent from source, extension registration,
Python packages and the current Windows Live installer. ANARI remains a separate
renderer route; retirement does not add ANARI capture support to the VTK Live path.

Use [INSTALLATION.md](INSTALLATION.md). The Windows installer handles both fresh
installs and upgrades, with backup and removal of retired files in managed paths.
For Python-only packaging, run `python3 scripts/package_plugins.py`.

Host tests use the actual Slicer application and its bundled VTK:

```bash
python3 scripts/package_plugins.py
python3 tests/test_plugin_packaging.py
Slicer --no-splash --ignore-slicerrc --additional-module-paths "$PWD/package/QAROS-VTK-plugins/slicer" --python-script tests/slicer_host_render_smoke.py
Slicer --no-splash --ignore-slicerrc --additional-module-paths "$PWD/package/QAROS-VTK-plugins/slicer" --python-script tests/slicer_panels_smoke.py
Slicer --no-splash --ignore-slicerrc --additional-module-paths "$PWD/package/QAROS-VTK-plugins/slicer" --python-script tests/slicer_multivolume_smoke.py
```

For headless Linux prefix host commands with `xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1`.
These tests do not establish a real Windows QAROS/headset connection.

## ParaView

The direct Live integration is being prepared in
[PR #4](https://github.com/HonzaHovora/QAROS_VTK/pull/4), branch
`codex/paraview-live`. It is separate from this Slicer cleanup.

Main still contains the earlier surface-export prototype:
`integrations/paraview/QarosVTKPlugin.py`. It controls a standalone
`qarvtk-sender` via the shared process controller. The Slicer Live installer
does not install or invoke that sender.

To use the earlier prototype, package the Python files and load
`package/QAROS-VTK-plugins/paraview/QarosVTKPlugin.py` through ParaView's Plugin
Manager. Use a local built-in connection, add its QAROS Streaming filter, set
Sender Executable, SDK Folder and Metres Per Unit, then Apply and Start streaming.
The input pipeline must supply transforms; display-only changes are not exported.
If its SDK requires pairing, use `scripts/run_sender.py` in a terminal. Pairing
through the retired Slicer module is no longer available.

The optional snapshot helper includes the active view's display transform:

```python
import sys
sys.path.insert(0, r"C:/dev/QAROS_VTK/integrations/paraview")
import qarvtk_paraview
qarvtk_paraview.export_active(r"C:/data/paraview-surface.vtp")
```

`ExtractSurface` executes on the selected output and `Fetch` gathers geometry
to the client. Composite outputs are flattened; the temporary filter is deleted
and the active source restored. The streaming filter rejects remote/MPI sessions.
The standalone sender and preview build instructions are in [WINDOWS.md](WINDOWS.md).
They are development/ParaView tools and are not required by Slicer.

The process fixture is a test double, not a QAROS connection:

```bash
cmake -S . -B build-fixture -DQARVTK_BUILD_RENDERER=OFF
cmake --build build-fixture --parallel 2
export QARVTK_TEST_BACKEND="$PWD/build-fixture/qarvtk-process-fixture"
pvpython tests/paraview_smoke.py
pvpython tests/paraview_plugin_smoke.py
```
