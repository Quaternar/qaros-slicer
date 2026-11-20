"""Dispatch native Qt taps into a real PythonQt Stop slot and MRML browser.

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
    controls.close()
    dll.qarvtk_qt_controls_destroy(handle)
    print('PASS: 10 native DLL mouse taps call PythonQt Stop and reset the actual MRML proxy', flush=True)


try:
    run()
except Exception:
    result_path.write_text(traceback.format_exc(), encoding='utf-8')
    traceback.print_exc()
    slicer.app.exit(1)
else:
    result_path.write_text('passed', encoding='utf-8')
    slicer.app.exit(0)
