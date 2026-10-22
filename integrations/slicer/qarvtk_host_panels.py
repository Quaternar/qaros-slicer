"""Read-only slice previews from completed VTK frames, including live video.

No extra renders, screen capture, image readback or input forwarding. Image
bounds drive a GPU crop; corner labels are hidden while a preview owns a view.
The latest rendered image replaces any older pending image.
"""
from time import perf_counter
from math import hypot, floor, ceil, isfinite
from itertools import product
import json
import vtk

PANEL_DIAGONAL_METERS = 14. * .0254


def panel_size(width, height):
    scale = min(1., 768. / max(width, height))
    return tuple(max(16, 2 * int(x * scale / 2)) for x in (width, height))


def panel_positions(sizes, position, workspace):
    """Center rows of at most three image rectangles with 2.5 cm gaps."""
    result = {}
    x, y, z = position
    top = y - workspace[1]*.5 - .06
    entries = list(sizes.items())
    for start in range(0, len(entries), 3):
        row = entries[start:start+3]
        left = x - (sum(size[0] for _, size in row) + .025*(len(row)-1))*.5
        for name, size in row:
            result[name] = (left + size[0]*.5, top, z + .05)
            left += size[0] + .025
        top -= max(size[1] for _, size in row) + .025
    return result


def image_layers(widget):
    if widget is None or not hasattr(widget, "sliceLogic"):
        return []
    logic = widget.sliceLogic()
    composite = logic.GetSliceCompositeNode()
    layers = [logic.GetBackgroundLayer()]
    if composite.GetForegroundOpacity() > 0:
        layers.append(logic.GetForegroundLayer())
    if composite.GetLabelOpacity() > 0:
        layers.append(logic.GetLabelLayer())
    return [layer for layer in layers if layer and layer.GetVolumeNode() and layer.GetVolumeNode().GetImageData()]


def panel_title(widget, name):
    names = list(dict.fromkeys(str(layer.GetVolumeNode().GetName()) for layer in image_layers(widget)))
    title = " / ".join(names) + " · " + name if names else "Slicer " + name
    # Native labels are UTF-8 with a 128-byte limit; identity uses common_name.
    return title.encode("utf-8")[:128].decode("utf-8", "ignore")


def image_region(widget, size):
    """Visible image/slice intersection in bottom-left render-window pixels.

    Derive the rectangle from geometry, never from pixel brightness: black
    image content is valid data. For nonlinear transforms or lightbox layouts,
    retain the full view instead of risking a crop through visible data.
    """
    full = (0, 0, *size)
    layers = image_layers(widget)
    if not layers:
        return full
    node = widget.mrmlSliceNode()
    if node.GetLayoutGridRows() != 1 or node.GetLayoutGridColumns() != 1:
        return full
    dimensions = node.GetDimensions()
    if min(dimensions[:2]) <= 0:
        return full
    points = []
    for layer in layers:
        volume = layer.GetVolumeNode()
        parent = volume.GetParentTransformNode()
        if parent and not parent.IsTransformToWorldLinear():
            return full
        extent = volume.GetImageData().GetExtent()
        if any(extent[i] > extent[i+1] for i in (0, 2, 4)):
            continue
        ijk_to_xy = layer.GetXYToIJKTransform().GetInverse()
        corners = [ijk_to_xy.TransformPoint(p) for p in product(
            (extent[0]-.5, extent[1]+.5), (extent[2]-.5, extent[3]+.5), (extent[4]-.5, extent[5]+.5))]
        if not all(isfinite(x) for p in corners for x in p):
            return full
        for i, a in enumerate(corners):
            if abs(a[2]) < 1e-8:
                points.append(a[:2])
            for bit in (1, 2, 4):
                j = i ^ bit
                if j <= i:
                    continue
                b = corners[j]
                if a[2]*b[2] < 0:
                    t = -a[2] / (b[2]-a[2])
                    points.append(tuple(a[k] + t*(b[k]-a[k]) for k in (0, 1)))
    if not points:
        return full
    # Use the displayed part of the image; don't zoom or recenter Slicer's view.
    x0, y0 = [max(0, floor(min(p[i] for p in points)*size[i]/dimensions[i] + 1e-6)) for i in (0, 1)]
    x1, y1 = [min(size[i], ceil(max(p[i] for p in points)*size[i]/dimensions[i] - 1e-6)) for i in (0, 1)]
    return (x0, y0, x1-x0, y1-y0) if x1-x0 >= 2 and y1-y0 >= 2 else full


