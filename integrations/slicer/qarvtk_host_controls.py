"""Qt controls for the existing Slicer slice offset sliders."""
import ctypes as c
from pathlib import Path
import qt

_libraries = {}  # Qt widgets and imported callbacks must outlive native teardown.


def controls_library(path):
    path = str(Path(path).resolve())
    if path in _libraries:
        return _libraries[path]
    dll = c.CDLL(path)
    dll.qarvtk_qt_version.restype = c.c_char_p
    version = dll.qarvtk_qt_version().decode("ascii")
    if version != str(qt.qVersion()):
        raise RuntimeError("Slice controls were built for Qt " + version + "; Slicer uses " + str(qt.qVersion()))
    signatures = {
        "create": ([c.c_void_p] + [c.c_double]*3 + [c.c_char_p, c.c_uint32], c.c_void_p),
        "update": ([c.c_void_p, c.c_uint32] + [c.c_double]*4 + [c.c_uint32, c.c_char_p, c.c_uint32], c.c_int),
        "tick": ([c.c_void_p, c.c_uint32, c.c_char_p, c.c_uint32], c.c_int),
        "changed": ([c.c_void_p, c.c_uint32, c.POINTER(c.c_double)], c.c_int),
        "destroy": ([c.c_void_p], None),
    }
    for name, (arguments, result) in signatures.items():
        function = getattr(dll, "qarvtk_qt_controls_" + name)
        function.argtypes, function.restype = arguments, result
    _libraries[path] = dll
    return dll


class SliceControls:
    names = ("Red", "Yellow", "Green")

    def __init__(self, layout, sink, position):
        self.layout, self.sink = layout, sink
        self.handle = None
        self.states = {}
        if sink.dll._qarvtk_info.get("panel_input") != 1:
            raise RuntimeError("Install the r8.10 bridge for Qt slice controls")
        path = Path(sink.dll._name).with_name("qarvtk-qt-controls.dll")
        self.dll = controls_library(path)
        sink.host.window.MakeCurrent()
        error = c.create_string_buffer(2048)
        self.handle = self.dll.qarvtk_qt_controls_create(sink.handle, *position, error, len(error))
        if not self.handle:
            raise RuntimeError(error.value.decode("utf-8", "replace"))

    def slider(self, index):
        widget = self.layout.sliceWidget(self.names[index])
        return widget.sliceController().sliceOffsetSlider() if widget else None

    def apply_changes(self, active):
        for index in range(len(self.names)):
            value = c.c_double()
            if self.dll.qarvtk_qt_controls_changed(self.handle, index, c.byref(value)):
                slider = self.slider(index)
                if active and slider and slider.enabled:
                    # Use Slicer's own slider and its normal MRML signal connections.
                    slider.value = value.value
                    self.sink.diagnostics.event("SLICE_CONTROL " + self.names[index] + " offset_mm=" + str(slider.value))

    def tick(self):
        error = c.create_string_buffer(2048)
        active = bool(self.sink.receiver_ids)
        self.apply_changes(active)
        for index in range(len(self.names)):
            slider = self.slider(index)
            state = (float(slider.minimum), float(slider.maximum), max(float(slider.singleStep), 1e-6),
                     float(slider.value), bool(slider.enabled and slider.maximum > slider.minimum)) if slider else (0., 0., 1., 0., False)
            if self.states.get(index) != state:
                if self.dll.qarvtk_qt_controls_update(self.handle, index, *state, error, len(error)) != 0:
                    raise RuntimeError(error.value.decode("utf-8", "replace"))
                self.states[index] = state
        if self.dll.qarvtk_qt_controls_tick(self.handle, int(active), error, len(error)) < 0:
            raise RuntimeError(error.value.decode("utf-8", "replace"))
        if active:
            self.apply_changes(True)

    def close(self):
        if self.handle:
            self.dll.qarvtk_qt_controls_destroy(self.handle)
            self.handle = None
