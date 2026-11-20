#pragma once
#include "host_hub.hpp"

namespace qarvtk {
struct HostPanelScope {
  std::vector<QarPeerId> peers;
  std::string note;
};
inline HostPanelScope host_panel_scope(const std::vector<HostTarget> &peers,
                                       const std::vector<QarPeerId> &selected,
                                       const QarPeerId &source) {
  HostPanelScope scope{selected, {}};
  const auto same = [](const auto &a, const auto &b) {
    return std::memcmp(a.data, b.data, sizeof(a.data)) == 0;
  };
  const auto add = [&](const QarPeerId &id) {
    if (std::none_of(scope.peers.begin(), scope.peers.end(),
                     [&](const auto &p) { return same(p, id); }))
      scope.peers.push_back(id);
  };
  add(source); // A source observes its controls; it is not a display target.
  const auto reachable = [](const HostTarget &peer) {
#if QARVTK_HOST_PEER_PRESENCE
    return peer.presence != QAR_PEER_PRESENCE_OFFLINE &&
           peer.presence != QAR_PEER_PRESENCE_LEAVING && peer.presence != QAR_PEER_PRESENCE_RETIRED;
#else
    return peer.state != QAR_APP_STATE_SHUTTING_DOWN && peer.state != QAR_APP_STATE_RETIRED;
#endif
  };
  for (const auto &id : selected) {
    const auto target = std::find_if(peers.begin(), peers.end(), [&](const auto &p) {
      return same(p.id, id) && p.role == QAR_PEER_ROLE_TARGET;
    });
    if (target == peers.end() || target->name.empty()) {
      scope.note = "Waiting for receiver controls";
      continue;
    }
    std::size_t named_targets = 0, selected_named = 0;
    std::vector<QarPeerId> mixers;
    for (const auto &peer : peers) {
      if (!reachable(peer))
        continue;
      if (peer.role == QAR_PEER_ROLE_TARGET && peer.name == target->name) {
        ++named_targets;
        if (std::any_of(selected.begin(), selected.end(),
                        [&](const auto &p) { return same(p, peer.id); }))
          ++selected_named;
      }
      // The runtime documents the mixer label as "<target> mixer".
      // Match only internal peers and never guess between same-named targets.
      if (peer.role == QAR_PEER_ROLE_INTERNAL && peer.name == target->name + " mixer")
        mixers.push_back(peer.id);
    }
    if (selected_named != named_targets || mixers.size() > named_targets) {
      scope.note =
          "Qt input needs distinct receiver names, or all receivers with the same name selected";
      continue;
    }
    if (mixers.empty()) {
      scope.note = "Waiting for receiver controls";
      continue;
    }
    for (const auto &mixer : mixers)
      add(mixer);
  }
  return scope;
}
} // namespace qarvtk
