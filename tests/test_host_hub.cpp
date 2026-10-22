#if !defined(_WIN32) && defined(__GNUC__)
#define __declspec(annotation)
#endif
#include "../apps/host_hub.hpp"
#undef NDEBUG
#include <cassert>
#include <iostream>

int main() {
  using namespace qarvtk;
  HostTarget viewer{{}, QAR_PEER_ROLE_TARGET, QAR_DEVICE_CATEGORY_VISUALIZER, QAR_APP_STATE_RUNNING, "Viewer"};
  viewer.id.data[0] = 1;
  auto control = viewer; control.id.data[0] = 2; control.role = QAR_PEER_ROLE_CONTROL;
  auto source = viewer; source.id.data[0] = 3; source.role = QAR_PEER_ROLE_SOURCE;
  auto internal = viewer; internal.id.data[0] = 4; internal.role = QAR_PEER_ROLE_INTERNAL;
  auto headset = viewer; headset.id.data[0] = 5; headset.category = QAR_DEVICE_CATEGORY_HEADSET;
#if QARVTK_HOST_PEER_PRESENCE
  viewer.presence = headset.presence = QAR_PEER_PRESENCE_ONLINE;
  for (auto presence : {QAR_PEER_PRESENCE_OFFLINE, QAR_PEER_PRESENCE_LEAVING, QAR_PEER_PRESENCE_RETIRED}) {
    auto inactive = viewer; inactive.presence = presence;
    // A stale Running claim must not make a disconnected receiver selectable.
    assert(inactive.state == QAR_APP_STATE_RUNNING);
    assert(!select_host_target({control, source, internal, inactive, headset}, QAR_DEVICE_CATEGORY_VISUALIZER));
  }
  for (auto presence : {QAR_PEER_PRESENCE_ONLINE, QAR_PEER_PRESENCE_UNKNOWN, static_cast<QarPeerPresence>(7)}) {
    auto reachable = viewer; reachable.presence = presence;
    reachable.state = QAR_APP_STATE_RETIRED; // The deprecated field is not authoritative.
    assert(select_host_target({reachable}, QAR_DEVICE_CATEGORY_VISUALIZER));
  }
  auto observer = viewer; observer.category = QAR_DEVICE_CATEGORY_OBSERVER;
  auto camera = viewer; camera.category = QAR_DEVICE_CATEGORY_CAMERA;
  assert(!select_host_target({observer, camera}, QAR_DEVICE_CATEGORY_VISUALIZER));
  assert(!select_host_target({observer, camera}, QAR_DEVICE_CATEGORY_HEADSET));
#else
  for (auto state : {QAR_APP_STATE_SHUTTING_DOWN, QAR_APP_STATE_RETIRED}) {
    auto inactive = viewer; inactive.state = state;
    assert(!select_host_target({control, source, internal, inactive, headset}, QAR_DEVICE_CATEGORY_VISUALIZER));
  }
#endif
  auto selected = select_host_target({control, source, internal, viewer, headset}, QAR_DEVICE_CATEGORY_VISUALIZER);
  assert(selected && selected->id.data[0] == 1);
  selected = select_host_target({control, viewer, headset}, QAR_DEVICE_CATEGORY_HEADSET);
  assert(selected && selected->id.data[0] == 5);
  auto other = viewer; other.id.data[15] = 1;
  bool ambiguous = false;
  try { select_host_target({viewer, other}, QAR_DEVICE_CATEGORY_VISUALIZER); }
  catch (const std::runtime_error&) { ambiguous = true; }
  assert(ambiguous);
  assert(select_host_target({viewer, viewer}, QAR_DEVICE_CATEGORY_VISUALIZER));
  // A connected headset can arrive after the Visualizer is already running.
  // Control/source peers and repeated updates must not consume a target slot.
  HostTargetRegistry registry;
  registry.seed(control);registry.seed(source);registry.seed(viewer);
  assert(!select_host_target(registry.snapshot(),QAR_DEVICE_CATEGORY_HEADSET));
  auto warming=headset;warming.state=QAR_APP_STATE_INITIALIZING;registry.record(warming);
  assert(select_host_target(registry.snapshot(),QAR_DEVICE_CATEGORY_HEADSET));
  auto unknown=headset;unknown.state=QAR_APP_STATE_UNKNOWN;registry.record(unknown);
  assert(select_host_target(registry.snapshot(),QAR_DEVICE_CATEGORY_HEADSET));
  assert(!select_host_target({viewer},QAR_DEVICE_CATEGORY_HEADSET)); // No Visualizer fallback.
  assert(!select_host_target({headset},QAR_DEVICE_CATEGORY_VISUALIZER));
  registry.record(headset);registry.record(headset);
  assert(select_host_target(registry.snapshot(),QAR_DEVICE_CATEGORY_HEADSET)->id.data[0]==5);
  assert(select_host_target(registry.snapshot(),QAR_DEVICE_CATEGORY_VISUALIZER)->id.data[0]==1);
  // Subscription updates win a race with the initial snapshot. A retired peer
  // must not be resurrected and mistaken for a live receiver at startup.
  auto retired=headset;retired.state=QAR_APP_STATE_RETIRED;registry.record(retired);
#if QARVTK_HOST_PEER_PRESENCE
  retired.presence=QAR_PEER_PRESENCE_RETIRED;registry.record(retired);
#endif
  registry.seed(headset);
  assert(!select_host_target(registry.snapshot(),QAR_DEVICE_CATEGORY_HEADSET));
#if QARVTK_HOST_PEER_PRESENCE
  // A retained offline receiver must not create ambiguity with its replacement.
  // When the same peer comes online again it can be used on the next Start.
  auto offline=headset;offline.presence=QAR_PEER_PRESENCE_OFFLINE;registry.record(offline);
  registry.seed(headset);
  assert(!select_host_target(registry.snapshot(),QAR_DEVICE_CATEGORY_HEADSET));
  registry.record(headset);
  assert(select_host_target(registry.snapshot(),QAR_DEVICE_CATEGORY_HEADSET)->id.data[0]==5);
  registry.record(offline);
#endif
  auto replacement=headset;replacement.id.data[0]=6;registry.record(replacement);
  assert(select_host_target(registry.snapshot(),QAR_DEVICE_CATEGORY_HEADSET)->id.data[0]==6);
  registry.record(headset);ambiguous=false;
  try{select_host_target(registry.snapshot(),QAR_DEVICE_CATEGORY_HEADSET);}
  catch(const std::runtime_error&){ambiguous=true;}
  assert(ambiguous);
  const auto key = host_resource_name(viewer.id, "volume", 0);
  assert(key != host_resource_name(viewer.id, "volume", 1));
  assert(key != host_resource_name(other.id, "volume", 0));
  assert(key != host_resource_name(viewer.id, "panel", 0));
  for (unsigned char c : key) assert(c < 128);
  std::cout << "PASS: reachable target discovery; terminal peers excluded; snapshot/update races; category filtering; ambiguity; isolated volume/panel identities\n";
}
