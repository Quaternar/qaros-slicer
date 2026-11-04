#pragma once
#include "host_wgl.hpp"
#include <cstdint>
#include <stdexcept>

namespace qarvtk {
// A fence belongs to the shared object group. Seal on the original GUI
// context, flush there, then wait on this sender's separate completion context.
// No SDK texture may be submitted or destroyed before wait_and_release returns.
class HostGpuFence {
  struct SyncTag;
  using Sync=SyncTag*;
  using Create=Sync(APIENTRY*)(GLenum,GLbitfield);
  using Wait=GLenum(APIENTRY*)(Sync,GLbitfield,std::uint64_t);
  using Delete=void(APIENTRY*)(Sync);
  Sync sync_{};
  Wait wait_{};
  Delete delete_{};
  static PROC resolve(const char* name){
    const auto proc=wglGetProcAddress(name);
    const auto value=reinterpret_cast<std::uintptr_t>(proc);
    if(!proc||value<=3||value==static_cast<std::uintptr_t>(-1))
      throw std::runtime_error("OpenGL 3.2 sync objects are required for asynchronous host copies");
    return proc;
  }
public:
  HostGpuFence(){
    if(!wglGetCurrentContext())throw std::runtime_error("No current context for host GPU fence");
    const auto create=reinterpret_cast<Create>(resolve("glFenceSync"));
    wait_=reinterpret_cast<Wait>(resolve("glClientWaitSync"));
    delete_=reinterpret_cast<Delete>(resolve("glDeleteSync"));
    sync_=create(0x9117 /* GL_SYNC_GPU_COMMANDS_COMPLETE */,0);
    if(!sync_)throw std::runtime_error("Cannot fence host GPU copies");
    glFlush(); // Make the producer's fence visible to the receiver worker.
  }
  HostGpuFence(const HostGpuFence&)=delete;
  HostGpuFence& operator=(const HostGpuFence&)=delete;
  ~HostGpuFence(){
    // Normal ownership releases on the completion worker while its context is
    // bound. A current context also covers exceptional GUI setup/unwind paths.
    if(sync_&&wglGetCurrentContext())delete_(sync_);
  }
  void wait_and_release(){
    if(!wglGetCurrentContext())throw std::runtime_error("No current receiver completion context");
    if(!sync_)return;
    while(true){
      const auto result=wait_(sync_,0,1000000 /* one millisecond, no producer flush here */);
      if(result==0x911A /* GL_ALREADY_SIGNALED */||result==0x911C /* GL_CONDITION_SATISFIED */)break;
      if(result!=0x911B /* GL_TIMEOUT_EXPIRED */){
        delete_(sync_);sync_=nullptr;
        throw std::runtime_error("Receiver GPU fence wait failed");
      }
    }
    delete_(sync_);sync_=nullptr;
  }
};
}
