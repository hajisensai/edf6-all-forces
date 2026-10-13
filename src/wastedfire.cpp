// Fire that cannot hurt its target (wasted_fire.h, hit_predict.h; docs/feedback-2026-10-12-ai-targeting.md). The damage
// hook (damagestats.cpp DamageHook) reports each hit it settles; the AI (npcai.cpp, heli.cpp, jet_combat.cpp, ground.cpp)
// asks WastedFire / HoldFire before it takes a target or pulls a trigger, and WeakAim where to aim.
//
// How the game decides whether a hit counts (EDF.dll 0x678CCB46, static, H): the damage function 0x547C30 asks the
// target's vtable slot 16 about the hit (0x54808A; skipped when +0x380 has 0x800) and then slot 17 (0x5480A2); a no from
// either leaves the HP alone (0x5480C7 -> 0x548625) unless the hit's GameDamageInfo +0x60 has 0x40. Before that, +0x380
// bit 0 (0x5480F8) is the invincible flag every class shares. Of the 150 GameObjectBase classes (RTTI walk), 21 override
// slot 16 and 15 override slot 17 (docs/feedback-2026-10-12-ai-targeting.md lists them). The prediction works per
// override, never per enemy name:
//  - kAccept: the overrides whose answer depends only on the target's state and the hit's position / body, with no side
//    effects (their code reads, the helpers 0x6EA430 / 0x6BB5A0 / 0x6BB4A0 / 0x71810 read; nothing written, nothing sent):
//    the plugin asks them about a predicted hit (a one-hit GameDamageInfo built from a layer-11 ray, heli.cpp ObjectRay):
//      0x54AA40 / 0x4BC8A0 always yes; 0x3EE2F0 (EDF6_TimeShip) always no; 0x4F7190 (UfoCarrier / UfoCarrier508 /
//      DropBoat512) only the core part (+0xD8C) while its damage window +0xD85 is open; 0x3F6970 (EDF6_TimeShip_Anchor)
//      a hit inside its box (+0x1128) while no living ship shields it; 0x401200 (GeneratorPoll) a hit inside its box
//      (+0xCA0); 0x4D1790 (MovingFortress parts) unless invincible.
//  - every other override (the mothership and its parts, the moving fortress body, the shield bearer, the grandmother,
//    soldiers...): side effects (network messages, state writes) or not read through: not asked; the feedback
//    (wasted_fire.h: 4 hits in a row that hurt nothing hold that shooter's fire 6 s) covers them.
// The windows (hit_predict.h): the invincible flag for all; a ship's damage window (+0xD85) and its hatch timer (+0xD84
// open, +0xD90 frames left: UfoCarrier_CloseInterval counting down, 0x4F8507); an anchor while a living ship shields it.
#include "crew.h"
#include "hit_predict.h"
#include "memory.h"
#include "wasted_fire.h"

