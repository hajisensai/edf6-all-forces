// Jet engine sound (ini JetSound / JetSoundVolume, docs/sound-re.md): every plugin jet (the NPC jets, drones and
// gunships, jet.cpp; the player's jet, playerjet.cpp) carries a looping engine sound that follows it, louder and
// higher the faster it flies, its pitch shifted by the Doppler effect between it and the camera.
// The game plays its sounds through CRI ADX2 but with no 3D source: it pans and fades each one itself from the
// listener's position (the camera, SoundSystem +0x58) and has no velocity anywhere, so there is no Doppler in the
// engine. The plugin uses the game's own sound wrapper (docs/sound-re.md §2): a sound preset (SePreset, 0x80 bytes)
// filled by name from SEPRESET.SGO, played at a position into a handle, and per frame the handle's position, volume
// and pitch (1 = as recorded, 0..2 = an octave down..up), which the sound system applies in its own frame update.
// Its Doppler and engine pitch are computed here: pitch = 1 + log2(engine ratio * Doppler ratio), the Doppler ratio
// (c - v_listener.u) / (c - v_source.u) with u the unit line from the jet to the listener and c kSoundSpeed.
// The stock 506's rotor loop the jets were built on is silenced in their SGOs (tools/make_jets.py).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kSeManager=0x20B2950,kSoundSystem=0x20B2948;
constexpr unsigned kPresetByName=0x7B16F0,kPlayPreset=0x7B4510,kSetPosition=0x7A8C20,kSetPitch=0x7A8BF0,kSetVolume=0x7A8C70,
                   kIsPlaying=0x7A8A20,kStop=0x7A8CD0,kRelease=0x7A8730;
// SePreset: +0x14 the distance its volume starts falling at (1/r beyond), +0x18 times that past which a sound is not
// started, +0x20 its cue (the bank: null for a name SEPRESET.SGO does not hold), +0x74 its volume.
constexpr std::size_t kPresetSize=0x80,kPresetRef=0x14,kPresetCull=0x18,kPresetBank=0x20,kPresetVolume=0x74;
// The listeners (0x7AF3E0 rebuilds them every frame from the cameras): a vector at SoundSystem +0x58 (count +0x68)
// of 0x50-byte entries, the camera's position first.
constexpr std::size_t kListeners=0x58,kListenerCount=0x68;
// The engine: the transport's booster loop (the stock jets have no engine sound of their own), heard as a jet's
// out to kEngineRef metres before it fades (1/r), started out to kEngineRef * kEngineCull.
const wchar_t kEnginePreset[]=L"輸送機ブースター稼働";   // vhc_transport_boosterMove, TIKYUUX_SE.ACB (always loaded)
constexpr float kEngineRef=160.0f,kEngineCull=12.0f;
// Volume kIdleVolume..1 and engine pitch ratio kIdlePitch..kFullPitch from standing to kFullSpeed.
constexpr float kFullSpeed=150.0f,kIdleVolume=0.3f,kIdlePitch=0.75f,kFullPitch=1.3f;
constexpr float kSoundSpeed=340.0f;
constexpr float kMaxClosing=0.8f*kSoundSpeed;   // the Doppler ratio's speeds along the line, clamped short of c
constexpr float kListenerJump=60.0f;             // a camera moving this far in a frame cut: no velocity from it
constexpr int kStopFrames=30;                    // a fade-out on stopping
constexpr ULONGLONG kStaleMs=200;                // a jet not seen this long (game ms) is gone: its sound stops
constexpr int kSounds=32;

struct alignas(16) Handle { void* ctrl; void* inst; };
struct Sound {
    ObjRef ref;
    Handle h;
    float last[3],vel[3];
    ULONGLONG lastFrame,seen;
};
Sound sounds[kSounds]{};
alignas(16) unsigned char preset[kPresetSize]{};
bool sigOk=false,presetTried=false,presetOk=false,broken=false;
float listener[3]{},listenerVel[3]{};
bool hasListener=false;
ULONGLONG listenerFrame=0;

using PresetByNameFn=bool(*)(void*,void*,const wchar_t*,bool);
using PlayPresetFn=void(*)(void*,const float*,Handle*);
using SetPositionFn=void(*)(Handle*,const float*);
using SetFloatFn=void(*)(Handle*,float);
using IsPlayingFn=bool(*)(Handle*);
using StopFn=void(*)(Handle*,int);
using ReleaseFn=void(*)(Handle*);

