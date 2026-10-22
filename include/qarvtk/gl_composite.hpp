#pragma once
#include "qarvtk/renderer.hpp"
namespace qarvtk {
// GPU-only composition of VTK's surface framebuffer and ray-cast image.
class GlComposite {
public:
  GlComposite();
  ~GlComposite();
  GlComposite(const GlComposite&)=delete;
  GlComposite& operator=(const GlComposite&)=delete;
  RenderedEye combine(RenderedEye surface,unsigned volume_color,unsigned volume_depth);
  unsigned framebuffer() const { return fbo_; }
private:
  unsigned program_{},vao_{},fbo_{},color_{},depth_{},width_{},height_{};
};
}
