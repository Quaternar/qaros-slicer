"""Scoped changes to the existing host pipeline. No readback or scene copy."""
from contextlib import contextmanager

import vtk


class DesktopTextures:
    """Keep the desktop's attachments intact while the host renders XR eyes.

    The existing VTK framebuffers stay in place (including Qt's references to
    them). Only their attachments are exchanged for reusable eye textures.
    Restoring the original attachments needs neither a render nor a GPU copy.
    """
    def __init__(self, window):
        self.window = window
        self.buffers = {}
        self.saved = []

    def begin(self):
        window = self.window
        if not window.IsA("vtkGenericOpenGLRenderWindow") or window.GetStereoRender():
            return False
        window.MakeCurrent()
        state = window.GetState()
        state.PushFramebufferBindings()
        try:
            targets = ((window.GetRenderFramebuffer(), 1), (window.GetDisplayFramebuffer(), 2))
            # Validate everything before exchanging any attachment.
            originals = []
            for target, count in targets:
                colors = [target.GetColorAttachmentAsTextureObject(i) for i in range(count)]
                depth = target.GetDepthAttachmentAsTextureObject()
                if not target.GetFBOIndex() or not all(colors) or not depth:
                    return False
                size = [0, 0]
                target.GetLastSize(size)
                originals.append((target, colors, depth, tuple(size)))
            for index, (target, colors, depth, size) in enumerate(originals):
                spec = (len(colors), target.GetMultiSamples(),
                        window.GetRenderBufferTargetDepthSize(), window.GetStencilCapable())
                entry = self.buffers.get(index)
                if not entry or entry[0] != spec:
                    if entry:
                        entry[1].ReleaseGraphicsResources(window)
                    spare = vtk.vtkOpenGLFramebufferObject()
                    spare.SetContext(window)
                    if not spare.PopulateFramebuffer(*size, True, spec[0], vtk.VTK_UNSIGNED_CHAR,
                                                     True, spec[2], spec[1], bool(spec[3])):
                        raise RuntimeError("Cannot allocate host eye framebuffer attachments")
                    self.buffers[index] = (spec, spare)
                spare = self.buffers[index][1]
                # Record before the first mutation, so partial setup is reversible.
                self.saved.append((target, colors, depth, size))
                for i in range(len(colors)):
                    target.AddColorAttachment(i, spare.GetColorAttachmentAsTextureObject(i))
                target.AddDepthAttachment(spare.GetDepthAttachmentAsTextureObject())
            return True
        finally:
            state.PopFramebufferBindings()

    def restore(self):
        if not self.saved:
            return
        self.window.MakeCurrent()
        state = self.window.GetState()
        state.PushFramebufferBindings()
        try:
            for target, colors, depth, size in self.saved:
                for i, color in enumerate(colors):
                    target.AddColorAttachment(i, color)
                target.AddDepthAttachment(depth)
                target.Resize(*size)
            self.saved.clear()
        finally:
            state.PopFramebufferBindings()

    def close(self):
        self.restore()
        if self.buffers:
            self.window.MakeCurrent()
            for _, buffer in self.buffers.values():
                buffer.ReleaseGraphicsResources(self.window)
            self.buffers.clear()


# Same first-contributing-sample definition as VTK's RenderToImage, but write
# the normal host depth attachment alongside its unchanged color output.
_DEPTH = {
    "Dec": "vec3 qar_firstPos;\nbool qar_firstHit;",
    "Init": "qar_firstPos = vec3(0.0);\nqar_firstHit = false;",
    "Impl": """if (!g_skip && g_srcColor.a > 0.0 && !qar_firstHit) {
      qar_firstPos = g_dataPos;
      qar_firstHit = true;
    }""",
    "Exit": """gl_FragDepth = 1.0;
    if (qar_firstHit) {
      vec4 qar_clip = in_projectionMatrix * in_modelViewMatrix *
        in_volumeMatrix[0] * in_textureDatasetMatrix[0] * vec4(qar_firstPos, 1.0);
      gl_FragDepth = 0.5 * (gl_DepthRange.diff * qar_clip.z / qar_clip.w +
                           gl_DepthRange.near + gl_DepthRange.far);
    }""",
}


def single_pass_supported(renderer, volume):
    # This hook is verified against Slicer's VTK 9.6. Older/other pipelines keep
    # the existing supplementary depth path instead of silently changing color.
    if (vtk.vtkVersion.GetVTKMajorVersion(), vtk.vtkVersion.GetVTKMinorVersion()) != (9, 6):
        return False
    mapper = volume.GetMapper()
    if (renderer.GetPass() or renderer.GetUseSSAO() or renderer.GetUseShadows()
            or mapper.GetUseDepthPass()):
        return False
    # Slicer enables peeling by default, but VTK only runs the peeling pass
    # when a visible prop actually has translucent polygonal geometry. The
    # standard single-volume + opaque-surface path does not enter that pass.
    props = renderer.GetViewProps()
    if any(p.GetVisibility() and p.HasTranslucentPolygonalGeometry()
           for p in (props.GetItemAsObject(i) for i in range(props.GetNumberOfItems()))):
        return False
    keys = volume.GetPropertyKeys()
    key = vtk.vtkOpenGLActor.GLDepthMaskOverride()
    if keys and (keys.Has(key) or keys.Has(vtk.vtkOpenGLRenderPass.RenderPasses())):
        return False
    shader = volume.GetShaderProperty()
    if shader.HasFragmentShaderCode():
        return False
    for i in range(shader.GetNumberOfShaderReplacements()):
        name, first, value, all_matches = vtk.reference(""), vtk.reference(False), vtk.reference(""), vtk.reference(False)
        shader.GetNthShaderReplacement(i, name, first, value, all_matches)
        if any(token in str(name) + str(value) for token in ("RenderToImage", "gl_FragDepth", "qar_first")):
            return False
    return True


@contextmanager
def volume_depth(volume):
    """Use the SAME mapper, transfer functions, lights, crop and host shaders."""
    mapper = volume.GetMapper()
    original_shader = volume.GetShaderProperty()
    original_keys = volume.GetPropertyKeys()
    old_sampling = mapper.GetAutoAdjustSampleDistances(), mapper.GetImageSampleDistance()
    shader = vtk.vtkShaderProperty()
    shader.DeepCopy(original_shader)
    for stage, code in _DEPTH.items():
        tag = "//VTK::RenderToImage::" + stage
        shader.AddFragmentShaderReplacement(tag, True, tag + "\n" + code, False)
    keys = vtk.vtkInformation()
    if original_keys:
        keys.Copy(original_keys)
    keys.Set(vtk.vtkOpenGLActor.GLDepthMaskOverride(), 1)
    try:
        # ImageSampleDistance != 1 uses a separate color-only image-sampling FBO;
        # write full-resolution depth and color directly into the host FBO.
        mapper.SetAutoAdjustSampleDistances(False)
        mapper.SetImageSampleDistance(1.)
        volume.SetShaderProperty(shader)
        volume.SetPropertyKeys(keys)
        yield
    finally:
        volume.SetShaderProperty(original_shader)
        volume.SetPropertyKeys(original_keys)
        mapper.SetAutoAdjustSampleDistances(old_sampling[0])
        mapper.SetImageSampleDistance(old_sampling[1])
