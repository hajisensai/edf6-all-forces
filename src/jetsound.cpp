// Jet engine sound (ini JetSound / JetSoundVolume, docs/sound-re.md): every plugin jet (the NPC jets, drones and
// gunships, jet.cpp; the player's jet, playerjet.cpp) carries an engine that follows it: a roar heard mostly behind
// and beside it and a fan's whine heard mostly ahead, both louder and higher the faster it flies, their pitch shifted
// by the Doppler effect between it and the camera, panned to the camera's ears, quieter and duller far off.
// The game has no jet engine sound and no Doppler (it pans and fades each of its sounds itself from the camera, with
// no velocity anywhere), so the plugin plays the engine through its own voices (jetaudio.cpp) and mixes it here, as
// the game would: from the camera (the sound system's listener 0, SoundSystem +0x58: its position and its matrix's
// inverse), at the game's master and effect volume (GameStatus +0x2E8 / +0x2F0) times JetSoundVolume.
// Doppler ratio (c - v_listener.u) / (c - v_source.u), u the unit line from the jet to the camera, c kSoundSpeed.
// The same listener places the ground vehicles' sounds (SoundAt, vehsound.cpp).
// The stock 506's rotor loop the jets were built on is silenced in their SGOs (tools/make_jets.py); the lock-on beeps
// of NPC seats are silenced here (LockSound).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "jetaudio.h"
#include "jetsound_state.h"
#include "jet_internal.h"
#include "stores.h"
#include "layout.h"
#include "memory.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kSoundSystem=0x20B2948,kGameStatus=0x20B2890;
constexpr unsigned kPlayPreset=0x7B4510;
constexpr std::size_t kPresetBank=0x20;   // SePreset: its cue (the bank: null plays nothing, 0x7B4510)
// The listeners (0x7AF3E0 rebuilds them every frame from the cameras): a vector at SoundSystem +0x58 (count +0x68)
// of 0x50-byte entries: the camera's position, then the four rows of its world matrix's inverse (a point's camera
// coordinates p.x R1 + p.y R2 + p.z R3 + R4; +x is the camera's left, +z ahead: the game's pan atan2(-x, z), 0x7ACD20).
constexpr std::size_t kListeners=0x58,kListenerCount=0x68,kListenerFloats=20;
constexpr std::size_t kMasterVolume=0x2E8,kEffectVolume=0x2F0;
constexpr float kRoarRef=180.0f,kWhineRef=90.0f; // full volume within these; 1/r beyond (the whine's highs fall faster)
constexpr float kFarAt=2500.0f;                  // the air's dulling at its fullest this far
constexpr float kSpread=0.75f;                   // how far a sound to one side leaves the other ear
constexpr float kSoundSpeed=340.0f;
constexpr float kMaxClosing=0.8f*kSoundSpeed;   // the Doppler ratio's speeds along the line, clamped short of c
constexpr float kListenerJump=60.0f;             // a camera moving this far in a frame cut: no velocity from it
constexpr ULONGLONG kStaleMs=200;                // a jet not seen this long (game ms) is gone: its sound stops
constexpr int kSounds=32;

struct Sound {
    ObjRef ref;
    int slot=-1;                 // its voices (jetaudio.h), -1: none
    float last[3]{},vel[3]{};
    ULONGLONG lastFrame=0,seen=0;
};
Sound sounds[kSounds]{};
bool sigOk=false,broken=false,started=false;
float listener[kListenerFloats]{},listenerVel[3]{};
bool hasListener=false;
ULONGLONG listenerFrame=0;

struct Sig { unsigned rva; unsigned char bytes[12]; std::size_t size; };
const Sig kSigs[]={
    {0x7AF4BC,{0x49,0x8B,0x46,0x68},4},   // the listeners' count: mov rax,[r14+68h]
    {0x7AF4E6,{0x49,0x03,0x4E,0x58},4},   // ...and their array: add rcx,[r14+58h]
    {0x705950,{0x48,0x8B,0x0D,0xF1,0xCF,0x9A,0x01},7},   // the frame's update on the SoundSystem: mov rcx,[EDF+0x20B2948]
};
const unsigned char kPlayPresetSig[]={0x48,0x83,0xEC,0x48,0x48,0x8B,0x41,0x20,0x48,0x85,0xC0,0x75};   // returns with no bank
// The lock-on tick's two plays: lea rcx,[rbx+0CC0h] / lea rcx,[rbx+0D40h] (the presets' places in a weapon).
const unsigned char kLockSearchSig[]={0x48,0x8D,0x8B,0xC0,0x0C,0x00,0x00};
const unsigned char kLockDoneSig[]={0x48,0x8D,0x8B,0x40,0x0D,0x00,0x00};

