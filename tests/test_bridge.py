"""Real VTK file/transform tests; no host application or rendering required."""
from pathlib import Path
import sys
import tempfile
import unittest
import vtk

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integrations"))
from qarvtk_bridge import export_polydata


class BridgeTests(unittest.TestCase):
    def test_world_transform_atomic_replace_and_no_input_mutation(self):
        cube = vtk.vtkCubeSource()
        cube.Update()
        before = cube.GetOutput().GetBounds()
        transform = vtk.vtkTransform()
        transform.Translate(100, 200, 300)  # Slicer world RAS mm, still mm in VTP.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "world.vtp"
            export_polydata(cube.GetOutput(), path, transform)
            reader = vtk.vtkXMLPolyDataReader()
            reader.SetFileName(str(path))
            reader.Update()
            self.assertEqual(reader.GetOutput().GetBounds(), (99.5, 100.5, 199.5, 200.5, 299.5, 300.5))
            self.assertEqual(reader.GetOutput().GetNumberOfPolys(), 12)
            self.assertEqual(cube.GetOutput().GetBounds(), before)
            saved = path.read_bytes()
            with self.assertRaises(ValueError):
                export_polydata(vtk.vtkPolyData(), path)
            self.assertEqual(path.read_bytes(), saved)
            export_polydata(cube.GetOutput(), path)
            reader.Modified(); reader.Update()
            self.assertEqual(reader.GetOutput().GetBounds(), before)
            self.assertEqual(list(Path(directory).iterdir()), [path])

    def test_reject_points_and_wrong_extension(self):
        source = vtk.vtkPointSource(); source.Update()
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                export_polydata(source.GetOutput(), Path(directory) / "points.vtp")
            with self.assertRaises(ValueError):
                export_polydata(source.GetOutput(), Path(directory) / "points.stl")


if __name__ == "__main__":
    unittest.main()
