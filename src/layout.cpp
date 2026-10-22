#include "qarvtk/layout.hpp"
#include <algorithm>
#include <stdexcept>
namespace qarvtk {
unsigned width(const View& v) { return std::max(v.x0,v.x1)-std::min(v.x0,v.x1); }
unsigned height(const View& v) { return std::max(v.y0,v.y1)-std::min(v.y0,v.y1); }
std::array<EyeViews,2> validate_stereo(const Frame& f) {
  if (f.views.size()!=4 || f.textures.empty()) throw std::invalid_argument("Require stereo color+depth (four views)");
  std::array<EyeViews,2> result{};
  bool seen[2][2]{};
  for (std::size_t i=0;i<f.views.size();++i) {
    const auto& v=f.views[i];
    if (v.texture>=f.textures.size()) throw std::invalid_argument("View references a missing texture");
    const auto& t=f.textures[v.texture];
    if (!t.handle || !t.width || !t.height || t.width>16384 || t.height>16384)
      throw std::invalid_argument("Invalid texture dimensions/handle");
    if (t.layers!=1 || v.layer!=0) throw std::invalid_argument("Only 2D textures are supported; negotiate separated textures");
    if (!width(v)||!height(v)||std::max(v.x0,v.x1)>t.width||std::max(v.y0,v.y1)>t.height)
      throw std::invalid_argument("View rectangle exceeds texture");
    const int e=v.eye==Eye::left?0:1, p=v.plane==Plane::color?0:1;
    if (seen[e][p]) throw std::invalid_argument("Duplicate eye/plane");
    seen[e][p]=true;
    (p==0?result[e].color:result[e].depth)=i;
  }
  for (auto& eye:seen) for(bool plane:eye) if(!plane) throw std::invalid_argument("Missing eye/plane");
  // Overlapping views in one texture would overwrite another view.
  for(std::size_t i=0;i<f.views.size();++i) for(std::size_t j=i+1;j<f.views.size();++j) {
    const auto&a=f.views[i]; const auto&b=f.views[j];
    if(f.textures[a.texture].handle==f.textures[b.texture].handle &&
       std::max(std::min(a.x0,a.x1),std::min(b.x0,b.x1))<std::min(std::max(a.x0,a.x1),std::max(b.x0,b.x1)) &&
       std::max(std::min(a.y0,a.y1),std::min(b.y0,b.y1))<std::min(std::max(a.y0,a.y1),std::max(b.y0,b.y1)))
      throw std::invalid_argument("Overlapping frame views");
  }
  return result;
}
}