class SlicePreview:
    def __init__(self, name, view, sink, position, widget=None):
        self.name, self.view, self.sink = name, view, sink
        self.widget = widget
        self.window = view.renderWindow()
        self.context_stamp = self.window.GetContextCreationTime()
        self.revision, self.captured_revision = 1, 0
        self.next_capture = 0.
        self.visible = True
        self.observer = None
        self.start_observer = None
        self.annotation = None
        self.annotation_visible = None
        self.handle = None
        self.region = image_region(widget, self.window.GetSize())
        self.size = panel_size(*self.region[2:])
        self.title = panel_title(widget, name)
        # Physical screen size is independent of voxel/model scale and pixel
        # resolution. Set it only at creation; QAROS owns later user resizing.
        self.physical_size = tuple(PANEL_DIAGONAL_METERS * x / hypot(*self.size) for x in self.size)
        self.window.MakeCurrent()
        try:
            self.handle = sink.panel_create(self.title, self.size, position, self.physical_size[0])
            self.observer = self.window.AddObserver(vtk.vtkCommand.EndEvent, self.rendered)
            if hasattr(view, "cornerAnnotation"):
                self.annotation = view.cornerAnnotation()
                self.annotation_visible = self.annotation.GetVisibility()
                if self.annotation_visible:
                    # Wait for Slicer's next normal frame, never send the old
                    # texture containing labels or trigger a recursive Render().
                    self.captured_revision = self.revision
                self.hide_annotation()
                self.start_observer = self.window.AddObserver(vtk.vtkCommand.StartEvent, self.hide_annotation)
                view.scheduleRender()
        except Exception:
            self.close()
            raise

    def hide_annotation(self, *unused):
        if self.annotation:
            self.annotation.SetVisibility(False)

    def rendered(self, *unused):
        self.region = image_region(self.widget, self.window.GetSize())
        self.revision += 1

    def matches(self, view):
        return (view == self.view and view.renderWindow() == self.window
                and self.window.GetContextCreationTime() == self.context_stamp)

    def set_visible(self, visible):
        if visible != self.visible:
            self.sink.panel_visible(self.handle, visible)
            self.visible = visible
            if visible:
                self.revision += 1

    def capture(self, now, interval):
        if not self.visible or self.revision == self.captured_revision or now < self.next_capture:
            return None
        if self.window.GetNeverRendered():
            return None
        self.window.MakeCurrent()
        framebuffer = self.window.GetDisplayFramebuffer()
        texture = framebuffer.GetColorAttachmentAsTextureObject(0) if framebuffer else None
        if not texture or not texture.GetHandle():
            return None
        width, height = texture.GetWidth(), texture.GetHeight()
        if width < 1 or height < 1:
            return None
        revision = self.revision
        started = perf_counter()
        copied = self.sink.panel_capture(self.handle, texture.GetHandle(), width, height, self.region)
        if not copied:
            # Keep it dirty; retry the newest image later, without waiting.
            self.next_capture = now + min(.02, interval)
            return dict(busy=1)
        skipped = max(0, revision - self.captured_revision - 1)
        self.captured_revision = revision
        self.next_capture = now + interval
        return dict(busy=0, replaced=skipped, copy_ms=(perf_counter() - started) * 1000,
                    source_size=(width, height))

    def close(self):
        if self.start_observer is not None:
            self.window.RemoveObserver(self.start_observer)
            self.start_observer = None
        if self.observer is not None:
            self.window.RemoveObserver(self.observer)
            self.observer = None
        if self.annotation is not None:
            self.annotation.SetVisibility(self.annotation_visible)
            self.annotation = None
            self.view.scheduleRender()
        if self.handle is not None:
            self.sink.panel_close(self.handle)
            self.handle = None


