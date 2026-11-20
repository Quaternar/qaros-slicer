"""Qt controls for the existing Slicer slice offset sliders."""
import ctypes as c
from pathlib import Path
import qt
import slicer

_libraries = {}  # Qt widgets and imported callbacks must outlive native teardown.


def controls_library(path):
    path = str(Path(path).resolve())
    if path in _libraries:
        return _libraries[path]
    # Qt events can synchronously call PythonQt slots (for example Stop).
    # Keep the GIL while the GUI bridge dispatches those events.
    dll = c.PyDLL(path)
    dll.qarvtk_qt_version.restype = c.c_char_p
    version = dll.qarvtk_qt_version().decode("ascii")
    if version != str(qt.qVersion()):
        raise RuntimeError("Slice controls were built for Qt " + version + "; Slicer uses " + str(qt.qVersion()))
    signatures = {
        "create": ([c.c_void_p] + [c.c_double]*3 + [c.c_char_p, c.c_uint32], c.c_void_p),
        "update": ([c.c_void_p, c.c_uint32] + [c.c_double]*4 + [c.c_uint32, c.c_char_p, c.c_uint32], c.c_int),
        "tick": ([c.c_void_p, c.c_uint32, c.c_char_p, c.c_uint32], c.c_int),
        "changed": ([c.c_void_p, c.c_uint32, c.POINTER(c.c_double)], c.c_int),
        "attach_playback": ([c.c_void_p, c.c_char_p, c.c_char_p, c.c_uint32], c.c_int),
        "playback_visible": ([c.c_void_p, c.c_uint32], None),
        "status": ([c.c_void_p, c.c_char_p, c.c_uint32], None),
        "add_slice": ([c.c_void_p, c.c_char_p, c.c_char_p, c.c_char_p, c.c_uint32], c.c_int),
        "destroy": ([c.c_void_p], None),
    }
    for name, (arguments, result) in signatures.items():
        function = getattr(dll, "qarvtk_qt_controls_" + name)
        function.argtypes, function.restype = arguments, result
    _libraries[path] = dll
    return dll


class SliceControls:
    def __init__(self, layout, sink, position):
        self.layout, self.sink = layout, sink
        self.handle = None
        self.states = {}
        self.names = []
        if (sink.dll._qarvtk_info.get("panel_input") != 1 or
                sink.dll._qarvtk_info.get("panel_pipeline_audience") != 1 or
                sink.dll._qarvtk_info.get("panel_drag_origin") != 1):
            raise RuntimeError("Install the r8.13 bridge for Qt slice controls")
        path = Path(sink.dll._name).with_name("qarvtk-qt-controls.dll")
        self.dll = controls_library(path)
        sink.host.window.MakeCurrent()
        error = c.create_string_buffer(2048)
        self.handle = self.dll.qarvtk_qt_controls_create(sink.handle, *position, error, len(error))
        if not self.handle:
            raise RuntimeError(error.value.decode("utf-8", "replace"))
        self.playback = None
        try:
            self.refresh_views()
            self.playback = SequenceControls("qarvtkSequenceControls" + str(self.handle))
            if self.dll.qarvtk_qt_controls_attach_playback(self.handle, self.playback.widget.objectName.encode("utf-8"),
                                                         error, len(error)) != 0:
                raise RuntimeError(error.value.decode("utf-8", "replace"))
            self.playback.on_available = lambda available: self.dll.qarvtk_qt_controls_playback_visible(self.handle, int(available))
            self.playback.available = None
            self.playback.update_available()
        except Exception:
            self.close()
            raise

    def refresh_views(self):
        names = [str(name) for name in self.layout.sliceViewNames()]
        names.sort(key=lambda name: (("Red", "Yellow", "Green").index(name)
                                    if name in ("Red", "Yellow", "Green") else 3, name))
        for name in names:
            if name in self.names:
                continue
            view = self.layout.sliceWidget(name)
            if not view or not view.mrmlSliceNode().IsViewVisibleInLayout():
                continue
            color = qt.QColor.fromRgbF(*view.mrmlSliceNode().GetLayoutColor()).name()
            error = c.create_string_buffer(2048)
            index = self.dll.qarvtk_qt_controls_add_slice(self.handle, name.encode("utf-8"), color.encode("ascii"), error, len(error))
            if index < 0:
                raise RuntimeError(error.value.decode("utf-8", "replace"))
            self.names.append(name)
            self.update_row(index)

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
            self.update_row(index)
        if self.dll.qarvtk_qt_controls_tick(self.handle, int(active), error, len(error)) < 0:
            raise RuntimeError(error.value.decode("utf-8", "replace"))
        if active:
            self.apply_changes(True)

    def update_row(self, index):
        slider = self.slider(index)
        view = self.layout.sliceWidget(self.names[index])
        state = (float(slider.minimum), float(slider.maximum), max(float(slider.singleStep), 1e-6),
                 float(slider.value), bool(slider.enabled and slider.maximum > slider.minimum and
                                          view.mrmlSliceNode().IsViewVisibleInLayout())) if slider else (0., 0., 1., 0., False)
        if self.states.get(index) != state:
            error = c.create_string_buffer(2048)
            if self.dll.qarvtk_qt_controls_update(self.handle, index, *state, error, len(error)) != 0:
                raise RuntimeError(error.value.decode("utf-8", "replace"))
            self.states[index] = state

    def close(self):
        if self.playback:
            self.playback.close()
            self.playback = None
        if self.handle:
            self.dll.qarvtk_qt_controls_destroy(self.handle)
            self.handle = None

    def status(self):
        text = c.create_string_buffer(2048)
        self.dll.qarvtk_qt_controls_status(self.handle, text, len(text))
        return text.value.decode("utf-8", "replace")


