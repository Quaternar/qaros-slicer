// The supplied SDK uses Windows annotations even for dynamic loading on Linux.
#if !defined(_WIN32) && defined(__GNUC__)
#define __declspec(annotation)
#endif
#include "../apps/legacy_peer_observer.hpp"
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
int main() { try {
  qarvtk::LegacyPeerObserver peers;
  QarPeerId viewer{}, mixer{};
  viewer.data[0] = mixer.data[0] = 1;
  mixer.data[sizeof(mixer.data)-1] = 2;
  require(!peers.state(viewer), "unseen viewer accepted");
  peers.record(mixer, QAR_APP_STATE_RUNNING);
  require(!peers.state(viewer), "another running peer accepted as viewer");
  // This update can arrive before invite_peer returns the authoritative id.
  peers.record(viewer, QAR_APP_STATE_UNKNOWN);
  const auto early = peers.state(viewer);
  require(early.has_value() && *early == QAR_APP_STATE_UNKNOWN,
          "early UNKNOWN viewer observation lost");
  peers.record(viewer, QAR_APP_STATE_INITIALIZING);
  require(peers.state(viewer) == QAR_APP_STATE_INITIALIZING, "state update lost");
  peers.record(mixer, QAR_APP_STATE_SHUTTING_DOWN);
  require(peers.state(viewer) == QAR_APP_STATE_INITIALIZING, "peer states mixed");
  peers.record(viewer, QAR_APP_STATE_SHUTTING_DOWN);
  require(peers.state(viewer) == QAR_APP_STATE_SHUTTING_DOWN, "shutdown update lost");
  std::cout << "PASS: exact invited peer, early UNKNOWN observation and state updates\n";
  return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
