// The tables the game indexes by the soldier type (slot 83) in battle, made to hold types 4 and 5 (docs/newclass-re/inject.md
// §2, §3, §6; plan §1, §2 B):
//  - the HUD nameplate table 0x806FE0 (HUD+0xB0): reserved and resized for 6 entries instead of 4, entries 4 / 5 filled after the
//    stock fill (V orange, S blue-purple), so the HUD's read of entry [type] stays inside the table;
//  - the damage number colour 0x81F6F0: types 4 / 5 read the plugin's rows instead of the in-vehicle row (4) and past the table (5);
//  - the script boundary: GetPlayerSoldierType 0x1B5030 and the old BVM native 1005 0x222740 see ScriptType(type) (0 for both).
// The key config set is no type-indexed table: it is soldier+0xD40, written by each class's constructor (AssultSoldier's: 0), so
// the new classes use the Ranger's keys (tests/soldier_native_audit.py).
#pragma once

namespace crew {
bool InstallSoldierTypePatches() noexcept;   // module "soldier type patches" (src/module_table.h), the kFeatures phase
}
