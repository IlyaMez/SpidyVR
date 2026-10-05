#pragma once
#include "math.hpp"

namespace spidy {
// Overlay vertex: world position in metres and linear RGB.
struct Vertex {
    Vec3 position, color;
};
} // namespace spidy
