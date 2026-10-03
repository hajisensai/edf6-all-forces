// The teleportation ships' portal laser (传送舰激光大招): while a submarine carrier (subcarrier.cpp) is out, every
// live e508 teleportation ship (UfoCarrier508, app:/object/e508_carrier.sgo) now and then charges its portal for
// kChargeMs, a red aim light from its hatch to its target, then fires one wide beam along the aim it locked
// kLockMs before. Shooting the ship for CarrierLaserBreak of its HP during the charge (or killing it) breaks it off.
//
// Both beams are DemoIndirectFire objects (the missions' satellite laser class, docs/carrier-laser-re.md) from
// tools/make_jets.py's EDF6VC_PORTAL_SIGHT.SGO / EDF6VC_PORTAL_LASER.SGO, made with the game's CreateObject:
//   DemoIndirectFire (vtable 0x17D4B20, ctor 0x5B55F0, Update slot 5 0x5B5C50): its IndirectFireControl at +0x170
//   is opened by the ctor (aim +0x20 = its own position) and stepped by Update (0x2B95A0), which deletes the
//   object (0x118A1B0) once the IFC is done (0x2B7B90). (H)
//   IFC step (0x2B95A0): each round, with +0x2F9 set the start is +0x300 (else 0x2B43A0's sky point from param
//   #0/#1); the round flies from the start to +0x20 plus a random offset within the spread +0x224 (0 here), a
//   straight line while +0x2F8 is 0 (lasers). Team +0xD0 is refreshed each step from the owner (weak +0x78/+0x80,
//   its +0x314). (H)
//   0x2B8390 (ifc, &weak{object,ctrl}): owner; takes its own weak reference (lock inc +0xC). (H)
//   0x2B82E0 (ifc, float): damage +0xDC (the ctor wrote factor x indirect_fire_damage, which the SGOs make 0). (H)
// The plugin makes the ship their owner (team enemy, kills credited to it, its own hull not hit), sets the start
// on the ship's hatch and the aim on the target each frame, and deletes the sight when the charge ends.
// UfoCarrier508 (vtable 0x17C9DC0 -> UfoCarrier 0x17C9930 -> GameObjectBase): HP max +0x2F4 / HP +0x2F8, which
// UfoCarrier's own code reads and writes (0x4F0FB0, 0x4F6D00, 0x4F81D0, 0x4F94A4). (H)
// The hatch: E508_CARRIER.MRAB's hatch_A..H ring sits 16.8 m under the body origin (radius ~35 m), the portal
// (in_ring) round the vertical axis; the beams start kHatchBelow under the origin along -up. (M)
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstdint>

