"""Stereo frames from Slicer's existing view. No scene export or pixel readback.

The consumer must copy the supplied GPU textures before returning. Texture names
are borrowed from the host context, not handles that can be sent to a process.
"""
import math
from itertools import product
from time import perf_counter, thread_time
from contextlib import ExitStack, contextmanager, nullcontext

import vtk

from .qarvtk_host_gpu import DesktopTextures, single_pass_supported, volume_depth


def _items(collection):
    return [collection.GetItemAsObject(i) for i in range(collection.GetNumberOfItems())]


def _rotate(q, v):
    norm = math.sqrt(sum(x * x for x in q))
    if not math.isfinite(norm) or norm < 1e-12:
        raise ValueError("Invalid eye quaternion")
    x, y, z, w = (a / norm for a in q)
    a, b, c = v
    tx, ty, tz = 2 * (y * c - z * b), 2 * (z * a - x * c), 2 * (x * b - y * a)
    return (a + w * tx + y * tz - z * ty,
            b + w * ty + z * tx - x * tz,
            c + w * tz + x * ty - y * tx)


def projection(fov, near, far):
    if not (0 < near < far) or not all(math.isfinite(x) for x in (*fov, near, far)):
        raise ValueError("Invalid clipping or eye FOV")
    if any(abs(x) >= math.pi / 2 for x in fov):
        raise ValueError("Eye FOV outside perspective range")
    left, right, up, down = (math.tan(x) for x in fov)
    if right - left < 1e-8 or up - down < 1e-8:
        raise ValueError("Inverted eye FOV")
    result = vtk.vtkMatrix4x4()
    result.Zero()
    for r, c, value in ((0, 0, 2 / (right - left)), (0, 2, (right + left) / (right - left)),
                        (1, 1, 2 / (up - down)), (1, 2, (up + down) / (up - down)),
                        (2, 2, -(far + near) / (far - near)),
                        (2, 3, -2 * far * near / (far - near)), (3, 2, -1)):
        result.SetElement(r, c, value)
    return result


