"""Version 2 scene snapshots: lit surfaces and one scalar volume, using VTK XML.

Each immutable generation is complete before the small manifest is replaced.
Published generations live until Backend.close(), so a reader can never race a
writer deleting data. Volume updates are deliberately manual in the Slicer UI.
"""
import math
import os
from pathlib import Path
import shutil
import uuid
import xml.etree.ElementTree as ET

if __package__:
    from .qarvtk_bridge import vtk_module
else:
    from qarvtk_bridge import vtk_module


def array(data, name, rows, components=None):
    vtk = vtk_module()
    rows = list(rows)
    if not rows:
        raise ValueError("Empty scene property: " + name)
    if components is None:
        rows = [(x,) for x in rows]
        components = 1
    values = vtk.vtkDoubleArray()
    values.SetName("qarvtk." + name)
    values.SetNumberOfComponents(components)
    for row in rows:
        if len(row) != components or not all(math.isfinite(x) for x in row):
            raise ValueError("Invalid scene property: " + name)
        values.InsertNextTuple(row)
    data.GetFieldData().AddArray(values)


def surface(data, transform=None, color=(0.8, 0.8, 0.85), opacity=1.0,
            material=(0.2, 0.75, 0.25, 30.0), scalar_mode=0):
    vtk = vtk_module()
    if data is None or not data.IsA("vtkPolyData") or not data.GetNumberOfPoints():
        raise ValueError("Expected a non-empty polygon surface")
    if transform is not None:
        bake = vtk.vtkTransformPolyDataFilter()
        bake.SetInputData(data); bake.SetTransform(transform); bake.Update()
        data = bake.GetOutput()
    triangles = vtk.vtkTriangleFilter()
    triangles.SetInputData(data); triangles.PassVertsOff(); triangles.PassLinesOff(); triangles.Update()
    if not triangles.GetOutput().GetNumberOfPolys():
        raise ValueError("This object has no surface polygons")
    normals = vtk.vtkPolyDataNormals()
    normals.SetInputConnection(triangles.GetOutputPort())
    normals.SplittingOff(); normals.ConsistencyOn(); normals.Update()
    result = vtk.vtkPolyData(); result.DeepCopy(normals.GetOutput())
    array(result, "material", [(*color, opacity, *material)], 8)
    array(result, "scalar_mode", [scalar_mode])
    return result


def volume(image, matrix, prop):
    """Image physical coordinates -> world matrix; transfer functions stay in voxel units."""
    vtk = vtk_module()
    if image is None or image.GetNumberOfPoints() == 0 or image.GetNumberOfScalarComponents() != 1:
        raise ValueError("Volume rendering requires a non-empty single-component scalar volume")
    result = vtk.vtkImageData(); result.DeepCopy(image)
    array(result, "matrix", [[matrix.GetElement(r, c) for r in range(4) for c in range(4)]], 16)
    for name, function, count in (("color", prop.GetRGBTransferFunction(0), 6),
                                  ("opacity", prop.GetScalarOpacity(0), 4),
                                  ("gradient", prop.GetGradientOpacity(0), 4)):
        nodes = []
        for index in range(function.GetSize()):
            node = [0.0] * count; function.GetNodeValue(index, node); nodes.append(node)
        if nodes:
            array(result, name, nodes, count)
        elif name != "gradient":
            raise ValueError("Configure the volume's color and opacity in Volume Rendering first")
    array(result, "volume", [[prop.GetScalarOpacityUnitDistance(0),
          prop.GetShade(0), prop.GetAmbient(0), prop.GetDiffuse(0),
          prop.GetSpecular(0), prop.GetSpecularPower(0), prop.GetInterpolationType(),
          prop.GetDisableGradientOpacity(0), prop.GetColorChannels(0)]], 9)
    if prop.GetColorChannels(0) != 3:
        raise ValueError("Use an RGB transfer function in Volume Rendering")
    return result


def export_scene(blocks, destination, fit=True):
    vtk = vtk_module()
    path = Path(destination).expanduser().resolve()
    if path.suffix.lower() != ".vtm":
        raise ValueError("Scene filename must end in .vtm")
    blocks = list(blocks)
    if not blocks:
        raise ValueError("No visible models, segments or rendered scalar volume to send")
    if sum(bool(b.IsA("vtkImageData")) for b in blocks) > 1:
        raise ValueError("This version supports one rendered volume per scene; hide the other volumes")
    if any(b.IsA("vtkImageData") for b in blocks):
        for block in blocks:
            if not block.IsA("vtkPolyData"):
                continue
            material = block.GetFieldData().GetArray("qarvtk.material")
            colors = block.GetPointData().GetScalars() or block.GetCellData().GetScalars()
            if (material.GetComponent(0, 3) < 1 or
                    (colors and colors.GetNumberOfComponents() == 4 and colors.GetRange(3)[0] < 255)):
                raise ValueError("A volume can currently be combined with opaque surfaces only; set model/segment opacity to 100% or stream it separately")
    scene = vtk.vtkMultiBlockDataSet()
    for index, block in enumerate(blocks):
        if not all(math.isfinite(x) for x in block.GetBounds()):
            raise ValueError("Scene has invalid bounds")
        array(block, "version", [2])
        array(block, "fit_size_m", [0.65 if fit else 0.0])
        scene.SetBlock(index, block)
    path.parent.mkdir(parents=True, exist_ok=True)
    generation = path.parent / (path.stem + "-data-" + uuid.uuid4().hex)
    generation.mkdir()
    temporary = path.with_name(path.name + ".tmp")
    try:
        writer = vtk.vtkXMLMultiBlockDataWriter()
        writer.SetFileName(str(generation / "payload.vtm"))
        writer.SetInputData(scene); writer.SetDataModeToBinary()
        if writer.Write() != 1 or writer.GetErrorCode() != 0:
            raise OSError("VTK failed to write the scene")
        root = ET.parse(str(generation / "payload.vtm")).getroot()
        for dataset in root.iter("DataSet"):
            dataset.set("file", generation.name + "/" + dataset.attrib["file"])
        ET.ElementTree(root).write(str(temporary), encoding="utf-8", xml_declaration=True)
        os.replace(temporary, path)
    except Exception:
        shutil.rmtree(generation)
        raise
    finally:
        temporary.unlink(missing_ok=True)
    return str(path)