struct Sig { unsigned rva; unsigned char bytes[12]; std::size_t size; };
const Sig kSigs[]={
    {kPresetByName,{0x40,0x53,0x56,0x57,0x48,0x81,0xEC,0xD0,0x00,0x00,0x00,0x48},12},
    {kPlayPreset,{0x48,0x83,0xEC,0x48,0x48,0x8B,0x41,0x20,0x48,0x85,0xC0,0x75},12},   // returns with no bank (+0x20)
    {kSetPosition,{0x4C,0x8B,0x41,0x08,0x4D,0x85,0xC0,0x74,0x20,0x8B,0x02,0x41},12},
    {kSetPitch,{0x48,0x8B,0x41,0x08,0x48,0x85,0xC0,0x74,0x08,0xF3,0x0F,0x11},12},     // inst +0x28
    {kSetVolume,{0x48,0x8B,0x41,0x08,0x48,0x85,0xC0,0x74,0x08,0xF3,0x0F,0x11},12},    // inst +0x2C
    {kIsPlaying,{0x48,0x83,0xEC,0x28,0x48,0x8B,0x41,0x08,0x48,0x85,0xC0,0x74},12},
    {kStop,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x41,0x08,0x48,0x8B},12},
    {kRelease,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8,0xC2,0x03},12},
    {0x7AF4BC,{0x49,0x8B,0x46,0x68},4},   // the listeners' count: mov rax,[r14+68h]
    {0x7AF4E6,{0x49,0x03,0x4E,0x58},4},   // ...and their array: add rcx,[r14+58h]
    {0x705950,{0x48,0x8B,0x0D,0xF1,0xCF,0x9A,0x01},7},   // the frame's update on the SoundSystem: mov rcx,[EDF+0x20B2948]
};

// The lock-on tick's two plays: lea rcx,[rbx+0CC0h] / lea rcx,[rbx+0D40h] (the presets' places in a weapon).
const unsigned char kLockSearchSig[]={0x48,0x8D,0x8B,0xC0,0x0C,0x00,0x00};
const unsigned char kLockDoneSig[]={0x48,0x8D,0x8B,0x40,0x0D,0x00,0x00};

int Fault(const EXCEPTION_POINTERS* e) noexcept {
    const auto r=e->ExceptionRecord;
    Log("SOUND the game faulted on a jet's engine sound (%08lX at EDF+%llX): jet sounds off until the game restarts",r->ExceptionCode,
        static_cast<unsigned long long>(static_cast<const unsigned char*>(r->ExceptionAddress)-image));
    broken=true;
    return EXCEPTION_EXECUTE_HANDLER;
}

// The engine preset, filled once a mission (its cue is in the always loaded bank); false: none (logged once).
bool Preset() noexcept {
    if(presetTried)return presetOk;
    presetTried=true;
    void* const se=At<void*>(image,kSeManager);
    if(!se){presetTried=false;return false;}   // not up yet: asked again next time
    std::memset(preset,0,sizeof(preset));
    reinterpret_cast<PresetByNameFn>(image+kPresetByName)(se,preset,kEnginePreset,false);
    presetOk=At<void*>(preset,kPresetBank)!=nullptr;
    if(presetOk) {
        Put<float>(preset,kPresetRef,kEngineRef);
        Put<float>(preset,kPresetCull,kEngineCull);
    }
    Log("SOUND engine preset %s (base volume %.2f)",presetOk ? "ready" : "missing from SEPRESET.SGO: jets stay silent",
        At<float>(preset,kPresetVolume));
    return presetOk;
}

void Silence(Sound& s,int fade) noexcept {
    if(s.h.inst) {
        reinterpret_cast<StopFn>(image+kStop)(&s.h,fade);
        reinterpret_cast<ReleaseFn>(image+kRelease)(&s.h);
    }
    s=Sound{};
}

// The jet's entry: its own, else a free one (a stale one silenced first); nullptr with every one a live jet's.
Sound* EntryFor(const unsigned char* v,ULONGLONG ms) noexcept {
    Sound* free=nullptr;
    for(auto& s:sounds) {
        if(s.ref.Is(v))return &s;
        if(!free && (!s.ref || ms-s.seen>kStaleMs))free=&s;
    }
    if(!free)return nullptr;
    Silence(*free,kStopFrames);
    free->ref=ObjRef::Of(v);
    return free;
}

float Clamp01(float x) noexcept { return x<0.0f ? 0.0f : x>1.0f ? 1.0f : x; }
float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

