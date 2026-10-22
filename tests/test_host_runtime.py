"""Control-flow regression tests without a Windows SDK or graphics context."""
import ctypes as c
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integrations" / "slicer"))
from qarvtk_host_runtime import (EXPECTED_SDK, HUB_2927_SDK, HUB_SDKS, SESSION_2540_SDK, LEGACY_SDK, Frame, HostTimingLog,
                                HostTransport, library, default_sdk_path,
                                installed_sdk_path, default_host_dll_path,
                                validate_sdk, validate_host_sdk, validate_target_mode)


class FakeDll:
    def __init__(self):
        self.polls = self.destroys = self.stops = 0
        self.finished = []

    def qarvtk_host_poll(self, handle, out):
        self.polls += 1
        frame = c.cast(out, c.POINTER(Frame)).contents
        frame.serial = self.polls
        frame.near_plane, frame.far_plane = .025, 10.
        for eye in frame.eyes:
            eye.width, eye.height = 640, 480
            eye.depth_width, eye.depth_height = 320, 240
        return 1

    def qarvtk_host_finish(self, handle, serial):
        self.finished.append(serial)
        return 0

    def qarvtk_host_status(self, handle, text, size):
        text.value = b"fixture status"

    def qarvtk_host_stop(self, handle):
        self.stops += 1

    def qarvtk_host_destroy(self, handle):
        self.destroys += 1
        return 1


