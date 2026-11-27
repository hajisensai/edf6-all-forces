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
#include <new>
#include "surround.h"
#include "jet_mix.h"
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
constexpr ULONGLONG kStaleMs=200;                // a jet not seen this long (game ms) is gone: heard out, faded (Mix)
// The jets heard at once: every one there is (an entry made for each as it comes, below), up to the voice pool
// (audio::kLoopPool: each one heard takes a voice at least). 32 before 2026-11-27 (the user: 「全发声不行吗，为什么非得
// 32 个」): a table of the plugin's own, no limit of the machine's (docs/feature-wt-sound.md 「声道的上限」).
constexpr int kMostJets=audio::kLoopPool;

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
    // The mix (Mix, jet_mix.h): what it is heard at, and whether at all.
    float level=0.0f;                        // JetSoundVolume times its engine's gain (jetsound::For), set by Step
    bool own=false;                          // the local player's (the player's event)
    bool gone=false,dead=false;              // no longer stepped (deleted, stale, its engine off; a wreck): its trail
                                             // played on until its last sample is heard (caught), then faded out
    bool caught=false;
    bool mixed=false,ranked=false;           // through Mix once; among the loudest heard (jetmix::Rank)
    float presence=0.0f;                     // jetmix::Step: 0 unheard .. 1 at its level
    float loud=0.0f;                         // its layers' sum as heard now, before its presence (the ranking's)
    float distance=0.0f;                     // m from the camera, as heard (Mix): the farthest not heard gives way
};
// What one jet is heard as now: from its trail, the sample whose sound reaches the camera now (engine::Retarded).
struct Hearing {
    const engine::Sample* r=nullptr;
    SoundPlace p{};
    float cone=0.0f,farGain=1.0f,doppler=1.0f;
    int n=0;
    engine::Heard h[engine::kMostEventLayers];
};
// One jet's entry: its sound, what it is heard as and its past (the trail, 46 KB of the entry's 48).
struct Entry { Sound s; Hearing h; engine::Trail trail; };
// One entry a jet, made on the heap as the jets come (none for a mission with no jet; not in the image: 1.5 MB of
// .data committed for every player otherwise, tests/plugin_image_size_guard.py) and never freed: a jet gone leaves
// its entry to the next one, a pointer to one stays good.
Entry* rows[kMostJets]{};
int rowCount=0;
bool noHeap=false;
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
int VoicesFree() noexcept { inAudio=true;const int n=audio::LoopsFree();inAudio=false;return n; }
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
    for(int i=0;i<rowCount;++i){Sound& s=rows[i]->s;if(voicesOk)CloseVoices(s);s=Sound{};}
}

void Silence(Sound& s) noexcept {
    CloseVoices(s);
    s=Sound{};
}

// A new entry on the heap (out of line: Step's guarded caller carries no allocation), or nullptr (logged once).
__declspec(noinline) Entry* NewEntry() noexcept {
    Entry* const e=new(std::nothrow) Entry{};
    if(!e && !noHeap){noHeap=true;Log("SOUND jet engine sound: no heap for jet %d's entry: it and any after it stay silent",rowCount+1);}
    return e;
}

