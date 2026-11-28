// The plugin's jets (jet*.cpp, jetprops.cpp, impact.cpp): the NPC strike / escort / ferry aircraft, their
// launches, the bomber's bay and the jet bodies' steps.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
void ResetJets() noexcept;        // jet.cpp (and the dolls, the walls learned)
// An aircraft the plugin spawns for the player's side (jet_spawn.cpp): its durability scaled to the mission's difficulty
// as a script's CreateFriend scales it, its firepower marked to go by the support level (support_firepower.h). False:
// not marked; the caller deletes it.
bool LevelVehicle(unsigned char* vehicle) noexcept;
// What a jet ran into (impact.cpp), logged when it is held back ("blocked"): the nearest map surface round `pos`
// (terrain or a building) and the nearest objects (class, team, distance). `who` "JET" / "PJET", `way` its velocity.
void LogImpact(const char* who,const void* self,const float* pos,const float* way) noexcept;

// The core modules' own declarations (crew, heli, ground, hud, mission, loadout) are in their
// headers, included at the end of this file.

// jet.cpp
bool IsJet(const void* vehicle) noexcept;          // a 506 body from an edf6tr_jet_* SGO
bool IsPrimerVehicle(const void* vehicle) noexcept;// ...a Primer creature's (an enemy: primer.cpp)
bool JetInLine(const float* from,const float* to,const void* self) noexcept;   // a wingman in the way (no pass-through)
void JetFrame(unsigned char* vehicle) noexcept;    // from HeliFrame: NPC-crewed jets, and the plugin's launched drones
bool JetFliesItself(const void* vehicle) noexcept; // one of the plugin's launched drones: flown with its seats empty (jet_internal.h Unmanned)
void JetReap(const void* self) noexcept;           // deletes withdrawn jets; call from another object's update
bool InstallJets() noexcept;
// jet_hooks.cpp: the bullets' candidate hook (a jet's rounds through its wingmen, a passenger's through their own
// bike), on its own signatures: before InstallJets and InstallSidecar, whichever profiles they have.
bool InstallBulletPass() noexcept;
bool InstallJetProps() noexcept;                   // jetprops.cpp: from InstallJets
bool JetMotionProps(void* body) noexcept;          // a jet body's own motion properties (no 200 m/s cap); each physics step
void PreloadJets() noexcept;                       // from the mission's player preload
// A jet made at run time at `from`, flying along `heading` to work round `target`; false when it cannot
// be made (not preloaded this mission, profile mismatch): the caller keeps the stock behaviour then.
// `source`: what launched it (any fixed address per kind of source); jets from one source in a row fly
// as one flight, whose rounds pass through each other.
// `role`: the jet.cpp Role it flies as (same order); one whose SGO is not installed flies as a fighter.
// `escort`: it works round the player (while seen), not round `target`.
// blastCarrier / dollCarrier: a carrier whose drones blow up next to the enemy (the doll ones carrying a
// singing, dancing hololive doll); after `carrier`, as they are no role of their own. arsenalCarrier: the Arsenal Bird's
// drone mode, a carrier launching M101s (appended: the order is the calls' and the room's).
enum class JetRole { strike, fighter, interceptor, multirole, carrier, blastCarrier, dollCarrier, gunship, salvo, arsenalCarrier };
bool JetLaunch(JetRole role,const float* from,const float* heading,const float* target,DWORD fuelSec,const void* source,
               bool escort=false) noexcept;
// A gun drone (the carrier's drone body, EDF6VC_JET_DRONE.SGO) with no carrier: launched as JetLaunch launches
// a jet, it works round `target` (the player while seen, `escort`) and withdraws, to be deleted, as a launched
// jet does (fuel, damage, ammo). The vehicle, or nullptr (not preloaded this mission, kMaxJets flying).
unsigned char* JetLaunchDrone(const float* from,const float* heading,const float* target,DWORD fuelSec,const void* source,
                              bool escort) noexcept;
// A Dreadnought fighter off its hangar (dreadnought_hangar.inc): kind 0 fighter, 1 defender, 2 bomber, 3 interceptor (its
// crewless NPC body, jet_internal.h Body::dn*), launched as JetLaunchDrone launches a drone. The vehicle, or nullptr.
constexpr int kDnFighterKinds=4;
unsigned char* JetLaunchDreadnoughtFighter(int kind,const float* from,const float* heading,const float* target,DWORD fuelSec,
                                           const void* source,bool escort) noexcept;