// Whether the game thread is inside a call on the jets' voices (jetaudio.cpp): a fault there may be the voices'
// own (a voice gone bad, the engine under them), and then nothing of theirs may be called again (Recover).
bool inAudio=false;
int OpenVoices() noexcept { inAudio=true;const int slot=audio::Open();inAudio=false;return slot; }
void SetVoices(int slot,const audio::Mix& m) noexcept { inAudio=true;audio::Set(slot,m);inAudio=false; }
void CloseVoices(int slot) noexcept { inAudio=true;audio::Close(slot);inAudio=false; }
void BeatVoices(float volume) noexcept { inAudio=true;audio::Beat(volume);inAudio=false; }

// The filter of every guarded call here: it runs before the stack unwinds, still inside the faulting call, so it
// only logs and decides; the voices are stopped by the handler (Recover).
int Fault(const EXCEPTION_POINTERS* e) noexcept {
    const auto r=e->ExceptionRecord;
    Log("SOUND fault on a jet's engine sound (%08lX at EDF+%llX, %s): jet sounds off until the game restarts",r->ExceptionCode,
        static_cast<unsigned long long>(static_cast<const unsigned char*>(r->ExceptionAddress)-image),
        inAudio ? "in the voices: they are let go of, unstopped" : "in the game's memory");
    broken=true;
    return EXCEPTION_EXECUTE_HANDLER;
}

// The handler's half (the stack unwound): every jet's sound forgotten. Other audio consumers keep beating the shared
// engine, so the watchdog never silences these loops: a fault in the game's memory leaves the voices sound and they
// are closed here. A fault inside a voice call leaves no voice trustworthy (a bad pointer in the slots, or the engine
// itself): calling DestroyVoice on one could fault again, outside any guard, so they are let go of untouched (their
// slots stay taken in jetaudio; with `broken` no jet opens one again).
void Recover() noexcept {
    const bool voicesOk=!inAudio;
    inAudio=false;
    for(auto& s:sounds){if(voicesOk)CloseVoices(s.slot);s=Sound{};}
}

void Silence(Sound& s) noexcept {
    CloseVoices(s.slot);
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
    Silence(*free);
    free->ref=ObjRef::Of(v);
    return free;
}

float Clamp01(float x) noexcept { return x<0.0f ? 0.0f : x>1.0f ? 1.0f : x; }
float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

// The game's volume for its effects: master times effects (each 0..1).
float GameVolume() noexcept {
    const unsigned char* const gs=At<const unsigned char*>(image,kGameStatus);
    if(!gs || !Readable(gs+kMasterVolume,12))return 1.0f;
    const float m=At<float>(gs,kMasterVolume),e=At<float>(gs,kEffectVolume);
    return std::isfinite(m) && std::isfinite(e) ? Clamp01(m)*Clamp01(e) : 1.0f;
}

// Where a sound at `pos` moving `vel` (m/s) is heard from the camera (see SoundPlace); false with no listener.
bool Place(const float* pos,const float* vel,SoundPlace* out) noexcept {
    if(!hasListener)return false;
    const float* r=listener+4;   // the inverse's rows
    float local[3];
    for(int c=0;c<3;++c)local[c]=pos[0]*r[c]+pos[1]*r[4+c]+pos[2]*r[8+c]+r[12+c];
    const float d=std::sqrt(Dot(local,local));
    const float right=d>1.0f ? -local[0]/d : 0.0f;
    SoundPlace p{};
    p.left=std::sqrt(0.5f*(1.0f-kSpread*right));p.right=std::sqrt(0.5f*(1.0f+kSpread*right));
    p.distance=d;p.doppler=1.0f;
    // Toward the camera from the sound: the Doppler ratio along it.
    float u[3]={listener[0]-pos[0],listener[1]-pos[1],listener[2]-pos[2]};
    const float n=std::sqrt(Dot(u,u));
    if(n>1.0f) {
        for(auto& c:u)c/=n;
        float vs=Dot(vel,u),vl=Dot(listenerVel,u);   // the source closing in; the listener going away
        vs=vs>kMaxClosing ? kMaxClosing : vs<-kMaxClosing ? -kMaxClosing : vs;
        vl=vl>kMaxClosing ? kMaxClosing : vl<-kMaxClosing ? -kMaxClosing : vl;
        p.doppler=(kSoundSpeed-vl)/(kSoundSpeed-vs);
        std::memcpy(p.toCamera,u,12);
    }
    *out=p;
    return true;
}

