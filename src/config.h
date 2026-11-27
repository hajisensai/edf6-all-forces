// The plugin's configuration (EDF6VehicleCrew.ini): struct Config, its fields generated into config.inc
// (tools/config_keys.py, tools/gen_config.py), the live snapshot Cfg() and the reload (plugin.cpp).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include "mission_packs.h"

namespace crew {
#include "map_keys.h"

struct Config {
    // The ini's keys (tools/config_keys.py; tools/gen_config.py writes them): members and defaults.
#define EDF6_CONFIG_FIELDS
#include "config.inc"
    // Filled by plugin.cpp LoadConfig itself (no ini key of their own, or a mask of several):
    bool verbose=false;        // Debug=2: the per-object periodic lines too (each NPC soldier's NPCAI line every 2 s)
    // Its hangar modules fitted (bit m: dreadnought_deck.h module m, ini DreadnoughtHangarLeftFore / LeftAft / RightFore /
    // RightAft). The installer builds its model and collision by them; the plugin runs the hangars its model has and only
    // says when these differ (installed before they were changed).
    unsigned dreadnoughtHangars=0xF;
    char testBoardVehicle[32]{};    // tests: class substring, optionally @exactModelBone to distinguish models sharing 506
    wchar_t traceFile[260]{};       // a recording of the vehicles' sounds' inputs and outputs for the offline replay (trace_rec.cpp,
                                    // docs/trace-replay.md): its path, empty off; read at load
    packs::Table missionPacks;       // EDF6VehicleCrew.packs.ini: each installed pack's identity and placeholder rows (mission_packs.h)
};
// Every value is range-checked when the ini is read (plugin.cpp Validate): a value out of range is clamped and
// the change logged.
// The live config: an immutable snapshot, swapped whole by the ini reload (plugin.cpp LoadConfig) and read
// from any thread (game, call picker, HUD draw) without a torn mix of old and new values.
const Config& Cfg() noexcept;
// An extra vehicle fire key's (3..6) binding: VehicleFire<key>Key's virtual key, VehicleFire<key>Button's XInput bits; 0
// none (payload.cpp fires on it, hud.cpp PayloadControls names it).
inline int FireKeyVk(unsigned char key) noexcept {
    const Config& c=Cfg();
    return key==3 ? c.vehicleFire3Key : key==4 ? c.vehicleFire4Key : key==5 ? c.vehicleFire5Key : key==6 ? c.vehicleFire6Key : 0;
}
inline int FireKeyButton(unsigned char key) noexcept {
    const Config& c=Cfg();
    return key==3 ? c.vehicleFire3Button : key==4 ? c.vehicleFire4Button : key==5 ? c.vehicleFire5Button :
           key==6 ? c.vehicleFire6Button : 0;
}
// Between SuppressBump(true) and SuppressBump(false) on this thread, the player's board button takes no
// NPC's seat (heli.cpp PressBoard): an override of the call, not a write to the config. (A pair of calls,
// not a scoped object: the callers run under __try, which allows no destructors.)
void SuppressBump(bool on) noexcept;
bool BumpSuppressed() noexcept;
void ReloadConfigIfChanged() noexcept;
// The ini read again now (the loadout menu saved it: loadout_store.cpp), and its path (Mods/Plugins/EDF6VehicleCrew.ini).
void ReloadConfigNow() noexcept;
const wchar_t* ConfigPath() noexcept;
}  // namespace crew
