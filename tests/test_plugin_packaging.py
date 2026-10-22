"""Exercise fresh packaging and upgrades from the retired Slicer exporter."""
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from package_plugins import package


class PluginPackagingTest(unittest.TestCase):
    def check_bundle(self, output):
        bundle = package(output)
        self.assertEqual({p.name for p in (bundle / "slicer").glob("*.py")}, {"QarosLive.py"})
        support = bundle / "slicer/QarosVTKLib"
        for name in ("view", "gpu", "runtime", "panels", "diagnostics"):
            self.assertEqual((support / ("qarvtk_host_" + name + ".py")).read_bytes(),
                             (ROOT / "integrations/slicer" / ("qarvtk_host_" + name + ".py")).read_bytes())
        self.assertTrue((support / "qarvtk_runtime.py").is_file())
        with zipfile.ZipFile(Path(output) / "QAROS-VTK-plugins.zip") as archive:
            self.assertIsNone(archive.testzip())
            slicer_files = {name.removeprefix("QAROS-VTK-plugins/slicer/")
                            for name in archive.namelist() if "/slicer/" in name}
            self.assertEqual({name for name in slicer_files if "/" not in name}, {"QarosLive.py"})
            for name in ("QarosVTK.py", "QarosVTKLib/qarvtk_slicer.py",
                         "QarosVTKLib/qarvtk_scene.py", "QarosVTKLib/qarvtk_bridge.py"):
                self.assertNotIn(name, slicer_files)
            self.assertTrue(any(name.endswith("paraview/QarosVTKPlugin.py") for name in archive.namelist()))
        return bundle

    def test_fresh_package_contains_only_the_live_slicer_module(self):
        with tempfile.TemporaryDirectory() as output:
            self.check_bundle(output)

    def test_upgrade_removes_only_retired_generated_files_and_caches(self):
        with tempfile.TemporaryDirectory() as output:
            base = Path(output) / "QAROS-VTK-plugins/slicer"
            retired = []
            for prefix, names in (("", ("QarosVTK", "qarvtk_slicer", "qarvtk_scene", "qarvtk_bridge", "qarvtk_runtime")),
                                  ("QarosVTKLib", ("qarvtk_slicer", "qarvtk_scene", "qarvtk_bridge"))):
                for name in names:
                    for suffix in (name + ".py", name + ".pyc", name + ".pyo", "__pycache__/" + name + ".cpython-310.pyc"):
                        path = base / prefix / suffix
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_text("retired module sentinel")
                        retired.append(path)
            user_file = base / "my-settings.json"
            user_file.write_text("preserve custom content")
            other_cache = base / "__pycache__/Unrelated.cpython-310.pyc"
            other_cache.write_text("preserve other cache")
            self.check_bundle(output)
            self.assertTrue(all(not path.exists() for path in retired))
            self.assertEqual(user_file.read_text(), "preserve custom content")
            self.assertEqual(other_cache.read_text(), "preserve other cache")
            self.check_bundle(output)  # Repeating the package operation is safe.


if __name__ == "__main__":
    unittest.main()
