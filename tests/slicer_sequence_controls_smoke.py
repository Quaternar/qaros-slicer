"""Check Slicer's real Qt playback widgets against MRML, without a QAROS session."""
from pathlib import Path
import sys
import time
import traceback
import numpy as np
import qt
import slicer

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'integrations/slicer'))
from qarvtk_host_controls import SequenceControls


def run():
    controls = SequenceControls('sequenceControlsSmoke')
    assert not controls.available
    volume = slicer.mrmlScene.AddNewNodeByClass('vtkMRMLScalarVolumeNode', 'Playback proxy')
    sequence = slicer.mrmlScene.AddNewNodeByClass('vtkMRMLSequenceNode', 'Four frames')
    for index in range(4):
        slicer.util.updateVolumeFromArray(volume, np.full((4, 4, 4), index, dtype=np.int16))
        sequence.SetDataNodeAtValue(volume, str(index))
    browser = slicer.mrmlScene.AddNewNodeByClass('vtkMRMLSequenceBrowserNode', 'Playback browser')
    browser.SetAndObserveMasterSequenceNodeID(sequence.GetID())
    browser.AddProxyNode(volume, sequence, False)
    controls.selector.setCurrentNode(browser)
    assert controls.available
    controls.seek.setSelectedItemNumber(2)
    assert browser.GetSelectedItemNumber() == 2
    assert np.all(slicer.util.arrayFromVolume(volume) == 2)
    browser.SetSelectedItemNumber(1)
    slider = controls.seek.findChild(qt.QSlider, 'slider_IndexValue')
    assert slider.value == 1
    controls.play.onVcrFirst()
    assert browser.GetSelectedItemNumber() == 0
    controls.play.onVcrLast()
    assert browser.GetSelectedItemNumber() == 3
    browser.SetPlaybackLooped(True)
    browser.SetPlaybackRateFps(5.)
    play = controls.play.findChild(qt.QPushButton, 'pushButton_VcrPlayPause')
    play.click()
    assert browser.GetPlaybackActive()
    start = browser.GetSelectedItemNumber()
    deadline = time.perf_counter() + 3.
    while browser.GetSelectedItemNumber() == start and time.perf_counter() < deadline:
        slicer.app.processEvents()
        time.sleep(.01)
    assert browser.GetSelectedItemNumber() != start, 'Native playback timer did not advance'
    play.click()
    assert not browser.GetPlaybackActive()
    controls.stop.click()
    assert not browser.GetPlaybackActive() and browser.GetSelectedItemNumber() == 0
    other = slicer.mrmlScene.AddNewNodeByClass('vtkMRMLSequenceBrowserNode', 'Empty browser')
    controls.selector.setCurrentNode(other)
    assert controls.available and not controls.play.enabled
    controls.selector.setCurrentNode(browser)
    assert controls.available
    controls.close()
    controls.widget.deleteLater()
    print('PASS: native Qt play/pause, first/last, seek, Stop, actual proxy data and browser selection', flush=True)


try:
    run()
except Exception:
    traceback.print_exc()
    slicer.app.exit(1)
else:
    slicer.app.exit(0)
