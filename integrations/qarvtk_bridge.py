"""Surface snapshot interchange. Import with the host application's own VTK."""
import math
import os
from pathlib import Path
import uuid


def vtk_module():
    try:
        import vtk
    except ImportError:
        import vtkmodules.all as vtk
    return vtk


def export_polydata(data, destination, transform=None):
    """Bake an optional world transform, triangulate, and atomically replace VTP.

    Coordinates/units are preserved. Sender applies --ras / --meters-per-unit.
    No process is launched, no network data is sent, and the input is not changed.
    """
    vtk = vtk_module()
    if data is None or not data.IsA("vtkPolyData") or data.GetNumberOfPoints() == 0:
        raise ValueError("Expected a non-empty vtkPolyData surface")
    path = Path(destination).expanduser().resolve()
    if path.suffix.lower() != ".vtp":
        raise ValueError("Output filename must end in .vtp")
    polydata = data
    if transform is not None:
        bake = vtk.vtkTransformPolyDataFilter()
        bake.SetInputData(data)
        bake.SetTransform(transform)
        bake.Update()
        polydata = bake.GetOutput()
    triangles = vtk.vtkTriangleFilter()
    triangles.SetInputData(polydata)
    triangles.PassVertsOff()
    triangles.PassLinesOff()
    triangles.Update()
    output = triangles.GetOutput()
    if output.GetNumberOfPolys() == 0:
        raise ValueError("No surface polygons to export (points/lines are not supported)")
    if not all(math.isfinite(x) for x in output.GetBounds()):
        raise ValueError("Surface has non-finite bounds")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        writer = vtk.vtkXMLPolyDataWriter()
        writer.SetFileName(str(temporary))
        writer.SetInputData(output)
        writer.SetDataModeToBinary()
        if writer.Write() != 1 or writer.GetErrorCode() != 0:
            raise OSError("VTK failed to write the surface")
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)
    return str(path)
