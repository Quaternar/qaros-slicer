"""Slicer or --vtk-egl: native color and independent per-volume depth oracles.

EGL exercises the real GPU renderer without a GUI; it does not validate MRML,
Qt attachment preservation, Windows transport or the NVIDIA encoder.
"""
from pathlib import Path
from types import SimpleNamespace
import sys
import traceback
import numpy as np
import vtk
from vtk.util.numpy_support import numpy_to_vtk, vtk_to_numpy

EGL = "--vtk-egl" in sys.argv
if not EGL:
    import slicer

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "package" / "QAROS-VTK-plugins" / "slicer"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from QarosVTKLib.qarvtk_host_view import HostView, create_host_views, workspace_envelope
from test_host_view_math import sdk_eye


def pixels(window, depth=False):
    width, height = window.GetSize()
    data = vtk.vtkFloatArray() if depth else vtk.vtkUnsignedCharArray()
    if depth:
        window.GetZbufferData(0, 0, width-1, height-1, data)
    else:
        window.GetRGBACharPixelData(0, 0, width-1, height-1, 1, data)
    return vtk_to_numpy(data).copy().reshape((height, width) if depth else (height, width, 4))


def state(volume):
    mapper = volume.GetMapper()
    return (volume.GetShaderProperty(), volume.GetPropertyKeys(),
            mapper.GetAutoAdjustSampleDistances(), mapper.GetImageSampleDistance(),
            mapper.GetRenderToImage(), volume.GetVisibility())


