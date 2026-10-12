// The jets' hooks (jet.cpp): the 506 physics step body506.cpp hands them to, the bullets' pass-through of a
// flight's wingmen, and the install of the jet files.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "jet_internal.h"
#include "online_authority.h"
#include "ownround.h"
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
// A jet's own rounds never hit the jet itself (ownround.h: its object or any of its own body ids), whatever the bullet's
// "may hit its owner" bit, on which the stock collector's owner test rests (the user, 2026-10-10: 「炮舰机的机炮有可能会
// 打在自己身上」).
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
// Each entry carries the jet's own body ids (ownround.h: its round never hits any of them).
struct Flights { int count; ownround::Craft jet[kMaxJets]; };
Flights flights{};
SRWLOCK flightsLock=SRWLOCK_INIT;
std::atomic<int> flown{0};   // flights.count as last published: the hook's quick "no jet out" test
ULONGLONG publishedFrame=~0ull;

// The body wrappers' ids of jet `v` (game memory: under __try, game thread), into `c`: its flight body (kBody) and every
// ragdoll part's (kRagdollParts, kRagdollCount, stride kRagdollStride, wrapper +kRagdollBody: body506.cpp AirframeShape,
// the crash step 0x650119); a wrapper's id at +kWrapperId (0x11B15E0 and 0x108260 read it there). Parts past
// ownround::kMaxBodies are left out (the V506 ragdoll has 8): the object test still covers them.
constexpr std::size_t kRagdollParts=0x1398,kRagdollCount=0x13A8,kRagdollStride=0xC0,kRagdollBody=0x50,kWrapperId=0xF0;
constexpr std::size_t kMaxRagdollParts=64;
void AddBody(ownround::Craft& c,const unsigned char* wrapper) noexcept {
    if(!wrapper || c.bodies>=ownround::kMaxBodies || !Readable(wrapper,kWrapperId+4))return;
    c.body[c.bodies++]=At<std::uint32_t>(wrapper,kWrapperId);
}
void OwnBodies(const unsigned char* v,ownround::Craft& c) noexcept {
    c.bodies=0;
    __try {
        if(!Readable(v,kBody+8))return;
        AddBody(c,At<const unsigned char*>(v,kBody));
        const auto count=At<std::size_t>(v,kRagdollCount);
        const auto base=At<const unsigned char*>(v,kRagdollParts);
        if(!base || count>kMaxRagdollParts)return;
        for(std::size_t i=0;i<count;++i) {
            const unsigned char* const part=base+i*kRagdollStride;
            if(!Readable(part,kRagdollBody+8))break;
            AddBody(c,At<const unsigned char*>(part,kRagdollBody));
        }
    } __except(FaultLog("JET own bodies",GetExceptionInformation())) {}
}

// What the hook kept off, per thread (no counter shared across threads). Own rounds are logged whatever the debug switch
// (at most every kOwnLogMs, the first at once): a user's log then shows whether a jet's rounds ever came at its own
// airframe (the user, 2026-10-10: 「炮舰机的机炮有可能会打在自己身上」), and through which body.
constexpr ULONGLONG kOwnLogMs=5000;
struct PassLog { ULONGLONG at; unsigned passed,strangers; };
struct OwnLog { ULONGLONG at; unsigned kept,byBody,gunship; const void* jet; std::uint32_t body; };
thread_local PassLog passLog{};
thread_local OwnLog ownLog{};

void CountOwn(const ownround::Judged& j,std::uint32_t body) noexcept {
    ++ownLog.kept;
    if(j.byBody)++ownLog.byBody;
    if(j.shooter->gunship)++ownLog.gunship;
    ownLog.jet=j.shooter->vehicle;ownLog.body=body;
    const ULONGLONG now=GetTickCount64();
    if(ownLog.at && now-ownLog.at<kOwnLogMs)return;
    Log("%s own round kept off its airframe: %u candidate bodies (%u of a gunship, %u by a part's body id only, not its "
        "object) since the last line; last jet %p body %u",ownLog.gunship ? "GUNSHIP" : "JET",ownLog.kept,ownLog.gunship,
        ownLog.byBody,ownLog.jet,ownLog.body);
    ownLog=OwnLog{now,0,0,0,nullptr,0};
}