class HostRuntimeTest(unittest.TestCase):
    def test_old_bridge_cannot_silently_claim_14_inch_panels(self):
        transport = object.__new__(HostTransport)
        transport.dll = MagicMock()
        transport.dll._qarvtk_info = {"host_abi": 5}
        with self.assertRaisesRegex(RuntimeError, "r8.3 host bridge"):
            transport.panel_create("Slicer Red", (640, 360), (0., 0., -1.5), .31)
        transport.dll.qarvtk_host_panel_create.assert_not_called()

    def test_headset_modes_require_current_target_discovery(self):
        for info in ({}, {"multi_target": 0}, {"multi_target": 1},
                     {"multi_target": 1, "target_discovery": 2},
                     {"multi_target": 1, "target_discovery": 3}):
            dll = type("Dll", (), {"_qarvtk_info": info})()
            for mode in ("visualizer", "quest"):
                validate_target_mode(dll, mode)
            for mode in ("both", "headset"):
                if info.get("multi_target") == 1 and info.get("target_discovery") in (2, 3):
                    validate_target_mode(dll, mode)
                else:
                    with self.assertRaisesRegex(RuntimeError, "multi-target host bridge"):
                        validate_target_mode(dll, mode)
            with self.assertRaises(ValueError):
                validate_target_mode(dll, "unknown")

    def test_sdk_mismatch_is_rejected_before_native_creation(self):
        for compiled in (EXPECTED_SDK, HUB_2927_SDK, SESSION_2540_SDK, LEGACY_SDK):
            dll = type("Dll", (), {"_qarvtk_info": {"header_sha256": compiled}})()
            validate_host_sdk(dll, compiled)
            other = LEGACY_SDK if compiled == EXPECTED_SDK else EXPECTED_SDK
            with self.assertRaisesRegex(RuntimeError, "do not mix SDK files"):
                validate_host_sdk(dll, other)

    def test_sdk_package_requires_header_runtime_and_launcher(self):
        root = Path(__file__).resolve().parents[1]
        for fixture, expected in (("hub-55e6e3f4", EXPECTED_SDK), ("hub-a32cecd4", HUB_2927_SDK), ("session-c709c479", SESSION_2540_SDK),
                                  ("session-022c40b6", LEGACY_SDK)):
            with tempfile.TemporaryDirectory() as folder:
                sdk = Path(folder)
                with self.assertRaisesRegex(RuntimeError, "Unsupported or missing"):
                    validate_sdk(sdk)
                (sdk / "include").mkdir()
                (sdk / "include/qar_streaming.h").write_bytes(
                    (root / "ci/sdk-headers" / fixture / "include/qar_streaming.h").read_bytes())
                (sdk / "bin").mkdir()
                with self.assertRaisesRegex(RuntimeError, "qar-streaming-c.dll"):
                    validate_sdk(sdk)
                (sdk / "bin/qar-streaming-c.dll").touch()
                with self.assertRaisesRegex(RuntimeError, "qar-runtime-launcher.exe"):
                    validate_sdk(sdk)
                (sdk / "bin/qar-runtime-launcher.exe").touch()
                if expected in HUB_SDKS:
                    with self.assertRaisesRegex(RuntimeError, "device-catalog.jsonc"):
                        validate_sdk(sdk)
                    (sdk / "bin/device-catalog.jsonc").write_text("{}")
                self.assertEqual(validate_sdk(sdk), expected)

    def test_saved_sdk_migration_requires_matching_new_header(self):
        fingerprints = {"old": LEGACY_SDK, "old-hub": HUB_2927_SDK, "portable": SESSION_2540_SDK, "new": EXPECTED_SDK,
                        "custom-new": EXPECTED_SDK, "future": "unsupported-new-header"}
        with patch("qarvtk_host_runtime.sdk_fingerprint", side_effect=fingerprints.get):
            self.assertEqual(default_sdk_path("old", "new"), "new")
            self.assertEqual(default_sdk_path("custom-new", "new"), "custom-new")
            self.assertEqual(default_sdk_path("old", "missing"), "old")
            self.assertEqual(default_sdk_path(None, "new"), "new")
            self.assertEqual(default_sdk_path("portable", "new"), "new")
            self.assertEqual(default_sdk_path("old-hub", "new"), "new")
            self.assertEqual(default_sdk_path("old", "future"), "future")

    def test_registered_installation_uses_machine_64_bit_registry(self):
        registry = MagicMock()
        registry.HKEY_LOCAL_MACHINE = 1
        registry.KEY_READ, registry.KEY_WOW64_64KEY = 2, 4
        registered = r"C:\Program Files\Quaternar\QAROS"
        registry.QueryValueEx.return_value = registered, 1
        with patch.dict(sys.modules, {"winreg": registry}):
            self.assertEqual(installed_sdk_path(), registered)
        registry.OpenKey.assert_called_once_with(1, r"SOFTWARE\Quaternar\QAROS", 0, 6)
        with patch("qarvtk_host_runtime.installed_sdk_path", return_value=registered), \
             patch("qarvtk_host_runtime.sdk_fingerprint", side_effect=lambda path: EXPECTED_SDK if path == registered else LEGACY_SDK):
            self.assertEqual(default_sdk_path(r"D:\install"), registered)

    def test_only_an_intact_update_overrides_the_saved_dll(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            dll = root / "Release/qarvtk-host.dll"
            dll.parent.mkdir()
            dll.write_bytes(b"new-build-fixture")
            self.assertEqual(default_host_dll_path("old.dll", dll), "old.dll")
            info = dict(sdk_header_sha256=EXPECTED_SDK, files={"Release/qarvtk-host.dll": hashlib.sha256(dll.read_bytes()).hexdigest()})
            (root / "HOST_UPDATE_INFO.json").write_text(json.dumps(info))
            self.assertEqual(default_host_dll_path("old.dll", dll), str(dll))
            dll.write_bytes(b"restored-old-DLL")
            self.assertEqual(default_host_dll_path("old.dll", dll), "old.dll")

    def test_old_placement_abi_is_rejected_before_binding_create(self):
        for abi, size in ((2, 64), (3, 112), (4, 112)):
            old_info = dict(host_abi=abi, header_sha256=EXPECTED_SDK, modern_api=0,
                            eye_size=104, frame_size=232, placement_size=size)
            old_dll = type("OldDll", (), {"qarvtk_host_info": staticmethod(lambda: json.dumps(old_info).encode())})()
            with patch("qarvtk_host_runtime.c.CDLL", return_value=old_dll):
                with self.assertRaisesRegex(RuntimeError, "ABI or SDK fingerprint"):
                    library("old-host.dll")

    def test_old_runtime_cleanup_is_rejected_even_with_current_abi(self):
        # r6 has the correct frame ABI and multi-target support but still calls
        # runtime_destroy at Stop. Do not let updated Python use that old DLL.
        for capability in (None, 0):
            info = dict(host_abi=5, header_sha256=EXPECTED_SDK, modern_api=0, multi_target=1,
                        eye_size=104, frame_size=240, placement_size=112, volume_size=120)
            if capability is not None:
                info["runtime_handle_only"] = capability
            old_dll = type("OldDll", (), {"qarvtk_host_info": staticmethod(lambda: json.dumps(info).encode())})()
            with patch("qarvtk_host_runtime.c.CDLL", return_value=old_dll):
                with self.assertRaisesRegex(RuntimeError, "shared-runtime cleanup fix"):
                    library("old-cleanup-host.dll")

    def test_dev3008_requires_presence_discovery_before_binding_create(self):
        for discovery in (None, 2):
            info = dict(host_abi=5, header_sha256=EXPECTED_SDK, modern_api=0, multi_target=1,
                        runtime_handle_only=1, eye_size=104, frame_size=240,
                        placement_size=112, volume_size=120, target_discovery=discovery)
            dll = type("Dll", (), {"qarvtk_host_info": staticmethod(lambda: json.dumps(info).encode())})()
            with patch("qarvtk_host_runtime.c.CDLL", return_value=dll):
                with self.assertRaisesRegex(RuntimeError, "peer presence discovery"):
                    library("no-presence-host.dll")

    def test_preview_failure_preserves_stereo_and_restores_context(self):
        with tempfile.TemporaryDirectory() as folder:
            events = []
            t = self.transport(folder, lambda *args, **kwargs: None)
            t.host.window = type("Window", (), {"MakeCurrent": lambda self: events.append("context")})()
            def fail(): raise RuntimeError("Preview-only failure")
            t.panels = type("Panels", (), {"tick": staticmethod(fail), "close": staticmethod(lambda: events.append("closed"))})()
            t.tick()
            self.assertEqual(t.dll.finished, [1])
            self.assertFalse(t.stopping)
            self.assertIsNone(t.panels)
            self.assertEqual(events, ["closed", "context"])
            t.tick()
            self.assertEqual(t.dll.finished, [1, 2])
            t.request_stop()

    def transport(self, folder, render):
        # Bypass Windows/SDK creation; exercise the actual poll/render/finish
        # lifecycle with a render callback that emulates nested Qt events.
        transport = HostTransport.__new__(HostTransport)
        transport.dll = FakeDll()
        transport.handle = c.c_void_p(1)
        transport.frame = Frame()
        transport.stopping = transport._ticking = False
        transport.last_status = "fixture"
        transport.diagnostics = HostTimingLog(folder)
        transport.host = type("Host", (), {"last_timings": {"frame_wall_ms": 10.},
                                           "name": "Fixture",
                                           "last_render_sizes": [(320, 240)]*2, "last_desktop_size": (800, 600),
                                           "render": staticmethod(render), "close": staticmethod(lambda: None)})()
        transport.hosts = [transport.host]
        return transport

    def test_frames_route_by_workspace_and_all_hosts_close(self):
        with tempfile.TemporaryDirectory() as folder:
            rendered, closed = [], []
            t = self.transport(folder, lambda *args, **kwargs: rendered.append(0))
            second = type("Host", (), dict(name="Second", last_timings={}, last_render_sizes=[(320, 240)]*2,
                last_desktop_size=(800, 600), render=staticmethod(lambda *a, **k: rendered.append(1)),
                close=staticmethod(lambda: closed.append(1))))()
            t.host.close = lambda: closed.append(0)
            t.hosts.append(second)
            for index in (0, 1, 1, 0):
                t.frame.volume_index = index
                t.tick()
            self.assertEqual(rendered, [0, 1, 1, 0])
            self.assertEqual(t.dll.finished, [1, 2, 3, 4])
            self.assertEqual(t.diagnostics.volume_frames, {0: 2, 1: 2})
            t.request_stop()
            t.tick()
            self.assertEqual(closed, [0, 1])

    def test_invalid_workspace_never_renders_or_submits(self):
        with tempfile.TemporaryDirectory() as folder:
            t = self.transport(folder, lambda *a, **k: self.fail("Rendered invalid workspace"))
            t.frame.volume_index = 2
            with self.assertRaisesRegex(RuntimeError, "unknown workspace"):
                t.tick()
            self.assertEqual(t.dll.finished, [])
            self.assertEqual(t.dll.stops, 1)
            t.tick()
            self.assertFalse(t.running)

    def test_two_targets_render_same_workspaces_with_their_own_eye_pose(self):
        with tempfile.TemporaryDirectory() as folder:
            rendered = []
            def render(volume):
                return lambda eyes, *a, **k: rendered.append((volume, eyes[0]["position"][0], eyes[1]["position"][0]))
            t = self.transport(folder, render(0))
            second = type("Host", (), dict(name="Second", last_timings={}, last_render_sizes=[(320, 240)]*2,
                last_desktop_size=(800, 600), render=staticmethod(render(1)), close=staticmethod(lambda: None)))()
            t.hosts.append(second)
            for target, volume in ((0, 0), (1, 0), (1, 1), (0, 1)):
                t.frame.volume_index = volume
                for eye_index, eye in enumerate(t.frame.eyes):
                    eye.position[0] = target * 10 + eye_index
                t.tick()
            self.assertEqual(rendered, [(0, 0, 1), (0, 10, 11), (1, 10, 11), (1, 0, 1)])
            self.assertEqual(t.dll.finished, [1, 2, 3, 4])
            t.request_stop()

    def test_nested_timer_cannot_overwrite_live_frame(self):
        with tempfile.TemporaryDirectory() as folder:
            def render(eyes, consumer, near, far):
                self.assertEqual((near, far), (.025, 10.))
                self.assertEqual(eyes[0]["depth_size"], (320, 240))
                serial = consumer.frame.serial
                consumer.tick()
                self.assertEqual(consumer.frame.serial, serial)
            t = self.transport(folder, render)
            t.tick()
            self.assertEqual(t.dll.polls, 1)
            self.assertEqual(t.dll.finished, [1])
            t.tick()
            self.assertEqual(t.dll.finished, [1, 2])
            t.request_stop()
            record = Path(folder, "host-python.log").read_text().split("PERF_HOST ")[1].splitlines()[0]
            metrics = json.loads(record)
            self.assertEqual(metrics["frames"], 2)
            self.assertEqual(metrics["last_serial"], 2)
            self.assertEqual(metrics["eye_sizes"], [[640, 480], [640, 480]])
            self.assertEqual(metrics["render_sizes"], [[320, 240], [320, 240]])
            self.assertEqual(metrics["depth_sizes"], [[320, 240], [320, 240]])
            self.assertEqual(metrics["desktop_size"], [800, 600])
            self.assertEqual(metrics["frame_wall_ms"], 10.)

    def test_nested_stop_cannot_destroy_transport_inside_render(self):
        with tempfile.TemporaryDirectory() as folder:
            def render(eyes, consumer, near, far):
                consumer.request_stop()
                consumer.tick()
                self.assertEqual(consumer.dll.destroys, 0)
                raise RuntimeError("render cancelled by observer")
            t = self.transport(folder, render)
            with self.assertRaisesRegex(RuntimeError, "cancelled by observer"):
                t.tick()
            self.assertEqual(t.dll.stops, 1)
            self.assertFalse(t._ticking)
            self.assertEqual(t.dll.finished, [])
            t.tick()
            self.assertEqual(t.dll.destroys, 1)
            self.assertFalse(t.running)
            self.assertIn("render cancelled by observer", Path(folder, "host-python.log").read_text())

    def test_first_stop_reason_survives_repeated_stop_and_worker_shutdown(self):
        with tempfile.TemporaryDirectory() as folder:
            t = self.transport(folder, lambda *args, **kwargs: None)
            t.request_stop("Slicer scene closing")
            t.request_stop("Module cleanup or application exit")
            self.assertIn("Slicer scene closing", t.status())
            t.tick()
            self.assertFalse(t.running)
            self.assertIn("Slicer scene closing", t.status())
            self.assertEqual(t.dll.stops, 1)
            events = [json.loads(line.split("STOP_REQUEST ")[1])
                      for line in Path(folder, "host-python.log").read_text().splitlines()
                      if "STOP_REQUEST " in line]
            self.assertEqual(events, [{"reason": "Slicer scene closing"}])


if __name__ == "__main__":
    unittest.main()

