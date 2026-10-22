#pragma once
#include <filesystem>
#include <memory>

namespace qarvtk {
// A plugin can stop its own backend without terminating another sender.
class Shutdown {
public:
  Shutdown(std::filesystem::path stop_file, unsigned long parent_pid);
  ~Shutdown();
  bool requested() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
