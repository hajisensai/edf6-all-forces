// The ground vehicles' sounds (the user, 2026-10-06: "add or change the sounds: the engine and the turret turning, the
// shell loading and the case thrown out, and a more powerful main gun"; ini VehicleSound, docs/sound-re.md §9). Every
// ground vehicle the plugin sees (its input, crew.cpp InputHook) is heard through the plugin's own voices (jetaudio.cpp,
// the clips made in vsynth.h, the mix decided in vehmix.h), placed at the camera as the jets' engines are (SoundAt:
// panned to the ears, the Doppler ratio, the air's dulling far off):
//  - the engine (the CarBase classes: the tanks, the Kepler and the howitzer, the Titan, the Maser, the Grape and the
//    trucks, the Naegling): a heavy V12 or a light six, two layers faded across by the stock engine's own load (+0x1A80)
//    and pitched up with the revs (the load and the speed); a tank's tracks clacking with its speed. It runs while
//    someone sits in the driver's seat, winding up when one gets in and down when they get out;
//  - the turret: the traverse drive whining with the fastest of the seats' aim axes against their top rate (their
//    rates, seat aim +0x1C / +0x5C: the stock axis step's), a stop's clunk when it comes to rest;
//  - the loader, on a main gun with a wait of a loader's length (vehmix.h ReloadCues): the case thrown out after the
//    shot, the next round rammed and the breech closed as the wait runs out (the fire interval +0xE0C after a shot, the
//    magazine's reload +0xE68 when it is empty: the stock gauge's own counters, vhud.cpp);
//  - the main gun (vehmix.h MainGun: a shell-firing class, a breech's interval, a one-shot fire sound): a report of its
//    own, punchy near and rumbling far, arriving late by the distance (sound at 340 m/s).
// The stock sounds these replace are silenced on that vehicle (its own copies: the SePresets live in the object):
// the engine's idle / drive / turn presets ([0], [1], [2] of car_base_se_table, the vector at +0x1A20) with their
// loops stopped (handles +0x1A40 / +0x1A50 / +0x1A60, CarBase's engine sound 0x676370), the turret's move / stop presets
// ([13], [14]: 0x632BD0 plays them per axis) and each main gun's FireSe (weapon +0x380, played at 0x697FE4): their cues
// cleared (a preset with no bank plays nothing, 0x7B4510) and kept to be given back the moment a group is off (its
// volume 0, VehicleSound=0, the plugin off) or the sounds cannot be made. A group whose volume is 0 keeps its stock
// sound. All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
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