namespace crew {
namespace {
constexpr unsigned kShipVtable=0x17C9DC0;          // UfoCarrier508
constexpr unsigned kDemoVtable=0x17D4B20,kDemoUpdate=0x5B5C50;
constexpr std::size_t kDemoUpdateSlot=5,kDemoIfc=0x170;
constexpr unsigned kPreload=0x7A3780,kCreateObject=0x11945E0,kDelete=0x118A1B0,kInitParamVtable=0x1762068;
constexpr std::size_t kPreloadMgr=0x20B29A8,kObjectMgr=0x20B2958;
constexpr unsigned kIfcOwner=0x2B8390,kIfcDamage=0x2B82E0,kIfcStepFromPoint=0x2B970D,kIfcStepStart=0x2B9756;
constexpr std::size_t kIfcAim=0x20,kIfcFromPoint=0x2F9,kIfcStart=0x300;
constexpr std::size_t kHpMax=0x2F4,kHp=0x2F8,kObjFlags=0x18;
constexpr unsigned char kObjDeleted=4;
constexpr float kHatchBelow=20.0f;     // metres under the ship's origin the beams start (the portal's mouth)
constexpr float kPlayerRange=400.0f;   // the player within this of the ship is its target, else the carrier's deck
constexpr float kShipRange=1500.0f;    // ships further than this from the carrier never charge
constexpr ULONGLONG kChargeMs=4000,kLockMs=1000;   // the charge; the aim is held for its last kLockMs
constexpr ULONGLONG kCoolMinMs=30000,kCoolMaxMs=45000,kFirstMs=12000;   // the gap between a ship's charges; the first after kFirstMs+
constexpr ULONGLONG kGapMs=6000;       // after any charge ends, no ship starts one for this long
constexpr ULONGLONG kStaleMs=1500;     // a ship not seen for this long is forgotten
constexpr int kMaxShips=8;
const wchar_t kSightSgo[]=L"app:/object/edf6vc_portal_sight.sgo";
const wchar_t kLaserSgo[]=L"app:/object/edf6vc_portal_laser.sgo";
const wchar_t* const kFiles[]={L"\\Mods\\OBJECT\\EDF6VC_PORTAL_SIGHT.SGO",L"\\Mods\\OBJECT\\EDF6VC_PORTAL_LASER.SGO"};

const unsigned char kDeleteSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48};
const unsigned char kPreloadSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
const unsigned char kCreateObjectSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9};
const unsigned char kIfcOwnerSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48};
const unsigned char kIfcDamageSig[]={0xF3,0x0F,0x11,0x89,0xDC,0x00,0x00,0x00,0xC3,0xCC,0xCC,0xCC};
// 0x2B970D cmp byte [r14+0x2F9],0 / jne; 0x2B9756 movups xmm0,[r14+0x300]: the step's start-point choice.
const unsigned char kStepFromPointSig[]={0x41,0x80,0xBE,0xF9,0x02,0x00,0x00,0x00,0x75,0x3F};
const unsigned char kStepStartSig[]={0x41,0x0F,0x10,0x86,0x00,0x03,0x00,0x00};
// DemoIndirectFire Update; its ctor's `lea rsi,[rdi+0x170]` (0x5B56AC) before the IFC ctor.
const unsigned char kDemoUpdateSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
const unsigned char kDemoIfcSig[]={0x48,0x8D,0xB7,0x70,0x01,0x00,0x00,0x48,0x8B,0xCE};

struct alignas(16) InitParam { const void* vtable; unsigned char rest[0x28]; };
struct Weak { const void* object; const void* ctrl; };
using PreloadFn=void(*)(void*,const wchar_t*,std::int32_t,std::int32_t);
using CreateObjectFn=unsigned char*(*)(void*,const float*,const wchar_t*,InitParam*);
using DeleteFn=void(*)(void*);
using OwnerFn=void(*)(void*,const Weak*);
using DamageFn=void(*)(void*,float);

enum class Phase { idle, charging };
struct Ship {
    const unsigned char* ship; const void* ctrl;
    ULONGLONG seen,nextAt,phaseAt;
    Phase phase;
    float hpAtCharge;
    unsigned char* sight; const void* sightCtrl;
    float aim[3];                       // the beams' end: follows the target until kLockMs before the shot
    bool atPlayer;
};
Ship ships[kMaxShips]{};
bool sigOk=false,preloaded=false,broken=false;
ULONGLONG tickAt=0,quietUntil=0;
std::uint32_t seed=0;

std::uint32_t Rand() noexcept {
    if(!seed)seed=static_cast<std::uint32_t>(GetTickCount64())|1u;
    seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
    return seed;
}
ULONGLONG Cooldown() noexcept { return kCoolMinMs+Rand()%(kCoolMaxMs-kCoolMinMs+1); }

bool FilesThere() noexcept {
    wchar_t dir[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,dir,MAX_PATH);
    wchar_t* slash=n && n<MAX_PATH ? wcsrchr(dir,L'\\') : nullptr;
    if(!slash)return false;
    *slash=0;
    for(const auto file:kFiles) {
        wchar_t path[MAX_PATH];
        if(wcscpy_s(path,dir)!=0 || wcscat_s(path,file)!=0)return false;
        if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES){Log("LASER %ls not installed (python tools/make_jets.py): off",file);return false;}
    }
    return true;
}

float Dist(const float* a,const float* b) noexcept {
    const float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};
    return std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
}

// The portal's mouth: kHatchBelow along the ship's -up from its origin.
void Hatch(const unsigned char* s,float* out) noexcept {
    const float* up=reinterpret_cast<const float*>(s+kMatrix+0x10);
    const float* p=reinterpret_cast<const float*>(s+kPosition);
    for(int i=0;i<3;++i)out[i]=p[i]-up[i]*kHatchBelow;
}