// What jet `v` at `pos`, flying `vel` with its current engine load, sounds like at the camera.
audio::Mix MixFor(const unsigned char* v,const float* pos,const float* vel,const jetsound::Engine& engine) noexcept {
    audio::Mix m{};
    SoundPlace p{};
    if(!Place(pos,vel,&p))return m;
    const float d=p.distance,leftGain=p.left,rightGain=p.right,doppler=p.doppler;
    // The jet's nose against the line to the camera: the whine carries ahead of it, the roar behind.
    const float* mat=reinterpret_cast<const float*>(v+kMatrix);
    const float ahead=mat[8]*p.toCamera[0]+mat[9]*p.toCamera[1]+mat[10]*p.toCamera[2];
    const float roarAt=d>kRoarRef ? kRoarRef/d : 1.0f,whineAt=d>kWhineRef ? std::pow(kWhineRef/d,1.3f) : 1.0f;
    // JetSoundVolume here, not on the master voice: the cockpit's warnings share that at their own WarnVolume.
    const float own=Cfg().jetSoundVolume*engine.gain,share=engine.power;
    const float roar=own*(0.45f+0.55f*share)*roarAt*(0.6f+0.4f*(ahead<0.0f ? -ahead : 0.0f));
    const float whine=own*(0.2f+0.8f*share)*whineAt*(0.35f+0.65f*(ahead>0.0f ? ahead : 0.0f));
    m.roarL=roar*leftGain;m.roarR=roar*rightGain;
    m.whineL=whine*leftGain;m.whineR=whine*rightGain;
    m.roarRatio=(0.85f+0.3f*share)*doppler;
    m.whineRatio=(0.75f+0.55f*share)*doppler;
    m.distance=d/kFarAt;
    return m;
}

void Step(unsigned char* v) noexcept {
    const ULONGLONG ms=GameMs(),frame=GameFrame();
    jetsound::State state{};
    if(!jetsound::PlayerState(v,&state)) {
        // A live NPC flight includes a stationary carrier's hover. A retained entry
        // alone (Adopt/hand-back, or an old command) is not evidence of an engine running.
        const jet::Jet* const j=jet::FindJet(v);
        if(j && !j->reap && j->lastStep && ms-j->lastStep<=kStaleMs) {
            state.commanded=true;state.parked=false;
            state.rotor=jet::KindOf(j->role).flight==jet::FlightModel::rotor;
            state.speed=std::sqrt(Dot(j->m.vel,j->m.vel));
            if(state.rotor)state.throttle=j->m.power;   // its engine (Hover): hovering, climbing, braking
        }
    }
    state.occupied=SeatCount(v)>0 && SeatRider(SeatAt(v,0))!=Rider::none;
    const jetsound::Engine engine=v[kDead] ? jetsound::Engine{} : jetsound::For(state);
    if(engine.gain<=0.0f) {
        // Parked exhibits must not consume the finite aircraft/voice tables.
        for(auto& old:sounds)if(old.ref.Is(v)){Silence(old);break;}
        return;
    }
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
    if(s->slot<0)s->slot=OpenVoices();   // a new jet, or all voices were in use: (again) now
    SetVoices(s->slot,MixFor(v,pos,s->vel,engine));
}

