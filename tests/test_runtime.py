"""Exercise real child-process start/stop, identity stdin and SDK mismatch rejection."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integrations"))
from qarvtk_runtime import Backend, verify_sdk
from fixture_support import make_sdk, wait_ready

EXECUTABLE = Path(sys.argv.pop(1)).resolve()


class RuntimeTest(unittest.TestCase):
    def test_lifecycle_and_isolation(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = make_sdk(directory)
            first, second = Backend(scene_suffix=".vtm"), Backend()
            try:
                for index, backend in enumerate((first, second)):
                    backend.scene.write_text("fixture surface")
                    identity = Path(directory) / ("id" + str(index))
                    backend.start(EXECUTABLE, sdk, pairing_code="not-in-log", identity=identity)
                    wait_ready(backend)
                    self.assertEqual(identity.read_text(), "fixture-id\n")
                    self.assertNotIn("not-in-log", backend.log_tail())
                generation = first.directory / "scene-data-test"
                generation.mkdir(); (generation / "private-data.vti").write_text("synthetic image")
                first.close()
                self.assertFalse(generation.exists())
                self.assertEqual(first.process.returncode, 0)
                self.assertFalse(first.forced_stop)
                self.assertFalse(first.scene.exists())
                self.assertTrue(second.running)
                # A second Start uses the retained identity, and no stale stop file.
                first.scene.write_text("another surface")
                first.start(EXECUTABLE, sdk, identity=Path(directory) / "id0")
                wait_ready(first)
                first.request_stop()
                first.process.wait(timeout=5)
                self.assertEqual(first.poll(), 0)
                (sdk / "include" / "qar_streaming.h").write_text("wrong SDK")
                with self.assertRaisesRegex(ValueError, "does not match"):
                    verify_sdk(EXECUTABLE, sdk)
            finally:
                first.close()
                second.close()


if __name__ == "__main__":
    unittest.main()