// A Dreadnought's hangar and its fighters (dreadnought_hangar.inc; jet.cpp): the hangar holds one (on a lift, landing,
// launched off the deck): this frame's velocity and spin are its motion, a player aboard or not (false: none of the
// plugin's jets); lets it go (it flies on from that motion: its own flight as an escort, or the player's); takes it in
// (deleted, out of sight down its lift). JetLeaving: it is withdrawing or recovering (fuel, hull, stores).
bool JetHangarHold(unsigned char* v,const float* vel,const float* omega) noexcept;
void JetHangarRelease(unsigned char* v) noexcept;
void JetHangarStow(unsigned char* v) noexcept;
bool JetLeaving(const void* vehicle) noexcept;
bool JetMotion(const void* vehicle,float* vel) noexcept;   // its velocity as its flight (or hangar) last sent it
// A drone a thrown Robot Bomb releases where it landed (airstrike.cpp kThrows; tools/calls.py brings 'throw'): the
// blast or doll drone (rotor, a few metres over `at`) or the gun drone (fixed wing, taking off from ~20 m over it),
// with no carrier, working round `at` within a short reach and leaving (a charge: blowing up there) out of fuel
// (`fuelSec`). The vehicle, or nullptr (its body not preloaded this mission, JetPilot off, too many thrown drones
// out, kMaxJets): the caller keeps the stock bomb then.
enum class ThrownDrone { blast, doll, drone };
unsigned char* JetLaunchThrown(ThrownDrone what,const float* at,const float* heading,DWORD fuelSec,const void* source) noexcept;
// Whether jet.cpp still flies `vehicle` (the object with weak-this control block `ctrl`), alive and not
// withdrawing.
bool JetFlying(const void* vehicle,const void* ctrl) noexcept;
// jet.cpp: the paratroop plane (transport.cpp): passes over `at` along the straight line through it along `heading`, ends off
// the map, attacking nothing (a ferry; ferry_line.h); JetFerryDone: its stick is out, on to the end of the pass and deleted
// there (its crew by support_dispatch.cpp Retire); sent off now (withdrawn: deleted out of sight). False when the plugin
// does not fly `vehicle` (JetFerry: or no line with room to turn off the map at both ends; JetFerryDone: no ferry).
bool JetFerry(const void* vehicle,const float* at,const float* heading) noexcept;
bool JetFerryDone(const void* vehicle) noexcept;
bool JetWithdrawNow(const void* vehicle,const char* why) noexcept;
// The plugin's helicopter bodies (EDF6VC_HELI_410 / _506 / _MEDIC / _TRANSPORT.SGO, tools/make_jets.py). Every one of them
// is made by the support deployment (support_aircraft.h PrepareSupportAircraft): in the air with its real crew seated at
// once; the sea rescue too (support_call.h SupportRescueAt). Nothing makes one with an empty or dummy seat.
// A sortie call's pass (docs/plan-2026-10-25-strikes.md §1.2; tools/calls.py SORTIE_PASSES after `none`): its aircraft make
// one pass at the mark and leave the map (jets: jet_sortie.cpp; helicopters: heli.cpp HeliSortie).
#include "sortie_pass.inc"   // enum class SortiePass (tools/gen_calls.py)
enum class HeliBody { brute410, eros506, medic410, transport410 };   // medic410: EDF6VC_HELI_MEDIC (heli.cpp Medic); transport410:
                                                                      // EDF6VC_HELI_TRANSPORT (transport.cpp)
// A bomber's payload: BombingPlane_Init's arguments (0x5AABB0; speed in metres a frame), which a jet's bomb
// bay is set up from.
struct BombLoad { const void* owner; float damage,spread,speed,adjust,reach; const void* param; std::int32_t seed; };
// A strike jet that flies the bomber's run from `from` along `heading` over `target` and drops its bombs
// itself; false (the caller keeps the stock bomber) when it or its bay cannot be made.
// `body`: the model it flies in (BomberBody of the bomber's model instance).
enum class JetBody { kind=-1, bomber401=2, bomber501_2=3 };
// `hold`: the stock bomber's token (its weak-this control block) JetHolds answers for.
bool JetLaunchBomber(const float* from,const float* heading,const float* target,const BombLoad& load,DWORD fuelSec,const void* source,
                     JetBody body,const void* hold) noexcept;
// Whether the jet launched with `hold` still has its bay (not yet open, open, or its bombs still tracked):
// false once the bay is gone, the jet shot down or no longer flown.
bool JetHolds(const void* hold) noexcept;
// Which bomber body a BombingPlane's model instance (plane+0x660, embedded) is: kind (the BOMBER501 look,
// the strike jet's) unless its bones name BOMBER401's or BOMBER501_2's model.
JetBody BomberBody(const unsigned char* inst) noexcept;
bool JetBodyStep(unsigned char* v,float* lin,float* ang) noexcept;        // jet.cpp
bool JetHeldStep(unsigned char* v,float* lin,float* ang) noexcept;        // jet_hooks.cpp: a hangar's (JetHangarHold)

// What jet.cpp flies a jet as (game thread): its role's name, seconds of fuel left (-1: none, a carrier's drone),
// a carrier's drone launches left (-1: not a carrier), whether it is withdrawing; false when it does not fly it.
struct JetHudInfo { const char* role; float fuelSec; int drones; bool leaving; };
bool JetHud(const void* vehicle,JetHudInfo* out) noexcept;
namespace jet { bool SpawnReady() noexcept; bool PassThrough() noexcept;   // jet_hooks.cpp: the addBody hook is in
 bool ModFileThere(const wchar_t* file) noexcept; bool LockingOn(const void* target) noexcept;
// jet.cpp: the jets locking on to `target` lose their lock with `chance` each (a flare drop); how many did
int BreakLocks(const void* target,float chance) noexcept;
// jet.cpp: the jets with `target` in their missile lock now (LockingOn's): where they are, at most `most`; how many
int LockersOf(const void* target,float (*at)[3],int most) noexcept; }
}  // namespace crew
