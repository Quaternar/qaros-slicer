"""Validate the DLL ABI without a QAROS runtime or a supported XR GPU."""
import ctypes as c
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integrations" / "slicer"))
from qarvtk_host_runtime import Placement, Volume, Frame, EXPECTED_SDK, HUB_SDKS

dll = c.CDLL(sys.argv[1])
dll.qarvtk_host_info.restype = c.c_char_p
info = json.loads(dll.qarvtk_host_info())
assert info == dict(host_abi=5, modern_api=0, panel_probe=0, panel_size_on_start=1, panel_image_region=1,
    target_discovery=3 if sys.argv[2] == EXPECTED_SDK else 2, runtime_handle_only=1,
    multi_target=int(sys.argv[2] in HUB_SDKS),
    header_sha256=sys.argv[2],
    eye_size=104, frame_size=240, placement_size=112, volume_size=120)
assert c.sizeof(Placement) == 112
assert c.sizeof(Volume) == 120 and Volume.name.offset == 112
assert c.sizeof(Frame) == 240 and Frame.volume_index.offset == 232
assert Placement.workspace_size.offset == 64 and Placement.workspace_position.offset == 88
dll.qarvtk_host_create.argtypes = [c.c_char_p, c.c_char_p, c.c_char_p, c.POINTER(Placement), c.POINTER(c.c_void_p), c.c_char_p, c.c_uint32]
dll.qarvtk_host_create.restype = c.c_int
for _ in range(2):
    placement = Placement((c.c_double*3)(0, 0, 0), (c.c_double*4)(0, 0, 0, 1), 1.,
                          (c.c_double*3)(.7, .8, .9), (c.c_double*3)(0, 0, -1.5))
    handle = c.c_void_p()
    error = c.create_string_buffer(1024)
    result = dll.qarvtk_host_create(b"no-runtime", b"visualizer", b"unused", c.byref(placement), c.byref(handle), error, len(error))
    assert result == -1 and not handle.value
    assert b"Make the Slicer render window current" in error.value, error.value
dll.qarvtk_host_create_volumes.argtypes = [c.c_char_p, c.c_char_p, c.c_char_p, c.POINTER(Volume), c.c_uint32, c.POINTER(c.c_void_p), c.c_char_p, c.c_uint32]
dll.qarvtk_host_create_volumes.restype = c.c_int
volumes = (Volume*2)(Volume(placement, b"First"), Volume(placement, b"Second"))
for count in (0, 2, 33):
    assert dll.qarvtk_host_create_volumes(b"no-runtime", b"visualizer", b"unused", volumes, count, c.byref(handle), error, len(error)) == -1
    assert not handle.value
    assert (b"Make the Slicer render window current" if count == 2 else b"1 to 32") in error.value
dll.qarvtk_host_destroy.argtypes = [c.c_void_p]
dll.qarvtk_host_destroy.restype = c.c_int
assert dll.qarvtk_host_destroy(None) == 1
for name in ("create", "capture", "capture_region", "visible", "close", "status"):
    assert getattr(dll, "qarvtk_host_panel_" + name)
dll.qarvtk_host_panel_create.argtypes = [c.c_void_p, c.c_char_p] + [c.c_uint32]*2 + [c.c_double]*4 + [c.POINTER(c.c_void_p), c.c_char_p, c.c_uint32]
panel = c.c_void_p()
error = c.create_string_buffer(1024)
assert dll.qarvtk_host_panel_create(None, b"Red", 320, 240, 0., 0., -1.5, .45, c.byref(panel), error, len(error)) == -1
assert not panel.value and b"No active panel transport" in error.value
print("PASS: VTK-free DLL loads; ABI metadata; missing host context fails cleanly and can be retried")

