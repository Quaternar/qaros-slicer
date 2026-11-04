#include "qarvtk/gl.hpp"
#include "qarvtk/gl_copy.hpp"
#include "qarvtk/host_gpu_fence.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace qarvtk;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Bootstrap {
  HWND window{};HDC dc{};HGLRC context{};
  Bootstrap(){
    WNDCLASSW wc{};wc.style=CS_OWNDC;wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"QarvtkGpuCopyTest";
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Register test window");
    window=CreateWindowExW(0,wc.lpszClassName,L"GPU completion test",WS_POPUP,0,0,16,16,nullptr,nullptr,wc.hInstance,nullptr);
    require(window!=nullptr,"Create test window");dc=GetDC(window);
    PIXELFORMATDESCRIPTOR p{};p.nSize=sizeof(p);p.nVersion=1;
    p.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;p.iPixelType=PFD_TYPE_RGBA;p.cColorBits=24;p.cDepthBits=24;
    const auto format=ChoosePixelFormat(dc,&p);require(format&&SetPixelFormat(dc,format,&p),"Set pixel format");
    context=wglCreateContext(dc);require(context&&wglMakeCurrent(dc,context),"Create bootstrap context");
  }
  ~Bootstrap(){wglMakeCurrent(nullptr,nullptr);if(context)wglDeleteContext(context);if(dc)ReleaseDC(window,dc);if(window)DestroyWindow(window);}
};
constexpr unsigned test_width=64,test_height=32;
GLuint texture(GLint internal,GLenum format){
  GLuint id{};glGenTextures(1,&id);glBindTexture(GL_TEXTURE_2D,id);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D,0,internal,test_width,test_height,0,format,format==GL_RGBA?GL_UNSIGNED_BYTE:GL_FLOAT,nullptr);return id;
}
int main(){try{
  Bootstrap bootstrap;
  int major=0,minor=0;
  const auto version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
  if(!version||std::sscanf(version,"%d.%d",&major,&minor)!=2)throw std::runtime_error("Cannot read test OpenGL version");
  if(major<3||(major==3&&minor<3)){
    // Basic Windows CI contexts still run the independent WGL lifecycle
    // test. This exact-copy oracle additionally needs the real GL 3.3 path.
    std::cout<<"SKIP: OpenGL 3.3 GPU copy oracle unavailable; GL "<<version<<'\n';return 77;
  }
  HostSharedWgl host(bootstrap.dc,bootstrap.context);
  WglBinding original(host);hostgl::load();
  HostSharedWgl completionA(host.dc,host.context),completionB(host.dc,host.context);
  std::array<HostSharedWgl*,2> contexts{&completionA,&completionB};
  GlCopy copier;
  const auto color=texture(GL_RGBA8,GL_RGBA),depth=texture(GL_DEPTH_COMPONENT32F,GL_DEPTH_COMPONENT);
  GLuint framebuffer{};glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depth,0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Incomplete source framebuffer");
  std::array<Frame,2> targets;
  std::vector<GLuint> owned{color,depth};
  for(auto& target:targets){
    for(unsigned i=0;i<4;++i){
      const bool isDepth=i%2;
      const auto id=texture(isDepth?GL_R32F:GL_RGBA8,isDepth?GL_RED:GL_RGBA);
      owned.push_back(id);target.textures.push_back({id,test_width,test_height,1});
      target.views.push_back({i<2?Eye::left:Eye::right,isDepth?Plane::depth:Plane::color,i,0,0,test_width,test_height,0});
    }
  }
  std::array<std::future<void>,2> waiting;
  for(unsigned frame=0;frame<240;++frame){
    const auto route=frame%2;
    if(waiting[route].valid())waiting[route].get();
    const auto views=validate_stereo(targets[route]);
    std::array<unsigned char,2> red{static_cast<unsigned char>(17+(frame%200)),static_cast<unsigned char>(239-(frame%200))};
    std::array<float,2> depths{.125F,.75F};
    for(unsigned eye=0;eye<2;++eye){
      glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glViewport(0,0,test_width,test_height);
      glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);
      glClearColor(red[eye]/255.F,.2F,.4F,1.F);glClearDepth(depths[eye]);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
      // Deliberately leave non-default state around the production copy path.
      glEnable(GL_BLEND);glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_FALSE);
      RenderedEye source{color,depth,test_width,test_height};
      copier.copy_plane(source,targets[route],views[eye],false);
      copier.copy_plane(source,targets[route],views[eye],true);
      GLboolean mask[4]{};glGetBooleanv(GL_COLOR_WRITEMASK,mask);
      require(glIsEnabled(GL_BLEND)&&glIsEnabled(GL_SCISSOR_TEST)&&!mask[0]&&mask[1]&&!mask[2]&&!mask[3],"Host GL state changed");
    }
    // No glFinish on the producer: immediately overwrite the same borrowed
    // source textures for the next receiver while this SDK worker completes.
    auto fence=std::make_shared<HostGpuFence>();
    waiting[route]=std::async(std::launch::async,[&,route,red,depths,fence]{
      WglBinding binding(*contexts[route]);fence->wait_and_release();
      for(unsigned eye=0;eye<2;++eye){
        std::vector<unsigned char> rgba(test_width*test_height*4);std::vector<float> pixels(test_width*test_height);
        glBindTexture(GL_TEXTURE_2D,targets[route].textures[eye*2].handle);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
        glBindTexture(GL_TEXTURE_2D,targets[route].textures[eye*2+1].handle);glGetTexImage(GL_TEXTURE_2D,0,GL_RED,GL_FLOAT,pixels.data());
        for(unsigned p=0;p<test_width*test_height;++p){
          require(rgba[p*4]==red[eye]&&rgba[p*4+3]==255,"Eye color overwritten before asynchronous copy completed");
          require(std::abs(pixels[p]-depths[eye])<1e-6F,"Eye depth overwritten before asynchronous copy completed");
        }
      }
      require(glGetError()==GL_NO_ERROR,"Completion context GL error");
    });
    require(wglGetCurrentContext()==host.context,"GPU worker changed the GUI context");
  }
  for(auto& worker:waiting)if(worker.valid())worker.get();
  glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(static_cast<GLsizei>(owned.size()),owned.data());
  require(glGetError()==GL_NO_ERROR,"Host copy left an OpenGL error");
  std::cout<<"PASS: 240 stereo frames, two completion workers, immediate borrowed-texture reuse, exact color/depth and host GL state\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
