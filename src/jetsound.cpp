// Jet engine sound (ini JetSound / JetSoundVolume, docs/sound-re.md §5, docs/feature-wt-sound.md 「引擎」): every plugin jet
// (the NPC jets, drones and gunships and the rotor-flown carriers, jet.cpp; the player's jet, playerjet.cpp; the other
// players' online) carries War Thunder's EJ200 (sound_engine.h): its layers (idle and full power behind and ahead, the
// afterburner, the turbine's whine near, the far roar) cross-faded and pitched by the spooling rpm, faded over the
// distance and the cone (where the camera is round its nose), placed among the speakers (surround.h), shifted by the
// Doppler effect and heard late by the speed of sound (what is heard left the jet when the sound's travel time equals
// its age: engine::Retarded). The jet the local player sits in plays the player's event, every other one the AI's.
// The game has no jet engine sound and no Doppler (it pans and fades each of its sounds itself from the camera, with
// no velocity anywhere), so the plugin plays the engine through its own voices (jetaudio.cpp) and mixes it here, as
// the game would: from the camera (the sound system's listener 0, SoundSystem +0x58: its position and its matrix's
// inverse), at the game's master and effect volume (GameStatus +0x2E8 / +0x2F0) times JetSoundVolume.
// Doppler ratio (c - v_listener.u) / (c - v_source.u), u the unit line from the jet to the camera, c kSoundSpeed.
// The same listener places the ground vehicles' sounds (SoundAt, vehsound.cpp).
// The stock 506's rotor loop the jets were built on is silenced in their SGOs (tools/make_jets.py); the lock-on beeps
// of NPC seats are silenced here (LockSound).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "objref.h"
#include "edf_layout.h"
#include "config.h"
#include "plugin_log.h"
#include "scene.h"
#include "plugin_body.h"
#include "jet.h"
#include "sound.h"
#include "npcai.h"
#include "support_call.h"
#include "transport.h"
#include "jetaudio.h"
#include "jetsound_state.h"
#include "jet_internal.h"
#include "wing_engine.h"
#include "stores.h"
#include "layout.h"
#include "memory.h"
#include "sound_place.h"
#include "sound_engine.h"
#include "lazy_rows.h"
#include "surround.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kSoundSystem=0x20B2948,kGameStatus=0x20B2890;
constexpr unsigned kPlayPreset=0x7B4510;
constexpr std::size_t kPresetBank=0x20;   // SePreset: its cue (the bank: null plays nothing, 0x7B4510)
// The listeners (0x7AF3E0 rebuilds them every frame from the cameras): a vector at SoundSystem +0x58 (count +0x68)
// of 0x50-byte entries: the camera's position, then the four rows of its world matrix's inverse (a point's camera
// coordinates p.x R1 + p.y R2 + p.z R3 + R4; +x is the camera's left, +z ahead: the game's pan atan2(-x, z), 0x7ACD20).
constexpr std::size_t kListeners=0x58,kListenerCount=0x68,kListenerFloats=soundplace::kListenerFloats;
constexpr std::size_t kMasterVolume=0x2E8,kEffectVolume=0x2F0;
constexpr float kJetSize=15.0f;   // m: a fighter's length, how wide it is heard near (surround.h SpreadAt)
constexpr float kFarAt=2500.0f;                  // the air's dulling at its fullest this far
constexpr float kJetTop=245.0f;                  // m/s: the fastest attack speed (jet.cpp kKinds' interceptor), a
                                                 // jet of no record's (another machine's) top: its speed_relative
constexpr float kListenerJump=60.0f;             // a camera moving this far in a frame cut: no velocity from it
constexpr ULONGLONG kStaleMs=200;                // a jet not seen this long (game ms) is gone: its sound stops
constexpr int kSounds=32;

