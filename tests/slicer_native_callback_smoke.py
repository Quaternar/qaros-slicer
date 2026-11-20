"""Exercise native Qt input with real PythonQt Stop and MRML time controls.

Run with Slicer's --python-script. Set QARVTK_QT_FIXTURE to the built
qarvtk-qt-controls-fixture.dll and QARVTK_CALLBACK_RESULT to an output file.
QARVTK_TEST_UNSAFE_LOADER=1 reproduces the previous CDLL/GIL failure.
No QAROS connection is created.
"""
from pathlib import Path
import ctypes as c
import os
import sys
import time
import traceback
import numpy as np
import qt
import slicer

result_path = Path(os.environ['QARVTK_CALLBACK_RESULT'])
result_path.write_text('started', encoding='utf-8')
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'integrations/slicer'))
import qarvtk_host_controls as controls_module


def run():
    if os.environ.get('QARVTK_TEST_UNSAFE_LOADER') == '1':
        class UnsafeLoader:
            PyDLL = c.CDLL

            def __getattr__(self, name):
                return getattr(c, name)
        controls_module.c = UnsafeLoader()
    dll = controls_module.controls_library(os.environ['QARVTK_QT_FIXTURE'])
    dll.qarvtk_test_panel_tap.argtypes = [c.c_int, c.c_int]
    dll.qarvtk_test_panel_tap.restype = None
    dll.qarvtk_test_panel_gesture.argtypes = [c.c_int, c.c_int, c.c_uint, c.c_uint,
                                             c.c_uint64, c.c_int, c.c_int]
    dll.qarvtk_test_panel_gesture.restype = None
    error = c.create_string_buffer(2048)
    handle = dll.qarvtk_qt_controls_create(c.c_void_p(1), 0., 0., -1., error, len(error))
    assert handle, error.value
    controls = controls_module.SequenceControls('nativeCallbackSmoke')
    volume = slicer.mrmlScene.AddNewNodeByClass('vtkMRMLScalarVolumeNode', 'Callback proxy')
    sequence = slicer.mrmlScene.AddNewNodeByClass('vtkMRMLSequenceNode', 'Callback frames')
    for index in range(4):
        slicer.util.updateVolumeFromArray(volume, np.full((4, 4, 4), index, dtype=np.int16))
        sequence.SetDataNodeAtValue(volume, str(index))
    browser = slicer.mrmlScene.AddNewNodeByClass('vtkMRMLSequenceBrowserNode', 'Callback browser')
    browser.SetAndObserveMasterSequenceNodeID(sequence.GetID())
    browser.AddProxyNode(volume, sequence, False)
    controls.selector.setCurrentNode(browser)
    assert dll.qarvtk_qt_controls_attach_playback(
        handle, controls.widget.objectName.encode('utf-8'), error, len(error)) == 0, error.value
    dll.qarvtk_qt_controls_playback_visible(handle, 1)

    def pump():
        slicer.app.processEvents()
        assert dll.qarvtk_qt_controls_tick(handle, 1, error, len(error)) >= 0, error.value
        slicer.app.processEvents()
    for _ in range(30):
        pump()
        time.sleep(.01)
    roots = [w for w in qt.QApplication.topLevelWidgets() if w.objectName == 'qarvtkSliceControls']
    assert len(roots) == 1
    root = roots[0]
    callbacks = []

    def clicked():
        callbacks.append(True)

    controls.stop.connect('clicked()', clicked)
    for index in range(10):
        browser.SetSelectedItemNumber(3)
        browser.SetPlaybackActive(True)
        point = controls.stop.mapTo(root, controls.stop.rect.center())
        dll.qarvtk_test_panel_tap(point.x(), point.y())
        pump()
        assert len(callbacks) == index + 1, 'Native mouse tap missed the PythonQt slot'
        assert not browser.GetPlaybackActive() and browser.GetSelectedItemNumber() == 0
        assert np.all(slicer.util.arrayFromVolume(volume) == 0)
    controls.stop.disconnect('clicked()', clicked)
    sliders = controls.seek.findChildren(qt.QSlider)
    print('Seek sliders:', [(w.objectName, type(w).__name__, w.minimum, w.maximum,
                             w.value, w.size.width(), w.size.height()) for w in sliders], flush=True)
    assert len(sliders) == 1, 'Expected one actual time slider'
    slider = sliders[0]
    browser.SetSelectedItemNumber(0)
    pump()
    origin = slider.mapTo(root, qt.QPoint(22, slider.height // 2))
    def gesture(point, kind, phase):
        dll.qarvtk_test_panel_gesture(point.x(), point.y(), kind, phase, 80 if kind == 0 else 17,
                                      origin.x(), origin.y())
        pump()
    gesture(origin, 0, 0)
    gesture(origin + qt.QPoint(40, 0), 3, 0)
    assert slider.sliderDown, 'Native drag did not grab the time slider'
    gesture(origin + qt.QPoint(slider.width // 2, 0), 3, 1)
    assert slider.sliderPosition > 0, 'Time slider did not move its preview handle'
    assert browser.GetSelectedItemNumber() == 0, 'Scrubbing loaded a volume before release'
    assert np.all(slicer.util.arrayFromVolume(volume) == 0)
    gesture(origin + qt.QPoint(slider.width // 2, 0), 0, 1)
    gesture(origin + qt.QPoint(slider.width // 2, 0), 3, 2)
    assert not slider.sliderDown, 'Native release left the time slider pressed'
    assert browser.GetSelectedItemNumber() > 0, 'Release did not commit the chosen MRML frame'
    assert np.all(slicer.util.arrayFromVolume(volume) == browser.GetSelectedItemNumber())
    released_frame = browser.GetSelectedItemNumber()
    gesture(origin + qt.QPoint(slider.width - 40, 0), 0, 1)
    assert not slider.sliderDown and browser.GetSelectedItemNumber() == released_frame, (
        'Hover after release continued seeking the sequence')
    gesture(origin + qt.QPoint(slider.width - 40, 0), 0, 2)
    browser.SetSelectedItemNumber(0)
    pump()
    gesture(origin + qt.QPoint(40, 0), 3, 0)
    gesture(origin + qt.QPoint(slider.width // 2, 0), 3, 1)
    assert slider.sliderDown
    time.sleep(.85)
    # A lost drag end must time out even when hover continues over the panel.
    gesture(origin + qt.QPoint(slider.width - 40, 0), 0, 1)
    assert not slider.sliderDown, 'Lost drag end left the time slider pressed after timeout'
    browser.SetSelectedItemNumber(0)
    pump()
    changes = []

    def frame_changed(*args):
        frame = browser.GetSelectedItemNumber()
        if not changes or changes[-1] != frame:
            changes.append(frame)

    observer = browser.AddObserver('ModifiedEvent', frame_changed)
    gesture(origin + qt.QPoint(40, 0), 3, 0)
    for index in range(60):
        point = origin + qt.QPoint(slider.width - 40 if index % 2 else slider.width // 3, 0)
        for kind in (0, 3):
            dll.qarvtk_test_panel_gesture(point.x(), point.y(), kind, 1,
                                          80 if kind == 0 else 17, origin.x(), origin.y())
    dll.qarvtk_test_panel_gesture(point.x(), point.y(), 3, 2, 17, origin.x(), origin.y())
    pump()
    browser.RemoveObserver(observer)
    assert not slider.sliderDown
    assert len(changes) <= 1, 'Queued movement replayed %d obsolete MRML frame changes' % len(changes)
    controls.close()
    dll.qarvtk_qt_controls_destroy(handle)
    print('PASS: native Stop callbacks and actual MRML time-slider drag/release/hover', flush=True)


try:
    run()
except Exception:
    result_path.write_text(traceback.format_exc(), encoding='utf-8')
    traceback.print_exc()
    slicer.app.exit(1)
else:
    result_path.write_text('passed', encoding='utf-8')
    slicer.app.exit(0)