namespace crew {
namespace {
// --- The game's hit-acceptance overrides (slot 16) the plugin may ask ---
constexpr std::size_t kSlotAccept=16,kSlotFalse12=12;
constexpr std::uintptr_t kAcceptYes=0x54AA40,kAcceptMonster=0x4BC8A0,kAcceptTimeShip=0x3EE2F0,kAcceptCarrier=0x4F7190,
                         kAcceptAnchor=0x3F6970,kAcceptGenerator=0x401200,kAcceptFortressPart=0x4D1790,kConstFalse=0x266BE0;
constexpr std::size_t kObjFlags=0x380;
constexpr std::uint32_t kInvincible=0x1,kNoHitFilter=0x800;
constexpr std::size_t kCarrierHatch=0xD84,kCarrierDamageOpen=0xD85,kCarrierCloseLeft=0xD90;
constexpr std::size_t kAnchorShip=0x1160,kAnchorShipCtrl=0x1168,kAnchorFree=0x1106,kAnchorFlags=0x10A8;
constexpr float kFrameMs=1000.0f/60.0f;
// Each override's first bytes (the version's gate: a different EDF.dll asks none of them).
struct AcceptSig { std::uintptr_t rva; unsigned char bytes[8]; };
const AcceptSig kAcceptSigs[]={
    {kAcceptYes,{0xB0,0x01,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC}},
    {kAcceptMonster,{0xB0,0x01,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC}},
    {kAcceptTimeShip,{0x32,0xC0,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC}},
    {kAcceptCarrier,{0x40,0x55,0x48,0x83,0xEC,0x50,0x80,0xB9}},
    {kAcceptAnchor,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48}},
    {kAcceptGenerator,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C}},
    {kAcceptFortressPart,{0x48,0x83,0xEC,0x28,0xF6,0x81,0x80,0x03}},
    {kConstFalse,{0x32,0xC0,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC}},
};
constexpr unsigned kBodyObject=0x108260;   // body id -> its scene object (jet_hooks.cpp, shield.cpp)
using BodyObjectFn=const void*(__fastcall*)(std::uint32_t);
BodyObjectFn bodyObject=nullptr;   // image+kBodyObject once checked (tests/hit_accept_native_test.cpp stands one in)
const unsigned char kBodyObjectSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x05};
int acceptOk=0;   // 0 unchecked, 1 the overrides are the known code, -1 not
bool AcceptOk() noexcept {
    if(!acceptOk) {
        bool ok=Matches(kBodyObject,kBodyObjectSig,sizeof(kBodyObjectSig));
        for(const auto& s:kAcceptSigs)ok=ok && Matches(s.rva,s.bytes,sizeof(s.bytes));
        acceptOk=ok ? 1 : -1;
        if(ok && !bodyObject)bodyObject=reinterpret_cast<BodyObjectFn>(image+kBodyObject);
        Log("WASTEDFIRE hit prediction: %s",ok ? "the hit-acceptance overrides are the known code" : "EDF.dll differs: feedback only");
    }
    return acceptOk>0;
}

// A one-hit GameDamageInfo as the overrides read it: type +0x20 (0: a round; 4 skips the generator's box), the hit point
// +0x30, the hits +0x78 (array of 0x30-byte entries: point +0, body +0x20) and their count +0x88, flags +0x60.
struct alignas(16) Gdi { unsigned char raw[0x100]; };
struct alignas(16) GdiHit { float at[4]; unsigned char pad[0x10]; std::uint32_t body; unsigned char pad2[0xC]; };
static_assert(sizeof(GdiHit)==0x30,"a GameDamageInfo hit entry");

enum class Kind : std::uint8_t { none, yes, no, carrier, anchor, generator, fortressPart };
Kind KindOf(const unsigned char* o) noexcept {
    const auto vt=At<const unsigned char* const*>(o,0);
    if(!Readable(vt,(kSlotAccept+1)*8))return Kind::none;
    const auto f=reinterpret_cast<std::uintptr_t>(vt[kSlotAccept])-reinterpret_cast<std::uintptr_t>(image);
    switch(f) {
    case kAcceptYes: case kAcceptMonster: return Kind::yes;
    case kAcceptTimeShip: return Kind::no;
    case kAcceptCarrier: return Kind::carrier;
    case kAcceptAnchor: return Kind::anchor;
    case kAcceptGenerator: return Kind::generator;
    case kAcceptFortressPart: return Kind::fortressPart;
    default: return Kind::none;
    }
}
// The bytes of `o` the plan reads for kind `k`.
std::size_t ReachOf(Kind k) noexcept {
    switch(k) {
    case Kind::carrier: return kCarrierCloseLeft+4;
    case Kind::anchor: return kAnchorShipCtrl+8;
    case Kind::generator: return 0xCA8;
    default: return kObjFlags+4;
    }
}
// Whether `o`'s slot 12 is the stock const false (what the anchor and fortress part overrides call).
bool Slot12False(const unsigned char* o) noexcept {
    const auto vt=At<const unsigned char* const*>(o,0);
    return Readable(vt,(kSlotFalse12+1)*8) && vt[kSlotFalse12]==image+kConstFalse;
}
// The ship an anchor hangs from, alive (its weak reference's uses), else nullptr.
const unsigned char* AnchorShip(const unsigned char* o) noexcept {
    const auto ctrl=At<const unsigned char*>(o,kAnchorShipCtrl);
    if(!ctrl || !Readable(ctrl,0x10) || At<std::int32_t>(ctrl,8)<=0)return nullptr;
    const auto ship=At<const unsigned char*>(o,kAnchorShip);
    return Readable(ship,kObjFlags+4) ? ship : nullptr;
}

