"""Preview/video scheduling without a graphics context or SDK receiver."""
import importlib.util
from math import hypot
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace

path = Path(__file__).resolve().parents[1] / "integrations/slicer/qarvtk_host_panels.py"
spec = importlib.util.spec_from_file_location("host_panels_test_module", path)
panels = importlib.util.module_from_spec(spec)
spec.loader.exec_module(panels)


class Window:
    def __init__(self):
        self.revision = 0
        self.observers = {}
        self.size = (640, 360)
        self.stamp = 1
    def GetContextCreationTime(self): return self.stamp
    def GetSize(self): return self.size
    def MakeCurrent(self): pass
    def GetNeverRendered(self): return False
    def GetDisplayFramebuffer(self): return self
    def GetColorAttachmentAsTextureObject(self, index): return self
    def GetHandle(self): return self.revision + 100
    def GetWidth(self): return self.size[0]
    def GetHeight(self): return self.size[1]
    def AddObserver(self, event, callback):
        index = max(self.observers, default=0) + 1
        self.observers[index] = callback
        return index
    def RemoveObserver(self, index): del self.observers[index]
    def frame(self):
        self.revision += 1
        for callback in list(self.observers.values()): callback()
    def Render(self): raise AssertionError("Preview must never trigger rendering")


class Sink:
    def __init__(self):
        self.next = 0
        self.copies, self.closed, self.visibility, self.events = [], [], [], []
        self.created = []
        self.diagnostics = type("Diagnostics", (), {"event": self.events.append})()
        self.ready, self.fail = True, False
        self.running, self.stopping, self.panels = True, False, None
    def panel_create(self, *args):
        self.created.append(args)
        self.next += 1
        return self.next
    def panel_capture(self, handle, texture, width, height, region=None):
        if self.fail: raise RuntimeError("Panel encoder failed")
        if not self.ready: return False
        self.copies.append((handle, texture, width, height))
        return True
    def panel_visible(self, handle, visible): self.visibility.append((handle, visible))
    def panel_close(self, handle): self.closed.append(handle)
    def panel_status(self, handle):
        return "Live 640 x 360; 1 frames"


