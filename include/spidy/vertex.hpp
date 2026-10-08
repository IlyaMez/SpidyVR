#pragma once
#include "math.hpp"

namespace spidy {
// Overlay vertex: world position in metres, linear RGB, and opacity (the
// overlay blends a vertex below 1 over what is drawn behind it).
struct Vertex {
    Vec3 position, color;
    float alpha = 1;
};
} // namespace spidy