// The ship's target: the player within kPlayerRange of `from`, else the carrier deck nearest to it. Read only.
bool Target(const float* from,float* out,bool* atPlayer) noexcept {
    const unsigned char* human=PlayerHuman();
    if(human) {
        const float* p=reinterpret_cast<const float*>(human+kPosition);
        const float chest[3]={p[0],p[1]+1.2f,p[2]};
        if(std::isfinite(chest[0]) && std::isfinite(chest[1]) && std::isfinite(chest[2]) && Dist(chest,from)<=kPlayerRange) {
            std::memcpy(out,chest,12);*atPlayer=true;return true;
        }
    }
    *atPlayer=false;
    return SubDeck(from,out);
}

bool ObjectLive(const unsigned char* o,const void* ctrl,unsigned vtable) noexcept {
    return o && Readable(o,kTeam+4) && At<const void*>(o,0)==image+vtable && At<const void*>(o,kSelfCtrl)==ctrl &&
           !(o[kObjFlags]&kObjDeleted);
}

int MakeFault(const EXCEPTION_POINTERS* e) noexcept {
    const auto r=e->ExceptionRecord;
    Log("LASER the game faulted building a beam (%08lX at EDF+%llX): the laser is off until the game restarts",r->ExceptionCode,
        static_cast<unsigned long long>(static_cast<const unsigned char*>(r->ExceptionAddress)-image));
    broken=true;preloaded=false;
    return EXCEPTION_EXECUTE_HANDLER;
}
unsigned char* Create(const wchar_t* sgo,const float* m) noexcept {
    InitParam param{image+kInitParamVtable,{}};
    __try { return reinterpret_cast<CreateObjectFn>(image+kCreateObject)(At<void*>(image,kObjectMgr),m,sgo,&param); }
    __except(MakeFault(GetExceptionInformation())) { return nullptr; }
}

// A beam object from `sgo`, owned by ship `s`, from `start` to `aim` with `damage`: the object, or nullptr.
unsigned char* Beam(const wchar_t* sgo,const unsigned char* s,const float* start,const float* aim,float damage) noexcept {
    if(!At<void*>(image,kObjectMgr))return nullptr;
    alignas(16) const float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, aim[0],aim[1],aim[2],1};
    unsigned char* const o=Create(sgo,m);
    if(!o)return nullptr;
    __try {
        if(At<const void*>(o,0)!=image+kDemoVtable) {
            Log("LASER %ls: %p is no DemoIndirectFire: deleted",sgo,o);
            reinterpret_cast<DeleteFn>(image+kDelete)(o);
            return nullptr;
        }
        unsigned char* const ifc=o+kDemoIfc;
        const Weak owner{At<const void*>(s,kSelf),At<const void*>(s,kSelfCtrl)};
        reinterpret_cast<OwnerFn>(image+kIfcOwner)(ifc,&owner);
        reinterpret_cast<DamageFn>(image+kIfcDamage)(ifc,damage);
        ifc[kIfcFromPoint]=1;
        const float st[4]={start[0],start[1],start[2],1.0f},am[4]={aim[0],aim[1],aim[2],1.0f};
        std::memcpy(ifc+kIfcStart,st,16);std::memcpy(ifc+kIfcAim,am,16);
        return o;
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("LASER %ls: fault setting up %p",sgo,o);return nullptr;}
}

void Steer(unsigned char* o,const void* ctrl,const float* start,const float* aim) noexcept {
    if(!ObjectLive(o,ctrl,kDemoVtable))return;
    unsigned char* const ifc=o+kDemoIfc;
    const float st[4]={start[0],start[1],start[2],1.0f},am[4]={aim[0],aim[1],aim[2],1.0f};
    std::memcpy(ifc+kIfcStart,st,16);std::memcpy(ifc+kIfcAim,am,16);
}

void DropSight(Ship& s) noexcept {
    unsigned char* const o=s.sight;
    s.sight=nullptr;
    __try {
        if(ObjectLive(o,s.sightCtrl,kDemoVtable))reinterpret_cast<DeleteFn>(image+kDelete)(o);
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("LASER sight %p: fault deleting",o);}
}

void EndCharge(Ship& s,ULONGLONG ms) noexcept {
    DropSight(s);
    s.phase=Phase::idle;s.nextAt=ms+Cooldown();
    quietUntil=ms+kGapMs;
}

