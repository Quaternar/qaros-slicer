"""Experimental direct streaming of Slicer's own render window."""
from datetime import datetime
from pathlib import Path
import json
import sys
import qt
import ctk
import slicer
from slicer.ScriptedLoadableModule import ScriptedLoadableModule, ScriptedLoadableModuleWidget

LIVE_VERSION = "r8.4"

sys.path.insert(0, str(Path(__file__).resolve().parent))
if (Path(__file__).resolve().parent / "QarosVTKLib").is_dir():
    from QarosVTKLib.qarvtk_host_view import create_host_views, workspace_envelope
    from QarosVTKLib.qarvtk_host_runtime import HostTransport, default_sdk_path, default_host_dll_path
    from QarosVTKLib.qarvtk_host_panels import SlicePanels
    from QarosVTKLib.qarvtk_runtime import user_directory
else:
    from qarvtk_host_view import create_host_views, workspace_envelope
    from qarvtk_host_runtime import HostTransport, default_sdk_path, default_host_dll_path
    from qarvtk_host_panels import SlicePanels
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from qarvtk_runtime import user_directory


class QarosLive(ScriptedLoadableModule):
    def __init__(self, parent):
        super().__init__(parent)
        parent.title = "QAROS Live Renderer"
        parent.categories = ["QAROS"]
        parent.contributors = ["QuaternAR"]
        parent.helpText = "Stream the first Slicer 3D view using its actual renderer. Experimental Windows session-SDK integration."
        parent.acknowledgementText = "QAROS live host-rendering prototype."


