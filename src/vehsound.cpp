// The vehicles' sounds (the user, 2026-10-06: "add or change the sounds: the engine and the turret turning, the shell
// loading and the case thrown out, and a more powerful main gun", then "the vehicles' sounds: change the like ones too";
// ini VehicleSound, docs/sound-re.md §9). Every vehicle the plugin sees (its input, crew.cpp InputHook) is heard through
// the plugin's own voices (jetaudio.cpp, the clips made in vsynth.h, the mix decided in vehmix.h), placed at the camera
// as the jets' engines are (SoundAt: panned to the ears, the Doppler ratio, the air's dulling far off):
//  - the engine (the CarBase classes: the tanks, the Kepler and the howitzer, the Titan, the Maser, the Grape and the
//    trucks, the Naegling, the bikes and the sidecar): a heavy V12, a light six or a V-twin, two layers faded across by
//    the stock engine's own load (+0x1A80) and pitched up with the revs (the load and the speed); a tank's tracks
//    clacking with its speed. It runs while someone sits in the driver's seat, winding up and down;
//  - the turret: the traverse drive whining with the fastest of the seats' aim axes against their top rate (their
//    rates, seat aim +0x1C / +0x5C: the stock axis step's), a stop's clunk when it comes to rest; the mechs' arms the
//    same drive, quicker (the Begaruta, the Nix: their begaruta_aim_se_table);
//  - every gun by its calibre (vehmix.h ProfileOf, kProfiles; the user, 2026-10-07: "closer to the real thing, by
//    calibre and round"): a main gun's report of its own (a 75-105 mm gun's, a 120 mm tank gun's, a 155 mm howitzer's, a
//    super-heavy gun's: the bigger the lower and the longer; a rail gun's discharge: a crack, no boom of powder), punchy
//    near and rumbling far; an autocannon's round, a 40 mm grenade launcher's thump; a machine gun's, a gatling's or the
//    flak's burst as a loop while it fires (its rate pitching it) with the cases raining near it and its echo when it
//    stops (rapid fire stacks no one-shots); a rocket's ignition off each rail of a ripple; a missile's launch. The
//    reports and rounds arrive late by the distance (sound at 340 m/s). Every vehicle's guns, the helicopters' and the
//    plugin's aircraft's too (their missiles keep the launch sounds their weapon files were made with); the engine and
//    the turret only on the ground. A shot fired by another machine's gunner (online) is heard by its shot count;
//  - the cases: each round's case landing as its calibre's (a 30-40 mm brass, a 40 mm grenade's aluminium, a 75-105 mm
//    brass case on the turret floor, the 120 mm combustible case's steel stub base inside the turret: muffled), after a
//    fall; none for a rail gun, rockets, a howitzer's or a super-heavy gun's separate loading; none either for a weapon
//    that throws its own physical case (ShellCase: +0x4E0), which lands with its own sound: one case, one sound;
//  - the loader, on a gun with a wait of a loader's length (vehmix.h ReloadCues): its calibre's steps, each at its time
//    after the shot or before it is ready (a tank gun's case out, round rammed, breech closed; the howitzer's breech
//    opened, shell rammed, charges pushed in one by one, breech closed, primer; the rail gun's capacitors charging and
//    its ready click; the rocket rails' next rockets latched on), drawn in when the wait is short (the fire interval
//    +0xE0C after a shot, the magazine's reload +0xE68 when it is empty: the stock gauge's own counters, vhud.cpp).
// The stock sounds these replace are silenced on that vehicle (its own copies: the SePresets live in the object): the
// engine's idle / drive / turn presets ([0], [1], [2] of car_base_se_table, the vector at +0x1A20) with their loops
// stopped (handles +0x1A40 / +0x1A50 / +0x1A60, CarBase's engine sound 0x676370), the turret's move / stop presets
// ([13], [14]: 0x632BD0 plays them per axis; the 403's and the Titan's second gun's [15], [16]), a mech's aim table
// (+0x1A38, entries 0xB0 apart: the seat aim plays them, 0x643857), each replaced gun's FireSe (weapon +0x380, played at
// 0x697FE4; a looped one's handle +0xE28 stopped): their cues cleared (a preset with no bank plays nothing, 0x7B4510)
// and kept to be given back the moment a group is off (its volume 0, VehicleSound=0, the plugin off) or the sounds
// cannot be made. A group whose volume is 0 keeps its stock sound. All addresses are RVAs into EDF.dll TimeDateStamp
// 0x678CCB46.
#include "crew.h"
#include "jetaudio.h"
#include "vehmix.h"
#include "vsynth.h"
#include "layout.h"
#include "memory.h"
#include "vecmath.h"
#include "edf/weapon.h"
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace crew {
namespace {
// CarBase's engine sound, in every class's vtable at kEngineSlot (0x250 / 8) the plugin gives an engine.
constexpr unsigned kEngineSound=0x676370;
constexpr std::size_t kEngineSlot=0x250/8;
constexpr std::size_t kPresets=0x1A28,kPresetCount=0x1A38,kPresetSize=0x80;   // car_base_se_table's SePresets (a vector)
constexpr std::size_t kCues=0x20,kCueBytes=0x50;   // a preset's five {bank, cue} (the ground's materials); bank null: silent
constexpr std::size_t kPresetPitch=0x70,kPresetVolume=0x74,kPresetLoops=0x78;
constexpr std::size_t kEngineLoad=0x1A80;           // the stock engine's load (0..1), eased: its drive loop's volume
constexpr int kEnginePresets[3]={0,1,2},kTurretPresets[4]={13,14,15,16};
constexpr std::size_t kEngineHandles[3]={0x1A40,0x1A50,0x1A60};
// A mech's begaruta_aim_se_table (0x63684F builds it at +0x1A38: an object of kAimTableVtable whose entries, kAimEntry
// apart, begin with their SePreset; 0x5F33F0 indexes them from +0x10, the count is at +0x20).
constexpr std::size_t kAimTable=0x1A38,kAimTableData=0x1A48,kAimTableCount=0x1A58,kAimEntry=0xB0;
constexpr unsigned kAimTableVtable=0x17DDEA8;
constexpr int kMostAimSounds=6;
// The weapon (docs/hud-re.md §7, vhud.cpp): FireSe's preset, the handle its loop plays on, FireInterval (frames), the
// wait to the next shot (frames left, a float), ReloadTime and its frames left.
constexpr std::size_t kFirePreset=0x380,kFireLoop=0xE28,kFireInterval=0x36C,kCooldown=0xE0C,kReloadTime=0x20C,kReloadLeft=0xE68;
// ...and what stands for its calibre (vehmix.h GunFacts): FireBurstCount, AmmoDamage, AmmoExplosion; the factory of the
// physical case it throws (ShellCase's class: null with none, docs/sound-re.md §9.5); the shot count it was sent
// (+0x1544, stored by 0x690420) and the shots its own copy fired (+0xBD0: 0x690420 counts them up, a local shot sends it).
constexpr std::size_t kFireBurst=0x370,kWeaponDamage=0x89C,kWeaponBlast=0x8B0,kCaseFactory=0x4E0,kWeaponShots=0x1544,kWeaponFired=0xBD0;
// The seat's aim: its params {brake, accel, top rate} and each axis' rate (rad a frame), turretcam.h AxisStep.
constexpr std::size_t kAimParams=0x90,kAxisRate=0xC;
constexpr unsigned kIsPlaying=0x7A8A20,kStopSound=0x7A8CD0;
using IsPlayingFn=bool(__fastcall*)(void*);
using StopSoundFn=void(__fastcall*)(void*,int);

using vmix::kEngineRef; using vmix::kTracksRef; using vmix::kTurretRef; using vmix::kReloadRef; using vmix::kTrackHalf;
using vmix::kEngineShare; using vmix::kTurretShare; using vmix::kReloadShare;
constexpr float kEngineHear=500.0f,kTurretHear=150.0f,kReloadHear=80.0f,kRapidHear=1200.0f,kBrassHear=40.0f;   // m: no voice farther
using vmix::kFarAt;                                 // m: the air's dulling at its fullest (the jets')
constexpr float kStartSec=1.2f,kStopSec=2.0f;       // the engine winding up / down
constexpr float kMechServo=1.25f;                   // a mech's arm drives: the turret's, quicker and higher
constexpr ULONGLONG kStaleMs=300;                   // a vehicle not seen this long (game ms) is gone: its voices too
constexpr int kVehicles=48,kMostGuns=8,kPending=96;   // a Katyusha's ripple: forty rockets on their way

enum class Body : std::uint8_t { none, heavy, light, bike, mech };
enum Loop : int { kLoopIdle, kLoopLoad, kLoopTracks, kLoopTurret, kLoopCount };
struct Saved { unsigned char cues[kCueBytes]; bool held; };
struct Gun {
    const unsigned char* w;
    vmix::Bore bore;
    bool stockCase;           // it throws a physical case of its own (kCaseFactory): no case sound of ours
    Saved fire;
    std::int32_t ammo,shots;  // its rounds, the shot count it was sent (kWeaponShots)
    float wait,total,since;   // frames: to the next shot / the cycle's full wait / since the shot
    int burst=-1,brass=-1;    // a rapid gun's loops while it fires (jetaudio.h OpenLoop), -1: none
};
struct Vehicle {
    ObjRef ref;
    ULONGLONG seen,frame;
    bool ground;
    Body body;
    int turrets;                          // its turret presets ([13] [14], and [15] [16] on the 403 and the Titan)
    Saved engine[3],turret[4],aim[kMostAimSounds];
    int loop[kLoopCount]={-1,-1,-1,-1};   // its voices (jetaudio.h OpenLoop), -1: none
    float last[3],vel[3],nose[3];
    float on,slew,turn;                   // turn: the hull's yaw rate (rad/s), the tracks' as much as its speed
    bool moving;
    int guns;
    Gun gun[kMostGuns];
};
Vehicle vehicles[kVehicles]{};
// A report on its way (the sound's travel time, a case's fall): played when the game clock reaches `due`.
struct Pending { int clip=-1; audio::Heard heard{}; ULONGLONG due=0; };
Pending pending[kPending]{};
bool sigOk=false,stopOk=false,broken=false;
ULONGLONG frameDone=0;

struct Sig { unsigned rva; unsigned char bytes[16]; std::size_t size; };
const Sig kSigs[]={
    {kEngineSound,{0x40,0x53,0x48,0x81,0xEC,0xC0,0x00,0x00,0x00,0xF6,0x81,0xD0,0x1A,0x00,0x00,0x02},16},
    {0x67648E,{0x48,0x8B,0x83,0x38,0x1A,0x00,0x00},7},   // the presets' count: mov rax,[rbx+1A38h]
    {0x6764B4,{0x48,0x8B,0x8B,0x28,0x1A,0x00,0x00},7},   // ...their array: mov rcx,[rbx+1A28h] ([0] idle)
    {0x6764F5,{0x4C,0x8D,0xB3,0x40,0x1A,0x00,0x00},7},   // the idle loop's handle: lea r14,[rbx+1A40h]
    {0x676577,{0x48,0x8D,0xAB,0x50,0x1A,0x00,0x00},7},   // the drive loop's: lea rbp,[rbx+1A50h]
    {0x676665,{0x48,0x8D,0xB3,0x60,0x1A,0x00,0x00},7},   // the turn loop's: lea rsi,[rbx+1A60h]
    {0x5FF1E4,{0x48,0x8B,0x83,0x28,0x1A,0x00,0x00,0x48,0x05,0x00,0x07,0x00,0x00},13},       // a turret's stop: [14]
    {0x5FF1F1,{0x48,0x8B,0x8B,0x28,0x1A,0x00,0x00,0x48,0x81,0xC1,0x80,0x06,0x00,0x00},14},  // ...its move: [13]
    {0x65B605,{0x48,0x8B,0x83,0x28,0x1A,0x00,0x00,0x48,0x05,0x00,0x07,0x00,0x00},13},       // the Grape's the same
    {0x68CFC6,{0x48,0x8D,0x8E,0x80,0x03,0x00,0x00,0xE8},8},     // FireSe into weapon +0x380 (0x7B3EA0)
    {0x697FE4,{0x49,0x8D,0x9F,0x80,0x03,0x00,0x00},7},          // ...played from there at a shot
    {0x698005,{0x49,0x8D,0x8F,0x28,0x0E,0x00,0x00},7},          // ...a looped one on the handle +0xE28
    {0x68CE85,{0x89,0x86,0x70,0x03,0x00,0x00},6},               // FireBurstCount into +0x370
    {0x68D6EC,{0xF3,0x0F,0x11,0x86,0x9C,0x08,0x00,0x00},8},     // AmmoDamage into +0x89C
    {0x68D82F,{0xF3,0x0F,0x11,0x86,0xB0,0x08,0x00,0x00},8},     // AmmoExplosion into +0x8B0
    {0x68DC6D,{0x48,0x89,0xBE,0xE0,0x04,0x00,0x00},7},          // ShellCase: its factory +0x4E0 null (rdi 0)...
    {0x68DCA2,{0x48,0x89,0x86,0xE0,0x04,0x00,0x00},7},          // ...or its class's, when [0] names one
    {0x690540,{0x89,0xAB,0x44,0x15,0x00,0x00},6},               // the shot count sent, into +0x1544
    {0x690505,{0xFF,0x83,0xD0,0x0B,0x00,0x00},6},               // ...the copy's own shots +0xBD0 counted up to it
    {0x690586,{0x84,0xC0,0x0F,0x85,0xB8,0x00,0x00,0x00},8},     // ...not for another machine's operator (no fire, no count)
    {0x6947AD,{0x8B,0x83,0xD0,0x0B,0x00,0x00,0x48,0x8B,0xCB,0x89,0x83,0x44,0x15,0x00,0x00},15},   // a local shot sent from +0xBD0
    {0x68CE36,{0x89,0x86,0x6C,0x03,0x00,0x00},6},               // FireInterval into +0x36C
    {0x6981E0,{0x41,0x8B,0x87,0x6C,0x03,0x00,0x00},7},          // a shot: the wait +0xE0C set to it
    {0x6981F7,{0xF3,0x41,0x0F,0x11,0x87,0x0C,0x0E,0x00,0x00},9},
    {0x7B4510,{0x48,0x83,0xEC,0x48,0x48,0x8B,0x41,0x20,0x48,0x85,0xC0,0x75},12},   // a preset with no bank plays nothing
    {0x63684F,{0x48,0x8D,0x8F,0x38,0x1A,0x00,0x00,0xE8},8},     // a mech's aim table built at +0x1A38 (0x5F3460)
    {0x638183,{0x48,0x8D,0x35,0x1E,0x5D,0x1A,0x01},7},          // ...an object of kAimTableVtable
    {0x5F33FF,{0x48,0x69,0xD8,0xB0,0x00,0x00,0x00,0x48,0x03,0x59,0x10},11},   // its entries 0xB0 apart from +0x10
    {0x643857,{0x48,0x8D,0x96,0x38,0x1A,0x00,0x00},7},          // the seat aims play it
};
const unsigned char kIsPlayingSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x41,0x08,0x48,0x85,0xC0,0x74};
const unsigned char kStopSoundSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x41,0x08,0x48,0x8B};

