#pragma once
#include "swing.hpp"
#include <utility>
namespace spidy {
struct Box {
    Vec3 min, max, color;
    std::uint64_t id;
};
class LabWorld final : public World {
  public:
    LabWorld();
    explicit LabWorld(std::vector<Box> boxes) : boxes_(std::move(boxes)) {}
    std::optional<RayHit> raycast(Vec3, Vec3, float) const override;
    MoveResult sweep(Vec3, Vec3, float) const override;
    bool exists(std::uint64_t) const override;
    const std::vector<Box>& boxes() const {
        return boxes_;
    }

  private:
    std::vector<Box> boxes_;
};
} // namespace spidy