// Whether candidate `body` (its object `target`) of a round of `owner` is left out (ownround.h Judge): the jet's own
// body, or a wingman's.
bool Passes(const void* owner,const void* target,std::uint32_t body) noexcept {
    AcquireSRWLockShared(&flightsLock);
    const ownround::Judged j=ownround::Judge(flights.jet,flights.count,owner,target,body);
    ownround::Craft shooter{};
    if(j.shooter)shooter=*j.shooter;
    const bool stranger=j.verdict==ownround::Verdict::stock && owner && ownround::Find(flights.jet,flights.count,target) &&
                        !ownround::Find(flights.jet,flights.count,owner);
    ReleaseSRWLockShared(&flightsLock);
    if(j.verdict==ownround::Verdict::own) {
        CountOwn(ownround::Judged{j.verdict,&shooter,j.byBody},body);
        return true;
    }
    const bool pass=j.verdict==ownround::Verdict::wingman;
    if(pass)++passLog.passed;
    else if(stranger)++passLog.strangers;
    const ULONGLONG now=GetTickCount64();
    if(Cfg().debug && now-passLog.at>2000 && (passLog.passed || passLog.strangers)) {
        Log("BULLET through wingmen: %u candidates passed, %u near a jet from a non-jet owner (last %p)",passLog.passed,passLog.strangers,owner);
        passLog=PassLog{now,0,0};
    }
    return pass;
}

// The owner of the bullet whose candidate collector this is, and the object of `body` (nullptr: it has none; game
// memory: under the hook's __try). False with no bullet core.
bool Candidate(void* collector,std::uint32_t body,const void** owner,const void** target) noexcept {
    const auto core=At<const unsigned char*>(collector,kCollectorCore);
    if(!core)return false;
    *owner=At<const void*>(core,kBulletOwner);
    *target=reinterpret_cast<BodyObjectFn>(image+kBodyObject)(body);
    return true;
}

// online_authority.h SparesRide for this candidate: the bullet's owner a player of this machine, the candidate the vehicle
// they ride (human +0x1548, the ride's weak object: boarding.cpp). Offline keeps the stock fast path without looking up
// the candidate. The owner's fields are read only once the candidate is that pointer.
constexpr std::size_t kHumanVehicle=0x1548;
bool SparesOwnRide(void* collector,std::uint32_t body) noexcept {
    if(!InSession())return false;
    const void* owner=nullptr;
    const void* target=nullptr;
    if(!Candidate(collector,body,&owner,&target) || !owner || !target)return false;
    const auto human=static_cast<const unsigned char*>(owner);
    if(!Readable(human,kHumanVehicle+8) || At<const void*>(human,kHumanVehicle)!=target)return false;
    return online::SparesRide(true,IsPlayer(human),target,target);
}

void __fastcall AddBodyHook(void* collector,std::uint32_t body) {
    // A slow round through the Shield Bearer's shield (shield.cpp), whoever fired it.
    bool through=false;
    __try { through=ShieldLetsThrough(collector,body); } __except(FaultLog("SHIELD round",GetExceptionInformation())) { through=false; }
    if(through)return;
    // A round a player of this machine fired from their vehicle, named theirs online (online_authority.h SparesRide).
    bool spare=false;
    __try { spare=SparesOwnRide(collector,body); } __except(FaultLog("BULLET own ride",GetExceptionInformation())) { spare=false; }
    if(spare)return;
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
                (flown.load(std::memory_order_relaxed) && Passes(owner,target,body)));
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
        ownround::Craft& c=f.jet[f.count++];
        c=ownround::Craft{j.ref.obj,j.bay.bombOwner,j.flight,j.bay.ifc!=nullptr || ms<j.bay.bombClear,j.role==Role::gunship,0,{}};
        OwnBodies(static_cast<const unsigned char*>(j.ref.obj),c);
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
