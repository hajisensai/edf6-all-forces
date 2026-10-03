// The teleportation ships' portal laser (传送舰激光大招): while a submarine carrier (subcarrier.cpp) is out, every
// live e508 teleportation ship (UfoCarrier508, app:/object/e508_carrier.sgo) within kShipRange of it now and then
// flies over the carrier, stops kOverDeck above its deck over the bow, the middle or the stern (the spot nearest
// to it), charges its portal for kChargeMs with a red sight beam straight down from its core, then fires one wide
// beam straight down onto the deck. It keeps dropping its monsters meanwhile (the drops are its own state machine,
// untouched). Shooting the ship for CarrierLaserBreak of its HP during the charge (or killing it) breaks it off.
// Afterwards the ship is let go: a routed ship flies on along its route (its explorer only waited), an unrouted
// one stays hovering where it fired, which is what an unrouted 508 does anyway.
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
// on the ship's core and the aim straight under it each frame, and deletes the sight when the charge ends.
// UfoCarrier508 (vtable 0x17C9DC0 -> UfoCarrier 0x17C9930 -> GameObjectBase): HP max +0x2F4 / HP +0x2F8, which
// UfoCarrier's own code reads and writes (0x4F0FB0, 0x4F6D00, 0x4F81D0, 0x4F94A4). (H)
// E508_CARRIER.MRAB: the portal (in_ring) at the body origin round the vertical axis, its core catapult_A 5 m
// under it on the axis (kCoreBelow), the hatch_A..H ring 16.8 m under it (radius ~35 m); the hull ~44 m deep. (H)
//
// The flight (docs/carrier-laser-re.md §6): the ship flies itself. UfoCarrier's AI (slot 7, 0x4F6D70, run each
// frame before its Update) zeroes the wanted move +0xB40 and, on a route, points it at the route's next point;
// its Update (0x4F74D0) eases the velocity +0x5C0 toward it (x0.01 a frame) and moves the ship by it (m/frame)
// while +0xDF0 is 0. The plugin wraps slot 7 of UfoCarrier508's vtable: after the stock AI, a ship being sent
// over the carrier gets the game's own fly-to-point (0x4F2370, the one UfoCarrier's states 3/4 use), which sets
// +0xB40 toward the point at the given speed and turns the ship to it. The position is never written. (H/M)
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstdint>