struct Sound {
    ObjRef ref;
    int voice[engine::kMostEventLayers]{};   // each layer's loop (jetaudio.h OpenWtLoop) + 1, 0: none
    const engine::Event* event=nullptr;      // the event its voices are of (the player's or the AI's)
    engine::Spool spool{};
    engine::Inputs heard{};                  // the parameters last heard (its one-shots cross from them)
    bool hasHeard=false;
    unsigned turn=0;                         // the one-shots' variants in turn
    float last[3]{},vel[3]{},speed=0.0f;
    ULONGLONG lastFrame=0,seen=0,logAt=0;
};
Sound sounds[kSounds]{};
// Each sound's past (sounds[i]'s), 46 KB each: on the heap from the first jet heard, not in the image (1.5 MB of
// .data committed for every player otherwise: tests/plugin_image_size_guard.py).
LazyRows<engine::Trail,kSounds> trails;
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
int OpenVoice(int group) noexcept { inAudio=true;const int l=audio::OpenWtLoop(group);inAudio=false;return l; }
void SetVoice(int loop,const audio::Heard& h) noexcept { inAudio=true;audio::SetLoop(loop,h);inAudio=false; }
void CloseVoice(int loop) noexcept { inAudio=true;audio::CloseLoop(loop);inAudio=false; }
void ShotVoice(int group,int variant,const audio::Heard& h) noexcept { inAudio=true;audio::PlayWt(group,variant,h);inAudio=false; }
bool There(int group) noexcept { inAudio=true;const bool t=audio::WtThere(group);inAudio=false;return t; }
void CloseVoices(Sound& s) noexcept {
    for(int& l:s.voice){if(l)CloseVoice(l-1);l=0;}
}
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
    for(auto& s:sounds){if(voicesOk)CloseVoices(s);s=Sound{};}
}