// The pitch for a jet at `pos` flying `vel` (m/s): its engine's ratio for its speed times the Doppler ratio.
float Pitch(const float* pos,const float* vel,float share) noexcept {
    float ratio=kIdlePitch+(kFullPitch-kIdlePitch)*share;
    if(hasListener) {
        float u[3]={listener[0]-pos[0],listener[1]-pos[1],listener[2]-pos[2]};
        const float d=std::sqrt(Dot(u,u));
        if(d>1.0f) {
            for(auto& c:u)c/=d;
            float vs=Dot(vel,u),vl=Dot(listenerVel,u);   // the source closing in; the listener going away
            vs=vs>kMaxClosing ? kMaxClosing : vs<-kMaxClosing ? -kMaxClosing : vs;
            vl=vl>kMaxClosing ? kMaxClosing : vl<-kMaxClosing ? -kMaxClosing : vl;
            ratio*=(kSoundSpeed-vl)/(kSoundSpeed-vs);
        }
    }
    const float p=1.0f+std::log2(ratio);
    return p<0.02f ? 0.02f : p>1.98f ? 1.98f : p;
}

void Step(unsigned char* v) noexcept {
    const ULONGLONG ms=GameMs(),frame=GameFrame();
    Sound* const s=EntryFor(v,ms);
    if(!s)return;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    if(s->lastFrame && frame>s->lastFrame) {
        // The game moves a body 1/60 s of its velocity a frame: the frames between, not the clock, give its speed.
        const float k=60.0f/static_cast<float>(frame-s->lastFrame);
        for(int i=0;i<3;++i)s->vel[i]+=((pos[i]-s->last[i])*k-s->vel[i])*0.3f;
    }
    if(frame!=s->lastFrame){std::memcpy(s->last,pos,12);s->lastFrame=frame;}
    s->seen=ms;
    const float share=Clamp01(std::sqrt(Dot(s->vel,s->vel))/kFullSpeed);
    alignas(16) const float at[4]={pos[0],pos[1],pos[2],1.0f};
    if(!s->h.inst || !reinterpret_cast<IsPlayingFn>(image+kIsPlaying)(&s->h)) {
        if(s->h.inst)reinterpret_cast<ReleaseFn>(image+kRelease)(&s->h);
        s->h=Handle{};
        reinterpret_cast<PlayPresetFn>(image+kPlayPreset)(preset,at,&s->h);
        if(!s->h.inst)return;   // not started (beyond its cull distance): tried again next frame
    }
    const float volume=At<float>(preset,kPresetVolume)*Cfg().jetSoundVolume*(kIdleVolume+(1.0f-kIdleVolume)*share);
    reinterpret_cast<SetPositionFn>(image+kSetPosition)(&s->h,at);
    reinterpret_cast<SetFloatFn>(image+kSetVolume)(&s->h,volume);
    reinterpret_cast<SetFloatFn>(image+kSetPitch)(&s->h,Pitch(pos,s->vel,share));
}

// Once a frame: the camera's position and velocity, and every sound whose jet is gone stopped.
void Tick() noexcept {
    const ULONGLONG ms=GameMs(),frame=GameFrame();
    const unsigned char* const ss=At<const unsigned char*>(image,kSoundSystem);
    const float* cam=nullptr;
    if(ss && Readable(ss,kListenerCount+8) && At<std::uint64_t>(ss,kListenerCount)>0) {
        const auto list=At<const float*>(ss,kListeners);
        if(Readable(list,16) && std::isfinite(list[0]+list[1]+list[2]))cam=list;
    }
    if(cam) {
        if(hasListener && frame>listenerFrame) {
            const float k=60.0f/static_cast<float>(frame-listenerFrame);
            const float d[3]={cam[0]-listener[0],cam[1]-listener[1],cam[2]-listener[2]};
            const bool cut=Dot(d,d)>kListenerJump*kListenerJump;
            for(int i=0;i<3;++i)listenerVel[i]=cut ? 0.0f : listenerVel[i]+(d[i]*k-listenerVel[i])*0.3f;
        }
        std::memcpy(listener,cam,12);listenerFrame=frame;hasListener=true;
    } else hasListener=false;
    for(auto& s:sounds)
        if(s.ref && ms-s.seen>kStaleMs)Silence(s,kStopFrames);   // deleted, wrecked or gone with the mission
}
// --- The lock-on beeps ---
// Every weapon loads two presets at init (0x68A920: 0x68E90B 'ロックオンサーチ' into +0xCC0, 0x68E928 'ロックオン完了'
// into +0xD40; weapon_Common_lockonSearch / _lockonLocked, heard at full out to 10 km) and its lock-on tick plays them
// (0x6963A0: 0x69656F / 0x6965D9) unless the weapon's holder (+0x120) runs an AI (+0x1A & 8): a soldier NPC's
// weapon is quiet, but a vehicle has no such flag, so an NPC-driven vehicle's homing missiles beeped in the player's
// ears like their own. The plugin gives every weapon of a seat no local player sits in a preset with no cue (bank
// null at +0x20: 0x7B4510 plays nothing) and a local player's seat its cue back. The cue is the same for every
// weapon (one SEPRESET entry each): the first one seen is kept to give back.
constexpr std::size_t kLockPresets[2]={0xCC0,0xD40};
struct Cue { void* bank; std::uint64_t index; };
Cue lockCue[2]{};
bool lockOk=false;

