"""Run inside real Slicer. CPU readback is used only as a test oracle."""
from pathlib import Path
import sys
import traceback
import numpy as np
import vtk
import slicer
from vtk.util.numpy_support import vtk_to_numpy

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "package" / "QAROS-VTK-plugins" / "slicer"))
from QarosVTKLib.qarvtk_host_view import HostView
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_host_view_math import run as check_placement, sdk_eye


def pixels(window):
    w, h = window.GetSize()
    data = vtk.vtkUnsignedCharArray()
    window.GetRGBACharPixelData(0, 0, w-1, h-1, 1, data)
    return vtk_to_numpy(data).copy().reshape(h, w, 4)


def camera_state(camera):
    return (camera.GetPosition(), camera.GetFocalPoint(), camera.GetViewUp(),
            camera.GetClippingRange(), camera.GetParallelProjection(),
            camera.GetUseExplicitProjectionTransformMatrix())


def depths(window):
    w, h = window.GetSize()
    data = vtk.vtkFloatArray()
    window.GetZbufferData(0, 0, w-1, h-1, data)
    return vtk_to_numpy(data).copy().reshape(h, w)


def run():
    check_placement()
    assert hasattr(slicer.modules, "qaroslive"), "Packaged live module was not discovered"
    assert not hasattr(slicer.modules, "qarosvtk"), "Retired export module was discovered"
    live_widget = slicer.modules.qaroslive.widgetRepresentation().self()
    assert live_widget.startButton.enabled and not live_widget.stopButton.enabled
    slicer.app.layoutManager().setLayout(slicer.vtkMRMLLayoutNode.SlicerLayoutOneUp3DView)
    view = slicer.app.layoutManager().threeDWidget(0).threeDView()
    window = view.renderWindow()
    renderer = window.GetRenderers().GetFirstRenderer()
    view.mrmlViewNode().SetBoxVisible(False)
    view.mrmlViewNode().SetAxisLabelsVisible(False)
    renderer.SetBackground(0., 0., 0.)
    renderer.GradientBackgroundOff()

    ct = slicer.mrmlScene.AddNewNodeByClass("vtkMRMLScalarVolumeNode", "Live CT test")
    z, y, x = np.indices((32, 32, 32))
    data = np.where((x-15.5)**2+(y-15.5)**2+(z-15.5)**2 < 12**2, 900, 0).astype(np.int16)
    slicer.util.updateVolumeFromArray(ct, data)
    ct.SetSpacing(3., 3., 3.)
    ct.SetOrigin(100., 200., 300.)
    ct.CreateDefaultDisplayNodes()
    vr = slicer.modules.volumerendering.logic().CreateDefaultVolumeRenderingNodes(ct)
    vr.SetVisibility(True)
    prop = vr.GetVolumePropertyNode().GetVolumeProperty()
    prop.GetRGBTransferFunction().RemoveAllPoints()
    prop.GetRGBTransferFunction().AddRGBPoint(0, 0., 0., 0.)
    prop.GetRGBTransferFunction().AddRGBPoint(900, 1., .2, .1)
    prop.GetScalarOpacity().RemoveAllPoints()
    prop.GetScalarOpacity().AddPoint(0, 0.)
    prop.GetScalarOpacity().AddPoint(899, 0.)
    prop.GetScalarOpacity().AddPoint(900, .6)
    prop.ShadeOff()
    slicer.app.processEvents()
    view.resetFocalPoint()
    window.Render()
    host = HostView(view)
    # Real Slicer volume matrix carries its 3 mm spacing; centering and workspace
    # must use world bounds, not voxel counts or raw mapper input dimensions.
    assert np.allclose(host.workspace_size, [.093, .093, .093]), host.workspace_size
    physical = HostView(view, fit=False)
    assert np.allclose(physical.workspace_size, [.093, .093, .093]), physical.workspace_size
    physical.close()
    print("PASS: real Slicer CT stays 1:1 with and without centering", flush=True)
    print("HOST: VTK", vtk.vtkVersion.GetVTKVersion(), "window", window.GetClassName(),
          "mapper", host.volumes()[0].GetMapper().GetClassName(), "app_scale", host.app_scale, flush=True)
    original_camera = camera_state(host.camera)
    original_size = window.GetSize()
    original_blit = window.GetFrameBlitMode()
    original_cullers = tuple(host.renderer.GetCullers())
    mapper = host.volumes()[0].GetMapper()
    volume = next(v for v in renderer.GetVolumes() if v.GetVisibility())
    original_shader, original_keys = volume.GetShaderProperty(), volume.GetPropertyKeys()
    old_mapper = (mapper.GetRenderToImage(), mapper.GetImageSampleDistance(),
                  mapper.GetAutoAdjustSampleDistances(), mapper.GetDepthImageScalarType())
    eyes = [sdk_eye(host, (x, 0., 1.2)) for x in (-.032, .032)]

    class Oracle:
        def __init__(self):
            self.colors, self.depths = [], []
        def color(self, index, color, depth, width, height):
            assert color.GetWidth() == width and color.GetHeight() == height
            self.colors.append(pixels(window))
        def depth(self, index, color, depth, vc, vd, width, height):
            if vc is None:
                values = depths(window)
            else:
                image = vtk.vtkImageData()
                mapper.GetDepthImage(image)
                values = vtk_to_numpy(image.GetPointData().GetScalars()).copy().reshape(height, width)
            assert np.isfinite(values).all() and values.min() >= 0 and values.max() <= 1
            assert np.count_nonzero(values < .99999) > 100, "No tissue depth"
            assert values[0, 0] == 1, "Empty rays must have far depth"
            self.depths.append(values)

    oracle = Oracle()
    desktop_color, desktop_depth = pixels(window), depths(window)
    desktop_textures = (window.GetDisplayFramebuffer().GetColorAttachmentAsTextureObject(0),
                        window.GetRenderFramebuffer().GetDepthAttachmentAsTextureObject())
    render_events = []
    render_observer = window.AddObserver(vtk.vtkCommand.StartEvent, lambda *args: render_events.append(1))
    host.render(eyes, oracle)
    diagnostic_state = host.diagnostic_state()
    assert diagnostic_state["volume"]["dimensions"] == (32, 32, 32)
    # Slicer can put voxel spacing in the volume matrix and keep mapper input
    # spacing at one. Verify the physical spacing using both recorded values.
    diagnostic_matrix = np.asarray(diagnostic_state["volume"]["matrix"]).reshape(4, 4)
    world_spacing = np.linalg.norm(diagnostic_matrix[:3, :3], axis=0) * diagnostic_state["volume"]["spacing"]
    assert np.allclose(world_spacing, ct.GetSpacing()), diagnostic_state
    assert diagnostic_state["volume"]["image_sample_distance"] == old_mapper[1]
    assert host.last_timings["host_render_thread_cpu_ms"] >= 0
    print("PASS: read-only live diagnostic state and render thread CPU time", flush=True)
    print("HOST initial timing", host.last_timings, "flags",
          renderer.GetUseDepthPeeling(), renderer.GetUseDepthPeelingForVolumes(), renderer.GetUseSSAO(),
          mapper.GetUseDepthPass(), "keys", original_keys, "renders", len(render_events), flush=True)
    assert len(render_events) == 2, f"Expected two renders, got {len(render_events)}"
    assert np.array_equal(pixels(window), desktop_color), "Desktop color was not preserved"
    assert np.array_equal(depths(window), desktop_depth), "Desktop depth was not preserved"
    assert window.GetDisplayFramebuffer().GetColorAttachmentAsTextureObject(0) == desktop_textures[0]
    assert window.GetRenderFramebuffer().GetDepthAttachmentAsTextureObject() == desktop_textures[1]
    assert volume.GetShaderProperty() == original_shader and volume.GetPropertyKeys() == original_keys
    timings = host.last_timings
    parts = ("color_render_ms", "color_copy_call_ms", "depth_copy_call_ms",
             "desktop_save_ms", "desktop_restore_ms")
    assert all(timings[name] > 0 for name in parts)
    assert timings["depth_render_ms"] == 0 and timings["volume_depth_passes"] == 0
    assert timings["host_render_calls"] == 2 and timings["desktop_restore_render_calls"] == 0
    assert sum(timings[name] for name in parts) <= timings["frame_wall_ms"]
    assert len(oracle.colors) == 2 and len(oracle.depths) == 2
    assert np.count_nonzero(oracle.colors[0][:, :, 0] > 100) > 100
    assert not np.array_equal(oracle.colors[0], oracle.colors[1]), "Eyes must use distinct poses"
    assert camera_state(host.camera) == original_camera
    assert window.GetSize() == original_size and window.GetFrameBlitMode() == original_blit
    assert tuple(host.renderer.GetCullers()) == original_cullers
    assert (mapper.GetRenderToImage(), mapper.GetImageSampleDistance(),
            mapper.GetAutoAdjustSampleDistances(), mapper.GetDepthImageScalarType()) == old_mapper

    # Independently compare single-pass tissue depth with VTK's existing
    # RenderToImage depth. An existing depth override intentionally selects the
    # compatibility path; it must remain intact after capture.
    keys = vtk.vtkInformation()
    if original_keys:
        keys.Copy(original_keys)
    keys.Set(vtk.vtkOpenGLActor.GLDepthMaskOverride(), 0)
    volume.SetPropertyKeys(keys)
    reference = Oracle()
    host.render(eyes, reference)
    assert host.last_timings["host_render_calls"] == 4
    assert host.last_timings["volume_depth_passes"] == 2
    assert volume.GetPropertyKeys() == keys
    volume.SetPropertyKeys(original_keys)
    for actual, expected_depth in zip(oracle.depths, reference.depths):
        assert np.max(np.abs(actual - expected_depth)) < 2e-5, "Single-pass tissue depth differs from VTK"
    for actual, expected_color in zip(oracle.colors, reference.colors):
        assert np.max(np.abs(actual.astype(int) - expected_color.astype(int))) <= 2

    # Resolution control changes the real host textures. Compare it to an
    # independently requested smaller eye, including the tissue depth.
    host.render_scale = .5
    reduced = Oracle()
    host.render(eyes, reduced)
    assert host.last_render_sizes == [(96, 80), (96, 80)]
    host.render_scale = 1.
    small = [dict(eye, size=(96, 80)) for eye in eyes]
    explicit_small = Oracle()
    host.render(small, explicit_small)
    for a, b in zip(reduced.colors, explicit_small.colors):
        assert np.array_equal(a, b), "Render resolution did not select the requested host pixels"
    for a, b in zip(reduced.depths, explicit_small.depths):
        assert np.max(np.abs(a-b)) < 2e-5
    # An incompatible volume shader can still render its supplementary depth
    # at half width/height while its full-resolution host color stays intact.
    volume.SetPropertyKeys(keys)
    half_depth = Oracle()
    host.render([dict(eye, depth_size=(96, 80)) for eye in eyes], half_depth)
    volume.SetPropertyKeys(original_keys)
    assert half_depth.colors[0].shape[:2] == (160, 192)
    assert half_depth.depths[0].shape == (80, 96)
    assert np.max(np.abs(half_depth.depths[0] - explicit_small.depths[0])) < 2e-5
    assert window.GetSize() == original_size

    # A live native actor that has no MRML/export representation must appear.
    sphere = vtk.vtkSphereSource()
    sphere.SetCenter(170., 190., 346.5)
    sphere.SetRadius(10.)
    surface_mapper = vtk.vtkPolyDataMapper()
    surface_mapper.SetInputConnection(sphere.GetOutputPort())
    actor = vtk.vtkActor()
    actor.SetMapper(surface_mapper)
    actor.GetProperty().SetColor(1., 0., 0.)
    actor.GetProperty().LightingOff()
    actor.GetShaderProperty().AddFragmentShaderReplacement("//VTK::Light::Impl", True,
        "//VTK::Light::Impl\n  gl_FragData[0] = vec4(0.0, 1.0, 0.0, 1.0);", False)
    renderer.AddActor(actor)
    changed = Oracle()
    host.render(eyes, changed)
    assert np.count_nonzero(changed.colors[0][:, :, 1] > 150) > 30, "Host-only actor absent"
    assert not np.array_equal(changed.colors[0], oracle.colors[0])
    with host._frame_state():
        host._camera_for_eye(eyes[0], *host.clip)
        window.SetSize(*eyes[0]["size"])
        window.Render()
        surface_color, surface_depth = pixels(window), depths(window)
    green = (surface_color[:, :, 1] > 150) & (surface_color[:, :, 0] < 20)
    assert green.sum() > 30
    assert np.max(np.abs(changed.depths[0][green] - surface_depth[green])) < 2e-5
    renderer.RemoveActor(actor)

    # Color is exactly the normal host pipeline for the same eye, before the
    # supplementary depth pass. No new actor/mapper/transfer function is built.
    with host._frame_state():
        host._camera_for_eye(eyes[0], *host.clip)
        window.SetSize(*eyes[0]["size"])
        window.Render()
        expected = pixels(window)
    again = Oracle()
    host.render(eyes, again)
    assert np.max(np.abs(again.colors[0].astype(int)-expected.astype(int))) <= 2

    # Preserve a real custom VOLUME shader as well as surface shaders. Only
    # add our depth output; its normal color expression must remain unchanged.
    tag = "//VTK::Base::Exit"
    original_shader.AddFragmentShaderReplacement(tag, True,
        tag + "\ng_fragColor.rgb *= vec3(0.5, 1.0, 1.0);", False)
    try:
        with host._frame_state():
            host._camera_for_eye(eyes[0], *host.clip)
            window.SetSize(*eyes[0]["size"])
            window.Render()
            custom_expected = pixels(window)
        custom = Oracle()
        host.render(eyes, custom)
        assert host.last_timings["host_render_calls"] == 2
        assert np.max(np.abs(custom.colors[0].astype(int)-custom_expected.astype(int))) <= 2
        assert not np.array_equal(custom.colors[0], again.colors[0])
        assert volume.GetShaderProperty() == original_shader
    finally:
        original_shader.ClearFragmentShaderReplacement(tag, True)

    # Existing plugins may already use the depth hook. Keep their shader and
    # select the supplementary depth path instead of replacing their code.
    tag = "//VTK::RenderToImage::Dec"
    original_shader.AddFragmentShaderReplacement(tag, True, tag + "\n// existing plugin hook", False)
    try:
        host.render(eyes, Oracle())
        assert host.last_timings["volume_depth_passes"] == 2
        assert volume.GetShaderProperty() == original_shader
    finally:
        original_shader.ClearFragmentShaderReplacement(tag, True)

    # Host ROI cropping, explicitly unsupported by the snapshot exporter.
    roi = slicer.modules.volumerendering.logic().CreateROINode(vr)
    slicer.modules.volumerendering.logic().FitROIToVolume(vr)
    roi.GetDisplayNode().SetVisibility(False)
    roi.SetSize(35., 96., 96.)
    vr.SetCroppingEnabled(True)
    slicer.app.processEvents()
    cropped = Oracle()
    host.render(eyes, cropped)
    assert np.count_nonzero(cropped.colors[0][:, :, 0] > 100) < np.count_nonzero(again.colors[0][:, :, 0] > 100)
    assert not np.array_equal(cropped.depths[0], again.depths[0])
    assert vr.GetCroppingEnabled(), "Host crop state was lost"

    class EmptyOracle(Oracle):
        def depth(self, index, color, depth, vc, vd, width, height):
            assert vc is None and vd is None
            values = depths(window)
            assert np.all(values == 1), "Offscreen volume retained an earlier eye's depth"
    away = [sdk_eye(host, (x, 0., 1.2), orientation=(0., 1., 0., 0.)) for x in (-.032, .032)]
    host.render(away, EmptyOracle())

    class Broken(Oracle):
        def depth(self, *args):
            raise RuntimeError("injected consumer failure")
    # ROI/MRML updates are allowed to replace property objects. Restore the
    # current host state at entry, not the fixture's pre-ROI objects.
    before_error_shader, before_error_keys = volume.GetShaderProperty(), volume.GetPropertyKeys()
    try:
        host.render(eyes, Broken())
        raise AssertionError("Consumer failure did not propagate")
    except RuntimeError as error:
        assert str(error) == "injected consumer failure"
    assert camera_state(host.camera) == original_camera
    assert window.GetSize() == original_size and window.GetFrameBlitMode() == original_blit
    assert tuple(host.renderer.GetCullers()) == original_cullers
    assert (mapper.GetRenderToImage(), mapper.GetImageSampleDistance(),
            mapper.GetAutoAdjustSampleDistances(), mapper.GetDepthImageScalarType()) == old_mapper
    assert volume.GetShaderProperty() == before_error_shader, "Error changed the host shader object"
    assert volume.GetPropertyKeys() == before_error_keys, "Error changed the host property keys"
    assert view.renderEnabled, "Error left Slicer rendering disabled"

    # Reuse the private attachments with different eye sizes and with MSAA.
    # Both desktop textures and their content must survive, including errors.
    original_samples = window.GetMultiSamples()
    for samples in (0, 4):
        window.SetMultiSamples(samples)
        window.Render()
        desktop_color, desktop_depth = pixels(window), depths(window)
        varied = [dict(eyes[0], size=(256, 128)), dict(eyes[1], size=(128, 192))]
        host.render(varied, Oracle())
        assert host.last_timings["host_render_calls"] == 2
        assert np.array_equal(pixels(window), desktop_color), f"Desktop color changed with MSAA {samples}"
        assert np.array_equal(depths(window), desktop_depth), f"Desktop depth changed with MSAA {samples}"
        try:
            host.render(varied, Broken())
        except RuntimeError as error:
            assert str(error) == "injected consumer failure"
        else:
            raise AssertionError("Consumer failure did not propagate")
        assert np.array_equal(pixels(window), desktop_color), "Error corrupted desktop color"
        assert np.array_equal(depths(window), desktop_depth), "Error corrupted desktop depth"
    window.SetMultiSamples(original_samples)
    window.Render()
    slicer.app.processEvents()
    render_events.clear()
    host.render(eyes, Oracle())
    synchronous_count = len(render_events)
    slicer.app.processEvents()
    slicer.app.processEvents()
    print("HOST render counts: synchronous", synchronous_count,
          "after queued Qt events", len(render_events), flush=True)
    window.RemoveObserver(render_observer)

    # Exercise the connected layout signal, not just its handler. The previous
    # plugin stopped a live stream on every signal, including same-view maximize.
    class LayoutTransport:
        running, stopping = True, False
        resolution_status = "layout test"
        def __init__(self):
            self.host = host
            self.events, self.reasons = [], []
            self.diagnostics = self
        def event(self, message):
            self.events.append(message)
        def request_stop(self, reason):
            self.reasons.append(reason)
            self.stopping = True
        def tick(self):
            pass
        def status(self):
            return "layout test"

    transport = LayoutTransport()
    live_widget.transport = transport
    lm = slicer.app.layoutManager()
    main_window = slicer.util.mainWindow()
    try:
        for label, change in (
                ("four-up", lambda: lm.setLayout(slicer.vtkMRMLLayoutNode.SlicerLayoutFourUpView)),
                ("maximize 3D", lambda: lm.addMaximizedViewNode(view.mrmlViewNode())),
                ("restore 3D", lm.removeAllMaximizedViewNodes),
                ("one-up 3D", lambda: lm.setLayout(slicer.vtkMRMLLayoutNode.SlicerLayoutOneUp3DView)),
                ("application fullscreen", main_window.showFullScreen),
                ("application normal", main_window.showNormal)):
            change()
            slicer.app.processEvents()
            window.Render()
            assert not transport.reasons, (label, transport.reasons)
            assert lm.threeDWidget(0).threeDView() == view
            host.validate_source()
            before_color, before_depth = pixels(window), depths(window)
            captured = Oracle()
            host.render(eyes, captured)
            assert host.last_timings["host_render_calls"] == 2, label
            assert np.array_equal(pixels(window), before_color), label
            assert np.array_equal(depths(window), before_depth), label
            # A layout can change MRML-managed props. Compare against the
            # current native pipeline, not a picture from the previous layout.
            for eye, actual in zip(eyes, captured.colors):
                with host._frame_state():
                    host._camera_for_eye(eye, *host.clip)
                    window.SetSize(*eye["size"])
                    window.Render()
                    expected_color = pixels(window)
                difference = np.max(np.abs(actual.astype(int)-expected_color.astype(int)))
                assert difference <= 2, (label, "native color", difference)
            saved_keys = volume.GetPropertyKeys()
            comparison_keys = vtk.vtkInformation()
            if saved_keys:
                comparison_keys.Copy(saved_keys)
            comparison_keys.Set(vtk.vtkOpenGLActor.GLDepthMaskOverride(), 0)
            volume.SetPropertyKeys(comparison_keys)
            try:
                reference = Oracle()
                host.render(eyes, reference)
                assert host.last_timings["volume_depth_passes"] == 2
            finally:
                volume.SetPropertyKeys(saved_keys)
            for a, b in zip(captured.depths, reference.depths):
                assert np.max(np.abs(a-b)) < 2e-5, (label, "VTK tissue depth")
        assert len(transport.events) >= 4, transport.events
        lm.setLayout(slicer.vtkMRMLLayoutNode.SlicerLayoutOneUpRedSliceView)
        slicer.app.processEvents()
        assert transport.stopping and len(transport.reasons) == 1
        assert "source 3D view" in transport.reasons[0], transport.reasons
        print("PASS: same-view layout/maximize/fullscreen preserves stereo and desktop; removed source stops explicitly", flush=True)
    finally:
        live_widget.transport = None
        lm.setLayout(slicer.vtkMRMLLayoutNode.SlicerLayoutOneUp3DView)
        slicer.app.processEvents()

    # A recreated context must be rejected before borrowing cached attachments.
    context_stamp = host.context_creation_time
    host.context_creation_time = -1
    try:
        host.render(eyes, Oracle())
        raise AssertionError("Changed context was accepted")
    except RuntimeError as error:
        assert "OpenGL context changed" in str(error), str(error)
    finally:
        host.context_creation_time = context_stamp
    host.close()
    print("PASS: two renders per stereo frame; single-pass CT depth matches VTK; exact desktop texture preservation; "
          "host-only shader/actor and occlusion; ROI; offscreen rays; MSAA/eye resize/error restoration", flush=True)
    live_widget.cleanup()


try:
    run()
except Exception:
    traceback.print_exc()
    slicer.app.exit(1)
else:
    slicer.app.exit(0)

