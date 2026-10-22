#pragma once
#include <array>

namespace qarvtk {
struct Vec3 { double x{}, y{}, z{}; };
struct Quaternion { double x{}, y{}, z{}, w{1.0}; };
struct Pose { Vec3 position; Quaternion orientation; };
struct Fov { double left{-0.6}, right{0.6}, up{0.6}, down{-0.6}; };
struct Clip { double near_plane{0.05}, far_plane{20.0}; };
using Matrix = std::array<double, 16>; // Row-major, column vectors.
Vec3 rotate(Quaternion q, Vec3 v);
Matrix projection(Fov fov, Clip clip); // OpenGL NDC z in [-1,1].
double depth_from_distance(double distance, Clip clip); // Texture z in [0,1].
Vec3 ras_mm_to_meters(Vec3 ras); // +X right, +Y up, -Z anterior.
void validate(Pose pose);
void validate(Clip clip);
}
