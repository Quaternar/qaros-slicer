#!/usr/bin/env python3
"""Build portable, self-contained Python plugin folders; never fetch/install an SDK."""
import argparse
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def package(output):
    output = Path(output).resolve()
    bundle = output / "QAROS-VTK-plugins"
    files = {
        "slicer/QarosLive.py": "integrations/slicer/QarosLive.py",
        "slicer/QarosVTKLib/qarvtk_host_view.py": "integrations/slicer/qarvtk_host_view.py",
        "slicer/QarosVTKLib/qarvtk_host_panels.py": "integrations/slicer/qarvtk_host_panels.py",
        "slicer/QarosVTKLib/qarvtk_host_gpu.py": "integrations/slicer/qarvtk_host_gpu.py",
        "slicer/QarosVTKLib/qarvtk_host_runtime.py": "integrations/slicer/qarvtk_host_runtime.py",
        "slicer/QarosVTKLib/qarvtk_host_diagnostics.py": "integrations/slicer/qarvtk_host_diagnostics.py",
        "slicer/QarosVTKLib/qarvtk_runtime.py": "integrations/qarvtk_runtime.py",
        "paraview/QarosVTKPlugin.py": "integrations/paraview/QarosVTKPlugin.py",
        "paraview/qarvtk_paraview.py": "integrations/paraview/qarvtk_paraview.py",
        "paraview/qarvtk_bridge.py": "integrations/qarvtk_bridge.py",
        "paraview/qarvtk_runtime.py": "integrations/qarvtk_runtime.py",
        "INSTALLATION.md": "docs/INSTALLATION.md",
        "KHRONOS-LICENSE.txt": "third_party/KHRONOS-LICENSE.txt",
    }
    for relative, source in files.items():
        destination = bundle / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / source, destination)
    init = "slicer/QarosVTKLib/__init__.py"
    (bundle / init).write_text('"""Private support package, not a Slicer module."""\n')
    # Remove only known obsolete generated files, so Slicer will not discover them
    # as separate modules after an upgrade from the first packaging layout.
    retired = ("QarosVTK", "qarvtk_slicer", "qarvtk_bridge", "qarvtk_scene", "qarvtk_runtime")
    for directory, names in ((bundle / "slicer", retired),
                             (bundle / "slicer/QarosVTKLib", retired[1:4])):
        for name in names:
            for old in [directory / (name + suffix) for suffix in (".py", ".pyc", ".pyo")]:
                if old.is_file():
                    old.unlink()
            for old in (directory / "__pycache__").glob(name + ".*.pyc"):
                old.unlink()
    archive = output / "QAROS-VTK-plugins.zip"
    # Explicit allowlist: no caches, SDKs, local credentials or extra user files.
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as stream:
        for relative in [*files, init]:
            path = bundle / relative
            stream.write(path, path.relative_to(output))
    print("Plugin folders: " + str(bundle))
    print("Archive: " + str(archive))
    return bundle


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "package")
    package(parser.parse_args().output)
