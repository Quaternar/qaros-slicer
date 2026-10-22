"""Prepare a real Slicer GUI with the packaged plugin and synthetic geometry."""
import os
from pathlib import Path
import traceback

import qt
import slicer
import vtk


def prepare():
    try:
        surface = vtk.vtkSuperquadricSource()
        surface.SetToroidal(True)
        surface.SetSize(80)
        surface.SetScale(1.0, 0.75, 1.0)
        surface.SetThickness(0.35)
        surface.SetPhiRoundness(0.8)
        surface.SetThetaRoundness(0.7)
        surface.SetPhiResolution(96)
        surface.SetThetaResolution(128)
        surface.Update()
        model = slicer.modules.models.logic().AddModel(surface.GetOutput())
        model.SetName("QAROS demo surface")
        display = model.GetDisplayNode()
        display.SetColor(0.20, 0.82, 0.50)
        display.SetScalarVisibility(False)
        display.SetBackfaceCulling(False)

        slicer.app.layoutManager().setLayout(
            slicer.vtkMRMLLayoutNode.SlicerLayoutOneUp3DView)
        slicer.util.selectModule("QarosLive")
        widget = slicer.modules.qaroslive.widgetRepresentation().self()
        # No SDK, credentials or network session are fabricated for a screenshot.
        assert widget.transport is None

        window = slicer.util.mainWindow()
        window.resize(1680, 1040)
        panel = slicer.util.findChild(window, "PanelDockWidget")
        panel.setMinimumWidth(470)
        view = slicer.app.layoutManager().threeDWidget(0).threeDView()
        view.mrmlViewNode().SetBackgroundColor(0.10, 0.13, 0.17)
        view.mrmlViewNode().SetBackgroundColor2(0.23, 0.27, 0.33)
        view.mrmlViewNode().SetBoxVisible(False)
        slicer.util.resetThreeDViews()
        camera = view.renderWindow().GetRenderers().GetFirstRenderer().GetActiveCamera()
        camera.Azimuth(25)
        camera.Elevation(25)
        camera.Zoom(1.15)
        view.renderWindow().Render()
        qt.QTimer.singleShot(2000, mark_ready)
    except Exception:
        traceback.print_exc()
        Path(os.environ["QARVTK_SCREENSHOT_READY"] + ".failed").touch()


def mark_ready():
    Path(os.environ["QARVTK_SCREENSHOT_READY"]).write_text("Slicer GUI ready\n")


qt.QTimer.singleShot(3000, prepare)