int Fault(const EXCEPTION_POINTERS* e) noexcept {
    const auto r=e->ExceptionRecord;
    Log("SOUND fault on a vehicle's sound (%08lX at EDF+%llX): the vehicles' sounds off until the game restarts",r->ExceptionCode,
        static_cast<unsigned long long>(static_cast<const unsigned char*>(r->ExceptionAddress)-image));
    broken=true;
    return EXCEPTION_EXECUTE_HANDLER;
}

// --- The stock sounds silenced and given back ---
void Hold(unsigned char* cues,Saved& s) noexcept {
    if(s.held)return;
    std::memcpy(s.cues,cues,kCueBytes);
    std::memset(cues,0,kCueBytes);
    s.held=true;
}
void GiveBack(unsigned char* cues,Saved& s) noexcept {
    if(!s.held)return;
    if(!At<const void*>(cues,0))std::memcpy(cues,s.cues,kCueBytes);   // unless something else gave it a cue meanwhile
    s.held=false;
}
void Keep(unsigned char* cues,Saved& s,bool hold) noexcept {
    if(hold)Hold(cues,s);
    else GiveBack(cues,s);
}
// Preset `k` of the vehicle's car_base_se_table, nullptr past its end.
unsigned char* PresetOf(unsigned char* v,int k) noexcept {
    const auto n=At<std::uint64_t>(v,kPresetCount);
    unsigned char* const base=At<unsigned char*>(v,kPresets);
    if(static_cast<std::uint64_t>(k)>=n || n>64 || !Readable(base,n*kPresetSize))return nullptr;
    return base+static_cast<std::size_t>(k)*kPresetSize;
}
void Presets(unsigned char* v,const int* which,Saved* saved,int count,bool hold) noexcept {
    for(int i=0;i<count;++i)
        if(unsigned char* const p=PresetOf(v,which[i]))Keep(p+kCues,saved[i],hold);
}
// A preset as the game makes them (0x7B3200 / 0x7B3EA0): a pitch and a volume a person could set.
bool LooksLikePreset(const unsigned char* p) noexcept {
    const float pitch=At<float>(p,kPresetPitch),volume=At<float>(p,kPresetVolume);
    return std::isfinite(pitch) && std::isfinite(volume) && pitch>=0.0f && pitch<=4.0f && volume>=0.0f && volume<=8.0f;
}
// A mech's aim table (see kAimTable), its entries' presets held or given back.
void AimTable(unsigned char* v,Saved* saved,bool hold) noexcept {
    if(At<const unsigned char*>(v,kAimTable)!=image+kAimTableVtable)return;
    unsigned char* const base=At<unsigned char*>(v,kAimTableData);
    const auto n=At<std::uint64_t>(v,kAimTableCount);
    if(!n || n>kMostAimSounds || !Readable(base,n*kAimEntry))return;
    for(std::uint64_t i=0;i<n;++i) {
        unsigned char* const p=base+i*kAimEntry;
        if(LooksLikePreset(p))Keep(p+kCues,saved[i],hold);
    }
}
bool StopPlaying(unsigned char* handle) noexcept {
    if(!stopOk)return false;
    if(!reinterpret_cast<IsPlayingFn>(image+kIsPlaying)(handle))return false;
    reinterpret_cast<StopSoundFn>(image+kStopSound)(handle,4);
    return true;
}

