#pragma once
#include <qar_streaming.h>
#include <functional>
#include <memory>
#include <string>
#include <cstdint>
#include <vector>

namespace qarvtk {
struct HostPanel;
struct ColorRegion;
// Owned by the GUI thread. SDK video/panel work runs on its own worker and
// finishes before the shared session/library can be destroyed. No input API.
class HostPanels {
  struct Impl;
  std::unique_ptr<Impl> impl_;
public:
  explicit HostPanels(std::function<void(const std::string&)> log);
  ~HostPanels();
  HostPanel* create(const char* name,unsigned width,unsigned height,
                    double x,double y,double z,double width_meters);
  int capture(HostPanel*,unsigned texture,unsigned width,unsigned height,const ColorRegion* region=nullptr);
  void visible(HostPanel*,bool);
  void close(HostPanel*);
  std::string status(HostPanel*);
  void start(QarSession*,const std::vector<QarPeerId>&);
  // Thread-safe, additive audience: a late headset sees the existing panels.
  void add_peers(const std::vector<QarPeerId>&);
  void request_stop();
  void join(); // SDK-owning worker calls this before session destruction.
};
}
