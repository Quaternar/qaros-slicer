"""Load with Tools > Manage Plugins > Load New (local, built-in connection only)."""
from pathlib import Path
import sys
import weakref

_folder = Path(__file__).resolve().parent
sys.path.insert(0, str(_folder if (_folder / "qarvtk_runtime.py").exists() else _folder.parent))
from qarvtk_bridge import export_polydata
from qarvtk_runtime import Backend, user_directory
from paraview.util.vtkAlgorithm import VTKPythonAlgorithmBase, smproxy, smproperty, smdomain
from paraview.modules.vtkRemotingCore import vtkProcessModule
from vtkmodules.vtkCommonDataModel import vtkDataObject, vtkPolyData
from vtkmodules.vtkFiltersGeometry import vtkGeometryFilter, vtkCompositeDataGeometryFilter


@smproxy.filter(name="QarosStreaming", label="QAROS Streaming")
@smproperty.input(name="Input")
@smdomain.datatype(dataTypes=["vtkDataSet", "vtkCompositeDataSet"], composite_data_supported=True)
class QarosStreaming(VTKPythonAlgorithmBase):
    """Surface pipeline plugin controlling the shared, out-of-process sender.

    Use a Transform filter; display-only transforms and LUTs are not transferred.
    """
    def __init__(self):
        super().__init__(nInputPorts=1, nOutputPorts=1,
                         inputType="vtkDataObject", outputType="vtkPolyData")
        self._backend = Backend()
        self._settings = {"exe": "", "sdk": "", "scale": 1.0, "identity": "", "hub": "",
                          "invite": "", "target": "visualizer"}
        self._error = ""
        self._cleanup = weakref.finalize(self, self._backend.close)

    def _local_only(self):
        pm = vtkProcessModule.GetProcessModule()
        session = pm.GetActiveSession() if pm else None
        if (pm is None or pm.GetProcessType() != vtkProcessModule.PROCESS_CLIENT
                or pm.GetNumberOfLocalPartitions() != 1
                or session is None or session.IsA("vtkSMSessionClient")):
            raise RuntimeError("QAROS Streaming requires a local built-in connection (no pvserver/MPI).")

    def _set(self, key, value):
        if key != "scale":
            value = "" if value is None else str(value)
        if self._backend.running:
            raise RuntimeError("Stop the sender before changing connection settings")
        self._settings[key] = value
        self.Modified()

    @smproperty.stringvector(name="SenderExecutable", default_values="")
    @smdomain.filelist()
    def SetSenderExecutable(self, value):
        self._set("exe", value)

    @smproperty.stringvector(name="SDKFolder", default_values="")
    def SetSDKFolder(self, value):
        self._set("sdk", value)

    @smproperty.doublevector(name="MetresPerUnit", default_values=1.0)
    @smdomain.doublerange(min=0.000000001)
    def SetMetresPerUnit(self, value):
        self._set("scale", value)

    @smproperty.stringvector(name="IdentityFile", default_values="")
    def SetIdentityFile(self, value):
        self._set("identity", value)

    @smproperty.stringvector(name="HubHost", default_values="")
    def SetHubHost(self, value):
        self._set("hub", value)

    @smproperty.stringvector(name="LegacyInviteFile", default_values="")
    @smdomain.filelist()
    def SetLegacyInviteFile(self, value):
        self._set("invite", value)

    @smproperty.stringvector(name="LegacyTarget", default_values="visualizer")
    def SetLegacyTarget(self, value):
        self._set("target", value)

    @smproperty.xml('''<Property name="StartStream" label="Start streaming"
        command="StartStream" panel_widget="command_button"/>''')
    def StartStream(self):
        try:
            self._local_only()
            self.Update()
            s = self._settings
            # Reuse pairing from Slicer/the launcher; no codes in saved PV states.
            self._backend.start(s["exe"], s["sdk"], scale=s["scale"], hub=s["hub"],
                identity=s["identity"] or user_directory() / "onboarding-id.txt",
                invite=s["invite"], target=s["target"])
            self._error = ""
        except Exception as error:
            self._error = str(error)
        print(self.GetStatus(), flush=True)

    @smproperty.xml('''<Property name="StopStream" label="Stop streaming"
        command="StopStream" panel_widget="command_button"/>''')
    def StopStream(self):
        self._backend.close()
        self.Modified()
        print(self.GetStatus(), flush=True)

    @smproperty.xml('''<Property name="ShowLog" label="Show sender log"
        command="ShowLog" panel_widget="command_button"/>''')
    def ShowLog(self):
        print(self.GetStatus() + "\n" + self._backend.log_tail(), flush=True)

    @smproperty.stringvector(name="SenderStatus", information_only="1")
    def GetStatus(self):
        return self._error or self._backend.status()

    def RequestData(self, request, in_info, out_info):
        self._local_only()
        data = vtkDataObject.GetData(in_info[0])
        geometry = vtkCompositeDataGeometryFilter() if data.IsA("vtkCompositeDataSet") else vtkGeometryFilter()
        geometry.SetInputData(data)
        geometry.Update()
        surface = geometry.GetOutput()
        export_polydata(surface, self._backend.scene)
        vtkPolyData.GetData(out_info).ShallowCopy(surface)
        self._error = ""
        return 1
