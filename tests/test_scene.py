"""Round-trip real XML payloads, transfer functions and immutable publication."""
from pathlib import Path
import sys
import tempfile
import unittest
import vtk

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integrations"))
from qarvtk_scene import export_scene, surface, volume


class SceneTests(unittest.TestCase):
    def test_material_volume_and_atomic_generations(self):
        cube = vtk.vtkCubeSource(); cube.Update()
        model = surface(cube.GetOutput(), color=(1, 0.25, 0), opacity=1.0)
        image = vtk.vtkImageData(); image.SetDimensions(4, 5, 6); image.SetSpacing(0.7, 0.8, 2)
        image.AllocateScalars(vtk.VTK_SHORT, 1); image.GetPointData().GetScalars().Fill(1200)
        matrix = vtk.vtkMatrix4x4(); matrix.SetElement(0, 3, 123)
        prop = vtk.vtkVolumeProperty()
        prop.GetRGBTransferFunction().AddRGBPoint(-1000, 0, 0, 0)
        prop.GetRGBTransferFunction().AddRGBPoint(1200, 1, 0.9, 0.8, 0.4, 0.2)
        prop.GetScalarOpacity().RemoveAllPoints()
        prop.GetScalarOpacity().AddPoint(-1000, 0)
        prop.GetScalarOpacity().AddPoint(1200, 0.8, 0.3, 0.1)
        prop.SetScalarOpacityUnitDistance(0.75); prop.ShadeOn()
        vol = volume(image, matrix, prop)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "scene.vtm"
            export_scene([model, vol], path)
            original = path.read_bytes()
            reader = vtk.vtkXMLMultiBlockDataReader(); reader.SetFileName(str(path)); reader.Update()
            self.assertEqual(reader.GetOutput().GetNumberOfBlocks(), 2)
            m, v = [reader.GetOutput().GetBlock(i) for i in range(2)]
            self.assertEqual(m.GetFieldData().GetArray("qarvtk.material").GetTuple(0)[:4], (1, .25, 0, 1.0))
            self.assertEqual(v.GetSpacing(), image.GetSpacing())
            self.assertEqual(v.GetScalarRange(), (1200, 1200))
            self.assertEqual(v.GetFieldData().GetArray("qarvtk.matrix").GetComponent(0, 3), 123)
            self.assertEqual(v.GetFieldData().GetArray("qarvtk.opacity").GetTuple(1), (1200, .8, .3, .1))
            self.assertEqual(v.GetFieldData().GetArray("qarvtk.volume").GetComponent(0, 0), .75)
            self.assertIsNone(image.GetFieldData().GetArray("qarvtk.matrix"))
            with self.assertRaisesRegex(ValueError, "opaque surfaces"):
                export_scene([surface(cube.GetOutput(), opacity=.5), vol], path)
            with self.assertRaisesRegex(ValueError, "one rendered volume"):
                export_scene([vol, vol], path)
            self.assertEqual(path.read_bytes(), original)
            old_manifest = Path(directory) / "previous.vtm"; old_manifest.write_bytes(original)
            export_scene([model], path)
            # A sender that opened the previous manifest can still finish reading it.
            reader.SetFileName(str(old_manifest)); reader.Update()
            self.assertEqual(reader.GetOutput().GetNumberOfBlocks(), 2)
            self.assertEqual(reader.GetOutput().GetBlock(1).GetScalarRange(), (1200, 1200))


if __name__ == "__main__":
    unittest.main()
