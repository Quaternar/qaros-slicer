#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>
#include <cstdint>
#include <stdexcept>

namespace qarvtk {
// Own hidden drawable/context, sharing textures with the host. The host's Qt
// context is never migrated to SDK threads or destroyed by the bridge.
class HostSharedWgl {
public:
  HWND window{}; HDC dc{}; HGLRC context{};
  HostSharedWgl(HDC host_dc,HGLRC host_context) {
    if(!host_dc||!host_context)throw std::runtime_error("No current host WGL context");
    WNDCLASSW wc{};wc.style=CS_OWNDC;wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"QarvtkHostSharedContext";
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
      throw std::runtime_error("Cannot register shared WGL window");
    try {
      window=CreateWindowExW(0,wc.lpszClassName,L"QAROS host context",WS_POPUP,
                            0,0,16,16,nullptr,nullptr,wc.hInstance,nullptr);
      if(!window)throw std::runtime_error("Cannot create shared WGL window");
      dc=GetDC(window);
      const int format=GetPixelFormat(host_dc);
      PIXELFORMATDESCRIPTOR pfd{};
      if(!dc||!format||!DescribePixelFormat(host_dc,format,sizeof(pfd),&pfd)||!SetPixelFormat(dc,format,&pfd))
        throw std::runtime_error("Cannot match host WGL pixel format");
      using Create=HGLRC(WINAPI*)(HDC,HGLRC,const int*);
      auto proc=wglGetProcAddress("wglCreateContextAttribsARB");
      const auto value=reinterpret_cast<std::uintptr_t>(proc);
      if(proc&&value>3&&value!=static_cast<std::uintptr_t>(-1)) {
        // OpenGL 3.3 core is sufficient for copies; share group inherits the
        // host's resources without importing its VTK/Qt ABI.
        const int attributes[]{0x2091,3,0x2092,3,0x9126,1,0};
        context=reinterpret_cast<Create>(proc)(dc,host_context,attributes);
      } else {
        // Basic Windows CI context, used only by the sharing/lifecycle test.
        context=wglCreateContext(dc);
        if(context&&!wglShareLists(host_context,context)) {
          wglDeleteContext(context);context=nullptr;
        }
      }
      if(!context)throw std::runtime_error("Cannot create a WGL context sharing host textures");
    } catch(...) { close();throw; }
  }
  HostSharedWgl(const HostSharedWgl&)=delete;
  HostSharedWgl& operator=(const HostSharedWgl&)=delete;
  ~HostSharedWgl(){close();}
  void close() noexcept {
    if(context){if(wglGetCurrentContext()==context)wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);context=nullptr;}
    if(dc){ReleaseDC(window,dc);dc=nullptr;}
    if(window){DestroyWindow(window);window=nullptr;}
  }
};
class WglBinding {
  HDC dc_; HGLRC context_;
public:
  explicit WglBinding(const HostSharedWgl& next):dc_(wglGetCurrentDC()),context_(wglGetCurrentContext()){
    if(!wglMakeCurrent(next.dc,next.context))throw std::runtime_error("Cannot bind shared copy context");
  }
  ~WglBinding(){wglMakeCurrent(dc_,context_);}
};
}
