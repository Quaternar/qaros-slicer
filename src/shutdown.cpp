#include "qarvtk/shutdown.hpp"
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#endif

namespace qarvtk {
struct Shutdown::Impl {
  std::filesystem::path stop_file;
  unsigned long parent_pid{};
#ifdef _WIN32
  HANDLE parent{};
  ~Impl() { if (parent) CloseHandle(parent); }
#endif
};
Shutdown::Shutdown(std::filesystem::path stop_file, unsigned long parent_pid)
  : impl_(std::make_unique<Impl>()) {
  impl_->stop_file=std::move(stop_file);
  impl_->parent_pid=parent_pid;
#ifdef _WIN32
  if (parent_pid) {
    impl_->parent=OpenProcess(SYNCHRONIZE,FALSE,static_cast<DWORD>(parent_pid));
    if (!impl_->parent) throw std::runtime_error("Cannot monitor plugin parent process");
  }
#endif
}
Shutdown::~Shutdown()=default;
bool Shutdown::requested() const {
  if (!impl_->stop_file.empty() && std::filesystem::exists(impl_->stop_file)) return true;
#ifdef _WIN32
  return impl_->parent && WaitForSingleObject(impl_->parent,0)==WAIT_OBJECT_0;
#else
  return impl_->parent_pid && kill(static_cast<pid_t>(impl_->parent_pid),0)!=0 && errno==ESRCH;
#endif
}
}
