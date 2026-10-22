#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${QARVTK_BUILD_DIR:-$repo_dir/build-linux}"
args=(-S "$repo_dir" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release)
if [[ $# -gt 0 ]]; then
  args+=(-DQARVTK_BUILD_SENDER=ON "-DQAROS_SDK_ROOT=$1")
else
  args+=(-DQARVTK_BUILD_SENDER=OFF)
fi
cmake "${args[@]}"
cmake --build "$build_dir" --parallel 4
ctest --test-dir "$build_dir" --output-on-failure
