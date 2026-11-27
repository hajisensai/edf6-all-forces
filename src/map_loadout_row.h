#pragma once
// The M map's loadout panel's rows (map_loadout.h builds them): the types alone, for the readout the HUD draws
// (mapcmd.h MapLoadoutReadout) without the loadout grammar's headers.
#include <cstdint>

namespace crew::maploadout {

inline constexpr int kRowsMost=8;            // stock, then up to seven loadouts
inline constexpr int kValueMost=96;          // a SupportVehicle_<KEY> value (five pylons "AAM_M:40," at most)
inline constexpr int kFileMost=64;           // EDF6VC_LO_RINTERCEPTOR_<16 hex>.SGO

// A row's state: the stock load; a variant ready this mission (preloaded here, online every peer has it too); on the
// disk but not preloaded this mission (the next one's); a peer without its file (the host's plan flies stock: the
// existing fallback, support_dispatch.cpp DecideVariant).
enum class State : std::uint8_t { stock, ready, next, peerMissing };
struct Row {
    wchar_t value[kValueMost];   // the ini value (empty: stock)
    wchar_t file[kFileMost];     // its variant file (empty: stock)
    State state;
    bool current;                // what SupportVehicle_<KEY> is now
};

}  // namespace crew::maploadout