void Interrupt(Ship& s,ULONGLONG ms,const char* why) noexcept {
    Log("LASER interrupted ship=%p (%s) after %.1fs aim=(%.0f,%.0f,%.0f)",s.ship,why,static_cast<double>(ms-s.phaseAt)/1000.0,
        s.aim[0],s.aim[1],s.aim[2]);
    EndCharge(s,ms);
}

struct Seen { const unsigned char* list[kMaxShips]; int n; };
void SeeShip(void* ctx,const void* object,const float*) noexcept {
    auto& seen=*static_cast<Seen*>(ctx);
    const auto o=static_cast<const unsigned char*>(object);
    if(At<const void*>(o,0)!=image+kShipVtable || seen.n>=kMaxShips)return;
    for(int i=0;i<seen.n;++i)if(seen.list[i]==o)return;   // one lock point per part: the ship once
    seen.list[seen.n++]=o;
}

Ship* Slot(const unsigned char* o,ULONGLONG ms) noexcept {
    Ship* free=nullptr;
    for(auto& s:ships) {
        if(s.ship==o && s.ctrl==At<const void*>(o,kSelfCtrl))return &s;
        if(!free && !s.ship)free=&s;
    }
    if(!free)return nullptr;
    *free=Ship{};free->ship=o;free->ctrl=At<const void*>(o,kSelfCtrl);
    // Staggered: the first charge 12-27 s after the ship is first seen, ships kept apart by kGapMs anyway.
    free->nextAt=ms+kFirstMs+Rand()%(kCoolMaxMs-kCoolMinMs+1);
    Log("LASER ship=%p seen at (%.0f,%.0f,%.0f) hp=%.0f/%.0f: first charge in %.0fs",o,At<float>(o,kPosition),At<float>(o,kPosition+4),
        At<float>(o,kPosition+8),At<float>(o,kHp),At<float>(o,kHpMax),static_cast<double>(free->nextAt-ms)/1000.0);
    return free;
}

void Fire(Ship& s,const float* hatch,ULONGLONG ms) noexcept {
    const float damage=cfg.carrierLaserDamage>0.0f ? cfg.carrierLaserDamage : 0.0f;
    unsigned char* const o=Beam(kLaserSgo,s.ship,hatch,s.aim,damage);
    Log("LASER fire ship=%p from (%.0f,%.0f,%.0f) to (%.0f,%.0f,%.0f)%s damage=%.0f beam=%p",s.ship,hatch[0],hatch[1],hatch[2],
        s.aim[0],s.aim[1],s.aim[2],s.atPlayer ? " (player)" : " (carrier deck)",damage,o);
    EndCharge(s,ms);
}

void Charge(Ship& s,const float* hatch,ULONGLONG ms) noexcept {
    float aim[3];bool atPlayer=false;
    if(!Target(hatch,aim,&atPlayer))return;
    std::memcpy(s.aim,aim,12);s.atPlayer=atPlayer;
    s.sight=Beam(kSightSgo,s.ship,hatch,aim,0.0f);
    if(!s.sight){s.nextAt=ms+Cooldown();return;}
    s.sightCtrl=At<const void*>(s.sight,kSelfCtrl);
    s.phase=Phase::charging;s.phaseAt=ms;s.hpAtCharge=At<float>(s.ship,kHp);
    Log("LASER charge ship=%p hatch=(%.0f,%.0f,%.0f) target=(%.0f,%.0f,%.0f)%s hp=%.0f/%.0f break=%.0f",s.ship,hatch[0],hatch[1],hatch[2],
        aim[0],aim[1],aim[2],atPlayer ? " (player)" : " (carrier deck)",s.hpAtCharge,At<float>(s.ship,kHpMax),
        At<float>(s.ship,kHpMax)*cfg.carrierLaserBreak);
}

// One charging ship's frame: interrupted, aim followed (until the lock), fired at kChargeMs.
void Charging(Ship& s,const float* hatch,ULONGLONG ms) noexcept {
    const float hp=At<float>(s.ship,kHp),hpMax=At<float>(s.ship,kHpMax);
    if(s.ship[kDead] || hp<=0.0f){Interrupt(s,ms,"shot down");return;}
    if(s.hpAtCharge-hp>=hpMax*cfg.carrierLaserBreak){Interrupt(s,ms,"took too much damage");return;}
    if(ms-s.phaseAt<kChargeMs-kLockMs) {
        float aim[3];bool atPlayer=false;
        if(Target(hatch,aim,&atPlayer)){std::memcpy(s.aim,aim,12);s.atPlayer=atPlayer;}
    }
    if(ms-s.phaseAt>=kChargeMs){Fire(s,hatch,ms);return;}
    Steer(s.sight,s.sightCtrl,hatch,s.aim);
}