// --- The target's window now: open or not, and the time it stays open (ms, <0 not readable) ---
struct WindowNow { bool known,open; float leftMs; };
WindowNow WindowOf(const unsigned char* o,Kind k) noexcept {
    const std::uint32_t flags=At<std::uint32_t>(o,kObjFlags);
    if(flags&kInvincible)return WindowNow{true,false,-1.0f};
    switch(k) {
    case Kind::carrier: {
        if(flags&kNoHitFilter)return WindowNow{true,true,-1.0f};
        const bool open=o[kCarrierDamageOpen]!=0;
        const float left=o[kCarrierHatch] ? static_cast<float>(At<std::int32_t>(o,kCarrierCloseLeft))*kFrameMs : -1.0f;
        return WindowNow{true,open,open ? left : -1.0f};
    }
    case Kind::anchor: {
        const unsigned char* ship=AnchorShip(o);
        const bool shut=(ship && !o[kAnchorFree]) || (At<std::uint32_t>(o,kAnchorFlags)&4) ||
                        (ship && ((At<std::uint32_t>(ship,kObjFlags)&kInvincible) || !Slot12False(ship)));
        return WindowNow{true,!shut,-1.0f};
    }
    default: return WindowNow{true,true,-1.0f};   // only the invincible flag opens and shuts it
    }
}

// Whether the game would take a hit on `o` at `at` on body `body` (the override asked; the window checked first).
bool Accepts(unsigned char* o,Kind k,const float* at,std::uint32_t body) noexcept {
    if(k==Kind::no)return !(At<std::uint32_t>(o,kObjFlags)&kNoHitFilter) ? false : true;
    if(k==Kind::yes || k==Kind::none)return true;
    if(At<std::uint32_t>(o,kObjFlags)&kNoHitFilter)return true;
    if(k==Kind::fortressPart)return Slot12False(o) && !(At<std::uint32_t>(o,kObjFlags)&kInvincible);
    if(k==Kind::anchor) {   // its override calls the ship's slot 12: asked only when that is the stock const false
        const unsigned char* ship=AnchorShip(o);
        if(ship && !Slot12False(ship))return false;
    }
    Gdi g{};
    GdiHit h{};
    std::memcpy(h.at,at,12);h.at[3]=1.0f;h.body=body;
    GdiHit* hits=&h;
    std::memcpy(g.raw+0x30,h.at,16);
    std::memcpy(g.raw+0x78,&hits,8);
    const std::uint64_t one=1;
    std::memcpy(g.raw+0x88,&one,8);
    __try {
        using AcceptFn=bool(__fastcall*)(void*,void*);
        return reinterpret_cast<AcceptFn>(image+(k==Kind::carrier ? kAcceptCarrier : k==Kind::anchor ? kAcceptAnchor : kAcceptGenerator))(o,&g);
    } __except(EXCEPTION_EXECUTE_HANDLER){return true;}   // not answered: the feedback decides
}

// --- The watch: each target's window and motion, each shooter-target pair's last plan ---
struct Watched { const void* target; std::uint64_t at,loggedAt; float pos[3],vel[3]; hitpredict::Window w; };
constexpr std::uint64_t kLogMs=3000;
constexpr int kWatched=64;
Watched watched[kWatched]{};
Watched& Watch(const void* target) noexcept {
    Watched* oldest=&watched[0];
    for(Watched& x:watched) {
        if(x.target==target)return x;
        if(x.at<oldest->at)oldest=&x;
    }
    *oldest=Watched{target,0,0,{},{},{}};
    return *oldest;
}
struct Plan { const void* shooter; const void* target; std::uint64_t at; hitpredict::Verdict verdict; float aim[3]; bool aimSet;
              float from[3],speed,gravity; };
constexpr int kPlans=128;
constexpr std::uint64_t kPlanMs=150;   // a pair's plan holds this long (rays a pair at most ~7 a second)
Plan plans[kPlans]{};
// Whether a layer-11 ray has met a body of an object yet this mission: until then a miss says nothing about the target.
bool rayMeetsObjects=false;

wastedfire::Book book{};
SRWLOCK lock=SRWLOCK_INIT;

// The target's lock points (its own registry entries), at most `max`; its root when it has none.
struct Points { const void* target; float p[4][3]; int n; };
void SeePoint(void* ctx,const void* object,const float* aim) {
    auto& s=*static_cast<Points*>(ctx);
    if(object==s.target && s.n<4){std::memcpy(s.p[s.n],aim,12);++s.n;}
}

