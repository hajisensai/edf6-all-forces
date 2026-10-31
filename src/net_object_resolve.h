#pragma once
// Object references (net_object_ref.h) in this machine's world: making one for an object, finding the object one names.
// Game thread only, outside plugin state locks (one native all-team walk, 5E0C80). Every channel that names an object on
// the wire goes through these (tests/player_identity_guard.py holds it): the players by their mission slot, the rest by
// their NetId, so a target that is a player (a teammate grabbed by the gravity gun, marked, attacked by a controlled enemy)
// is found on every machine, and a player that died is still named (a show its caller started plays on).
// Returned references and PUIDs are borrowed for immediate synchronous use, never retained or queued.
#include "crew.h"
#include "net_object_ref.h"
#include <cstdint>

namespace crew {
// What a reference must name to be found: a live object, or one present (not deleted, dead or alive: a corpse thrown about,
// a player who fell during the show it called).
enum class NetLife : std::uint8_t { live, present };
struct NetResolved {
    ObjRef obj{};          // the object (empty: not found)
    int slot=-1;           // a player reference's slot, found or not (-1: not a player's)
    void* puid=nullptr;    // a found player's native PUID (its User+0x18)
    bool dead=false;       // found, and dead (only ever with NetLife::present)
    bool found=false;
    bool ambiguous=false;  // not found because two objects answer it (two live players of one slot): not "gone"
};
constexpr unsigned kMaxNetRefs=18;   // a map command's requester, 16 units and its focus

// The reference of `object` as every machine finds it: a player (one of this mission's, its User bound) by its slot, dead or
// alive, scene-deleted or not; anything else by its native NetId. What it is called only: whether it is still found is the
// receiver's NetLife. False (and `out` zero) for a stale weak reference, a player soldier with no mission slot (no User
// bound), or an object with no NetId.
bool EncodeNetRef(const ObjRef& object,unsigned char out[32]) noexcept;
bool EncodeNetRef(const void* object,unsigned char out[32]) noexcept;   // the object's own current reference
// `n` references (each null or all zero: none, never found) in one walk, each to `lives[i]`. A player's: its one live
// player of that slot; with none live and NetLife::present, its one present player (dead=true); two live of one slot, or
// two present and none live, are none (`ambiguous`). A NetId's: the one object of it live (or present), never a player
// soldier (a player only by its slot). Not found leaves `out[i].found` false (a player's slot still told). Player references
// alone reuse the frame's (GameFrame) table of the mission's players, each entry rechecked, so a per-frame lookup costs no
// walk. False only when the walk could not run (no world, an invalid reference, n too many):
// every `out` cleared.
bool ResolveNetRefs(const unsigned char* const* refs,const NetLife* lives,unsigned n,NetResolved* out) noexcept;
bool ResolveNetRef(const unsigned char* ref,NetLife life,NetResolved* out) noexcept;
// After the walk (the native registry may have changed under a game operation since): the object `r` found for `ref` is
// still it (its weak reference, its NetId or its player's slot and PUID), and still to `life`.
bool StillNetRef(const unsigned char* ref,NetLife life,const NetResolved& r) noexcept;
// Whether the authenticated transport sender `authenticatedPuid` is mission player `slot`'s machine: EDF6Coop maps the slot
// to its member (EDF6Coop_ResolveMissionPlayerPuid). For a message about a player that may not be in this world (any more).
bool SenderOwnsSlot(int slot,const char* authenticatedPuid) noexcept;
// SenderOwns (command_identity.h): the sender check on a resolved player reference.
}
