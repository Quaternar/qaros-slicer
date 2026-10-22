#pragma once
#include <qar_streaming.h>
#include <cstring>
#include <mutex>
#include <optional>
#include <vector>

namespace qarvtk {
// Subscribe before inviting: the target can announce itself before invite_peer
// returns its id. Keep those early updates and look up the exact invited id.
class LegacyPeerObserver {
 public:
  void record(const QarPeerId& id, QarAppState state) noexcept {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto& peer : peers_) {
        if (same(peer.id, id)) { peer.state = state; return; }
      }
      peers_.push_back({id, state});
    } catch (...) {
      // Never propagate an exception through a runtime callback.
    }
  }

  // nullopt means not observed. UNKNOWN is an observed peer: current legacy
  // Visualizers can receive frames without ever announcing RUNNING.
  std::optional<QarAppState> state(const QarPeerId& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& peer : peers_) if (same(peer.id, id)) return peer.state;
    return std::nullopt;
  }

 private:
  static bool same(const QarPeerId& a, const QarPeerId& b) {
    return std::memcmp(a.data, b.data, sizeof(a.data)) == 0;
  }
  struct Peer { QarPeerId id; QarAppState state; };
  mutable std::mutex mutex_;
  std::vector<Peer> peers_;
};
} // namespace qarvtk
