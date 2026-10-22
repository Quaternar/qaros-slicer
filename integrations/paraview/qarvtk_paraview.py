"""Run inside ParaView/pvpython. See docs/HOST_INTEGRATIONS.md."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from qarvtk_bridge import export_polydata, vtk_module


def export_active(destination, source=None):
    """Export the active output's surface, including its display transform.

    In client/server mode Fetch gathers this surface to the client: use a
    reduced/extracted selection for large datasets. LUTs, volumes, overlays and
    animation state are not serialized by this surface bridge.
    """
    from paraview import simple, servermanager
    vtk = vtk_module()
    source = source if source is not None else simple.GetActiveSource()
    if source is None:
        raise ValueError("Select a source/filter in ParaView's Pipeline Browser")
    surface = simple.ExtractSurface(Input=source)
    try:
        surface.UpdatePipeline()
        data = servermanager.Fetch(surface)
        if data is None:
            raise ValueError("ParaView returned no surface")
        if data.IsA("vtkCompositeDataSet"):
            flatten = vtk.vtkCompositeDataGeometryFilter()
            flatten.SetInputData(data)
            flatten.Update()
            data = flatten.GetOutput()
        # Reproduce vtkProp3D's display transform without modifying the pipeline.
        actor = vtk.vtkActor()
        view = simple.GetActiveView()
        if view is not None:
            display = simple.GetRepresentation(source, view)
            if display is not None:
                for name in ("Origin", "Position", "Orientation", "Scale"):
                    if hasattr(display, name):
                        getattr(actor, "Set" + name)(*getattr(display, name))
        transform = vtk.vtkTransform()
        transform.SetMatrix(actor.GetMatrix())
        return export_polydata(data, destination, transform)
    finally:
        simple.Delete(surface)
        simple.SetActiveSource(source)
