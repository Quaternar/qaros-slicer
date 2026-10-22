"""SDK placement contract against independent homogeneous transforms; no GPU."""
from pathlib import Path
from itertools import product
import sys
import numpy as np
import vtk

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "package" / "QAROS-VTK-plugins" / "slicer"))
from QarosVTKLib.qarvtk_host_view import HostView


def rotation(q):
    x, y, z, w = np.asarray(q) / np.linalg.norm(q)
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                     [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                     [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])


def sdk_eye(host, position, orientation=(0, 0, 0, 1), translation=(0, 0, 0)):
    # SDK documented volume -> app content conversion, using independent
    # matrix inversion. No HostView camera/conversion helpers are used here.
    r = rotation(host.app_orientation)
    p = r.T @ (np.asarray(position) - host.app_position - np.asarray(translation)) / host.app_scale
    q = [0.]*4
    vtk.vtkMath.Matrix3x3ToQuaternion((r.T @ rotation(orientation)).tolist(), q)
    return dict(position=tuple(p), orientation=(*q[1:], q[0]),
                fov=(-.4, .4, .4, -.4), size=(192, 160))


def run():
    renderer = vtk.vtkRenderer()
    window = vtk.vtkRenderWindow()
    window.AddRenderer(renderer)
    source = vtk.vtkCubeSource()
    source.SetCenter(140, -80, 55)
    source.SetXLength(240)
    source.SetYLength(180)
    source.SetZLength(320)
    mapper = vtk.vtkPolyDataMapper()
    mapper.SetInputConnection(source.GetOutputPort())
    actor = vtk.vtkActor()
    actor.SetMapper(mapper)
    renderer.AddActor(actor)
    view = type("View", (), {"renderWindow": lambda self: window})()
    camera = renderer.GetActiveCamera()
    center = np.array([140., -80., 55.])
    corners = np.asarray([center + np.array(signs)*[120., 90., 160.]
                          for signs in product((-1, 1), repeat=3)])
    # Fixed data axes: R -> +X, A -> -Z, S -> +Y, independent of desktop pose.
    basis = np.array([[1., 0., 0.], [0., 0., -1.], [0., 1., 0.]])
    expected_orientation = None
    for back in ([0, 1, 0], [0, -1, 0], [.7, 1, .4]):
        back = np.asarray(back, dtype=float) / np.linalg.norm(back)
        right = np.cross(-back, [0, 0, 1]); right /= np.linalg.norm(right)
        up = np.cross(right, -back)
        camera.SetPosition(center + back*1000)
        camera.SetFocalPoint(center)
        camera.SetViewUp(up)
        host = HostView(view)
        assert np.allclose(rotation(host.app_orientation), basis.T)
        if expected_orientation is None:
            expected_orientation = host.app_orientation
        assert host.app_orientation == expected_orientation, "Desktop camera changed the streamed orientation at Start"
        # Independent corner transform verifies stable physical bounds even
        # when a new stream starts from an oblique or opposite desktop view.
        extents = np.ptp(corners @ basis, axis=0)
        assert host.app_scale == .001
        assert np.allclose(host.workspace_size, extents * host.app_scale)
        placed = corners @ rotation(host.app_orientation).T * host.app_scale + host.app_position
        assert np.allclose(placed.min(axis=0), -np.asarray(host.workspace_size)/2)
        assert np.allclose(placed.max(axis=0), np.asarray(host.workspace_size)/2)
        assert np.allclose(host.workspace_position, [0., 0., -1.5])
        assert np.isclose(max(host.workspace_size), extents.max() * .001)

        physical = HostView(view, fit=False)
        assert physical.app_scale == .001
        assert np.allclose(physical.workspace_size, extents*.001)
        physical_app = corners @ rotation(physical.app_orientation).T*.001 + physical.app_position
        physical_room = physical_app + physical.workspace_position
        assert np.allclose(physical_room, corners @ basis*.001 + [0., 0., -1.5]), "Physical placement changed"
        assert np.allclose((physical_app.min(axis=0)+physical_app.max(axis=0))/2, 0.), "Workspace misses its content"
        assert np.isclose(np.linalg.det(rotation(host.app_orientation)), 1), "Placement mirrors the scene"
        # A desktop orbit during streaming cannot change placement or XR eyes.
        before = (host.app_orientation, host.app_position, host.workspace_size)
        camera.Azimuth(37.)
        camera.Elevation(21.)
        camera.Roll(19.)
        assert (host.app_orientation, host.app_position, host.workspace_size) == before
        restarted = HostView(view)
        assert (restarted.app_orientation, restarted.app_position, restarted.workspace_size) == before
        for orientation in ((0, 0, 0, 1), (.1, .2, -.05, .97)):
            for x in (-.032, .032):
                pose = np.array([x, .05, 1.2])
                eye = sdk_eye(host, pose, orientation)
                host._camera_for_eye(eye, *host.clip)
                assert np.allclose(camera.GetPosition(), center + basis @ pose / host.app_scale)
                assert np.allclose(camera.GetPosition(), eye["position"]), "Eye pose was transformed again in the renderer"
                assert np.allclose(camera.GetDirectionOfProjection(), basis @ rotation(orientation) @ [0, 0, -1])
                assert np.allclose(camera.GetViewUp(), basis @ rotation(orientation) @ [0, 1, 0])
        def screen(point, translation):
            host._camera_for_eye(sdk_eye(host, (0, 0, 1.2), translation=translation), *host.clip)
            matrix = camera.GetCompositeProjectionTransformMatrix(1., -1., 1.)
            result = matrix.MultiplyPoint((*point, 1.))
            return np.asarray(result[:3]) / result[3]
        initial = screen(center, (0, 0, 0))
        assert screen(center, (.1, 0, 0))[0] > initial[0] + .1, "+app_pose X moves content left"
        marker = center + basis[:, 0]*40
        assert screen(marker, (0, 0, .1))[0] > screen(marker, (0, 0, 0))[0], "+app_pose Z moves content away"
        assert actor.GetPosition() == (0., 0., 0.), "Placement changed the host actor"
    # Every dataset retains its physical dimensions; magnification is explicit.
    for factor in (.125, .5, 1., 2., 8.):
        scaled = HostView(view, size_multiplier=factor)
        assert np.allclose(scaled.workspace_size, np.array([.24, .32, .18]) * factor)
        assert np.isclose(scaled.app_scale, .001 * factor)
        placed_center = rotation(scaled.app_orientation) @ center * scaled.app_scale + scaled.app_position
        assert np.allclose(placed_center, 0.), "Scaling must stay centered"
        assert np.allclose(scaled.clip, [.05 / scaled.app_scale, 20 / scaled.app_scale])
    for factor in (0., -.5, float("nan"), float("inf"), .01, 9.):
        try:
            HostView(view, size_multiplier=factor)
            raise AssertionError("Invalid multiplier accepted")
        except ValueError:
            pass
    # Actual data transforms are preserved, unlike desktop camera rotation.
    actor.RotateY(30.)
    transformed = HostView(view)
    assert transformed.app_orientation == expected_orientation
    assert np.allclose(transformed.scene_bounds, actor.GetBounds())
    assert not np.allclose(transformed.workspace_size, [.24, .32, .18])
    assert np.isclose(actor.GetOrientation()[1], 30.), "Streaming changed the data transform"
    actor.SetOrientation(0., 0., 0.)
    # Distant native coordinates must not inflate a centred workspace. A
    # hidden prop must not influence the working bounds either.
    actor.SetPosition(10000., -20000., 30000.)
    distant = HostView(view, fit=False)
    assert np.allclose(distant.workspace_size, [.24, .32, .18])
    stray = vtk.vtkActor()
    stray.SetMapper(mapper)
    stray.SetPosition(-90000., 80000., 70000.)
    stray.VisibilityOff()
    renderer.AddActor(stray)
    assert np.allclose(HostView(view).workspace_size, [.24, .32, .18])
    stray.VisibilityOn()
    stray.UseBoundsOff()
    assert np.allclose(HostView(view).workspace_size, [.24, .32, .18])
    renderer.RemoveActor(stray)
    source.SetZLength(0.)
    flat = HostView(view)
    assert min(flat.workspace_size) == .001, "Flat scenes need a positive workspace thickness"
    print("PASS: scene-derived XYZ workspace; 1:1 default, multiplicative size and room position; oblique/distant/hidden/flat bounds; "
          "fixed RAS orientation across desktop orbits and restarts, live data transforms, SDK stereo/head poses")


if __name__ == "__main__":
    run()
