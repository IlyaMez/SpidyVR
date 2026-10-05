#include "spidy/swing.hpp"
#include <iostream>
using namespace spidy;
class PendulumWorld final : public World {
  public:
    std::optional<RayHit> raycast(Vec3 o, Vec3 d, float range) const override {
        const Vec3 anchor{0, 20, 0};
        const float t = dot(anchor - o, d);
        if (t > 0 && t <= range && length(o + d * t - anchor) < .01f)
            return RayHit{anchor, {}, 1, true};
        return {};
    }
    MoveResult sweep(Vec3, Vec3 to, float) const override {
        return {to, {}, {}};
    }
    bool exists(std::uint64_t id) const override {
        return id == 1;
    }
};
int main() {
    PendulumWorld world;
    Swing s;
    s.reset({{0, 0, 0}, {9, 0, 0}, false});
    Input in;
    in.hands[0] = {{{0, 0, 0}, {.70710678f, 0, 0, .70710678f}}, {0, 0, 0}, true, 1, 1};
    std::cout << "seconds,x,y,z,speed,rope_length\n";
    for (int f = 0; f < 900; ++f) {
        if (f == 450)
            in.hands[0].grip = 0;
        s.update(1.f / 90, in, world);
        if (f % 9 == 0) {
            const auto& b = s.body();
            std::cout << f / 90.f << ',' << b.position.x << ',' << b.position.y << ',' << b.position.z << ','
                      << length(b.velocity) << ',' << s.webs()[0].length << '\n';
        }
    }
}
