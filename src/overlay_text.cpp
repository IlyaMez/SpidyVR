#include "spidy/overlay_text.hpp"
#include <algorithm>
#include <cctype>

namespace spidy {
namespace {
// A glyph in half units of a cell 4 wide and 6 tall (the capital height):
// its width, and its strokes, each a run of points of two characters, x
// (0-8) then y (hex, 0-c), the runs separated by spaces. Corners are cut at
// 45 degrees instead of rounded, so every stroke is a straight bar.
struct Glyph {
    char c;
    unsigned char width;
    const char* strokes;
};
constexpr Glyph glyphs[] = {
    {'A', 8, "000a2c6c8a80 0585"},
    {'B', 8, "0666848260000c6c8a8866"},
    {'C', 8, "826020020a2c6c8a"},
    {'D', 8, "000c5c89835000"},
    {'E', 8, "80000c8c 0666"},
    {'F', 8, "000c8c 0666"},
    {'G', 8, "8a6c2c0a022060828545"},
    {'H', 8, "000c 808c 0686"},
    {'I', 4, "0040 202c 0c4c"},
    {'J', 8, "4c8c8260200204"},
    {'K', 8, "000c 8c04 3780"},
    {'L', 8, "0c0080"},
    {'M', 8, "000c468c80"},
    {'N', 8, "000c808c"},
    {'O', 8, "2060828a6c2c0a0220"},
    {'P', 8, "000c6c8a886606"},
    {'Q', 8, "2060828a6c2c0a0220 5380"},
    {'R', 8, "000c6c8a886606 4680"},
    {'S', 8, "02206082846626080a2c6c8a"},
    {'T', 8, "0c8c 4c40"},
    {'U', 8, "0c022060828c"},
    {'V', 8, "0c408c"},
    {'W', 8, "0c2047608c"},
    {'X', 8, "008c 0c80"},
    {'Y', 8, "0c468c 4640"},
    {'Z', 8, "0c8c0080"},
    // A zero is slashed, unlike the letter O.
    {'0', 8, "2060828a6c2c0a0220 0a82"},
    {'1', 6, "2a4c40 2060"},
    {'2', 8, "0a2c6c8a880080"},
    {'3', 8, "0a2c6c8a886636 668482602002"},
    {'4', 8, "606c0383"},
    {'5', 8, "8c0c07678582602002"},
    {'6', 8, "8a6c2c0a02206082856707"},
    {'7', 8, "0c8c30"},
    {'8', 8, "26080a2c6c8a88662604022060828466"},
    {'9', 8, "022060828a6c2c0a072585"},
    {'.', 2, "1011"},
    {',', 2, "1200"},
    {':', 2, "1112 1718"},
    {'-', 8, "1676"},
    {'\'', 2, "1c19"},
    {'%', 8, "008c 0c2c2a0a0c 6282806062"},
    {'/', 8, "008c"},
    {'(', 4, "3c1a1230"},
    {')', 4, "1c3a3210"},
    {'!', 2, "1c14 1110"},
    {'?', 8, "0a2c6c8a884543 4140"},
    {'+', 8, "424a 0686"},
    {'=', 8, "0484 0888"},
    {'<', 8, "8c0680"},
    {'>', 8, "0c8600"},
};
constexpr unsigned char spaceWidth = 4, spacing = 3; // half units
const Glyph* glyph(char c) {
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const auto& g : glyphs)
        if (g.c == upper)
            return &g;
    return nullptr;
}
float hexDigit(char c) {
    return static_cast<float>(c <= '9' ? c - '0' : c - 'a' + 10);
}
void bar(std::vector<Vertex>& out, Vec3 p, Vec3 q, Vec3 normal, float half, Vec3 color) {
    const Vec3 along = normalized(q - p) * half, side = normalized(cross(normal, q - p)) * half;
    if (length(along) < half * .5f)
        return;
    // Squared off half the width past each end, so two bars close a corner.
    const Vertex a{p - along - side, color}, b{q + along - side, color}, c{q + along + side, color},
        d{p - along + side, color};
    out.insert(out.end(), {a, b, c, a, c, d});
}
} // namespace

bool drawable(char c) {
    return glyph(c) != nullptr;
}
float textWidth(std::string_view text, float height) {
    unsigned halves = 0;
    for (const char c : text) {
        const auto* g = glyph(c);
        halves += static_cast<unsigned>((g ? g->width : spaceWidth) + spacing);
    }
    return text.empty() ? 0.f : static_cast<float>(halves - spacing) * height / 12;
}
float fittedHeight(std::string_view text, float height, float width) {
    const float wide = textWidth(text, height);
    return wide > width && wide > 0 ? height * width / wide : height;
}
void appendText(std::vector<Vertex>& out, std::string_view text, Vec3 origin, Vec3 right, Vec3 up,
                const TextStyle& style) {
    const float unit = style.height / 12, half = style.height * style.weight / 2;
    const Vec3 normal = cross(right, up);
    if (!finite(origin) || length(normal) < .5f || !(unit > 0) || !(half > 0))
        return;
    float x = 0;
    for (const char c : text) {
        const auto* g = glyph(c);
        if (g) {
            const auto at = [&](const char* point) {
                return origin + right * ((x + hexDigit(point[0])) * unit) + up * (hexDigit(point[1]) * unit);
            };
            for (const char* run = g->strokes; *run;) {
                const char* end = run;
                while (*end && *end != ' ')
                    ++end;
                for (const char* p = run; p + 3 < end; p += 2)
                    bar(out, at(p), at(p + 2), normal, half, style.color);
                run = *end ? end + 1 : end;
            }
        }
        x += static_cast<float>((g ? g->width : spaceWidth) + spacing);
    }
}
} // namespace spidy