// --- Which vehicle, which gun ---
bool Ground(const void* v) noexcept {
    return BodyOf(v)==PluginBody::none && !IsHelicopter(v) && !IsPlayerJet(v) && !IsSub(v);
}
bool Named(const char* name,std::initializer_list<const char*> names) noexcept {
    for(const char* n:names)if(!std::strcmp(name,n))return true;
    return false;
}
// The engine (and turret) of ours a ground vehicle gets: CarBase's engine sound in its vtable, and a class with tracks
// (heavy), wheels (light) or two wheels (bike); a mech: its arms' drives (its aim table) and no engine.
Body BodyOfVehicle(const unsigned char* v) noexcept {
    const char* const name=VehicleClassName(v);
    if(Named(name,{"504_begaruta","612_nix","Begaruta"}))return At<const unsigned char*>(v,kAimTable)==image+kAimTableVtable ? Body::mech : Body::none;
    const auto vt=At<void* const*>(v,0);
    if(!Readable(vt+kEngineSlot,8) || vt[kEngineSlot]!=image+kEngineSound)return Body::none;
    if(Named(name,{"403_Tank","404_Tank","505_Tank","510_Maser","601_Tank","603_Flak"}))return Body::heavy;
    if(Named(name,{"402_Rocket","Car"}))return Body::light;
    if(Named(name,{"503_Bike","511_Bike"}))return Body::bike;
    return Body::none;
}
vmix::Round RoundOf(const RoundModel& m) noexcept {
    if(!m.label)return vmix::Round::other;
    if(m.rtti && !std::strcmp(m.rtti,".?AVFactory@SolidBullet01Rail@@"))return vmix::Round::rail;   // CANNON on the HUD
    if(!std::strcmp(m.label,"CANNON"))return vmix::Round::cannon;
    if(!std::strcmp(m.label,"BEAM"))return vmix::Round::beam;
    if(!std::strcmp(m.label,"GREN"))return vmix::Round::grenade;
    if(!std::strcmp(m.label,"GUN"))return vmix::Round::gun;
    if(!std::strcmp(m.label,"MSL") || !std::strcmp(m.label,"RKT"))return vmix::Round::missile;
    return vmix::Round::other;
}
vmix::Bore BoreOfWeapon(const unsigned char* v,const unsigned char* w) noexcept {
    RoundModel m{};
    if(!ReadRound(w,&m))return vmix::Bore::stock;
    const vmix::GunFacts f{RoundOf(m),m.kind==RoundKind::homing,At<unsigned char>(w,kFirePreset+kPresetLoops)!=0,
                           At<std::int32_t>(w,kFireInterval),At<std::int32_t>(w,kFireBurst),At<float>(w,kFirePreset+kPresetVolume),
                           At<float>(w,kWeaponDamage),At<float>(w,kWeaponBlast),m.speed};
    const vmix::Bore b=vmix::ProfileOf(f);
    // The plugin's aircraft fly missiles whose launch sounds their weapon files were made with (pylib/vcobjects.py).
    return vmix::ProfileFor(b).group==vmix::Group::missile && BodyOf(v)!=PluginBody::none ? vmix::Bore::stock : b;
}
// The wait to `w`'s next shot (frames): its interval after a shot, its magazine's reload once empty (the stock gauge's).
float WaitOf(const unsigned char* w) noexcept {
    const float cool=At<float>(w,kCooldown);
    if(At<std::int32_t>(w,kWeaponAmmo)>0 || cool>0.0f || At<std::int32_t>(w,kReloadTime)<=0)return std::isfinite(cool) && cool>0.0f ? cool : 0.0f;
    const std::int32_t left=At<std::int32_t>(w,kReloadLeft);
    return left>0 ? static_cast<float>(left) : 0.0f;
}
float Volume(float v) noexcept { return std::isfinite(v) && v>0.0f ? (v>4.0f ? 4.0f : v) : 0.0f; }
float GroupVolume(vmix::Group g) noexcept {
    return Volume(g==vmix::Group::main ? Cfg().vehicleGunVolume : g==vmix::Group::missile ? Cfg().vehicleMissileVolume :
                  g==vmix::Group::mg ? Cfg().vehicleMgVolume : 0.0f);
}