class SlicePanels:
    def __init__(self, layout, sink, workspace_position, workspace_size, fps=15, clock=perf_counter):
        if fps not in (5, 10, 15, 30):
            raise ValueError("Slice preview FPS must be 5, 10, 15 or 30")
        self.layout, self.sink = layout, sink
        self.position, self.workspace = workspace_position, workspace_size
        self.interval, self.clock = 1. / fps, clock
        self.sources, self.errors, self.slots = {}, {}, {}
        self.initial_positions = {}
        self.next_refresh = self.next_status = 0.
        self.next_report = clock() + 5
        self.cursor = 0
        self.closed = False
        self.frames = self.busy = self.replaced = 0
        self.copy_ms = 0.
        self.source_sizes = {}
        self.status = "Waiting for visible slice views"

    def _error(self, name, error):
        self.errors[name] = str(error)
        source = self.sources.pop(name, None)
        if source:
            source.close()
        self.sink.diagnostics.event("PANEL_ERROR " + json.dumps(dict(view=name, error=str(error))))

    def refresh(self):
        names = [str(name) for name in self.layout.sliceViewNames()]
        names.sort(key=lambda name: (("Red", "Yellow", "Green").index(name)
                                    if name in ("Red", "Yellow", "Green") else 3, name))
        if not self.initial_positions:
            sizes = {}
            for name in names:
                try:
                    widget = self.layout.sliceWidget(name)
                    if not widget or not widget.mrmlSliceNode().IsViewVisibleInLayout() or not widget.sliceView().isVisible():
                        continue
                    window = widget.sliceView().renderWindow()
                    if min(window.GetSize()) < 16 or window.GetNeverRendered():
                        continue
                    size = panel_size(*image_region(widget, window.GetSize())[2:])
                    sizes[name] = tuple(PANEL_DIAGONAL_METERS*x/hypot(*size) for x in size)
                except Exception:
                    pass  # The ordinary per-panel path below reports the error.
            self.initial_positions = panel_positions(sizes, self.position, self.workspace)
        for name in list(self.sources):
            if name not in names:
                self.sources.pop(name).close()
        for name in names:
            if name in self.errors:
                continue
            try:
                widget = self.layout.sliceWidget(name)
                if not widget:
                    continue
                view = widget.sliceView()
                visible = bool(widget.mrmlSliceNode().IsViewVisibleInLayout() and view.isVisible())
                source = self.sources.get(name)
                if source and not source.matches(view):
                    self.sources.pop(name).close()
                    source = None
                if source:
                    source.set_visible(visible)
                elif visible and min(view.renderWindow().GetSize()) >= 16 and not view.renderWindow().GetNeverRendered():
                    if name not in self.slots:
                        if len(self.slots) >= 8:
                            raise RuntimeError("At most eight slice previews are supported per stream")
                        self.slots[name] = len(self.slots)
                    slot = self.slots[name]
                    x, y, z = self.position
                    position = self.initial_positions.get(name,
                        (x + (slot % 3 - 1) * (PANEL_DIAGONAL_METERS + .025),
                         y - self.workspace[1] * .5 - .06 - (slot // 3) * (PANEL_DIAGONAL_METERS + .025), z + .05))
                    self.sources[name] = SlicePreview(name, view, self.sink, position, widget)
                    self.sink.diagnostics.event("PANEL_SOURCE " + json.dumps(dict(view=name,
                        stream_size=self.sources[name].size, physical_size_m=self.sources[name].physical_size,
                        image_region=self.sources[name].region, display_name=self.sources[name].title,
                        position=position, fps_limit=round(1/self.interval))))
            except Exception as error:
                self._error(name, error)

    def tick(self):
        if self.closed:
            return
        now = self.clock()
        if now >= self.next_refresh:
            self.refresh()
            self.next_refresh = now + .25
        # Fair rotation; at most one GPU capture in a GUI timer turn. Video
        # frames arriving while a capture/send is busy are naturally coalesced.
        sources = list(self.sources.values())
        for _ in sources:
            self.cursor %= len(sources)
            source = sources[self.cursor]
            self.cursor += 1
            try:
                result = source.capture(now, self.interval)
                if result:
                    self.busy += result["busy"]
                    if not result["busy"]:
                        self.frames += 1
                        self.copy_ms += result["copy_ms"]
                        self.replaced += result["replaced"]
                        self.source_sizes[source.name] = result["source_size"]
                    break
            except Exception as error:
                self._error(source.name, error)
        if now >= self.next_status:
            labels = [name + ": " + (self.sink.panel_status(s.handle) if s.visible else "hidden")
                      for name, s in self.sources.items()]
            labels += [name + ": " + error for name, error in self.errors.items()]
            self.status = "; ".join(labels) if labels else "No visible slice views in this layout"
            self.next_status = now + 1
        if now >= self.next_report:
            if self.frames or self.busy:
                self.sink.diagnostics.event("PERF_PANELS " + json.dumps(dict(frames=self.frames,
                    capture_ms=self.copy_ms / max(1, self.frames), busy_skips=self.busy,
                    replaced_frames=self.replaced, source_sizes=self.source_sizes)))
            self.frames = self.busy = self.replaced = 0
            self.copy_ms = 0.
            self.next_report = now + 5

    def close(self):
        self.closed = True
        for source in self.sources.values():
            source.close()
        self.sources.clear()
