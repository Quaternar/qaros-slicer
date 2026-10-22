"""Run with pvpython, under Xvfb if the host build needs a display."""
from pathlib import Path
import sys
import tempfile
from paraview import simple
from vtkmodules.vtkIOXML import vtkXMLPolyDataReader

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integrations" / "paraview"))
from qarvtk_paraview import export_active

source = simple.Box(XLength=2, YLength=4, ZLength=6, Center=[1, 2, 3])
view = simple.CreateRenderView()
display = simple.Show(source, view)
display.Position = [10, 20, 30]
display.Scale = [2, 2, 2]
simple.SetActiveSource(source)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "paraview.vtp"
    export_active(path)
    reader = vtkXMLPolyDataReader()
    reader.SetFileName(str(path)); reader.Update()
    assert reader.GetOutput().GetBounds() == (10, 14, 20, 28, 30, 42), reader.GetOutput().GetBounds()
    assert simple.GetActiveSource().SMProxy == source.SMProxy
    assert list(display.Position) == [10, 20, 30]
print("PASS: ParaView surface and display transform, active source preserved")
