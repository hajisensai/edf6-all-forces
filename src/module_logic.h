// The module registry's pure part (module.h the game's; tests/module_registry_test.cpp runs this): the passes' phases,
// the registry's rules as constexpr checks (src/modules.cpp static_asserts them on the real tables), and the fault policy.
//
// A module is one feature's unit of the plugin's lifecycle: one row of modules.cpp kModules, with its name and its fault
// id (the row), and its entries in the four passes:
//  - install: once at load (plugin.cpp EML6_Load), in InstallPhase order;
//  - reset: at each mission's start (mission.cpp MissionStart), in ResetPhase order;
//  - tick: once a game frame (crew.cpp SharedFrameTick), in TickPhase order;
//  - vehicle: each vehicle's input, each frame (crew.cpp InputHook), in VehiclePhase order; with the plugin off
//    (ini Enabled=0) only the entries marked kRunsWhenDisabled run.
// Within a phase a pass runs its entries in their table's order. The phases are the seams the passes have (what must
// be done before what), named, so a new entry says where it goes, not a comment beside a call.
//
// A module that keeps anything across frames has a reset entry: what it kept of the last mission's objects is dropped
// before the new mission's exist. A module that keeps nothing says why in its row (`stateless`); the checks below refuse a
// module that does neither, so a forgotten reset no longer carries a mission's state into the next one unseen.
#pragma once
#include <cstddef>
#include <cstdint>

namespace crew::modules {

// The load's installs (plugin.cpp EML6_Load, after the profile checks).
enum class InstallPhase : std::uint8_t {
    kCrashDump,          // first: a crash in any install after it is dumped (its filter and handlers point into this DLL)
    kBeforeGameStatus,   // before anything could make the game status (weapon_records.cpp enlarges its allocation)
    kBeforeAppInit,      // before the application's init makes the device, CRI File System, the team manager, Havok
    kPhysics,            // the physics hooks the jets, the carrier and the player's jets build on (the one 506 hook)
    kHeliProfile,        // flown from the heli pilot's frame: installed only with the heli profile (plugin.cpp CheckHeliProfile)
    kFeatures,           // the features' own hooks (their order among themselves: the table's)
    kMission,            // the mission's start and the per-frame hooks it triggers; the crews' seat hook
    kLast,               // the probes that chain call sites other hooks redirect at load
};
// The mission's start (mission.cpp MissionStart), before the mission's preloads.
enum class ResetPhase : std::uint8_t {
    kReleaseHolds,   // what the plugin holds of the game's objects let go first: the controlled enemies' leases before
                     // the native input is restored, the PvP teams after them (their identities)
    kMissionState,   // every module's per-mission tables
};
// Once a game frame (crew.cpp SharedFrameTick).
enum class TickPhase : std::uint8_t {
    kWorld,              // the player's own surroundings (under the ground, off a vehicle, the sea rescue)
    kBeforeHudPublish,   // what the HUD's publish carries (the warnings, the thermal view, PvP, the controlled enemies)
    kHudPublish,         // the HUD's publish of what it gathered
    kAfterHudPublish,    // the rest (the world's edges, the supports, the sounds, the queues)
};
// Each vehicle's input (crew.cpp InputHook): the seams of the hook.
enum class VehiclePhase : std::uint8_t {
    kBeforeGears,        // before the stock input reads the seats: the triggers taken, the NPC sticks written
    kBeforeStockInput,   // after the player's gears (drivegear.cpp), last before the stock input
    kAfterStockInput,    // after the stock input and the config's reload
    kSound,              // the vehicles' sounds and their recording, at one "now" (vehsound.cpp stepMs)
    kAfterSound,         // after the recording's end (trace_rec.cpp TraceEnd)
    kFrame,              // after the shared frame tick (the plugin on): the crews, the aircraft, the HUD's data
};

// An entry's flags.
enum Flags : std::uint8_t {
    kNone=0,
    kRunsWhenDisabled=1,   // vehicle pass: runs with the plugin off too (it gives back what it changed, or it is a clock)
};

// The registry's rules over a table of rows (`.id`, `.stateless`) and the passes' entries (`.id`, `.phase`).
template<class Row,std::size_t N> constexpr bool RowsInIdOrder(const Row (&rows)[N]) noexcept {
    for(std::size_t i=0;i<N;++i)if(static_cast<std::size_t>(rows[i].id)!=i || !rows[i].name || !rows[i].name[0])return false;
    return true;
}
template<class Entry,std::size_t N> constexpr bool PhasesInOrder(const Entry (&entries)[N]) noexcept {
    for(std::size_t i=1;i<N;++i)if(entries[i].phase<entries[i-1].phase)return false;
    return true;
}
template<class Entry,std::size_t N> constexpr bool Has(const Entry (&entries)[N],std::size_t id) noexcept {
    for(std::size_t i=0;i<N;++i)if(static_cast<std::size_t>(entries[i].id)==id)return true;
    return false;
}
template<class Entry,std::size_t N> constexpr std::size_t CountOf(const Entry (&entries)[N],std::size_t id) noexcept {
    std::size_t n=0;
    for(std::size_t i=0;i<N;++i)if(static_cast<std::size_t>(entries[i].id)==id)++n;
    return n;
}
// Every entry names a row; every row is in some pass.
template<class Entry,std::size_t N> constexpr bool IdsInRange(const Entry (&entries)[N],std::size_t rows) noexcept {
    for(std::size_t i=0;i<N;++i)if(static_cast<std::size_t>(entries[i].id)>=rows || !entries[i].run)return false;
    return true;
}
// The first row that breaks the reset rule (rows: none): a stateful one (no `stateless` reason) with no reset entry, a
// stateless one with a reset entry or an empty reason, or one with more than one reset entry.
template<class Row,std::size_t N,class Reset,std::size_t R>
constexpr std::size_t FirstResetBreak(const Row (&rows)[N],const Reset (&resets)[R]) noexcept {
    for(std::size_t i=0;i<N;++i) {
        const std::size_t n=CountOf(resets,i);
        const char* why=rows[i].stateless;
        if(why ? (n!=0 || !why[0]) : n!=1)return i;
    }
    return N;
}
// The first row in no pass at all (rows: none).
template<class Row,std::size_t N,class I,std::size_t A,class S,std::size_t B,class T,std::size_t C,class V,std::size_t D>
constexpr std::size_t FirstUnused(const Row (&)[N],const I (&installs)[A],const S (&resets)[B],const T (&ticks)[C],
                                  const V (&steps)[D]) noexcept {
    for(std::size_t i=0;i<N;++i)if(!Has(installs,i) && !Has(resets,i) && !Has(ticks,i) && !Has(steps,i))return i;
    return N;
}
}  // namespace crew::modules