// The jet's entry (`pos`: where it is now; its index in rows, -1 none): a free one, else a new one; with kMostJets
// made (as many as the voices), that of the farthest not heard (ranked out, faded to nothing: taking it is silent) if
// `pos` is nearer by the ranking's margin (no two jets out of the ranking taking each other's entry frame by frame,
// each one's trail started over every time).
int EntryFor(const unsigned char* v,const float* pos) noexcept {
    int at=-1;
    for(int i=0;i<rowCount;++i)if(!rows[i]->s.ref){at=i;break;}
    if(at<0 && rowCount<kMostJets)if(Entry* const e=NewEntry()){rows[rowCount]=e;at=rowCount++;}
    if(at<0 && hasListener) {
        float d2=0.0f;
        for(int i=0;i<3;++i)d2+=(pos[i]-listener[i])*(pos[i]-listener[i]);
        const float d=std::sqrt(d2)*jetmix::kKeepMargin;
        for(int i=0;i<rowCount;++i) {
            const Sound& o=rows[i]->s;
            if(o.mixed && !o.ranked && !(o.presence>0.0f) && o.distance>d && (at<0 || o.distance>rows[at]->s.distance))at=i;
        }
    }
    if(at<0)return -1;
    Sound& s=rows[at]->s;
    Silence(s);
    s.ref=ObjRef::Of(v);
    rows[at]->trail.count=0;
    return at;
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

// `s`'s layers heard as `h` (n of them) from `p` at `gain`: a voice opened for each loud enough, closed once quiet (a
// gain of 0 closes them all).
void Voices(Sound& s,const engine::Heard* h,int n,const SoundPlace& p,float gain,float doppler) noexcept {
    const float spread=surround::SpreadAt(p.distance,kJetSize,0.0f),cutoff=audio::AirCutoff(p.distance/kFarAt);
    for(int k=0;k<engine::kMostEventLayers;++k) {
        int& l=s.voice[k];
        const float g=k<n ? gain*h[k].gain : 0.0f;
        if(l && !(g>=engine::kCloseGain)){CloseVoice(l-1);l=0;continue;}
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

// A jet no longer heard from here (its engine off, a wreck): its trail plays on (Mix) until caught up, then fades.
void Stop(Sound& s,bool dead) noexcept {
    if(!s.gone && Cfg().debug)Log("SOUND jet %s: %s, heard on for its sound's way here, then faded",s.own ? "own" : "ai",
                                  dead ? "wrecked" : "engine off");
    s.gone=true;s.dead=s.dead || dead;
}

jetmix::Params MixParams() noexcept {
    jetmix::Params m{};
    m.fadeIn=m.fadeOut=Cfg().jetSoundFade;
    m.reach=Cfg().jetSoundFar;
    const int most=Cfg().jetSoundMostHeard;
    m.most=most<1 ? 0 : most>kMostJets ? kMostJets : most;   // 0: every jet
    return m;
}

// The jet's frame: its engine, its velocity and its sample pushed on its trail. What is heard of it is Mix's.
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
    int at=-1;
    for(int i=0;i<rowCount;++i)if(rows[i]->s.ref.Is(v)){at=i;break;}
    Sound* const known=at>=0 ? &rows[at]->s : nullptr;
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
        // Off or wrecked: heard out and faded (Mix), then its entry free (parked exhibits must not keep one).
        if(known)Stop(*known,v[kDead]!=0);
        return;
    }
    if(at<0)at=EntryFor(v,reinterpret_cast<const float*>(v+kPosition));
    if(at<0)return;
    Sound* const s=&rows[at]->s;
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
    if(s->gone && Cfg().debug)Log("SOUND jet v=%p heard from again",v);
    s->gone=s->dead=s->caught=false;
    s->own=flown && Own(v);
    const engine::Event& e=s->own ? engine::kJetPlayer : engine::kJetAi;
    if(s->event!=&e){CloseVoices(*s);s->event=&e;s->hasHeard=false;}   // got in / out: the other event's voices
    // JetSoundVolume here, not on the master voice: the cockpit's warnings share that at their own WarnVolume.
    s->level=Cfg().jetSoundVolume*engine.gain;
    engine::Step(s->spool,engine.power,burner,dt);
    engine::Sample x{};
    std::memcpy(x.pos,pos,12);std::memcpy(x.vel,s->vel,12);std::memcpy(x.nose,v+kMatrix+32,12);
    const float kmh=s->speed*3.6f;
    x.in.v[engine::kRpm]=s->own ? s->spool.rpm : engine::AiRpm(s->spool.rpm);
    x.in.v[engine::kLoad]=s->spool.load;x.in.v[engine::kWep]=s->spool.wep;
    x.in.v[engine::kSpeed]=kmh;x.in.v[engine::kTurn]=0.0f;
    x.in.v[engine::kSpeedShare]=100.0f*s->speed/top;
    x.ms=ms;
    engine::Push(rows[at]->trail,x);
}

ULONGLONG mixFrame=0;
// Mix's scratch, a row each (in .bss: 14 KB).
float mixLoud[kMostJets];
bool mixWas[kMostJets],mixIn[kMostJets];
int mixNeed[kMostJets],mixOrder[kMostJets];

// Once a frame, every jet heard together (jet_mix.h): each one's layers from its trail; every one heard (or the
// loudest JetSoundMostHeard), as long as the voice pool has voices for them (the loudest first when it has not); each
// one's presence faded toward being heard or not, its voices set; a gone one freed once heard out and faded.
void Mix() noexcept {
    const ULONGLONG ms=GameMs(),frame=GameFrame();
    if(frame==mixFrame)return;
    float dt=mixFrame && frame>mixFrame ? static_cast<float>(frame-mixFrame)/60.0f : 0.0f;
    if(dt>0.25f)dt=0.25f;   // a hitch (a load): no fade skipped through in one step
    mixFrame=frame;
    const jetmix::Params m=MixParams();
    float* const loud=mixLoud;
    bool* const was=mixWas;
    bool* const in=mixIn;
    int* const need=mixNeed;
    int budget=VoicesFree();   // the pool's free voices and those the jets hold: theirs to share
    for(int i=0;i<rowCount;++i) {
        Sound& s=rows[i]->s;
        Hearing& h=rows[i]->h;
        h.r=nullptr;h.n=0;loud[i]=0.0f;was[i]=false;need[i]=0;
        for(int l:s.voice)budget+=l ? 1 : 0;
        if(!s.ref || !s.event)continue;
        if(!s.gone && ms-s.seen>kStaleMs) {   // deleted, or gone with the mission: heard out, not cut
            s.gone=true;
            if(Cfg().debug)Log("SOUND jet %s: not seen for %llu ms, heard on for its sound's way here, then faded",
                               s.own ? "own" : "ai",static_cast<unsigned long long>(ms-s.seen));
        }
        const engine::Trail& trail=rows[i]->trail;
        h.r=engine::Retarded(trail,listener,ms);
        if(!h.r || !Place(h.r->pos,h.r->vel,&h.p)){h.r=nullptr;Silence(s);continue;}
        const bool caught=s.gone && h.r==&trail.s[trail.head];
        if(caught && !s.caught && Cfg().debug)
            Log("SOUND jet %s: its last sound heard (%.0f m), fading out",s.own ? "own" : "ai",h.p.distance);
        s.caught=caught;
        h.cone=engine::ConeAngle(h.r->nose,h.p.toCamera);
        h.n=engine::Hear(*s.event,h.r->in,h.p.distance,h.cone,h.h);
        h.farGain=jetmix::FarGain(s.event->maxD,h.p.distance,m.reach);
        h.doppler=engine::Doppler(*s.event,h.p.doppler,h.cone);
        float sum=0.0f;
        for(int k=0;k<h.n;++k)sum+=h.h[k].gain;
        loud[i]=s.level*h.farGain*sum;
        was[i]=s.ranked;
        for(int k=0;k<h.n;++k)need[i]+=s.level*h.farGain*h.h[k].gain>=engine::kCloseGain ? 1 : 0;   // its voices, heard
    }
    jetmix::Rank(loud,was,need,rowCount,m.most,budget,engine::kOpenGain,mixOrder,in);
    for(int i=0;i<rowCount;++i) {
        Sound& s=rows[i]->s;
        const Hearing& h=rows[i]->h;
        if(!s.ref || !s.event || !h.r)continue;
        if(in[i]!=s.ranked && s.mixed && Cfg().debug)
            Log("SOUND jet %s at %.0f m %s (level %.4f)",s.own ? "own" : "ai",h.p.distance,
                in[i] ? "ranked in" : "ranked out, fading",loud[i]);
        s.loud=loud[i];s.distance=h.p.distance;s.ranked=in[i];s.mixed=true;
        s.presence=jetmix::Step(s.presence,in[i] && !s.caught,m.fadeIn,s.dead ? jetmix::kDeadFade : m.fadeOut,dt);
        if(s.caught && !(s.presence>0.0f)){Silence(s);continue;}   // heard out: its entry free
        const float gain=s.level*h.farGain*jetmix::Gain(s.presence);
        Voices(s,h.h,h.n,h.p,gain,h.doppler);
        const engine::Event& e=*s.event;
        if(s.hasHeard && !s.gone && gain>0.0f) {
            const unsigned bits=engine::Crossed(e,s.heard,h.r->in);
            for(int k=0;k<e.shots;++k) {
                if(!(bits&(1u<<k)) || !There(e.s[k].clip))continue;
                const float g=gain*engine::ShotGain(e,k,h.r->in,h.p.distance);
                ShotVoice(e.s[k].clip,static_cast<int>(s.turn++%static_cast<unsigned>(wt::kVariants[e.s[k].clip])),
                          audio::Heard{g,h.p.side,h.p.front,surround::SpreadAt(h.p.distance,kJetSize,0.0f),h.doppler,
                                       audio::AirCutoff(h.p.distance/kFarAt),false});
                if(Cfg().debug)Log("SOUND jet %s one-shot %d at %.0f m, gain %.3f",s.own ? "own" : "ai",e.s[k].clip,
                                   h.p.distance,g);
            }
        }
        s.heard=h.r->in;s.hasHeard=true;
        if((Cfg().verbose || (s.own && Cfg().debug)) && ms-s.logAt>=2000) {
            s.logAt=ms;
            char g[256];int at=0,open=0;
            for(int k=0;k<h.n && at<200;++k) {
                open+=s.voice[k] ? 1 : 0;
                at+=std::snprintf(g+at,sizeof(g)-at,"%s%.3f",k ? "," : "",gain*h.h[k].gain);
            }
            Log("SOUND jet %s rpm %.0f load %.2f wep %.2f %.0f km/h, %.0f m cone %.0f late %.2f s doppler %.2f far %.3f "
                "presence %.2f%s%s; layers [%s] %d voice(s) of its %d, %d loop(s) open",s.own ? "own" : "ai",
                h.r->in.v[engine::kRpm],h.r->in.v[engine::kLoad],h.r->in.v[engine::kWep],h.r->in.v[engine::kSpeed],
                h.p.distance,h.cone,static_cast<double>(ms-h.r->ms)/1000.0,h.doppler,h.farGain,s.presence,
                s.ranked ? "" : " ranked out",s.gone ? " gone" : "",g,open,h.n,audio::LoopsOpen());
        }
    }
}

// Once a frame: the camera's place, matrix and velocity, and the beat (at the game's volume).
void Tick() noexcept {
    const ULONGLONG frame=GameFrame();
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
        if(!jets)for(int i=0;i<rowCount;++i)if(rows[i]->s.ref)Silence(rows[i]->s);   // switched off
        // The camera is the ground vehicles' and the explosions' listener too (vehsound.cpp, blastsound.cpp SoundAt):
        // kept while any sounds. The jets need it from their first jet on (started); the others from their switch alone,
        // a mission with no jet in it too.
        const bool others=Cfg().vehicleSound || Cfg().explosionVolume>0.0f || Cfg().flybyVolume>0.0f;
        if((jets && started) || (Cfg().enabled && others))Tick();
        else hasListener=false;   // not kept: never a stale camera's place for a consumer switched on later
        if(jets && started && hasListener)Mix();   // every jet heard together (no camera: their voices held as they are)
    } __except(Fault(GetExceptionInformation())){Recover();}
}

// A new mission (mission.cpp MissionStart): the last mission's sounds stopped and their voices let go of.
void ResetJetSound() noexcept {
    for(int i=0;i<rowCount;++i)Silence(rows[i]->s);
    hasListener=false;listenerFrame=0;mixFrame=0;
    listenerVel[0]=listenerVel[1]=listenerVel[2]=0.0f;
}
}  // namespace crew
