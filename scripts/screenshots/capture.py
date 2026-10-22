#!/usr/bin/env python3
"""Capture the actual X desktop after the host application's scene is ready."""
import argparse
import os
from pathlib import Path
import signal
import subprocess
import time


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", choices=("slicer", "paraview"))
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output", type=Path, default=Path("screenshots"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    ready = output / (args.app + ".ready")
    ready.unlink(missing_ok=True)
    ready.with_suffix(".ready.failed").unlink(missing_ok=True)
    env = dict(os.environ, QARVTK_SCREENSHOT_READY=str(ready), QARVTK_SOURCE_ROOT=str(root),
               LIBGL_ALWAYS_SOFTWARE="1")
    script = root / "scripts/screenshots" / (args.app + "_scene.py")
    command = [str(args.executable.resolve())]
    if args.app == "slicer":
        command += ["--no-splash", "--ignore-slicerrc", "--additional-module-paths",
                    str(root / "package/QAROS-VTK-plugins/slicer"),
                    "--python-script", str(script)]
        title, filename = "Slicer", "QAROS_Slicer_Linux.png"
    else:
        command += ["--script", str(script)]
        title, filename = "ParaView", "QAROS_ParaView_Linux.png"

    with (output / (args.app + ".log")).open("w") as log:
        wm = subprocess.Popen(["openbox"], stdout=log, stderr=log)
        process = subprocess.Popen(command, env=env, cwd=root, stdout=log,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            deadline = time.monotonic() + 180
            while not ready.exists():
                if process.poll() is not None:
                    raise RuntimeError(f"{args.app} exited with {process.returncode}")
                if ready.with_suffix(".ready.failed").exists():
                    raise RuntimeError(f"{args.app} scene setup failed; see log")
                if "Traceback (most recent call last):" in (
                        output / (args.app + ".log")).read_text(errors="replace"):
                    raise RuntimeError(f"{args.app} startup script failed; see log")
                if time.monotonic() > deadline:
                    raise TimeoutError(f"{args.app} did not prepare its scene")
                time.sleep(1)
            time.sleep(3)
            # Dismiss the first-run welcome dialog if present; focus the host.
            subprocess.run(["xdotool", "key", "Escape"], check=False)
            subprocess.run(["wmctrl", "-a", title], check=True)
            subprocess.run(["wmctrl", "-r", title, "-b",
                            "add,maximized_vert,maximized_horz"], check=True)
            subprocess.run(["xdotool", "mousemove", "800", "9"], check=False)
            time.sleep(4)
            subprocess.run(["wmctrl", "-l"], check=True)
            subprocess.run(["import", "-window", "root", str(output / filename)],
                           check=True)
            assert (output / filename).stat().st_size > 20000
            print(f"Captured real {args.app} GUI: {output / filename}", flush=True)
        except Exception:
            subprocess.run(["import", "-window", "root",
                            str(output / (args.app + "-diagnostic.png"))], check=False)
            raise
        finally:
            # Only terminate the process group created by this capture invocation.
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
            wm.terminate()
            wm.wait(timeout=10)


if __name__ == "__main__":
    run()
