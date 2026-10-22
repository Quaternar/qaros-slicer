#include "../apps/wgl_callbacks.hpp"
#include "../include/qarvtk/host_wgl.hpp"
#include <iostream>

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
struct Context {
  HWND window{}; HDC dc{}; HGLRC context{};
  Context() {
    WNDCLASSW wc{}; wc.style=CS_OWNDC; wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"QarvtkWglCallbackTest";
    if (!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
      throw std::runtime_error("Cannot register WGL test window class");
    try {
      window=CreateWindowExW(0,wc.lpszClassName,L"WGL callback test",WS_OVERLAPPEDWINDOW,
                             0,0,32,32,nullptr,nullptr,wc.hInstance,nullptr);
      require(window!=nullptr,"Cannot create hidden WGL test window");
      dc=GetDC(window); require(dc!=nullptr,"Cannot obtain WGL test DC");
      PIXELFORMATDESCRIPTOR pfd{}; pfd.nSize=sizeof(pfd); pfd.nVersion=1;
      pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
      pfd.iPixelType=PFD_TYPE_RGBA; pfd.cColorBits=24; pfd.cDepthBits=24;
      const int pixel=ChoosePixelFormat(dc,&pfd);
      require(pixel!=0 && SetPixelFormat(dc,pixel,&pfd),"Cannot set WGL test pixel format");
      context=wglCreateContext(dc); require(context!=nullptr,"Cannot create basic WGL test context");
    } catch (...) { close(); throw; }
  }
  void close() {
    if (context) { if (wglGetCurrentContext()==context) wglMakeCurrent(nullptr,nullptr); wglDeleteContext(context); context=nullptr; }
    if (dc) { ReleaseDC(window,dc); dc=nullptr; }
    if (window) { DestroyWindow(window); window=nullptr; }
  }
  ~Context() { close(); }
};
int main() { try {
  Context host, sdk;
  require(wglMakeCurrent(host.dc,host.context)!=FALSE,"Cannot bind host context");
  {
    GLuint texture{};glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    const unsigned char pixel[]{17,93,201,255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixel);glFinish();
    qarvtk::HostSharedWgl shared(host.dc,host.context);
    {
      qarvtk::WglBinding binding(shared);
      require(glIsTexture(texture)==GL_TRUE,"Host texture is not visible in sharing context");
      unsigned char result[4]{};glBindTexture(GL_TEXTURE_2D,texture);
      glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,result);
      require(result[0]==17&&result[1]==93&&result[2]==201&&result[3]==255,"Shared host texture contents changed");
    }
    require(wglGetCurrentContext()==host.context,"Shared copy context did not restore host binding");
    glDeleteTextures(1,&texture);
  }
  qarvtk::WglCallbacks callbacks(sdk.dc,sdk.context);
  auto native_params=callbacks.parameters();
#if QARVTK_SPLIT_OPENGL_PARAMS
  require(native_params.header.type==QAR_STRUCTURE_TYPE_STREAM_OPENGL_WGL_PARAMS_EXT,
          "Wrong WGL extension discriminator");
  require(native_params.device_context==sdk.dc,"Wrong SDK device context");
  auto& params=native_params.callbacks;
#else
  require(native_params.context_type==QAR_OPENGL_CONTEXT_TYPE_WGL,"Wrong context type");
  require(native_params.display_or_device_context==sdk.dc,"Wrong SDK device context");
  auto& params=native_params;
#endif
  require(native_params.context==sdk.context,"Wrong SDK native context");
  // Test worker migration and OpenGL 1.1 export fallback using Windows' basic
  // context. This does not claim glGetStringi or NVIDIA interop support in CI.
  const auto probe=callbacks.probe_on_worker();
  require(!probe.version.empty(),"Worker could not query its own GL version");
  require(wglGetCurrentContext()==host.context,"Worker disturbed host thread context");
  require(params.get_proc_address(params.user_state,"glGetString")==nullptr,
          "Resolved against the wrong context");
  const auto before=params.capture_current(params.user_state);
  require(before.has_context && before.context==host.context,"Host binding was not captured");
  require(params.make_current(params.user_state),"Callback did not bind SDK context");
  require(wglGetCurrentContext()==sdk.context,"SDK context is not current");
  require(params.get_proc_address(params.user_state,"glGetString")!=nullptr,
          "OpenGL 1.1 export fallback failed");
  require(params.get_proc_address(params.user_state,"qarvtkNonexistentGlFunction")==nullptr,
          "Invalid function returned an address");
  require(params.restore_current(params.user_state,&before),"Host restore failed");
  require(wglGetCurrentContext()==host.context && wglGetCurrentDC()==host.dc,"Wrong host binding restored");
  require(wglMakeCurrent(nullptr,nullptr)!=FALSE,"Cannot prepare empty binding");
  const auto empty=params.capture_current(params.user_state);
  require(!empty.has_context,"Empty binding reported as current");
  require(params.make_current(params.user_state),"Rebind failed");
  require(params.restore_current(params.user_state,&empty) && wglGetCurrentContext()==nullptr,
          "Empty binding was not restored");
  std::cout << "PASS: native WGL worker binding, export loading, host restore and unbind; GL " << probe.version << '\n';
  return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
