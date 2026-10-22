#pragma once
#include "qar_streaming.h"
#include "qarvtk/host_bridge.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace qarvtk {
inline void apply_host_placement(QarAppVolumeInit& app,const QarvtkHostPlacement& p){
  double norm=0;
  for(double v:p.orientation){if(!std::isfinite(v))throw std::invalid_argument("Invalid app orientation");norm+=v*v;}
  norm=std::sqrt(norm);
  if(norm<1e-12||!std::isfinite(norm)||!std::isfinite(p.app_scale)||p.app_scale<1e-6||p.app_scale>1e6)
    throw std::invalid_argument("Invalid app orientation or scale");
  for(double v:p.position)if(!std::isfinite(v)||std::abs(v)>1e12)throw std::invalid_argument("Invalid app position");
  for(double v:p.workspace_position)if(!std::isfinite(v)||std::abs(v)>1e12)
    throw std::invalid_argument("Invalid workspace position");
  for(double v:p.workspace_size)if(!std::isfinite(v)||v<=0||v>1e12)
    throw std::invalid_argument("Invalid workspace size");
  app.app_pose.position={static_cast<float>(p.position[0]),static_cast<float>(p.position[1]),static_cast<float>(p.position[2])};
  app.app_pose.orientation={static_cast<float>(p.orientation[0]/norm),static_cast<float>(p.orientation[1]/norm),
    static_cast<float>(p.orientation[2]/norm),static_cast<float>(p.orientation[3]/norm)};
  app.app_scale=static_cast<float>(p.app_scale);
  app.pose.position={static_cast<float>(p.workspace_position[0]),static_cast<float>(p.workspace_position[1]),
    static_cast<float>(p.workspace_position[2])};
  // The SDK struct is X/Z/Y (width/length/height), unlike our XYZ ABI arrays.
  app.size.width_meters=static_cast<float>(p.workspace_size[0]);
  app.size.height_meters=static_cast<float>(p.workspace_size[1]);
  app.size.length_meters=static_cast<float>(p.workspace_size[2]);
}

// Rebuild as separate planes. Preserve per-plane flips, formats and eye order;
// size each depth plane from its COLOR eye, never repeatedly halve old depth.
inline QarVideoFrameLayout half_depth_layout(const QarVideoFrameLayout& input){
  if(input.views_count!=4)throw std::invalid_argument("Expected stereo color and depth layout");
  auto out=qar_video_frame_layout_default();out.views_count=out.textures_count=4;
  bool seen[2][2]{};
  auto span=[](uint32_t a,uint32_t b){return std::max(a,b)-std::min(a,b);};
  for(size_t i=0;i<4;++i){
    const auto& old=input.views[i];
    const bool depth=old.data_type==QAR_VIDEO_FRAME_VIEW_TYPE_DEPTH;
    if((!depth&&old.data_type!=QAR_VIDEO_FRAME_VIEW_TYPE_COLOR)||
       (old.eye!=QAR_VIDEO_FRAME_VIEW_EYE_LEFT&&old.eye!=QAR_VIDEO_FRAME_VIEW_EYE_RIGHT))
      throw std::invalid_argument("Invalid eye or plane in SDK layout");
    auto& duplicate=seen[old.eye==QAR_VIDEO_FRAME_VIEW_EYE_RIGHT][depth];
    if(duplicate)throw std::invalid_argument("Duplicate SDK eye plane");
    duplicate=true;
    const QarVideoFrameView* color=nullptr;
    for(size_t j=0;j<4;++j)if(input.views[j].eye==old.eye&&input.views[j].data_type==QAR_VIDEO_FRAME_VIEW_TYPE_COLOR)color=&input.views[j];
    if(!color)throw std::invalid_argument("Missing SDK color eye");
    auto w=span(color->start_x,color->end_x),h=span(color->start_y,color->end_y);
    if(!w||!h||w>8192||h>8192)throw std::invalid_argument("Invalid SDK eye size");
    if(depth){w=std::max(1u,(w+1)/2);h=std::max(1u,(h+1)/2);}
    auto& v=out.views[i];v=old;v.texture_index=static_cast<uint32_t>(i);v.array_layer_index=0;
    v.start_x=old.start_x>old.end_x?w:0;v.end_x=old.start_x>old.end_x?0:w;
    v.start_y=old.start_y>old.end_y?h:0;v.end_y=old.start_y>old.end_y?0:h;
    out.textures[i]={old.texture_format,w,h,1};
  }
  return out;
}
}
