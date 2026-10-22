# Exact headers used for Live bridge builds

`hub-55e6e3f4/include/qar_streaming.h` is the unmodified 373637-byte header
from `QAROS.zip`, supplied on 2026-10-22. Its accompanying changelog identifies
`0.9.0-dev.3008+65dc9aeb`.
SHA256: `55e6e3f48d940ca29331f0528fe475c1b7230fb19a59c31d5525c6e90b097f6c`.
The current r8.4 Live update targets these bytes. It uses peer presence for
target discovery and no longer loads the removed `runtime_destroy` export.
See [the installed update guide](../../docs/QAROS_DEV3008_UPDATE.md).

`hub-a32cecd4/include/qar_streaming.h` is the unmodified header supplied from
the installed `C:\Program Files\Quaternar\QAROS` on 2026-10-19. Its accompanying
changelog identifies `0.9.0-dev.2927+f291e404`.
SHA256: `a32cecd4137dd3b28e0d5e0c1642a066cbea976f81b90f1f31fbac4d0fba8d4a`.
It uses local app enrollment into the installed Hub, running target discovery,
and get-or-create volumes/panels with ASCII common names. The archived r8.3
Live ZIP targets these bytes. See [its update guide](../../docs/QAROS_DEV2927_UPDATE.md).

`session-c709c479/include/qar_streaming.h` is the unmodified 256428-byte header
supplied on 2026-10-19 from `QAROS-0.9.0-dev.2540+1ed038e4`.
SHA256: `c709c479cc6a1acc0040fb61a071cc7bc16e65bf5acb90d624d9d6b0a6fcd3c1`.
It retains session invites but splits OpenGL context parameters into WGL/GLX/EGL
structures and changes the gesture API. The archived dev.2540 ZIP targets these bytes.
See [the update guide](../../docs/QAROS_DEV2540_UPDATE.md).

`session-022c40b6/include/qar_streaming.h` is the unmodified header supplied
from the existing Windows QAROS installation on 2026-10-14. It contains the
session invite API and OpenGL sender API, not the modern Hub onboarding API.

- SHA256: `022c40b670f87a066664c37daeb06da525db37fffdf5810e72cff8a5b81e677d`
- Size: 191192 bytes
- Original runtime version: not inferred from the header

This is a compile fixture. Do not copy it into a different SDK installation.
The runtime, viewer and all required QAROS DLLs must remain from the matching
installed package. The plugin still checks the installed header against the
sender's embedded hash. CI also verifies this hash before and after compilation.

The modern sender remains a separate CI artifact compiled from the pinned
upstream public header. No runtime DLLs or session credentials are included here.