// --- Bookkeeping ---
void CloseLoop(int& l) noexcept { audio::CloseLoop(l);l=-1; }
void CloseLoops(Vehicle& s) noexcept {
    for(int& l:s.loop)CloseLoop(l);
    for(int i=0;i<s.guns;++i){CloseLoop(s.gun[i].burst);CloseLoop(s.gun[i].brass);}
}
// Everything given back (the vehicle `v`, seen now: its memory is its own) and its voices closed.
void Release(Vehicle& s,unsigned char* v) noexcept {
    if(s.body==Body::mech)AimTable(v,s.aim,false);
    else if(s.body!=Body::none) {
        Presets(v,kEnginePresets,s.engine,3,false);
        Presets(v,kTurretPresets,s.turret,4,false);
    }
    for(int i=0;i<s.guns;++i) {
        Gun& g=s.gun[i];
        if(g.fire.held && Readable(g.w,kFirePreset+kPresetSize))GiveBack(const_cast<unsigned char*>(g.w)+kFirePreset+kCues,g.fire);
    }
    CloseLoops(s);
}
// A vehicle not seen any more: its voices closed (the object is gone with whatever was held in it).
void Forget(Vehicle& s) noexcept {
    CloseLoops(s);
    s=Vehicle{};
}
Vehicle* EntryFor(unsigned char* v,ULONGLONG ms,bool make) noexcept {
    Vehicle* free=nullptr;
    for(auto& s:vehicles) {
        if(s.ref.Is(v))return &s;
        if(!free && (!s.ref || ms-s.seen>kStaleMs))free=&s;
    }
    if(!free || !make)return nullptr;
    Forget(*free);
    free->ref=ObjRef::Of(v);
    free->ground=Ground(v);
    free->body=free->ground ? BodyOfVehicle(v) : Body::none;
    // The 403's machine gun turret and the Titan's second guns have their own move / stop pair after the main one's.
    free->turrets=Named(VehicleClassName(v),{"403_Tank","404_Tank"}) ? 4 : 2;
    std::memcpy(free->last,v+kPosition,12);
    std::memcpy(free->nose,v+kMatrix+32,12);
    return free;
}