class QarosLiveWidget(ScriptedLoadableModuleWidget):
    def setup(self):
        super().setup()
        self.transport = None
        self.cleaned_up = False
        self.settings = qt.QSettings()
        form = qt.QFormLayout()
        self.layout.addLayout(form)
        label = qt.QLabel("Live first 3D view. Changes in Slicer are rendered automatically. "
                         "Experimental. Each visible VTK GPU Composite volume can have its own movable QAROS workspace. "
                         "Transfer functions and ROI cropping stay live.")
        label.wordWrap = True
        form.addRow(label)
        self.versionLabel = qt.QLabel("Live plugin " + LIVE_VERSION + " · Slice panels: 14-inch diagonal at Start")
        self.versionLabel.toolTip = str(Path(__file__).resolve())
        form.addRow(self.versionLabel)
        self.separateVolumes = qt.QCheckBox("Separate QAROS workspace for each volume")
        self.separateVolumes.checked = True
        self.separateVolumes.toolTip = "One cube and stereo stream per visible volume, placed side by side. " \
            "Each cube contains only its volume; surfaces and view decorations stay in Slicer. " \
            "Turn off to stream the whole scene in one workspace. Restart streaming after adding/removing a volume."
        form.addRow(self.separateVolumes)
        self.fit = qt.QCheckBox("Center model in workspace at Start")
        self.fit.checked = True
        self.fit.toolTip = "Center the data without changing their physical size. The workspace fits the data. " \
                           "Separate workspaces are arranged side by side."
        form.addRow(self.fit)
        self.modelScale = qt.QSlider(qt.Qt.Horizontal)
        self.modelScale.minimum = -300
        self.modelScale.maximum = 300
        self.modelScale.singleStep = 25
        self.modelScale.pageStep = 100
        self.modelScale.tickInterval = 100
        self.modelScale.tickPosition = qt.QSlider.TicksBelow
        self.modelScale.value = 0
        self.modelScale.toolTip = "Applied at Start. Middle = actual size: 1 Slicer mm = 1 physical mm. " \
            "Each large tick halves or doubles the size (1/8x to 8x). Same factor for every volume."
        self.modelScaleValue = qt.QLabel("1x (1:1)")
        self.resetScale = qt.QPushButton("1:1")
        self.resetScale.toolTip = "Restore actual physical size"
        self.resetScale.connect("clicked()", lambda: self.modelScale.setValue(0))
        self.modelScale.connect("valueChanged(int)", self.scale_changed)
        scaleRow = qt.QHBoxLayout()
        scaleRow.addWidget(qt.QLabel("1/8x"))
        scaleRow.addWidget(self.modelScale)
        scaleRow.addWidget(qt.QLabel("8x"))
        scaleRow.addWidget(self.modelScaleValue)
        scaleRow.addWidget(self.resetScale)
        form.addRow("Model size (at Start)", scaleRow)
        orientation = qt.QLabel("Slicer camera rotation affects only the desktop view. "
                               "Rotate the workspace in QAROS to turn the streamed model.")
        orientation.wordWrap = True
        form.addRow(orientation)
        self.slicePreviews = qt.QCheckBox("Show image-only slice panels (preview only)")
        self.slicePreviews.checked = True
        self.slicePreviews.toolTip = "Live images of visible slice views, including video/sequences. " \
            "Each panel starts with a 14-inch diagonal, independent of model size; resize it in QAROS. " \
            "Image bounds remove empty margins; data names appear in the window title. " \
            "Slice corner annotations are hidden in Slicer while streaming and restored on Stop. " \
            "No remote slice interaction. Applied at Start."
        form.addRow(self.slicePreviews)
        self.sliceFps = qt.QComboBox()
        self.sliceFps.addItems(["5", "10", "15", "30"])
        self.sliceFps.currentIndex = 2
        self.sliceFps.toolTip = "Maximum updates per second per slice. Newest frame wins if the sender cannot keep up."
        form.addRow("Slice preview FPS limit", self.sliceFps)
        self.panelStatus = qt.QLabel("Slice previews start with streaming.")
        self.panelStatus.wordWrap = True
        form.addRow(self.panelStatus)
        self.workspaceStatus = qt.QLabel("Dimensions follow the visible scene at Start.")
        self.workspaceStatus.wordWrap = True
        form.addRow("Workspace", self.workspaceStatus)
        self.renderScale = qt.QComboBox()
        self.renderScale.addItems(["100%", "75%", "50%"])
        self.renderScale.toolTip = "Render width and height relative to the viewer request. 50% uses one quarter of the pixels. Applied at Start."
        form.addRow("Render resolution", self.renderScale)
        self.resolutionStatus = qt.QLabel("Resolution will be shown when the viewer connects.")
        self.resolutionStatus.wordWrap = True
        form.addRow(self.resolutionStatus)
        self.dll = ctk.ctkPathLineEdit()
        self.dll.filters = ctk.ctkPathLineEdit.Files
        default_dll = Path(__file__).resolve().parents[2] / "Release" / "qarvtk-host.dll"
        self.dll.currentPath = default_host_dll_path(self.settings.value("QAROS_LIVE/Dll", ""), default_dll)
        form.addRow("Host bridge DLL", self.dll)
        self.sdk = ctk.ctkPathLineEdit()
        self.sdk.filters = ctk.ctkPathLineEdit.Dirs
        self.sdk.currentPath = default_sdk_path(self.settings.value(
            "QAROS_LIVE/Sdk", self.settings.value("QAROS_VTK/Sdk", "")))
        form.addRow("QAROS SDK folder", self.sdk)
        self.target = qt.QComboBox()
        self.targetModes = ("both", "visualizer", "headset")
        self.target.addItems(["Visualizer + headset", "Visualizer only", "Headset only"])
        saved_target = str(self.settings.value("QAROS_LIVE/Target", "both"))
        self.target.currentIndex = self.targetModes.index(saved_target) if saved_target in self.targetModes else 0
        self.target.toolTip = "Supports one Visualizer receiver and one headset. Each has its own stereo view. " \
            "Connect the headset through QAROS; it can join after streaming starts. Applied at Start."
        form.addRow("Stream to", self.target)
        self.hubHint = qt.QLabel("Connect HoloLens in QAROS and allow Slicer when prompted. Visualizer + headset sends the volumes and slice panels to both; either target may join later. Keep QAROS and the headset player on the same version. For a replaced receiver, stop and restart streaming.")
        self.hubHint.wordWrap = True
        form.addRow(self.hubHint)
        self.startButton = qt.QPushButton("Start live streaming")
        self.stopButton = qt.QPushButton("Stop live streaming")
        form.addRow(self.startButton, self.stopButton)
        self.stopButton.enabled = False
        self.status = qt.QLabel("Configure the first 3D view in Slicer, then Start live streaming.")
        self.status.wordWrap = True
        form.addRow(self.status)
        self.logPath = qt.QLineEdit()
        self.logPath.readOnly = True
        form.addRow("Log folder", self.logPath)
        self.startButton.connect("clicked()", self.start_stream)
        self.stopButton.connect("clicked()", self.stop_stream)
        # Application ownership keeps polling alive while SDK shutdown finishes,
        # even when Slicer unloads the module's widget.
        self.timer = qt.QTimer(slicer.app)
        self.timer.setInterval(5)
        self.timer.connect("timeout()", self.tick)
        self.timer.start()
        self.scene_observer = slicer.mrmlScene.AddObserver(slicer.mrmlScene.StartCloseEvent, self.scene_closing)
        self.layout_manager = slicer.app.layoutManager()
        if self.layout_manager:
            self.layout_manager.connect("layoutChanged(int)", self.layout_changed)
        slicer.app.connect("aboutToQuit()", self.cleanup)
        self.layout.addStretch(1)

    def start_stream(self):
        hosts = []
        created_transport = None
        try:
            if self.transport and self.transport.running:
                return
            lm = slicer.app.layoutManager()
            if not lm or lm.threeDViewCount < 1:
                raise RuntimeError("Open a Slicer layout containing a 3D view")
            view = lm.threeDWidget(0).threeDView()
            view.renderWindow().Render()
            manager = view.displayableManagerByClassName("vtkMRMLVolumeRenderingDisplayableManager")
            get_actor = getattr(manager, "GetVolumeActor", None)
            names = [(get_actor(node), node.GetName())
                     for node in slicer.util.getNodesByClass("vtkMRMLVolumeNode")] if get_actor else []
            hosts = create_host_views(view, separate=self.separateVolumes.checked,
                names=lambda actor: next((name for prop, name in names if prop == actor), None),
                fit=self.fit.checked,
                size_multiplier=2. ** (self.modelScale.value / 100.),
                render_scale=(1., .75, .5)[self.renderScale.currentIndex])
            folder = user_directory() / "live-logs" / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
            target_mode = self.targetModes[self.target.currentIndex]
            self.transport = HostTransport(self.dll.currentPath, self.sdk.currentPath,
                                            target_mode, folder, hosts)
            created_transport = self.transport
            self.transport.diagnostics.event("PLUGIN " + json.dumps(dict(
                version=LIVE_VERSION, path=str(Path(__file__).resolve()), orientation_mode="fixed_ras")))
            if self.slicePreviews.checked:
                position, size = workspace_envelope(hosts)
                self.transport.panels = SlicePanels(lm, self.transport, position, size, fps=int(self.sliceFps.currentText))
            self.workspaceStatus.text = "\n".join(host.name + ": " + " × ".join(f"{x * 100:.1f}" for x in host.workspace_size)
                                                 + " cm" for host in hosts)
            self.settings.setValue("QAROS_LIVE/Dll", self.dll.currentPath)
            self.settings.setValue("QAROS_LIVE/Sdk", self.sdk.currentPath)
            self.settings.setValue("QAROS_LIVE/Target", target_mode)
            self.logPath.text = str(folder)
            self.startButton.enabled = False
            self.stopButton.enabled = True
            self.status.text = "Connecting the live Slicer renderer..."
        except Exception as error:
            if created_transport:
                created_transport.request_stop("Startup error: " + str(error))
            else:
                for host in hosts:
                    host.close()
            self.status.text = str(error)
            slicer.util.errorDisplay(str(error))

    def stop_stream(self, *args):
        if self.transport:
            self.transport.request_stop("Stop button")

    def scale_changed(self, value):
        factor = 2. ** (value / 100.)
        self.modelScaleValue.text = "1x (1:1)" if value == 0 else f"{factor:.3g}x"

    def scene_closing(self, *args):
        if self.transport:
            self.transport.request_stop("Slicer scene closing")

    def layout_changed(self, layout):
        transport = self.transport
        if not transport or not transport.running or transport.stopping:
            return
        try:
            # Slicer emits layoutChanged even when maximizing the same view.
            # Continue using that view; never silently switch the source.
            widget = self.layout_manager.threeDWidget(0)
            if not widget or widget.threeDView() != transport.host.view:
                raise RuntimeError("The source 3D view was replaced")
            if not widget.mrmlViewNode().IsViewVisibleInLayout():
                raise RuntimeError("The source 3D view was removed from the layout")
            for host in transport.hosts:
                host.validate_source()
        except Exception as error:
            transport.request_stop("Slicer layout changed: " + str(error))
            return
        transport.diagnostics.event("LAYOUT_CHANGED " + json.dumps(dict(
            layout=int(layout), action="continue", desktop_size=list(transport.host.window.GetSize()))))

    def tick(self):
        if self.transport:
            try:
                self.transport.tick()
                if not self.cleaned_up:
                    self.status.text = self.transport.status()
                    self.resolutionStatus.text = self.transport.resolution_status
                    self.panelStatus.text = getattr(self.transport, "panel_status_text", "Slice previews off")
            except Exception as error:
                self.transport.request_stop("GUI error: " + str(error))
                if not self.cleaned_up:
                    self.status.text = str(error)
            if not self.cleaned_up:
                self.startButton.enabled = not self.transport.running
                self.stopButton.enabled = self.transport.running and not self.transport.stopping
                for control in (self.target, self.separateVolumes, self.fit, self.modelScale, self.resetScale,
                                self.renderScale, self.slicePreviews, self.sliceFps):
                    control.enabled = not self.transport.running
        if self.cleaned_up and (not self.transport or not self.transport.running):
            self.timer.stop()
            self.timer.disconnect("timeout()", self.tick)
            self.timer.deleteLater()

    def cleanup(self):
        if self.cleaned_up:
            return
        self.cleaned_up = True
        if self.transport:
            self.transport.request_stop("Module cleanup or application exit")
        slicer.mrmlScene.RemoveObserver(self.scene_observer)
        if self.layout_manager:
            self.layout_manager.disconnect("layoutChanged(int)", self.layout_changed)
        slicer.app.disconnect("aboutToQuit()", self.cleanup)