// The plan for `shooter` firing from `from` (rounds of `speed` m/s, `gravity` m/s^2; 0: unknown, taken as a straight line)
// at `target`: fire / wait / skip and the point to aim at (its lock point whose predicted hit the game would take).
Plan Assess(const void* shooter,const void* target,const float* from,float speed,float gravity) noexcept {
    Plan p{shooter,target,0,hitpredict::Verdict::fire,{},false};
    std::memcpy(p.from,from,12);p.speed=speed;p.gravity=gravity;
    auto o=static_cast<unsigned char*>(const_cast<void*>(target));
    const Kind k=KindOf(o);
    if(!Readable(o,ReachOf(k)))return p;
    const std::uint64_t ms=GameMs();
    Watched& w=Watch(target);
    const float* root=reinterpret_cast<const float*>(o+kPosition);
    if(w.at && ms>w.at)for(int i=0;i<3;++i)w.vel[i]=(root[i]-w.pos[i])*1000.0f/static_cast<float>(ms-w.at);
    std::memcpy(w.pos,root,12);w.at=ms;
    const WindowNow now=WindowOf(o,k);
    hitpredict::Observe(w.w,now.open,ms);
    Points pts{target,{},0};
    VisitLockPoints(&SeePoint,&pts);
    if(!pts.n){std::memcpy(pts.p[0],root,12);pts.n=1;}
    // The first lock point whose predicted hit the game takes: its aim point; none: the first one's, not accepted.
    bool accepted=false;float flightMs=0.0f;
    int rays=0,met=0,onTarget=0;std::uint32_t lastBody=0xFFFFFF;   // for the log line
    for(int i=0;i<pts.n && !accepted;++i) {
        float aim[3];
        const float t=hitpredict::Intercept(from,pts.p[i],w.vel,speed,gravity,aim);
        if(i==0){std::memcpy(p.aim,aim,12);flightMs=t*1000.0f;}
        bool ok=true;
        if(k==Kind::carrier || k==Kind::anchor || k==Kind::generator || k==Kind::no || k==Kind::fortressPart) {
            float d[3]={aim[0]-from[0],aim[1]-from[1],aim[2]-from[2]};
            const float len=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
            if(len<1.0f)continue;
            const float past=(len+20.0f)/len;   // on through the lock point (inside the body) 20 m
            const float end[3]={from[0]+d[0]*past,from[1]+d[1]*past,from[2]+d[2]*past};
            float hit[3];std::uint32_t body=0xFFFFFF;
            const float r=ObjectRay(from,end,hit,&body);
            const void* by=nullptr;
            if(r>=0.0f) {
                __try { by=bodyObject ? bodyObject(body) : nullptr; }
                __except(EXCEPTION_EXECUTE_HANDLER){by=nullptr;}
                if(by)rayMeetsObjects=true;
            }
            ++rays;met+=by!=nullptr;onTarget+=by==target;lastBody=body;
            if(by==target)ok=Accepts(o,k,hit,body);
            else if(by)ok=false;                                   // another body first: this one is not where the round goes
            else ok=rayMeetsObjects ? false : Accepts(o,k,aim,0xFFFFFF) || k==Kind::carrier;   // no ray answer: the state only
            if(ok){std::memcpy(p.aim,aim,12);flightMs=t*1000.0f;}
        }
        accepted=ok;
    }
    p.aimSet=accepted;
    p.verdict=hitpredict::Decide(&w.w,ms,flightMs,accepted,now.leftMs);
    // One line a target every kLogMs: what the ray met and what the game's override said (the evidence a player's log
    // gives of the prediction: the layer-11 ray meeting these bodies, their ids the override takes).
    if(k!=Kind::none && k!=Kind::yes && ms-w.loggedAt>=kLogMs) {
        w.loggedAt=ms;
        static const char* const kNames[]={"none","yes","never","ship","anchor","generator","fortress part"};
        Log("WASTEDFIRE plan %p -> %p (%s): window %s left %.0f ms, %d point(s), rays %d met %d on it %d (body %u), "
            "accepted %d, flight %.0f ms: %s",shooter,target,kNames[static_cast<int>(k)],now.open ? "open" : "shut",
            static_cast<double>(now.leftMs),pts.n,rays,met,onTarget,lastBody,accepted,static_cast<double>(flightMs),
            p.verdict==hitpredict::Verdict::fire ? "fire" : p.verdict==hitpredict::Verdict::wait ? "wait" : "skip");
    }
    return p;
}

