#pragma once
#include "qarvtk/layout.hpp"
#include "qarvtk/renderer.hpp"
namespace qarvtk {
// Source pixels in the texture's bottom-left coordinate system.
struct ColorRegion { unsigned x{}, y{}, width{}, height{}; };
// Copy VTK's bottom-left raster into QAROS's top-left raster for both planes,
// composing with the SDK view flips. No pixel readback, no PBO/CPU fallback.
// Destruction requires the render context to be current.
class GlCopy {
public:
  GlCopy();~GlCopy();
  GlCopy(const GlCopy&)=delete;GlCopy& operator=(const GlCopy&)=delete;
  void copy(const RenderedEye& source,const Frame& target,const EyeViews& views,bool report=false);
  // Host rendering delivers the complete color image before a supplementary
  // volume depth pass; the two planes must therefore be copied separately.
  void copy_plane(const RenderedEye& source,const Frame& target,const EyeViews& views,bool depth);
  // Mono panel/staging copy. No stereo or depth required. Fit letterboxes;
  // source_top_left prevents a second flip when uploading a staged frame.
  void copy_color(const RenderedEye& source,const Frame& target,bool source_top_left=false,bool fit=false,
                  const ColorRegion* region=nullptr);
  void finish(); // Conservative cross-context completion before SDK show_frame.
private:
  unsigned program_{},vao_{},fbo_{};
  void plane(unsigned source,const Texture& texture,const View& view,bool depth,bool report,bool source_top_left=false,
             const ColorRegion* region=nullptr);
};
}
