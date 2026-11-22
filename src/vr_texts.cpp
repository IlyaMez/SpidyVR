// The game's texts in VR wording (vr_texts.hpp).
#include "spidy/vr_texts.hpp"
#include <algorithm>
#include <iterator>

namespace spidy::vr_texts {
namespace {
constexpr Entry entries[] = {
#include "vr_texts_table.inc"
};
constexpr bool sorted() {
    for (size_t i = 1; i < std::size(entries); ++i)
        if (entries[i - 1].hash >= entries[i].hash)
            return false;
    return true;
}
static_assert(sorted(), "vr_texts_table.inc: hashes in order, each once (tools/game_texts.py table)");
} // namespace

std::span<const Entry> all() {
    return entries;
}

const Entry* find(uint32_t hash) {
    const auto at = std::lower_bound(std::begin(entries), std::end(entries), hash,
                                     [](const Entry& e, uint32_t h) { return e.hash < h; });
    return at != std::end(entries) && at->hash == hash ? at : nullptr;
}

const char* forText(uint32_t hash, std::string_view original) {
    const Entry* e = find(hash);
    return e && e->english == original ? e->vr.data() : nullptr;
}

bool asked(const char* shown, std::string_view words, prompt_buttons::Demand& out) {
    const Entry* wording = nullptr;
    for (const auto& e : entries)
        if ((shown && shown == e.vr.data()) || (!words.empty() && e.vr == words)) {
            wording = &e;
            break;
        }
    if (!wording)
        return false;
    out = {};
    for (const auto& e : entries) {
        if (e.vr != wording->vr)
            continue;
        const auto d = prompt_buttons::forText(e.english);
        out.buttons |= d.buttons;
        out.leftTrigger |= d.leftTrigger;
        out.rightTrigger |= d.rightTrigger;
        out.hold |= d.hold;
        out.leftX = out.leftX ? out.leftX : d.leftX;
        out.leftY = out.leftY ? out.leftY : d.leftY;
        out.rightX = out.rightX ? out.rightX : d.rightX;
        out.rightY = out.rightY ? out.rightY : d.rightY;
        out.leftStickMotion |= d.leftStickMotion;
        out.rightStickMotion |= d.rightStickMotion;
        out.keyboard |= d.keyboard;
    }
    return true;
}

bool tokenAllowed(std::string_view token) {
    constexpr std::string_view allowed[] = {
        "[AWARE_ICON]",    "[ALERT]",          "[INVESTIGATE_ICON]", "[BLACKCAT_ICON]", "[GADGET_BLAST]",
        "[GADGET_MATRIX]", "[GADGET_ELECTRIC]", "[GADGET_IMPACT]",   "[GADGET_BOMB]",   "[GADGET_DRONE]",
        "[GADGET_TRIPMINE]",
    };
    return std::find(std::begin(allowed), std::end(allowed), token) != std::end(allowed);
}
} // namespace spidy::vr_texts
