#pragma once
#include "host_bridge.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

namespace qarvtk {
// The SDK can keep a drag alive after the fingertip has withdrawn. Only
// advancing hand samples can release it; an SDK-cached sample cannot do so.
class HostPanelContact {
  struct Contact {
    QarvtkPanelGesture last{};
    int64_t timestamp{};
    bool blocked{}, ready{};
    double seen{};
  };
  std::vector<Contact> contacts_;
  uint64_t samples_{};
  auto find(const uint8_t *peer, uint32_t hand) {
    return std::find_if(contacts_.begin(), contacts_.end(), [&](const auto &c) {
      return c.last.hand == hand && !std::memcmp(c.last.peer, peer, sizeof(c.last.peer));
    });
  }

public:
  enum class Decision { allow, drop, repress };
  HostPanelContact() { contacts_.reserve(24); }
  void reset() { contacts_.clear(); }
  bool needed() const { return !contacts_.empty(); }
  uint64_t samples() const { return samples_; }
  Decision offer(const QarvtkPanelGesture &event, double now) {
    if (event.kind != 3)
      return Decision::allow;
    auto it = find(event.peer, event.hand);
    if (event.phase == 2 || event.phase == 4) {
      if (it != contacts_.end())
        contacts_.erase(it);
      return Decision::allow;
    }
    if (event.phase != 0 && event.phase != 1)
      return Decision::allow;
    if (it == contacts_.end()) {
      if (contacts_.size() >= 24)
        return Decision::drop;
      contacts_.push_back({event});
      return Decision::allow;
    }
    if (event.phase == 0 || it->last.gesture_id != event.gesture_id) {
      *it = {event};
      return Decision::allow;
    }
    if (it->blocked) {
      if (!it->ready || now - it->seen > .25)
        return Decision::drop;
      it->blocked = it->ready = false;
      it->last = event;
      return Decision::repress;
    }
    it->last = event;
    return Decision::allow;
  }
  std::optional<QarvtkPanelGesture> sample(const uint8_t *peer, uint32_t hand, int64_t timestamp,
                                         double front_meters, double now, double radius = 0) {
    auto it = find(peer, hand);
    if (it == contacts_.end() || !std::isfinite(front_meters) || !std::isfinite(radius) || radius < 0 || timestamp <= 0 ||
        timestamp <= it->timestamp)
      return {};
    const bool primed = it->timestamp != 0;
    ++samples_;
    it->timestamp = timestamp;
    it->seen = now;
    if (!primed)
      return {}; // Do not act on the SDK's first, possibly cached sample.
    if (it->blocked) {
      it->ready = front_meters < .02 + std::min(radius, .03);
      return {};
    }
    if (front_meters <= .03 + std::min(radius, .03))
      return {};
    it->blocked = true;
    it->ready = false;
    auto canceled = it->last;
    canceled.phase = 4;
    return canceled;
  }
};
} // namespace qarvtk