void LockQuiet(unsigned char* v) noexcept {
    const unsigned count=SeatCount(v);
    for(unsigned s=0;s<count && s<8;++s) {
        const auto seat=SeatAt(v,s);
        const bool quiet=SeatRider(seat)!=Rider::player;
        const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
        const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
        if(!n || n>8 || !Readable(holders,n*8))continue;
        for(std::uint64_t i=0;i<n;++i) {
            if(!Readable(holders[i],kHolderWeapon+8))continue;
            unsigned char* const w=At<unsigned char*>(holders[i],kHolderWeapon);
            if(!Readable(w,kLockPresets[1]+kPresetBank+sizeof(Cue)))continue;
            for(int k=0;k<2;++k) {
                Cue& c=*reinterpret_cast<Cue*>(w+kLockPresets[k]+kPresetBank);
                if(c.bank && !lockCue[k].bank)lockCue[k]=c;   // the shared cue, kept to give back
                if(quiet)c=Cue{};
                else if(!c.bank && lockCue[k].bank)c=lockCue[k];
            }
        }
    }
}
}  // namespace

void LockSound(unsigned char* v) noexcept {
    if(!lockOk || broken)return;
    __try { LockQuiet(v); }
    __except(Fault(GetExceptionInformation())) {}
}

bool InstallJetSound() noexcept {
    __try {
        bool ok=true;
        for(const auto& g:kSigs)ok=ok && Matches(g.rva,g.bytes,g.size);
        sigOk=ok;
        lockOk=Matches(0x69656F,kLockSearchSig,sizeof(kLockSearchSig)) && Matches(0x6965D9,kLockDoneSig,sizeof(kLockDoneSig)) &&
               Matches(kPlayPreset,kSigs[1].bytes,kSigs[1].size);
        Log("HOOK jet sound=%d lock-on beeps (NPC seats quiet)=%d",sigOk,lockOk);
        return sigOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void JetSound(unsigned char* v) noexcept {
    if(!sigOk || broken || !Cfg().jetSound)return;
    const PluginBody body=BodyOf(v);
    if(body!=PluginBody::jet && body!=PluginBody::playerJet)return;
    __try {
        if(v[kDead] || !Preset())return;   // a wreck's sound stops with it going stale
        Step(v);
    } __except(Fault(GetExceptionInformation())) {}
}

void JetSoundTick() noexcept {
    static ULONGLONG tickFrame=0;
    if(!sigOk || broken || tickFrame==GameFrame())return;
    tickFrame=GameFrame();
    __try {
        if(!Cfg().enabled || !Cfg().jetSound){for(auto& s:sounds)if(s.ref)Silence(s,kStopFrames);return;}   // switched off
        Tick();
    } __except(Fault(GetExceptionInformation())) {}
}

// A new mission (mission.cpp MissionStart): the last mission's sounds are stopped and let go of (each handle holds a
// reference on its sound: dropped here, not leaked), and the preset filled again for the new mission.
void ResetJetSound() noexcept {
    if(sigOk && !broken) {
        __try { for(auto& s:sounds)if(s.h.inst)Silence(s,0); }
        __except(Fault(GetExceptionInformation())) {}
    }
    for(auto& s:sounds)s=Sound{};
    presetTried=presetOk=false;
    hasListener=false;listenerFrame=0;
    listenerVel[0]=listenerVel[1]=listenerVel[2]=0.0f;
}
}  // namespace crew
