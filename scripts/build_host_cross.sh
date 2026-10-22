#!/usr/bin/env bash
# Optional Linux -> Windows x64 build; the usual MSVC CMake build also works.
set -euo pipefail
cd "$(dirname "$0")/.."
qarvtk_cxx="${QARVTK_CXX:-x86_64-w64-mingw32-g++-posix}"
qarvtk_strip="${QARVTK_STRIP:-x86_64-w64-mingw32-strip}"
qarvtk_sdk="${QAROS_SDK_ROOT:-ci/sdk-headers/hub-55e6e3f4}"
qarvtk_output="${QARVTK_HOST_BUILD_DIR:-build-host-cross}"
qarvtk_hash="$(sha256sum "$qarvtk_sdk/include/qar_streaming.h" | cut -d' ' -f1)"
qarvtk_onboarding=0
qarvtk_presence=0
case "$qarvtk_hash" in
  022c40b670f87a066664c37daeb06da525db37fffdf5810e72cff8a5b81e677d) qarvtk_split=0 ;;
  c709c479cc6a1acc0040fb61a071cc7bc16e65bf5acb90d624d9d6b0a6fcd3c1) qarvtk_split=1 ;;
  a32cecd4137dd3b28e0d5e0c1642a066cbea976f81b90f1f31fbac4d0fba8d4a) qarvtk_split=1; qarvtk_onboarding=1 ;;
  55e6e3f48d940ca29331f0528fe475c1b7230fb19a59c31d5525c6e90b097f6c) qarvtk_split=1; qarvtk_onboarding=1; qarvtk_presence=1 ;;
  *) echo "Unsupported SDK header: $qarvtk_hash" >&2; exit 1 ;;
esac
mkdir -p "$qarvtk_output"
"$qarvtk_cxx" -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ -pthread \
  -Iinclude -I"$qarvtk_sdk/include" -DQARVTK_HOST_GL -DQARVTK_HOST_EXPORTS \
  -DQAR_ENABLE_OPENGL -DQAR_ENABLE_DYNAMIC_LOADING -DNOMINMAX -DWIN32_LEAN_AND_MEAN \
  "-DQARVTK_HOST_SDK_HASH=\"$qarvtk_hash\"" \
  "-DQARVTK_SPLIT_OPENGL_PARAMS=$qarvtk_split" \
  "-DQARVTK_HOST_ONBOARDING_API=$qarvtk_onboarding" \
  "-DQARVTK_HOST_PEER_PRESENCE=$qarvtk_presence" \
  src/host_bridge.cpp src/host_panels.cpp src/host_gl.cpp src/gl_copy.cpp \
  src/gl_composite.cpp src/layout.cpp src/math.cpp src/shutdown.cpp \
  -o "$qarvtk_output/qarvtk-host.dll" -lopengl32 -lgdi32 -luser32 -Wl,--no-insert-timestamp
"$qarvtk_strip" --strip-unneeded "$qarvtk_output/qarvtk-host.dll"
sha256sum "$qarvtk_output/qarvtk-host.dll"
