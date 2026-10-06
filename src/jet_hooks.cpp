// The jets' hooks (jet.cpp): the 506 physics step body506.cpp hands them to, the bullets' pass-through of a
// flight's wingmen, and the install of the jet files.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "jet_internal.h"
#include <atomic>

namespace crew {
namespace jet {
namespace {
// Flights (docs/bullet-pass-re.md): the jets a mission places are one flight; launched jets from one
// source (an Air Raider's call, a mission's strike) within a short time of the last are one (jet_spawn.cpp
// FlightFor), a carrier's drones are its. A jet's rounds pass through the other jets of its flight: the
// bullets' candidate collector (vtable kAddBodySlot, slot 0 addBody kAddBody) leaves out the body of a wingman
// (kBodyObject: body id -> object) when the bullet's owner (core = collector+kCollectorCore, owner at
// core+kBulletOwner) is a jet of the same flight; all else is the stock function's (friendly fire stays as it is).
// The sidecar's passengers' rounds pass through their own bike and its driver (sidecar.cpp SidecarBulletPass) by the
// same hook: it is installed on its own signatures (InstallBulletPass), not with the jets, whose profile is the heli
// pilot's.
constexpr unsigned kAddBodySlot=0x179E128,kAddBody=0x232AA0,kBodyObject=0x108260;
constexpr std::size_t kCollectorCore=0x88,kBulletOwner=0x9A8;
const unsigned char kAddBodySig[]={0x48,0x89,0x4C,0x24,0x08,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,
                                   0x83,0xEC,0x30,0x4C,0x8B,0xF1,0x45,0x33,0xE4,0x44,0x89,0xA4,0x24,0x80,0x00,0x00};
const unsigned char kBodyObjectSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x05};
const unsigned char kBodyObjectSig2[]={0x8B,0xD1,0x48,0x8D,0x48,0x10,0xE8};   // at +11
const unsigned char kDeleteSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48};
using AddBodyFn=void(__fastcall*)(void*,std::uint32_t);
using BodyObjectFn=const void*(__fastcall*)(std::uint32_t);
AddBodyFn nextAddBody=nullptr;
bool passOk=false,hooksOk=false;

// Who is in which flight, as the pass-through reads it. The bullets' batch step (0x237CF0) runs from the main
// loop's Application slot 7 (docs/bullet-pass-re.md §2, §4.1: "M", on the game thread); the jets' table is
// changed by every jet's frame. Not proven to be the same thread, the hook reads only this copy, which the game
// thread publishes once a frame and whenever an entry comes or goes, under a lock it never holds while reading
// game memory: no torn entries, no read of the table while a frame rewrites it, no game clock off its thread.
struct Wingman { const void* vehicle; const void* bombOwner; unsigned flight; bool bombs; };
struct Flights { int count; Wingman jet[kMaxJets]; };
Flights flights{};
SRWLOCK flightsLock=SRWLOCK_INIT;
std::atomic<int> flown{0};   // flights.count as last published: the hook's quick "no jet out" test
ULONGLONG publishedFrame=~0ull;

// Whether a round of `owner` is a bomb (or bomblet) of a bomber in `flight`: the stock bombers have no body to
// hit, ours do, and their bombs leave the bay inside them.
bool BombOf(const Flights& f,const void* owner,unsigned flight) noexcept {
    for(int i=0;i<f.count;++i)if(f.jet[i].flight==flight && f.jet[i].bombs && f.jet[i].bombOwner==owner)return true;
    return false;
}

const Wingman* Find(const Flights& f,const void* v) noexcept {
    if(!v)return nullptr;
    for(int i=0;i<f.count;++i)if(f.jet[i].vehicle==v)return &f.jet[i];
    return nullptr;
}

// Whether a round of `owner` passes through `target` (a body's object): both of one flight.
struct PassLog { ULONGLONG at; unsigned passed,strangers; };
thread_local PassLog passLog{};   // per thread: no counter shared across threads
bool Passes(const void* owner,const void* target) noexcept {
    AcquireSRWLockShared(&flightsLock);
    const Wingman* const t=Find(flights,target);
    const Wingman* const s=t ? Find(flights,owner) : nullptr;
    const bool pass=t && (s ? s!=t && s->flight==t->flight : owner && BombOf(flights,owner,t->flight));
    const bool stranger=t && !s && owner && !pass;
    ReleaseSRWLockShared(&flightsLock);
    if(!t)return false;
    if(pass)++passLog.passed;
    else if(stranger)++passLog.strangers;
    const ULONGLONG now=GetTickCount64();
    if(Cfg().debug && now-passLog.at>2000 && (passLog.passed || passLog.strangers)) {
        Log("BULLET through wingmen: %u candidates passed, %u near a jet from a non-jet owner (last %p)",passLog.passed,passLog.strangers,owner);
        passLog=PassLog{now,0,0};
    }
    return pass;
}

// The owner of the bullet whose candidate collector this is, and the object of `body` (game memory: under the
// hook's __try).
bool Candidate(void* collector,std::uint32_t body,const void** owner,const void** target) noexcept {
    const auto core=At<const unsigned char*>(collector,kCollectorCore);
    if(!core)return false;
    *owner=At<const void*>(core,kBulletOwner);
    *target=reinterpret_cast<BodyObjectFn>(image+kBodyObject)(body);
    return *target!=nullptr;
}

void __fastcall AddBodyHook(void* collector,std::uint32_t body) {
    // A slow round through the Shield Bearer's shield (shield.cpp), whoever fired it.
    bool through=false;
    __try { through=ShieldLetsThrough(collector,body); } __except(FaultLog("SHIELD round",GetExceptionInformation())) { through=false; }
    if(through)return;
    bool pass=false;
    // Only while something can be passed: a plugin jet flown (Publish) or a passenger in a sidecar (sidecar.cpp, kept
    // by who boards and leaves). Every other round keeps the stock path, with no body lookup and no lock.
    if(flown.load(std::memory_order_relaxed) || SidecarPassengers()) {
        const void* owner=nullptr;
        const void* target=nullptr;
        bool found=false;
        __try { found=Candidate(collector,body,&owner,&target); } __except(FaultLog("BULLET pass-through",GetExceptionInformation())) { found=false; }
        __try {
            const auto core=At<const unsigned char*>(collector,kCollectorCore);
            pass=found && (SidecarBulletPass(owner,target,At<const void*>(core,kBulletOwner+8)) ||
                (flown.load(std::memory_order_relaxed) && Passes(owner,target)));
        } __except(FaultLog("SIDECAR pass-through",GetExceptionInformation())) { pass=false; }
    }
    if(!pass)nextAddBody(collector,body);
}
}  // namespace