const Plan& PlanFor(const void* shooter,const void* target,const float* from,float speed,float gravity) noexcept {
    const std::uint64_t ms=GameMs();
    Plan* oldest=&plans[0];
    for(Plan& p:plans) {
        if(p.shooter==shooter && p.target==target && p.speed==speed && p.gravity==gravity &&
           std::memcmp(p.from,from,12)==0 && ms-p.at<kPlanMs)return p;
        if(p.at<oldest->at)oldest=&p;
    }
    *oldest=Assess(shooter,target,from,speed,gravity);
    oldest->at=ms;
    return *oldest;
}
// The shooter's eye: a soldier's 1.5 m over its feet, a vehicle's 2 m over its root.
void EyeOf(const void* shooter,float* eye) noexcept {
    const auto s=static_cast<const unsigned char*>(shooter);
    const float* p=reinterpret_cast<const float*>(s+kPosition);
    eye[0]=p[0];eye[1]=p[1]+(IsSoldierClass(s) ? 1.5f : 2.0f);eye[2]=p[2];
}
bool Feedback(const void* shooter,const void* target) noexcept {
    const ULONGLONG ms=GameMs();
    AcquireSRWLockShared(&lock);
    const bool wasted=wastedfire::Wasted(book,shooter,target,ms);
    ReleaseSRWLockShared(&lock);
    return wasted;
}
// The plan (from the shooter's own eye when `from` is null).
hitpredict::Verdict VerdictOf(const void* shooter,const void* target,const float* from,float speed,float gravity,float* aim) noexcept {
    if(!shooter || !target || !Readable(shooter,kPosition+12) || !Readable(target,kObjFlags+4))return hitpredict::Verdict::fire;
    const auto o=static_cast<const unsigned char*>(target);
    const Kind k=AcceptOk() ? KindOf(o) : Kind::none;
    // An ordinary enemy (its override always takes a hit, or one the plugin does not ask): only the invincible flag.
    if(k==Kind::none || k==Kind::yes)
        return At<std::uint32_t>(o,kObjFlags)&kInvincible ? hitpredict::Verdict::skip : hitpredict::Verdict::fire;
    float eye[3];
    if(!from){EyeOf(shooter,eye);from=eye;}
    const Plan& p=PlanFor(shooter,target,from,speed,gravity);
    if(aim && p.aimSet)std::memcpy(aim,p.aim,12);
    return p.verdict;
}
}  // namespace

void WastedFireHit(const void* shooter,const void* target,bool hurt) noexcept {
    const ULONGLONG ms=GameMs();
    AcquireSRWLockExclusive(&lock);
    const bool held=wastedfire::Hit(book,shooter,target,hurt,ms);
    ReleaseSRWLockExclusive(&lock);
    // One line a hold (a few a minute at most per pair): the evidence in a player's log that the AI held its fire.
    if(held)Log("WASTEDFIRE %p: %d hits on %p hurt nothing: its fire at it held %.0f s",shooter,wastedfire::kWastedHits,target,
                static_cast<double>(wastedfire::kWastedHoldMs)*0.001);
}

bool WastedFire(const void* shooter,const void* target) noexcept {
    if(!target)return false;
    __try {
        if(VerdictOf(shooter,target,nullptr,0.0f,0.0f,nullptr)==hitpredict::Verdict::skip)return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return Feedback(shooter,target);
}

bool HoldFire(const void* shooter,const void* target,const float* from,float speed,float gravity,float* aim) noexcept {
    if(!target)return false;
    __try {
        if(VerdictOf(shooter,target,from,speed,gravity,aim)!=hitpredict::Verdict::fire)return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return Feedback(shooter,target);
}

void ResetWastedFire() noexcept {
    AcquireSRWLockExclusive(&lock);
    book=wastedfire::Book{};
    ReleaseSRWLockExclusive(&lock);
    for(auto& w:watched)w=Watched{};
    for(auto& p:plans)p=Plan{};
    rayMeetsObjects=false;
}
}  // namespace crew
