"""Load the distributable through ParaView's real plugin loader and invoke its controls."""
import os
from pathlib import Path
import sys
import tempfile
import time
from paraview import simple, servermanager
from vtkmodules.vtkIOXML import vtkXMLPolyDataReader

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fixture_support import make_sdk

plugin = Path(__file__).resolve().parents[1] / "package" / "QAROS-VTK-plugins" / "paraview" / "QarosVTKPlugin.py"
simple.LoadPlugin(str(plugin), remote=False, ns=globals())
with tempfile.TemporaryDirectory() as directory:
    # Isolate all plugin-created data; this is not a repository import test.
    previous = tempfile.tempdir
    tempfile.tempdir = directory
    source = simple.Box(XLength=2, YLength=4, ZLength=6)
    proxy = QAROSStreaming(Input=source)
    try:
        proxy.UpdatePipeline()
        assert servermanager.Fetch(proxy).GetBounds() == (-1, 1, -2, 2, -3, 3)
        private = list(Path(directory).glob("qarvtk-*"))
        assert len(private) == 1, private
        scene = private[0] / "scene.vtp"
        assert scene.is_file()
        sdk = make_sdk(directory)
        identity = Path(directory) / "identity.txt"
        identity.write_text("fixture-id\n")
        proxy.SenderExecutable = os.environ["QARVTK_TEST_BACKEND"]
        proxy.SDKFolder = str(sdk)
        proxy.IdentityFile = str(identity)
        proxy.UpdatePipeline()
        proxy.InvokeCommand("StartStream")
        deadline = time.monotonic() + 5
        log = private[0] / "sender.log"
        while time.monotonic() < deadline:
            if log.exists() and "FIXTURE_READY" in log.read_text():
                break
            time.sleep(0.02)
        else:
            proxy.InvokeCommand("ShowLog")
            raise AssertionError("Plugin Start did not launch the fixture")
        source.XLength = 4
        proxy.UpdatePipeline()
        reader = vtkXMLPolyDataReader()
        reader.SetFileName(str(scene)); reader.Update()
        assert reader.GetOutput().GetBounds() == (-2, 2, -2, 2, -3, 3)
        proxy.InvokeCommand("StopStream")
        proxy.UpdatePropertyInformation()
        status = proxy.GetProperty("SenderStatus").GetData()
        assert "exited with code 0" in str(status), status
        assert not scene.exists()
        assert "FIXTURE_STOPPED" in log.read_text()
        # Normal Start after Stop republishes the surface without a manual export.
        proxy.InvokeCommand("StartStream")
        assert scene.exists()
        proxy.InvokeCommand("StopStream")
    finally:
        proxy.InvokeCommand("StopStream")
        simple.Delete(proxy)
        simple.Delete(source)
        tempfile.tempdir = previous
print("PASS: packaged ParaView plugin loader, pipeline update, Start/Stop and restart with process fixture")
