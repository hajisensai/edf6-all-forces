#pragma once
#include "crew.h"

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
// IDs must be nonzero canonical NativeNetId32 (bytes 20..23 zero), mutually distinct.
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
struct MarkIdentities {
    ObjRef player{};
    int slot=-1;
    void* puid=nullptr;
    ObjRef target{};
};
bool ResolveMarkIdentities(const unsigned char marker[32],const unsigned char target[32],MarkIdentities* out) noexcept;
}
