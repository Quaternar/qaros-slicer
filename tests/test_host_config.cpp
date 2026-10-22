#if !defined(_WIN32) && defined(__GNUC__)
#define __declspec(annotation)
#endif
#include "../apps/host_config.hpp"
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <limits>
int main(){
  QarvtkHostPlacement p{{.1,.2,-.3},{0,2,0,0},2.5,{.7,1.1,.3},{1.,2.,-3.}};
  auto app=qar_app_volume_init_default();qarvtk::apply_host_placement(app,p);
  assert(app.app_scale==2.5F&&app.app_pose.position.x==.1F&&app.app_pose.position.z==-.3F);
  assert(app.app_pose.orientation.y==1&&app.app_pose.orientation.w==0);
  assert(app.size.width_meters==.7F&&app.size.height_meters==1.1F&&app.size.length_meters==.3F);
  assert(app.pose.position.x==1.F&&app.pose.position.y==2.F&&app.pose.position.z==-3.F);
  auto input=qar_video_frame_layout_default();input.views_count=4;
  input.views[0]={1920,0,0,1080,0,0,QAR_PIXEL_FORMAT_D32_FLOAT,QAR_VIDEO_FRAME_VIEW_TYPE_DEPTH,QAR_VIDEO_FRAME_VIEW_EYE_RIGHT};
  input.views[1]={0,0,1920,1080,1,0,QAR_PIXEL_FORMAT_R8G8B8A8,QAR_VIDEO_FRAME_VIEW_TYPE_COLOR,QAR_VIDEO_FRAME_VIEW_EYE_LEFT};
  input.views[2]={0,601,801,0,2,0,QAR_PIXEL_FORMAT_R8G8B8A8,QAR_VIDEO_FRAME_VIEW_TYPE_COLOR,QAR_VIDEO_FRAME_VIEW_EYE_RIGHT};
  input.views[3]={1920,1080,0,0,3,0,QAR_PIXEL_FORMAT_R32_FLOAT,QAR_VIDEO_FRAME_VIEW_TYPE_DEPTH,QAR_VIDEO_FRAME_VIEW_EYE_LEFT};
  auto out=qarvtk::half_depth_layout(input);
  assert(out.textures_count==4&&out.textures[0].width==401&&out.textures[0].height==301);
  assert(out.views[0].start_x==401&&out.views[0].end_x==0);
  assert(out.textures[1].width==1920&&out.textures[1].height==1080);
  assert(out.textures[2].width==801&&out.textures[2].height==601&&out.views[2].start_y==601);
  assert(out.textures[3].width==960&&out.textures[3].height==540);
  out=qarvtk::half_depth_layout(out);assert(out.textures[3].width==960);
  bool rejected=false;p.app_scale=0;
  try{qarvtk::apply_host_placement(app,p);}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
  p.app_scale=1.;
  for(double invalid:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){
    p.workspace_size[1]=invalid;rejected=false;
    try{qarvtk::apply_host_placement(app,p);}catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
  }
  p.workspace_size[1]=1.1;p.workspace_position[0]=std::numeric_limits<double>::quiet_NaN();rejected=false;
  try{qarvtk::apply_host_placement(app,p);}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
  std::cout<<"PASS: SDK app pose/scale; XYZ workspace -> SDK width/length/height and room centre; "
    "invalid sizes rejected; half-depth layout, odd sizes, eye order and flips\n";
}