namespace crew {
namespace {
constexpr unsigned kShipVtable=0x17C9DC0;          // UfoCarrier508
constexpr unsigned kShipAi=0x4F6D70,kFlyTo=0x4F2370;
constexpr std::size_t kShipAiSlot=7;
constexpr std::size_t kYawRate=0xB54,kMoveState=0xDF0,kRoute=0x4A8;
constexpr unsigned kDemoVtable=0x17D4B20,kDemoUpdate=0x5B5C50;
constexpr std::size_t kDemoUpdateSlot=5,kDemoIfc=0x170;
constexpr unsigned kPreload=0x7A3780,kCreateObject=0x11945E0,kDelete=0x118A1B0,kInitParamVtable=0x1762068;
constexpr std::size_t kPreloadMgr=0x20B29A8,kObjectMgr=0x20B2958;
constexpr unsigned kIfcOwner=0x2B8390,kIfcDamage=0x2B82E0,kIfcStepFromPoint=0x2B970D,kIfcStepStart=0x2B9756;
constexpr std::size_t kIfcAim=0x20,kIfcFromPoint=0x2F9,kIfcStart=0x300;
constexpr std::size_t kHpMax=0x2F4,kHp=0x2F8,kObjFlags=0x18;
constexpr unsigned char kObjDeleted=4;
constexpr float kHatchBelow=20.0f;     // metres under the ship's origin the fallback beams start (the hatch ring)
constexpr float kCoreBelow=5.0f;       // metres under the ship's origin its core (catapult_A) sits
constexpr float kPlayerRange=400.0f;   // fallback: the player within this of the ship is its target, else the deck
constexpr float kShipRange=1500.0f;    // ships further than this from the carrier never charge
// The carrier (subcarrier.cpp): deck kDeckTop over its origin along up; spots along its nose at kSpotZ, kOverDeck
// over the deck (its tower tops out 173 m over the deck at z -280, the ship hangs ~44 m under its origin).
constexpr float kDeckTop=13.25f+179.83f,kOverDeck=220.0f;
constexpr float kSpotZ[]={500.0f,120.0f,-520.0f};  // bow deck, middle (ahead of the turrets), stern (over the bay)
constexpr float kArrive=40.0f;         // horizontal and vertical metres from the spot that count as over it
constexpr float kFlySpeed=0.75f;       // m/frame (45 m/s) at most
constexpr float kFlyGain=0.005f;       // speed per metre left: with the 0.01 velocity ease, damped (~0.7)
constexpr float kLeadMax=150.0f;       // the carrier's own motion led by at most this
constexpr float kClimbFirst=40.0f,kClimbFar=300.0f;   // further below the spot than this and far off: climb first
constexpr ULONGLONG kChargeMs=12000,kLockMs=1000;   // the charge; the fallback aim is held for its last kLockMs
constexpr ULONGLONG kCoolMinMs=30000,kCoolMaxMs=45000,kFirstMs=12000;   // the gap between a ship's charges; the first after kFirstMs+
constexpr ULONGLONG kGapMs=6000;       // after any charge ends, no ship starts one for this long
constexpr ULONGLONG kStaleMs=1500;     // a ship or carrier not seen for this long is forgotten
constexpr ULONGLONG kMoveMaxMs=60000;  // not over the carrier by then: the fallback charge from where it is
constexpr ULONGLONG kAiDeadMs=2000;    // the AI wrapper silent this long on a moving ship: it never runs, fallback
constexpr int kMaxShips=8,kMaxCarriers=3;
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
// UfoCarrier AI 0x4F6D70: push rbx / sub rsp,0x60 / xor eax,eax / mov [rcx+0xB4C],1.0f / mov [rcx+0xB40],rax.
const unsigned char kShipAiSig[]={0x40,0x53,0x48,0x83,0xEC,0x60,0x33,0xC0,0x48,0xC7,0x81,0x4C,0x0B,0x00,0x00,0x00,0x00,0x80,0x3F,
                                  0x48,0x89,0x81,0x40,0x0B};
// 0x4F6DA9 mov rax,[rbx+0x4A8]: the AI reads the route explorer after the base AI.
const unsigned char kShipAiRouteSig[]={0x48,0x8B,0x83,0xA8,0x04,0x00,0x00};
// The fly-to 0x4F2370 (ship, const float* point, radius, speed) and its `lea rdi,[rbx+0xB40]` (0x4F2487).
const unsigned char kFlyToSig[]={0x48,0x8B,0xC4,0x57,0x48,0x81,0xEC,0xA0,0x00,0x00,0x00,0x44,0x0F,0x29,0x48,0xB8,0x45,0x0F,0x57,0xC9,
                                 0x44,0x0F,0x29,0x60};
const unsigned char kFlyToWishSig[]={0x48,0x8D,0xBB,0x40,0x0B,0x00,0x00};
// Update 0x4F76F5: mov eax,[rdi+0xDF0] / test eax,eax, then 0x4F76FF movups xmm0,[rdi+0xB40] /
// movups [rdi+0x5D0],xmm0: the wanted move is flown while +0xDF0 is 0.
const unsigned char kUpdateStateSig[]={0x8B,0x87,0xF0,0x0D,0x00,0x00,0x85,0xC0};
const unsigned char kUpdateWishSig[]={0x0F,0x10,0x87,0x40,0x0B,0x00,0x00,0x0F,0x11,0x87,0xD0,0x05,0x00,0x00};

struct alignas(16) InitParam { const void* vtable; unsigned char rest[0x28]; };
struct Weak { const void* object; const void* ctrl; };
using PreloadFn=void(*)(void*,const wchar_t*,std::int32_t,std::int32_t);
using CreateObjectFn=unsigned char*(*)(void*,const float*,const wchar_t*,InitParam*);
using DeleteFn=void(*)(void*);
using OwnerFn=void(*)(void*,const Weak*);
using DamageFn=void(*)(void*,float);
using ShipAiFn=void(__fastcall*)(void*,const void*);
using FlyToFn=void(*)(void*,const float*,float,float);

enum class Phase { idle, moving, charging };
struct Ship {
    const unsigned char* ship; const void* ctrl;
    ULONGLONG seen,nextAt,phaseAt,flownAt;   // flownAt: the AI wrapper's last fly-to (0: none yet)
    Phase phase;
    bool steer,down;                    // steer: fly it to the spot; down: this charge fires straight down
    int carrier; float spotZ;
    float hpAtCharge;
    unsigned char* sight; const void* sightCtrl;
    float aim[3];                       // the beams' end
    bool atPlayer;
};
struct Carrier { const unsigned char* sub; const void* ctrl; ULONGLONG seen; float pos[3],vel[3]; };   // vel: m/frame
Ship ships[kMaxShips]{};
Carrier carriers[kMaxCarriers]{};
bool sigOk=false,flyOk=false,preloaded=false,broken=false,aiDead=false;
ShipAiFn nextShipAi=nullptr;
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
float Flat(const float* a,const float* b) noexcept {
    const float dx=a[0]-b[0],dz=a[2]-b[2];
    return std::sqrt(dx*dx+dz*dz);
}

// kBelow along the ship's -up from its origin: the hatch ring (fallback) or the core.
void Under(const unsigned char* s,float below,float* out) noexcept {
    const float* up=reinterpret_cast<const float*>(s+kMatrix+0x10);
    const float* p=reinterpret_cast<const float*>(s+kPosition);
    for(int i=0;i<3;++i)out[i]=p[i]-up[i]*below;
}

// Fallback target: the player within kPlayerRange of `from`, else the carrier deck nearest to it. Read only.
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

// Carriers: each one that calls CarrierLaserFrame this frame, its position and velocity.
bool CarrierLive(const Carrier& c,ULONGLONG ms) noexcept {
    return c.sub && ms-c.seen<=kStaleMs && Readable(c.sub,kTeam+4) && At<const void*>(c.sub,kSelfCtrl)==c.ctrl &&
           !(c.sub[kObjFlags]&kObjDeleted);
}
void SeeCarrier(const unsigned char* sub,ULONGLONG ms) noexcept {
    const void* const ctrl=At<const void*>(sub,kSelfCtrl);
    const float* p=reinterpret_cast<const float*>(sub+kPosition);
    Carrier* slot=nullptr;
    for(auto& c:carriers) {
        if(c.sub==sub && c.ctrl==ctrl){slot=&c;break;}
        if(!slot && (!c.sub || ms-c.seen>kStaleMs))slot=&c;
    }
    if(!slot)return;
    if(slot->sub==sub && slot->ctrl==ctrl) {
        const ULONGLONG dt=ms-slot->seen;
        if(dt==0)return;
        const float frames=static_cast<float>(dt)/(1000.0f/60.0f);
        for(int i=0;i<3;++i)slot->vel[i]=slot->vel[i]*0.8f+(p[i]-slot->pos[i])/frames*0.2f;
    } else *slot=Carrier{sub,ctrl,0,{},{}};
    std::memcpy(slot->pos,p,12);
    slot->seen=ms;
}

// A spot over carrier `c`: kOverDeck over its deck at `z` along its nose, centred across it.
void Spot(const Carrier& c,float z,float* out) noexcept {
    const float* m=reinterpret_cast<const float*>(c.sub+kMatrix);
    const float* p=reinterpret_cast<const float*>(c.sub+kPosition);
    for(int i=0;i<3;++i)out[i]=p[i]+m[4+i]*(kDeckTop+kOverDeck)+m[8+i]*z;
}

// Straight down from `core` to carrier `c`'s deck plane.
void DownAim(const Carrier& c,const float* core,float* out) noexcept {
    const float* up=reinterpret_cast<const float*>(c.sub+kMatrix+0x10);
    const float* p=reinterpret_cast<const float*>(c.sub+kPosition);
    float over=0.0f;
    for(int i=0;i<3;++i)over+=(core[i]-(p[i]+up[i]*kDeckTop))*up[i];
    float drop=up[1]>0.5f ? over/up[1] : over;
    if(!(drop>5.0f))drop=5.0f;
    if(drop>1000.0f)drop=1000.0f;
    out[0]=core[0];out[1]=core[1]-drop;out[2]=core[2];
}

// The AI wrapper's work for one ship being sent over the carrier: the game's fly-to toward the spot (its carrier's
// motion led), at a speed easing off near it. Climbs first when far below the spot and far off.
void Fly(unsigned char* o,Ship& s,ULONGLONG ms) noexcept {
    const Carrier& c=carriers[s.carrier];
    if(!CarrierLive(c,ms))return;
    const float* pos=reinterpret_cast<const float*>(o+kPosition);
    float spot[3];
    Spot(c,s.spotZ,spot);
    alignas(16) float to[4]={spot[0],spot[1],spot[2],1.0f};
    float lead[3]={c.vel[0]/kFlyGain,c.vel[1]/kFlyGain,c.vel[2]/kFlyGain};
    const float leadLen=std::sqrt(lead[0]*lead[0]+lead[1]*lead[1]+lead[2]*lead[2]);
    const float leadScale=leadLen>kLeadMax ? kLeadMax/leadLen : 1.0f;
    for(int i=0;i<3;++i)to[i]+=std::isfinite(lead[i]) ? lead[i]*leadScale : 0.0f;
    const float flat=Flat(pos,spot);
    if(spot[1]-pos[1]>kClimbFirst && flat>kClimbFar) {   // up steeply first, still heading for it
        to[0]=pos[0]+(spot[0]-pos[0])/flat*50.0f;to[2]=pos[2]+(spot[2]-pos[2])/flat*50.0f;
    }
    float speed=Dist(pos,to)*kFlyGain;
    if(speed>kFlySpeed)speed=kFlySpeed;
    reinterpret_cast<FlyToFn>(image+kFlyTo)(o,to,0.0f,speed);
    if(flat<kArrive)Put<float>(o,kYawRate,0.0f);   // over it: no circling round the point
    s.flownAt=ms;
}

void __fastcall ShipAiHook(void* object,const void* context) {
    nextShipAi(object,context);
    if(!flyOk || broken)return;
    __try {
        auto const o=static_cast<unsigned char*>(object);
        for(auto& s:ships)
            if(s.ship==o && s.steer){Fly(o,s,GameMs());break;}
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("LASER fault flying a ship: the flight is off until the game restarts");flyOk=false;}
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

// The charge (or the flight) over: the ship let go, its cooldown and everyone's gap started.
void EndCharge(Ship& s,ULONGLONG ms) noexcept {
    DropSight(s);
    s.phase=Phase::idle;s.steer=false;s.down=false;s.nextAt=ms+Cooldown();
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

// The beams' start and end this frame: the core and straight under it on the deck, or (fallback) the hatch and
// the target, followed until kLockMs before the shot.
bool Line(Ship& s,ULONGLONG ms,float* start) noexcept {
    if(s.down) {
        const Carrier& c=carriers[s.carrier];
        if(!CarrierLive(c,ms))return false;
        Under(s.ship,kCoreBelow,start);
        DownAim(c,start,s.aim);s.atPlayer=false;
        return true;
    }
    Under(s.ship,kHatchBelow,start);
    if(s.phase==Phase::charging && ms-s.phaseAt>=kChargeMs-kLockMs)return true;
    float aim[3];bool atPlayer=false;
    if(!Target(start,aim,&atPlayer))return s.phase==Phase::charging;
    std::memcpy(s.aim,aim,12);s.atPlayer=atPlayer;
    return true;
}

void Fire(Ship& s,const float* start,ULONGLONG ms) noexcept {
    const float damage=cfg.carrierLaserDamage>0.0f ? cfg.carrierLaserDamage : 0.0f;
    unsigned char* const o=Beam(kLaserSgo,s.ship,start,s.aim,damage);
    Log("LASER ship=%p %s from (%.0f,%.0f,%.0f) to (%.0f,%.0f,%.0f)%s damage=%.0f beam=%p",s.ship,s.down ? "firing down" : "fire",
        start[0],start[1],start[2],s.aim[0],s.aim[1],s.aim[2],s.down ? "" : s.atPlayer ? " (player)" : " (carrier deck)",damage,o);
    EndCharge(s,ms);   // the ship let go: back on its route, or hovering where it is
}

void Charge(Ship& s,ULONGLONG ms) noexcept {
    float start[3];
    if(!Line(s,ms,start)){EndCharge(s,ms);return;}
    s.sight=Beam(kSightSgo,s.ship,start,s.aim,0.0f);
    if(!s.sight){EndCharge(s,ms);return;}
    s.sightCtrl=At<const void*>(s.sight,kSelfCtrl);
    s.phase=Phase::charging;s.phaseAt=ms;s.hpAtCharge=At<float>(s.ship,kHp);
    Log("LASER charge ship=%p %s from (%.0f,%.0f,%.0f) to (%.0f,%.0f,%.0f)%s hp=%.0f/%.0f break=%.0f",s.ship,
        s.down ? "straight down" : "oblique",start[0],start[1],start[2],s.aim[0],s.aim[1],s.aim[2],
        s.down ? "" : s.atPlayer ? " (player)" : " (carrier deck)",s.hpAtCharge,At<float>(s.ship,kHpMax),
        At<float>(s.ship,kHpMax)*cfg.carrierLaserBreak);
}

// One charging ship's frame: interrupted, beams followed, fired at kChargeMs.
void Charging(Ship& s,ULONGLONG ms) noexcept {
    const float hp=At<float>(s.ship,kHp),hpMax=At<float>(s.ship,kHpMax);
    if(s.ship[kDead] || hp<=0.0f){Interrupt(s,ms,"shot down");return;}
    if(s.hpAtCharge-hp>=hpMax*cfg.carrierLaserBreak){Interrupt(s,ms,"took too much damage");return;}
    float start[3];
    if(!Line(s,ms,start)){Interrupt(s,ms,"carrier gone");return;}
    if(ms-s.phaseAt>=kChargeMs){Fire(s,start,ms);return;}
    Steer(s.sight,s.sightCtrl,start,s.aim);
}

// The fallback charge where the ship is: the hatch at the player or deck, as before the flight existed.
void Oblique(Ship& s,ULONGLONG ms,const char* why) noexcept {
    Log("LASER ship=%p %s: oblique charge from where it is",s.ship,why);
    s.steer=false;s.down=false;s.phase=Phase::idle;
    Charge(s,ms);
}

// One ship on its way: over the spot (the charge), shot down, its carrier gone, too slow, or the wrapper silent.
void Moving(Ship& s,ULONGLONG ms) noexcept {
    const float hp=At<float>(s.ship,kHp);
    if(s.ship[kDead] || hp<=0.0f){Interrupt(s,ms,"shot down on the way");return;}
    const Carrier& c=carriers[s.carrier];
    if(!CarrierLive(c,ms)){Interrupt(s,ms,"carrier gone");return;}
    if(!flyOk || ms-(s.flownAt ? s.flownAt : s.phaseAt)>kAiDeadMs) {
        if(flyOk && !s.flownAt){aiDead=true;Log("LASER ship=%p: the 508 AI wrapper never ran (state %d): flights off",s.ship,
                                                 At<std::int32_t>(s.ship,kMoveState));}
        Oblique(s,ms,"not flown");return;
    }
    if(ms-s.phaseAt>kMoveMaxMs){Oblique(s,ms,"not over the carrier in 60s");return;}
    const float* pos=reinterpret_cast<const float*>(s.ship+kPosition);
    float spot[3];
    Spot(c,s.spotZ,spot);
    if(Flat(pos,spot)>=kArrive || std::fabs(pos[1]-spot[1])>=kArrive)return;
    Log("LASER ship=%p over the carrier: charging (spot z=%.0f, %.1fs on the way)",s.ship,s.spotZ,
        static_cast<double>(ms-s.phaseAt)/1000.0);
    s.down=true;s.phase=Phase::idle;
    Charge(s,ms);   // it keeps being flown to the spot: the carrier sails on
}

// Ship `s` (idle, due) starts: sent over the nearest carrier within kShipRange, or the fallback charge.
bool Start(Ship& s,ULONGLONG ms) noexcept {
    const float* pos=reinterpret_cast<const float*>(s.ship+kPosition);
    int best=-1;float bestD=kShipRange;
    for(int i=0;i<kMaxCarriers;++i) {
        if(!CarrierLive(carriers[i],ms))continue;
        const float d=Dist(pos,carriers[i].pos);
        if(d<=bestD){bestD=d;best=i;}
    }
    if(best<0)return false;
    s.carrier=best;s.phaseAt=ms;s.flownAt=0;
    if(!flyOk || aiDead){Oblique(s,ms,"flights off");return s.phase==Phase::charging;}
    float spot[3],d=1e30f;
    for(const float z:kSpotZ) {
        float p[3];
        Spot(carriers[best],z,p);
        if(Flat(pos,p)<d){d=Flat(pos,p);s.spotZ=z;std::memcpy(spot,p,12);}
    }
    s.phase=Phase::moving;s.steer=true;s.down=false;
    Log("LASER ship=%p moving over the carrier sub=%p spot z=%.0f (%.0f,%.0f,%.0f) %.0fm off, state=%d route=%d",s.ship,
        carriers[best].sub,s.spotZ,spot[0],spot[1],spot[2],Dist(pos,spot),At<std::int32_t>(s.ship,kMoveState),
        At<const void*>(s.ship,kRoute)!=nullptr);
    return true;
}

void Tick() noexcept {
    const ULONGLONG ms=GameMs();
    Seen seen{{},0};
    bool any=false;
    for(const auto& c:carriers) {
        if(!CarrierLive(c,ms))continue;
        VisitEnemiesOf(At<std::int32_t>(c.sub,kTeam),&SeeShip,&seen);
        any=true;break;   // the carriers are all on the player's team: one visit sees every ship
    }
    if(!any)return;
    bool busy=false;
    for(int i=0;i<seen.n;++i) {
        Ship* s=Slot(seen.list[i],ms);
        if(s)s->seen=ms;
    }
    for(auto& s:ships) {
        if(!s.ship)continue;
        if(ms-s.seen>kStaleMs || !ObjectLive(s.ship,s.ctrl,kShipVtable)) {
            if(s.phase!=Phase::idle)Interrupt(s,ms,"ship gone");
            DropSight(s);
            s=Ship{};
            continue;
        }
        if(s.phase==Phase::moving)Moving(s,ms);
        else if(s.phase==Phase::charging)Charging(s,ms);
        busy=busy || s.phase!=Phase::idle;
    }
    if(busy || ms<quietUntil)return;
    for(auto& s:ships) {
        if(!s.ship || s.phase!=Phase::idle || ms<s.nextAt)continue;
        if(Start(s,ms) && s.phase!=Phase::idle)return;   // one ship at a time
    }
}
}  // namespace

void PreloadLaser() noexcept {
    __try {
        preloaded=false;
        for(auto& s:ships)s=Ship{};   // a new mission: the old objects are gone with the old one
        for(auto& c:carriers)c=Carrier{};
        quietUntil=0;
        if(!sigOk || broken || !cfg.carrierLaser)return;
        const auto mgr=At<void*>(image,kPreloadMgr);
        preloaded=mgr && FilesThere();
        if(preloaded) {
            reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kSightSgo,2,-1);
            reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kLaserSgo,2,-1);
        }
        Log("LASER preload=%d flight=%d",preloaded,flyOk && !aiDead);
    } __except(EXCEPTION_EXECUTE_HANDLER){preloaded=false;}
}

void CarrierLaserFrame(const unsigned char* sub) noexcept {
    if(!sigOk || !preloaded || broken || !cfg.carrierLaser)return;
    __try {
        SeeCarrier(sub,GameMs());   // every carrier, every frame
        const ULONGLONG tick=GetTickCount64();
        if(tick-tickAt<10)return;   // once a frame of the carriers that call it
        tickAt=tick;
        Tick();
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("LASER fault in the frame: off until the game restarts");broken=true;}
}

// The flight: UfoCarrier508's AI slot wrapped, if its AI, the fly-to and the Update's move are the known code.
bool InstallFlight() noexcept {
    const auto slot=reinterpret_cast<void**>(image+kShipVtable+kShipAiSlot*8);
    if(!Matches(kShipAi,kShipAiSig,sizeof(kShipAiSig)) || !Matches(0x4F6DA9,kShipAiRouteSig,sizeof(kShipAiRouteSig)) ||
       !Matches(kFlyTo,kFlyToSig,sizeof(kFlyToSig)) || !Matches(0x4F2487,kFlyToWishSig,sizeof(kFlyToWishSig)) ||
       !Matches(0x4F76F5,kUpdateStateSig,sizeof(kUpdateStateSig)) || !Matches(0x4F76FF,kUpdateWishSig,sizeof(kUpdateWishSig)) ||
       !Readable(slot,8) || !*slot) {
        Log("LASER flight: unexpected EDF.dll code: ships fire from where they are");
        return false;
    }
    void* const current=*slot;
    if(current!=image+kShipAi)Log("LASER 508 AI: chaining onto %p (another plugin)",current);
    nextShipAi=reinterpret_cast<ShipAiFn>(current);
    return PatchVtableSlot(slot,current,reinterpret_cast<void*>(&ShipAiHook));
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
        flyOk=sigOk && InstallFlight();   // idle unless the laser sends a ship
        Log("HOOK carrier laser=%d flight=%d (%s)",sigOk,flyOk,!sigOk ? "off: unexpected EDF.dll code" : cfg.carrierLaser ? "on" : "off in the ini");
        return sigOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
