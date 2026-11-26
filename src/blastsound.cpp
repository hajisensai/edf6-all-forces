// The rounds' explosions as War Thunder layers them (the user, 2026-11-26: 「烟不够劲，让他抄抄战雷的」「还有炮声呼啸声
// 什么的」「分层和距离混音，环绕声道之类的记得做好」; docs/feature-wt-sound.md §爆炸; ini ExplosionVolume). Every round's
// explosion sound is the stock AmmoHitSe preset (core+0xA70: common_damages_explode_S .. XL on the explosive weapons)
// played by 0x7B4510 from the round's own code: its hit (0x2325EE in 0x2321B0, only with core+0xAF4 0x400, fuze.h), its
// expiry and timer bursts (0x23FB84, 0x264BEF, 0x28E9CD), SolidExp's and the bomb's (0x297283, 0x297D6D, 0x297FA3) and the
// tail jump 0x233BCA (0x233BC0: add rcx,0xA70 / xor r8d,r8d / jmp 0x7B4510). Each call is pointed at ExplodeSe: an
// explosive round's (core+0xAF4 0x10, the area damage, or a blast radius) is taken off the stock sound and heard as
// ours instead (sound_event.h kBlastSmall .. kBlastHeavy by the larger of its burst radius core+0x788 and its
// AmmoExplosion core+0xA20), placed at the camera (SoundAt: panned to the ears, spread by the event's sound size, the
// low layer to the LFE alone), late by the distance (sound at 340 m/s), within each tier's voice budget. A plain bullet's
// hit, a preset with no bank (it plays nothing), the module not armed (switched off, ExplosionVolume 0, the sounds not
// made, no listener this frame: the tick arms it for kArmedMs at a time) and a fault keep the stock call: never a stock
// sound taken with nothing in its place, never both. A remote player's rounds are the game's own copies of them, run by
// the same code (their hits and bursts at the same calls): heard the same, with no message of ours.
// The rounds going by and coming down (ini FlybyVolume): the bullet system's list walked once a frame (Flyby below).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "jetaudio.h"
#include "sound_event.h"
#include "surround.h"
#include "vehmix.h"
#include "memory.h"
#include "lazy_rows.h"
#include "fuze.h"
#include "edf/host.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