// --- Hearing ---
// `muffle`: more of the air's dulling (a case landing inside the turret).
audio::Heard HeardAt(const SoundPlace& p,float gain,float ratio,float muffle=0.0f) noexcept {
    return audio::Heard{gain*p.left,gain*p.right,ratio*p.doppler,vmix::Muffled(p.distance/kFarAt,muffle)};
}
void Set(int& loop,int clip,bool want,const audio::Heard& h) noexcept {
    if(!want){CloseLoop(loop);return;}
    if(loop<0)loop=audio::OpenLoop(clip);   // none free (or not made yet): again next frame
    audio::SetLoop(loop,h);
}
// `clip` heard from `at` after the sound's travel time (and `after` s more), at `gain` (`muffle`: HeardAt's).
void Later(int clip,const SoundPlace& p,float gain,float after,float muffle=0.0f) noexcept {
    if(gain<0.002f || clip<0)return;
    for(auto& q:pending)
        if(q.clip<0){q=Pending{clip,HeardAt(p,gain,1.0f,muffle),GameMs()+static_cast<ULONGLONG>((p.distance/vmix::kSoundSpeed+after)*1000.0f)};return;}
    // more on their way than kPending: the rest are not heard
}

void EngineStep(unsigned char* v,Vehicle& s,float dt) noexcept {
    const float vol=Volume(Cfg().vehicleEngineVolume);
    const bool engine=s.body==Body::heavy || s.body==Body::light || s.body==Body::bike;
    const bool ours=engine && vol>0.0f;
    if(ours){Presets(v,kEnginePresets,s.engine,3,true);for(const std::size_t h:kEngineHandles)StopPlaying(v+h);}
    else if(engine)Presets(v,kEnginePresets,s.engine,3,false);
    const bool driven=SeatCount(v)>0 && SeatRider(SeatAt(v,0))!=Rider::none && !v[kDead];
    s.on=vec::Approach(s.on,driven ? 1.0f : 0.0f,dt/(driven ? kStartSec : kStopSec));
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float at[3];
    for(int i=0;i<3;++i)at[i]=m[12+i]-m[8+i]*2.0f+m[4+i]*1.0f;   // the engine deck: aft and up of the origin
    SoundPlace p{};
    const bool heard=ours && s.on>0.001f && SoundAt(at,s.vel,&p) && p.distance<kEngineHear;
    const bool heavy=s.body==Body::heavy,bike=s.body==Body::bike;
    const vmix::EngineClass& c=heavy ? vmix::kHeavy : bike ? vmix::kBike : vmix::kLight;
    const float speed=std::sqrt(vec::Dot(s.vel,s.vel)),load=At<float>(v,kEngineLoad);
    const float l=std::isfinite(load) ? load : 0.0f;
    const vmix::EngineMix e=vmix::Engine(vmix::Revs(speed,l,c),l,s.on,c);
    const float g=heard ? vol*kEngineShare*vmix::Falloff(p.distance,kEngineRef,1.0f) : 0.0f;
    const int idle=heavy ? audio::kClipHeavyIdle : bike ? audio::kClipBikeIdle : audio::kClipLightIdle;
    const int loaded=heavy ? audio::kClipHeavyLoad : bike ? audio::kClipBikeLoad : audio::kClipLightLoad;
    Set(s.loop[kLoopIdle],idle,heard,HeardAt(p,g*e.idle,e.ratio));
    Set(s.loop[kLoopLoad],loaded,heard,HeardAt(p,g*e.load,e.ratio));
    // A tank turning on the spot runs its tracks too: each side kTrackHalf m from the middle.
    const vmix::TrackMix t=vmix::Tracks(speed+kTrackHalf*s.turn,vsynth::kTrackSpeed);
    const float tg=heard ? vol*kEngineShare*vmix::Falloff(p.distance,kTracksRef,1.0f)*t.gain : 0.0f;
    Set(s.loop[kLoopTracks],audio::kClipTracks,heard && heavy,HeardAt(p,tg,t.ratio));   // kept open standing: silent, not closed
}

