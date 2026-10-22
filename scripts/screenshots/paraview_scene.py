"""Prepare a real ParaView GUI; run with paraview --script, not pvpython."""
import os
from pathlib import Path

from paraview import simple

# ParaView's GUI executes --script as a string without defining __file__.
root = Path(os.environ["QARVTK_SOURCE_ROOT"])
simple.LoadPlugin(str(root / "package/QAROS-VTK-plugins/paraview/QarosVTKPlugin.py"),
                  remote=False, ns=globals())

wavelet = simple.Wavelet(registrationName="Demo volume (Wavelet)")
wavelet.WholeExtent = [-20, 20, -20, 20, -20, 20]
contour = simple.Contour(registrationName="Surface (isovalue 180)", Input=wavelet)
contour.ContourBy = ["POINTS", "RTData"]
contour.Isosurfaces = [180.0]
stream = QAROSStreaming(registrationName="QAROS Streaming", Input=contour)
stream.MetresPerUnit = 0.01
stream.UpdatePipeline()
assert stream.GetDataInformation().GetNumberOfPoints() > 0
assert stream.GetProperty("SenderStatus").GetData() == "Sender has not been started."

view = simple.GetActiveViewOrCreate("RenderView")
display = simple.Show(stream, view)
display.Representation = "Surface"
simple.ColorBy(display, None)
display.DiffuseColor = [0.20, 0.82, 0.50]
display.AmbientColor = [0.20, 0.82, 0.50]
simple.Hide(wavelet, view)
simple.Hide(contour, view)
view.UseColorPaletteForBackground = 0
view.Background = [0.10, 0.13, 0.17]
view.OrientationAxesVisibility = 1
simple.ResetCamera(view)
camera = view.GetActiveCamera()
camera.Azimuth(30)
camera.Elevation(25)
simple.ResetCamera(view)
simple.SetActiveSource(stream)
simple.Render(view)
Path(os.environ["QARVTK_SCREENSHOT_READY"]).write_text("ParaView GUI ready\n")
