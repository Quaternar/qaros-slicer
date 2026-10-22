"""Host-independent lifecycle for a separate QAROS rendering process (stdlib only)."""
import atexit
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import weakref


def user_directory():
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
    else:
        base = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local" / "state"))
    path = base / "QAROS_VTK"
    path.mkdir(parents=True, exist_ok=True)
    return path


def backend_environment(environment=None):
    env = dict(os.environ if environment is None else environment)
    # Embedded Python/Qt/VTK from a host must not determine the sender's ABI.
    for key in ("PYTHONPATH", "PYTHONHOME", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                "LD_LIBRARY_PATH", "LD_PRELOAD"):
        env.pop(key, None)
    return env


def verify_sdk(executable, sdk, environment=None):
    executable, sdk = Path(executable).resolve(), Path(sdk).resolve()
    header = sdk / "include" / "qar_streaming.h"
    if not executable.is_file() or not header.is_file():
        raise ValueError("Select qarvtk-sender and the SDK folder containing include/qar_streaming.h")
    info = json.loads(subprocess.check_output(
        [str(executable), "--sdk-info"], text=True, timeout=10,
        env=backend_environment(environment), **_process_options()))
    if hashlib.sha256(header.read_bytes()).hexdigest() != info.get("header_sha256"):
        raise ValueError("SDK does not match this sender. Rebuild the sender with this complete SDK package.")
    if info.get("plugin_protocol") not in (1, 2):
        raise ValueError("Sender is too old for these plugins. Rebuild it from the same repository revision.")
    library = "qar-streaming-c.dll" if os.name == "nt" else "libqar-streaming-c.so"
    if not (sdk / "bin" / library).is_file():
        raise ValueError("Missing SDK runtime: " + str(sdk / "bin" / library))
    return info


def _process_options():
    return {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}


_controllers = weakref.WeakSet()


def _shutdown():
    for controller in list(_controllers):
        controller.close()


atexit.register(_shutdown)


class Backend:
    """One private scene, log and sender per plugin instance.

    'running' means the process exists, not that a viewer is connected.
    Pairing codes travel over stdin and are never persisted by this module.
    """
    def __init__(self, environment=None, scene_suffix=".vtp"):
        if scene_suffix not in (".vtp", ".vtm"):
            raise ValueError("Unsupported scene format")
        self.directory = Path(tempfile.mkdtemp(prefix="qarvtk-"))
        self.scene = self.directory / ("scene" + scene_suffix)
        self.log_path = self.directory / "sender.log"
        self.stop_path = self.directory / "stop"
        self.environment = backend_environment(environment)
        self.process = None
        self._log = None
        self._stop_deadline = None
        self.forced_stop = False
        _controllers.add(self)

    @property
    def running(self):
        return self.process is not None and self.process.poll() is None

    def start(self, executable, sdk, scale=1.0, ras=False, pairing_code="", hub="",
              identity=None, invite="", target="visualizer"):
        if self.running:
            raise RuntimeError("This plugin already has a running sender")
        if not self.scene.is_file():
            raise ValueError("Publish a scene before starting the sender")
        if not math.isfinite(scale) or scale <= 0:
            raise ValueError("Metres per unit must be positive and finite")
        info = verify_sdk(executable, sdk, self.environment)
        if self.scene.suffix == ".vtm" and info.get("plugin_protocol", 0) < 2:
            raise ValueError("Scene/volume streaming requires the r9 sender or newer. Update the complete starter package.")
        if self._log:
            self._log.close()
        if self.stop_path.exists():
            self.stop_path.unlink()
        command = [str(Path(executable).resolve()), "--sdk", str(Path(sdk).resolve()),
                   "--scene", str(self.scene), "--watch", "--meters-per-unit", str(scale),
                   "--stop-file", str(self.stop_path), "--parent-pid", str(os.getpid())]
        if ras:
            command.append("--ras")
        if info["modern_api"]:
            identity = Path(identity) if identity else user_directory() / "onboarding-id.txt"
            identity = identity.expanduser().resolve()
            identity.parent.mkdir(parents=True, exist_ok=True)
            if not identity.exists() and not pairing_code.strip():
                raise ValueError("Enter a Hub pairing code for the first connection")
            command += ["--identity", str(identity)]
            if hub.strip():
                command += ["--hub", hub.strip()]
        else:
            command += ["--target", target]
            if invite:
                command += ["--invite-file", str(Path(invite).resolve())]
        self._log = self.log_path.open("wb")
        self._stop_deadline = None
        self.forced_stop = False
        try:
            self.process = subprocess.Popen(command, stdin=subprocess.PIPE,
                stdout=self._log, stderr=subprocess.STDOUT, cwd=str(self.directory),
                env=self.environment, **_process_options())
            try:
                self.process.stdin.write((pairing_code.strip() + "\n").encode("utf-8"))
                self.process.stdin.flush()
            except (BrokenPipeError, OSError):
                # An immediate loader failure is reported through exit status/log.
                pass
            finally:
                try:
                    self.process.stdin.close()
                except OSError:
                    pass
        except Exception:
            self._log.close()
            self._log = None
            raise

    def request_stop(self):
        if self.running and self._stop_deadline is None:
            self.stop_path.touch()
            # Allow an SDK operation with a 60-second timeout to finish normally.
            self._stop_deadline = time.monotonic() + 65

    def poll(self):
        if self.running and self._stop_deadline and time.monotonic() > self._stop_deadline:
            self.process.kill()
            self.process.wait(timeout=5)
            self.forced_stop = True
        code = self.process.poll() if self.process else None
        if code is not None and self._log:
            self._log.close()
            self._log = None
        return code

    def status(self):
        code = self.poll()
        if self.running:
            return "Stopping sender..." if self._stop_deadline else "Sender process running; check log for viewer connection."
        if self.process is None:
            return "Sender has not been started."
        return "Sender exited with code {}{}. Log: {}".format(
            code, " (forced stop)" if self.forced_stop else "", self.log_path)

    def log_tail(self, limit=6000):
        if not self.log_path.exists():
            return ""
        with self.log_path.open("rb") as stream:
            stream.seek(max(0, self.log_path.stat().st_size - limit))
            return stream.read().decode("utf-8", errors="replace")

    def close(self):
        self.request_stop()
        if self.running:
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
                self.forced_stop = True
        self.poll()
        # Keep the diagnostic log, but remove the exported (potentially medical) data.
        for path in (self.scene, self.stop_path):
            try:
                path.unlink()
            except FileNotFoundError:
                pass
        for generation in self.directory.glob("scene-data-*"):
            if generation.is_dir():
                shutil.rmtree(generation)
