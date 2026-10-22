#pragma once
#include "qarvtk/math.hpp"
#include <memory>
#include <string>
struct GLFWwindow;
class vtkPolyData;
namespace qarvtk {
struct RenderedEye { unsigned color{},depth{},width{},height{}; };
// Standalone context owner. The second context is reserved for the SDK graphics
// thread. It shares textures, never a current context, with VTK's main thread.
class Renderer {
public:
  explicit Renderer(bool visible=false);
  ~Renderer();
  Renderer(const Renderer&)=delete;
  Renderer& operator=(const Renderer&)=delete;
  void set_polydata(vtkPolyData* data, double meters_per_unit=1.0, bool ras=false);
  void load_vtp(const std::string& path, double meters_per_unit=1.0, bool ras=false);
  void load_scene(const std::string& path, double meters_per_unit=1.0, bool ras=false);
  RenderedEye render(Pose pose,Fov fov,Clip clip,unsigned width,unsigned height);
  void present();
  void poll();
  bool closing() const;
  void make_current();
  GLFWwindow* main_window() const;
  GLFWwindow* sdk_window() const;
  std::string capabilities() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
