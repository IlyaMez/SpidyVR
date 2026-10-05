#pragma once
#include <cstdint>

namespace spidy::bridge {
constexpr uint32_t configMagic = 0x53424346;
constexpr uint32_t controlMagic = 0x53424354;
constexpr uint32_t dataMagic = 0x53424441;
constexpr uint32_t version = 1;
// Codes independently decoded from NxInputImpl::ProcessKeyboardInput. They are
// scan codes, not virtual-key codes. The bit positions are our public bindings.
constexpr uint32_t keyCodes[] = {0x11, 0x1e, 0x1f, 0x20, 0x39, 0x2a, 0x1d, 0x12,
                                 0x13, 0x10, 0x21, 0x01, 0x1c, 0x02, 0x03};
enum Mode : uint32_t { input = 1, cameraOffset = 2 };
struct Config {
    uint32_t magic = configMagic, protocol = version, bytes = sizeof(Config), pid{};
    uint64_t imageBase{}, hero{}, actorRecord{};
};
struct Control {
    uint32_t magic = controlMagic, protocol = version, bytes = sizeof(Control), modes{};
    uint64_t serial{};
    uint32_t leaseMs = 250, keys{};
    float offset[4]{};             // Camera-local meters; bounded to two meters for this experiment.
    float rotation[4]{0, 0, 0, 1}; // Reserved: no unverified orientation writes.
};
struct alignas(8) Data {
    uint32_t magic = dataMagic, protocol = version, bytes = sizeof(Data), state{};
    int64_t sequence{};
    uint64_t cameraCalls{}, matched{}, cameraWrites{}, inputFrames{}, digitalQueries{}, analogQueries{},
        inputServed{}, qpc{}, mover{}, target{}, cameraTransform{}, playerTransform{}, inputSelf{}, caller{};
    uint32_t thread{}, error{};
    float before[16]{}, after[16]{}, player[16]{};
    uint32_t queriedKeys[8]{};
    uint64_t serial{}, expired{};
};
static_assert(sizeof(Config) == 40);
static_assert(sizeof(Control) == 64);
static_assert(sizeof(Data) == 384);
} // namespace spidy::bridge
