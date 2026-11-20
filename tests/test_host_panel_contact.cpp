#include "../apps/host_panel_contact.hpp"
#if !defined(_WIN32) && defined(__GNUC__)
#define __declspec(annotation)
#endif
#include "qarvtk/host_panel_contact.hpp"
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <limits>

int main() {
  using Guard = qarvtk::HostPanelContact;
  Guard guard;
  QarvtkPanelGesture event{};
  event.peer[0] = 1;
  event.hand = 2;
  event.kind = 3;
  event.gesture_id = 9;
  assert(guard.offer(event, 0) == Guard::Decision::allow);
  assert(!guard.sample(event.peer, 2, 1, .055, 0));
  assert(!guard.sample(event.peer, 2, 1, .055, .1)); // Cached SDK data is insufficient.
  auto canceled = guard.sample(event.peer, 2, 2, .055, .2);
  assert(canceled && canceled->phase == 4 && canceled->gesture_id == 9);
  event.phase = 1;
  assert(guard.offer(event, .21) == Guard::Decision::drop);
  assert(!guard.sample(event.peer, 2, 3, .005, .22));
  assert(guard.offer(event, .23) == Guard::Decision::repress);
  assert(!guard.sample(event.peer, 2, 3, .055, .24)); // Duplicate sample cannot cancel again.
  assert(!guard.sample(event.peer, 2, 4, .015, .25)); // Hysteresis while touching.
  assert(guard.sample(event.peer, 2, 5, .055, .26));
  assert(!guard.sample(event.peer, 2, 6, .005, .3));
  assert(guard.offer(event, .6) == Guard::Decision::drop); // Re-entry sample is stale.
  assert(!guard.sample(event.peer, 2, 7, std::numeric_limits<double>::quiet_NaN(), .61));
  assert(!guard.sample(event.peer, 2, 8, .005, .62));
  assert(guard.offer(event, .63) == Guard::Decision::repress);
  auto other = event;
  other.peer[0] = 2;
  other.phase = 0;
  assert(guard.offer(other, .64) == Guard::Decision::allow);
  auto left = event;
  left.hand = 1;
  left.phase = 0;
  assert(guard.offer(left, .64) == Guard::Decision::allow);
  assert(!guard.sample(event.peer, 2, 9, -.015, .65));
  assert(!guard.sample(other.peer, 2, 1, .055, .65));
  assert(guard.sample(other.peer, 2, 2, .055, .66));
  event.phase = 2;
  assert(guard.offer(event, .67) == Guard::Decision::allow);
  assert(guard.needed());
  other.phase = 4;
  guard.offer(other, .68);
  left.phase = 4;
  guard.offer(left, .68);
  assert(!guard.needed());
  event.phase=0; event.gesture_id=10;
  guard.offer(event,1);
  assert(!guard.sample(event.peer,2,100,.023,1,.008));
  assert(!guard.sample(event.peer,2,101,.023,1.01,.008)); // A grazing fingertip still touches.
  assert(guard.sample(event.peer,2,102,.055,1.02,.008));
  assert(!guard.sample(event.peer,2,103,.023,1.03,.008));
  event.phase=1;
  assert(guard.offer(event,1.04)==Guard::Decision::repress);

  auto app = qar_pose_default(), volume = qar_pose_default();
  app.position = {1, 2, 3};
  app.orientation = {0, 0, float(std::sqrt(.5)), float(std::sqrt(.5))};
  volume.position = {10, 20, 30};
  volume.orientation = {0, 1, 0, 0};
  const auto room = qarvtk::contact_room_point(volume, app, .001f, {1000, 0, 0});
  assert(room && std::abs(room->x - 9) < 1e-5 && std::abs(room->y - 23) < 1e-5 &&
         std::abs(room->z - 27) < 1e-5);
  auto panel = qar_pose_default();
  panel.orientation = {0, 1, 0, 0};
  panel.position = {0, .2f, -.8f};
  assert(std::abs(*qarvtk::contact_front_distance(panel, {0, 0, -.745f}) - .055) < 1e-5);
  assert(std::abs(*qarvtk::contact_front_distance(panel, {0, 0, -.815f}) + .015) < 1e-5);
  assert(!qarvtk::contact_room_point(volume, app, 0, {1, 2, 3}));
  panel.orientation = {0, 0, 0, 0};
  assert(!qarvtk::contact_front_distance(panel, {0, 0, 0}));
  std::cout << "PASS: fresh contact release, stale-data rejection, re-entry, separate hands/peers "
               "and app-to-room transforms\n";
}
