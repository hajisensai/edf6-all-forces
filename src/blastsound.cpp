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
    Log("SOUND fault on an explosion's sound (%08lX at EDF+%llX): the stock explosions back until the game restarts",r->ExceptionCode,
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

// An event heard from `at` (standing still): its layers loud enough at the distance, each after the sound's travel time
// and its own start.
void PlayEvent(const wtevent::Event& e,const SoundPlace& p,float vol,ULONGLONG now) noexcept {
    wtevent::Heard h[wtevent::kMostLayers];
    const int n=wtevent::Hear(e,p.distance,h);
    const float spread=surround::SpreadAt(p.distance,e.size,e.least);
    int free=0;
    for(int i=0;i<n;++i) {
        const float gain=vol*h[i].gain;
        if(gain<0.001f || !audio::WtThere(h[i].clip))continue;
        while(free<kPending && pending[static_cast<std::size_t>(free)].clip>=0)++free;
        if(free>=kPending)return;   // more on their way than kPending: the rest are not heard
        pending[static_cast<std::size_t>(free)]=Pending{h[i].clip,wtevent::Next(rotation,h[i].clip),
                   audio::Heard{gain,p.side,p.front,spread,1.0f,h[i].cutoff,h[i].sub},
                   now+static_cast<ULONGLONG>((p.distance/vmix::kSoundSpeed+h[i].after)*1000.0f)};
    }
}

// Within the tier's budget (sound_event.h BudgetSlot): its start recorded.
bool Budgeted(wtevent::Blast tier,float distance,ULONGLONG now) noexcept {
    const int t=static_cast<int>(tier);
    const wtevent::Budget& b=wtevent::kBlastBudget[t];
    float ages[8],dists[8];
    const int n=b.most<8 ? b.most : 8;
    for(int i=0;i<n;++i) {
        const Started& s=started[t][i];
        ages[i]=s.at ? static_cast<float>(now-s.at)/1000.0f : 1e9f;
        dists[i]=s.distance;
    }
    const int k=wtevent::BudgetSlot(b,distance,ages,dists,n);
    if(k<0 || k>=n)return false;
    started[t][k]=Started{now ? now : 1,distance};
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
        if(Budgeted(tier,p.distance,now))PlayEvent(wtevent::BlastEvent(tier),p,vol,now);
    }
}

void Due(ULONGLONG ms) noexcept {
    for(auto& q:pending)
        if(q.clip>=0 && ms>=q.due){audio::PlayWt(q.clip,q.variant,q.heard);q.clip=-1;}
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
    if(!sigOk || !hooked || broken)return;
    __try {
        const bool on=Cfg().enabled && Volume(Cfg().explosionVolume)>0.0f && pending.Allocate("blast sound") &&
                      audio::ClipsReady() && SoundListening();
        // Armed only while ours can sound: until then, and the moment it is switched off, the stock explosions.
        armedUntil.store(on ? GetTickCount64()+kArmedMs : 0,std::memory_order_relaxed);
        Drain(on);
        if(on)Due(GameMs());
        else for(auto& q:pending)q.clip=-1;
    } __except(Fault(GetExceptionInformation())) {}
}

void ResetBlastSound() noexcept {
    AcquireSRWLockExclusive(&queueLock);
    queued=0;
    ReleaseSRWLockExclusive(&queueLock);
    for(auto& q:pending)q.clip=-1;
    std::memset(started,0,sizeof(started));
}
}  // namespace crew
