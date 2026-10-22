"""Real Slicer slice/video preview test; readback is an oracle only."""
from pathlib import Path
import sys
import traceback
import unittest
import numpy as np
import slicer
import vtk
from vtk.util.numpy_support import vtk_to_numpy

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "package/QAROS-VTK-plugins/slicer"))
sys.path.insert(0, str(root / "tests"))
from QarosVTKLib.qarvtk_host_panels import SlicePanels, image_region
import test_host_panels


def pixels(window):
    w, h = window.GetSize()
    data = vtk.vtkUnsignedCharArray()
    window.GetRGBACharPixelData(0, 0, w-1, h-1, 1, data)
    return vtk_to_numpy(data).copy()

def image_pixels(name):
    widget = slicer.app.layoutManager().sliceWidget(name)
    window = widget.sliceView().renderWindow()
    w, h = window.GetSize()
    x, y, cw, ch = image_region(widget, (w, h))
    return pixels(window).reshape(h, w, 4)[y:y+ch, x:x+cw].copy()


def run():
    result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromModule(test_host_panels))
    assert result.wasSuccessful()
    lm = slicer.app.layoutManager()
    lm.setLayout(slicer.vtkMRMLLayoutNode.SlicerLayoutFourUpView)
    ct = slicer.mrmlScene.AddNewNodeByClass("vtkMRMLScalarVolumeNode", "Moving slice frames")
    z, y, x = np.indices((32, 64, 96))
    base = (z * 8 + y * 3 + x * 5).astype(np.int16)
    slicer.util.updateVolumeFromArray(ct, base)
    ct.CreateDefaultDisplayNodes()
    ct.GetDisplayNode().AutoWindowLevelOff()
    ct.GetDisplayNode().SetWindowLevel(2000, 1000)
    slicer.util.setSliceViewerLayers(background=ct)
    slicer.util.resetSliceViews()
    slicer.app.processEvents()
    windows = {name: lm.sliceWidget(name).sliceView().renderWindow() for name in ("Red", "Yellow", "Green")}
    annotations = {name: lm.sliceWidget(name).sliceView().cornerAnnotation().GetVisibility() for name in windows}
    counts = dict.fromkeys(windows, 0)
    observers = []
    for name, window in windows.items():
        window.Render()
        observers.append((window, window.AddObserver(vtk.vtkCommand.StartEvent,
            lambda caller, event, name=name: counts.__setitem__(name, counts[name] + 1))))

    class Sink:
        def __init__(self):
            self.next = 0
            self.names, self.sizes, self.images = {}, {}, {}
            self.frames, self.closed, self.visibility, self.events = [], [], [], []
            self.ready = True
            self.diagnostics = type("Log", (), {"event": self.events.append})()
        def panel_create(self, name, size, position, width_m):
            self.next += 1
            self.names[self.next], self.sizes[self.next] = name.rsplit(" · ", 1)[-1].removeprefix("Slicer "), size
            assert "Moving slice frames" in name, "Data name missing from panel title"
            assert max(size) <= 768 and all(x % 2 == 0 for x in size)
            assert width_m <= .45
            return self.next
        def panel_capture(self, handle, texture, w, h, region):
            if not self.ready: return False
            name = self.names[handle]
            window = lm.sliceWidget(name).sliceView().renderWindow()
            color = window.GetDisplayFramebuffer().GetColorAttachmentAsTextureObject(0)
            assert texture == color.GetHandle() and (w, h) == (color.GetWidth(), color.GetHeight())
            x, y, cw, ch = region
            self.images[name] = pixels(window).reshape(h, w, 4)[y:y+ch, x:x+cw].copy()
            self.frames.append((name, w, h))
            return True
        def panel_visible(self, handle, visible): self.visibility.append((self.names[handle], visible))
        def panel_close(self, handle): self.closed.append(handle)
        def panel_status(self, handle): return "fixture live"

    sink, now = Sink(), [0.]
    previews = SlicePanels(lm, sink, (0, 0, -1.5), (.65, .65, .65), fps=30, clock=lambda: now[0])
    previews.refresh()
    # The one normal Qt refresh after hiding labels must finish before capture.
    slicer.app.processEvents()
    for window in windows.values(): window.Render()
    before = {name: pixels(window) for name, window in windows.items()}
    expected = {name: image_pixels(name) for name in windows}
    before_counts = dict(counts)
    for _ in range(3): previews.tick()
    assert set(sink.images) == set(windows), (previews.status, previews.errors)
    assert counts == before_counts, "Preview caused additional slice rendering"
    for name, window in windows.items():
        assert np.array_equal(sink.images[name], expected[name]), "Panel differs from the image rectangle"
        assert not lm.sliceWidget(name).sliceView().cornerAnnotation().GetVisibility()
        assert np.array_equal(pixels(window), before[name]), "Preview changed the desktop"

    # Animated image data follows the same Render/EndEvent lifecycle as a
    # sequence/video player. A stalled receiver must resume at its newest frame.
    sink.ready = False
    previous_red, video_changed = before["Red"], False
    for frame in range(12):
        data = base.copy()
        data[:, :, frame*5:frame*5+15] = 1900
        slicer.util.updateVolumeFromArray(ct, data)
        slicer.app.processEvents()
        for window in windows.values(): window.Render()
        red = pixels(windows["Red"])
        # Qt can settle the splitter geometry after the initial snapshot. Only
        # compare equal-sized frames when checking that the video really moves.
        video_changed |= (red.shape == previous_red.shape and np.count_nonzero(red != previous_red) > 100)
        previous_red = red
        now[0] += .04
        previews.tick()
    assert len(sink.frames) == 3, "Busy receiver accepted a queued frame"
    latest = {name: image_pixels(name) for name in windows}
    sink.ready = True
    now[0] += .1
    before_counts = dict(counts)
    for _ in range(3): previews.tick()
    assert len(sink.frames) == 6 and counts == before_counts
    for name in windows:
        assert np.array_equal(sink.images[name], latest[name]), "Receiver got a stale video frame"
    assert video_changed, "Video fixture did not change the image"
    now[0] += .2
    for _ in range(3): previews.tick()
    assert len(sink.frames) == 6, "Static image triggered a new GPU capture"

    # Same panels follow layout visibility. Restoring the layout refreshes their
    # existing native views; no substitute renderer or extra depth path exists.
    lm.setLayout(slicer.vtkMRMLLayoutNode.SlicerLayoutOneUp3DView)
    slicer.app.processEvents(); now[0] += .3; previews.tick()
    assert not any(s.visible for s in previews.sources.values())
    lm.setLayout(slicer.vtkMRMLLayoutNode.SlicerLayoutFourUpView)
    slicer.app.processEvents()
    for name in windows: lm.sliceWidget(name).sliceView().renderWindow().Render()
    now[0] += .3
    for _ in range(3): previews.tick()
    assert len(previews.sources) == 3 and all(s.visible for s in previews.sources.values())
    assert not previews.errors, previews.errors
    count = len(sink.frames)
    previews.close()
    for name in windows:
        assert lm.sliceWidget(name).sliceView().cornerAnnotation().GetVisibility() == annotations[name]
    for name in windows: lm.sliceWidget(name).sliceView().renderWindow().Render()
    now[0] += .5; previews.tick()
    assert len(sink.frames) == count
    assert len(sink.closed) == sink.next, "Panel handles were not retired on Stop"
    for window, observer in observers: window.RemoveObserver(observer)
    print("PASS: real Red/Yellow/Green image-only GPU rectangles; data titles; corner labels restored on Stop; "
          "video/sequence frames coalesced under backpressure; no extra renders; layout hide/restore; stop cleanup", flush=True)


try:
    run()
except Exception:
    traceback.print_exc()
    slicer.app.exit(1)
else:
    slicer.app.exit(0)
