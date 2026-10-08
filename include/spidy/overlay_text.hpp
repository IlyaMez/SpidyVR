#pragma once
#include "vertex.hpp"
#include <string_view>
#include <vector>

namespace spidy {
// Text over the eye images, in a stroke font of capital letters, digits and
// some punctuation (lower case is drawn in capitals, anything else as a
// space). Each stroke is a flat bar in the plane the text stands in.
struct TextStyle {
    float height = .04f; // capital height, metres
    float weight = .12f; // stroke width, a share of the height
    Vec3 color{1, 1, 1}; // linear RGB, as the overlay's vertices take it
};
// Whether the font has a glyph for `c` (either case for letters).
bool drawable(char c);
// How wide `text` is at capital height `height`, metres.
float textWidth(std::string_view text, float height);
// The largest height up to `height` at which `text` is at most `width` wide.
float fittedHeight(std::string_view text, float height, float width);
// Appends `text` standing on its baseline from `origin` along unit `right`,
// its letters up along unit `up` (square to `right`).
void appendText(std::vector<Vertex>& out, std::string_view text, Vec3 origin, Vec3 right, Vec3 up,
                const TextStyle& style = {});
} // namespace spidy
