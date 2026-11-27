// EDF6VehicleCrew: shared layout, config and helpers.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46; see docs/re-notes.md.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "edf/input_source.h"
#include "edf/layout.h"
#include "edf/patch.h"
#include "gunsight.h"
#include "sightzoom.h"
#include "stores.h"
#include "edf/seat.h"
#include "objref.h"
#include "mission_packs.h"
#include "step_fresh.h"   // handoff freshness by the scene step (GameFrame), any thread
#include "sound_place.h"

namespace crew {
extern unsigned char* image;

struct Config {
    // The ini's keys (tools/config_keys.py; tools/gen_config.py writes them): members and defaults.
#define EDF6_CONFIG_FIELDS
#include "config.inc"
    // Filled by plugin.cpp LoadConfig itself (no ini key of their own, or a mask of several):
    bool verbose=false;        // Debug=2: the per-object periodic lines too (each NPC soldier's NPCAI line every 2 s)
    // Its hangar modules fitted (bit m: dreadnought_deck.h module m, ini DreadnoughtHangarLeftFore / LeftAft / RightFore /
    // RightAft). The installer builds its model and collision by them; the plugin runs the hangars its model has and only
    // says when these differ (installed before they were changed).
    unsigned dreadnoughtHangars=0xF;
    char testBoardVehicle[32]{};    // tests: class substring, optionally @exactModelBone to distinguish models sharing 506
    wchar_t traceFile[260]{};       // a recording of the vehicles' sounds' inputs and outputs for the offline replay (trace_rec.cpp,
                                    // docs/trace-replay.md): its path, empty off; read at load
    packs::Table missionPacks;       // EDF6VehicleCrew.packs.ini: each installed pack's identity and placeholder rows (mission_packs.h)
};
// Every value is range-checked when the ini is read (plugin.cpp Validate): a value out of range is clamped and
// the change logged.
// The live config: an immutable snapshot, swapped whole by the ini reload (plugin.cpp LoadConfig) and read
// from any thread (game, call picker, HUD draw) without a torn mix of old and new values.
const Config& Cfg() noexcept;
// An extra vehicle fire key's (3..6) binding: VehicleFire<key>Key's virtual key, VehicleFire<key>Button's XInput bits; 0
// none (payload.cpp fires on it, hud.cpp PayloadControls names it).
inline int FireKeyVk(unsigned char key) noexcept {
    const Config& c=Cfg();
    return key==3 ? c.vehicleFire3Key : key==4 ? c.vehicleFire4Key : key==5 ? c.vehicleFire5Key : key==6 ? c.vehicleFire6Key : 0;
}
inline int FireKeyButton(unsigned char key) noexcept {
    const Config& c=Cfg();
    return key==3 ? c.vehicleFire3Button : key==4 ? c.vehicleFire4Button : key==5 ? c.vehicleFire5Button :
           key==6 ? c.vehicleFire6Button : 0;
}
// Between SuppressBump(true) and SuppressBump(false) on this thread, the player's board button takes no
// NPC's seat (heli.cpp PressBoard): an override of the call, not a write to the config. (A pair of calls,
// not a scoped object: the callers run under __try, which allows no destructors.)
void SuppressBump(bool on) noexcept;
bool BumpSuppressed() noexcept;

// --- Time ---
// The game clock, game thread only (game_clock.h): wall time, stopped while the game is paused (GamePaused), and a
// gap between two reads longer than 250 ms (loading) counts as one 16 ms frame. Every timer of the plugin's logic,
// the player fix's included, is on this clock; wall time (GetTickCount64) is for log throttles and other threads only.
ULONGLONG GameMs() noexcept;
// The game's own pause (the pause menu: xgs::game::System's pause bits, docs/hud-re.md §10); any thread. False when
// CheckPauseFlag (at load, patches nothing) found the code different.
bool CheckPauseFlag() noexcept;
bool GamePaused() noexcept;
// Another thread, or data handed between hooks: stepfresh::Now() / Fresh (step_fresh.h), the same count mirrored.
// The game frame number, game thread only: it steps when a vehicle's per-frame input comes round again
// (crew.cpp InputHook calls SeeFrame), so "once a frame" work compares frame numbers, not clocks.
ULONGLONG GameFrame() noexcept;
// Wall time (GetTickCount64) for what has to see a stalled frame (memo_epoch.h): a function of its own, like GameMs and
// GameFrame, so a test steps it instead of sleeping.
ULONGLONG WallMs() noexcept;
// The native scene step as it reads now (System +0xCE0, stepped before the scene's input/update traversal), with its
// scene and the mission (ResetCrew): a per-frame key valid from the frame's first input on, where GameFrame still has
// the last frame's until the first vehicle's input is done. scene null: unreadable.
struct SceneStep { const void* scene; std::uint32_t count,mission; };
SceneStep SceneStepNow() noexcept;
void SeeFrame(const void* vehicle) noexcept;

// --- Mission lifecycle (mission.cpp) ---
// The mission's player preload (mission.cpp hooks it): every table of per-object state from the last
// mission is dropped here, before the new mission's objects (which may reuse the old addresses) exist.
void MissionStart() noexcept;
void ResetCrew() noexcept;        // crew.cpp
void SceneOver() noexcept;        // crew.cpp: the scene's end (mission.cpp), every step stamp stale
void ResetHelis() noexcept;       // heli.cpp (and the player track, the rescue)
void ResetGround() noexcept;      // ground.cpp
void ResetJets() noexcept;        // jet.cpp (and the dolls, the walls learned)
void ResetAirstrikes() noexcept;  // airstrike.cpp
void ResetBoosters() noexcept;    // booster.cpp
void ResetShields() noexcept;     // shield.cpp
void ViewDiagTick() noexcept;     // viewdiag.cpp: once a frame, logs (VIEWDIAG) when the view's eye leaves the map or the near pass's clip breaks
bool InstallUmbraWatch() noexcept;   // viewdiag.cpp: logs (VIEWDIAG umbra) a box handed to Umbra out of any map's reach
// view.cpp: while the map view is open (map.cpp, game thread, each frame) the cameras' far clip raised to `farClip` and
// their near clip to `nearClip` (the depth range for a view from up to 3 km); off: the values from before put back.
void ViewMapClip(bool on,float farClip,float nearClip) noexcept;
void ResetSubs() noexcept;        // subcarrier.cpp
void ResetLaser() noexcept;       // carrierlaser.cpp
void ResetPlayerJets() noexcept;  // playerjet.cpp
void ResetHud() noexcept;         // hud.cpp
void ResetLauncher() noexcept;    // launcher.cpp
void ResetHeliSight() noexcept;   // helisight.cpp
void ResetJetSound() noexcept;    // jetsound.cpp
void ResetMissiles() noexcept;    // missile.cpp
// An aircraft the plugin spawns for the player's side (jet_spawn.cpp): its durability scaled to the mission's difficulty
// as a script's CreateFriend scales it, its firepower marked to go by the support level (support_firepower.h). False:
// not marked; the caller deletes it.
bool LevelVehicle(unsigned char* vehicle) noexcept;
void ResetBigWorld() noexcept;    // bigworld.cpp
bool InstallTerrainShare() noexcept;   // fieldshare.cpp
void ResetTerrainShare() noexcept;
void BigWorldProbe() noexcept;
// The process's memory and the machine's, one log row (bigworld.cpp): at the mission's start and once its map is in, so
// a map's cost shows in the log (the 3 x 3 big map ran an 8 GB machine out of memory, testhub report #3).
void LogMemory(const char* when) noexcept;
// What a jet ran into (impact.cpp), logged when it is held back ("blocked"): the nearest map surface round `pos`
// (terrain or a building) and the nearest objects (class, team, distance). `who` "JET" / "PJET", `way` its velocity.
void LogImpact(const char* who,const void* self,const float* pos,const float* way) noexcept;
// m: the physics world's half size (3000 stock, ini BigWorld when raised): the plugin's walls stand inside it.
// The edge of the play area every flyer keeps inside (the user, 2026-10-05: "don't let them go out there; a buffer
// before it; past the line, coming back comes first"): the stock world's 2400 (600 m inside its +-3000), or the big
// map's ground's own edge, its BigWorld less the margin tools/make_bigmap.py adds past the last block (WORLD_MARGIN;
// selftest holds the two equal). It was the physics world less 600 m on the big map too: 150 m out over no ground,
// where the player slid along the wall below the ground with the heading snapping +-17 deg (2026-10-05 11:47).
constexpr float kBigWorldMargin=750.0f,kStockEdgeIn=600.0f;
// The buffer inside the edge: from here in the flyers are turned in, the more the nearer the edge (EdgeTurn).
constexpr float kEdgeBuffer=800.0f;
inline float WorldHalf() noexcept;
inline float PlayEdge() noexcept { return Cfg().bigWorld>3000.0f ? Cfg().bigWorld-kBigWorldMargin : 3000.0f-kStockEdgeIn; }
inline float WorldHalf() noexcept { return Cfg().bigWorld>3000.0f ? Cfg().bigWorld : 3000.0f; }    // bigworld.cpp: once a mission, the map's ground on a grid (log)
// m: the Havok world's half size in fact (bigworld.cpp): ini BigWorld only once its bounds were patched in, else the
// stock 3000 (WorldHalf takes the ini at its word).
float HavokHalf() noexcept;
// m: the square the support arrivals are made and fly their passes in (support_entry.h Reach::half): kArrivalRoom inside
// both the Havok world (a body past it is no longer simulated) and the jets' deletion (jet.cpp kWorldGoneIn of WorldHalf).
constexpr float kArrivalRoom=300.0f;
inline float ArrivalHalf() noexcept { const float h=HavokHalf(),w=WorldHalf();return (h<w ? h : w)-kArrivalRoom; }
// m: how far the near camera draws (view.cpp: the mission's FarClipZ, 1000 m in every mission, raised to ini
// ViewDistance); past it only what has the far-render bit is drawn (jet_spawn.cpp FarRender).
constexpr float kStockFarClip=1000.0f;
// ViewDistance -1 (the default since 2026-10-10, the user: "改一下上限，改成无限"): no limit of the plugin's -- the near
// camera reaches past anything the world holds: its diagonal (2.83 x WorldHalf) and no nearer than the stock far pass's
// end (20000 m). view.cpp also never sets it nearer than the mission's own far pass end.
constexpr float kUnlimitedView=-1.0f,kStockDistantFar=20000.0f;
inline float UnlimitedViewDistance() noexcept { const float d=2.83f*WorldHalf();return d>kStockDistantFar ? d : kStockDistantFar; }
// m: the near camera's far clip the plugin asks for (0: none, the mission's own).
inline float ViewDistanceWanted() noexcept { return Cfg().viewDistance<0.0f ? UnlimitedViewDistance() : Cfg().viewDistance; }
inline float NearDrawDistance() noexcept { const float v=ViewDistanceWanted();return v>kStockFarClip ? v : kStockFarClip; }
// The camera's view-projection (row vectors, the HUD's) as of the last frame drawn; false before one (hud.cpp).
bool LastViewProj(float* out) noexcept;
// The camera's eye and its unit look through the screen's centre, from LastViewProj (hud.cpp); false: no camera yet.
bool CameraRay(float* eye,float* dir) noexcept;
// Split screen: each local player's own view (player_view.h). KeepPlayerViewProj from each HUD pass with the camera it
// draws for (subcarrier.cpp GaugeHook); CameraRayOf the ray of the view last drawn for `human`, false with none fresh.
void KeepPlayerViewProj(const void* camera,const float* viewProj) noexcept;
bool CameraRayOf(const void* human,float* eye,float* dir) noexcept;
// A bigger physics world and the map pieces' log (bigworld.cpp): at load, before any mission.
bool InstallBigWorld() noexcept;
// legacymap.cpp: EDF4.1 maps' Havok 2014 collision migrated to hknp on the map path (plan P6).
bool InstallLegacyMap() noexcept;
// edf41sound.cpp: EDF4.1's weapon SE bank and voice banks beside EDF6's own (plan P7).
bool InstallEdf41Sound() noexcept;
// edf41ai.cpp: the converted EDF4.1 Hectors attack as 4.1's do (per-arm range gate, back-off cycle).
bool InstallEdf41Ai() noexcept;
// edf41fortress.cpp: the converted EDF4.1 four-legged fortress stands from the start and drops through its core hatch.
bool InstallEdf41Fortress() noexcept;
// edf41dragon.cpp: the converted EDF4.1 giant dragon breathes 4.1's flame stream and keeps 4.1's ranges and glide pass.
bool InstallEdf41Dragon() noexcept;
// edf41smalldragon.cpp: the DLC's EDF4.1 small dragons' hit reaction drives 4.1's ragdoll bodies (koshi, spine1, spine2), not mune.
bool InstallEdf41SmallDragon() noexcept;
// edf41insects.cpp: the DLC's EDF4.1 queen bee and Nephilas: 4.1's needle speed and thread scatter.
bool InstallEdf41Insects() noexcept;
// edf41mech_boost.cpp: the 406 Begaruta's jet group skips the thruster slots its old begaruta_boost parser leaves unbuilt (its jump crashed).
bool InstallEdf41MechBoost() noexcept;
// edf41natives.cpp: EDF4.1 script natives EDF6's BVM executor lacks (SetEncountReportRadius as table 1 id 3003).
bool InstallEdf41Natives() noexcept;
// edf41before.cpp: EDF4.1's pre-mission scenes (BEFORE.BVM) play with the menu's UIs (its black fade) closed, as in 4.1.
bool InstallEdf41Before() noexcept;
// edf41mother.cpp: UfoMother301 (EDF4.1's mothership) and its part classes, registered in the game's own factory table.
bool InstallEdf41Mother() noexcept;
bool InstallEdf41Robo() noexcept;      // edf41robo.cpp: EDF4.1's UfoRobo (ini Edf41Robo)
bool InstallEdf41Mother401() noexcept;   // edf41mother401.cpp
// edf41deiroi.cpp: a falling Deiroi's landing ray, cast by EDF6 against a null Havok world (crash in EDF4.1's M412), goes through the map ray.
bool InstallEdf41Deiroi() noexcept;
// allclass.cpp: every class in every seat (ini AllClassesRide): the seat build's class mask widened, the ride pose's fallback.
bool InstallAllClassesRide() noexcept;
// fsbudget.cpp: CRI File System's loader pool (num_loaders: 130 as the game builds it, 0x60 + its caller's 0x22) raised
// by the model archives the spawn catalog can keep open (src/spawn_catalog_archives.h) before the library starts, so the
// test range's place panel catalog does not run it out. The other pools do not grow with open archives (fsbudget.cpp).
bool InstallFsLoaderBudget() noexcept;
// killcount.cpp: the battle record (戦績) also lists the EDF4.1 port's own kill counters (edf41_*KillCount, the
// installer's Mods/ETC/ACHIEVEMENT.SGO; docs/kill-counter-re.md): its row table moved to the plugin's, rows added.
bool InstallKillCounters() noexcept;
// The plugin's missiles guided by proportional navigation with a proximity fuse (missile.cpp).
bool InstallMissiles() noexcept;
// The stock split missiles (MissileBullet02) split short of a big target's surface (splitmissile.cpp).
bool InstallSplitMissiles() noexcept;
// Support units (SupportUnitBullet01) stick to allies (supportunit.cpp): the ctor's pass-through-allies byte cleared
// while the ini's SupportUnitOnAllies is on (set by every config load).
bool InstallSupportUnitOnAllies() noexcept;
void SetSupportUnitOnAllies(bool on) noexcept;
// The stock route guide strip (RouteGuide) drawn along the navmesh route's corners (route_guide.cpp).
bool InstallRouteGuide() noexcept;
bool InstallGuidance() noexcept;   // guidance.cpp: the stock homing rounds' steering calls
// The jets' engine sound (jetsound.cpp): checked at load; per vehicle input (it picks the plugin's jets itself);
// once a frame, the plugin off too (the camera's motion; the sounds of jets gone, or all with the plugin off, stopped).
bool InstallJetSound() noexcept;
// The game's glyph cache under one lock (glyphs.cpp): its threads lost glyphs and showed one character as another.
bool InstallGlyphLock() noexcept;
bool InstallEdf5Campaign() noexcept;
// The offline HQ's Q / E (LB / RB): the previous / next mission pack without the title on the way (pack_switch.cpp).
bool InstallPackSwitch() noexcept;
// Whether the mission-pack dialog would let the player pick content `id` (mgr: the GameDataMgr; edf5campaign.cpp).
bool MissionPackSelectable(std::uintptr_t mgr,int id) noexcept;
bool InstallPortedMissions() noexcept;   // PortedMissionsUnlocked: the packs' mission select (edf5campaign.cpp)
void SetMissionPackSaveHook(bool ready) noexcept;   // the save-load hook is in (airstrike.cpp): packs with Legacy rows owned
void MigrateMissionPackSave(unsigned char* gs) noexcept;   // a pack's loaded save moved to the rows now, once (edf5campaign.cpp)
bool InstallEdf41MenuBg() noexcept;      // EDF4.1's HQ background in the 4.1 packs (edf5campaign.cpp)
// The current mode's content id (0..2 EDF6's, then the installed packs': Cfg().edf5CampaignContent, edf41Content ...), -1
// when there is none to read (edf5campaign.cpp, as the mission select reads it). Any thread: only reads.
int GameContentId() noexcept;
// EDF5's compiled mission scripts online: the BVM executor's PreloadPlayerResource and CreatePlayer given their online
// half (edf5online.cpp). Edf5BvmOnlinePlayers: the patched creation loop's count.
bool InstallEdf5Online() noexcept;
bool Edf5OnlineReady() noexcept;
std::uint32_t Edf5BvmOnlinePlayers() noexcept;
bool Edf5BvmOnlinePlayerArgs(int index,int* pad,int* split) noexcept;
void Edf5BvmOnlinePlayerMade(int index,const void* made) noexcept;
void ResetEdf5Online() noexcept;
// More than the BVM loop's four online (edf5online_room.cpp, edf5online_room.h): EDF6Coop's BvmPlayerTableHooks in
// place here; every one of the mission's `count` players' machines said it creates them too (EDF6Coop member attribute
// EDF6X_BVMPLAYERS; false with the reason in `why`); this machine's own word told and the others' watched (the room's
// frame, room_ui.cpp).
bool Edf5CoopBvmTableHooked() noexcept;
bool Edf5RoomAllowsBvmPlayers(std::uint32_t count,char* why,std::size_t size) noexcept;
void Edf5OnlineRoomTick() noexcept;
void JetSound(unsigned char* vehicle) noexcept;
void JetSoundTick() noexcept;
float GameEffectVolume() noexcept;   // jetsound.cpp: the game's master volume times its effect volume (0..1)
// The lock-on beeps of a vehicle's weapons: kept for a local player's seat, silenced for every other (jetsound.cpp).
void LockSound(unsigned char* vehicle) noexcept;
// Where a sound at `pos` moving `vel` (m/s) is heard from the camera (jetsound.cpp, the sound system's listener 0, as the
// jets' engines are placed; SoundPlace and the arithmetic: sound_place.h). `vel` may be nullptr: a still sound. False: no
// listener this frame.
bool SoundAt(const float* pos,const float* vel,SoundPlace* out) noexcept;
bool SoundListening() noexcept;   // the listener is placed this frame (JetSoundTick ran for the jets or the vehicles)
// SoundAt's whole input besides the sound's own (the trace's, trace_rec.cpp): whether it places sounds now, the listener
// (soundplace::kListenerFloats) and its velocity.
bool ListenerState(float* listener,float* listenerVel) noexcept;
bool ListenerAt(float* pos) noexcept;   // where that listener (the camera) is this frame; false: none
// The ground vehicles' sounds (vehsound.cpp, ini VehicleSound): checked at load; every vehicle's input, the plugin off
// too (then it gives the stock sounds back); a new mission.
bool InstallVehicleSound() noexcept;
void VehicleSound(unsigned char* vehicle) noexcept;              // VehicleSoundAt at GameMs
void VehicleSoundAt(unsigned char* vehicle,ULONGLONG ms) noexcept; // `ms`: the step's one "now" (GameMs read once)
// The offline replay's recording (trace_rec.cpp, ini TraceFile; docs/trace-replay.md): a step's inputs taken before it
// runs on `vehicle`, what it does recorded until TraceEnd; TraceReset where the plugin resets the step's state (a mission).
void TraceVehicleSound(unsigned char* vehicle,ULONGLONG ms) noexcept;   // `ms`: the step's, as VehicleSoundAt is given
void TraceEnd() noexcept;
void TraceReset() noexcept;
// Inside a recorded step: where its outputs go (null outside one). A pointer, not a function, so that the sources that
// record an output (jetaudio.cpp) need nothing of the recorder's when a test builds them.
inline void (*traceOutput)(const char* name,const void* data,std::size_t size) noexcept=nullptr;
inline bool TraceRecording() noexcept { return traceOutput!=nullptr; }
inline void TraceOutput(const char* name,const void* data,std::size_t size) noexcept { if(traceOutput)traceOutput(name,data,size); }
void ResetVehicleSound() noexcept;
bool VehicleSoundHeld(const void* preset) noexcept;   // a vehicle's stock sound preset held silent (vehsound.cpp)
// The rounds' explosions as War Thunder layers them (blastsound.cpp, ini ExplosionVolume): the calls that play a round's
// AmmoHitSe redirected at load; once a frame (SharedFrameTick, after JetSoundTick: the listener); a new mission.
bool InstallBlastSound() noexcept;
void BlastSoundTick() noexcept;
void ResetBlastSound() noexcept;

// --- EDF.dll layout ---
// The facts EDF6AutoTurret rests on too live in common/edf/layout.h (one definition for both plugins):
// GameObject weak-this +0x28 / +0x30, the vehicle's matrix, position, dead byte, team, seats, the seat's
// rider weak_ptr, the human's pad / player flag, the dummy rider's vtable, At / Put.
using edf::kSelf; using edf::kSelfCtrl; using edf::kMatrix; using edf::kPosition; using edf::kDead; using edf::kTeam;
using edf::kSeats; using edf::kSeatCount; using edf::kSeatStride; using edf::kSeatRider; using edf::kSeatRiderCtrl;
using edf::kHumanPad; using edf::kHumanPlayer; using edf::kDummyRiderVtable; using edf::kSlotInput;
using edf::kSeatAim; using edf::kAimAxes; using edf::kAxisStride; using edf::kAxisMin; using edf::kAxisMax; using edf::kAxisAngle;
using edf::At; using edf::Put;
// Teams (mission AsCommon.h): player 0, enemy 1, friend 2, neutral 3, vehicle 5 = nobody's vehicle,
// which anyone may board (CanRideSeat skips the team test for it).
constexpr std::int32_t kTeamVehicle=5;
constexpr std::int32_t kTeamNeutral=3;   // hostile to nobody (playerjet.cpp: the parachute's canopy)
// The game's SetTeam 0x54EE70(object, team, registered). The team manager (*(image+0x20B2978)) keeps a set of objects
// per team (+0x38, 0x38 bytes a team) and finds an object's set by its team +0x314, when it adds it (0x5E0B70), takes
// it out (0x5E1C60: SetTeam, the object's destruction) or walks a team's objects. A write to +0x314 alone leaves the
// object in its old team's set while everything after looks in the new one's: freed, it stays in the old set, and the
// next walk of it reads freed memory (the crash at the next mission's start after a crewed 603_Flak, 2026-10-04).
// Every team change goes through SetTeam, the object's registration (+0x380 bit 6) kept as it is.
constexpr unsigned kSetTeam=0x54EE70;
constexpr std::size_t kObjectFlags=0x380;
// Never from inside a team walk's visitor (the board prompt, FindSeat): crew.cpp WithTeamField.
void SetObjectTeam(unsigned char* object,std::int32_t team) noexcept;
// Human: the vehicle it is in (weak_ptr object +0x1548, control block +0x1550)
constexpr std::size_t kHumanVehicleCtrl=0x1550;
// VehicleBase virtual slots (input: edf::kSlotInput)
constexpr std::size_t kSlotFindSeat=49,kSlotRideAi=50;
constexpr unsigned kFindSeat=0x633B80,kRideAi=0x633030;
// Seat functions
constexpr unsigned kCanRideSeat=0x6346D0;   // (vehicle, human, seat) -> bool: team, mask, free, in reach
constexpr unsigned kCanRide=0x62DCB0;       // (vehicle, human) -> bool: any seat passes the above
constexpr unsigned kSeatRide=0x633C10;      // (vehicle, rider, index, force) -> seat or null
constexpr unsigned kSeatClear=0x634940;     // (vehicle, seat): forget the rider, no message to it
constexpr unsigned kSeatKick=0x62E1A0;      // (vehicle, seat): get-off message, then clear (a dummy rider dies)
// The NPC in seat `from` moved to seat `to` (seat, then clear, as the bump moves one); false: not moved, the NPC where it
// was (crew.cpp).
bool MoveRider(unsigned char* vehicle,unsigned from,unsigned to) noexcept;
// The on-foot ride-prompt visitor (0x5735E7): {vtable, human, bool result}; slot 1 is called per object
constexpr unsigned kPromptFunctorVtable=0x17D09B8,kPromptVisit=0x5725A0;
constexpr std::size_t kFunctorHuman=0x8,kFunctorResult=0x10;

// ObjRef / WeakRef and the one liveness test (objref::Exists / Alive): src/objref.h, docs/objref-re.md.

// The plugin's log (EDF6VehicleCrew.log; over kLogMax it is renamed to .log.1 and a new one begun).
void Log(const char* format,...) noexcept;
// Preformatted crash diagnostics: direct append, without the ordinary logger's locks or CRT formatting.
void CrashLog(const char* text,std::size_t size) noexcept;
void ReloadConfigIfChanged() noexcept;
// The ini read again now (the loadout menu saved it: loadout_store.cpp), and its path (Mods/Plugins/EDF6VehicleCrew.ini).
void ReloadConfigNow() noexcept;
const wchar_t* ConfigPath() noexcept;
// The patch primitives and the seat test are common/'s (shared with EDF6AutoTurret).
inline bool Matches(std::size_t rva,const unsigned char* bytes,std::size_t size) noexcept { return edf::Matches(image,rva,bytes,size); }
using edf::PatchVtableSlot;

// What sits in a seat (common/seat.cpp).
using Rider=edf::Rider;
inline Rider SeatRider(const unsigned char* seat) noexcept { return edf::SeatRider(image,seat); }
// A player of any machine in the seat (Rider::player is this machine's only: common/seat.cpp).
inline bool AnyPlayerIn(const unsigned char* seat) noexcept { return edf::AnyPlayerIn(image,seat); }
using edf::SeatAt; using edf::SeatCount; using edf::IsPlayer; using edf::IsAnyPlayer; using edf::IsAnyPlayerSelf;

// The player as last seen (on foot through the prompt visitor, or riding through a vehicle input); `at` is
// GameMs (0: never seen).
struct PlayerFix { float pos[3]; std::int32_t team; ULONGLONG at; };
extern PlayerFix player;
void SeePlayer(const float* pos,std::int32_t team) noexcept;
// A new mission (MissionStart): the last mission's fix and player human are forgotten. GameMs counts the load
// as one frame, so without this they would pass for fresh at the new mission's start.
void ResetPlayer() noexcept;
// Shared game-thread scene tick, called by the local Soldier and by vehicle inputs; de-duplicated by GameFrame.
void FrameTick() noexcept;

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
bool InstallBoosters() noexcept;                   // booster.cpp: the carrier's nozzle flames (stock Booster)
bool InstallShields() noexcept;                    // shield.cpp: the Shield Bearer's shield lets slow things through
bool InstallBodyBox() noexcept;                    // body_box.cpp: before InstallMissiles and InstallJets
// body_box.cpp: physics body (wrapper) `body`'s world box, min xyz then max xyz; false without one
bool BodyWorldBox(const void* body,float* box) noexcept;
// shield.cpp: whether a round (its candidate collector) passes a shield layer's body (slow: true, so it is left out)
bool ShieldLetsThrough(void* collector,std::uint32_t body) noexcept;
// shield.cpp: a fast vehicle's velocity (m/s) kept from crossing a hostile shield's face; the speed it lost
float ShieldBlock(const unsigned char* vehicle,float* vel) noexcept;
void ShieldVehicle(unsigned char* vehicle) noexcept;   // shield.cpp: the same for a vehicle with no plugin body
// booster.cpp: an aircraft's exhaust flames on its model's nozzles (exhaust_nozzles.h: a jet's exits, the carrier's four
// pods), burning `intensity` (0..1), `burner` longer.
void JetFlames(const unsigned char* v,float intensity,bool burner,ULONGLONG ms) noexcept;
// booster.cpp: flames on nozzles placed in the world (+z out of each, rows unit), `size[i]` (length, width m) and `level[i]` (0 out .. 1) each; at most 10.
void NozzleFlames(const unsigned char* v,const float (*m)[16],int n,const float (*size)[2],const float* level,ULONGLONG ms) noexcept;
void JetSmoke(const unsigned char* v,bool on,ULONGLONG ms) noexcept;   // booster.cpp: an arriving aircraft's smoke trails (its flames' nozzles)
bool JetMotionProps(void* body) noexcept;          // a jet body's own motion properties (no 200 m/s cap); each physics step
void PreloadJets() noexcept;                       // from the mission's player preload
// A jet made at run time at `from`, flying along `heading` to work round `target`; false when it cannot
// be made (not preloaded this mission, profile mismatch): the caller keeps the stock behaviour then.
// `source`: what launched it (any fixed address per kind of source); jets from one source in a row fly
// as one flight, whose rounds pass through each other.
// `role`: the jet.cpp Role it flies as (same order); one whose SGO is not installed flies as a fighter.
// `escort`: it works round the player (while seen), not round `target`.
// blastCarrier / dollCarrier: a carrier whose drones blow up next to the enemy (the doll ones carrying a
// singing, dancing hololive doll); after `carrier`, as they are no role of their own.
enum class JetRole { strike, fighter, interceptor, multirole, carrier, blastCarrier, dollCarrier, gunship, salvo };
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
// Whether the local player leaving `fighter` is put aboard its dreadnought (dreadnought_hangar.inc: its own bay's hangar,
// running here, has it at that bay's mouth putting its pilot out onto the bridge): no parachute, no catch
// (playerjet.cpp Leave / Left), the fighter handed back. Anywhere else, false: they eject.
bool DreadnoughtTakesBack(const unsigned char* fighter) noexcept;
// A board press of `human` on foot that visits Dreadnought `vehicle` (crew.cpp FindSeatHook): true when the local player
// stands over one of its fitted hangars and the press is taken for it (a fighter of that hangar out to its bay's mouth
// for them: dreadnought_hangar.inc HangarBoard), no seat of the ship's.
bool DreadnoughtHangarBoard(unsigned char* vehicle,unsigned char* human) noexcept;
// A Dreadnought's hangar and its fighters (dreadnought_hangar.inc; jet.cpp): the hangar holds one (on a lift, landing,
// launched off the deck): this frame's velocity and spin are its motion, a player aboard or not (false: none of the
// plugin's jets); lets it go (it flies on from that motion: its own flight as an escort, or the player's); takes it in
// (deleted, out of sight down its lift). JetLeaving: it is withdrawing or recovering (fuel, hull, stores).
bool JetHangarHold(unsigned char* v,const float* vel,const float* omega) noexcept;
void JetHangarRelease(unsigned char* v) noexcept;
void JetHangarStow(unsigned char* v) noexcept;
bool JetLeaving(const void* vehicle) noexcept;
bool JetMotion(const void* vehicle,float* vel) noexcept;   // its velocity as its flight (or hangar) last sent it
// The player's jet `v` launched off a Dreadnought's bay (dreadnought_hangar.inc): its flight takes over at the motion its launch gave
// it (jet.cpp's entry), in the air, full throttle.
void PlayerJetLaunched(unsigned char* v) noexcept;
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

// body506.cpp: the 506 body the plugin's jets, carriers and player jets fly in. Which one a vehicle is comes
// from its SGO's mark (veh+0x162C, kMark* in body506.cpp, the one table of them); the 506's physics step
// (slot 57) is hooked once, there, and hands each body to its owner's step, which returns the velocity and
// spin to set (false: leave the stock step's).
enum class PluginBody { none, jet, sub, playerJet, sazabi, dreadnought };
PluginBody BodyOf(const void* vehicle) noexcept;
float BodyMark(const void* vehicle) noexcept;      // the mark of a 506 body, 0 for anything else
// A 506's vtable (BodyMark is 0 for any other: so is IsJet): for an object the caller knows is live (one the game's
// walk hands over), whose vtable it read itself.
bool Heli506Vtable(const void* vtable) noexcept;
bool InstallBody506() noexcept;                    // before InstallJets / InstallSub / InstallPlayerJets
bool Body506Ok() noexcept;                         // the physics hook is in
bool Body506ReadsSolver() noexcept;                // ...and it reads the solver's velocity before the stock step (GroundContact)
bool JetBodyStep(unsigned char* v,float* lin,float* ang) noexcept;        // jet.cpp
bool JetHeldStep(unsigned char* v,float* lin,float* ang) noexcept;        // jet_hooks.cpp: a hangar's (JetHangarHold)
bool SubBodyStep(unsigned char* v,float* lin,float* ang) noexcept;        // subcarrier.cpp
bool PlayerJetBodyStep(unsigned char* v,float* lin,float* ang) noexcept;  // playerjet.cpp
bool SazabiBodyStep(unsigned char* v,float* lin,float* ang) noexcept;     // sazabi.cpp
bool DreadnoughtBodyStep(unsigned char* v,float* lin,float* ang) noexcept;   // dreadnought.cpp
// sazabi.cpp: the Sazabi (docs/gundam-plan.md), a 506 body with the Sazabi mark (7401-7499): never crewed, never flown
// as a heli; with the player in seat 0 it walks and flies; its bones are posed every frame.
bool IsSazabi(const void* vehicle) noexcept;
// dreadnought.cpp: the Dreadnought (docs/dreadnought-plan.md), a 506 body with its mark (7601-7699): never crewed, never
// flown as a heli; with the player in seat 0 the plugin flies it, empty after that it settles on the ground.
bool IsDreadnought(const void* vehicle) noexcept;
// `human` at a Dreadnought's bridge on the keyboard, and `vk` one of its strafe keys (dreadnought.cpp): the bridge holds the
// key, the Q mark yields to it (npcai.cpp; plan-2026-10-25-keybinds.md §3.3 B).
bool DreadnoughtBridgeClaims(const unsigned char* human,int vk) noexcept;
// boarding.cpp: the player into it (the boarding gun's path). False, logged: not taken on (the boarding hook not in, no
// player, or refused: not a board-able friend, the player in a vehicle, no seats); nothing will press for it.
bool BoardingRequest(unsigned char* vehicle) noexcept;
bool BoardingReady() noexcept;                           // boarding.cpp: its hook is in (BoardingRequest can take one on)
bool BoardingUnderway() noexcept;                        // boarding.cpp: a boarding is still pressing the button
// The local player's Sazabi as the HUD shows it (sazabi.cpp publishes it each frame on the game thread; hud.cpp reads it,
// fresh while published in the last steps, step_fresh.h). False with none (not riding one).
struct SazabiCue {
    float thruster;          // 0..1 the thrusters' charge
    float rifleReady;        // 0..1 to the beam rifle's next shot (1 ready)
    int missiles;            // shield missiles left
    float missileReady;      // 0..1 to the next salvo
    float missileLock;       // 0..1 the lock's progress (1 locked)
    float cannonCharge;      // 0..1 the chest cannon charging (0 not)
    float cannonReady;       // 0..1 to the cannon's next charge
    int funnelsOut;          // 0..6 funnels flying
    float funnelReady;       // 0..1 to the next launch
    bool guard,swinging,air; // the shield up, the tomahawk swinging, off its feet
    bool hasAim;
    float aim[3];            // the aim point (world): what the screen's centre looks at, every weapon fires at it
    bool hasLock;
    float lock[3];           // the missiles' locked target (world)
    // The flight (sazabi_flight.h) and the special weapon, for the reticle's gauges and readouts:
    float altitude;          // m: its soles over what is under them (0 on its feet)
    float speed;             // m/s over the ground
    float climb;             // m/s up (+) / down (-)
    bool boosting;           // the thrusters burning (a boost dash, a climb)
    bool overheat;           // the thrusters spent: no boost until it lands and they cool
    int special;             // the special used last: 0 shield missiles, 1 funnels, 2 mega particle cannon
    bool keys;               // the pilot on the keyboard and mouse (else a pad): which bindings the HUD names
    float aimRange;          // m from the muzzle to the aim point
    bool aimHit;             // the centre's ray meets something within the reticle's reach (else the aim is its far end)
    bool centred;            // the camera is the Sazabi's own (sazabi.cpp): the aim point is the screen's centre
    bool hasAssist;
    float assist[3];         // the aim assist's enemy (its lock point, world): the aim point is it (sazabi_assist.h)
    bool lockOn;             // ...held by the lock-on (its key pressed: the camera follows it)
};
bool PlayerSazabiCue(SazabiCue* out) noexcept;
// The player's Sazabi's riding camera (turretcam.cpp's look-at hook): seat `seat`'s eye and look point, false when the
// seat is no player's Sazabi's (the stock camera then).
bool SazabiCamera(const unsigned char* seat,const float* wasEye,const float* wasLook,float* eye,float* look) noexcept;
void SazabiFrame(unsigned char* vehicle) noexcept;   // crew.cpp InputHook, after the stock input
void DreadnoughtFrame(unsigned char* vehicle) noexcept;   // crew.cpp InputHook, after the stock input
// The fixed emplacements (emplacement.cpp): one (a V603 with the marker bone), and its driver's drive stick cleared
// (crew.cpp InputHook, just before the stock input).
bool IsEmplacement(const unsigned char* vehicle) noexcept;
void EmplacementInput(unsigned char* vehicle) noexcept;
void DreadnoughtInput(unsigned char* vehicle) noexcept;   // crew.cpp InputHook, before it: the local rider's aim stick
bool InstallDreadnought() noexcept;                   // after InstallBody506
void ResetDreadnought() noexcept;                     // a new mission
bool InstallSazabi() noexcept;                        // after InstallBody506
void ResetSazabi() noexcept;                          // a new mission
// An impact `by` a vehicle (a crash, jet.cpp / playerjet.cpp; a ground vehicle's ram, vehicleram.cpp) at `at`: `damage`
// to the enemies of its side within about `radius` metres (the charge nearest that size: vehicleram.h NearestCharge; a
// charge of the vehicle's own, as the blast drones' is: its team, its kills, friends untouched). False when it could not
// be dealt (no charge preloaded this mission).
bool ImpactDamage(const unsigned char* by,const float* at,float damage,float radius) noexcept;
// vehicleram.cpp: the ground vehicles' ram (README 载具撞击伤害). Install at load (the CarBase mass read's check, the
// Barga's own update hook: the one class crew.cpp does not chain); the frame from every vehicle's input (crew.cpp, the
// Proteus's among them) and that hook.
bool InstallVehicleRam() noexcept;
void VehicleRamFrame(unsigned char* vehicle) noexcept;
void ResetVehicleRams() noexcept;
// jet_bay.cpp: a bite of the drill tank's drill: its charge fired by `by` straight from `from` at `at` with `damage`
// (its side's enemies, its kills, the map's buildings and rocks). False when not fired (not preloaded this mission).
bool DrillCharge(const unsigned char* by,const float* from,const float* at,float damage) noexcept;

// drill.cpp: the drill tank (EDF6VC_DRILL.SGO, docs/drill-re.md). DrillInput before the stock input (the player's
// trigger taken for the drill), DrillFrame after it (spin, pose, bites).
bool InstallDrill() noexcept;
bool IsDrillTank(const void* vehicle) noexcept;
void DrillInput(unsigned char* vehicle) noexcept;
void DrillFrame(unsigned char* vehicle) noexcept;
void ResetDrills() noexcept;
// The local player's drill (hud.cpp): its RPM, the top RPM, whether it touches something now, whether it is launched
// (flying) and on its way back (returning). False with none.
struct DrillCue { float rpm,maxRpm,heat; bool touching,overheated,flying=false,returning=false,keys=true; };
bool PlayerDrillCue(DrillCue* out) noexcept;

// jet_bay.cpp: the EMC's rounds (emc.cpp; pylib/vcobjects.py EMC_*, tools/make_emc.py), DemoIndirectFire objects owned
// by the EMC (its team: its side's enemies hurt, its kills, friends spared): the beam, the charge's glow (sight), the
// break charge fired at each building on the beam's line, the blast at its end. Ready: preloaded this mission.
// The Sazabi's beams are made and fired the same way (sazabi.cpp; pylib/vcobjects.py SAZABI_ROUND_FILES): the mega
// particle cannon's beam, its charge's glow at the chest, a funnel's burst. The Proteus's shield is raised the same way
// (proteus_shield.inc; tools/make_proteus.py): one BarrierBullet01, the stock Air Raider's electromagnetic barrier.
enum class EmcRound { beam, sight, breakCharge, blast, szMega, szCharge, szFunnel, proteusShield,
                      ultSight, ultBolt, ultSpark, ultBeam, ultCore, ultBlast, ultGather, barrage, barrageBurst, barrageMissile,
                      andryushaBlast, andryushaFire };
struct RoundObj { unsigned char* obj; const void* ctrl; };   // an object and its weak-this control block (none: obj null)
bool EmcRoundReady(EmcRound kind) noexcept;
RoundObj EmcFire(EmcRound kind,const unsigned char* by,const float* from,const float* at,float damage) noexcept;
bool RoundSteer(const RoundObj& r,const float* from,const float* at) noexcept;   // its next rounds' start and aim; false: gone
bool RoundSize(const RoundObj& r,float size) noexcept;       // its next rounds' thickness (AmmoSize)
bool RoundBlast(const RoundObj& r,float radius) noexcept;    // its next rounds' blast radius (AmmoExplosion)
void RoundDrop(RoundObj& r) noexcept;                        // deleted while it is there; forgotten
bool EmcIfcOk() noexcept;                                    // RoundSize / RoundBlast's IFC fields are where they write

// jet_bay.cpp: a long-range barrage's round (barrage.cpp; EmcRound::barrage): fired by `by` from `from` toward `aim` with
// `damage`, its AmmoSize `size`, blast `blast` m and AmmoHitSizeAdjust `mark` (the plugin's flight tells it by that); a
// cluster's flash where its bomblets leave (EmcRound::barrageBurst, no damage). False: not fired (not preloaded, the IFC's
// fields not where they are written).
bool BarrageFire(const unsigned char* by,const float* from,const float* aim,float damage,float size,float blast,float mark,bool missile) noexcept;
void BarrageBurst(const unsigned char* by,const float* at) noexcept;
// jet_bay.cpp: one of the Andryusha's bomblets (andryusha.cpp; docs/andryusha-re.md): two rounds of `by` on one arc from
// `from` onto `aim` (the IFC's own ballistic solve, both at one speed and gravity): its blast (EmcRound::andryushaBlast,
// GrenadeBullet01, `blast` damage) and its incendiary (EmcRound::andryushaFire, NapalmBullet01: the ground fire where it
// lands, `fire` damage). False: neither fired (not preloaded this mission).
bool AndryushaBomblet(const unsigned char* by,const float* from,const float* aim,float blast,float fire) noexcept;
// barrage.cpp: the long-range barrages (docs/plan-2026-10-25-strikes.md §1.1, src/barrage_plan.h): the map's bar
// (airstrike.cpp SupportStrikeAt) starts kind `kind` (barrage::Kind) at `at` along `heading` by `caller` (this machine's
// player) with `damage` a round; it is told to every peer (barrage_net_protocol.h), which plays it on its copy of the caller.
// BarrageTick fires its rounds (SharedFrameTick); the rounds' flight is RocketBullet01's update wrapped (InstallBarrages, at
// load). Anything but started: not started, and why (said on the bar's line; its pt are not taken).
enum class BarrageResult : int { started, noTarget, noHook, full, noCaller, noRounds };
BarrageResult BarrageStart(int kind,const unsigned char* caller,const float* at,const float* heading,float damage) noexcept;
void BarrageTick() noexcept;
void ResetBarrages() noexcept;
bool InstallBarrages() noexcept;
// airstrike.cpp: the caller's HUD line for its strike calls (hud.cpp StrikeBanner, the draw thread): a sortie's second wave
// not called; a moment.
bool PlayerStrikeCue(wchar_t* out,std::size_t capacity) noexcept;

// ultimate.cpp: the ultimate strike (docs/plan-2026-10-19-ultimate.md, src/ultimate_plan.h): the map's bar (airstrike.cpp
// SupportStrikeAt) starts its show at `at` by `caller` with `damage`; UltimateTick plays it a frame at a time
// (SharedFrameTick); ResetUltimate at a new mission forgets it, nothing touched (its objects were the last mission's).
// False: no show (its rounds not preloaded, every show slot taken; its pt are not taken).
// Several play at once; online every machine plays each machine's player's shows (ultimate_net_protocol.h).
bool UltimateStart(const unsigned char* caller,const float* at,float damage,std::uint64_t seed) noexcept;
void UltimateTick() noexcept;
void ResetUltimate() noexcept;
void PreloadUltimate(void* mgr) noexcept;   // the magic circles, with the shells (jet_spawn.cpp)

// emc.cpp: the EMC's charged beam (docs/emc-re.md, README EMC 蓄力光束): the player's trigger in a Vehicle510_Maser charges,
// and one thick beam carries the stock burst's damage through every enemy and building on its line, a blast at its end.
// EmcInput before the stock input (the trigger taken for the charge), EmcFrame after it.
bool InstallEmc() noexcept;
bool IsEmc(const void* vehicle) noexcept;
void EmcInput(unsigned char* vehicle) noexcept;
void EmcFrame(unsigned char* vehicle) noexcept;
void ResetEmc() noexcept;
void EmcTick() noexcept;   // once a frame: an EMC gone mid-charge or mid-beam has its sound and rounds dropped
// The local player's EMC (hud.cpp): the charge (0..1), the beam's seconds left, the beams its rounds still make (the stock
// burst's rounds each), and its state; `pos` the EMC's (the HUD draws the line only on that vehicle's block). False with none.
struct EmcCue { float charge,beamLeft,rearm; int beams; bool charging,firing,empty; float pos[3]; };
bool PlayerEmcCue(EmcCue* out) noexcept;

// sidecar.cpp: the sidecar motorcycle (EDF6VC_SIDECAR.SGO, docs/sidecar-re.md): a Freed bike whose second rider stands
// in the sidecar on foot (their own weapons), held there by the plugin. SidecarFrame after the stock input (the held
// gunner put back, an NPC gunner taken in, the bike driven for the player in the sidecar); SidecarBoard from the board
// button (crew.cpp FindSeatHook, before the stock seat search): true when it took the player into the sidecar (no seat
// then); SidecarHoldsPlayer: the player stands in its sidecar (crew.cpp gives it no NPC driver: the plugin drives);
// SidecarLevel: from the car step's setAngVel (physics.cpp), the roll part of a sidecar bike's angular velocity
// replaced by its way back to level.
bool InstallSidecar() noexcept;
bool IsSidecar(const void* vehicle) noexcept;
void SidecarFrame(unsigned char* vehicle) noexcept;
bool SidecarBoard(unsigned char* vehicle,unsigned char* human) noexcept;
bool SidecarHoldsPlayer(const void* vehicle) noexcept;
// crew.cpp (game thread): the vehicle has been started this mission: someone other than this machine's player (an NPC,
// another machine's player) has been aboard, or PowerVehicle said this machine's player started it (a panoramic
// cockpit's boot, cockpit_instruments.h kBootSteps). A vehicle the crew table has no room for reads as never started.
bool VehiclePowered(const void* vehicle) noexcept;
void PowerVehicle(const void* vehicle) noexcept;
// Projectile candidates and explosion targets: only this passenger's current bike and its native driver.
bool SidecarBulletPass(const void* owner,const void* target,const void* ownerCtrl) noexcept;
// The game's warp of a human's character controller to `pos` (sidecar.cpp, as the ride exit does); false: not known.
bool WarpHuman(unsigned char* human,const float* pos) noexcept;
int SidecarPassengers() noexcept;     // passengers riding now (the bullets' hook's quick "nothing to pass" test)
bool SidecarBulletHooked() noexcept;  // jet_hooks.cpp: the bullets' candidate hook is in (InstallBulletPass)
// jet_hooks.cpp: Dreadnought `v`'s hull bodies in slot `slot` (0..3, dreadnought.cpp's ships; nullptr: none), its own and its
// riders' rounds pass through them.
void HullBodies(int slot,const unsigned char* v) noexcept;
void SidecarLevel(const void* body,float* w) noexcept;
void ResetSidecars() noexcept;
// physics.cpp: the car step's final setAngVel (0x6746C6) goes through the plugin (SidecarLevel), redirected at load.
bool SidecarLevelHooked() noexcept;

// highcam.cpp: the artillery's high camera (an indirect-fire vehicle the player drives: its key / pad button switches
// the vehicle's camera block between its own points and a high view, docs/camera-re.md). From every vehicle's input,
// the plugin off too (it gives the block back then). PlayerHighCam (hud.cpp): whether the toggle is offered this
// moment, the view's state and whether the player is on keys (else a pad).
void HighCamFrame(unsigned char* vehicle) noexcept;
bool PlayerHighCam(bool* on,bool* keys) noexcept;
bool HighCamOn(const void* vehicle) noexcept;   // turretcam.cpp: the high view is on in `vehicle` now
bool TurretCamHighTransition(const void* vehicle) noexcept; // includes the return from overhead to the normal camera
// The weapon seat `seat` of `vehicle`'s turret is laid by (turretcam.cpp TurretGun: the fire-control pick when it turns
// with both of the seat's axes, else the seat's first weapon that does); nullptr: none. vhud.cpp: whose aim overlay it is.
const unsigned char* PlayerTurretGun(const unsigned char* vehicle,const unsigned char* seat) noexcept;
// The seat holds an indirect-fire weapon (the Katyusha's rockets, the howitzer's shells: lofted or ground marked, rounds
// living 10 s or more): its high view (HighCamClass 1) and no gun stabilizer (stab.cpp: it fires from a halt).
bool IndirectFireSeat(const unsigned char* seat) noexcept;
void ResetHighCam() noexcept;
// The high view is offered in `vehicle` (seat 0 the player's, HighCam on): its button is the high view's there.
bool HighCamOffered(unsigned char* vehicle) noexcept;
// Game thread, after HighCamFrame: this frame the shared pad button is the gunner sight's in `vehicle` (first person,
// a mounted optic, the high view off), not the high view's.
bool HighCamYieldsButton(const void* vehicle) noexcept;

// sightzoom.cpp: a vehicle gun's sight magnified (src/sightzoom.h, docs/zoom-re.md). InstallSightZoom at load (the
// player camera's step, chained with map.cpp's); SightZoomFrame from the frame of the seat the player sits at (its key
// or, `padButton`, its pad button steps 1x -> 3x -> 6x; a seat taken again starts at 1x), SightZoomStock from every stock
// vehicle's input (the player's seat in it); SightZoomNow: the magnification now (in `vehicle`, nullptr any), 1 with none.
bool InstallSightZoom() noexcept;
void SightZoomFrame(unsigned char* vehicle,unsigned seat,bool padButton) noexcept;
void SightZoomStock(unsigned char* vehicle) noexcept;
float SightZoomNow(const void* vehicle) noexcept;
bool SightZoomCanMount(const void* vehicle,unsigned seat) noexcept; // game-thread query: real current optic, including at 1x
bool SightZoomMounted(const void* vehicle=nullptr) noexcept; // active physical optic; turret input stays native
// The seat's sight raised (magnified, or at 1x in first person by the sight key: sightzoom.h Press), the look key aside.
bool SightZoomRaised(const void* vehicle=nullptr) noexcept;
// Current validated seat/weapon fire-control surface; none when unavailable or another camera owns the view.
sightzoom::Kind SightZoomView(const void* vehicle=nullptr) noexcept;
// The sight's field over the view now (sightzoom.h ScopeField, from one snapshot of the seat's cue): the vehicle, the
// soldier looking through it, its fire-control surface and magnification; false (all empty) with none, or while another
// camera owns the view. hud.cpp draws the field by it, and so holds the stock crosshair back for that soldier.
struct SightScope { const void* vehicle=nullptr; const void* human=nullptr; sightzoom::Kind kind=sightzoom::Kind::none; float zoom=1.0f;
                   sightzoom::Eyepiece eyepiece=sightzoom::Eyepiece::binocular; };
bool SightZoomScope(SightScope* out) noexcept;
// The sight's range setting (sightzoom.h kZeroStep..; 0 the bore line) and the superelevation, rad, it lays the line of
// sight under the bore for the weapon looked through now; any thread, 0 with no magnified optic.
int SightZoomRange(const void* vehicle=nullptr,float* angle=nullptr) noexcept;
// The range drum laid on the gun (sightzoom.h LayStep): from every seat aim's step, after the stock step and the
// stabilizer (stab.cpp StepHook), `before` the pitch axis' angle before that step. Only the player's magnified sight's
// aim is touched: its pitch axis turned toward the set range's superelevation over the line of sight (game thread).
void SightLayStep(unsigned char* aim,float before) noexcept;
void ResetSightZoom() noexcept;
// map.cpp: the game window's mouse wheel for the magnified sight's range (`hold` while it is up: the wheel's notches are
// swallowed and counted, up +; the count since the last call), and the first XInput pad's buttons now (false: none).
int TakeScopeWheel(bool hold) noexcept;
bool MapPadButtons(std::uint16_t* buttons) noexcept;
// The same pad with its analog triggers (0..255), for the controlled enemy's attack keys.
bool MapPadState(std::uint16_t* buttons,std::uint8_t* leftTrigger,std::uint8_t* rightTrigger) noexcept;

// turretcam.cpp: the turret camera (README 炮塔镜头, docs/camera-re.md §3b, §5). InstallTurretCam at load (the riding
// camera's look-at fetch, the seat aim's step); TurretCamFrame from every vehicle's input, the plugin off too (it lets
// go then). TurretCamServes: it places the camera of `vehicle` (the player's turret, seat 0); TurretCamLarge: and its
// rig is a big vehicle's (highcam.cpp HighCamClass 2).
bool InstallTurretCam() noexcept;
void TurretCamFrame(unsigned char* vehicle) noexcept;
bool TurretCamServes(const void* vehicle) noexcept;
bool TurretCamLarge(const void* vehicle) noexcept;
// TurretCamTurret: the camera is decoupled in `vehicle`'s seat `seat` (the player's turret follows the view: the right
// stick turns the camera; EDF6AutoTurret asks, common/edf/aimlink.h CameraTurret). TurretCamSteers: and the camera
// turned that seat's aim itself in its last step (not another plugin's hand): nix.cpp then leaves the torso's yaw to it.
bool TurretCamTurret(const void* vehicle,unsigned seat) noexcept;
bool TurretCamSteers(const void* vehicle) noexcept;
// TurretCamFreeLook: the free-look key in `vehicle`'s seat `seat` is the turret camera's own (War Thunder's: the view
// swings round the vehicle on the rig, the turret holds the point it was on and the HUD keeps that point's circle,
// TurretMark): seat 0 of the turret it places, in third person, the optic not up. vehicle_look.cpp yields to it there
// (the user, 2026-10-21: "鼠标中键的时候应该是绕着车转…战雷一样的那个圆的鼠标指示点怎么没了").
bool TurretCamFreeLook(const void* vehicle,unsigned seat) noexcept;
void ResetTurretCam() noexcept;
// The chase follower's roll (turretcam.cpp, chase_cam.h; README 载具镜头跟随): from the camera step after the native one
// (firstperson.cpp CameraAfterNative), with the camera's rows (+0x220) in third person to turn them about their forward
// row onto this step's followed up (a jet's or a heli's partial bank), null where another view owns them (first person,
// a controlled enemy): either way this step's roll is used up.
void ChaseCamUp(const unsigned char* cam,float* rows) noexcept;
// The player's turret against their view (hud.cpp's marker; any HUD may draw it), fresh within 200 ms while the camera
// is decoupled or looking round: `aim` the point the turret is sent to (under the screen's centre, or the one it holds
// in free look), `gun` where the gun's round would be at that point's range as it points now (on its arc), the muzzle
// and its direction; `onTarget` both axes within half a degree of their want.
struct TurretCamReadout { bool decoupled,freeLook,high,onTarget; float aim[3],gun[3],muzzle[3],gunDir[3]; bool physicalOnly=true; bool aimValid=false; };
bool PlayerTurretCam(TurretCamReadout* out) noexcept;

// stab.cpp: the gun stabilizer (README 炮管稳定器, docs/camera-re.md §7). InstallStabilizer at load (the plain seat aim's
// step; the AddSe one comes through turretcam.cpp's hook, which calls StabStep in place of the next step); StabFrame from
// every vehicle's input (registers its seats' aims); StabHeld: for a controller of `aim`'s gun before this frame's step
// (turretcam.cpp Steer, EDF6AutoTurret through common/edf/aimlink.h V3), the axes the stabilizer holds it at with no
// command (`held`, rad, the aim's senses) and how much of that is the hull's turn since the last step (`hull`): it steers
// from `held` and takes `hull` out of its want's drift and of the axes' motion it learns from (false: not held; `held`
// the axes as they are, `hull` 0); `frame` (9 floats, may be null; written only when held): the world rows (x, up, nose)
// `held` is seen in, the frame its wants must be seen in too (stab.h HeldIn); StabState: 1 the seat's gun is held, 2
// held but the drive is outrun, 0 not held (vhud.cpp).
using AimStepFn=void(__fastcall*)(void*,const float*);
bool InstallStabilizer() noexcept;
void StabFrame(unsigned char* vehicle) noexcept;
void StabStep(void* aim,const float* in,AimStepFn next) noexcept;
bool StabHeld(const void* aim,float* held,float* hull,float* frame) noexcept;
int StabState(unsigned char* vehicle,unsigned seat) noexcept;
// RemapAimAxis: after `aim`'s step this frame, set its axis `axis` (0 yaw, 1 pitch) to `corrected` and map it onto the
// bones with the whole turn from `before` (its angle before the step) as the stabilizer's own correction is (0x5FC280,
// stab.h Remap); a gun the stabilizer holds has its held line moved with it, so the turn is kept, not pulled back
// (stab.h Shift). False when the bone map is not in (AimAxisAdjustable).
bool AimAxisAdjustable() noexcept;
bool RemapAimAxis(unsigned char* aim,int axis,float before,float corrected) noexcept;
void ResetStabilizer() noexcept;

// airstrike.cpp
bool InstallAirstrikes() noexcept;
void SupportDispatchTick() noexcept;
void ResetSupportDispatch() noexcept;

// subcarrier.cpp: the submarine carrier (潜水母艦, docs/subcarrier-re.md), a 506 body from EDF6VC_SUB_CARRIER.SGO
// (tools/make_sub.py) driven by the plugin: it sits surfaced, follows the player at a ship's pace, turns its bow
// on the nearest enemy, fires its turret guns and homing missiles, reloads aboard; its HP shows as a follower gauge.
bool IsSub(const void* vehicle) noexcept;         // a 506 body with the carrier's mark
void SubFrame(unsigned char* vehicle) noexcept;   // from every vehicle's input hook (crew.cpp SubStep), carriers only
bool InstallSub() noexcept;                       // after InstallJets (it chains onto the 506 physics slot)
void PreloadSub() noexcept;                       // from the mission's player preload
// A carrier made at run time on the ground at `pos` (metres; it is raised to sit on the highest ground under
// its hull), its bow along `heading` (horizontal part used), friend, NPC pilot: the vehicle, or nullptr (its
// files not installed or not preloaded this mission, three already out, the game failed to build it).
unsigned char* SubLaunch(const float* pos,const float* heading) noexcept;
// The live carrier nearest to `from`: a point on its flat bow deck (the hull box top, 193 m over its origin)
// nearest to `from`, into `deck`; false with no carrier out.
bool SubDeck(const float* from,float* deck) noexcept;
// Horizontal metres from `p` to the nearest live carrier's hull footprint (0 over it), -1 with none.
float SubHullGap(const float* p) noexcept;
// carrierlaser.cpp: the e508 teleportation ships' portal laser (warning beam, interruptible charge, main beam)
// while a submarine carrier is out; its SGOs from tools/make_jets.py (EDF6VC_PORTAL_SIGHT / _LASER.SGO).
bool InstallLaser() noexcept;                         // at load
void PreloadLaser() noexcept;                         // from the mission's player preload
void CarrierLaserFrame(const unsigned char* sub) noexcept;   // from a flown carrier's frame, at most once a frame
// physics.cpp: stock EDF6 physics defects, patched at load (needs a game restart to toggle)
bool InstallPhysics() noexcept;

// What jet.cpp flies a jet as (game thread): its role's name, seconds of fuel left (-1: none, a carrier's drone),
// a carrier's drone launches left (-1: not a carrier), whether it is withdrawing; false when it does not fly it.
struct JetHudInfo { const char* role; float fuelSec; int drones; bool leaving; };
bool JetHud(const void* vehicle,JetHudInfo* out) noexcept;
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
// The Dreadnought aboard (dreadnought_turrets.inc): the local player's camera on its bridge or at a turret (turretcam.cpp
// LookHook), their sight (hud.cpp GunnerMarks), and a round of a turret (jet_bay.cpp: the gunship's cannon round, from
// `from` at `at`; false: not fired).
bool DreadnoughtCamera(const unsigned char* seat,float* eye,float* look) noexcept;
bool DreadnoughtGunnerHud(GunnerReadout* out) noexcept;
// The ship's readout for the local player in any of its seats (hud.cpp DreadnoughtHud): `seat` 0 its bridge, 1 + n turret
// n; its way (m/s), climb (m/s), throttle (-0.35 .. 1), boost left (0 .. 1), keel over the ground (m, -1 none seen),
// heading (deg, 0 north, clockwise), hull; its turrets by bit (trained by this player, by other players, by the commander
// for the guns nobody mans, on their aim now); its hangar (fighters stowed, out, on the lifts' way up or down; s till the
// next flight may go); keyboard or pad; first person.
struct DreadnoughtCue {
    int seat,turrets;
    float speed,climb,throttle,boost,keel,heading,hp,hpMax;
    bool boosting;
    std::uint8_t mine,others,commanded,onAim;
    int stowed,out,lifting;
    float flightWait;
    bool keys,firstPerson;
};
bool PlayerDreadnoughtCue(DreadnoughtCue* out) noexcept;
// `real`: the gunner's own round (its damage); false: a remote gunner's, seen here (no damage: theirs is theirs).
bool DreadnoughtRound(unsigned char* owner,int round,const float* from,const float* at,bool real) noexcept;
// The vehicle class (crew.cpp kClasses) of an object by its vtable, -1 for anything else (a board-able vehicle or not).
int VehicleClassOf(const void* object) noexcept;
// The seat buttons `vehicle`'s stock player input tests in its seat `seat` (seat_switch_logic.h CarPadReads: the Car's
// path, a Grape's driver A / LB / L3 horn), 0 for the classes that read none (or are not reversed).
unsigned StockPadReads(const void* vehicle,unsigned seat) noexcept;
// The boarding gun (boarding.cpp): a real friendly hit suppresses damage and asks the game thread to board the
// vehicle. Broadphase candidates alone never request boarding; FrameTick handles the pending actual hit.
bool PlayerJetBoardingSupported(const void* vehicle) noexcept; // aircraft controls, without the on-foot reach/speed gate
void BoardingTick() noexcept;
bool InstallBoarding() noexcept;   // after CheckHeliProfile (the board button)
void ResetBoarding() noexcept;
// While the boarding gun presses the board button for the player: the one vehicle a seat may be found in (crew.cpp
// FindSeatHook gives none in any other), else null.
const void* BoardingOnly() noexcept;
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
// launcher.cpp: the Katyusha's impact point (CCIP) while the player rides a vehicle whose seat 0 holds a launcher marked
// lofted (edf::Lofted, common/edf/weapon.h): where a rocket fired now comes down (reach: it does within its life), `ring` points
// round it its ripple's spread reaches (FireAccuracy's cone), the horizontal range, the flight in seconds and the
// launcher's elevation in degrees. LauncherFrame from every vehicle's input (game thread); PlayerLauncher the last one,
// false with none this moment.
constexpr int kLauncherRing=16;
struct LauncherReadout {
    float impact[3];
    float ring[kLauncherRing][3];
    int rings;
    float range,flight,elevation;
    bool reach;
    int fuse;      // the Andryusha's fuse (andryusha.cpp AndryushaSeat: 1 impact, 2 airburst), 0 another launcher
    bool keys;     // the rider plays on the keyboard (the fuse switch's key named), else the pad
};
// A round's arc as the game steps it (a frame: vel += drop, pos += vel; m/frame, m/frame^2) from `pos` for at most
// `frames` frames: the first ground a map ray finds along it (`hit`) and the frames it took. launcher.cpp.
bool RoundImpact(const float* pos,const float* vel,const float* drop,int frames,float* hit,float* took) noexcept;
void LauncherFrame(unsigned char* vehicle) noexcept;
// andryusha.cpp: the Andryusha's incendiary cluster rockets (docs/andryusha-re.md, src/andryusha.h). InstallAndryusha wraps
// GrenadeBullet01's update (chained with EDF6AutoTurret's); AndryushaTick (SharedFrameTick) runs the airburst fuses' ground
// probes and splits the carriers that burst into bomblets (AndryushaBomblet); AndryushaSeat (LauncherFrame, the player
// on seat 0 of a lofted launcher whose weapon is `weapon`) turns the fuse on VehicleSecondarySwitchKey / -Button and
// returns it (andryusha::Fuse: 0 not the Andryusha's weapon, 1 impact, 2 airburst).
bool InstallAndryusha() noexcept;
void AndryushaTick() noexcept;
int AndryushaSeat(const unsigned char* vehicle,const unsigned char* seat,unsigned char* weapon) noexcept;
// Each vehicle's frame (crew.cpp, after LauncherFrame): an Andryusha's fuse sent to its copies by the machine that speaks
// for it (andryusha_net.h: its player on the launcher's seat, else the machine that runs it); the copies write it.
void AndryushaFrame(unsigned char* vehicle) noexcept;
void ResetAndryusha() noexcept;
bool PlayerLauncher(LauncherReadout* out) noexcept;
// katyusha.cpp: the Katyusha's launcher pose. The 402 class's pose call is detoured: after the stock pose the launcher
// is held at the elevation LauncherFrame asks for with SetLauncherLoft (the arc onto where the player's camera looks;
// `aim` false: none, the stock pose, the player's own aim), eased at the stock turret's rate, and the telescopic ram's
// two bones are aimed at each other's pivot; the launcher is never posed under the floor over the cab at the turntable's
// azimuth (common/edf/katyusha_cab.h). LauncherLoft: the last pose of `vehicle` (rad; held: off the stock pose).
bool InstallKatyusha() noexcept;
void SetLauncherLoft(const void* vehicle,bool aim,float elevation) noexcept;
struct LoftReadout { bool held; float elevation,stock,ramTurn,ramLength,azimuth,floor; };
bool LauncherLoft(const void* vehicle,LoftReadout* out) noexcept;
void ResetKatyushas() noexcept;
// Whether the player in `vehicle` sees our gun sight instead of the stock aim lines (crew.cpp AimLines): an aircraft
// the player-jet flight flies (playerjet.cpp), ini PlayerJetGunSight on.
bool PlayerJetOwnSight(const void* vehicle) noexcept;
// Whether `vehicle` is one of the plugin's aircraft the player-jet flight flies (a kind in playerjet_kinds.h), whatever
// the ini says (firstperson.cpp: a flown craft's pilot monitor is no gun camera).
bool PlayerJetCraft(const void* vehicle) noexcept;
// helisight.cpp: the weapons' sight of a stock helicopter (heli.cpp IsHelicopter, no plugin body) the player flies or
// mans (ini PlayerHeliGunSight). The gun (the primary trigger's: `gun`): `bore` the way its muzzle points (a
// direction), `pipper` where a round fired now first hits the map along its real arc (hit), else where it is at the
// end of its life; `range` m from the muzzle to it. The other weapon (the secondary button's: `arm`, HeliArm):
// `armBore` its muzzle's way; a missile's lock (`lock` 2 locked / 1 locking, `lockProgress` 0..1, `armAt` the target's
// lock point, as the jets' stores read it: stores.h StoreLock) or with none its LockonRange (`lockRange`); the rockets'
// (or a dropped weapon's) mark `armAt` where their path as the game flies them meets the map (`armHit`; vhud.h
// RoundLands), `armRange` m to it, `armLabel` what it is (RKT, GREN...). HeliSightFrame from every vehicle's input
// (game thread); PlayerHeliSight the last one, false with none this moment (hud.cpp HudPublish).
enum class HeliArm : std::uint8_t { none, missile, rockets };
struct HeliSightReadout {
    bool gun,hit;
    bool physicalOnly=true;
    int paths=0;
    roundaim::Impact path[roundaim::kSightPaths]{};
    float bore[3],pipper[3],range;
    HeliArm arm;
    bool armHit;
    int lock;
    float armBore[3],armAt[3],armRange,lockProgress,lockRange;
    const char* armLabel;
    gunsight::Ladder ladder;     // the gun's range ladder (gunsight.h; no ticks: none), under its boresight
};
bool PlayerHeliOwnSight(const void* vehicle) noexcept;
void HeliSightFrame(unsigned char* vehicle) noexcept;
// netprobe.cpp: Debug=1, online only: once a second per helicopter-class vehicle, which machine runs it and how its
// pose replication stands (the NET lines, docs/online-re.md). Reads only.
void NetProbe(unsigned char* vehicle) noexcept;
// Whether this machine is in an online session (netprobe.cpp; false when the session function is not the one read).
// This is a session query, not proof that an object participates in replication: plugin call aircraft do not
// register a network identity; delivered vehicles do (docs/online-re.md sections 1 and 2).
bool InSession() noexcept;
bool PlayerHeliSight(HeliSightReadout* out) noexcept;
// crew.cpp: the seat's weapons whose stock aim line AimLines has hidden now (the walk it hides them by), at most
// `most`; how many.
int HiddenAimGuns(const unsigned char* seat,const unsigned char** out,int most) noexcept;
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
namespace jet { bool SpawnReady() noexcept; bool PassThrough() noexcept;   // jet_hooks.cpp: the addBody hook is in
 bool ModFileThere(const wchar_t* file) noexcept; bool LockingOn(const void* target) noexcept;
// jet.cpp: the jets locking on to `target` lose their lock with `chance` each (a flare drop); how many did
int BreakLocks(const void* target,float chance) noexcept;
// jet.cpp: the jets with `target` in their missile lock now (LockingOn's): where they are, at most `most`; how many
int LockersOf(const void* target,float (*at)[3],int most) noexcept; }
// missile.cpp: a guided round now homing on a point within `radius` m of `at` (its lock point there)
bool MissileHoming(const float* at,float radius) noexcept;
// missile.cpp: MissileHoming's rounds: where they are, at most `most`; how many (all of them, past `most` too)
int MissilesHomingAt(const float* at,float radius,float (*pos)[3],int most) noexcept;
// missile.cpp: flares. A flare dropped by `owner` at `at` (m/s `vel`): the rounds homing there may take it for their
// target. FlaresStep moves them once a frame; FlaresOf: `owner`'s burning flares, at most `most` (their places, speeds).
// `nose`: the jet's nose (the aspect); `pairStart`: the first flare of a drop (each drop is judged once a round).
void FlareDrop(const void* owner,const float* at,const float* vel,const float* nose,bool pairStart) noexcept;
void FlaresStep() noexcept;
// missile.cpp: round `b` (`age` frames into its flight, at `pos` flying along `dir`) tracked for the missile warnings and
// the flares, homing on the lock held at `lockAt` (entry, control block): the point it flies at (its lock point, or the
// flare that fooled it) into `aim` and that point's velocity (m a frame; zero at first and after a jump) into `tv`.
// False: no live lock (it flies on).
bool TrackRound(const unsigned char* b,std::int32_t age,const float* pos,const float* dir,const unsigned char* lockAt,
                float* aim,float* tv) noexcept;
int FlaresOf(const void* owner,float (*at)[3],float (*vel)[3],int most) noexcept;
// booster.cpp: the flares' fire, drawn as Booster flames on `v` (their owner) at `at`, trailing against `vel`.
void FlareFlames(const unsigned char* v,const float (*at)[3],const float (*vel)[3],int n,ULONGLONG ms) noexcept;
bool InstallPlayerJets() noexcept;                      // after InstallSub (it chains onto the 506 physics slot)

// The local player's human (plugin.cpp): the soldier a local camera follows, as its step reports it
// (SeeCameraPlayer, firstperson.cpp CamStepHook); nullptr when no camera reported it for kPlayerHumanMs or it is
// no longer the same live player object.
void SeeCameraPlayer(unsigned char* human) noexcept;
unsigned char* PlayerHuman() noexcept;

// debug_spawn.cpp (ini DebugSpawn, off by default; src/debug_spawn.h): spawn vehicles, the plugin's aircraft, enemies and
// friendly soldiers out of thin air where the crosshair points. No hook of its own: PreloadDebugSpawn from the mission's
// start (mission.cpp), DebugSpawnFrame from the player's frame (map.cpp MapHumanFrame, crew.cpp FrameTick; once a game
// frame whichever comes first), the menu drawn from the HUD pass (hud.cpp) through DebugSpawnReadout.
void Edf41Mother401DevTick() noexcept;   // development (Edf41Mother401Dev bit 32768): Edf41DevShoot at the mission's ordinary enemies, once a frame
void PreloadDebugSpawn() noexcept;
void DebugSpawnFrame(unsigned char* human) noexcept;
struct DebugSpawnCue {
    bool open;                // the menu is up
    int category;             // debugspawn::Category shown
    int row;                  // kEntries index picked (-1 none)
    int pick,count;           // its place in the category, of how many
    bool ready;               // the row can be spawned now (preloaded / its module up)
    int status;               // the last spawn's result, for a moment: DebugSpawnStatus (none: nothing to say)
    int statusRow;            // ...whose (kEntries index)
    wchar_t note[96];         // ...a support call's own answer (support_dispatch.cpp SupportCallAt), else empty
    int keys[5];              // the ini's keys: toggle, prev, next, category, spawn
};
// kDebugSpawnLoading: the row was not loaded this mission; its load is queued (debug_spawn.cpp RequestLoad) and the
// spawn is made at the same place as soon as the game reports its resources complete.
enum DebugSpawnStatus : int { kDebugSpawnNone, kDebugSpawnDone, kDebugSpawnFailed, kDebugSpawnOnline, kDebugSpawnNoPlace,
                                   kDebugSpawnRequested, kDebugSpawnRefused, kDebugSpawnLoading };
bool DebugSpawnReadout(DebugSpawnCue* out) noexcept;   // any thread; false: nothing to show
}  // namespace crew

// The core modules' declarations (self-contained; every file that includes crew.h sees them as before).
#include "core.h"
#include "ground.h"
#include "heli.h"
#include "hud.h"
#include "nix.h"
#include "vhud.h"
#include "mapbounds.h"
#include "payload.h"
#include "map.h"
#include "damagestats.h"
#include "mapcmd.h"
#include "proteus.h"
#include "npcai.h"
#include "qmark.h"
#include "infantryscope.h"

namespace crew {
bool InstallViewDepth() noexcept; bool ViewDepthReady() noexcept; void ViewWriteNear(void* camera,float value) noexcept;
// Release a near plane written through ViewWriteNear: while `written` is still the camera's value, the native baseline
// the shared clip lease recorded before any plugin write comes back. A newer value of another owner (the map) is left.
// Never a private snapshot of the camera: that may hold another layer's temporary value (the map's 0.5..5 m).
void ViewReturnNear(void* camera,float written) noexcept;
}
