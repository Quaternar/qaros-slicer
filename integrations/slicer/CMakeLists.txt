cmake_minimum_required(VERSION 3.20)
project(QarosVTK)
find_package(Slicer REQUIRED)
include(${Slicer_USE_FILE})
set(EXTENSION_HOMEPAGE "https://github.com/HonzaHovora/QAROS_VTK")
set(EXTENSION_CATEGORY "QAROS")
set(EXTENSION_CONTRIBUTORS "QuaternAR")
set(EXTENSION_DESCRIPTION "Stream Slicer's live 3D view and slice panels to QAROS using the host renderer.")
set(EXTENSION_ICONURL "")
set(EXTENSION_SCREENSHOTURLS "")
set(EXTENSION_DEPENDS "NA")
slicerMacroBuildScriptedModule(NAME QarosLive SCRIPTS QarosLive.py)
set(library_dir ${CMAKE_BINARY_DIR}/${Slicer_QTSCRIPTEDMODULES_LIB_DIR}/QarosVTKLib)
file(MAKE_DIRECTORY ${library_dir})
file(WRITE ${library_dir}/__init__.py "\"\"\"QAROS support package.\"\"\"\n")
configure_file(qarvtk_host_view.py ${library_dir}/qarvtk_host_view.py COPYONLY)
configure_file(qarvtk_host_panels.py ${library_dir}/qarvtk_host_panels.py COPYONLY)
configure_file(qarvtk_host_gpu.py ${library_dir}/qarvtk_host_gpu.py COPYONLY)
configure_file(qarvtk_host_runtime.py ${library_dir}/qarvtk_host_runtime.py COPYONLY)
configure_file(qarvtk_host_diagnostics.py ${library_dir}/qarvtk_host_diagnostics.py COPYONLY)
configure_file(../qarvtk_runtime.py ${library_dir}/qarvtk_runtime.py COPYONLY)
# Clean an existing build tree so CPack cannot rediscover the retired module.
file(REMOVE
  ${CMAKE_BINARY_DIR}/${Slicer_QTSCRIPTEDMODULES_LIB_DIR}/QarosVTK.py
  ${CMAKE_BINARY_DIR}/${Slicer_QTSCRIPTEDMODULES_LIB_DIR}/QarosVTK.pyc
  ${library_dir}/qarvtk_slicer.py
  ${library_dir}/qarvtk_scene.py
  ${library_dir}/qarvtk_bridge.py)
file(GLOB retired_module_cache
  ${CMAKE_BINARY_DIR}/${Slicer_QTSCRIPTEDMODULES_LIB_DIR}/__pycache__/QarosVTK.*.pyc)
if(retired_module_cache)
  file(REMOVE ${retired_module_cache})
endif()
install(FILES
  ${library_dir}/__init__.py
  ${library_dir}/qarvtk_host_view.py
  ${library_dir}/qarvtk_host_panels.py
  ${library_dir}/qarvtk_host_gpu.py
  ${library_dir}/qarvtk_host_runtime.py
  ${library_dir}/qarvtk_host_diagnostics.py
  ${library_dir}/qarvtk_runtime.py
  DESTINATION ${Slicer_INSTALL_QTSCRIPTEDMODULES_LIB_DIR}/QarosVTKLib
  COMPONENT RuntimeLibraries)
include(${Slicer_EXTENSION_GENERATE_CONFIG})
include(${Slicer_EXTENSION_CPACK})
