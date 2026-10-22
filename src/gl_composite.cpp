#include "qarvtk/gl.hpp"
#include "qarvtk/gl_composite.hpp"
#include <array>
#include <stdexcept>
namespace qarvtk { namespace {
const char* vertex=R"(#version 330 core
out vec2 uv;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2-1,0,1);}
)";
const char* fragment=R"(#version 330 core
in vec2 uv;
layout(location=0) out vec4 color;
layout(location=1) out float depth;
uniform sampler2D surfaceColor,surfaceDepth,volumeColor,volumeDepth;
void main(){
  vec4 s=texture(surfaceColor,uv),v=texture(volumeColor,uv);
  // VTK clears the ray-cast image to white with zero alpha. Its actual rays
  // contain premultiplied color and already stop at opaque surface depth.
  if(v.a<=0.0)v=vec4(0);
  color=v+(1.0-v.a)*s;
  depth=min(texture(surfaceDepth,uv).r,v.a>0.0?texture(volumeDepth,uv).r:1.0);
}
)";
unsigned compile(GLenum kind,const char* source){
  auto shader=glCreateShader(kind);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
  GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
  if(!ok){char log[2048]{};glGetShaderInfoLog(shader,2048,nullptr,log);glDeleteShader(shader);throw std::runtime_error(log);}
  return shader;
}
struct State {
  GLint read{},draw{},program{},vao{},active{},viewport[4]{},textures[4]{},samplers[4]{};
  GLboolean mask[4]{};
  std::array<GLenum,6> caps{GL_DEPTH_TEST,GL_BLEND,GL_SCISSOR_TEST,GL_CULL_FACE,GL_FRAMEBUFFER_SRGB,GL_RASTERIZER_DISCARD};
  std::array<GLboolean,6> enabled{};
  State(){
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_VIEWPORT,viewport);glGetBooleanv(GL_COLOR_WRITEMASK,mask);
    for(unsigned i=0;i<4;++i){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);glGetIntegerv(GL_SAMPLER_BINDING,&samplers[i]);}
    for(unsigned i=0;i<caps.size();++i)enabled[i]=glIsEnabled(caps[i]);
  }
  ~State(){
    glBindFramebuffer(GL_READ_FRAMEBUFFER,read);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);
    glUseProgram(program);glBindVertexArray(vao);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    glColorMask(mask[0],mask[1],mask[2],mask[3]);
    for(unsigned i=0;i<4;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i]);glBindSampler(i,samplers[i]);}
    glActiveTexture(active);
    for(unsigned i=0;i<caps.size();++i)if(enabled[i])glEnable(caps[i]);else glDisable(caps[i]);
  }
};
}
GlComposite::GlComposite(){
  auto v=compile(GL_VERTEX_SHADER,vertex),f=compile(GL_FRAGMENT_SHADER,fragment);
  program_=glCreateProgram();glAttachShader(program_,v);glAttachShader(program_,f);glLinkProgram(program_);
  glDeleteShader(v);glDeleteShader(f);GLint ok=0;glGetProgramiv(program_,GL_LINK_STATUS,&ok);
  if(!ok){glDeleteProgram(program_);program_=0;throw std::runtime_error("Cannot link volume composition shader");}
  glGenVertexArrays(1,&vao_);glGenFramebuffers(1,&fbo_);glGenTextures(1,&color_);glGenTextures(1,&depth_);
}
GlComposite::~GlComposite(){
  glDeleteTextures(1,&color_);glDeleteTextures(1,&depth_);glDeleteFramebuffers(1,&fbo_);glDeleteVertexArrays(1,&vao_);glDeleteProgram(program_);
}
RenderedEye GlComposite::combine(RenderedEye s,unsigned vc,unsigned vd){
  State state;
  for(auto cap:state.caps)glDisable(cap);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glActiveTexture(GL_TEXTURE0);
  glBindFramebuffer(GL_FRAMEBUFFER,fbo_);
  if(width_!=s.width||height_!=s.height){
    for(unsigned i=0;i<2;++i){
      glBindTexture(GL_TEXTURE_2D,i?depth_:color_);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      glTexImage2D(GL_TEXTURE_2D,0,i?GL_R32F:GL_RGBA8,s.width,s.height,0,i?GL_RED:GL_RGBA,i?GL_FLOAT:GL_UNSIGNED_BYTE,nullptr);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,GL_TEXTURE_2D,i?depth_:color_,0);
    }
    const GLenum buffers[]{GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};glDrawBuffers(2,buffers);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete volume composition framebuffer");
    width_=s.width;height_=s.height;
  }
  glViewport(0,0,s.width,s.height);glUseProgram(program_);glBindVertexArray(vao_);
  const unsigned textures[]{s.color,s.depth,vc,vd};
  const char* names[]{"surfaceColor","surfaceDepth","volumeColor","volumeDepth"};
  for(unsigned i=0;i<4;++i){glActiveTexture(GL_TEXTURE0+i);glBindSampler(i,0);glBindTexture(GL_TEXTURE_2D,textures[i]);glUniform1i(glGetUniformLocation(program_,names[i]),i);}
  glDrawArrays(GL_TRIANGLES,0,3);
  if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("OpenGL volume composition failed");
  return {color_,depth_,s.width,s.height};
}
}
