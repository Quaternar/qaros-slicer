"""Read-only focus and NVIDIA telemetry. No graphics-context access or waits on the GUI."""
import ctypes as c
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import threading
from time import perf_counter, process_time


class ForegroundWindow:
    """Only PID/executable, never window titles or document names."""
    def __init__(self):
        self.user = c.WinDLL("user32", use_last_error=True)
        self.kernel = c.WinDLL("kernel32", use_last_error=True)
        self.user.GetForegroundWindow.argtypes = []
        self.user.GetForegroundWindow.restype = c.c_void_p
        self.user.GetWindowThreadProcessId.argtypes = [c.c_void_p, c.POINTER(c.c_uint32)]
        self.user.GetWindowThreadProcessId.restype = c.c_uint32
        self.kernel.OpenProcess.argtypes = [c.c_uint32, c.c_int, c.c_uint32]
        self.kernel.OpenProcess.restype = c.c_void_p
        self.kernel.QueryFullProcessImageNameW.argtypes = [c.c_void_p, c.c_uint32, c.c_wchar_p,
                                                        c.POINTER(c.c_uint32)]
        self.kernel.QueryFullProcessImageNameW.restype = c.c_int
        self.kernel.CloseHandle.argtypes = [c.c_void_p]
        self.kernel.CloseHandle.restype = c.c_int

    def pid(self):
        hwnd = self.user.GetForegroundWindow()
        pid = c.c_uint32()
        if hwnd and self.user.GetWindowThreadProcessId(hwnd, c.byref(pid)):
            return pid.value
        return None

    def name(self, pid):
        if not pid:
            return None
        handle = self.kernel.OpenProcess(0x1000, False, pid)  # QUERY_LIMITED_INFORMATION
        if not handle:
            return None
        try:
            text, size = c.create_unicode_buffer(32768), c.c_uint32(32768)
            if self.kernel.QueryFullProcessImageNameW(handle, 0, text, c.byref(size)):
                return text.value.replace("\\", "/").rsplit("/", 1)[-1]
        finally:
            self.kernel.CloseHandle(handle)
        return None


class _Utilization(c.Structure):
    _fields_ = [("gpu", c.c_uint), ("memory", c.c_uint)]


class _Memory(c.Structure):
    _fields_ = [("total", c.c_ulonglong), ("free", c.c_ulonglong), ("used", c.c_ulonglong)]


