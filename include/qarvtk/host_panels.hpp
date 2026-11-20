#pragma once
#include <qar_streaming.h>
#include <functional>
#include <memory>
#include <string>
#include <cstdint>
#include <vector>
#include "host_bridge.h"

namespace qarvtk {
struct HostPanel;
struct ColorRegion;
// Owned by the GUI thread. SDK video/panel work runs on its own worker and
// finishes before the shared session/library can be destroyed.
class HostPanels {
  struct Impl;
  std::unique_ptr<Impl> impl_;
public:
  explicit HostPanels(std::function<void(const std::string&)> log);
  ~HostPanels();
  HostPanel* create(const char* name,unsigned width,unsigned height,
                    double x,double y,double z,double width_meters,bool interactive=false,double height_meters=0);
  int capture(HostPanel*,unsigned texture,unsigned width,unsigned height,const ColorRegion* region=nullptr);
  int capture_rgba(HostPanel*,const uint8_t*,unsigned width,unsigned height,unsigned stride);
  int gestures(HostPanel*,QarvtkPanelGesture*,unsigned capacity,uint64_t& epoch);
  void visible(HostPanel*,bool);
  void close(HostPanel*);
  std::string status(HostPanel*);
  void start(QarSession*,const std::vector<QarPeerId>&,const std::vector<QarPeerId>& visibility={},const std::string& scope_note={});
  // Thread-safe, additive audience: a late headset sees the existing panels.
  void add_peers(const std::vector<QarPeerId>&);
  // Exact live audience. Empty hides panels and suspends input, never broadcasts.
  void set_peers(const std::vector<QarPeerId>&,const std::vector<QarPeerId>& visibility={},const std::string& scope_note={});
  // Uses cached SDK hand data only while an interactive drag is outstanding.
  void sample_hands(QarSession*,QarRenderSender*,const QarAppVolumeId&,const QarPeerId&);
  void add_hand_source(QarSession*,QarRenderSender*,const QarAppVolumeId&,const QarPeerId&);
  void remove_hand_source(QarRenderSender*);
  void request_stop();
  void join(); // SDK-owning worker calls this before session destruction.
};
}
