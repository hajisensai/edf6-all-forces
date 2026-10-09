// npcai.cpp: the plugin's own AI for the friendly NPC soldiers (docs/npc-ai-design.md §3, §4). Included by crew.h.
#pragma once
#include <cstdint>

namespace crew {
struct ObjRef;
// The soldier classes' Think (vtable slot 7) wrapped, chained after any other plugin's: once, from EnsureInputs
// (every plugin loaded by then). False when the intent block's code is not as read (the AI stays stock).
bool InstallNpcAi() noexcept;
// Ask existing friendly soldiers to walk to free seats; true means an assignment is pending.
// No soldier or dummy is created. Driver seats precede armed seats, then passengers.
bool NpcRequestCrew(unsigned char* vehicle,bool spawned=false) noexcept;
// Explicit support crew assignment; the supplied real soldiers still walk to the entry and board.
int NpcBoardCrew(unsigned char* vehicle,unsigned char* const* humans,int count) noexcept;
// A real NPC driver, or a legacy mission-script Dummy (never made by the plugin).
bool NpcDriver(const unsigned char* vehicle) noexcept;
bool NpcCanYieldSeat(const unsigned char* seat) noexcept;
// Already aboard: native Human seat transition, or dismount for to=-1.
bool NpcMoveSeat(unsigned char* vehicle,unsigned from,int to) noexcept;
// Delivery complete: native dismount of commandable local real NPCs, no deletion or teleport.
bool NpcReleaseVehicleCrew(unsigned char* vehicle) noexcept;
// Repair a cleared snapshot seat only when the real human still references that exact current seat.
bool NpcRestoreMissionSeat(unsigned char* vehicle,unsigned char* human) noexcept;
// A new mission (mission.cpp MissionStart): the last mission's soldiers are gone.
void ResetNpcAi() noexcept;
// Whether `human` is one of the four soldier classes (AssultSoldier, PaleWing, HeavyArmor, Engineer).
bool IsSoldierClass(const void* human) noexcept;
// Authority-only support ingress: an exact, short leader waypoint. Followers keep native follow;
// combat/evade/boarding stay ahead of the route. Updating it never clears target or combat state.
bool NpcPrepareSquadRoute(unsigned char* leader,const float* waypoint,float arrivalRadius) noexcept;
// End ingress with the usual guard behavior, including the configured defensive formation.
bool NpcFinishSquadRoute(unsigned char* leader,const float* destination) noexcept;
// An enemy is marked now (the Q mark, §6.3; the focus order needs one).
bool NpcMarked() noexcept;
ObjRef NpcMarkedIdentity() noexcept; // copied game-thread identity for an explicit per-squad focus command
// The mark for the HUD (draw thread): where it is; false with none (or none published lately).
bool NpcMarkReadout(float* at) noexcept;
// The mark for qmark.cpp (game thread): the marked enemy (alive, the same object) and its last lock point.
bool NpcMarkOwn(const void** object,float* at) noexcept;
// The squads' formations (formation.h): the map's T on a selected squad cycles a guarding squad's defence
// (CycleGuardFormation: the new shape, -1 when it guards nothing, -2 when it takes no orders) or the march of the
// player's recruited squads (CycleMarchFormation: the new shape). The HUD's banner: the march's shape, shown a
// moment after it changes (wall clock), and the key that cycles it.
int CycleGuardFormation(const void* leader) noexcept;
// Fireteams (the map's P and L): split a squad in two (the soldiers moved to the new one, -1 when it cannot be), put
// squad `from` under squad `into`'s top (false when either takes no orders or the two are more than a squad holds).
int SplitSquad(const void* leader) noexcept;
bool MergeSquads(const void* into,const void* from) noexcept;
int CycleMarchFormation() noexcept;
struct FormationCue { int shape; int key; };
bool PlayerFormationCue(FormationCue* out) noexcept;
// The box sweep (the player's NpcPickupKey): going (`on`: the boxes still to fetch `left`) or just over, the boxes
// brought in so far (`taken`), the key.
struct SweepCue { bool on; int left,taken,key; };
// The map's buttons (mapcmd.cpp): the sweep started for the squads whose tops are `tops` (`n` 0: the player's
// recruited squads) or called back (the new state); the health-box switch flipped (the new state: hurt soldiers may
// take them); the states and the march's formation for the buttons' labels.
bool NpcSweepToggle(const void* const* tops,int n) noexcept;
bool NpcSweepOn() noexcept;
bool NpcPickupHealthToggle() noexcept;
bool NpcPickupHealthOn() noexcept;
int NpcMarchShape() noexcept;
bool PlayerSweepCue(SweepCue* out) noexcept;
const wchar_t* FormationText(int shape) noexcept;   // hud.cpp: a shape's name as the HUD says it
// The mark key on foot (marking the enemy at the screen's centre, or sending the map's selection to the ground there) and
// the mark kept while its enemy is in the game: every frame of the local player `human` (map.cpp MapHumanFrame, game
// thread), `mapOpen` while the map view is (the key is the map's then).
void NpcMarkFrame(unsigned char* human,bool mapOpen) noexcept;
// The map marks an enemy (mapcmd.cpp: the mark key or the focus order with the pointer on it; game thread): `object` an
// enemy's, `at` its lock point. `toggle`: the one marked already is let go. True when it is marked now.
bool NpcMarkEnemy(const void* object,const float* at,bool toggle) noexcept;
// The mark key on foot with no enemy near the screen's centre: the point the selected units were sent to and how many took
// it (MapCommandGuardAt's result: -1 none selected, -2 online; kPingNearEnemy: an enemy near the centre but outside the
// mark's cone, nothing sent), `wall` when (GetTickCount64).
constexpr int kPingNearEnemy=-3;
struct NpcPing { bool on; float at[3]; int given; ULONGLONG wall; };
// The last point for the HUD (draw thread): false when there is none shown now.
bool NpcPingReadout(NpcPing* out) noexcept;

// npcpost.cpp: NPC tanks back to their post (docs/npc-ai-design.md §8). Each vehicle's input, before the stock input
// reads seat 0's stick (crew.cpp InputHook).
void NpcPostInput(unsigned char* vehicle) noexcept;
// A tank's post moved to `at` (a map command); false when the vehicle keeps no post now.
bool NpcPostCommand(const void* vehicle,const float* at) noexcept;
// Store an arrival destination until the assigned real driver boards the support vehicle.
bool NpcPrepareVehiclePost(unsigned char* vehicle,const float* at) noexcept;
// Verified navigation waypoint: stop within this explicit radius, independent of TankPostHold.
// The caller must use a radius no larger than the planner's waypoint advancement radius.
// at=current position requests a controlled stop; ordinary guard commands clear route mode.
bool NpcPrepareVehicleRoutePost(unsigned char* vehicle,const float* at,float arrivalRadius) noexcept;
void ResetNpcPosts() noexcept;   // a new mission
// A ground vehicle an NPC in seat 0 drives (and a map command sends to a post): the stock CarBase AI (0x661440: the
// tanks, the Titan, the bikes, the Grape and the trucks of its class, the rescue vehicle), armed or not.
bool NpcDrivable(const unsigned char* vehicle) noexcept;
// The AI riders in a CarBase vehicle's gunner seats aim and fire (§7): each vehicle's input, before the stock input.
void NpcGunnersInput(unsigned char* vehicle) noexcept;
// NPC gun input belongs to the local NPC's machine, not necessarily the vehicle driver's. Registered-vehicle
// Dummies run on the host; unregistered copies use their recorded owner. All human seats are excluded.
bool AiGunner(const unsigned char* vehicle,const unsigned char* seat) noexcept;
}  // namespace crew