// The fastest of the seats' aim axes against their top rate.
float Slew(unsigned char* v) noexcept {
    float most=0.0f;
    const unsigned seats=SeatCount(v);
    for(unsigned i=0;i<seats && i<8;++i) {
        const unsigned char* const aim=SeatAt(v,i)+kSeatAim;
        const float top=At<float>(aim,kAimParams+8);
        if(!std::isfinite(top) || top<1e-5f)continue;
        for(int a=0;a<2;++a) {
            const float r=std::fabs(At<float>(aim,kAimAxes+static_cast<std::size_t>(a)*kAxisStride+kAxisRate))/top;
            if(std::isfinite(r) && r>most)most=r;
        }
    }
    return most>1.0f ? 1.0f : most;
}
void TurretStep(unsigned char* v,Vehicle& s) noexcept {
    const float vol=Volume(Cfg().vehicleTurretVolume);
    const bool mech=s.body==Body::mech;
    const bool ours=s.body!=Body::none && s.body!=Body::bike && vol>0.0f;
    if(mech)AimTable(v,s.aim,ours);
    else if(s.body!=Body::none && s.body!=Body::bike)Presets(v,kTurretPresets,s.turret,s.turrets,ours);
    const float slew=ours ? Slew(v) : 0.0f;
    s.slew+=(slew-s.slew)*0.35f;   // a frame's jitter of the axis step out
    float at[3];
    std::memcpy(at,v+kPosition,12);
    at[1]+=2.0f;
    SoundPlace p{};
    const bool heard=ours && SoundAt(at,s.vel,&p) && p.distance<kTurretHear;
    const vmix::TurretMix t=vmix::Turret(s.slew);
    const float g=heard ? vol*kTurretShare*vmix::Falloff(p.distance,kTurretRef,1.2f) : 0.0f;
    const float pitch=mech ? kMechServo : 1.0f;
    Set(s.loop[kLoopTurret],audio::kClipTurret,heard,HeardAt(p,g*t.gain,t.ratio*pitch));   // kept open still: no voice made each slew
    if(s.slew>vmix::kTurretMoving)s.moving=true;
    else if(s.moving && s.slew<vmix::kTurretStill) {
        s.moving=false;
        if(heard)audio::PlayOnce(audio::kClipTurretStop,HeardAt(p,g*0.8f,pitch));
    }
}

