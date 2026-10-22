#pragma once
#include <qar_streaming.h>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace qarvtk {
struct HostTarget {
  QarPeerId id{};
  QarPeerRole role{};
  QarDeviceCategory category{};
  QarAppState state{};
  std::string name;
  std::string device;
#if QARVTK_HOST_PEER_PRESENCE
  QarPeerPresence presence = QAR_PEER_PRESENCE_UNKNOWN;
#endif
};

// SDK callbacks only update this inbox. Sender creation and graphics work
// belong to our workers, never to the SDK callback thread.
class HostTargetRegistry {
  mutable std::mutex mutex_;
  std::vector<HostTarget> peers_;
  std::string error_;
  void put(HostTarget peer,bool initial) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found=std::find_if(peers_.begin(),peers_.end(),[&](const auto& p){
      return std::memcmp(p.id.data,peer.id.data,sizeof(p.id.data))==0;
    });
    // Subscribe before taking the initial snapshot. A newer callback wins
    // over a snapshot entry that was read while that callback was delivered.
    if(found!=peers_.end()){if(!initial)*found=std::move(peer);return;}
    if(peers_.size()>=4096)throw std::runtime_error("Too many QAROS targets");
    peers_.push_back(std::move(peer));
  }
public:
  void record(HostTarget peer){put(std::move(peer),false);}
  void seed(HostTarget peer){put(std::move(peer),true);}
  void fail(std::string error){std::lock_guard<std::mutex> lock(mutex_);error_=std::move(error);}
  std::vector<HostTarget> snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!error_.empty())throw std::runtime_error(error_);
    return peers_;
  }
};

// Presence describes reachability, not receiver readiness. Negotiation is
// still the sender's job. Keep the AppState fallback only for pinned SDKs
// that predate presence; never load the new function into an old runtime.
inline bool host_target_available(const HostTarget& peer) {
#if QARVTK_HOST_PEER_PRESENCE
  if (peer.role != QAR_PEER_ROLE_TARGET) return false;
  switch (peer.presence) {
    case QAR_PEER_PRESENCE_OFFLINE:
    case QAR_PEER_PRESENCE_LEAVING:
    case QAR_PEER_PRESENCE_RETIRED: return false;
    default: return true; // Unknown, Online and future values (SDK contract).
  }
#else
  return peer.role == QAR_PEER_ROLE_TARGET &&
    (peer.state == QAR_APP_STATE_UNKNOWN || peer.state == QAR_APP_STATE_INITIALIZING ||
     peer.state == QAR_APP_STATE_RUNNING);
#endif
}

#if QARVTK_HOST_PEER_PRESENCE
inline const char* host_peer_presence_name(QarPeerPresence presence) {
  switch (presence) {
    case QAR_PEER_PRESENCE_ONLINE: return "Online";
    case QAR_PEER_PRESENCE_OFFLINE: return "Offline";
    case QAR_PEER_PRESENCE_LEAVING: return "Leaving";
    case QAR_PEER_PRESENCE_RETIRED: return "Retired";
    default: return "Unknown";
  }
}
#else
inline const char* host_peer_state_name(QarAppState state) {
  switch(state){
    case QAR_APP_STATE_UNKNOWN:return "Unknown";
    case QAR_APP_STATE_INITIALIZING:return "Initializing";
    case QAR_APP_STATE_RUNNING:return "Running";
    case QAR_APP_STATE_SHUTTING_DOWN:return "ShuttingDown";
    case QAR_APP_STATE_RETIRED:return "Retired";
    default:return "Unsupported";
  }
}
#endif

// A Visualizer's control peer is not a receiver. Only the requested category
// of target is eligible; headset-only must never fall back to a Visualizer.
inline std::optional<HostTarget> select_host_target(const std::vector<HostTarget>& peers,
                                                   QarDeviceCategory category) {
  std::optional<HostTarget> selected;
  for (const auto& peer : peers) {
    if (!host_target_available(peer) || peer.category != category) continue;
    if (selected && std::memcmp(selected->id.data, peer.id.data, sizeof(peer.id.data)) != 0)
      throw std::runtime_error("Several matching QAROS receivers are available. Keep one receiver of the selected type connected, then restart streaming.");
    selected = peer;
  }
  return selected;
}

// Common names are ASCII identities, not display labels. Include this source
// peer so a second Slicer process, duplicate labels or Unicode names cannot
// reuse another source's volume/panel and close it during teardown.
inline std::string host_resource_name(const QarPeerId& source, const char* kind, std::size_t index) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string name = "slicer-";
  for (auto byte : source.data) { name += hex[byte >> 4]; name += hex[byte & 15]; }
  return name + '-' + kind + '-' + std::to_string(index);
}
}
