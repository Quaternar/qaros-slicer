"""Tiny SDK-shaped test input, only for qarvtk-process-fixture (not a real SDK)."""
import os
from pathlib import Path
import time


def make_sdk(directory):
    sdk = Path(directory) / "fixture-sdk"
    (sdk / "include").mkdir(parents=True, exist_ok=True)
    (sdk / "bin").mkdir(exist_ok=True)
    (sdk / "include" / "qar_streaming.h").write_bytes(b"fixture SDK\n")
    library = "qar-streaming-c.dll" if os.name == "nt" else "libqar-streaming-c.so"
    (sdk / "bin" / library).touch()
    return sdk


def wait_ready(backend):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if "FIXTURE_READY" in backend.log_tail():
            return
        time.sleep(0.02)
    raise AssertionError("Fixture did not start: " + backend.log_tail())