def run():
    if EGL:
        window = vtk.vtkEGLRenderWindow()
        window.SetOffScreenRendering(True)
        window.SetSize(320, 240)
        window.SetMultiSamples(0)
        renderer = vtk.vtkRenderer()
        window.AddRenderer(renderer)
        view = SimpleNamespace(renderWindow=lambda: window, isVisible=lambda: True,
                               resetFocalPoint=renderer.ResetCamera, renderEnabled=True)
        logic = None
    else:
        slicer.app.layoutManager().setLayout(slicer.vtkMRMLLayoutNode.SlicerLayoutOneUp3DView)
        view = slicer.app.layoutManager().threeDWidget(0).threeDView()
        window = view.renderWindow()
        renderer = window.GetRenderers().GetFirstRenderer()
        view.mrmlViewNode().SetBoxVisible(False)
        view.mrmlViewNode().SetAxisLabelsVisible(False)
        logic = slicer.modules.volumerendering.logic()
    renderer.SetBackground(0., 0., 0.)
    renderer.GradientBackgroundOff()
    z, y, x = np.indices((32, 32, 32))
    data = np.where((x-15.5)**2+(y-15.5)**2+(z-15.5)**2 < 12**2, 900, 0).astype(np.int16)
    displays = []
    for index, (origin, color) in enumerate((((-75., -30., -31.), (1., .1, .1)),
                                            ((15., 15., -31.), (.1, .2, 1.)))):
        if EGL:
            image = vtk.vtkImageData()
            image.SetDimensions(32, 32, 32)
            image.SetSpacing(2., 2., 2.)
            image.SetOrigin(origin)
            image.GetPointData().SetScalars(numpy_to_vtk(data.ravel(), deep=True))
            mapper = vtk.vtkOpenGLGPUVolumeRayCastMapper()
            mapper.SetInputData(image)
            volume = vtk.vtkVolume()
            volume.SetMapper(mapper)
            renderer.AddVolume(volume)
            prop = volume.GetProperty()
            display = None
        else:
            node = slicer.mrmlScene.AddNewNodeByClass("vtkMRMLScalarVolumeNode", "Multi CT " + str(index))
            slicer.util.updateVolumeFromArray(node, data)
            node.SetSpacing(2., 2., 2.)
            node.SetOrigin(origin)
            node.CreateDefaultDisplayNodes()
            display = logic.CreateDefaultVolumeRenderingNodes(node)
            display.SetVisibility(True)
            prop = display.GetVolumePropertyNode().GetVolumeProperty()
        prop.GetRGBTransferFunction().RemoveAllPoints()
        prop.GetRGBTransferFunction().AddRGBPoint(0, *color)
        prop.GetRGBTransferFunction().AddRGBPoint(900, *color)
        prop.GetScalarOpacity().RemoveAllPoints()
        prop.GetScalarOpacity().AddPoint(0, 0.)
        prop.GetScalarOpacity().AddPoint(899, 0.)
        prop.GetScalarOpacity().AddPoint(900, .25)
        prop.ShadeOff()
        displays.append(display)
    if not EGL:
        slicer.app.processEvents()
    view.resetFocalPoint()
    window.Render()
    host = HostView(view)
    volumes = host.volumes()
    assert len(volumes) == 2
    for volume in volumes:
        volume.GetMapper().SetAutoAdjustSampleDistances(False)
        volume.GetMapper().SetImageSampleDistance(1.)
    # Physical-size 62 mm fixtures need a closer camera to keep the pixel/depth
    # oracles well sampled at this small test resolution.
    eyes = [sdk_eye(host, (x, 0., .5)) for x in (-.032, .032)]

    class Oracle:
        def __init__(self, mapper=None):
            self.colors, self.depths = [], []
            self.mapper = mapper
        def color(self, index, color, depth, width, height):
            self.colors.append(pixels(window))
        def depth(self, index, color, depth, vc, vd, width, height):
            values = pixels(window, True)
            if vc is not None:
                assert self.mapper is not None
                image = vtk.vtkImageData()
                self.mapper.GetDepthImage(image)
                tissue = vtk_to_numpy(image.GetPointData().GetScalars()).copy().reshape(height, width)
                values = np.minimum(values, tissue)
            assert values.shape == (height, width)
            assert np.isfinite(values).all() and values.min() >= 0 and values.max() <= 1
            self.depths.append(values)

    def verify(label, requests=eyes):
        if EGL:
            window.Render()  # EGL restores by rendering; Qt keeps original attachments.
        saved = [state(v) for v in volumes]
        native = []
        for eye in requests:
            with host._frame_state():
                host._camera_for_eye(eye, *host.clip)
                window.SetSize(*eye["size"])
                window.Render()
                native.append(pixels(window))
        desktop = pixels(window), pixels(window, True)
        actual = Oracle()
        host.render(requests, actual)
        assert host.last_timings["host_render_calls"] == 4 + int(EGL), (label, host.last_timings)
        assert host.last_timings["volume_depth_passes"] == 2
        assert all(np.array_equal(a, pixels(window, depth)) for a, depth in zip(desktop, (False, True)))
        assert [state(v) for v in volumes] == saved, label
        for a, b in zip(actual.colors, native):
            assert np.max(np.abs(a.astype(int)-b.astype(int))) <= 2, (label, "native color changed")
        reference_eyes = [dict(eye, size=eye.get("depth_size", eye["size"])) for eye in requests]
        independent = []
        try:
            for selected in volumes:
                for volume in volumes:
                    volume.SetVisibility(volume == selected)
                keys = vtk.vtkInformation()
                old_keys = selected.GetPropertyKeys()
                if old_keys:
                    keys.Copy(old_keys)
                # Force VTK's RenderToImage fallback. This oracle does not use
                # the shader hook under test to derive the expected tissue depth.
                keys.Set(vtk.vtkOpenGLActor.GLDepthMaskOverride(), 0)
                selected.SetPropertyKeys(keys)
                try:
                    reference = Oracle(selected.GetMapper())
                    host.render(reference_eyes, reference)
                    assert host.last_timings["volume_depth_passes"] == 2
                    independent.append(reference.depths)
                finally:
                    selected.SetPropertyKeys(old_keys)
        finally:
            for volume in volumes:
                volume.SetVisibility(True)
        expected = np.minimum.reduce(independent)
        for actual_eye, expected_eye in zip(actual.depths, expected):
            error = np.max(np.abs(actual_eye-expected_eye))
            assert error < 2e-5, (label, "nearest independent tissue/surface depth", error)
        assert [state(v) for v in volumes] == saved
        print("PASS:", label, "native stereo color; nearest independent VTK depth; desktop/state restored", flush=True)
        return actual

    baseline = verify("two separated volumes")
    assert np.count_nonzero(baseline.colors[0][:, :, 0] > 100) > 30
    assert np.count_nonzero(baseline.colors[0][:, :, 2] > 100) > 30
    diagnostics = host.diagnostic_state()
    assert diagnostics["volume_count"] == len(diagnostics["volumes"]) == 2
    volumes[1].SetPosition(-75., 40., 0.)
    verify("transformed volumes overlapping in projection")
    for volume in volumes:
        renderer.RemoveVolume(volume)
    for volume in reversed(volumes):
        renderer.AddVolume(volume)
    verify("reversed volume draw order")
    half = [dict(eye, depth_size=(96, 80)) for eye in eyes]
    verify("half-resolution combined depth", half)

    sphere = vtk.vtkSphereSource()
    sphere.SetCenter(-35., -45., 0.)
    sphere.SetRadius(10.)
    mapper = vtk.vtkPolyDataMapper()
    mapper.SetInputConnection(sphere.GetOutputPort())
    surface = vtk.vtkActor()
    surface.SetMapper(mapper)
    renderer.AddActor(surface)
    verify("opaque surface occludes both volumes")
    renderer.RemoveActor(surface)

    if EGL:
        volumes[0].GetMapper().SetCroppingRegionPlanes(-62., -40., -30., 32., -31., 31.)
        volumes[0].GetMapper().CroppingOn()
    else:
        roi = logic.CreateROINode(displays[0])
        logic.FitROIToVolume(displays[0])
        roi.GetDisplayNode().SetVisibility(False)
        roi.SetSize(22., 62., 62.)
        displays[0].SetCroppingEnabled(True)
        slicer.app.processEvents()
    verify("live ROI cropping")

    volumes[1].SetPosition(10000., 0., 0.)
    verify("one volume offscreen")
    volumes[0].SetPosition(10000., 0., 0.)
    empty = verify("all volumes offscreen, no stale tissue depth")
    assert all(np.all(d == 1.) for d in empty.depths)
    for volume in volumes:
        volume.SetPosition(0., 0., 0.)

    class Broken(Oracle):
        def depth(self, index, *args):
            if index == 1:
                raise RuntimeError("injected multi-volume consumer failure")
            super().depth(index, *args)
    saved = [state(v) for v in volumes]
    if EGL:
        window.Render()
    desktop = pixels(window), pixels(window, True)
    try:
        host.render(eyes, Broken())
        raise AssertionError("Consumer failure was swallowed")
    except RuntimeError as error:
        assert str(error) == "injected multi-volume consumer failure"
    assert [state(v) for v in volumes] == saved
    assert all(np.array_equal(a, pixels(window, depth)) for a, depth in zip(desktop, (False, True)))
    assert view.renderEnabled

    unsupported = volumes[1].GetMapper()
    unsupported.SetBlendModeToMaximumIntensity()
    try:
        host.render(eyes, Oracle())
        raise AssertionError("Unsupported second volume was accepted")
    except ValueError as error:
        assert "Composite" in str(error)
    volumes[1].VisibilityOff()
    host.render(eyes, Oracle())
    assert host.last_timings["host_render_calls"] == 2 + int(EGL), "One visible volume must retain the fast path"
    volumes[1].VisibilityOn()
    unsupported.SetBlendModeToComposite()
    renderer.SetUseSSAO(True)
    try:
        host.render(eyes, Oracle())
        raise AssertionError("Unsupported multi-volume pipeline was accepted")
    except ValueError as error:
        assert "SSAO" in str(error)
    finally:
        renderer.SetUseSSAO(False)
    host.render(eyes, Oracle())
    print("PASS: multi-volume failure cleanup, hidden unsupported data, live visibility and conservative validation", flush=True)

    # Independent workspaces use the SAME actors but must never capture another
    # dataset, opaque/translucent surfaces or decorations from the source view.
    renderer.AddActor(surface)
    surface.GetProperty().SetOpacity(.4)
    independent = create_host_views(view,
        names=lambda v: "red data" if v == volumes[0] else "blue data")
    assert len(independent) == 2
    assert independent[0].desktop is independent[1].desktop
    centre, size = workspace_envelope(independent)
    assert abs(centre[0]) < 1e-10
    assert np.isclose(size[0], sum(h.workspace_size[0] for h in independent) + .12)
    for h in independent:
        selected = h.selected_volume
        assert h.scene_bounds == selected.GetBounds()
        bounds = selected.GetBounds()
        # Default orientation maps RAS XYZ to workspace XZY. No per-volume fit.
        assert h.app_scale == .001
        assert np.allclose(h.workspace_size, np.array([
            bounds[1]-bounds[0], bounds[5]-bounds[4], bounds[3]-bounds[2]]) * .001)
        assert h.diagnostic_state()["volume_count"] == 1
        requests = [sdk_eye(h, (x, 0., .5)) for x in (-.032, .032)]
        # Independent native color and VTK RenderToImage depth with explicit
        # manual visibility, not the isolation context under test.
        saved = [(p, p.GetVisibility()) for p in (surface, *volumes)]
        native = []
        old_keys = selected.GetPropertyKeys()
        try:
            for p, visible in saved:
                p.SetVisibility(visible if p == selected else False)
            for eye in requests:
                with host._frame_state():
                    host._camera_for_eye(eye, *h.clip)
                    window.SetSize(*eye["size"])
                    window.Render()
                    native.append(pixels(window))
            keys = vtk.vtkInformation()
            if old_keys:
                keys.Copy(old_keys)
            keys.Set(vtk.vtkOpenGLActor.GLDepthMaskOverride(), 0)
            selected.SetPropertyKeys(keys)
            reference = Oracle(selected.GetMapper())
            host.render(requests, reference, near=h.clip[0], far=h.clip[1])
        finally:
            selected.SetPropertyKeys(old_keys)
            for p, visible in saved:
                p.SetVisibility(visible)
        window.Render()
        desktop = pixels(window), pixels(window, True)
        state_before = [state(v) for v in volumes]
        actual = Oracle()
        h.render(requests, actual)
        assert h.last_timings["host_render_calls"] == 2 + int(EGL)
        assert h.last_timings["volume_depth_passes"] == 0
        assert [state(v) for v in volumes] == state_before
        assert all(p.GetVisibility() == visible for p, visible in saved)
        assert all(np.array_equal(a, pixels(window, depth)) for a, depth in zip(desktop, (False, True)))
        for a, b, d, expected in zip(actual.colors, native, actual.depths, reference.depths):
            assert np.max(np.abs(a.astype(int)-b.astype(int))) <= 2, (h.name, "foreign content in workspace")
            assert np.max(np.abs(d-expected)) < 2e-5, (h.name, "wrong depth")
            assert np.count_nonzero(d < 1) > 30
        try:
            h.render(requests, Broken())
            raise AssertionError("Isolated consumer failure swallowed")
        except RuntimeError as error:
            assert str(error) == "injected multi-volume consumer failure"
        assert all(p.GetVisibility() == visible for p, visible in saved)
        assert all(np.array_equal(a, pixels(window, depth)) for a, depth in zip(desktop, (False, True)))
        selected.VisibilityOff()
        empty = Oracle()
        h.render(requests, empty)
        assert all(np.all(d == 1.) for d in empty.depths), "Hidden workspace captured another volume"
        selected.VisibilityOn()
        renderer.RemoveVolume(selected)
        try:
            h.render(requests, Oracle())
            raise AssertionError("Removed source silently switched to another volume")
        except RuntimeError as error:
            assert "removed" in str(error)
        finally:
            renderer.AddVolume(selected)
        print("PASS: independent workspace", h.name, "isolated color/depth, own bounds, hidden/removed source, failure cleanup", flush=True)
    for h in independent:
        h.close()
    host.close()


try:
    run()
except Exception:
    if EGL:
        raise
    traceback.print_exc()
    slicer.app.exit(1)
else:
    if not EGL:
        slicer.app.exit(0)
