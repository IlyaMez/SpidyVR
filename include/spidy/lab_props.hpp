#pragma once
#include "web_grab.hpp"
#include <span>

namespace spidy {
// Crates, barrels and thugs for the lab: web grab targets on simple sphere
// physics against the lab's boxes. A thug stands still until a web pulls it
// or something hits it hard, then tumbles, lies still, and gets up again.
struct LabProp {
    enum class Stance : std::uint8_t { Standing, Tumbling, Rising };
    std::uint64_t id{};
    TargetKind kind = TargetKind::Object;
    Vec3 position{}, velocity{};
    Vec3 half{}, color{}; // drawn half extents and colour of an object
    float mass = 20, radius = .4f;
    Quat orientation{}; // drawing only
    Vec3 spin{};        // radians per second, world axes; drawing only
    Stance stance{};
    float still{}, rise{}; // seconds lying at rest; 0..1 of getting up
    bool grounded{};
    Vec3 lying{}; // where a rising character got up from
};
class LabProps final : public TargetQueries {
  public:
    // A thug's centre stands this high above its feet.
    static constexpr float standingHeight = .95f;
    LabProps(); // the lab's own arrangement around the launch rooftop
    explicit LabProps(std::vector<LabProp> props);
    std::optional<GrabTarget> pick(Vec3 origin, Vec3 direction, float distance, float cone) const override;
    std::optional<GrabTarget> find(std::uint64_t id) const override;
    void characters(std::vector<GrabTarget>& out) const override;
    // One physics step: a commanded prop moves as the command's law gives
    // it, the rest fall; all collide with the world and with each other.
    void step(float dt, std::span<const TargetCommand> commands, const World& world);
    // The web grab and the props together, at the grab's fixed step.
    void advance(float seconds, WebGrab& grab, const World& world);
    void reset();
    const std::vector<LabProp>& props() const {
        return props_;
    }
    // Standing characters knocked over since the last reset.
    unsigned knockdowns() const {
        return knockdowns_;
    }

  private:
    void knock(LabProp& prop);
    std::vector<LabProp> props_, initial_;
    std::vector<TargetCommand> commands_;
    double accumulator_{};
    unsigned knockdowns_{};
};
} // namespace spidy