class HostView:
    """One live Slicer 3D view; all methods run on its GUI/render thread."""

    def __init__(self, view, fit=True, render_scale=1., volume=None, name="Slicer live renderer", size_multiplier=1.):
        self.view = view
        self.window = view.renderWindow()
        self.context_creation_time = self.window.GetContextCreationTime()
        self.renderer = view.renderWindow().GetRenderers().GetFirstRenderer()
        self.camera = self.renderer.GetActiveCamera()
        self.selected_volume = volume
        self.name = str(name)
        if volume is not None and volume not in _items(self.renderer.GetVolumes()):
            raise ValueError("The selected volume is not in the source Slicer view")
        if not math.isfinite(render_scale) or not .25 <= render_scale <= 1.:
            raise ValueError("Render scale must be between 25% and 100%")
        if not math.isfinite(size_multiplier) or not .125 <= size_multiplier <= 8.:
            raise ValueError("Model size must be between 1/8x and 8x")
        self.size_multiplier = float(size_multiplier)
        self.render_scale = render_scale
        self.last_render_sizes = []
        self.last_desktop_size = self.window.GetSize()
        self._busy = False
        self.last_timings = {}
        self.desktop = DesktopTextures(self.window)
        # One app coordinate unit is one native Slicer RAS millimetre. The SDK
        # alone maps these units into the room, including the mm -> m scale.
        self.fit = bool(fit)
        # Use the same visible-prop bounds as Slicer's view framing. The SDK
        # workspace, placement and scale are fixed together at Start.
        self.scene_bounds = tuple(volume.GetBounds() if volume is not None else self.renderer.ComputeVisiblePropBounds())
        bounds = self.scene_bounds
        if not all(math.isfinite(x) for x in bounds) or any(bounds[2*i] > bounds[2*i+1] for i in range(3)):
            raise ValueError("The selected Slicer 3D view has no visible content")
        self.center = tuple((bounds[2*i] + bounds[2*i+1]) * .5 for i in range(3))
        # App content uses unmodified world RAS coordinates. Axis conversion,
        # placement and scale belong to the SDK, never the per-eye camera.
        # Fixed RAS -> QAROS axes: right -> +X, anterior -> -Z, superior -> +Y.
        # Orbiting the desktop camera must never rotate data inside a workspace,
        # including at Start. Actual live actor/MRML transforms remain intact.
        self.app_orientation = (-math.sqrt(.5), 0., 0., math.sqrt(.5))
        # Bounds already include live data transforms, independent of the camera.
        corners = [_rotate(self.app_orientation, p)
                   for p in product(bounds[0:2], bounds[2:4], bounds[4:6])]
        extents = tuple(max(p[i] for p in corners) - min(p[i] for p in corners) for i in range(3))
        extent = max(extents)
        if not math.isfinite(extent) or extent <= 0:
            raise ValueError("The visible content has empty bounds")
        # Centering never changes physical scale. All workspaces use the same
        # explicit multiplier, preserving size relationships between datasets.
        self.app_scale = .001 * self.size_multiplier
        scaled_center = tuple(self.app_scale * x for x in _rotate(self.app_orientation, self.center))
        self.app_position = tuple(-x for x in scaled_center)
        # Without centering, keep the previous physical scene position:
        # shift the workspace centre and compensate with app_pose, not actors.
        self.workspace_position = tuple(base + (0. if fit else offset)
                                        for base, offset in zip((0., 0., -1.5), scaled_center))
        # Flat surface scenes still need a positive thickness. Other axes match
        # the transformed scene bounds exactly; dimensions are XYZ in metres.
        self.workspace_size = tuple(max(.001, x * self.app_scale) for x in extents)
        self.clip = (.05 / self.app_scale, 20. / self.app_scale)
        for method in ("GetDisplayFramebuffer", "GetFrameBlitMode", "SetUseOffScreenBuffers"):
            if not hasattr(self.window, method):
                raise RuntimeError("This Slicer render window lacks " + method)
        self.volumes()  # Fail before connecting if the depth path cannot handle this view.

    def close(self):
        self.desktop.close()

    def validate_source(self):
        if self.view.renderWindow() != self.window:
            raise RuntimeError("The Slicer render window changed; restart live streaming")
        if self.window.GetContextCreationTime() != self.context_creation_time:
            raise RuntimeError("The Slicer OpenGL context changed; restart live streaming")
        if self.window.GetRenderers().GetFirstRenderer() != self.renderer:
            raise RuntimeError("The Slicer renderer changed; restart live streaming")
        if self.renderer.GetActiveCamera() != self.camera:
            raise RuntimeError("The Slicer view camera changed; restart live streaming")
        if self.selected_volume is not None and self.selected_volume not in _items(self.renderer.GetVolumes()):
            raise RuntimeError("Source volume '" + self.name + "' was removed or its rendering method changed; restart live streaming")

    def volumes(self):
        """Validate the visible volume props, including newly shown live data."""
        source = _items(self.renderer.GetVolumes()) if self.selected_volume is None else [self.selected_volume]
        volumes = [v for v in source if v.GetVisibility()]
        for volume in volumes:
            mapper = volume.GetMapper()
            if volume.IsA("vtkMultiVolume"):
                raise ValueError("VTK Multi-Volume fusion is not supported by live depth; use separate VTK GPU Ray Casting volumes")
            if not mapper or not mapper.IsA("vtkOpenGLGPUVolumeRayCastMapper"):
                raise ValueError("Live volume depth requires Slicer's VTK GPU volume rendering method")
            if mapper.GetBlendMode() != vtk.vtkVolumeMapper.COMPOSITE_BLEND:
                raise ValueError("Live volume depth requires Composite rendering for every visible volume")
            if mapper.GetRenderToImage():
                raise ValueError("A host volume mapper is already in RenderToImage mode")
        if self.selected_volume is None and volumes and any(a.GetVisibility() and a.GetProperty().GetOpacity() < 1 for a in _items(self.renderer.GetActors())):
            raise ValueError("Live depth for a volume combined with translucent surfaces is not validated")
        if len(volumes) > 1 and not all(single_pass_supported(self.renderer, v) for v in volumes):
            raise ValueError("Multiple-volume live depth requires Slicer 5.12 / VTK 9.6 with standard GPU Composite rendering, "
                             "opaque surfaces, no SSAO/shadows/custom render pass, and no conflicting depth shader")
        return volumes

    @contextmanager
    def _isolated_props(self):
        # Keep the original live actor, mapper, transfer functions and ROI.
        # Hide other content only inside the protected eye-render scope. Restore
        # visibility BEFORE _frame_state restores/renders the desktop, including
        # when copying a frame fails. Hidden sources yield an empty workspace.
        saved = []
        try:
            if self.selected_volume is not None:
                for renderer in _items(self.window.GetRenderers()):
                    for prop in _items(renderer.GetViewProps()):
                        if prop != self.selected_volume and prop.GetVisibility():
                            saved.append((prop, prop.GetVisibility()))
                            prop.SetVisibility(False)
            yield
        finally:
            for prop, visible in saved:
                prop.SetVisibility(visible)

    def _camera_for_eye(self, eye, near, far):
        position, orientation, fov = eye["position"], eye["orientation"], eye["fov"]
        if not all(math.isfinite(x) for x in (*position, *orientation)):
            raise ValueError("Non-finite eye pose")
        p = position
        forward = _rotate(orientation, (0., 0., -1.))
        up = _rotate(orientation, (0., 1., 0.))
        self.camera.SetPosition(p)
        self.camera.SetFocalPoint(tuple(a + b for a, b in zip(p, forward)))
        self.camera.SetViewUp(up)
        self.camera.SetParallelProjection(False)
        self.camera.SetClippingRange(near, far)
        self.camera.SetExplicitProjectionTransformMatrix(projection(fov, near, far))
        self.camera.SetUseExplicitProjectionTransformMatrix(True)

    @contextmanager
    def _timed(self, name):
        start = perf_counter()
        try:
            yield
        finally:
            self.last_timings[name] = self.last_timings.get(name, 0.) + (perf_counter() - start) * 1000

    @contextmanager
    def _frame_state(self):
        if self._busy:
            raise RuntimeError("Reentrant host rendering is not supported")
        self.validate_source()
        self._busy = True
        window = self.window
        saved_camera = vtk.vtkCamera()
        saved_camera.DeepCopy(self.camera)
        size = window.GetSize()
        blit, offscreen, swap, stereo = (window.GetFrameBlitMode(), window.GetUseOffScreenBuffers(),
                                         window.GetSwapBuffers(), window.GetStereoRender())
        flip_y = window.GetFramebufferFlipY()
        camera_node = self.view.cameraNode() if hasattr(self.view, "cameraNode") else None
        modifying = camera_node.StartModify() if camera_node else None
        render_enabled = self.view.renderEnabled if hasattr(self.view, "setRenderEnabled") else None
        preserved = False
        window.MakeCurrent()
        state = window.GetState()
        viewport, scissor = [0]*4, [0]*4
        state.vtkglGetIntegerv(0x0BA2, viewport)  # GL_VIEWPORT
        state.vtkglGetIntegerv(0x0C10, scissor)  # GL_SCISSOR_BOX
        try:
            # CTK can synchronously render when a temporary camera modification
            # requests an update. Only explicit eye renders may run in this
            # scope. Restore the flag without scheduling a redundant desktop
            # render; existing queued updates and later user edits still run.
            if render_enabled is not None:
                self.view.setRenderEnabled(False)
            with self._timed("desktop_save_ms"):
                preserved = self.desktop.begin()
            window.SetFrameBlitMode(vtk.vtkOpenGLRenderWindow.NoBlit)
            window.SetUseOffScreenBuffers(True)
            window.SetSwapBuffers(True)  # Resolve MSAA to the host display textures.
            window.SetStereoRender(False)  # Explicit QAROS eyes, never host anaglyph/interlace.
            window.SetFramebufferFlipY(False)  # Transport normalizes both planes to top-left.
            yield
        finally:
            restore_start = perf_counter()
            self.camera.DeepCopy(saved_camera)
            window.SetSize(size)
            window.SetFrameBlitMode(blit)
            window.SetUseOffScreenBuffers(offscreen)
            window.SetSwapBuffers(swap)
            window.SetStereoRender(stereo)
            window.SetFramebufferFlipY(flip_y)
            try:
                self.desktop.restore()
                state.vtkglViewport(*viewport)
                state.vtkglScissor(*scissor)
                if camera_node:
                    camera_node.EndModify(modifying)
                # Qt will paint the original attachments after we return. No
                # event processing is permitted while either XR eye is installed.
                if not preserved:
                    self._render_window()
                    self.last_timings["desktop_restore_render_calls"] = 1.
            finally:
                if render_enabled is not None:
                    self.view.setRenderEnabled(render_enabled)
                self.last_timings["desktop_restore_ms"] = (perf_counter() - restore_start) * 1000
                self._busy = False

    def _render_window(self):
        self.last_timings["host_render_calls"] = self.last_timings.get("host_render_calls", 0.) + 1
        cpu = thread_time()
        try:
            self.window.Render()
        finally:
            self.last_timings["host_render_thread_cpu_ms"] = (
                self.last_timings.get("host_render_thread_cpu_ms", 0.) + (thread_time() - cpu) * 1000)

    def diagnostic_state(self):
        """Read existing settings once per second; never update a VTK pipeline."""
        result = dict(desired_update_rate=self.window.GetDesiredUpdateRate(),
                      view_visible=bool(self.view.isVisible()))
        volumes = self.volumes()
        result["workspace_name"] = self.name
        result["isolated_volume"] = self.selected_volume is not None
        result["volume_count"] = len(volumes)
        result["volumes"] = []
        for volume in volumes:
            mapper = volume.GetMapper()
            image = mapper.GetInput()
            result["volumes"].append(dict(
                mapper=mapper.GetClassName(), sample_distance=mapper.GetSampleDistance(),
                image_sample_distance=mapper.GetImageSampleDistance(),
                auto_adjust=bool(mapper.GetAutoAdjustSampleDistances()),
                lock_sample_to_spacing=bool(mapper.GetLockSampleDistanceToInputSpacing()),
                cropping=bool(mapper.GetCropping()), crop_planes=mapper.GetCroppingRegionPlanes(),
                property_mtime=volume.GetProperty().GetMTime(),
                input_mtime=image.GetMTime() if image else None,
                spacing=image.GetSpacing() if image else None,
                dimensions=image.GetDimensions() if image else None,
                matrix=[volume.GetMatrix().GetElement(r, c) for r in range(4) for c in range(4)]))
        if volumes:
            result["volume"] = result["volumes"][0]  # Existing diagnostic readers.
        return result

    def _textures(self):
        framebuffer = self.window.GetDisplayFramebuffer()
        color = framebuffer.GetColorAttachmentAsTextureObject(0)
        depth = framebuffer.GetDepthAttachmentAsTextureObject()
        if not color or not depth or not color.GetHandle() or not depth.GetHandle():
            raise RuntimeError("The host did not expose resolved color/depth GPU textures")
        return color, depth

    def render(self, eyes, consumer, near=None, far=None):
        """consumer.color/depth copy borrowed textures synchronously on the GPU.

        color(index, color, depth, width, height) receives the unmodified host
        image. depth(index, color, depth, volume_color, volume_depth, width,
        height) receives a supplementary host-mapper depth pass only when the
        single-pass volume hook cannot safely be applied to this pipeline.
        All texture arguments are vtkTextureObject, except absent volume planes.
        """
        if len(eyes) != 2:
            raise ValueError("Exactly two eye requests are required")
        if self._busy:
            raise RuntimeError("Reentrant host rendering is not supported")
        near = self.clip[0] if near is None else near
        far = self.clip[1] if far is None else far
        self.last_desktop_size = self.window.GetSize()
        self.last_render_sizes = []
        for eye in eyes:
            width, height = eye["size"]
            if not (1 <= width <= 8192 and 1 <= height <= 8192):
                raise ValueError("Invalid host render size")
            self.last_render_sizes.append(tuple(max(1, int(x * self.render_scale + .5)) for x in (width, height)))
        self.last_timings = dict.fromkeys(("color_render_ms", "depth_render_ms",
                                          "color_copy_call_ms", "depth_copy_call_ms",
                                          "host_render_calls", "desktop_restore_render_calls",
                                          "volume_depth_passes"), 0.)
        self.volumes()  # Reject unsupported pipelines before touching the desktop.
        with self._timed("frame_wall_ms"), self._frame_state(), self._isolated_props(), ExitStack() as scope:
            volumes = self.volumes()
            volume = volumes[0] if len(volumes) == 1 else None
            mapper = volume.GetMapper() if volume else None
            fast_depth = volume is not None and single_pass_supported(self.renderer, volume)
            scope.enter_context(volume_depth(volume) if fast_depth else nullcontext())
            for index, eye in enumerate(eyes):
                width, height = self.last_render_sizes[index]
                self._camera_for_eye(eye, near, far)
                self.window.SetSize(width, height)
                with self._timed("color_render_ms"):
                    self._render_window()
                color, depth = self._textures()
                if any((t.GetWidth(), t.GetHeight()) != (width, height) for t in (color, depth)):
                    raise RuntimeError("Host GPU texture size differs from the selected render size")
                with self._timed("color_copy_call_ms"):
                    consumer.color(index, color, depth, width, height)
                if len(volumes) > 1 or (mapper and not fast_depth):
                    dw, dh = eye.get("depth_size", (width, height))
                    dw, dh = min(dw, width), min(dh, height)
                    if dw < 1 or dh < 1:
                        raise ValueError("Invalid depth render size")
                    self.window.SetSize(dw, dh)
                    if len(volumes) > 1:
                        self._multiple_volume_depth(index, volumes, consumer, dw, dh)
                    else:
                        self._volume_depth(index, mapper, consumer, dw, dh)
                else:
                    with self._timed("depth_copy_call_ms"):
                        consumer.depth(index, color, depth, None, None, width, height)

    def _multiple_volume_depth(self, index, volumes, consumer, width, height):
        # Keep the first (color) render completely native. Enabling depth writes
        # while capturing several translucent volumes would change their color
        # compositing. Only this supplementary depth render installs the hooks.
        # Normal depth testing retains the nearest contributing sample across
        # all volumes and opaque surfaces, independent of volume draw order.
        with ExitStack() as scope:
            for volume in volumes:
                scope.enter_context(volume_depth(volume))
            with self._timed("depth_render_ms"):
                self.last_timings["volume_depth_passes"] += 1
                self._render_window()
            color, depth = self._textures()
            if any((t.GetWidth(), t.GetHeight()) != (width, height) for t in (color, depth)):
                raise RuntimeError("Multi-volume depth texture size differs from the requested render size")
            with self._timed("depth_copy_call_ms"):
                consumer.depth(index, color, depth, None, None, width, height)

    def _volume_depth(self, index, mapper, consumer, width, height):
        # The first render is the host's complete color output, including custom
        # shaders and passes. A second render asks the SAME live mapper for its
        # first contributing sample depth; it never replaces the color image.
        old = (mapper.GetRenderToImage(), mapper.GetClampDepthToBackface(),
               mapper.GetAutoAdjustSampleDistances(), mapper.GetImageSampleDistance(),
               mapper.GetDepthImageScalarType())
        cullers = _items(self.renderer.GetCullers())
        try:
            mapper.SetRenderToImage(True)
            mapper.SetClampDepthToBackface(False)
            mapper.SetAutoAdjustSampleDistances(False)
            mapper.SetImageSampleDistance(1.)
            mapper.SetDepthImageScalarType(vtk.VTK_FLOAT)
            # Even an offscreen volume must execute its clear; otherwise its
            # texture may contain the preceding eye's data.
            self.renderer.GetCullers().RemoveAllItems()
            with self._timed("depth_render_ms"):
                self.last_timings["volume_depth_passes"] += 1
                self._render_window()
            color, depth = self._textures()
            vc, vd = mapper.GetColorTexture(), mapper.GetDepthTexture()
            if not vc or not vd or not vc.GetHandle() or not vd.GetHandle():
                raise RuntimeError("The live host volume mapper did not expose depth")
            with self._timed("depth_copy_call_ms"):
                consumer.depth(index, color, depth, vc, vd, width, height)
        finally:
            mapper.SetRenderToImage(old[0])
            mapper.SetClampDepthToBackface(old[1])
            mapper.SetAutoAdjustSampleDistances(old[2])
            mapper.SetImageSampleDistance(old[3])
            mapper.SetDepthImageScalarType(old[4])
            self.renderer.GetCullers().RemoveAllItems()
            for culler in cullers:
                self.renderer.GetCullers().AddItem(culler)


