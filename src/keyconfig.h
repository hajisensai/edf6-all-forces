// The plugins' keys in the game's own key config (keyconfig.cpp; common/edf/keybind.h, docs/keyconfig-re.md §7).
#pragma once
#include "crew.h"

namespace crew {
// The pages 11..16 put into the game's key config (HQ -> settings -> key config, the top list's 「插件」 items): every
// site checked first (keyconfig_signatures.inc), nothing patched on a mismatch; the top list's items added last, so a
// half-patched menu offers no plugin page. True with the pages offered.
bool InstallKeyConfig() noexcept;
// Once a frame (the HQ / room screens, the mission's frame): this plugin's slots seeded from the ini once (the store's
// marker), then the published config's keys made the store's when they changed.
void KeyConfigTick() noexcept;
// The config's keys from the store (LoadConfig, before it publishes): the slots' keys, the Q mark from the stock 「标记」.
// False (the config left as the ini has it) while the store is not built or not seeded yet. `fromIni`: `c` is the ini's
// (LoadConfig), whose NpcMarkKey says whether the Q mark is on at all.
bool OverlayStockKeys(Config& c,bool fromIni) noexcept;
// Whether two configs differ in a key the store holds.
bool StockKeysDiffer(const Config& a,const Config& b) noexcept;
}  // namespace crew
