// transport.cpp: the squads' transports, Broken Arrow's way (transport_logic.h has the rules and the user's words). A squad
// paired with a vehicle (a truck, an APC, a transport helicopter) rides it to a far point order and gets off short of
// it; its transport waits there and leaves on WITHDRAW. A transport plane drops its soldiers over a point.
// The pairs and the trips are the game thread's, kept where the squad's AI runs (online: its authority, the host for the
// plugin's support soldiers); a new mission drops them (ResetTransports).
#pragma once
#include "mapcmd_logic.h"
#include "npc_command.h"

namespace crew {
struct ObjRef;
// A squad (its top NPC) paired with `vehicle`: a helicopter or a ground vehicle an NPC can drive (npcpost.cpp NpcDrivable),
// friendly. Its earlier pair (and that pair's place in a trip) is let go. False: not a transport.
bool TransportPair(const void* top,const void* vehicle) noexcept;
// The vehicle a squad is paired with (live), else nullptr.
const void* TransportOf(const void* top) noexcept;
// Whether `vehicle` carries a paired squad aboard now: the first such squad's top (the map's order to the vehicle goes to
// its trip), else nullptr.
const void* TransportRiderOf(const void* vehicle) noexcept;
// A point order for a paired squad (npcai.cpp NpcSquadCommandForRequester): true when its transport takes it (a trip begun
// or joined; the squad's own order is held until it gets off), false when it walks (not paired, near enough on foot, its
// vehicle busy elsewhere or gone).
bool TransportOrder(const void* top,const mapcmd::Command& c) noexcept;
// Any other order for the squad: its place in a trip is let go (it stays where it is, aboard or not).
void TransportCancel(const void* top) noexcept;
// WITHDRAW: the squad's transport leaves the field (support_dispatch.cpp SupportWithdrawVehicle: a support vehicle out of
// the field is removed with its own crew). With the squad aboard a vehicle at rest it gets off first. none, or why not.
NpcCommandReason TransportWithdraw(const void* top) noexcept;
// npcai.cpp: a squad's top died and `lead` leads it now (nullptr: it joined another squad, its pair goes).
void TransportSucceed(const void* dead,const void* lead) noexcept;
// support_dispatch.cpp: a deployment's soldiers aboard their transport, the squads' tops `tops` (a crewed APC / truck
// arrived, a helicopter assault): paired with it and on a trip to `target`, where they get off and guard it. `aboard`:
// they are seated already (else they board first).
bool TransportDeliver(const void* vehicle,const void* const* tops,int count,const float* target) noexcept;
// support_dispatch.cpp: a transport plane over `target`: its passengers jump there (transport_logic.h JumpNow), then it
// is sent off (jet.cpp JetWithdrawNow). Each squad guards `target` once down.
bool TransportParadrop(const void* plane,const float* target) noexcept;
// Once a frame (map.cpp, after SupportDispatchTick): the trips, the drops, the canopies.
void TransportTick() noexcept;
void ResetTransports() noexcept;   // mission.cpp: a new mission
// The map's lines (game thread, mapcmd.cpp Publish): a paired squad on foot and its transport.
struct TransportLink { float squad[3],vehicle[3]; bool trip; };
int TransportLinks(TransportLink* out,int most) noexcept;
}  // namespace crew