def create_host_views(view, separate=True, names=None, **options):
    """Freeze workspace membership at Start; all data/shaders remain live."""
    renderer = view.renderWindow().GetRenderers().GetFirstRenderer()
    visible = [v for v in _items(renderer.GetVolumes()) if v.GetVisibility()]
    if not separate or not visible:
        return [HostView(view, **options)]
    hosts = []
    try:
        for i, volume in enumerate(visible):
            name = names(volume) if names else None
            hosts.append(HostView(view, volume=volume, name=name or "Slicer volume " + str(i+1), **options))
            # Captures are serialized on the GUI thread. Reuse spare attachments
            # instead of allocating another full eye framebuffer for every cube.
            hosts[-1].desktop = hosts[0].desktop
        # Place independent boxes side by side, centred as a group. Physical
        # size mode keeps millimetres; only the initial room positions change.
        gap = .12
        cursor = -(sum(h.workspace_size[0] for h in hosts) + gap * (len(hosts)-1)) / 2
        for host in hosts:
            host.workspace_position = (cursor + host.workspace_size[0]/2, 0., -1.5)
            cursor += host.workspace_size[0] + gap
        return hosts
    except Exception:
        for host in hosts:
            host.close()
        raise


def workspace_envelope(hosts):
    """Bounds of the initial workspace group, for the shared slice panel row."""
    low = [min(h.workspace_position[i] - h.workspace_size[i]/2 for h in hosts) for i in range(3)]
    high = [max(h.workspace_position[i] + h.workspace_size[i]/2 for h in hosts) for i in range(3)]
    return tuple((a+b)/2 for a, b in zip(low, high)), tuple(b-a for a, b in zip(low, high))