class PanelTest(unittest.TestCase):
    def fixture(self):
        window, sink = Window(), Sink()
        view = type("View", (), {"renderWindow": lambda self: window, "isVisible": lambda self: self.visible})()
        view.visible = True
        node = type("Node", (), {"IsViewVisibleInLayout": lambda self: True})()
        widget = type("Widget", (), {"sliceView": lambda self: view, "mrmlSliceNode": lambda self: node})()
        layout = type("Layout", (), {"sliceViewNames": lambda self: ["Red"], "sliceWidget": lambda self, name: widget})()
        now = [0.]
        manager = panels.SlicePanels(layout, sink, (0, 0, -1.5), (.65, .65, .65), fps=15, clock=lambda: now[0])
        return window, sink, view, now, manager

    def test_video_frames_are_coalesced_and_rate_limited(self):
        window, sink, view, now, manager = self.fixture()
        manager.tick()
        self.assertEqual(len(sink.copies), 1)
        for _ in range(10): window.frame()
        now[0] = .03; manager.tick()
        self.assertEqual(len(sink.copies), 1)
        sink.ready = False; now[0] = .08; manager.tick()
        for _ in range(20): window.frame()
        sink.ready = True; now[0] = .11; manager.tick()
        self.assertEqual([x[1] for x in sink.copies], [100, 130])
        now[0] = .5; manager.tick()
        self.assertEqual(len(sink.copies), 2, "Static view was captured repeatedly")
        window.frame(); now[0] = .6; manager.tick()
        self.assertEqual(sink.copies[-1][1], 131)
        manager.close()
        self.assertFalse(window.observers)

    def test_hide_restore_context_recreation_and_error_are_local(self):
        window, sink, view, now, manager = self.fixture()
        manager.tick(); view.visible = False; now[0] = .3; manager.tick()
        self.assertEqual(sink.visibility[-1], (1, False))
        window.frame(); now[0] = .4; manager.tick(); self.assertEqual(len(sink.copies), 1)
        view.visible = True; now[0] = .6; manager.tick()
        self.assertEqual(sink.visibility[-1], (1, True))
        window.stamp = 2; now[0] = .9; manager.tick()
        self.assertIn(1, sink.closed); self.assertEqual(sink.copies[-1][0], 2)
        sink.fail = True; window.frame(); now[0] = 1.2; manager.tick()
        self.assertIn("Red", manager.errors)
        self.assertIn(2, sink.closed)
        self.assertFalse(window.observers)
        manager.close()

    def test_dimensions_are_bounded_and_even(self):
        self.assertEqual(panels.panel_size(1920, 1080), (768, 432))
        self.assertEqual(panels.panel_size(451, 726), (450, 726))

    def test_initial_rows_are_compact_centered_and_do_not_overlap(self):
        sizes = dict(Red=(.25, .25), Yellow=(.31, .174), Green=(.174, .31), Video=(.25, .25))
        placed = panels.panel_positions(sizes, (0., 0., -1.5), (.2, .3, .4))
        self.assertAlmostEqual(placed['Red'][0]-.125, -(placed['Green'][0]+.087))
        for a, b in (('Red','Yellow'), ('Yellow','Green')):
            self.assertAlmostEqual(placed[b][0]-sizes[b][0]/2 - (placed[a][0]+sizes[a][0]/2), .025)
        self.assertEqual(placed['Video'][0], 0.)
        self.assertAlmostEqual(placed['Red'][1]-.31-placed['Video'][1], .025)

    def test_physical_size_reaches_transport_once_independent_of_model_size(self):
        for pixels in ((1920, 1080), (640, 360), (360, 640), (512, 512)):
            for workspace in ((.03, .04, .02), (.24, .32, .18), (2.4, 3.2, 1.8)):
                with self.subTest(pixels=pixels, workspace=workspace):
                    window, sink, view, now, manager = self.fixture()
                    window.size = pixels
                    manager.workspace = workspace
                    manager.tick()
                    name, size, position, width_m = sink.created[0]
                    height_m = width_m * size[1] / size[0]  # Native boundary contract.
                    self.assertAlmostEqual(hypot(width_m, height_m), .3556)
                    self.assertAlmostEqual(width_m / height_m, size[0] / size[1])
                    # Resizing the source or hiding/showing cannot reset a user's
                    # physical panel size; only a new stream/panel does that.
                    window.size = (800, 600); window.frame(); now[0] = .3; manager.tick()
                    view.visible = False; now[0] = .6; manager.tick()
                    view.visible = True; now[0] = .9; manager.tick()
                    self.assertEqual(len(sink.created), 1)
                    manager.close()

    def test_crop_uses_geometry_including_black_pixels_and_transformed_slices(self):
        image = panels.vtk.vtkImageData()
        image.SetDimensions(100, 100, 1)
        # Voxel edges map to x=120..220, y=70..170 within a 640x360 view.
        transform = panels.vtk.vtkTransform()
        transform.Translate(-120.5, -70.5, 0.)
        volume = SimpleNamespace(GetImageData=lambda: image, GetParentTransformNode=lambda: None,
                                 GetName=lambda: "Animated data")
        layer = SimpleNamespace(GetVolumeNode=lambda: volume, GetXYToIJKTransform=lambda: transform)
        composite = SimpleNamespace(GetForegroundOpacity=lambda: 0., GetLabelOpacity=lambda: 0.)
        logic = SimpleNamespace(GetBackgroundLayer=lambda: layer, GetSliceCompositeNode=lambda: composite)
        node = SimpleNamespace(GetLayoutGridRows=lambda: 1, GetLayoutGridColumns=lambda: 1,
                               GetDimensions=lambda: (640, 360, 1))
        widget = SimpleNamespace(sliceLogic=lambda: logic, mrmlSliceNode=lambda: node)
        self.assertEqual(panels.image_region(widget, (640, 360)), (120, 70, 100, 100))
        self.assertEqual(panels.image_region(widget, (1280, 720)), (240, 140, 200, 200))
        self.assertEqual(panels.panel_title(widget, "Red"), "Animated data · Red")
        # Rotating image coordinates must include all visible image corners.
        transform.Identity(); transform.RotateZ(90.); transform.Translate(-200.5, -50.5, 0.)
        self.assertEqual(panels.image_region(widget, (640, 360)), (200, 0, 100, 51))
        # Nonlinear geometry needs a conservative full-view fallback.
        volume.GetParentTransformNode = lambda: SimpleNamespace(IsTransformToWorldLinear=lambda: False)
        self.assertEqual(panels.image_region(widget, (640, 360)), (0, 0, 640, 360))

    def test_corner_labels_wait_for_clean_frame_and_are_restored(self):
        window, sink, view, now, manager = self.fixture()
        annotation = panels.vtk.vtkCornerAnnotation()
        annotation.SetVisibility(True)
        view.cornerAnnotation = lambda: annotation
        scheduled = []
        view.scheduleRender = lambda: scheduled.append(True)
        manager.tick()
        self.assertFalse(annotation.GetVisibility())
        self.assertFalse(sink.copies, "An old annotated texture was sent before a clean frame")
        # Fake an ordinary Slicer render's completion, not a plugin Render().
        manager.sources["Red"].rendered()
        now[0] = .1; manager.tick()
        self.assertEqual(len(sink.copies), 1)
        manager.close()
        self.assertTrue(annotation.GetVisibility())
        self.assertEqual(len(scheduled), 2)
        self.assertFalse(window.observers)



if __name__ == "__main__": unittest.main()
