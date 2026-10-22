"""Diagnostics must remain optional, bounded, and independent of render/stop latency."""
import ctypes as c
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integrations" / "slicer"))
from qarvtk_host_diagnostics import ForegroundWindow, NvidiaTelemetry, SystemMonitor, _Memory, _Utilization
from qarvtk_host_runtime import HostTimingLog


class Function:
    def __init__(self, callback):
        self.callback = callback

    def __call__(self, *args):
        return self.callback(*args)


class NvmlFixture:
    def __init__(self, fail_count=False):
        self.shutdowns = 0
        self.nvmlInit_v2 = Function(lambda: 0)
        self.nvmlShutdown = Function(self.shutdown)
        self.nvmlDeviceGetCount_v2 = Function(lambda out: 3 if fail_count else self.set(out, c.c_uint, 1))
        self.nvmlDeviceGetHandleByIndex_v2 = Function(lambda index, out: self.set(out, c.c_void_p, 0x123456789))
        self.nvmlDeviceGetName = Function(lambda handle, out, size: self.name(out))
        self.nvmlDeviceGetClockInfo = Function(lambda handle, domain, out: self.set(out, c.c_uint, (1200, 0, 6000)[domain]))
        self.nvmlDeviceGetTemperature = Function(lambda handle, sensor, out: self.set(out, c.c_uint, 75))
        self.nvmlDeviceGetPerformanceState = Function(lambda handle, out: self.set(out, c.c_uint, 0))
        self.nvmlDeviceGetPowerUsage = Function(lambda handle, out: 3)  # NOT_SUPPORTED, not zero watts
        self.nvmlDeviceGetCurrentClocksThrottleReasons = Function(lambda handle, out: self.set(out, c.c_ulonglong, 0x20))
        self.nvmlDeviceGetUtilizationRates = Function(self.utilization)
        self.nvmlDeviceGetMemoryInfo = Function(self.memory)

    def set(self, pointer, kind, value):
        c.cast(pointer, c.POINTER(kind))[0] = kind(value)
        return 0

    def name(self, out):
        out.value = b"Fixture GPU"
        return 0

    def memory(self, handle, out):
        self.assert_handle(handle)
        c.cast(out, c.POINTER(_Memory))[0] = _Memory(8 << 30, 3 << 30, 5 << 30)
        return 0

    def utilization(self, handle, out):
        c.cast(out, c.POINTER(_Utilization))[0] = _Utilization(96, 38)
        return 0

    def assert_handle(self, handle):
        assert handle.value == 0x123456789, "64-bit device handle was truncated"

    def shutdown(self):
        self.shutdowns += 1
        return 0


class HostDiagnosticsTest(unittest.TestCase):
    def test_nvml_units_layout_unsupported_and_balanced_shutdown(self):
        dll = NvmlFixture()
        gpu = NvidiaTelemetry(dll)
        row = gpu.sample()[0]
        self.assertEqual((row["graphics_mhz"], row["memory_mhz"], row["temperature_c"]), (1200, 6000, 75))
        self.assertEqual((row["gpu_percent"], row["memory_busy_percent"]), (96, 38))
        self.assertEqual((row["memory_used_bytes"], row["memory_total_bytes"]), (5 << 30, 8 << 30))
        self.assertEqual(row["clock_event_reasons"], "0x20")
        self.assertIsNone(row["power_mw"])
        self.assertEqual(row["errors"]["power_mw"], 3)
        gpu.close()
        gpu.close()
        self.assertEqual(dll.shutdowns, 1)
        dll = NvmlFixture(fail_count=True)
        with self.assertRaises(OSError):
            NvidiaTelemetry(dll)
        self.assertEqual(dll.shutdowns, 1)

    def test_slow_gpu_query_never_blocks_stop_and_shuts_down_after_query(self):
        entered, release = threading.Event(), threading.Event()
        closed = []
        owner = threading.get_ident()

        class SlowGpu:
            def __init__(self):
                assert threading.get_ident() != owner

            def sample(self):
                entered.set()
                if not release.wait(3):
                    raise RuntimeError("test did not release query")
                return []

            def close(self):
                closed.append(threading.get_ident())

        with tempfile.TemporaryDirectory() as folder:
            monitor = SystemMonitor(folder, provider=SlowGpu)
            monitor.start()
            try:
                self.assertTrue(entered.wait(2))
                monitor.stop()
                self.assertTrue(monitor.thread.is_alive(), "stop must not wait for pending driver query")
                self.assertEqual(closed, [])
            finally:
                release.set()
                monitor.thread.join(2)
            self.assertFalse(monitor.thread.is_alive())
            self.assertEqual(len(closed), 1)
            self.assertNotEqual(closed[0], owner)
            self.assertIn("MONITOR_STOPPED", Path(folder, "host-system.log").read_text())

    def test_missing_gpu_keeps_system_logging_available(self):
        with tempfile.TemporaryDirectory() as folder:
            monitor = SystemMonitor(folder, provider=lambda: (_ for _ in ()).throw(OSError("absent")))
            original = monitor.write
            def write(kind, data):
                original(kind, data)
                if kind == "SYSTEM":
                    monitor.stop()
            monitor.write = write
            monitor.start()
            monitor.thread.join(2)
            self.assertFalse(monitor.thread.is_alive())
            log = Path(folder, "host-system.log").read_text()
            self.assertIn("GPU_UNAVAILABLE", log)
            self.assertIn("SYSTEM", log)

    def test_frame_sequence_survives_flush_boundaries_and_focus_changes(self):
        with tempfile.TemporaryDirectory() as folder:
            logger = HostTimingLog(folder)
            eye = dict(size=(1920, 1080), depth_size=(960, 540), position=(0., 0., 0.),
                       orientation=(0., 0., 0., 1.), fov=(-.5, .5, .5, -.5))
            moments = [10., 10.067, 10.158, 11.01, 11.077]
            for index, started in enumerate(moments):
                with patch("qarvtk_host_runtime.perf_counter", return_value=started + .03):
                    logger.record(index+1, {"color_render_ms": 30., "host_render_thread_cpu_ms": 4.},
                                  [eye, eye], started, [(960, 540)]*2, (726, 451), 123 if index < 2 else 456)
            with patch("qarvtk_host_runtime.perf_counter", return_value=11.11):
                logger.flush(force=True)
            rows = [json.loads(line.split("PERF_HOST ")[1])
                    for line in Path(folder, "host-python.log").read_text().splitlines()]
            samples = [sample for row in rows for sample in row["samples"]]
            self.assertEqual(len(rows), 2)
            self.assertEqual([s[0] for s in samples], [1, 2, 3, 4, 5])
            self.assertEqual([s[2] for s in samples], [None, 67., 91., 852., 67.])
            self.assertEqual([s[5] for s in samples], [123, 123, 456, 456, 456])
            self.assertEqual([s[6] for s in samples], [False]*5)
            self.assertEqual(rows[0]["frames"], 4)
            self.assertEqual(rows[0]["color_render_ms"], 30.)

    @unittest.skipUnless(os.name == "nt", "Win32 API")
    def test_real_windows_focus_api_and_current_process_handle(self):
        foreground = ForegroundWindow()
        pid = foreground.pid()
        self.assertTrue(pid is None or isinstance(pid, int))
        self.assertTrue(foreground.name(os.getpid()).lower().endswith(".exe"))


if __name__ == "__main__":
    unittest.main()