namespace crew {
namespace {
// CarBase's engine sound, in every class's vtable at kEngineSlot (0x250 / 8) the plugin gives an engine.
constexpr unsigned kEngineSound=0x676370;
constexpr std::size_t kEngineSlot=0x250/8;
constexpr std::size_t kPresets=0x1A28,kPresetCount=0x1A38,kPresetSize=0x80;   // car_base_se_table's SePresets (a vector)
constexpr std::size_t kCues=0x20,kCueBytes=0x50;   // a preset's five {bank, cue} (the ground's materials); bank null: silent
constexpr std::size_t kPresetVolume=0x74,kPresetLoops=0x78;
constexpr std::size_t kEngineLoad=0x1A80;           // the stock engine's load (0..1), eased: its drive loop's volume
constexpr int kEnginePresets[3]={0,1,2},kTurretPresets[2]={13,14};
constexpr std::size_t kEngineHandles[3]={0x1A40,0x1A50,0x1A60};
// The weapon (docs/hud-re.md §7, vhud.cpp): FireSe's preset, FireInterval (frames), the wait to the next shot (frames
// left, a float), ReloadTime and its frames left.
constexpr std::size_t kFirePreset=0x380,kFireInterval=0x36C,kCooldown=0xE0C,kReloadTime=0x20C,kReloadLeft=0xE68;
// The seat's aim: its params {brake, accel, top rate} and each axis' rate (rad a frame), turretcam.h AxisStep.
constexpr std::size_t kAimParams=0x90,kAxisRate=0xC;
constexpr unsigned kIsPlaying=0x7A8A20,kStopSound=0x7A8CD0;
using IsPlayingFn=bool(__fastcall*)(void*);
using StopSoundFn=void(__fastcall*)(void*,int);

using vmix::kEngineRef; using vmix::kTracksRef; using vmix::kTurretRef; using vmix::kReloadRef; using vmix::kTrackHalf;
using vmix::kEngineShare; using vmix::kTurretShare; using vmix::kReloadShare; using vmix::kGunShare;
constexpr float kEngineHear=500.0f,kTurretHear=150.0f,kReloadHear=80.0f;   // m: no voice farther off
constexpr float kFarAt=2500.0f;                     // m: the air's dulling at its fullest (the jets')
constexpr float kStartSec=1.2f,kStopSec=2.0f;       // the engine winding up / down
constexpr ULONGLONG kStaleMs=300;                   // a vehicle not seen this long (game ms) is gone: its voices too
constexpr int kVehicles=48,kMostGuns=8,kPending=16;

enum class Body : std::uint8_t { none, heavy, light };
enum Loop : int { kLoopIdle, kLoopLoad, kLoopTracks, kLoopTurret, kLoopCount };
struct Saved { unsigned char cues[kCueBytes]; bool held; };
struct Gun {
    const unsigned char* w;
    bool main;
    Saved fire;
    std::int32_t ammo;
    float wait,total,since;   // frames: to the next shot / the cycle's full wait / since the shot
};
struct Vehicle {
    ObjRef ref;
    ULONGLONG seen,frame;
    Body body;
    Saved engine[3],turret[2];
    int loop[kLoopCount]={-1,-1,-1,-1};   // its voices (jetaudio.h OpenLoop), -1: none
    float last[3],vel[3],nose[3];
    float on,slew,turn;                   // turn: the hull's yaw rate (rad/s), the tracks' as much as its speed
    bool moving;
    int guns;
    Gun gun[kMostGuns];
};
Vehicle vehicles[kVehicles]{};
// A report on its way (the sound's travel time): played when the game clock reaches `due`.
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
    {0x68CE36,{0x89,0x86,0x6C,0x03,0x00,0x00},6},               // FireInterval into +0x36C
    {0x6981E0,{0x41,0x8B,0x87,0x6C,0x03,0x00,0x00},7},          // a shot: the wait +0xE0C set to it
    {0x6981F7,{0xF3,0x41,0x0F,0x11,0x87,0x0C,0x0E,0x00,0x00},9},
    {0x7B4510,{0x48,0x83,0xEC,0x48,0x48,0x8B,0x41,0x20,0x48,0x85,0xC0,0x75},12},   // a preset with no bank plays nothing
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
// Preset `k` of the vehicle's car_base_se_table, nullptr past its end.
unsigned char* PresetOf(unsigned char* v,int k) noexcept {
    const auto n=At<std::uint64_t>(v,kPresetCount);
    unsigned char* const base=At<unsigned char*>(v,kPresets);
    if(static_cast<std::uint64_t>(k)>=n || n>64 || !Readable(base,n*kPresetSize))return nullptr;
    return base+static_cast<std::size_t>(k)*kPresetSize;
}
void Presets(unsigned char* v,const int* which,Saved* saved,int count,bool hold) noexcept {
    for(int i=0;i<count;++i) {
        unsigned char* const p=PresetOf(v,which[i]);
        if(!p)continue;
        if(hold)Hold(p+kCues,saved[i]);
        else GiveBack(p+kCues,saved[i]);
    }
}
// The stock engine's loops stopped (their presets silent, the game does not start them again).
void StopEngineLoops(unsigned char* v) noexcept {
    if(!stopOk)return;
    const auto playing=reinterpret_cast<IsPlayingFn>(image+kIsPlaying);
    const auto stop=reinterpret_cast<StopSoundFn>(image+kStopSound);
    for(const std::size_t h:kEngineHandles)if(playing(v+h))stop(v+h,4);
}

// --- Which vehicle, which gun ---
bool Ground(const void* v) noexcept {
    return BodyOf(v)==PluginBody::none && !IsHelicopter(v) && !IsPlayerJet(v) && !IsSub(v);
}
// The engine of ours a vehicle gets: CarBase's engine sound in its vtable, and a class with tracks (heavy) or wheels
// (light); the bikes keep theirs.
Body BodyOfVehicle(const unsigned char* v) noexcept {
    const auto vt=At<void* const*>(v,0);
    if(!Readable(vt+kEngineSlot,8) || vt[kEngineSlot]!=image+kEngineSound)return Body::none;
    const char* const name=VehicleClassName(v);
    for(const char* heavy:{"403_Tank","404_Tank","505_Tank","510_Maser","601_Tank","603_Flak"})
        if(!std::strcmp(name,heavy))return Body::heavy;
    for(const char* light:{"402_Rocket","Car"})
        if(!std::strcmp(name,light))return Body::light;
    return Body::none;
}
vmix::Round RoundOf(const char* label) noexcept {
    if(!label)return vmix::Round::other;
    if(!std::strcmp(label,"CANNON"))return vmix::Round::cannon;
    if(!std::strcmp(label,"BEAM"))return vmix::Round::beam;
    if(!std::strcmp(label,"GREN"))return vmix::Round::grenade;
    return vmix::Round::other;
}
bool IsMainGun(const unsigned char* w) noexcept {
    RoundModel m{};
    if(!ReadRound(w,&m))return false;
    return vmix::MainGun(RoundOf(m.label),m.kind==RoundKind::homing,At<std::int32_t>(w,kFireInterval),
                         At<unsigned char>(w,kFirePreset+kPresetLoops)!=0,At<float>(w,kFirePreset+kPresetVolume));
}
// The wait to `w`'s next shot (frames): its interval after a shot, its magazine's reload once empty (the stock gauge's).
float WaitOf(const unsigned char* w) noexcept {
    const float cool=At<float>(w,kCooldown);
    if(At<std::int32_t>(w,kWeaponAmmo)>0 || cool>0.0f || At<std::int32_t>(w,kReloadTime)<=0)return std::isfinite(cool) && cool>0.0f ? cool : 0.0f;
    const std::int32_t left=At<std::int32_t>(w,kReloadLeft);
    return left>0 ? static_cast<float>(left) : 0.0f;
}

// --- Bookkeeping ---
void CloseLoops(Vehicle& s) noexcept {
    for(int& l:s.loop){audio::CloseLoop(l);l=-1;}
}
// Everything given back (the vehicle `v`, seen now: its memory is its own) and its voices closed.
void Release(Vehicle& s,unsigned char* v) noexcept {
    Presets(v,kEnginePresets,s.engine,3,false);
    Presets(v,kTurretPresets,s.turret,2,false);
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
    free->body=BodyOfVehicle(v);
    std::memcpy(free->last,v+kPosition,12);
    std::memcpy(free->nose,v+kMatrix+32,12);
    return free;
}

// --- Hearing ---
audio::Heard HeardAt(const SoundPlace& p,float gain,float ratio) noexcept {
    return audio::Heard{gain*p.left,gain*p.right,ratio*p.doppler,p.distance/kFarAt};
}
void Set(int& loop,int clip,bool want,const audio::Heard& h) noexcept {
    if(!want){audio::CloseLoop(loop);loop=-1;return;}
    if(loop<0)loop=audio::OpenLoop(clip);   // none free (or not made yet): again next frame
    audio::SetLoop(loop,h);
}
float Volume(float v) noexcept { return std::isfinite(v) && v>0.0f ? (v>4.0f ? 4.0f : v) : 0.0f; }

void EngineStep(unsigned char* v,Vehicle& s,float dt) noexcept {
    const float vol=Volume(Cfg().vehicleEngineVolume);
    const bool ours=s.body!=Body::none && vol>0.0f;
    if(ours){Presets(v,kEnginePresets,s.engine,3,true);StopEngineLoops(v);}
    else Presets(v,kEnginePresets,s.engine,3,false);
    const bool driven=SeatCount(v)>0 && SeatRider(SeatAt(v,0))!=Rider::none && !v[kDead];
    s.on=vec::Approach(s.on,driven ? 1.0f : 0.0f,dt/(driven ? kStartSec : kStopSec));
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float at[3];
    for(int i=0;i<3;++i)at[i]=m[12+i]-m[8+i]*2.0f+m[4+i]*1.0f;   // the engine deck: aft and up of the origin
    SoundPlace p{};
    const bool heard=ours && s.on>0.001f && SoundAt(at,s.vel,&p) && p.distance<kEngineHear;
    const bool heavy=s.body==Body::heavy;
    const vmix::EngineClass& c=heavy ? vmix::kHeavy : vmix::kLight;
    const float speed=std::sqrt(vec::Dot(s.vel,s.vel)),load=At<float>(v,kEngineLoad);
    const float l=std::isfinite(load) ? load : 0.0f;
    const vmix::EngineMix e=vmix::Engine(vmix::Revs(speed,l,c),l,s.on,c);
    const float g=heard ? vol*kEngineShare*vmix::Falloff(p.distance,kEngineRef,1.0f) : 0.0f;
    Set(s.loop[kLoopIdle],heavy ? audio::kClipHeavyIdle : audio::kClipLightIdle,heard,HeardAt(p,g*e.idle,e.ratio));
    Set(s.loop[kLoopLoad],heavy ? audio::kClipHeavyLoad : audio::kClipLightLoad,heard,HeardAt(p,g*e.load,e.ratio));
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
    const bool ours=s.body!=Body::none && vol>0.0f;
    Presets(v,kTurretPresets,s.turret,2,ours);
    const float slew=ours ? Slew(v) : 0.0f;
    s.slew+=(slew-s.slew)*0.35f;   // a frame's jitter of the axis step out
    float at[3];
    std::memcpy(at,v+kPosition,12);
    at[1]+=2.0f;
    SoundPlace p{};
    const bool heard=ours && SoundAt(at,s.vel,&p) && p.distance<kTurretHear;
    const vmix::TurretMix t=vmix::Turret(s.slew);
    const float g=heard ? vol*kTurretShare*vmix::Falloff(p.distance,kTurretRef,1.2f) : 0.0f;
    Set(s.loop[kLoopTurret],audio::kClipTurret,heard,HeardAt(p,g*t.gain,t.ratio));   // kept open still: no voice made each slew
    if(s.slew>vmix::kTurretMoving)s.moving=true;
    else if(s.moving && s.slew<vmix::kTurretStill) {
        s.moving=false;
        if(heard)audio::PlayOnce(audio::kClipTurretStop,HeardAt(p,g*0.8f,1.0f));
    }
}

// --- The guns ---
Gun* GunFor(Vehicle& s,const unsigned char* w) noexcept {
    for(int i=0;i<s.guns;++i)if(s.gun[i].w==w)return &s.gun[i];
    if(s.guns>=kMostGuns)return nullptr;
    Gun& g=s.gun[s.guns++];
    g=Gun{};
    g.w=w;g.main=IsMainGun(w);
    g.ammo=At<std::int32_t>(w,kWeaponAmmo);g.wait=WaitOf(w);g.total=g.wait;g.since=1e9f;
    return &g;
}
void Report(const float* at,const float* vel,float gain) noexcept {
    SoundPlace p{};
    if(!SoundAt(at,vel,&p))return;
    const vmix::GunMix m=vmix::Gun(p.distance);
    const ULONGLONG due=GameMs()+static_cast<ULONGLONG>(m.delay*1000.0f);
    const int clips[2]={audio::kClipGunNear,audio::kClipGunFar};
    const float gains[2]={m.nearGain,m.farGain};
    for(int k=0;k<2;++k) {
        if(gains[k]<0.002f)continue;
        Pending* slot=nullptr;
        for(auto& q:pending)if(q.clip<0){slot=&q;break;}
        if(!slot)return;   // more reports on their way than kPending: the rest are not heard
        *slot=Pending{clips[k],HeardAt(p,gain*gains[k],1.0f),due};
    }
}
void Cue(int clip,const float* at,const float* vel,float gain) noexcept {
    SoundPlace p{};
    if(!SoundAt(at,vel,&p) || p.distance>kReloadHear)return;
    audio::PlayOnce(clip,HeardAt(p,gain*vmix::Falloff(p.distance,kReloadRef,1.3f),1.0f));
}
void GunStep(unsigned char* v,Vehicle& s,float frames) noexcept {
    if(!WeaponStatusOk())return;
    const float gunVol=Volume(Cfg().vehicleGunVolume),reloadVol=Volume(Cfg().vehicleReloadVolume);
    const unsigned seats=SeatCount(v);
    bool fired=false;
    float firedAt[3]{};
    for(unsigned si=0;si<seats && si<8;++si) {
        const unsigned char* const seat=SeatAt(v,si);
        const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
        const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
        if(!n || n>8 || !Readable(holders,n*8))continue;
        for(std::uint64_t i=0;i<n;++i) {
            if(!Readable(holders[i],kHolderWeapon+8))continue;
            unsigned char* const w=At<unsigned char*>(holders[i],kHolderWeapon);
            if(!Readable(w,kReloadLeft+4))continue;
            Gun* const g=GunFor(s,w);
            if(!g || !g->main)continue;
            if(gunVol>0.0f)Hold(w+kFirePreset+kCues,g->fire);
            else GiveBack(w+kFirePreset+kCues,g->fire);
            const std::int32_t ammo=At<std::int32_t>(w,kWeaponAmmo);
            const float wait=WaitOf(w),before=g->wait,sinceBefore=g->since;
            const bool shot=ammo<g->ammo || (wait>before+2.0f && At<float>(w,kCooldown)>before+2.0f);
            if(wait>before+1.0f)g->total=wait;   // a new wait: a shot's interval, or the magazine's reload begun
            g->since=shot ? 0.0f : g->since+frames;
            g->ammo=ammo;g->wait=wait;
            float at[3],dir[3];
            if(!edf::MeanMuzzle(w,64,at,dir))std::memcpy(at,v+kPosition,12);
            if(shot && gunVol>0.0f){fired=true;std::memcpy(firedAt,at,12);}
            if(reloadVol<=0.0f)continue;
            // The case goes out after any shot of a gun with a loader's interval (the magazine's last too, whose own
            // wait is the short one before the reload begins).
            const float cycle=std::fmax(g->total,static_cast<float>(At<std::int32_t>(w,kFireInterval)));
            const unsigned cues=vmix::ReloadCues(before,wait,cycle,shot ? -1.0f : sinceBefore,g->since);
            const float rg=reloadVol*kReloadShare;
            if(cues&vmix::kCueEject)Cue(audio::kClipEject,at,s.vel,rg);
            if(cues&vmix::kCueLoad)Cue(audio::kClipLoad,at,s.vel,rg);
            if(cues&vmix::kCueClose)Cue(audio::kClipClose,at,s.vel,rg);
        }
    }
    if(fired)Report(firedAt,s.vel,gunVol*kGunShare);   // guns firing together (the howitzer's pair): one report
}

// Once a frame: the reports whose time has come played, the vehicles gone forgotten.
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
    EngineStep(v,*s,frames/60.0f);
    TurretStep(v,*s);
    GunStep(v,*s,frames);
}
}  // namespace

bool InstallVehicleSound() noexcept {
    __try {
        bool ok=true;
        for(const auto& g:kSigs)ok=ok && Matches(g.rva,g.bytes,g.size);
        sigOk=ok;
        stopOk=Matches(kIsPlaying,kIsPlayingSig,sizeof(kIsPlayingSig)) && Matches(kStopSound,kStopSoundSig,sizeof(kStopSoundSig));
        Log("HOOK vehicle sound=%d (engine loops stopped=%d)",sigOk,stopOk);
        return sigOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void VehicleSound(unsigned char* v) noexcept {
    if(!sigOk || broken)return;
    __try {
        if(!Ground(v))return;
        // Ours only with the sounds made (and the engine up): until then, and whenever switched off, the stock ones.
        const bool on=Cfg().enabled && Cfg().vehicleSound && !v[kDead] && audio::ClipsReady();
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