// Once a frame: the camera's place, matrix and velocity, the beat (at the game's volume), and every sound whose jet
// is gone stopped.
void Tick() noexcept {
    const ULONGLONG ms=GameMs(),frame=GameFrame();
    const unsigned char* const ss=At<const unsigned char*>(image,kSoundSystem);
    const float* cam=nullptr;
    if(ss && Readable(ss,kListenerCount+8) && At<std::uint64_t>(ss,kListenerCount)>0) {
        const auto list=At<const float*>(ss,kListeners);
        if(Readable(list,kListenerFloats*4) && std::isfinite(list[0]+list[1]+list[2]))cam=list;
    }
    if(cam) {
        if(hasListener && frame>listenerFrame) {
            const float k=60.0f/static_cast<float>(frame-listenerFrame);
            const float d[3]={cam[0]-listener[0],cam[1]-listener[1],cam[2]-listener[2]};
            const bool cut=Dot(d,d)>kListenerJump*kListenerJump;
            for(int i=0;i<3;++i)listenerVel[i]=cut ? 0.0f : listenerVel[i]+(d[i]*k-listenerVel[i])*0.3f;
        }
        std::memcpy(listener,cam,sizeof(listener));listenerFrame=frame;hasListener=true;
    } else hasListener=false;
    BeatVoices(GameVolume());
    for(auto& s:sounds)
        if(s.ref && ms-s.seen>kStaleMs)Silence(s);   // deleted, wrecked or gone with the mission
}

}  // namespace

float GameEffectVolume() noexcept {
    __try { return GameVolume(); }
    __except(EXCEPTION_EXECUTE_HANDLER){return 1.0f;}
}

bool SoundAt(const float* pos,const float* vel,SoundPlace* out) noexcept { return sigOk && !broken && Place(pos,vel,out); }
bool SoundListening() noexcept { return sigOk && !broken && hasListener; }

namespace {
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
            const bool own=quiet || IsStoreWeapon(w);   // a store's lock: the cockpit's own tone (audio::LockTone)
            for(int k=0;k<2;++k) {
                Cue& c=*reinterpret_cast<Cue*>(w+kLockPresets[k]+kPresetBank);
                if(c.bank && !lockCue[k].bank)lockCue[k]=c;   // the shared cue, kept to give back
                if(own)c=Cue{};
                else if(!c.bank && lockCue[k].bank)c=lockCue[k];
            }
        }
    }
}
}  // namespace

void LockSound(unsigned char* v) noexcept {
    if(!lockOk || broken)return;
    __try { LockQuiet(v); }
    __except(Fault(GetExceptionInformation())){Recover();}
}

bool InstallJetSound() noexcept {
    __try {
        bool ok=true;
        for(const auto& g:kSigs)ok=ok && Matches(g.rva,g.bytes,g.size);
        sigOk=ok;
        lockOk=Matches(0x69656F,kLockSearchSig,sizeof(kLockSearchSig)) && Matches(0x6965D9,kLockDoneSig,sizeof(kLockDoneSig)) &&
               Matches(kPlayPreset,kPlayPresetSig,sizeof(kPlayPresetSig));
        Log("HOOK jet sound=%d lock-on beeps (NPC seats quiet)=%d",sigOk,lockOk);
        return sigOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void JetSound(unsigned char* v) noexcept {
    if(!sigOk || broken || !Cfg().jetSound)return;
    const PluginBody body=BodyOf(v);
    if(body!=PluginBody::jet && body!=PluginBody::playerJet)return;
    if(!started){started=true;audio::Start();}   // the first jet: the engine (it logs why when it cannot)
    __try {
        Step(v);
    } __except(Fault(GetExceptionInformation())){Recover();}
}

void JetSoundTick() noexcept {
    static ULONGLONG tickFrame=0;
    if(!sigOk || broken || tickFrame==GameFrame())return;
    tickFrame=GameFrame();
    __try {
        const bool jets=Cfg().enabled && Cfg().jetSound;
        if(!jets)for(auto& s:sounds)if(s.ref)Silence(s);   // switched off
        // The camera is the ground vehicles' listener too (vehsound.cpp SoundAt): kept while either sounds. The jets need
        // it from their first jet on (started); the vehicles from the switch alone, a mission with no jet in it too.
        if((jets && started) || (Cfg().enabled && Cfg().vehicleSound))Tick();
        else hasListener=false;   // not kept: never a stale camera's place for a consumer switched on later
    } __except(Fault(GetExceptionInformation())){Recover();}
}

// A new mission (mission.cpp MissionStart): the last mission's sounds stopped and their voices let go of.
void ResetJetSound() noexcept {
    for(auto& s:sounds)Silence(s);
    hasListener=false;listenerFrame=0;
    listenerVel[0]=listenerVel[1]=listenerVel[2]=0.0f;
}
}  // namespace crew