// --- The guns ---
Gun* GunFor(unsigned char* v,Vehicle& s,const unsigned char* w) noexcept {
    for(int i=0;i<s.guns;++i)if(s.gun[i].w==w)return &s.gun[i];
    if(s.guns>=kMostGuns)return nullptr;
    Gun& g=s.gun[s.guns++];
    g=Gun{};
    g.w=w;g.bore=BoreOfWeapon(v,w);g.stockCase=At<const void*>(w,kCaseFactory)!=nullptr;
    g.ammo=At<std::int32_t>(w,kWeaponAmmo);g.shots=At<std::int32_t>(w,kWeaponShots);g.wait=WaitOf(w);g.total=g.wait;g.since=1e9f;
    return &g;
}
// A main gun's report: its calibre's near and far clips faded across by the distance.
void Report(const vmix::Profile& pf,const float* at,const float* vel,float gain) noexcept {
    SoundPlace p{};
    if(!SoundAt(at,vel,&p))return;
    const vmix::GunMix m=vmix::Gun(p.distance);
    Later(pf.nearClip,p,gain*m.nearGain,0.0f);
    Later(pf.farClip,p,gain*m.farGain,0.0f);
}
void Cue(int clip,const float* at,const float* vel,float gain,float pitch) noexcept {
    SoundPlace p{};
    if(!SoundAt(at,vel,&p) || p.distance>kReloadHear)return;
    audio::PlayOnce(clip,HeardAt(p,gain*vmix::Falloff(p.distance,kReloadRef,1.3f),pitch));
}
// A shot's case landing (its calibre's, kProfiles), `delay` after the shot, unless the weapon throws a physical case of
// its own (ShellCase) that lands with its own sound. A main gun's is the loader's work (VehicleReloadVolume), a small
// gun's its own group's.
void CaseOf(const Gun& g,const vmix::Profile& pf,const float* at,const float* vel) noexcept {
    if(g.stockCase || pf.casing.clip<0 || pf.fire==vmix::Fire::burst)return;
    const float vol=pf.group==vmix::Group::main ? Volume(Cfg().vehicleReloadVolume) : GroupVolume(pf.group);
    SoundPlace p{};
    if(vol<=0.0f || !SoundAt(at,vel,&p) || p.distance>=kBrassHear)return;
    Later(pf.casing.clip,p,vol*vmix::kBrassShare*vmix::Falloff(p.distance,kReloadRef,1.3f),pf.casing.delay,pf.casing.muffle);
}
// A gun's loader: its calibre's steps, each as its time comes in the wait (the fire interval +0xE0C after a shot, the
// magazine's reload +0xE68 once empty: the stock gauge's counters, vhud.cpp).
void Loader(const unsigned char* w,const Gun& g,const vmix::Profile& pf,const float* at,const float* vel,bool shot,float before,
            float sinceBefore) noexcept {
    const float reloadVol=Volume(Cfg().vehicleReloadVolume);
    if(reloadVol<=0.0f || pf.steps<=0)return;
    // The steps run in the gun's cycle: its interval, or the whole wait when longer (the magazine's last round: its own
    // wait is the short one before the reload begins).
    const float cycle=std::fmax(g.total,static_cast<float>(At<std::int32_t>(w,kFireInterval)));
    const unsigned cues=vmix::ReloadCues(pf,before,g.wait,cycle,shot ? -1.0f : sinceBefore,g.since);
    for(int i=0;i<pf.steps;++i)
        if(cues&(1u<<i))Cue(pf.step[i].clip,at,vel,reloadVol*kReloadShare,pf.step[i].pitch);
}
// A rapid gun's frame: its burst loop (the profile's two: made at kMgRate and at kGatlingRate, the nearer one to its
// rate) and the cases raining open while it fires, its tail when it stops.
void Rapid(const unsigned char* w,Gun& g,const vmix::Profile& pf,const float* at,const float* vel,float vol) noexcept {
    const float frames=static_cast<float>(At<std::int32_t>(w,kFireInterval));
    SoundPlace p{};
    const bool placed=SoundAt(at,vel,&p);
    const bool firing=vmix::Firing(g.since,frames) && placed && p.distance<kRapidHear;
    if(!firing) {
        if(g.burst>=0 && placed)Later(audio::kClipBurstTail,p,vol*pf.share*vmix::Falloff(p.distance,pf.ref,1.0f),0.0f);
        CloseLoop(g.burst);CloseLoop(g.brass);
        return;
    }
    const bool gatling=frames<60.0f/(0.5f*(vsynth::kMgRate+vsynth::kGatlingRate));   // the nearer of the two made rates
    const float made=gatling ? vsynth::kGatlingRate : vsynth::kMgRate;
    const float gain=vol*pf.share*vmix::Falloff(p.distance,pf.ref,1.0f);
    Set(g.burst,gatling ? pf.farClip : pf.nearClip,true,HeardAt(p,gain,vmix::BurstRatio(frames,made)));
    const bool close=!g.stockCase && pf.casing.clip>=0 && p.distance<kBrassHear;
    Set(g.brass,pf.casing.clip,close,HeardAt(p,vol*vmix::kBrassShare*vmix::Falloff(p.distance,kReloadRef,1.3f),
                                             vmix::BurstRatio(frames,vsynth::kBrassRate)));
}
void GunStep(unsigned char* v,Vehicle& s,float frames) noexcept {
    if(!WeaponStatusOk())return;
    const unsigned seats=SeatCount(v);
    // The reports this frame, one a calibre (guns firing together, the howitzer's pair: one report).
    constexpr int kBores=static_cast<int>(vmix::Bore::count);
    bool fired[kBores]{};
    float firedAt[kBores][3]{};
    for(unsigned si=0;si<seats && si<8;++si) {
        const unsigned char* const seat=SeatAt(v,si);
        const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
        const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
        if(!n || n>8 || !Readable(holders,n*8))continue;
        for(std::uint64_t i=0;i<n;++i) {
            if(!Readable(holders[i],kHolderWeapon+8))continue;
            unsigned char* const w=At<unsigned char*>(holders[i],kHolderWeapon);
            if(!Readable(w,kWeaponShots+4))continue;
            Gun* const g=GunFor(v,s,w);
            if(!g || g->bore==vmix::Bore::stock)continue;
            const vmix::Profile& pf=vmix::ProfileFor(g->bore);
            const float vol=GroupVolume(pf.group);
            Keep(w+kFirePreset+kCues,g->fire,vol>0.0f);
            if(vol>0.0f && pf.fire==vmix::Fire::burst)StopPlaying(w+kFireLoop);   // a stock loop begun before it was held
            const std::int32_t ammo=At<std::int32_t>(w,kWeaponAmmo),sent=At<std::int32_t>(w,kWeaponShots);
            const float wait=WaitOf(w),before=g->wait,sinceBefore=g->since;
            const bool shot=ammo<g->ammo || (wait>before+2.0f && At<float>(w,kCooldown)>before+2.0f) ||
                            (pf.fire==vmix::Fire::burst && wait>before+0.5f) ||   // a rapid gun's short wait jumps less
                            vmix::RemoteShot(g->shots,sent,At<std::int32_t>(w,kWeaponFired));
            if(wait>before+1.0f)g->total=wait;   // a new wait: a shot's interval, or the magazine's reload begun
            g->since=shot ? 0.0f : g->since+frames;
            g->ammo=ammo;g->shots=sent;g->wait=wait;
            float at[3],dir[3];
            if(!edf::MeanMuzzle(w,64,at,dir))std::memcpy(at,v+kPosition,12);
            if(vol<=0.0f){CloseLoop(g->burst);CloseLoop(g->brass);continue;}
            SoundPlace p{};
            switch(pf.fire) {
            case vmix::Fire::report:
                if(shot){fired[static_cast<int>(g->bore)]=true;std::memcpy(firedAt[static_cast<int>(g->bore)],at,12);}
                break;
            case vmix::Fire::burst:
                Rapid(w,*g,pf,at,s.vel,vol);
                break;
            case vmix::Fire::round:
                if(shot && SoundAt(at,s.vel,&p))Later(pf.nearClip,p,vol*pf.share*vmix::Falloff(p.distance,pf.ref,1.0f),0.0f);
                break;
            default: break;
            }
            if(shot)CaseOf(*g,pf,at,s.vel);
            Loader(w,*g,pf,at,s.vel,shot,before,sinceBefore);
        }
    }
    const float gunVol=GroupVolume(vmix::Group::main);
    for(int b=0;b<kBores;++b)
        if(fired[b])Report(vmix::ProfileFor(static_cast<vmix::Bore>(b)),firedAt[b],s.vel,gunVol*vmix::ProfileFor(static_cast<vmix::Bore>(b)).share);
}

