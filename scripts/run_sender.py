#!/usr/bin/env python3
"""Launch with a build-matched SDK and an unmodified bin/ directory."""
import argparse
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integrations"))
from qarvtk_runtime import backend_environment, verify_sdk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--sdk", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    exe, sdk = args.exe.resolve(), args.sdk.resolve()
    env = backend_environment()
    info = verify_sdk(exe, sdk, env)
    print("SDK header verified; API: " + ("Hub onboarding" if info["modern_api"] else "legacy session invite"), flush=True)
    return subprocess.call([str(exe), "--sdk", str(sdk), *remaining], env=env)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("ERROR:", error, file=sys.stderr)
        sys.exit(1)
