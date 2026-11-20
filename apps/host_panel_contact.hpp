#pragma once
#include <cmath>
#include <optional>
#include <qar_streaming.h>

namespace qarvtk {
inline std::optional<QarVector3> contact_rotate(QarQuaternion q, QarVector3 p) {
  const double n = double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w;
  if (!std::isfinite(n) || n < 1e-12 || !std::isfinite(p.x) || !std::isfinite(p.y) ||
      !std::isfinite(p.z))
    return {};
  const double tx = 2 * (q.y * p.z - q.z * p.y) / n;
  const double ty = 2 * (q.z * p.x - q.x * p.z) / n;
  const double tz = 2 * (q.x * p.y - q.y * p.x) / n;
  return QarVector3{float(p.x + q.w * tx + q.y * tz - q.z * ty),
                    float(p.y + q.w * ty + q.z * tx - q.x * tz),
                    float(p.z + q.w * tz + q.x * ty - q.y * tx)};
}
inline std::optional<QarVector3> contact_room_point(const QarPose &volume, const QarPose &app,
                                                    float scale, QarVector3 point) {
  if (!std::isfinite(scale) || scale <= 0)
    return {};
  point = {point.x * scale, point.y * scale, point.z * scale};
  auto local = contact_rotate(app.orientation, point);
  if (!local)
    return {};
  local->x += app.position.x;
  local->y += app.position.y;
  local->z += app.position.z;
  auto room = contact_rotate(volume.orientation, *local);
  if (!room)
    return {};
  room->x += volume.position.x;
  room->y += volume.position.y;
  room->z += volume.position.z;
  return room;
}
inline std::optional<double> contact_front_distance(const QarPose &panel, QarVector3 room) {
  const auto normal = contact_rotate(panel.orientation, {0, 0, -1});
  if (!normal)
    return {};
  const double distance = (room.x - panel.position.x) * normal->x +
                          (room.y - panel.position.y) * normal->y +
                          (room.z - panel.position.z) * normal->z;
  return std::isfinite(distance) ? std::optional<double>(distance) : std::nullopt;
}
} // namespace qarvtk
