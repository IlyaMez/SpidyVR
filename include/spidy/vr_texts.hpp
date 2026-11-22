#pragma once
#include "prompt_buttons.hpp"
#include <cstdint>
#include <span>
#include <string_view>

// The game's texts in VR wording (November 22). The game teaches its controls in its own words: help lines in
// play ("Hold [BTN_LT] and tap [BTN_RB] to fire webs and seal the leaks"), hint cards, loading tips, the Moves
// List and the skills, and its HUD's prompts and hint cards name buttons by their Xbox icons. A VR player has no
// bumpers, a D-pad or the stick clicks those name, so each such text has a VR wording here, its buttons in plain
// text and the motion said where VR plays it its own way ("Hold your left trigger and pull your right one to fire
// webs and seal the leaks", "Raise a hand beside your head and pull its trigger to scan", "Y: Interact"), which the
// game gets in place of its own as it looks the text up (game_prompts::tableText).
//
// Every text of the game is looked up by its tag's hash through one function (0x1748d90, under both the lookup by
// hash 0x1749970 and the one by tag 0x1749a20; docs/REFERENCE.md), and the table keeps the hash with the English
// words the VR wording was written for: a text is replaced only where the game's words are those, so another
// language, or a game update that changed a line, keeps the game's own.
//
// The words are src/vr_texts.tsv; tools/game_texts.py writes them with each tag's hash and English text into
// src/vr_texts_table.inc.
namespace spidy::vr_texts {
struct Entry {
    // The hash the game's lookup takes, the tag (for reports and checks), the game's English text and what a VR
    // player reads instead. Both texts are the game's markup: [BTN_A] and its other tokens draw its icons.
    uint32_t hash;
    std::string_view tag, english, vr;
};
// Every entry, in the order of their hashes.
std::span<const Entry> all();
// The entry for `hash`; null for a text with no VR wording.
const Entry* find(uint32_t hash);
// The VR wording of text `hash` whose words are `original`; null where there is none, or where the words are not
// the English ones it was written for.
const char* forText(uint32_t hash, std::string_view original);
// What a text the game shows asks for, where that text is a VR wording (by its pointer, the table's own string, or
// by its words where the game copied them on): the buttons the game's own English words named, all of them where
// one wording stands for several texts (the gadget wheel's variants name LB, RB or LB as a toggle; the VR player
// holds X for each). False for any other text. game_prompts reads a prompt's demand from this, so a line in VR
// wording still lets the VR controls send what the game waits for (game_pad::NamedPress).
bool asked(const char* shown, std::string_view words, prompt_buttons::Demand& out);
// The [TOKEN]s a VR wording may hold: only the game's pictures that are no button (its marks and gadgets). A
// wording writes its buttons out in plain text ("Press A", "your right trigger"): the game's icons are an Xbox
// controller's, which the VR controllers only partly share.
bool tokenAllowed(std::string_view token);
} // namespace spidy::vr_texts
