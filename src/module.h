// The module registry (docs/module-registry.md): every feature's install, mission reset, per-frame tick and per-vehicle
// step, listed once in src/modules.cpp as its module's entries, in phases (module_logic.h), each under the module's own
// fault id. plugin.cpp's load, mission.cpp's mission start and crew.cpp's frame and vehicle hooks run the passes below
// and list no feature themselves.
#pragma once
#include <cstdint>
#include "module_logic.h"

namespace crew::modules {

// The load's context: what the profile checks found (plugin.cpp EML6_Load), the ini.
struct LoadContext {
    bool heli;                 // the heli profile: the heli pilot's frame, the jets and the carrier flown from it
    bool ground;               // the ground profile
    const wchar_t* iniPath;
};

// A module: its row of kModules. Its fault id is its index (Id); `stateless` is null for a module that keeps something
// across frames (it then has its one reset entry), else why it keeps nothing a mission could leave behind.
enum class Id : std::uint16_t;
struct Module { Id id; const char* name; const char* stateless; };

// The passes' entries, each in its pass's table in run order.
struct InstallEntry { InstallPhase phase; Id id; void (*run)(const LoadContext&) noexcept; };
struct ResetEntry { ResetPhase phase; Id id; void (*run)() noexcept; };
struct TickEntry { TickPhase phase; Id id; void (*run)() noexcept; };
struct VehicleEntry { VehiclePhase phase; Id id; void (*run)(unsigned char* v,int cls) noexcept; std::uint8_t flags; };

void InstallAll(const LoadContext& context) noexcept;   // plugin.cpp EML6_Load, after the profile checks
void ResetAll() noexcept;                                // mission.cpp MissionStart, before the mission's preloads (then ClearFaults)
// The fault policy (module_logic.h FaultStreak, docs/module-registry.md): a module taken off mid-mission has its reset
// entries run at once, each under a guard (its state is what faulted): how many there were, how many faulted.
struct ResetRun { int entries; int faulted; };
ResetRun ResetModule(Id id) noexcept;
void ClearFaults() noexcept;   // every module on again, no fault counted (ResetAll, the mission's start)
// The HUD's notice of a module taken off this mission, shown a moment (hud.cpp ModuleBanner, the draw thread).
bool ModuleNotice(wchar_t* out,std::size_t capacity) noexcept;
void RunTicks() noexcept;                                // crew.cpp SharedFrameTick, once a game frame
// crew.cpp InputHook, at the hook's seam `phase`: the phase's entries for vehicle `v` of class `cls`; with `enabled`
// false (ini Enabled=0) only those marked kRunsWhenDisabled.
void RunVehicle(VehiclePhase phase,unsigned char* v,int cls,bool enabled) noexcept;
// The vehicles' sounds and their recording at one "now" (vehsound.cpp stepMs), read once by InputHook before the
// kSound phase: the recording's time is the very one the step ran at.
void SoundNow(std::uint64_t ms) noexcept;

}  // namespace crew::modules
