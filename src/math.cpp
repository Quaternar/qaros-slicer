#include "qarvtk/math.hpp"
#include <cmath>
#include <stdexcept>

namespace qarvtk {
namespace {
void finite(double v) { if (!std::isfinite(v)) throw std::invalid_argument("Non-finite camera value"); }
Quaternion normalized(Quaternion q) {
  finite(q.x); finite(q.y); finite(q.z); finite(q.w);
  const double n = std::hypot(std::hypot(q.x, q.y), std::hypot(q.z, q.w));
  if (n < 1e-12) throw std::invalid_argument("Zero camera quaternion");
  return {q.x/n, q.y/n, q.z/n, q.w/n};
}
}
void validate(Pose p) { finite(p.position.x); finite(p.position.y); finite(p.position.z); (void)normalized(p.orientation); }
void validate(Clip c) {
  finite(c.near_plane); finite(c.far_plane);
  if (c.near_plane <= 0 || c.far_plane <= c.near_plane)
    throw std::invalid_argument("Require 0 < near < far (reverse-Z is not used)");
}
Vec3 rotate(Quaternion q, Vec3 v) {
  q = normalized(q);
  const Vec3 t{2*(q.y*v.z-q.z*v.y), 2*(q.z*v.x-q.x*v.z), 2*(q.x*v.y-q.y*v.x)};
  return {v.x+q.w*t.x+q.y*t.z-q.z*t.y, v.y+q.w*t.y+q.z*t.x-q.x*t.z, v.z+q.w*t.z+q.x*t.y-q.y*t.x};
}
Matrix projection(Fov f, Clip c) {
  validate(c);
  for (double a : {f.left,f.right,f.up,f.down}) {
    finite(a);
    if (std::abs(a) >= 1.5707963267948966) throw std::invalid_argument("FOV angle outside perspective range");
  }
  const double l=std::tan(f.left),r=std::tan(f.right),b=std::tan(f.down),t=std::tan(f.up);
  if (r-l < 1e-8 || t-b < 1e-8) throw std::invalid_argument("Empty or inverted FOV");
  const double n=c.near_plane, z=c.far_plane;
  return {2/(r-l),0,(r+l)/(r-l),0, 0,2/(t-b),(t+b)/(t-b),0,
          0,0,-(z+n)/(z-n),-2*z*n/(z-n), 0,0,-1,0};
}
double depth_from_distance(double d, Clip c) {
  validate(c); finite(d);
  if (d <= 0) throw std::invalid_argument("Distance must be positive");
  return c.far_plane/(c.far_plane-c.near_plane) -
         c.far_plane*c.near_plane/((c.far_plane-c.near_plane)*d);
}
Vec3 ras_mm_to_meters(Vec3 r) { return {r.x*.001, r.z*.001, -r.y*.001}; }
}
