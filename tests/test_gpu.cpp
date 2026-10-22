#include "qarvtk/gl.hpp"
#include "qarvtk/gl_copy.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace qarvtk;
void check(bool good,const char*message){if(!good)throw std::runtime_error(message);}
struct OwnedTextures {
  std::array<unsigned,4> ids{};
  ~OwnedTextures(){glDeleteTextures(4,ids.data());}
};
int main(){try{
  Renderer renderer;std::cout<<renderer.capabilities()<<'\n';
  GlCopy copy;
  for(GLenum target:{GL_TEXTURE_2D,GL_TEXTURE_2D_ARRAY})
  for(bool floatColorDepth:{false,true})for(auto size:{std::array<unsigned,2>{128,128},{1920,1080}}){
    const auto width=size[0],height=size[1];
    OwnedTextures owned;
    // Allocate in the SDK context, then render/copy on the VTK context.
    glfwMakeContextCurrent(renderer.sdk_window());glGenTextures(4,owned.ids.data());
    for(unsigned i=0;i<4;++i){glBindTexture(target,owned.ids[i]);
      glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      const bool depth=i%2==1;
      const auto internal=depth?(floatColorDepth?GL_R32F:GL_DEPTH_COMPONENT32F):GL_RGBA8;
      const auto format=depth?(floatColorDepth?GL_RED:GL_DEPTH_COMPONENT):GL_RGBA;
      if(target==GL_TEXTURE_2D_ARRAY)
        glTexImage3D(target,0,internal,width,height,1,0,format,depth?GL_FLOAT:GL_UNSIGNED_BYTE,nullptr);
      else glTexImage2D(target,0,internal,width,height,0,format,depth?GL_FLOAT:GL_UNSIGNED_BYTE,nullptr);
    }
    glFinish();renderer.make_current();
    Frame frame;
    for(auto id:owned.ids)frame.textures.push_back({id,width,height,1});
    // Deliberately reorder SDK view metadata and flip only the right image.
    frame.views={{Eye::right,Plane::depth,3,width,height,0,0,0},{Eye::left,Plane::color,0,0,0,width,height,0},
                 {Eye::right,Plane::color,2,width,height,0,0,0},{Eye::left,Plane::depth,1,0,0,width,height,0}};
    auto views=validate_stereo(frame);Clip clip{.1,10};Fov fov{-.6,.6,.6,-.6};
    for(int eye=0;eye<2;++eye){Pose p;p.position.z=2;p.position.x=eye==0?-.032:.032;p.position.y=.12;
      auto s=renderer.render(p,fov,clip,width,height);
      // The copy must survive and restore a non-default host GL state.
      glEnable(GL_BLEND);glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);
      glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_FALSE);glDepthMask(GL_FALSE);
      GLint oldProgram=0;glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
      glActiveTexture(GL_TEXTURE0);
      // Leave a same-sized 2D texture bound: the old unchecked bind could query
      // this stale object after trying to bind a one-layer array as 2D.
      glBindTexture(GL_TEXTURE_2D,s.color);
      if(target==GL_TEXTURE_2D_ARRAY)glBindTexture(target,owned.ids[2]);
      GLint oldArray=0;glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&oldArray);
      glActiveTexture(GL_TEXTURE3);
      if(eye==0){
        // The live host copies its normal color before its volume-depth pass.
        // A depth-only copy must not touch that already submitted color plane.
        copy.copy_plane(s,frame,views[eye],false);
        auto depthOnly=s;depthOnly.color=0;
        copy.copy_plane(depthOnly,frame,views[eye],true);
      }else copy.copy(s,frame,views[eye]);
      check(glIsEnabled(GL_BLEND)&&glIsEnabled(GL_SCISSOR_TEST),"GL capabilities not restored");
      GLboolean mask[4]{};glGetBooleanv(GL_COLOR_WRITEMASK,mask);check(!mask[0]&&mask[1]&&!mask[2]&&!mask[3],"write mask not restored");
      GLint newProgram=0;glGetIntegerv(GL_CURRENT_PROGRAM,&newProgram);check(oldProgram==newProgram,"program not restored");
      GLint active=0,texture=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);check(active==GL_TEXTURE3,"active texture not restored");
      glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
      check(texture==static_cast<GLint>(s.color),"2D texture binding not restored");
      glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY,&texture);check(texture==oldArray,"array texture binding not restored");
      glBindTexture(GL_TEXTURE_2D_ARRAY,0);glBindTexture(GL_TEXTURE_2D,0);
      glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);
    }
    copy.finish();glfwMakeContextCurrent(renderer.sdk_window());
    // Readback is exclusively a test oracle, never part of the sender path.
    std::array<double,2> centroids{};
    for(int eye=0;eye<2;++eye){
      std::vector<unsigned char> rgba(width*height*4);std::vector<float> depth(width*height);
      glBindTexture(target,owned.ids[eye*2]);glGetTexImage(target,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
      glBindTexture(target,owned.ids[eye*2+1]);glGetTexImage(target,0,floatColorDepth?GL_RED:GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
      double xsum=0,ysum=0;unsigned count=0;
      for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const auto i=y*width+x;
        check(std::isfinite(depth[i])&&depth[i]>=0&&depth[i]<=1,"invalid depth range");
        if(rgba[i*4+3]>128){check(rgba[i*4+1]>30 && rgba[i*4+1]>rgba[i*4],"cube color missing");xsum+=x+.5;ysum+=y+.5;++count;
          check(std::abs(depth[i]-depth_from_distance(1.8,clip))<.005,"front depth differs from analytic projection");}
      }
      check(count>100,"no visible cube pixels");centroids[eye]=xsum/count;
      const double streamY=eye==0?ysum/count:height-ysum/count;
      check(streamY>height*.53,"camera above cube must place it below centre in the top-down stream");
      check(depth.front()>.999F&&rgba[3]==0,"background depth or transparency corrupted");
    }
    // Undo the deliberate horizontal flip before comparing stereo disparity.
    check(centroids[0] > width-centroids[1]+1,"stereo eye order or horizontal flip wrong");
    renderer.make_current();check(glGetError()==GL_NO_ERROR,"GPU test left an OpenGL error");
    std::cout<<"PASS: "<<width<<'x'<<height<<" px, "<<(target==GL_TEXTURE_2D_ARRAY?"2D array":"2D")
      <<", "<<(floatColorDepth?"R32F":"D32F")<<", shared contexts, stereo, depth, alpha, flips and GL state\n";
  }
  // Mono slice -> bounded staging -> SDK image. Preserve orientation through
  // both copies, letterbox a resized view, and honour destination view flips.
  for(GLenum target:{GL_TEXTURE_2D,GL_TEXTURE_2D_ARRAY})for(bool flipped:{false,true}){
    renderer.make_current();OwnedTextures owned;glGenTextures(4,owned.ids.data());
    std::vector<unsigned char> pattern(8*4*4);
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x){const auto i=(y*8+x)*4;
      pattern[i]=y<2?255:0;pattern[i+1]=x<4?51:203;pattern[i+2]=y<2?0:255;pattern[i+3]=255;}
    for(int i=0;i<3;++i){const auto type=i==2?target:GL_TEXTURE_2D;
      glBindTexture(type,owned.ids[i]);glTexParameteri(type,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(type,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      if(type==GL_TEXTURE_2D_ARRAY)glTexImage3D(type,0,GL_RGBA8,8,8,1,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
      else glTexImage2D(type,0,GL_RGBA8,8,i==0?4:8,0,GL_RGBA,GL_UNSIGNED_BYTE,i==0?pattern.data():nullptr);
    }
    Frame stage{{{owned.ids[1],8,8,1}},{{Eye::left,Plane::color,0,0,0,8,8,0}}};
    const unsigned first=flipped?8:0,last=flipped?0:8;
    Frame output{{{owned.ids[2],8,8,1}},{{Eye::left,Plane::color,0,first,first,last,last,0}}};
    glClearColor(.1F,.2F,.3F,.4F);glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);
    copy.copy_color({owned.ids[0],0,8,4},stage,false,true);
    copy.copy_color({owned.ids[1],0,8,8},output,true);copy.finish();
    GLfloat clear[4]{};glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);
    check(std::abs(clear[0]-.1F)<1e-6&&glIsEnabled(GL_SCISSOR_TEST),"Panel copy changed host GL state");
    glDisable(GL_SCISSOR_TEST);
    std::vector<unsigned char> result(8*8*4);
    glBindTexture(target,owned.ids[2]);glGetTexImage(target,0,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
      const auto expected_x=flipped?7-x:x,expected_y=flipped?7-y:y;
      const auto i=(y*8+x)*4;
      if(expected_y<2||expected_y>=6)check(result[i]==0&&result[i+1]==0&&result[i+2]==0&&result[i+3]==255,"Panel padding is not black");
      else {const auto src=((5-expected_y)*8+expected_x)*4;
        for(unsigned channel=0;channel<4;++channel)check(result[i+channel]==pattern[src+channel],"Panel mono orientation/flip changed");}
    }
    check(glGetError()==GL_NO_ERROR,"Panel GPU copy left an OpenGL error");
  }
  // Crop real source pixels, including black image content. Pixels outside
  // the image rectangle must not enter the panel or its interpolation border.
  for(GLenum target:{GL_TEXTURE_2D,GL_TEXTURE_2D_ARRAY})for(bool flipped:{false,true}){
    renderer.make_current();OwnedTextures owned;glGenTextures(4,owned.ids.data());
    std::vector<unsigned char> pattern(8*6*4,255);
    for(unsigned y=2;y<5;++y)for(unsigned x=1;x<5;++x){const auto i=(y*8+x)*4;
      pattern[i]=(x-1)*40;pattern[i+1]=(y-2)*70;pattern[i+2]=0;}
    for(int i=0;i<3;++i){const auto type=i==2?target:GL_TEXTURE_2D;
      glBindTexture(type,owned.ids[i]);glTexParameteri(type,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(type,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      if(type==GL_TEXTURE_2D_ARRAY)glTexImage3D(type,0,GL_RGBA8,4,3,1,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
      else glTexImage2D(type,0,GL_RGBA8,i==0?8:4,i==0?6:3,0,GL_RGBA,GL_UNSIGNED_BYTE,i==0?pattern.data():nullptr);
    }
    Frame stage{{{owned.ids[1],4,3,1}},{{Eye::left,Plane::color,0,0,0,4,3,0}}};
    Frame output{{{owned.ids[2],4,3,1}},{{Eye::left,Plane::color,0,flipped?4u:0u,flipped?3u:0u,flipped?0u:4u,flipped?0u:3u,0}}};
    ColorRegion region{1,2,4,3};
    copy.copy_color({owned.ids[0],0,8,6},stage,false,true,&region);
    copy.copy_color({owned.ids[1],0,4,3},output,true);copy.finish();
    std::vector<unsigned char> result(4*3*4);
    glBindTexture(target,owned.ids[2]);glGetTexImage(target,0,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
    for(unsigned y=0;y<3;++y)for(unsigned x=0;x<4;++x){
      const auto src=((flipped?y+2:4-y)*8+(flipped?4-x:x+1))*4;
      for(unsigned c=0;c<4;++c)check(result[(y*4+x)*4+c]==pattern[src+c],"Image crop lost data or included surrounding text/margins");
    }
    bool rejected=false;region={7,0,4,3};
    try{copy.copy_color({owned.ids[0],0,8,6},stage,false,true,&region);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Invalid image crop accepted");
  }
  std::cout<<"PASS: mono panel GPU staging/upload, image crop, letterbox, single vertical conversion, SDK flips and GL state\n";
  return 0;
}catch(const std::exception&e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}}