class NvidiaTelemetry:
    """Small, optional NVML reader, using stable v1 structures and read-only APIs.

    See https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceQueries.html
    All calls, including initialization and shutdown, belong to the sampler thread.
    Missing/unsupported counters remain null with their NVML status code.
    """
    def __init__(self, dll=None):
        if dll is None:
            candidates = [Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32/nvml.dll",
                          Path(os.environ.get("ProgramW6432", "C:/Program Files")) /
                          "NVIDIA Corporation/NVSMI/nvml.dll"]
            for path in candidates:
                try:
                    dll = c.CDLL(str(path))
                    break
                except OSError:
                    continue
            if dll is None:
                raise OSError("NVIDIA NVML is not available")
        self.dll, self.initialized, self.devices = dll, False, []
        self.functions = {}
        try:
            status = self.call("nvmlInit_v2", [], [])
            if status:
                raise OSError("nvmlInit_v2 status " + str(status))
            self.initialized = True
            count = c.c_uint()
            status = self.call("nvmlDeviceGetCount_v2", [c.POINTER(c.c_uint)], [c.byref(count)])
            if status:
                raise OSError("nvmlDeviceGetCount_v2 status " + str(status))
            for index in range(count.value):
                handle, name = c.c_void_p(), c.create_string_buffer(96)
                status = self.call("nvmlDeviceGetHandleByIndex_v2", [c.c_uint, c.POINTER(c.c_void_p)],
                                   [index, c.byref(handle)])
                if status:
                    self.devices.append((index, None, "unavailable: " + str(status)))
                    continue
                self.call("nvmlDeviceGetName", [c.c_void_p, c.c_char_p, c.c_uint], [handle, name, len(name)])
                self.devices.append((index, handle, name.value.decode("utf-8", "replace")))
        except Exception:
            self.close()
            raise

    def call(self, name, types, args):
        if name not in self.functions:
            function = getattr(self.dll, name, None)
            if function is not None:
                function.argtypes, function.restype = types, c.c_int
            self.functions[name] = function
        function = self.functions[name]
        return function(*args) if function is not None else 13  # NVML_ERROR_FUNCTION_NOT_FOUND

    def sample(self):
        result = []
        for index, handle, name in self.devices:
            row = dict(index=index, name=name, errors={})
            result.append(row)
            if handle is None:
                continue

            def query(key, function, kind=c.c_uint, extra=()):
                value = kind()
                status = self.call(function, [c.c_void_p] + [c.c_uint]*len(extra) + [c.POINTER(kind)],
                                   [handle, *extra, c.byref(value)])
                if status:
                    row["errors"][key] = status
                    return None
                return value

            for key, function, extra in (
                ("graphics_mhz", "nvmlDeviceGetClockInfo", (0,)),
                ("memory_mhz", "nvmlDeviceGetClockInfo", (2,)),
                ("temperature_c", "nvmlDeviceGetTemperature", (0,)),
                ("pstate", "nvmlDeviceGetPerformanceState", ()),
                ("power_mw", "nvmlDeviceGetPowerUsage", ()),
            ):
                value = query(key, function, extra=extra)
                row[key] = value.value if value is not None else None
            reasons = query("clock_event_reasons", "nvmlDeviceGetCurrentClocksThrottleReasons", c.c_ulonglong)
            row["clock_event_reasons"] = hex(reasons.value) if reasons is not None else None
            utilization = query("utilization", "nvmlDeviceGetUtilizationRates", _Utilization)
            row["gpu_percent"] = utilization.gpu if utilization is not None else None
            row["memory_busy_percent"] = utilization.memory if utilization is not None else None
            memory = query("memory", "nvmlDeviceGetMemoryInfo", _Memory)
            row["memory_used_bytes"] = memory.used if memory is not None else None
            row["memory_total_bytes"] = memory.total if memory is not None else None
        return result

    def close(self):
        if self.initialized:
            self.initialized = False
            self.call("nvmlShutdown", [], [])


class SystemMonitor:
    """One sample/s off the render thread. Stop only signals, never joins the GUI."""
    def __init__(self, folder, foreground=None, provider=NvidiaTelemetry):
        self.path = Path(folder) / "host-system.log"
        self.foreground, self.provider = foreground, provider
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self._run, name="QAROS diagnostics", daemon=True)

    def write(self, kind, data):
        try:
            with self.path.open("a", encoding="utf-8") as stream:
                stream.write(datetime.now(timezone.utc).isoformat(timespec="milliseconds") +
                             " " + kind + " " + json.dumps(data, sort_keys=True) + "\n")
        except OSError:
            pass

    def start(self):
        self.thread.start()

    def stop(self):
        self.stopped.set()

    def _run(self):
        gpu = None
        try:
            try:
                gpu = self.provider()
            except Exception as error:
                self.write("GPU_UNAVAILABLE", dict(reason=str(error)))
            last_pid, last_name = None, None
            while not self.stopped.is_set():
                started = perf_counter()
                pid = self.foreground.pid() if self.foreground else None
                if pid != last_pid:
                    last_pid = pid
                    last_name = self.foreground.name(pid) if self.foreground else None
                row = dict(monotonic_s=started, process_cpu_s=process_time(),
                           foreground_pid=pid, foreground_process=last_name,
                           slicer_pid=os.getpid())
                if gpu:
                    try:
                        row["gpus"] = gpu.sample()
                    except Exception as error:
                        self.write("GPU_UNAVAILABLE", dict(reason=str(error)))
                        gpu.close()
                        gpu = None
                row["sample_wall_ms"] = round((perf_counter() - started) * 1000, 3)
                self.write("SYSTEM", row)
                self.stopped.wait(max(.1, 1. - (perf_counter() - started)))
        except Exception as error:
            self.write("MONITOR_ERROR", dict(reason=str(error)))
        finally:
            if gpu:
                gpu.close()
            self.write("MONITOR_STOPPED", {})