namespace crew {
namespace {
constexpr unsigned kPlaySe=0x7B4510;
const unsigned char kPlaySeSig[]={0x48,0x83,0xEC,0x48,0x48,0x8B,0x41,0x20,0x48,0x85,0xC0,0x75};   // a preset with no bank plays nothing
// The calls that play a round's AmmoHitSe (see the top): rcx the preset core+0xA70, rdx where, r8 0.
constexpr unsigned kSites[]={0x2325EE,0x23FB84,0x264BEF,0x28E9CD,0x297283,0x297D6D,0x297FA3,0x233BCA};
constexpr int kSiteCount=static_cast<int>(sizeof(kSites)/sizeof(kSites[0]));
constexpr std::size_t kPresetInCore=0xA70,kBank=0x20,kCoreFlags=0xAF4,kCoreBurst=0x788,kCoreBlast=0xA20;
using PlaySeFn=void(__fastcall*)(void*,const float*,void*);

constexpr ULONGLONG kArmedMs=250;   // wall ms the tick's say-so holds (a frame or a few; the game paused: no rounds burst)
constexpr int kQueued=64,kPending=384;   // explosions a frame from the hooks to the tick; layers on their way (a bomb's 3 km: 9 s)

struct Burst { float pos[3]; float radius; bool armour; };
SRWLOCK queueLock=SRWLOCK_INIT;
Burst queue[kQueued]{};
int queued=0;
// A plain round's strike on armour (HitSeen): heard as wtevent::HitEvent(kind) at `pos`.
struct Struck { float pos[3]; wtevent::Hit kind; };
Struck struck[kQueued]{};
int strucks=0;
// A round that struck armour this frame, for its AmmoHitSe at 0x2325EE right after (Take): `ours` a plain one's (its hit
// heard as ours: the stock sound taken), else an explosive one's (its explosion heard with the armour's layers over it).
struct Mark { const void* core; bool ours; std::uint32_t frame; };
Mark marks[16]{};
std::uint32_t frameNo=1;   // the tick's count: a mark older than a frame is stale (its round's stock sound not played)
std::atomic<ULONGLONG> armedUntil{0};

// A layer on its way: played when the game clock reaches `due`.
struct Pending { int clip=-1; int variant=0; audio::Heard heard{}; ULONGLONG due=0; };
LazyRows<Pending,kPending> pending;
wtevent::Rotation rotation{};
// The tiers' budgets: each start's game ms and distance.
struct Started { ULONGLONG at; float distance; };
Started started[wtevent::kBlastTiers][8]{};
Started hitStarted[wtevent::kHitKinds][8]{};
int hooked=0,hitHooked=0,probeHooked=0;
bool sigOk=false,hitOk=false,broken=false;

int Fault(const EXCEPTION_POINTERS* e) noexcept {
    const auto r=e->ExceptionRecord;
    Log("SOUND fault on an explosion's or a passing round's sound (%08lX at EDF+%llX): the stock explosions back, no flybys, until the game restarts",r->ExceptionCode,
        static_cast<unsigned long long>(static_cast<const unsigned char*>(r->ExceptionAddress)-image));
    broken=true;
    armedUntil.store(0,std::memory_order_relaxed);
    return EXCEPTION_EXECUTE_HANDLER;
}

float Volume(float v) noexcept { return std::isfinite(v) && v>0.0f ? (v>4.0f ? 4.0f : v) : 0.0f; }

// An explosive round's explosion taken for ours (queued for the tick): true, the stock sound not played.
bool Take(const unsigned char* preset,const float* pos) noexcept {
    if(GetTickCount64()>=armedUntil.load(std::memory_order_relaxed) || !preset || !pos)return false;
    __try {
        // The round's mark taken first, whatever its preset holds: one left behind would be another frame's.
        const unsigned char* const core=preset-kPresetInCore;
        bool armour=false,ours=false;
        AcquireSRWLockExclusive(&queueLock);
        for(Mark& m:marks)
            if(m.core==core) {
                if(frameNo-m.frame<=1u){ours=m.ours;armour=!m.ours;}
                m.core=nullptr;
                break;
            }
        ReleaseSRWLockExclusive(&queueLock);
        if(!At<const void*>(preset,kBank)) {   // the stock plays nothing: an armoured hit's is then ours alone
            audio::Count(ours ? "stock_silent_armour" : "stock_silent");
            return false;
        }
        if(ours){audio::Count("stock_hit_taken");return true;}   // a plain round's hit on armour: heard as ours (Drain)
        const std::uint32_t flags=At<std::uint32_t>(core,kCoreFlags);
        const float burst=At<float>(core,kCoreBurst),blast=At<float>(core,kCoreBlast);
        float radius=std::isfinite(burst) && burst>0.0f ? burst : 0.0f;
        if(std::isfinite(blast) && blast>radius)radius=blast;
        if(!(flags&fuze::kAreaOnHit) && radius<=0.0f)return false;   // a plain bullet's hit: its own sound
        Burst b{{pos[0],pos[1],pos[2]},radius,armour};
        if(!std::isfinite(b.pos[0]+b.pos[1]+b.pos[2]))return false;
        AcquireSRWLockExclusive(&queueLock);
        if(queued<kQueued)queue[queued++]=b;   // more in a frame than that: the rest silent (the budget would drop them)
        ReleaseSRWLockExclusive(&queueLock);
        audio::Count("stock_blast_taken");
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void __fastcall ExplodeSe(void* preset,const float* pos,void* handle) {
    if(Take(static_cast<const unsigned char*>(preset),pos))return;
    // Counted only when the stock really sounds (a preset with a bank): Take counts the silent ones while armed.
    if(preset && At<const void*>(static_cast<const unsigned char*>(preset),kBank))audio::Count("stock_hit_kept");
    reinterpret_cast<PlaySeFn>(image+kPlaySe)(preset,pos,handle);
}

// A layer on its way at `due` (the game clock), unless kPending are already: then it is not heard.
bool Queue(int clip,const audio::Heard& h,ULONGLONG due) noexcept {
    if(!audio::WtThere(clip))return false;
    for(auto& q:pending)
        if(q.clip<0){q=Pending{clip,wtevent::Next(rotation,clip),h,due};return true;}
    return false;
}

// An event heard from `at` (standing still): its layers loud enough at the distance, each after the sound's travel time
// and its own start. `heardAt`: the distance its curves are read at (an incoming round's: where it lands).
void PlayEvent(const wtevent::Event& e,const SoundPlace& p,float heardAt,float vol,ULONGLONG now,bool travel) noexcept {
    wtevent::Heard h[wtevent::kMostLayers];
    const int n=wtevent::Hear(e,heardAt,h);
    const float spread=surround::SpreadAt(p.distance,e.size,e.least);
    int layers=0;
    audio::Heard first{};
    for(int i=0;i<n;++i) {
        const float gain=vol*h[i].gain;
        if(gain<0.001f)continue;
        const float late=(travel ? p.distance/vmix::kSoundSpeed : 0.0f)+h[i].after;
        const audio::Heard heard{gain,p.side,p.front,spread,h[i].ratio,h[i].cutoff,h[i].sub};
        if(Queue(h[i].clip,heard,now+static_cast<ULONGLONG>(late*1000.0f)) && !layers++)first=heard;
    }
    if(layers)audio::Note(e.name,layers,p.distance,first);
}

// Within the budget `b` of the starts `row` (sound_event.h BudgetSlot): its start recorded.
bool Budgeted(Started* row,const wtevent::Budget& b,float distance,ULONGLONG now) noexcept {
    float ages[8],dists[8];
    const int n=b.most<8 ? b.most : 8;
    for(int i=0;i<n;++i) {
        ages[i]=row[i].at ? static_cast<float>(now-row[i].at)/1000.0f : 1e9f;
        dists[i]=row[i].distance;
    }
    const int k=wtevent::BudgetSlot(b,distance,ages,dists,n);
    if(k<0 || k>=n)return false;
    row[k]=Started{now ? now : 1,distance};
    return true;
}

void Drain(bool play) noexcept {
    Burst got[kQueued];
    Struck hits[kQueued];
    AcquireSRWLockExclusive(&queueLock);
    const int n=queued,m=strucks;
    std::memcpy(got,queue,sizeof(Burst)*static_cast<std::size_t>(n));
    std::memcpy(hits,struck,sizeof(Struck)*static_cast<std::size_t>(m));
    queued=0;strucks=0;
    ++frameNo;
    for(Mark& k:marks)if(k.core && frameNo-k.frame>1u)k.core=nullptr;   // its round's stock sound not played: none to take
    ReleaseSRWLockExclusive(&queueLock);
    if(!play)return;
    const float vol=Volume(Cfg().explosionVolume);
    const ULONGLONG now=GameMs();
    static const float still[3]={0.0f,0.0f,0.0f};
    for(int i=0;i<n;++i) {
        SoundPlace p{};
        if(!SoundAt(got[i].pos,still,&p))continue;
        const wtevent::Blast tier=wtevent::BlastTier(got[i].radius);
        const int t=static_cast<int>(tier);
        if(!Budgeted(started[t],wtevent::kBlastBudget[t],p.distance,now))continue;
        PlayEvent(wtevent::BlastEvent(tier),p,p.distance,vol,now,true);
        if(got[i].armour)PlayEvent(wtevent::ArmourEvent(tier),p,p.distance,vol,now,true);
    }
    for(int i=0;i<m;++i) {
        SoundPlace p{};
        const int k=static_cast<int>(hits[i].kind);
        if(!SoundAt(hits[i].pos,still,&p) || p.distance>wtevent::kHitFar[k])continue;
        if(Budgeted(hitStarted[k],wtevent::kHitBudget[k],p.distance,now))
            PlayEvent(wtevent::HitEvent(hits[i].kind),p,p.distance,vol,now,true);
    }
}

void Due(ULONGLONG ms) noexcept {
    for(auto& q:pending)
        if(q.clip>=0 && ms>=q.due){audio::PlayWt(q.clip,q.variant,q.heard);q.clip=-1;}
}

// --- The rounds going by and coming down -----------------------------------------------------------------------------
// Every live round is on the bullet system's list (*(EDF+0x20B32F0): its batch step 0x237CF0, from 0x704EB0, walks it on
// the game's thread: the sentinel node *(system+8), each node {next, prev, core at +0x10}; docs/bullet-pass-re.md), the
// player's, the NPCs', the enemies' and a remote player's copies alike. Once a frame (after the frame's step) each round
// of a shell or bullet class is looked at: one whose closest point to the ear falls within this frame and within
// kPassbyFar m is heard going by there (sound_event.h HearPass by its calibre and speed), late by the sound's travel;
// one of an explosive class falling steeply that will land within kIncomingWithin m of the ear within its lead is heard
// coming down from where it is, once.
constexpr unsigned kBulletSystem=0x20B32F0;
constexpr std::size_t kSystemSentinel=8,kNodeCore=0x10,kBulletCore=0x140;
constexpr std::size_t kCorePos=0xB80,kCoreVel=0xB90,kCoreAcc=0xBA0,kCoreAge=0xAF8;
constexpr int kMostWalked=4096;   // more rounds than that in a frame: the rest not looked at (a broken list: not looped)
// The classes: SolidBullet01 (the guns' rounds, the enemies' too), its Rail, SolidExpBullet01 (the explosive shells),
// GrenadeBullet01 (the grenades, the mortars and howitzers) and its GrenadeBullet01_MapNoDamage (the Bohr's and the
// plugin's flak HE: buildings spared, the same object, its RTTI's vtables at the same offsets 0 / 0x120 / 0xE90);
// RocketBullet01 (and its RocketBullet01_LowLight, the same way) and BombBullet01 only coming down.
constexpr unsigned kSolid=0x17A3F78,kRail=0x17A4038,kSolidExp=0x17A4498,kGrenade=0x17A17E0,kGrenadeMapSafe=0x17A16E8,
                   kRocket=0x17A38E8,kRocketLowLight=0x17A39A8,kBomb=0x17A47D0;
enum class Kind : std::uint8_t { none, round, shell, falling, bomb };
Kind KindOf(const unsigned char* core) noexcept {
    const std::uintptr_t vt=At<std::uintptr_t>(core-kBulletCore,0)-reinterpret_cast<std::uintptr_t>(image);
    return vt==kSolid || vt==kRail ? Kind::round : vt==kSolidExp || vt==kGrenade || vt==kGrenadeMapSafe ? Kind::shell :
           vt==kRocket || vt==kRocketLowLight ? Kind::falling : vt==kBomb ? Kind::bomb : Kind::none;
}
constexpr float kFrameSeconds=1.0f/60.0f;   // the age's frames (core+0xAF8, int32)
constexpr float kSteep=0.5f;                // ours: coming down at least this steep (its fall over its way across) to whistle
constexpr ULONGLONG kIncomingOnce=4000;     // ms a round heard coming down is not heard again (the list's nodes are reused)
struct Seen { const void* core; ULONGLONG until; };
Seen seen[16]{};
Started passStarted[8]{},incomingStarted[2][8]{};
std::uint32_t chance=0x2545F491u;
ULONGLONG lastWalk=0;

float Roll() noexcept {
    chance^=chance<<13;chance^=chance>>17;chance^=chance<<5;
    return static_cast<float>(chance>>8)/16777216.0f;
}
float Radius(const unsigned char* core) noexcept {
    const float burst=At<float>(core,kCoreBurst),blast=At<float>(core,kCoreBlast);
    float r=std::isfinite(burst) && burst>0.0f ? burst : 0.0f;
    if(std::isfinite(blast) && blast>r)r=blast;
    return r;
}

void Passby(const unsigned char* core,const float* pos,const float* vel,const float* ear,float dt,float vol,ULONGLONG now) noexcept {
    const float speed=std::sqrt(vel[0]*vel[0]+vel[1]*vel[1]+vel[2]*vel[2]);
    if(static_cast<float>(At<std::int32_t>(core,kCoreAge))*kFrameSeconds*speed<wtevent::kPassbyFrom)return;
    const float r[3]={pos[0]-ear[0],pos[1]-ear[1],pos[2]-ear[2]};
    float t=0.0f,miss=0.0f;
    if(!wtevent::Closest(r,vel,&t,&miss) || t<0.0f || t>=dt || miss>wtevent::kPassbyFar)return;
    if(!Budgeted(passStarted,wtevent::kPassbyBudget,miss,now))return;
    const float rolls[4]={Roll(),Roll(),Roll(),Roll()};
    wtevent::Heard h[4];
    const int n=wtevent::HearPass(wtevent::PassbyFor(wtevent::CalibreOf(Radius(core))),miss,speed,rolls,h);
    const float at[3]={pos[0]+vel[0]*t,pos[1]+vel[1]*t,pos[2]+vel[2]*t};
    SoundPlace p{};
    if(!n || !SoundAt(at,nullptr,&p))return;
    const ULONGLONG due=now+static_cast<ULONGLONG>((t+p.distance/vmix::kSoundSpeed)*1000.0f);
    int layers=0;
    audio::Heard first{};
    for(int i=0;i<n;++i) {
        const audio::Heard heard{vol*h[i].gain,p.side,p.front,0.0f,1.0f,h[i].cutoff,false};
        if(Queue(h[i].clip,heard,due) && !layers++)first=heard;
    }
    if(layers)audio::Note("passby",layers,p.distance,first);
}

bool SeenBefore(const void* core,ULONGLONG now) noexcept {
    Seen* free=nullptr;
    for(auto& s:seen) {
        if(s.core==core && now<s.until)return true;
        if(!free || s.until<free->until)free=&s;
    }
    *free=Seen{core,now+kIncomingOnce};
    return false;
}

// Debug's tally of why a round coming down near the ear was not heard (docs/feature-wt-sound.md 实机数据): each
// reason counted once a round (its own table, as SeenBefore's, so the tally does not touch which rounds are heard).
Seen whyNot[8][16]{};
void NotHeard(int why,const char* what,const void* core,ULONGLONG now) noexcept {
    if(!Cfg().debug)return;
    for(const Seen& s:whyNot[why])if(s.core==core && now<s.until)return;
    Seen* free=&whyNot[why][0];
    for(Seen& s:whyNot[why])if(s.until<free->until)free=&s;
    *free=Seen{core,now+kIncomingOnce};
    audio::Count(what);
}

void Incoming(const unsigned char* core,bool bomb,const float* pos,const float* vel,const float* ear,float vol,ULONGLONG now) noexcept {
    // The same rounds heard as before the tally (every test below is needed), only tested in an order that tells why
    // one landing near the ear within its lead was not: too flat (by how steep it came down), no burst, or too far.
    if(!(vel[1]<0.0f))return;   // rising
    const float* acc=reinterpret_cast<const float*>(core+kCoreAcc);
    float t=0.0f,land[3];
    if(!wtevent::Lands(pos,vel,acc,ear[1],&t,land) || t>(bomb ? wtevent::kIncomingLeadBomb : wtevent::kIncomingLeadArt))return;
    const float d[3]={land[0]-ear[0],land[1]-ear[1],land[2]-ear[2]};
    const float off=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    if(off>wtevent::kIncomingWithin)return;
    const float across=std::sqrt(vel[0]*vel[0]+vel[2]*vel[2]);
    if(!(vel[1]<-kSteep*across)) {   // a flat shot: by its fall over its way across (tan 10 deg 0.176, 20 deg 0.364)
        const float fall=-vel[1];
        if(fall<0.176f*across)NotHeard(0,"incoming_flat_under10deg",core,now);
        else if(fall<0.364f*across)NotHeard(1,"incoming_flat_under20deg",core,now);
        else NotHeard(2,"incoming_flat_under27deg",core,now);
        return;
    }
    if(!bomb && Radius(core)<=0.0f){NotHeard(3,"incoming_no_burst",core,now);return;}   // nothing to burst
    if(SeenBefore(core,now))return;
    if(!(Roll()<wtevent::kIncomingChance)){audio::Count("incoming_rolled_out");return;}   // SeenBefore: once a round
    if(!Budgeted(incomingStarted[bomb],wtevent::kIncomingBudget[bomb],off,now)){audio::Count("incoming_over_budget");return;}
    SoundPlace p{};
    if(SoundAt(pos,nullptr,&p))PlayEvent(bomb ? wtevent::kIncomingBomb : wtevent::kIncomingArt,p,off,vol,now,false);
}

void Flyby(ULONGLONG now) noexcept {
    const float dt=lastWalk && now>lastWalk ? static_cast<float>(now-lastWalk)/1000.0f : 0.0f;
    lastWalk=now;
    if(!(dt>0.0f) || dt>0.25f)return;   // paused (the clock still) or a stall: the rounds' frame not this one
    const float vol=Volume(Cfg().flybyVolume);
    float ear[3];
    const unsigned char* system=At<const unsigned char*>(image,kBulletSystem);
    if(vol<=0.0f || !system || !ListenerAt(ear))return;
    const unsigned char* const sentinel=At<const unsigned char*>(system,kSystemSentinel);
    if(!sentinel)return;
    int walked=0;
    for(const unsigned char* node=At<const unsigned char*>(sentinel,0);node && node!=sentinel && walked<kMostWalked;
        node=At<const unsigned char*>(node,0),++walked) {
        const unsigned char* core=At<const unsigned char*>(node,kNodeCore);
        if(!core)continue;
        const Kind k=KindOf(core);
        if(k==Kind::none)continue;
        const float* pos=reinterpret_cast<const float*>(core+kCorePos);
        const float* vel=reinterpret_cast<const float*>(core+kCoreVel);
        if(!std::isfinite(pos[0]+pos[1]+pos[2]+vel[0]+vel[1]+vel[2]))continue;
        if(k==Kind::round || k==Kind::shell)Passby(core,pos,vel,ear,dt,vol,now);
        if(k!=Kind::round)Incoming(core,k==Kind::bomb,pos,vel,ear,vol,now);
    }
}

// --- The rounds striking armour ----------------------------------------------------------------------------------------
// Every round's hit is recorded by 0x230CA0 (core, from, its way core+0xB90, the struck body's id, the point, the face's
// normal, its material; docs/hit-sound-re.md): from the hit loop of the flight step 0x2321B0 (0x23238F, 0x23256D: its
// AmmoHitSe at 0x2325EE follows in the same call, once a frame, only with core+0xAF4 0x400 set, 0x20 and the piercing
// 0x100 clear unless 0x4, core+0xB04 not 3) and from the first frame's sweep 0x233CB0 (0x23403B, 0x234210: no sound of
// its own). Each call is pointed at HitSeen: the stock recorder first, then (armed) the struck body's object (0x108260):
// one of the armoured classes (kArmoured, by its RTTI hierarchy) is struck as WT's tanks are. A plain round's strike is
// queued for the tick as sound_event.h HitKind's (the class: a bullet's or a heavy one's; the angle: a glancing one's
// ricochet; its piercing) and its AmmoHitSe after it, when the stock would play it, marked to be taken (Take): the stock
// hit sound or ours, never both. An explosive round's is marked for its explosion to be heard with the armour's layers
// over it. Anything else (no object, an unarmoured one, a laser's, a flame's, a fault) keeps the stock alone. The targets'
// own sounds on being hit (a UFO's, the fortress's: their damage code's, not the round's) are theirs and stay.
constexpr unsigned kHitRecord=0x230CA0,kBodyObject=0x108260;
const unsigned char kHitRecordSig[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x55};
const unsigned char kBodyObjectSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0xED,0xA6,0xFA,0x01,0x8B,0xD1,0x48,0x8D,0x48};
constexpr unsigned kHitSites[]={0x23238F,0x23256D,0x23403B,0x234210};
constexpr unsigned kFlightStepFrom=0x2321B0,kFlightStepTo=0x233CB0;   // the sites whose AmmoHitSe follows
constexpr std::size_t kCoreMode=0xB04;
constexpr std::uint32_t kNoHitSe=0x20,kPierce=0x100,kPierceSe=0x4;
using HitRecordFn=void(__fastcall*)(void*,void*,void*,const std::uint32_t*,const float*,const float*,std::uint64_t);
using BodyObjectFn=const unsigned char*(__fastcall*)(std::uint32_t);
// The armoured: the vehicles, the robots and their gear, the ships, the carriers and the fortress with their parts.
const char* const kArmoured[]={
    ".?AVVehicleBase@@",".?AVEventHelicopter@@",".?AVWalkingEnemyBase@@",".?AVEDF6_BerserkerBase@@",
    ".?AVEDF6_Berserker_Equipment_Bomb@@",".?AVEDF6_Berserker_Equipment_GrappleWire@@",".?AVEDF6_Berserker_Equipment_RocketPunch@@",
    ".?AVTransporterBase@@",".?AVTransporter_Container@@",".?AVMovingFortress@@",".?AVMovingFortress_PartsBase@@",
    ".?AVUfoMother511@@",".?AVUfoMother511PartsBase@@",".?AVUfoSmall507@@",".?AVUfoSmall515@@",".?AVUfoCarrier@@",
    ".?AVUfoCarrier508@@",".?AVEDF6_SpinnerUfo@@",".?AVDeiroi@@",".?AVDeiroiFootParts_Base@@",".?AVEDF6_TimeShip@@",
    ".?AVEDF6_TimeShip_Anchor@@",".?AVEDF6_TimeShip_Anchor_Barrier@@",".?AVEDF6_TimeShip_Capsule@@",".?AVEDF6_GrandMother@@",
    ".?AVEDF6_GrandMotherPartsBase@@",".?AVEDF6_GrandMother_Arm@@",".?AVDropBoat512@@",".?AVSoldierDroneBase@@",
    ".?AVBombingPlane@@",".?AVGeneratorPoll@@",".?AVSentryGunBullet_Open01@@",".?AVDecoy@@",".?AVDecoyBody01@@",
    ".?AVEDF6_Jormungand_AdditionalParts@@"};
// RTTI: the vtable's complete object locator at -8, its hierarchy (+0x10) of bases (+8 count, +0xC array), each base's
// type descriptor (+0) with its name at +0x10 (damagestats.cpp's reading).
constexpr std::size_t kColHierarchy=0x10,kHierBases=0x8,kHierArray=0xC,kTypeName=0x10;
bool ArmouredVtable(const unsigned char* vt) noexcept {
    const auto col=*reinterpret_cast<const unsigned char* const*>(vt-8);
    const std::uint32_t hd=At<std::uint32_t>(col,kColHierarchy);
    if(!hd || hd+kHierArray+4>=edf::kImageSize)return false;
    const std::uint32_t bases=At<std::uint32_t>(image+hd,kHierBases),array=At<std::uint32_t>(image+hd,kHierArray);
    if(bases>64 || array+bases*4>=edf::kImageSize)return false;
    for(std::uint32_t i=0;i<bases;++i) {
        const std::uint32_t bcd=At<std::uint32_t>(image+array,4*i);
        if(!bcd || bcd+4>=edf::kImageSize)continue;
        const std::uint32_t td=At<std::uint32_t>(image+bcd,0);
        if(!td || td+kTypeName+64>=edf::kImageSize)continue;
        const char* name=reinterpret_cast<const char*>(image+td+kTypeName);
        for(const char* a:kArmoured)if(std::strcmp(name,a)==0)return true;
    }
    return false;
}
// The classes seen (their vtables, armoured or not): looked up once each.
struct Armour { const unsigned char* vt; bool armoured; };
constexpr std::size_t kArmours=128;
Armour armours[kArmours]{};
bool Armoured(const unsigned char* object) noexcept {
    const auto vt=At<const unsigned char*>(object,0);
    if(vt<image+8 || vt>=image+edf::kImageSize)return false;
    const std::size_t h=(reinterpret_cast<std::uintptr_t>(vt)>>3)%kArmours;
    for(std::size_t i=0;i<kArmours;++i) {
        Armour& a=armours[(h+i)%kArmours];
        if(a.vt==vt)return a.armoured;
        if(!a.vt){a=Armour{vt,ArmouredVtable(vt)};return a.armoured;}
    }
    return ArmouredVtable(vt);   // the table full: looked up each time
}
void MarkRound(const void* core,bool ours) noexcept {   // queueLock held
    Mark* slot=&marks[0];
    for(Mark& m:marks)if(!m.core || m.core==core){slot=&m;break;}
    *slot=Mark{core,ours,frameNo};
}

void Strike(const unsigned char* core,const float* vel,std::uint32_t body,const float* point,const float* normal,bool stepped) noexcept {
    const std::uintptr_t vt=At<std::uintptr_t>(core-kBulletCore,0)-reinterpret_cast<std::uintptr_t>(image);
    const bool solid=vt==kSolid,rail=vt==kRail,shellClass=vt==kSolidExp || vt==kGrenade;
    if(!solid && !rail && !shellClass)return;   // a laser's, a flame's, a missile's...: the stock alone
    const unsigned char* object=reinterpret_cast<BodyObjectFn>(image+kBodyObject)(body);
    if(!object || !Armoured(object))return;
    const std::uint32_t flags=At<std::uint32_t>(core,kCoreFlags);
    const bool explosive=(flags&fuze::kAreaOnHit) || Radius(core)>0.0f;
    const bool stockSe=stepped && !(flags&kNoHitSe) && At<std::int32_t>(core,kCoreMode)!=3 && (flags&fuze::kHitSound) &&
                       (!(flags&kPierce) || (flags&kPierceSe));
    if(explosive) {
        if(!stockSe)return;   // its explosion comes at another call: heard without the armour's layers
        AcquireSRWLockExclusive(&queueLock);
        MarkRound(core,false);
        ReleaseSRWLockExclusive(&queueLock);
        return;
    }
    if(!std::isfinite(point[0]+point[1]+point[2]))return;
    float facing=std::numeric_limits<float>::quiet_NaN();
    const float v=std::sqrt(vel[0]*vel[0]+vel[1]*vel[1]+vel[2]*vel[2]);
    const float nl=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
    if(v>1e-3f && nl>1e-3f && std::isfinite(v*nl))
        facing=std::fabs(vel[0]*normal[0]+vel[1]*normal[1]+vel[2]*normal[2])/(v*nl);
    const wtevent::Hit kind=wtevent::HitKind(!solid,(flags&kPierce)!=0,facing);
    AcquireSRWLockExclusive(&queueLock);
    if(strucks<kQueued)struck[strucks++]=Struck{{point[0],point[1],point[2]},kind};
    if(stockSe)MarkRound(core,true);
    ReleaseSRWLockExclusive(&queueLock);
}

template<bool kStepped>
void __fastcall HitSeen(void* core,void* from,void* vel,const std::uint32_t* body,const float* point,const float* normal,std::uint64_t material) {
    reinterpret_cast<HitRecordFn>(image+kHitRecord)(core,from,vel,body,point,normal,material);
    if(broken || GetTickCount64()>=armedUntil.load(std::memory_order_relaxed) || !core || !vel || !body || !point || !normal)return;
    __try {
        Strike(static_cast<const unsigned char*>(core),static_cast<const float*>(vel),*body,point,normal,kStepped);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

// --- The stock sound starts (Debug) ---
// Every preset start (0x7B4460 / 0x7B44B0 / 0x7B4510 / 0x7B4560 into 0x7AA8C0: rcx its bank, edx the cue, r8 the preset)
// counted, a vehicle's held one's apart (vehsound.cpp: none may start, its cues cleared): the stats line's stock_se and
// stock_se_held.
constexpr unsigned kSeStart=0x7AA8C0;
const unsigned char kSeStartSig[]={0x48,0x83,0xEC,0x58,0xF3,0x0F,0x10,0x84,0x24,0x80,0x00,0x00,0x00,0x4D,0x8B,0xC8};
constexpr unsigned kSeSites[]={0x7B449F,0x7B44FC,0x7B454F,0x7B45AE};
using SeStartFn=std::uintptr_t(__fastcall*)(void*,std::uint32_t,void*,float,float,void*,void*);
std::uintptr_t __fastcall SeProbe(void* bank,std::uint32_t cue,void* preset,float a4,float a5,void* a6,void* a7) {
    audio::Count("stock_se");
    if(VehicleSoundHeld(preset))audio::Count("stock_se_held");
    return reinterpret_cast<SeStartFn>(image+kSeStart)(bank,cue,preset,a4,a5,a6,a7);
}
}  // namespace

bool InstallBlastSound() noexcept {
    __try {
        sigOk=Matches(kPlaySe,kPlaySeSig,sizeof(kPlaySeSig));
        hooked=0;
        if(sigOk)
            for(const unsigned site:kSites) {
                bool changed=false;
                if(RedirectCall(image+site,image+kPlaySe,reinterpret_cast<void*>(&ExplodeSe),changed))++hooked;
                else Log("HOOK blast sound: the call at EDF+%X is not the stock one: its explosions keep the stock sound",site);
            }
        Log("HOOK blast sound=%d (%d/%d calls)",sigOk && hooked>0,hooked,kSiteCount);
        hitOk=sigOk && hooked>0 && Matches(kHitRecord,kHitRecordSig,sizeof(kHitRecordSig)) &&
              Matches(kBodyObject,kBodyObjectSig,sizeof(kBodyObjectSig));
        hitHooked=0;
        if(hitOk)
            for(const unsigned site:kHitSites) {
                bool changed=false;
                void* const to=site>kFlightStepFrom && site<kFlightStepTo ? reinterpret_cast<void*>(&HitSeen<true>)
                                                                          : reinterpret_cast<void*>(&HitSeen<false>);
                if(RedirectCall(image+site,image+kHitRecord,to,changed))++hitHooked;
                else Log("HOOK armour hit sound: the call at EDF+%X is not the stock one: its hits keep the stock sound",site);
            }
        Log("HOOK armour hit sound=%d (%d/%d calls)",hitOk && hitHooked>0,hitHooked,static_cast<int>(sizeof(kHitSites)/sizeof(kHitSites[0])));
        probeHooked=0;
        if(Cfg().debug && Matches(kSeStart,kSeStartSig,sizeof(kSeStartSig)))
            for(const unsigned site:kSeSites) {
                bool changed=false;
                if(RedirectCall(image+site,image+kSeStart,reinterpret_cast<void*>(&SeProbe),changed))++probeHooked;
            }
        if(Cfg().debug)Log("HOOK stock sound probe (Debug) %d/%d calls",probeHooked,static_cast<int>(sizeof(kSeSites)/sizeof(kSeSites[0])));
        return sigOk && hooked>0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void BlastSoundTick() noexcept {
    if(broken)return;
    __try {
        const bool ready=Cfg().enabled && pending.Allocate("blast sound") && audio::ClipsReady() && SoundListening();
        const bool blasts=ready && sigOk && hooked>0 && Volume(Cfg().explosionVolume)>0.0f;
        // Armed only while ours can sound: until then, and the moment it is switched off, the stock explosions.
        armedUntil.store(blasts ? GetTickCount64()+kArmedMs : 0,std::memory_order_relaxed);
        Drain(blasts);
        const ULONGLONG now=GameMs();
        if(ready && Volume(Cfg().flybyVolume)>0.0f)Flyby(now);
        else lastWalk=0;
        if(ready)Due(now);
        else for(auto& q:pending)q.clip=-1;
        audio::StatsTick();
    } __except(Fault(GetExceptionInformation())) {}
}

void ResetBlastSound() noexcept {
    AcquireSRWLockExclusive(&queueLock);
    queued=0;
    ReleaseSRWLockExclusive(&queueLock);
    for(auto& q:pending)q.clip=-1;
    std::memset(started,0,sizeof(started));
    std::memset(passStarted,0,sizeof(passStarted));
    std::memset(incomingStarted,0,sizeof(incomingStarted));
    std::memset(seen,0,sizeof(seen));
    std::memset(hitStarted,0,sizeof(hitStarted));
    AcquireSRWLockExclusive(&queueLock);
    strucks=0;
    std::memset(marks,0,sizeof(marks));
    ReleaseSRWLockExclusive(&queueLock);
    lastWalk=0;
}
}  // namespace crew
