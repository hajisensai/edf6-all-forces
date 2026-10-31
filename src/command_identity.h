#pragma once
#include "crew.h"
#include "support_net.h"

namespace crew {
constexpr unsigned kMaxCommandIdentities=16;
struct CommandIdentities {
    ObjRef requester{};
    ObjRef units[kMaxCommandIdentities]{};
    unsigned count=0;
    ObjRef focus{};
    void* requesterPuid=nullptr;
};
// Game thread only, outside plugin state locks. One native all-team walk; no local-AI ledger.
// The requester is a player requester (player_requester.h: its mission slot; the one live player of that slot, never
// found by a NetId, which a stock mission numbers per machine); units and focus are nonzero canonical NativeNetId32 (bytes
// 20..23 zero), mutually distinct and none of them the requester.
// count may be zero; focus==nullptr means absent. All matched actors must be alive/current.
// Output references/PUID are borrowed for immediate synchronous use, never retained or queued.
// Caller must convert requesterPuid using its PuidText callback and compare the authenticated
// sender's expected 65-byte text before dispatch. This helper does not authenticate a sender.
// Failure clears *out; no world, player fields, network state or reference counts are written.
bool ResolveCommandIdentities(const unsigned char requester[32],const unsigned char units[][32],
                              unsigned count,const unsigned char focus[32],CommandIdentities* out) noexcept;
// The Q mark's (qmark.cpp): one native all-team walk for a mark's marker (a live player: its ObjRef, its mission player
// slot and its native PUID, borrowed as above) and its target. Either ID may be all zero (not asked) or not found (its
// ObjRef left empty): only a walk that could not run fails. Game thread only; nothing written.
// The marker is a player requester (player_requester.h): the live player of that mission slot, whatever its NetId (none
// when two players hold the slot). A native NetId names no player (a stock mission numbers the players' per machine:
// every channel speaks of a player by its slot, 2026-10-31).
struct MarkIdentities {
    ObjRef player{};
    int slot=-1;
    void* puid=nullptr;
    ObjRef target{};
};
bool ResolveMarkIdentities(const unsigned char marker[32],const unsigned char target[32],MarkIdentities* out) noexcept;
// The gravity operations' (gravity_net.cpp): as ResolveMarkIdentities, but the target may be dead (a corpse is still
// thrown about), only not deleted.
bool ResolveGrabIdentities(const unsigned char grabber[32],const unsigned char target[32],MarkIdentities* out) noexcept;
// A live player's requester identity (player_requester.h: its mission slot), what every channel sends for a player.
// False (and `out` zero) for anything else.
bool PlayerRequesterOf(const ObjRef& who,unsigned char out[32]) noexcept;
bool PlayerRequesterOf(const void* object,unsigned char out[32]) noexcept;   // the object's own current reference
}
namespace crew {
// The sender check of every plugin channel (plugin_net.h) for a message that speaks for a player: `nativePuid` (a resolved
// player's, ResolveCommandIdentities' requesterPuid / ResolveMarkIdentities' puid) is the authenticated transport sender's
// own PUID, and one of the sealed current world's participants (support_net.cpp SupportCommandRequesterMatches). A peer can
// never speak for another machine's player.
inline bool SenderIsPlayer(void* nativePuid,const char* authenticatedPuid) noexcept {
    return nativePuid && authenticatedPuid && SupportCommandRequesterMatches(nativePuid,authenticatedPuid);
}
// The same from the player's requester identity (PlayerRequesterOf): it resolves (ResolveMarkIdentities; `deadOk`: ResolveGrabIdentities, a body too) to
// a player of this world whose PUID passes the check above. `out` (optional): the identities found, cleared on failure.
inline bool SenderIsPlayer(const unsigned char playerId[32],const char* authenticatedPuid,MarkIdentities* out=nullptr,
                           bool deadOk=false) noexcept {
    MarkIdentities ids{};
    const bool found=playerId && (deadOk ? ResolveGrabIdentities(playerId,nullptr,&ids) : ResolveMarkIdentities(playerId,nullptr,&ids)) &&
        ids.player && SenderIsPlayer(ids.puid,authenticatedPuid);
    if(out)*out=found ? ids : MarkIdentities{};
    return found;
}
}