void Tick(const unsigned char* sub) noexcept {
    const ULONGLONG ms=GameMs();
    Seen seen{{},0};
    VisitEnemiesOf(At<std::int32_t>(sub,kTeam),&SeeShip,&seen);
    const float* subPos=reinterpret_cast<const float*>(sub+kPosition);
    bool charging=false;
    for(int i=0;i<seen.n;++i) {
        Ship* s=Slot(seen.list[i],ms);
        if(s)s->seen=ms;
    }
    for(auto& s:ships) {
        if(!s.ship)continue;
        if(ms-s.seen>kStaleMs || !ObjectLive(s.ship,s.ctrl,kShipVtable)) {
            if(s.phase==Phase::charging)Interrupt(s,ms,"ship gone");
            DropSight(s);
            s=Ship{};
            continue;
        }
        float hatch[3];
        Hatch(s.ship,hatch);
        if(s.phase==Phase::charging){Charging(s,hatch,ms);charging=charging || s.phase==Phase::charging;}
    }
    if(charging || ms<quietUntil)return;
    for(auto& s:ships) {
        if(!s.ship || s.phase!=Phase::idle || ms<s.nextAt)continue;
        if(Dist(reinterpret_cast<const float*>(s.ship+kPosition),subPos)>kShipRange)continue;
        float hatch[3];
        Hatch(s.ship,hatch);
        Charge(s,hatch,ms);
        if(s.phase==Phase::charging)return;   // one ship at a time
    }
}
}  // namespace

void PreloadLaser() noexcept {
    __try {
        preloaded=false;
        for(auto& s:ships)s=Ship{};   // a new mission: the old objects are gone with the old one
        quietUntil=0;
        if(!sigOk || broken || !cfg.carrierLaser)return;
        const auto mgr=At<void*>(image,kPreloadMgr);
        preloaded=mgr && FilesThere();
        if(preloaded) {
            reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kSightSgo,2,-1);
            reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kLaserSgo,2,-1);
        }
        Log("LASER preload=%d",preloaded);
    } __except(EXCEPTION_EXECUTE_HANDLER){preloaded=false;}
}

void CarrierLaserFrame(const unsigned char* sub) noexcept {
    if(!sigOk || !preloaded || broken || !cfg.carrierLaser)return;
    __try {
        const ULONGLONG tick=GetTickCount64();
        if(tick-tickAt<10)return;   // once a frame of the carriers that call it
        tickAt=tick;
        Tick(sub);
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("LASER fault in the frame: off until the game restarts");broken=true;}
}

bool InstallLaser() noexcept {
    __try {
        sigOk=Matches(kPreload,kPreloadSig,sizeof(kPreloadSig)) && Matches(kCreateObject,kCreateObjectSig,sizeof(kCreateObjectSig)) &&
              Matches(kDelete,kDeleteSig,sizeof(kDeleteSig)) && Matches(kIfcOwner,kIfcOwnerSig,sizeof(kIfcOwnerSig)) &&
              Matches(kIfcDamage,kIfcDamageSig,sizeof(kIfcDamageSig)) &&
              Matches(kIfcStepFromPoint,kStepFromPointSig,sizeof(kStepFromPointSig)) &&
              Matches(kIfcStepStart,kStepStartSig,sizeof(kStepStartSig)) &&
              Matches(kDemoUpdate,kDemoUpdateSig,sizeof(kDemoUpdateSig)) && Matches(0x5B56AC,kDemoIfcSig,sizeof(kDemoIfcSig)) &&
              Readable(image+kDemoVtable,(kDemoUpdateSlot+1)*8) &&
              At<const unsigned char*>(image,kDemoVtable+kDemoUpdateSlot*8)==image+kDemoUpdate &&
              Readable(image+kShipVtable,8) && Readable(image+kInitParamVtable,8);
        Log("HOOK carrier laser=%d (%s)",sigOk,!sigOk ? "off: unexpected EDF.dll code" : cfg.carrierLaser ? "on" : "off in the ini");
        return sigOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
