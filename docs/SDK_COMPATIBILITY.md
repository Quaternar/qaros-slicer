# SDK compatibility

For the Live host bridge, CMake hashes the chosen **local**
`include/qar_streaming.h` and selects the API from the four pinned fingerprints
below. Unknown headers are rejected. The separate standalone sender detects
onboarding declarations; that does not promise compatibility with arbitrary
future V0 headers.

| API generation | Source inspected | Connection path |
| --- | --- | --- |
| Installed Hub, presence | Supplied QAROS `0.9.0-dev.3008+65dc9aeb`, header `55e6e3f4` | Local app enrollment, peer presence discovery, get-or-create volumes/panels; split WGL callbacks; current Live r8.4 |
| Installed Hub | Supplied QAROS `0.9.0-dev.2927+f291e404`, header `a32cecd4` | Local app enrollment, discover a running target, get-or-create volumes and panels; split WGL callbacks |
| Session, split OpenGL | Supplied QAROS `0.9.0-dev.2540+1ed038e4`, header `c709c479` | Create/serialize/join session, invite target, add volume; new WGL callback layout |
| Supplied session SDK | Exact header `022c40b670f87a066664c37daeb06da525db37fffdf5810e72cff8a5b81e677d` | Same session branch; Windows sender compiled separately for these exact bytes |
| Modern | [Quaternar/qaros at 678e5463](https://github.com/Quaternar/qaros/blob/678e5463bb14a9d4d1f681fcdb9dfab9fe61e77e/qar-streaming-c/include/qar_streaming.h) | Hub onboarding/rejoin, get-or-create volume, subscribe to render requests |

Both use `QAR_ENABLE_OPENGL`, dynamic loading, an adopted shared WGL/GLX context,
`qar_render_sender_try_begin_frame`, `qar_render_sender_frame_opengl`, per-view
pose/FOV, `qar_render_sender_show_frame`, and frame info destruction.

The Slicer Live host supports the four exact supplied headers above;
each DLL embeds the hash it was compiled against. Its Windows update procedure
is in [QAROS_DEV3008_UPDATE.md](QAROS_DEV3008_UPDATE.md). For this host, the full
header hash selects the API; arbitrary headers are rejected. Session API
selection and OpenGL layout are independent: dev.2540 retains session invites,
whereas dev.2927 uses Hub onboarding, and both use the split WGL extension.
Dev.3008 additionally selects `QARVTK_HOST_PEER_PRESENCE=1`; its bridge uses
`qar_peer_spec_get_presence` and reports `target_discovery=3`. It never calls
the removed `qar_runtime_destroy`. Older bridges keep their own symbol tables
and `target_discovery=2`. A DLL for one fingerprint cannot load another SDK.

The OpenGL context extension is detected separately from the session API.
The supplied `022c40b6` header uses `QarStreamParamsOpenGl`, with an explicit
WGL/GLX discriminator and native context fields. Newer headers split these into
`QarStreamParamsOpenGlWgl` and `QarStreamParamsOpenGlGlx`. CMake selects the
matching structs and default initializers; there are no casts between their
different ABI layouts. Both variants retain the separate SDK-owned shared context.

The selected adapter is read from OpenGL `GL_EXT_memory_object` (LUID on Windows,
UUID on Linux) and chained into session creation/join or onboarding/rejoin.
The inspected SDK documents NVIDIA-only support. Mesa is used only for the
renderer/copy tests; these tests do not invoke QAROS.

## Linux header issue

The older headers emit `__declspec(dllimport)` declarations even on Linux.
The sender temporarily suppresses this Windows declaration annotation only while
including the header under GCC/Clang, using push/pop macro. The original header,
struct layout, symbol names and dynamic loader are unchanged. This allows compile
coverage; it does not establish availability or correctness of a Linux runtime.
The supplied dev.3008 header has portable declarations. The compatibility
block remains for the older compile fixtures and standalone sender headers.

## Ownership and cancellation

The render context is owned by the application's main thread. A second GLFW
context in the same share group is reserved for the SDK graphics thread. Its
window/native handles outlive the sender and SDK. The SDK's default native context
callbacks are used; the application never tries to bind that context while the
SDK is running.

Modern callback state is mutex-protected and outlives cancellation and SDK
shutdown. Callbacks only copy the first target peer ID and destroy the request;
they never call VTK/OpenGL. Frame info uses RAII. A frame failure terminates and
destroys the sender instead of inventing an unsupported abort-frame call.
Onboarding/rejoin, target invitations and sender creation use cancellation
timeouts, as do periods without frames. Runtime initialization and the legacy
create/join calls expose no cancellation token in the inspected API. First-run
human pairing input can wait until the user enters a code or closes stdin.

Modern volumes use a unique per-run common name, so shutdown closes only the
volume created by this process. The prototype does not yet persist a stable
volume identity across runs. Rejoin identity is persisted separately.

## Version discipline

Use `scripts/run_sender.py`: it compares the header hash against `--sdk-info`.
Never replace the header without the matching runtime binaries. The check
cannot detect a manually mixed set of DLLs with an unchanged header.
No SDK runtime binaries or invite files are vendored here. Modern CI downloads
the pinned public header for compilation. The user-supplied session header is
preserved unchanged in `ci/sdk-headers/session-022c40b6/include/qar_streaming.h`
so CI can reproduce a sender for the installed SDK. Git preserves its exact
bytes, including LF line endings, on Windows. This header alone does not install
an SDK or establish a runtime test result.
