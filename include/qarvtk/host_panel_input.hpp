#pragma once
#include "host_bridge.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <vector>

namespace qarvtk {
// Keep only the newest continuous position for each pointer/gesture. Lifecycle
// boundaries stay ordered, so release never waits behind stale motion history.
inline bool enqueue_panel_input(std::deque<QarvtkPanelGesture> &pending,
                                const QarvtkPanelGesture &event) {
  const auto motion = [](const auto &value) {
    return value.phase == 1 && (value.kind == 0 || value.kind == 3);
  };
  bool coalesced = false;
  if (motion(event)) {
    for (std::size_t i = pending.size(); i; --i) {
      const auto &previous = pending[i - 1];
      if (!motion(previous))
        break;
      if (previous.kind == event.kind && previous.hand == event.hand &&
          previous.gesture_id == event.gesture_id &&
          !std::memcmp(previous.peer, event.peer, sizeof(event.peer))) {
        pending.erase(pending.begin() + (i - 1));
        coalesced = true;
        break;
      }
    }
  }
  pending.push_back(event);
  return coalesced;
}

// The recognizer starts a drag after a physical travel threshold. Qt needs
// its mouse press at the original contact, before moving to the current point.
class HostPanelInput {
  struct Drag {
    QarvtkPanelGesture event;
    double seen;
  };
  std::vector<Drag> drags_;

public:
  struct Batch {
    std::array<QarvtkPanelGesture, 2> events{};
    unsigned count = 1;
  };
  HostPanelInput() { drags_.reserve(16); }
  void reset() { drags_.clear(); }
  Batch offer(const QarvtkPanelGesture &event, int start_x, int start_y, double now) {
    Batch batch;
    batch.events[0] = event;
    if (event.kind != 3)
      return batch; // QAR_GUI_PANEL_GESTURE_KIND_DRAG
    auto found = std::find_if(drags_.begin(), drags_.end(), [&](const auto &drag) {
      return drag.event.hand == event.hand &&
             std::memcmp(drag.event.peer, event.peer, sizeof(event.peer)) == 0;
    });
    if (event.phase == 2 || event.phase == 4) {
      if (found != drags_.end())
        drags_.erase(found);
      return batch;
    }
    if (event.phase != 0 && event.phase != 1)
      return batch;
    const bool begin = event.phase == 0 || found == drags_.end() ||
                       found->event.gesture_id != event.gesture_id || now - found->seen > .75;
    if (found == drags_.end())
      drags_.push_back({event, now});
    else
      *found = {event, now};
    if (begin) {
      batch.events[0].phase = 0;
      batch.events[0].x = start_x;
      batch.events[0].y = start_y;
      if (event.x != start_x || event.y != start_y) {
        batch.events[1] = event;
        batch.events[1].phase = 1;
        batch.count = 2;
      }
    }
    return batch;
  }
};
} // namespace qarvtk
