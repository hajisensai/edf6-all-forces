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
#include <atomic>
#include <cmath>
#include <cstring>

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

struct Burst { float pos[3]; float radius; };
SRWLOCK queueLock=SRWLOCK_INIT;
Burst queue[kQueued]{};
int queued=0;
std::atomic<ULONGLONG> armedUntil{0};

// A layer on its way: played when the game clock reaches `due`.
struct Pending { int clip=-1; int variant=0; audio::Heard heard{}; ULONGLONG due=0; };
LazyRows<Pending,kPending> pending;
wtevent::Rotation rotation{};
// The tiers' budgets: each start's game ms and distance.
struct Started { ULONGLONG at; float distance; };
Started started[wtevent::kBlastTiers][8]{};
int hooked=0;
bool sigOk=false,broken=false;

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
        if(!At<const void*>(preset,kBank))return false;   // the stock plays nothing either
        const unsigned char* const core=preset-kPresetInCore;
        const std::uint32_t flags=At<std::uint32_t>(core,kCoreFlags);
        const float burst=At<float>(core,kCoreBurst),blast=At<float>(core,kCoreBlast);
        float radius=std::isfinite(burst) && burst>0.0f ? burst : 0.0f;
        if(std::isfinite(blast) && blast>radius)radius=blast;
        if(!(flags&fuze::kAreaOnHit) && radius<=0.0f)return false;   // a plain bullet's hit: its own sound
        Burst b{{pos[0],pos[1],pos[2]},radius};
        if(!std::isfinite(b.pos[0]+b.pos[1]+b.pos[2]))return false;
        AcquireSRWLockExclusive(&queueLock);
        if(queued<kQueued)queue[queued++]=b;   // more in a frame than that: the rest silent (the budget would drop them)
        ReleaseSRWLockExclusive(&queueLock);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void __fastcall ExplodeSe(void* preset,const float* pos,void* handle) {
    if(Take(static_cast<const unsigned char*>(preset),pos))return;
    reinterpret_cast<PlaySeFn>(image+kPlaySe)(preset,pos,handle);
}

// A layer on its way at `due` (the game clock), unless kPending are already: then it is not heard.
void Queue(int clip,const audio::Heard& h,ULONGLONG due) noexcept {
    if(!audio::WtThere(clip))return;
    for(auto& q:pending)
        if(q.clip<0){q=Pending{clip,wtevent::Next(rotation,clip),h,due};return;}
}

// An event heard from `at` (standing still): its layers loud enough at the distance, each after the sound's travel time
// and its own start. `heardAt`: the distance its curves are read at (an incoming round's: where it lands).
void PlayEvent(const wtevent::Event& e,const SoundPlace& p,float heardAt,float vol,ULONGLONG now,bool travel) noexcept {
    wtevent::Heard h[wtevent::kMostLayers];
    const int n=wtevent::Hear(e,heardAt,h);
    const float spread=surround::SpreadAt(p.distance,e.size,e.least);
    for(int i=0;i<n;++i) {
        const float gain=vol*h[i].gain;
        if(gain<0.001f)continue;
        const float late=(travel ? p.distance/vmix::kSoundSpeed : 0.0f)+h[i].after;
        Queue(h[i].clip,audio::Heard{gain,p.side,p.front,spread,1.0f,h[i].cutoff,h[i].sub},now+static_cast<ULONGLONG>(late*1000.0f));
    }
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
    AcquireSRWLockExclusive(&queueLock);
    const int n=queued;
    std::memcpy(got,queue,sizeof(Burst)*static_cast<std::size_t>(n));
    queued=0;
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
        if(Budgeted(started[t],wtevent::kBlastBudget[t],p.distance,now))
            PlayEvent(wtevent::BlastEvent(tier),p,p.distance,vol,now,true);
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
// GrenadeBullet01 (the grenades, the mortars and howitzers); RocketBullet01 and BombBullet01 only coming down.
constexpr unsigned kSolid=0x17A3F78,kRail=0x17A4038,kSolidExp=0x17A4498,kGrenade=0x17A17E0,kRocket=0x17A38E8,kBomb=0x17A47D0;
enum class Kind : std::uint8_t { none, round, shell, falling, bomb };
Kind KindOf(const unsigned char* core) noexcept {
    const std::uintptr_t vt=At<std::uintptr_t>(core-kBulletCore,0)-reinterpret_cast<std::uintptr_t>(image);
    return vt==kSolid || vt==kRail ? Kind::round : vt==kSolidExp || vt==kGrenade ? Kind::shell :
           vt==kRocket ? Kind::falling : vt==kBomb ? Kind::bomb : Kind::none;
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
    for(int i=0;i<n;++i)
        Queue(h[i].clip,audio::Heard{vol*h[i].gain,p.side,p.front,0.0f,1.0f,h[i].cutoff,false},due);
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

void Incoming(const unsigned char* core,bool bomb,const float* pos,const float* vel,const float* ear,float vol,ULONGLONG now) noexcept {
    const float across=std::sqrt(vel[0]*vel[0]+vel[2]*vel[2]);
    if(!(vel[1]<-kSteep*across) || (!bomb && Radius(core)<=0.0f))return;   // a flat shot, rising, or nothing to burst
    const float* acc=reinterpret_cast<const float*>(core+kCoreAcc);
    float t=0.0f,land[3];
    if(!wtevent::Lands(pos,vel,acc,ear[1],&t,land) || t>(bomb ? wtevent::kIncomingLeadBomb : wtevent::kIncomingLeadArt))return;
    const float d[3]={land[0]-ear[0],land[1]-ear[1],land[2]-ear[2]};
    const float off=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    if(off>wtevent::kIncomingWithin || SeenBefore(core,now))return;
    if(!(Roll()<wtevent::kIncomingChance) || !Budgeted(incomingStarted[bomb],wtevent::kIncomingBudget[bomb],off,now))return;
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
    lastWalk=0;
}
}  // namespace crew