void Publish(bool force) noexcept {
    const ULONGLONG frame=GameFrame();
    if(!force && frame==publishedFrame)return;
    publishedFrame=frame;
    const ULONGLONG ms=GameMs();
    Flights f{};
    for(const auto& j:jets) {
        if(!Flown(j,ms))continue;
        f.jet[f.count++]=Wingman{j.ref.obj,j.bay.bombOwner,j.flight,j.bay.ifc!=nullptr || ms<j.bay.bombClear};
    }
    AcquireSRWLockExclusive(&flightsLock);
    flights=f;
    ReleaseSRWLockExclusive(&flightsLock);
    flown.store(f.count,std::memory_order_relaxed);
}

bool PassThrough() noexcept { return passOk; }
bool HooksOk() noexcept { return hooksOk; }
}  // namespace jet

using namespace jet;

bool SidecarBulletHooked() noexcept { return jet::PassThrough(); }

// The 506 physics step (body506.cpp), after the stock one: the jet's velocity and spin replace the heli's.
bool JetBodyStep(unsigned char* v,float* lin,float* ang) noexcept {
    if(v[kDead]){PrimerCorpseStep(v);return false;}   // a shot-down centipede's body curls as it falls
    const ULONGLONG ms=GameMs();
    Jet* j=FindJet(v);
    if(!j || !j->m.ready || ms-j->seen>200)return false;
    const auto body=At<void*>(v,kBody);
    if(!body)return false;
    JetMotionProps(body);
    ShieldBlock(v,j->m.vel);   // its own velocity: the next frame's flight starts from the glance
    for(int i=0;i<3;++i){lin[i]=j->m.vel[i];ang[i]=j->m.omega[i];}
    return true;
}

// The bullets' candidate collector (see Flights): its own signatures only, before the jets and the sidecar, which
// both pass rounds through it; without it a jet's rounds hit its wingmen and a passenger's their own bike, as stock.
bool InstallBulletPass() noexcept {
    __try {
        const auto passSlot=reinterpret_cast<void**>(image+kAddBodySlot);
        if(Matches(kAddBody,kAddBodySig,sizeof(kAddBodySig)) && Matches(kBodyObject,kBodyObjectSig,sizeof(kBodyObjectSig)) &&
           Matches(kBodyObject+11,kBodyObjectSig2,sizeof(kBodyObjectSig2)) && *passSlot) {
            void* const was=*passSlot;
            if(was!=image+kAddBody)Log("BULLET addBody chaining onto %p (another plugin)",was);
            nextAddBody=reinterpret_cast<AddBodyFn>(was);
            passOk=PatchVtableSlot(passSlot,was,reinterpret_cast<void*>(&AddBodyHook));
        }
    } __except(FaultLog("BULLET pass install",GetExceptionInformation())){passOk=false;}
    Log("HOOK bullet pass-through (wingmen, sidecar passengers)=%d",passOk);
    return passOk;
}

bool InstallJets() noexcept {
    __try {
        if(!Body506Ok()){Log("JET: no 506 physics hook (body506): jets off");return false;}
        if(!Matches(kDelete,kDeleteSig,sizeof(kDeleteSig))){Log("JET profile mismatch: jets off");return false;}
        hooksOk=true;
        InstallJetProps();
        InstallBoosters();
        InstallShields();
        const bool spawn=InstallSpawn();
        const bool bay=InstallBay(spawn);
        const bool dolls=spawn && InstallDolls();
        const bool farOn=InstallFarRender();
        Log("HOOK jets physics=%d spawn=%d bay=%d wingmenPass=%d dolls=%d farRender=%d",hooksOk,spawn,bay,passOk,dolls,farOn);
        return hooksOk;
    } __except(FaultLog("JET install",GetExceptionInformation())){return false;}
}
}  // namespace crew
