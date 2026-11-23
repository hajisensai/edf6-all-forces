// 整备方案 at run time (docs/plan-2026-11-24-ui-rewrite.md stage 1; the pure part is loadout_preset.h, the player's slot
// rules player_slots.h): the book file beside the plugin's ini, EDF6VehicleCrew.presets.ini, read once per open and
// written atomically (loadout_file.h ReplaceFileAtomic) after every change, and a preset switched in:
//   - the support keys of EDF6VehicleCrew.ini made exactly the preset's (loadout_preset.h SwitchEdits) by one
//     loadout_store.h SaveSupportLoadouts (all or none, each value checked by the plugin's parsers, the config reloaded),
//     then the variant generator started (StartVariantGeneration); room_sync.cpp encodes SupportCfg() on its next tick,
//     so the room hears the new loadout without a call from here;
//   - the player's class and weapons written into GameStatus +0x6E90 / +0x6E98 (docs/loadout-re.md §1): only rows the
//     installed weapon table has by that name (0xE1BA0), the player owns (record flags bit 0) and the slot takes
//     (player_slots.h). The UI thread only, where the stock weapon select writes them (docs/armory-re.md §4).
// Every call: the UI thread (room_ui.cpp's screen frames, through armory.cpp).
#pragma once
#include "armory_logic.h"
#include "loadout_preset.h"
#include <cstddef>

namespace crew::armorypresets {

// The book read again from its file (absent: an empty book, not an error). False: unreadable or malformed (*why says,
// logged): the book is empty and every change refused until a later Load succeeds, so a file we could not read is
// never written over.
bool Load(wchar_t* why,std::size_t cap) noexcept;
// The book's chips for armory_logic.h (names UTF-16, the current one).
void Fill(armory::Presets* out) noexcept;
// One preset operation the screen asked for (armory::Effects op / index / name). False, *why set, when refused or when
// it failed; what was not done is left as it was (a failed switch writes neither the ini nor the book).
bool Apply(const armory::Effects& fx,wchar_t* why,std::size_t cap) noexcept;
// The player's class and every slot's weapon now, by row name. False: no game status or weapon table yet.
bool ReadPlayer(preset::Player* out) noexcept;

}  // namespace crew::armorypresets