// Once a frame: the sounds whose time has come played, the vehicles gone forgotten.
void Frame(ULONGLONG ms) noexcept {
    for(auto& q:pending)if(q.clip>=0 && ms>=q.due){audio::PlayOnce(q.clip,q.heard);q.clip=-1;}
    for(auto& s:vehicles)if(s.ref && ms-s.seen>kStaleMs)Forget(s);
}

void Step(unsigned char* v) noexcept {
    const ULONGLONG ms=GameMs(),frame=GameFrame();
    if(frameDone!=frame){frameDone=frame;Frame(ms);}
    Vehicle* const s=EntryFor(v,ms,true);
    if(!s)return;
    float frames=1.0f;
    if(s->frame && frame>s->frame) {
        frames=static_cast<float>(frame-s->frame);
        const float k=60.0f/frames;   // the game moves a body 1/60 s of its velocity a frame (jetsound.cpp)
        const float* pos=reinterpret_cast<const float*>(v+kPosition);
        for(int i=0;i<3;++i)s->vel[i]+=((pos[i]-s->last[i])*k-s->vel[i])*0.3f;
        std::memcpy(s->last,pos,12);
        const float* nose=reinterpret_cast<const float*>(v+kMatrix+32);
        const float turned=std::acos(vec::Clamp(vec::Dot(nose,s->nose),-1.0f,1.0f))*k;
        s->turn+=((std::isfinite(turned) ? turned : 0.0f)-s->turn)*0.3f;
        std::memcpy(s->nose,nose,12);
    } else if(s->frame==frame)return;   // seen this frame already
    s->frame=frame;s->seen=ms;
    if(s->ground) {
        EngineStep(v,*s,frames/60.0f);
        TurretStep(v,*s);
    }
    GunStep(v,*s,frames);
}
}  // namespace

bool InstallVehicleSound() noexcept {
    __try {
        bool ok=true;
        for(const auto& g:kSigs)ok=ok && Matches(g.rva,g.bytes,g.size);
        sigOk=ok;
        stopOk=Matches(kIsPlaying,kIsPlayingSig,sizeof(kIsPlayingSig)) && Matches(kStopSound,kStopSoundSig,sizeof(kStopSoundSig));
        Log("HOOK vehicle sound=%d (stock loops stopped=%d)",sigOk,stopOk);
        return sigOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void VehicleSound(unsigned char* v) noexcept {
    if(!sigOk || broken)return;
    __try {
        // Ours only while they can be heard: the sounds made (and the engine up) and the camera's listener placed (jetsound.cpp
        // JetSoundTick, the frame's first step: no listener, no voice of ours would sound). Until then, and whenever
        // switched off, the stock ones: never a stock sound silenced with nothing in its place.
        const bool on=Cfg().enabled && Cfg().vehicleSound && !v[kDead] && audio::ClipsReady() && SoundListening();
        if(!on) {
            if(Vehicle* s=EntryFor(v,GameMs(),false)){Release(*s,v);s->ref=ObjRef{};}
            return;
        }
        Step(v);
    } __except(Fault(GetExceptionInformation())) {}
}

void ResetVehicleSound() noexcept {
    for(auto& s:vehicles)Forget(s);
    for(auto& q:pending)q.clip=-1;
    frameDone=0;
}
}  // namespace crew
