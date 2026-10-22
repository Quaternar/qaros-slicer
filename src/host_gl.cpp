#include "qarvtk/host_gl.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#undef glActiveTexture
#undef glAttachShader
#undef glBindFramebuffer
#undef glBindSampler
#undef glBindVertexArray
#undef glCheckFramebufferStatus
#undef glCompileShader
#undef glCreateProgram
#undef glCreateShader
#undef glDeleteFramebuffers
#undef glDeleteProgram
#undef glDeleteShader
#undef glDeleteVertexArrays
#undef glDrawBuffers
#undef glFramebufferTexture
#undef glFramebufferTexture2D
#undef glFramebufferTextureLayer
#undef glGenFramebuffers
#undef glGenVertexArrays
#undef glGetProgramiv
#undef glGetShaderInfoLog
#undef glGetShaderiv
#undef glGetStringi
#undef glGetUniformLocation
#undef glLinkProgram
#undef glShaderSource
#undef glUniform1i
#undef glUniform2i
#undef glUseProgram
namespace qarvtk { namespace hostgl {
PFNGLACTIVETEXTUREPROC glActiveTexture{};
PFNGLATTACHSHADERPROC glAttachShader{};
PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer{};
PFNGLBINDSAMPLERPROC glBindSampler{};
PFNGLBINDVERTEXARRAYPROC glBindVertexArray{};
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus{};
PFNGLCOMPILESHADERPROC glCompileShader{};
PFNGLCREATEPROGRAMPROC glCreateProgram{};
PFNGLCREATESHADERPROC glCreateShader{};
PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers{};
PFNGLDELETEPROGRAMPROC glDeleteProgram{};
PFNGLDELETESHADERPROC glDeleteShader{};
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays{};
PFNGLDRAWBUFFERSPROC glDrawBuffers{};
PFNGLFRAMEBUFFERTEXTUREPROC glFramebufferTexture{};
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D{};
PFNGLFRAMEBUFFERTEXTURELAYERPROC glFramebufferTextureLayer{};
PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers{};
PFNGLGENVERTEXARRAYSPROC glGenVertexArrays{};
PFNGLGETPROGRAMIVPROC glGetProgramiv{};
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog{};
PFNGLGETSHADERIVPROC glGetShaderiv{};
PFNGLGETSTRINGIPROC glGetStringi{};
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation{};
PFNGLLINKPROGRAMPROC glLinkProgram{};
PFNGLSHADERSOURCEPROC glShaderSource{};
PFNGLUNIFORM1IPROC glUniform1i{};
PFNGLUNIFORM2IPROC glUniform2i{};
PFNGLUSEPROGRAMPROC glUseProgram{};
void load() {
  if (!wglGetCurrentContext()) throw std::runtime_error("Host OpenGL context is not current");
  const auto library=GetModuleHandleW(L"opengl32.dll");
  auto resolve=[&](const char* name) {
    PROC p=wglGetProcAddress(name);
    const auto value=reinterpret_cast<std::uintptr_t>(p);
    if(!p||value<=3||value==static_cast<std::uintptr_t>(-1)) p=GetProcAddress(library,name);
    if(!p) throw std::runtime_error(std::string("Missing OpenGL function: ")+name);
    return p;
  };
  glActiveTexture=reinterpret_cast<PFNGLACTIVETEXTUREPROC>(resolve("glActiveTexture"));
  glAttachShader=reinterpret_cast<PFNGLATTACHSHADERPROC>(resolve("glAttachShader"));
  glBindFramebuffer=reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(resolve("glBindFramebuffer"));
  glBindSampler=reinterpret_cast<PFNGLBINDSAMPLERPROC>(resolve("glBindSampler"));
  glBindVertexArray=reinterpret_cast<PFNGLBINDVERTEXARRAYPROC>(resolve("glBindVertexArray"));
  glCheckFramebufferStatus=reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(resolve("glCheckFramebufferStatus"));
  glCompileShader=reinterpret_cast<PFNGLCOMPILESHADERPROC>(resolve("glCompileShader"));
  glCreateProgram=reinterpret_cast<PFNGLCREATEPROGRAMPROC>(resolve("glCreateProgram"));
  glCreateShader=reinterpret_cast<PFNGLCREATESHADERPROC>(resolve("glCreateShader"));
  glDeleteFramebuffers=reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(resolve("glDeleteFramebuffers"));
  glDeleteProgram=reinterpret_cast<PFNGLDELETEPROGRAMPROC>(resolve("glDeleteProgram"));
  glDeleteShader=reinterpret_cast<PFNGLDELETESHADERPROC>(resolve("glDeleteShader"));
  glDeleteVertexArrays=reinterpret_cast<PFNGLDELETEVERTEXARRAYSPROC>(resolve("glDeleteVertexArrays"));
  glDrawBuffers=reinterpret_cast<PFNGLDRAWBUFFERSPROC>(resolve("glDrawBuffers"));
  glFramebufferTexture=reinterpret_cast<PFNGLFRAMEBUFFERTEXTUREPROC>(resolve("glFramebufferTexture"));
  glFramebufferTexture2D=reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(resolve("glFramebufferTexture2D"));
  glFramebufferTextureLayer=reinterpret_cast<PFNGLFRAMEBUFFERTEXTURELAYERPROC>(resolve("glFramebufferTextureLayer"));
  glGenFramebuffers=reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(resolve("glGenFramebuffers"));
  glGenVertexArrays=reinterpret_cast<PFNGLGENVERTEXARRAYSPROC>(resolve("glGenVertexArrays"));
  glGetProgramiv=reinterpret_cast<PFNGLGETPROGRAMIVPROC>(resolve("glGetProgramiv"));
  glGetShaderInfoLog=reinterpret_cast<PFNGLGETSHADERINFOLOGPROC>(resolve("glGetShaderInfoLog"));
  glGetShaderiv=reinterpret_cast<PFNGLGETSHADERIVPROC>(resolve("glGetShaderiv"));
  glGetStringi=reinterpret_cast<PFNGLGETSTRINGIPROC>(resolve("glGetStringi"));
  glGetUniformLocation=reinterpret_cast<PFNGLGETUNIFORMLOCATIONPROC>(resolve("glGetUniformLocation"));
  glLinkProgram=reinterpret_cast<PFNGLLINKPROGRAMPROC>(resolve("glLinkProgram"));
  glShaderSource=reinterpret_cast<PFNGLSHADERSOURCEPROC>(resolve("glShaderSource"));
  glUniform1i=reinterpret_cast<PFNGLUNIFORM1IPROC>(resolve("glUniform1i"));
  glUniform2i=reinterpret_cast<PFNGLUNIFORM2IPROC>(resolve("glUniform2i"));
  glUseProgram=reinterpret_cast<PFNGLUSEPROGRAMPROC>(resolve("glUseProgram"));
}
}}