class SequenceControls:
    """Stream Slicer's own playback widgets, bound to the same MRML browser.

    Qt/MRML updates the controls by events. No frame timer, copied sequence,
    rendering pipeline or second video stream is needed here.
    """
    def __init__(self, name):
        self.browser = None
        self.on_available = None
        self.available = None
        self.widget = qt.QWidget()
        self.widget.objectName = name
        self.widget.setStyleSheet("QAbstractButton {min-width:38px;min-height:38px;} QComboBox {min-height:36px;}")
        layout = qt.QVBoxLayout(self.widget)
        layout.setContentsMargins(0, 4, 0, 0)
        label = qt.QLabel("Sequence playback", self.widget)
        layout.addWidget(label)
        self.selector = slicer.qMRMLNodeComboBox(self.widget)
        self.selector.objectName = "sequenceSelector"
        self.selector.nodeTypes = ["vtkMRMLSequenceBrowserNode"]
        self.selector.addEnabled = self.selector.removeEnabled = False
        self.selector.noneEnabled = False
        self.selector.setMRMLScene(slicer.mrmlScene)
        layout.addWidget(self.selector)
        self.play = slicer.qMRMLSequenceBrowserPlayWidget(self.widget)
        self.play.objectName = "sequencePlay"
        self.play.setRecordingControlsVisible(False)
        row = qt.QHBoxLayout()
        row.addWidget(self.play)
        self.stop = qt.QPushButton("Stop", self.widget)
        self.stop.objectName = "sequenceStop"
        self.stop.toolTip = "Pause playback and return to the first frame"
        self.stop.connect("clicked()", self.stop_playback)
        row.addWidget(self.stop)
        layout.addLayout(row)
        self.seek = slicer.qMRMLSequenceBrowserSeekWidget(self.widget)
        self.seek.objectName = "sequenceSeek"
        layout.addWidget(self.seek)
        self.selector.connect("currentNodeChanged(vtkMRMLNode*)", self.bind)
        # Prefer the sequence whose proxy is already rendered in this scene.
        for browser in slicer.util.getNodesByClass("vtkMRMLSequenceBrowserNode"):
            master = browser.GetMasterSequenceNode()
            proxy = browser.GetProxyNode(master) if master else None
            displays = proxy.GetNumberOfDisplayNodes() if proxy and hasattr(proxy, "GetNumberOfDisplayNodes") else 0
            if any(proxy.GetNthDisplayNode(i).IsA("vtkMRMLVolumeRenderingDisplayNode") and
                   proxy.GetNthDisplayNode(i).GetVisibility() for i in range(displays)):
                self.selector.setCurrentNode(browser)
                break
        self.bind(self.selector.currentNode())

    def bind(self, browser):
        self.browser = browser
        self.play.setMRMLSequenceBrowserNode(browser)
        self.seek.setMRMLSequenceBrowserNode(browser)
        self.available = None
        self.update_available()

    def update_available(self, *args):
        # Keep the selector accessible even for an empty browser. The native
        # MRML widgets enable their own actions when frames become available.
        available = self.browser is not None
        if available != self.available:
            self.available = available
            if self.on_available:
                self.on_available(available)

    def stop_playback(self):
        if self.browser:
            self.browser.SetPlaybackActive(False)
            self.browser.SetSelectedItemNumber(0)

    def close(self):
        self.on_available = None
        self.selector.disconnect("currentNodeChanged(vtkMRMLNode*)", self.bind)
        self.stop.disconnect("clicked()", self.stop_playback)
        self.browser = None
        self.play.setMRMLSequenceBrowserNode(None)
        self.seek.setMRMLSequenceBrowserNode(None)
