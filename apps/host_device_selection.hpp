#pragma once
#include "host_hub.hpp"

namespace qarvtk {
// An application identity, never a run id, timestamp, PID or release version.
#define QARVTK_LOCAL_APP_TAG "slicer.qaros.quaternar"
inline constexpr const char* host_app_tag = QARVTK_LOCAL_APP_TAG;
inline constexpr std::size_t max_host_targets = 8;

inline std::string host_peer_id_text(const QarPeerId& id) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string out;
  for (auto byte : id.data) { out += hex[byte >> 4]; out += hex[byte & 15]; }
  return out;
}

inline std::vector<HostTarget> selected_host_targets(const std::vector<HostTarget>& peers,
                                                   const std::string& ids) {
  if (ids.empty()) throw std::invalid_argument("Select at least one connected receiver");
  std::vector<HostTarget> selected;
  std::size_t offset = 0;
  while (offset < ids.size()) {
    const auto end = ids.find(',', offset);
    auto key = ids.substr(offset, end == std::string::npos ? end : end - offset);
    if (key.size() != sizeof(QarPeerId{}.data) * 2 ||
        key.find_first_not_of("0123456789abcdef") != std::string::npos)
      throw std::invalid_argument("Invalid receiver identity");
    if (std::any_of(selected.begin(), selected.end(), [&](const auto& p) { return host_peer_id_text(p.id) == key; }))
      throw std::invalid_argument("Receiver selected more than once");
    const auto found = std::find_if(peers.begin(), peers.end(), [&](const auto& p) {
      return host_peer_id_text(p.id) == key && host_target_available(p) &&
        (p.category == QAR_DEVICE_CATEGORY_HEADSET || p.category == QAR_DEVICE_CATEGORY_VISUALIZER);
    });
    if (found == peers.end()) throw std::runtime_error("A selected receiver is disconnected. Refresh the device list and select a connected receiver.");
    selected.push_back(*found);
    if (selected.size() > max_host_targets) throw std::invalid_argument("Select at most eight receivers");
    if (end == std::string::npos) break;
    offset = end + 1;
    if (offset == ids.size()) throw std::invalid_argument("Invalid receiver identity");
  }
  return selected;
}
}
