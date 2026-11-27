// playerjet.cpp: the player's own aircraft (jets, the gunship, the plugin's rotor craft), its HUD readouts,
// the gunners, the ejection.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include "stores.h"
#include "objref.h"

namespace crew {
void ResetPlayerJets() noexcept;  // playerjet.cpp
// The player's jet `v` launched off a Dreadnought's bay (dreadnought_hangar.inc): its flight takes over at the motion its launch gave
// it (jet.cpp's entry), in the air, full throttle.
void PlayerJetLaunched(unsigned char* v) noexcept;
bool PlayerJetBodyStep(unsigned char* v,float* lin,float* ang) noexcept;  // playerjet.cpp
// playerjet.cpp: jets the player flies (docs/player-jet-re.md), 506 bodies with a player-jet mark (7201-7202).
// The plugin never crews them; with the player in seat 0 it flies them as fixed-wing planes.
bool IsPlayerJet(const void* vehicle) noexcept;
// One of the plugin's rotor craft the player may fly (playerjet_kinds.h Airframe::rotor): its L3 is the descend, so no pad
// free look there (vehicle_look.cpp; plan-2026-10-25-keybinds.md §3.3 D).
bool PlayerRotorCraft(const void* vehicle) noexcept;
// The local player `human` at the stick of one of the plugin's aircraft (playerjet.cpp): true, `object` the target its
// picked store has locked (nullptr: none), `at` its lock point. The Q mark there marks that and nothing else (npcai.cpp;
// plan §3.3 C).
bool PlayerJetMarkTarget(const unsigned char* human,const void** object,float* at) noexcept;
// Any other aircraft of the plugin (playerjet_kinds.h) is the player's too: they may board it now (one of ours, low and
// slow enough: crew.cpp bumps its NPC pilot for them); the plugin holds it for them (they fly it, it comes down for
// them, catches them or waits where they left it): jet.cpp does not fly it then, crew.cpp does not crew it.
bool PlayerJetBoardable(const void* vehicle) noexcept;
// The aircraft the hail key would call down for a player at `from` (none boardable near them: the HUD's hint), or the
// one called coming down (`*coming`): its position and distance. False: none, or no hail key.
bool PlayerJetHailHint(const float* from,float* at,float* distance,bool* coming) noexcept;
bool PlayerJetHolds(const void* vehicle) noexcept;
// The gunship's crew (playerjet_crew.inc, README 炮舰机): seat 0 its pilot, kGunnerSeat its side gunner (tools/make_jets.py
// with_gunner_seat; a gunship installed before has the one seat, and none of this). Whether `vehicle` is such a
// gunship; the seat its board button takes now (crew.cpp GunshipSeat: ini GunshipBoardGunner, the other one while
// GunshipGunnerKey is held).
constexpr unsigned kGunnerSeat=1;
bool GunshipCrewSeats(const void* vehicle) noexcept;
unsigned GunshipBoardSeat() noexcept;
// The player at its gun, its NPC pilot flying on (jet.cpp JetFrame): the pylon turn's centre, `at` the point they last
// shelled (centred: within the last kGunnerCentreMs), `home` where they boarded. False with the player not there.
struct GunnerOrder { bool centred; float at[3],home[3]; };
bool PlayerGunnerOrder(const void* vehicle,GunnerOrder* out) noexcept;
// The gunner's sight (hud.cpp GunnerMarks, game thread): where the screen's centre meets the ground (`ground`: within
// the camera's reach), its range from the gunship and whether the picked gun reaches it, that gun's wait (s, 0: ready;
// `ready`: its rounds are there and it is), the pylon turn's centre; `gun`: the gun picked (GunnerGun), `guns`: the
// guns there to switch between (a bit each, 1 << GunnerGun; the shells always). False with the player not at a gunship's gun.
enum class GunnerGun : int { shells, cannon, gatling, count };
// zoom: the sight's magnification (sightzoom.cpp; 1 none).
// keys: the rider plays on the keyboard and mouse (the controls row names their keys).
struct GunnerReadout { float sight[3]; bool ground,inReach,ready; float range,wait; float centre[3]; bool centred; GunnerGun gun; unsigned guns;
                       float zoom; bool keys; };
bool PlayerGunnerHud(GunnerReadout* out) noexcept;
// The gunship gunner's seat camera (playerjet_crew.inc GunnerLook): from turretcam.cpp's native Type-1 seat camera hook,
// the eye and look-at for the local player at the gun; false for any other seat (the stock camera stays).
bool GunshipGunnerCamera(const unsigned char* seat,const float* stockEye,const float* stockLook,float* eye,float* look) noexcept;
// The boarding gun (boarding.cpp): a real friendly hit suppresses damage and asks the game thread to board the
// vehicle. Broadphase candidates alone never request boarding; FrameTick handles the pending actual hit.
bool PlayerJetBoardingSupported(const void* vehicle) noexcept; // aircraft controls, without the on-foot reach/speed gate
void PlayerJetFrame(unsigned char* vehicle) noexcept;   // from every vehicle's input hook, after the stock step
// The jet the player flies now, for its cockpit readout (hud.cpp): game thread. False with none.
// The cockpit readout (hud.cpp): load in g; stall: all the wing gives is too little to hold its path; stores: what it
// carries (name, rounds left), `store` the one the secondary fire fires; bomb: that one is a bomb, `impact` where it
// would hit now (hasImpact: the ground is under its fall); clear: its
// height over the ground, or (ground: false, none under it) over the world's zero; keys: flown with the keyboard and
// mouse; aiming: in the air the mouse's aim steers it, `aim` the point it aims at, `path` the point it flies at.
// The fighter HUD's symbols for the aircraft the player flies (playerjet.cpp Sight / Threats, hud.cpp FighterHud).
constexpr int kMostThreats=6;
struct PlayerJetSymbols {
    float pos[3],nose[3];        // the aircraft and its body's nose (the guns fire along it: the boresight)
    float dir[3]; bool moving;   // its flight path's direction; moving: fast enough to have one (a hovering craft has not)
    bool gun;                    // a gun to sight: its rounds' `pipper` point at the range they are sighted for
    float pipper[3];
    bool lead,leadInRange;       // the picked store's target led for the guns (leadAt: aim the pipper there), within reach
    float leadAt[3],leadRange,gunRange;
    int threats;                 // what threatens it: where, and 2 a missile coming for it, 1 an enemy jet's lock on it
    float threatAt[kMostThreats][3];
    int threatKind[kMostThreats];
};
// The helicopter HUD's flight data (hud.cpp HeliHud; ini HeliFlightHud): a stock helicopter the player flies (heli.cpp
// PlayerHeliHud) or a rotor craft of the plugin (PlayerJetReadout::rotor, `heli`). vel: m/s, world (the hover's drift:
// its level part on the nose's frame); speed: its level part; clear: its height over the ground (ground: false, none
// under it: over the world's zero); climb m/s; setSpeed: the forward speed W / S set (m/s) of `top`; aim: the mouse's aim,
// a point ahead (aiming: the mouse-aim flight flies at it, heliaim.h); holding: it holds its height; rotor / hover: a stock
// heli on the ground, its rotor and the rotor whose lift holds it (the takeoff cue; 0: none); landed: on the ground;
// power: its engine, 0..1 (a stock heli's rotor, a rotor craft's Hover power: what holding the speed set and the height
// takes of it; <0: unknown, not shown).
// The ground-proximity warning (warn.cpp ClosureIn / GpwsOf), a real GPWS's modes: SINK RATE (sinking onto the ground
// under it too fast), TERRAIN (its path runs into something higher than that), PULL UP (either within kPullUpSeconds).
enum class Gpws : std::uint8_t { none, sinkRate, terrain, pullUp };
// gpws / impactIn: the ground-proximity warning and the seconds to the impact it warns of (<0: none).
struct HeliFlight {
    float vel[3],speed,clear,climb,hp,hpMax,setSpeed,top,aim[3],rotor,hover;
    float power;
    bool ground,landed,keys,aiming,holding,hoverMode;
    bool collective;   // W / S are the collective (a stock heli's instructor, heliaim.h), not a speed setpoint
    Gpws gpws;
    float impactIn;
};
// A vehicle's fuel tank (stockgauge.cpp FuelGauge: the FuelTank the stock FUEL gauge shows through its fuel weapon
// v_fuel01): ok false with none; share 0..1 of its capacity; sec the seconds left at its burn of the last seconds, <0
// unknown (not burning).
struct FuelReading { bool ok; float share,sec; };
struct PlayerHeliReadout { HeliFlight f; PlayerJetSymbols sym; FuelReading fuel; };
bool PlayerHeliHud(PlayerHeliReadout* out) noexcept;   // heli.cpp: the stock heli's, as of the last frame; false: none
struct PlayerJetReadout {
    float speed,throttle,clear,climb,hp,hpMax,load;
    float rotate;                // m/s: the speed it can lift off from (the kind's rotate), for the takeoff cue
    bool air,stall,ground,keys,aiming;   // ground: there is ground under it (clear is its height over it), not on it
    bool pullUp;                 // in the air and about to hit the ground or what stands on it (gpws == pullUp)
    Gpws gpws;                   // the ground-proximity warning, `impactIn` s to the impact it warns of (<0: none)
    float impactIn;
    float liftShare;             // the share of all the wing gives its path needs (1: STALL; 0 on the ground, a rotor craft)
    int threat;                  // 2 a missile homing on it, 1 an enemy's missile lock on it, 0 none
    int flares;                  // flare pairs left
    float aim[3],path[3];
    int stores,store;
    int storeButton,targetButton; // actual seat-button masks used by this aircraft
    const char* storeName[kMostStores];
    int storeRounds[kMostStores];
    int storeRole[kMostStores];            // each one's StoreRole (stores.h) as an int: its picture on the loadout strip (hud_cue.h)
    bool bomb,hasImpact;
    float impact[3];
    int lock;                    // the picked store's lock: 2 locked, 1 locking (lockProgress 0..1), 0 none (StoreLock)
    float lockAt[3],lockProgress;
    PlayerJetSymbols sym;        // the fighter HUD's (hud.cpp FighterHud)
    bool rotor;                  // a rotor craft of the plugin: `heli` (the helicopter HUD's) too
    HeliFlight heli;
    FuelReading fuel;            // its airframe's tank (the 506 body's: what the stock FUEL gauge showed)
    int guns,gunRounds;          // its guns (seat 0's weapons neither a store nor the tank) and the fewest rounds in one
    int area;                    // the play area's walls (playarea.h): 2 turned back by one, 1 heading out near one, 0 neither
};
bool PlayerJetHud(PlayerJetReadout* out) noexcept;
// Whether the player in `vehicle` sees our gun sight instead of the stock aim lines (crew.cpp AimLines): an aircraft
// the player-jet flight flies (playerjet.cpp), ini PlayerJetGunSight on.
bool PlayerJetOwnSight(const void* vehicle) noexcept;
// Whether `vehicle` is one of the plugin's aircraft the player-jet flight flies (a kind in playerjet_kinds.h), whatever
// the ini says (firstperson.cpp: a flown craft's pilot monitor is no gun camera).
bool PlayerJetCraft(const void* vehicle) noexcept;
void PlayerEjectTick() noexcept;   // playerjet.cpp: the player's ejection and parachute, a frame
// playerjet.cpp: a parachute canopy over anyone (transport.cpp's paratroopers): made over `feet` (its front along `drift`),
// moved there each frame, deleted. An empty ref: not made (EDF6VC_CHUTE.SGO not installed, or the game made nothing).
ObjRef ChuteCanopyMake(const float* feet,const float* drift) noexcept;
bool ChuteCanopyMove(const ObjRef& canopy,const float* feet,const float* drift) noexcept;
void ChuteCanopyFree(const ObjRef& canopy) noexcept;
// playerjet.cpp: a helicopter's rope (transport.cpp's rope descent; transport_logic.h kRopeLength long), hanging straight
// down from `top`, moved there each frame, deleted. EDF6VC_ROPE.SGO (tools/make_chute.py), a FarEventObject as the
// canopy is; an empty ref: not made (not installed, not preloaded, or the game made nothing).
ObjRef RopeMake(const float* top) noexcept;
bool RopeMove(const ObjRef& rope,const float* top) noexcept;
void RopeFree(const ObjRef& rope) noexcept;
void PreloadPlayerJets() noexcept; // playerjet.cpp: at a mission's start, the player jets' SGOs (the catch)
bool InstallPlayerJets() noexcept;                      // after InstallSub (it chains onto the 506 physics slot)
}  // namespace crew