void Silence(Sound& s) noexcept {
    CloseVoices(s);
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
    trails[static_cast<std::size_t>(free-sounds)].count=0;   // EntryFor runs after trails.Allocate
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

// Where a sound at `pos` moving `vel` (m/s; nullptr: still) is heard from the camera (sound_place.h); false with no
// listener.
bool Place(const float* pos,const float* vel,SoundPlace* out) noexcept {
    if(!hasListener)return false;
    *out=soundplace::Place(listener,listenerVel,pos,vel);
    return true;
}

// Whether the local player sits in `v` (any seat): its own jet, heard as the player's event.
bool Own(unsigned char* v) noexcept {
    const unsigned n=SeatCount(v);
    for(unsigned i=0;i<n && i<8;++i)if(SeatRider(SeatAt(v,i))==Rider::player)return true;
    return false;
}

// `s`'s layers heard as `h` (n of them) from `p` at `gain`: a voice opened for each loud enough, closed once quiet.
void Voices(Sound& s,const engine::Heard* h,int n,const SoundPlace& p,float gain,float doppler) noexcept {
    const float spread=surround::SpreadAt(p.distance,kJetSize,0.0f),cutoff=audio::AirCutoff(p.distance/kFarAt);
    for(int k=0;k<n;++k) {
        int& l=s.voice[k];
        const float g=gain*h[k].gain;
        if(l && g<engine::kCloseGain){CloseVoice(l-1);l=0;continue;}
        if(!l && g>engine::kOpenGain)l=OpenVoice(h[k].clip)+1;   // none free (or not there): again next frame
        if(l)SetVoice(l-1,audio::Heard{g,p.side,p.front,spread,h[k].ratio*doppler,cutoff,false});
    }
}

// `v`'s live NPC flight (jet.cpp): one stepped lately, a stationary carrier's hover too. A retained entry alone
// (Adopt/hand-back, or an old command) is not evidence of an engine running.
const jet::Jet* LiveNpc(const unsigned char* v,ULONGLONG ms) noexcept {
    const jet::Jet* const j=jet::FindJet(v);
    return j && !j->reap && j->lastStep && ms-j->lastStep<=kStaleMs ? j : nullptr;
}

void Step(unsigned char* v) noexcept {
    const ULONGLONG ms=GameMs(),frame=GameFrame();
    jetsound::State state{};
    const bool flown=jetsound::PlayerState(v,&state);
    bool burner=false,record=flown;
    float top=kJetTop;
    if(flown)burner=!state.rotor && state.throttle>0.95f;   // the player's: the throttle's last notch
    else if(const jet::Jet* const j=LiveNpc(v,ms)) {
        const jet::Kind& kind=jet::KindOf(j->role);
        record=true;
        state.commanded=true;state.parked=false;
        state.rotor=kind.flight==jet::FlightModel::rotor;
        state.speed=std::sqrt(Dot(j->m.vel,j->m.vel));
        state.throttle=j->m.power;   // its engine: a rotor's (Hover), a wing's lever (JetSteer, wing_engine.h)
        top=kind.attack>1.0f ? kind.attack : kJetTop;
        // Ours: lit where the thrust its flight commands is saturated (wing_engine.h), the player's lever's last notch.
        burner=!state.rotor && jet::wing::Burner(j->m.power);
    }
    state.occupied=SeatCount(v)>0 && SeatRider(SeatAt(v,0))!=Rider::none;
    Sound* known=nullptr;
    for(auto& old:sounds)if(old.ref.Is(v)){known=&old;break;}
    jetsound::Lever heard{};
    if(!record && jetsound::HeardLever(v,&heard)) {
        // A registered jet another machine runs (online: no flight of ours): the lever that machine flies it at
        // (jet_lever_net.cpp; the same lever as ours, so the same afterburner), its speed what this machine sees of it.
        // None heard: no guess from its speed; an occupied one idles (jetsound::For).
        state.commanded=true;state.parked=heard.parked;state.rotor=heard.rotor;
        state.speed=known ? known->speed : 0.0f;
        state.throttle=heard.lever;
        burner=!heard.rotor && jet::wing::Burner(heard.lever);
    }
    const jetsound::Engine engine=v[kDead] ? jetsound::Engine{} : jetsound::For(state);
    if(engine.gain<=0.0f) {
        // Parked exhibits must not consume the finite aircraft/voice tables.
        if(known)Silence(*known);
        return;
    }
    if(!trails.Allocate("jet engine sound"))return;   // no heap for the trails: the engines stay silent (logged once)
    Sound* const s=known ? known : EntryFor(v,ms);
    if(!s)return;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    float dt=10.0f;   // a jet first heard: its engine already where its power is
    if(s->lastFrame && frame>s->lastFrame) {
        // The game moves a body 1/60 s of its velocity a frame: the frames between, not the clock, give its speed.
        const float k=60.0f/static_cast<float>(frame-s->lastFrame);
        dt=1.0f/k;
        for(int i=0;i<3;++i)s->vel[i]+=((pos[i]-s->last[i])*k-s->vel[i])*0.3f;
        const float speed=std::sqrt(Dot(s->vel,s->vel));
        s->speed=speed;
    } else if(s->lastFrame==frame)return;   // heard this frame already
    std::memcpy(s->last,pos,12);s->lastFrame=frame;
    s->seen=ms;
    const bool own=flown && Own(v);
    const engine::Event& e=own ? engine::kJetPlayer : engine::kJetAi;
    if(s->event!=&e){CloseVoices(*s);s->event=&e;s->hasHeard=false;}   // got in / out: the other event's voices
    engine::Step(s->spool,engine.power,burner,dt);
    engine::Sample x{};
    std::memcpy(x.pos,pos,12);std::memcpy(x.vel,s->vel,12);std::memcpy(x.nose,v+kMatrix+32,12);
    const float kmh=s->speed*3.6f;
    x.in.v[engine::kRpm]=own ? s->spool.rpm : engine::AiRpm(s->spool.rpm);
    x.in.v[engine::kLoad]=s->spool.load;x.in.v[engine::kWep]=s->spool.wep;
    x.in.v[engine::kSpeed]=kmh;x.in.v[engine::kTurn]=0.0f;
    x.in.v[engine::kSpeedShare]=100.0f*s->speed/top;
    x.ms=ms;
    engine::Trail& trail=trails[static_cast<std::size_t>(s-sounds)];
    engine::Push(trail,x);
    const engine::Sample* r=engine::Retarded(trail,listener,ms);
    SoundPlace p{};
    if(!r || !Place(r->pos,r->vel,&p))return;
    const float cone=engine::ConeAngle(r->nose,p.toCamera);
    engine::Heard h[engine::kMostEventLayers];
    const int n=engine::Hear(e,r->in,p.distance,cone,h);
    // JetSoundVolume here, not on the master voice: the cockpit's warnings share that at their own WarnVolume.
    const float gain=Cfg().jetSoundVolume*engine.gain,doppler=engine::Doppler(e,p.doppler,cone);
    Voices(*s,h,n,p,gain,doppler);
    if(s->hasHeard) {
        const unsigned bits=engine::Crossed(e,s->heard,r->in);
        for(int i=0;i<e.shots;++i) {
            if(!(bits&(1u<<i)) || !There(e.s[i].clip))continue;
            const float g=gain*engine::ShotGain(e,i,r->in,p.distance);
            ShotVoice(e.s[i].clip,static_cast<int>(s->turn++%static_cast<unsigned>(wt::kVariants[e.s[i].clip])),
                      audio::Heard{g,p.side,p.front,surround::SpreadAt(p.distance,kJetSize,0.0f),doppler,
                                   audio::AirCutoff(p.distance/kFarAt),false});
            if(Cfg().debug)Log("SOUND jet v=%p %s one-shot %d at %.0f m, gain %.3f",v,own ? "own" : "ai",e.s[i].clip,
                               p.distance,g);
        }
    }
    s->heard=r->in;s->hasHeard=true;
    if((Cfg().verbose || (own && Cfg().debug)) && ms-s->logAt>=2000) {
        s->logAt=ms;
        char g[256];int at=0,open=0;
        for(int k=0;k<n && at<200;++k) {
            open+=s->voice[k] ? 1 : 0;
            at+=std::snprintf(g+at,sizeof(g)-at,"%s%.3f",k ? "," : "",gain*h[k].gain);
        }
        Log("SOUND jet v=%p %s rpm %.0f load %.2f wep %.2f %.0f km/h, %.0f m cone %.0f late %.2f s doppler %.2f; layers [%s] "
            "%d voice(s) of its %d, %d loop(s) open",v,own ? "own" : "ai",r->in.v[engine::kRpm],r->in.v[engine::kLoad],
            r->in.v[engine::kWep],r->in.v[engine::kSpeed],p.distance,cone,static_cast<double>(ms-r->ms)/1000.0,doppler,g,open,n,
            audio::LoopsOpen());
    }
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
bool ListenerState(float* pos,float* vel) noexcept {
    std::memcpy(pos,listener,sizeof(listener));std::memcpy(vel,listenerVel,sizeof(listenerVel));
    return sigOk && !broken && hasListener;
}
bool ListenerAt(float* pos) noexcept {
    if(!SoundListening())return false;
    std::memcpy(pos,listener,3*sizeof(float));
    return true;
}

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

bool jetsound::LocalLever(const unsigned char* v,Lever* out) noexcept {
    if(!v || !out)return false;
    State st{};
    if(PlayerState(v,&st)){*out=Lever{st.throttle,st.rotor,st.parked};return true;}
    const jet::Jet* const j=LiveNpc(v,GameMs());
    if(!j)return false;
    *out=Lever{j->m.power,jet::KindOf(j->role).flight==jet::FlightModel::rotor,false};
    return true;
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
        // The camera is the ground vehicles' and the explosions' listener too (vehsound.cpp, blastsound.cpp SoundAt):
        // kept while any sounds. The jets need it from their first jet on (started); the others from their switch alone,
        // a mission with no jet in it too.
        const bool others=Cfg().vehicleSound || Cfg().explosionVolume>0.0f || Cfg().flybyVolume>0.0f;
        if((jets && started) || (Cfg().enabled && others))Tick();
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
