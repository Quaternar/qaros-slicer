#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace qarvtk {
enum class Eye { left, right };
enum class Plane { color, depth };
struct Texture { unsigned handle{}, width{}, height{}, layers{1}; };
struct View {
  Eye eye; Plane plane; unsigned texture{};
  unsigned x0{},y0{},x1{},y1{},layer{};
};
struct Frame { std::vector<Texture> textures; std::vector<View> views; };
struct EyeViews { std::size_t color{}, depth{}; };
std::array<EyeViews,2> validate_stereo(const Frame& frame);
unsigned width(const View& v);
unsigned height(const View& v);
}
