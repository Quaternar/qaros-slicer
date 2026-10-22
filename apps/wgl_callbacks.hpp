#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <qar_streaming.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace qarvtk {
// The SDK invokes these on its graphics thread. Use native WGL, not GLFW's
// thread-local current-window bookkeeping, which the SDK does not maintain.
class WglCallbacks {
 public:
  WglCallbacks(HDC dc, HGLRC context) : dc_(dc), context_(context),
      opengl_(GetModuleHandleW(L"opengl32.dll")) {
    if (!dc_ || !context_ || !opengl_) throw std::runtime_error("Invalid WGL context or OpenGL module");
  }
#if QARVTK_SPLIT_OPENGL_PARAMS
  using Params = QarStreamParamsOpenGlWgl;
  void configure(Params& params) {
    params.callbacks = {this, get_proc, make_current, capture_current, restore_current};
  }
#else
  using Params = QarStreamParamsOpenGl;
  void configure(Params& params) {
    params.user_state=this;
    params.get_proc_address=get_proc;
    params.make_current=make_current;
    params.unbind=unbind;
    params.capture_current=capture_current;
    params.restore_current=restore_current;
  }
#endif
  Params parameters() {
#if QARVTK_SPLIT_OPENGL_PARAMS
    auto params=qar_stream_params_opengl_wgl_default();
    params.device_context=dc_;
#else
    auto params=qar_stream_params_opengl_default();
    params.context_type=QAR_OPENGL_CONTEXT_TYPE_WGL;
    params.display_or_device_context=dc_;
#endif
    params.context=context_;
    configure(params);
    return params;
  }
  static void* get_proc(void* user, const char* name) noexcept {
    auto& self=*static_cast<WglCallbacks*>(user);
    ++self.resolves_;
    // Extension pointers must be resolved with this context current. A pointer
    // from another adapter/pixel format is not a safe substitute.
    const bool current=wglGetCurrentContext()==self.context_;
    PROC proc=nullptr;
    if (current && name) {
      proc=wglGetProcAddress(name);
      const auto value=reinterpret_cast<std::uintptr_t>(proc);
      if (!proc || value<=3 || value==static_cast<std::uintptr_t>(-1))
        proc=GetProcAddress(self.opengl_,name); // OpenGL 1.1 exports live here.
    }
    if (name && std::string_view(name)=="glGetStringi")
      std::fprintf(stdout,"WGL resolver: glGetStringi=%s; supplied context current=%s\n",
                   proc?"available":"missing",current?"yes":"no");
    return reinterpret_cast<void*>(proc);
  }
  static bool make_current(void* user) noexcept {
    auto& self=*static_cast<WglCallbacks*>(user);
    ++self.binds_;
    const bool ok=wglMakeCurrent(self.dc_,self.context_)!=FALSE;
    if (!ok) std::fprintf(stderr,"WGL make_current failed: Windows error %lu\n",GetLastError());
    return ok;
  }
  static bool unbind(void*) noexcept { return wglMakeCurrent(nullptr,nullptr)!=FALSE; }
  static QarOpenGlContextThreadState capture_current(void* user) noexcept {
    ++static_cast<WglCallbacks*>(user)->captures_;
    QarOpenGlContextThreadState state{};
    state.display_or_device_context=wglGetCurrentDC();
    state.context=wglGetCurrentContext(); state.has_context=state.context!=nullptr;
    return state;
  }
  static bool restore_current(void* user, const QarOpenGlContextThreadState* state) noexcept {
    ++static_cast<WglCallbacks*>(user)->restores_;
    if (!state) return false;
    if (!state->has_context) return unbind(user);
    return wglMakeCurrent(static_cast<HDC>(state->display_or_device_context),
                          static_cast<HGLRC>(state->context))!=FALSE;
  }
  struct Probe { std::string version; bool get_string_i{}, memory_import{}, semaphore_import{}; };
  Probe probe_on_worker() {
    Probe probe; std::exception_ptr error;
    std::thread worker([&] {
      const auto before=capture_current(this);
      try {
        if (!make_current(this)) throw std::runtime_error("Cannot bind shared WGL context on a worker thread");
        using GetString=const unsigned char* (APIENTRY*)(unsigned int);
        const auto get=reinterpret_cast<GetString>(get_proc(this,"glGetString"));
        const auto version=get?get(0x1F02 /* GL_VERSION */):nullptr;
        if (!version) throw std::runtime_error("Shared WGL context has no GL_VERSION on a worker thread");
        probe.version=reinterpret_cast<const char*>(version);
        probe.get_string_i=get_proc(this,"glGetStringi")!=nullptr;
        probe.memory_import=get_proc(this,"glImportMemoryWin32HandleEXT")!=nullptr;
        probe.semaphore_import=get_proc(this,"glImportSemaphoreWin32HandleEXT")!=nullptr;
      } catch (...) { error=std::current_exception(); }
      if (!restore_current(this,&before) && !error)
        error=std::make_exception_ptr(std::runtime_error("Cannot restore WGL context after worker probe"));
    });
    worker.join();
    if (error) std::rethrow_exception(error);
    // The following counters will describe SDK activity, not the probe.
    binds_=0; resolves_=0; captures_=0; restores_=0;
    return probe;
  }
  void report() const {
    std::fprintf(stdout,"WGL SDK callbacks: bind=%u resolve=%u capture=%u restore=%u\n",
                 binds_.load(),resolves_.load(),captures_.load(),restores_.load());
  }
 private:
  HDC dc_; HGLRC context_; HMODULE opengl_;
  std::atomic<unsigned> binds_{0}, resolves_{0}, captures_{0}, restores_{0};
};
} // namespace qarvtk
