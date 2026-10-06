// The ground vehicles' ram (README 载具撞击伤害, docs/vehicle-ram-re.md; ini VehicleRam / VehicleRamDamage; the user,
// 2026-10-06: "载具增加碰撞伤害，和飞机一样，要看质量和速度的关系。这里的速度指的是子部件，比如脚踩"): a driven tank, bike,
// car or mech that drives a part of itself into an enemy deals that part's kinetic energy as damage, the jets' formula
// (vehicleram.h ram::Damage: 1/2 m v^2 at 287 kJ a point, times the vehicle's tier and the ini), through the jets' impact
// charges (jet_bay.cpp ImpactDamage): the vehicle's side's enemies hurt, its kills, never a friend (the charge's team is
// the vehicle's). The stock collision and the vehicle's own damage are left as they are: this only adds the enemy's side.
//  - What hits (vehicleram.h kProfiles): per class, a hull's front and back slabs, a mech's feet (begaruta_foot_node /
//    foot_name), the Barga's fists; each a box in the vehicle's axes round a bone's origin or the vehicle's own.
//  - How fast: the part's own speed, its box centre's world motion from frame to frame (the bone's world matrix,
//    rec+0xB0, as drill.cpp reads it), so a foot coming down stomps at the foot's speed, not the walker's, and a turning
//    hull's corner at its own. The larger of this frame's and the last's (the frame it hit may already be the stop).
//  - Into what: each enemy's body (its root, object+0x90, to its lock point: drill.cpp's contact) in the part's box grown
//    kBodyPad; the closing speed is the part's velocity along the line from its centre to the body (vehicleram.h Closing:
//    a foot coming down onto an ant closes at its whole descent, one passing beside it at nothing). The enemy's own
//    motion is not read: what it hits is taken as standing (a charging enemy onto a parked tank deals nothing here).
//  - How much: ram::Damage with the part's share of the vehicle's mass: a CarBase's total body mass as the game summed it
//    (veh+0x1710, kCarMass: the car step's own 1/m reads it) when it reads sane, else its class's stock SGO mass; the
//    walkers' are estimates (vehicleram.h). Tier: max HP over the class's SGO durability. At least kLeastClosing and
//    kLeastDamage, at most one a kHitGapMs per enemy and vehicle.
//  - The blast: the impact charge nearest the part's size (vehicleram.h BlastRadius), on the body's point nearest it.
//  - Who: a vehicle with a driver in seat 0 (the player or an NPC); an empty one rolling on is nobody's ram.
//  - Where it runs: every hooked class's input (crew.cpp InputHook); the two classes crew.cpp leaves alone, the Barga
//    (501_FortressRobo, slot 4 0x60AEC0) and the Proteus (BigBegaruta, slot 4 0x644350, the Begaruta family's update),
//    get a chained update of their own here (kExtras) that runs this alone.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "body506.h"
#include "layout.h"
#include "memory.h"
#include "vehicleram.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
using ram::Profile;using ram::Part;using ram::kProfiles;using ram::kProfileCount;using ram::kMostParts;

// CarBase's total body mass: a step of the car's physics setup sums every part body's mass (0x11B13D0, over veh+0x13A8
// parts) into veh+0x1710 (0x664380), and the next step, 0x664490 (called from the same setup, 0x65CA75), takes 1/m of it
// (0x664859); mass overrides add their difference to it (0x6662AD, 0x6668CF). (M: the sum and its readers seen, the
// getter not traced; docs/vehicle-ram-re.md §3.)
constexpr std::size_t kCarMass=0x1710;
const unsigned char kCarMassStore[]={0xF3,0x41,0x0F,0x11,0xB6,0x10,0x17,0x00,0x00};   // 0x664380 movss [r14+0x1710],xmm6
const unsigned char kCarMassRead[]={0xF3,0x41,0x0F,0x10,0x85,0x10,0x17,0x00,0x00};    // 0x664859 movss xmm0,[r13+0x1710]
constexpr unsigned kCarMassStoreAt=0x664380,kCarMassReadAt=0x664859;
constexpr float kSaneLeastKg=50.0f,kSaneMostKg=1.0e7f;
// Tier bounds: a vehicle variant's own durability may differ from its class's main SGO's (a gold Begaruta).
constexpr float kLeastTier=0.5f,kMostTier=100.0f;

constexpr float kBodyPad=2.0f;        // m: an enemy's body reaches this far past its root-to-lock-point segment (L: a giant
                                      // ant's lock point is mid-body; the near-miss log says what it was)
constexpr float kLeastClosing=2.0f;   // m/s: slower is a push, not a blow
constexpr float kLeastDamage=1.0f;    // less is not worth a charge (and its burst)
constexpr float kMostPartSpeed=150.0f;// m/s: no part of a ground vehicle moves faster; a faster step is a teleport / respawn
constexpr ULONGLONG kHitGapMs=1000;   // per enemy, per vehicle
constexpr ULONGLONG kStaleMs=2000;    // a vehicle not seen this long is gone: its slot is free
constexpr ULONGLONG kMissLogMs=2000;  // Debug: a moving vehicle's near miss at most this often
constexpr float kMissLogWithin=15.0f; // ...when an enemy's body came within this of a part's centre
constexpr ULONGLONG kResyncMs=250;    // a step longer than this (a pause, a new ride): the parts' motion starts over
constexpr int kMaxRammers=48,kHits=8,kMostContacts=16;

struct PartState {
    unsigned char* rec;   // the part's bone record (nullptr: the hull, or the bone not in this model)
    float at[3],vel[3],velPrev[3];
    bool have,moving;     // a position seen; a velocity measured
};
struct Hit { ObjRef target; ULONGLONG at; };
struct Rammer {
    ObjRef ref;
    const Profile* p;
    const void* bones;    // the model's bone array the records are in (looked up again when it changes)
    ULONGLONG seen,lastMs,frame,missLogAt;
    float kg;
    bool fromCar;         // kg is the game's CarBase sum (else the class's stock SGO mass)
    PartState parts[kMostParts];
    Hit hits[kHits];
    int nextHit;
};
Rammer rammers[kMaxRammers]{};
bool carMassOk=false;

const Profile* ProfileOf(const unsigned char* v) noexcept {
    const auto vt=At<const unsigned char*>(v,0);
    for(const auto& p:kProfiles)if(vt==image+p.vtable)return &p;
    return nullptr;
}

// The vehicle's mass (kg): see kCarMass.
float MassOf(const unsigned char* v,const Profile& p,bool* fromCar) noexcept {
    *fromCar=false;
    if(!p.carBase || !carMassOk)return p.kg;
    const float kg=At<float>(v,kCarMass);
    if(!std::isfinite(kg) || kg<kSaneLeastKg || kg>kSaneMostKg)return p.kg;
    *fromCar=true;
    return kg;
}

float TierOf(const unsigned char* v,const Profile& p) noexcept {
    const float hpMax=At<float>(v,kHpMax);
    if(!std::isfinite(hpMax) || hpMax<=0.0f || p.durability<=0.0f)return 1.0f;
    const float t=hpMax/p.durability;
    return t<kLeastTier ? kLeastTier : t>kMostTier ? kMostTier : t;
}

// The vehicle's slot: its own, a new one in a free slot (or a gone vehicle's at the same address, or a stale one's).
Rammer* RammerOf(const unsigned char* v,const Profile& p,ULONGLONG ms) noexcept {
    Rammer* slot=nullptr;
    for(auto& r:rammers) {
        if(r.ref.Is(v))return &r;
        if(!slot && (!r.ref || r.ref.obj==v || ms-r.seen>kStaleMs))slot=&r;
    }
    if(!slot)return nullptr;
    *slot=Rammer{};
    slot->ref=ObjRef::Of(v);slot->p=&p;slot->seen=ms;
    slot->kg=MassOf(v,p,&slot->fromCar);
    if(Cfg().debug)Log("RAM v=%p %s: %.1f t (%s), tier %.2f, %d parts",v,p.name,slot->kg*0.001f,
                       slot->fromCar ? "the game's body mass" : "its class's",TierOf(v,p),p.count);
    return slot;
}

// The parts' bone records, looked up again when the model's bone array changes (a part whose bone this model lacks:
// none, it never hits).
void Bones(Rammer& r,const unsigned char* v) noexcept {
    const unsigned char* inst=v+kModelInst506;
    const auto bones=At<const void*>(inst,kInstBones506);
    if(bones==r.bones)return;
    r.bones=bones;
    for(int i=0;i<r.p->count;++i) {
        const Part& part=r.p->parts[i];
        PartState& s=r.parts[i];
        s=PartState{};
        if(!part.bone)continue;
        s.rec=BoneRecord506(inst,part.bone);
        if(!s.rec && part.alt)s.rec=BoneRecord506(inst,part.alt);
    }
}

// Part i's box centre in the world now, false when it has no anchor (its bone not in this model).
bool Centre(const unsigned char* v,const Part& part,const PartState& s,float* out) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* origin=m+12;
    if(part.bone) {
        if(!s.rec)return false;
        origin=reinterpret_cast<const float*>(s.rec+kBoneWorld506)+12;
    }
    const float* c=part.box.centre;
    for(int i=0;i<3;++i)out[i]=origin[i]+m[i]*c[0]+m[4+i]*c[1]+m[8+i]*c[2];
    return std::isfinite(out[0]+out[1]+out[2]);
}

// Each part's centre now and its velocity over the step (dt s); `fresh`: the last step was a frame ago.
void Move(Rammer& r,const unsigned char* v,float dt,bool fresh) noexcept {
    for(int i=0;i<r.p->count;++i) {
        PartState& s=r.parts[i];
        float now[3];
        if(!Centre(v,r.p->parts[i],s,now)){s.have=s.moving=false;continue;}
        if(s.have && fresh) {
            float vel[3];
            for(int k=0;k<3;++k)vel[k]=(now[k]-s.at[k])/dt;
            const bool sane=ram::Dot(vel,vel)<kMostPartSpeed*kMostPartSpeed;
            std::memcpy(s.velPrev,s.moving ? s.vel : vel,12);
            std::memcpy(s.vel,vel,12);
            s.moving=sane;
        } else {
            s.moving=false;
        }
        std::memcpy(s.at,now,12);
        s.have=true;
    }
}

// What the parts touch this frame (the enemy walk only collects: firing a charge makes an object, not inside the walk).
struct Contact { const void* object; int part; float at[3],closing; };
struct Scan {
    const unsigned char* v; const Rammer* r; Contact found[kMostContacts]; int count;
    float nearest; int nearestPart;   // Debug: the nearest enemy body to a moving part's centre (m), that part (-1: none)
};

void SeeEnemy(void* ctx,const void* object,const float* lock) noexcept {
    auto& sc=*static_cast<Scan*>(ctx);
    const float* root=reinterpret_cast<const float*>(static_cast<const unsigned char*>(object)+kPosition);
    if(!std::isfinite(root[0]+root[1]+root[2]))root=lock;
    const float* m=reinterpret_cast<const float*>(sc.v+kMatrix);
    const Rammer& r=*sc.r;
    for(int i=0;i<r.p->count && sc.count<kMostContacts;++i) {
        const PartState& s=r.parts[i];
        if(!s.moving)continue;
        float at[3];
        if(!ram::Touches(m,s.at,r.p->parts[i].box,kBodyPad,root,lock,at)) {
            const float gap=ram::Gap(s.at,root,lock);
            if(sc.nearestPart<0 || gap<sc.nearest){sc.nearest=gap;sc.nearestPart=i;}
            continue;
        }
        const float closing=std::fmax(ram::Closing(s.at,s.vel,at),ram::Closing(s.at,s.velPrev,at));
        if(closing<kLeastClosing)continue;
        // One contact an enemy: its fastest part.
        int k=0;
        while(k<sc.count && sc.found[k].object!=object)++k;
        if(k<sc.count && sc.found[k].closing>=closing)continue;
        if(k==sc.count)++sc.count;
        Contact& c=sc.found[k];
        c.object=object;c.part=i;c.closing=closing;std::memcpy(c.at,at,12);
    }
}

// Whether `object` was hit by this vehicle within kHitGapMs; if not, it is now (true: hit it).
bool Due(Rammer& r,const void* object,ULONGLONG ms) noexcept {
    for(auto& h:r.hits)if(h.target.Is(object) && ms-h.at<kHitGapMs)return false;
    r.hits[r.nextHit]=Hit{ObjRef::Of(object),ms};
    r.nextHit=(r.nextHit+1)%kHits;
    return true;
}

void Strike(unsigned char* v,Rammer& r,const Contact& c,ULONGLONG ms) noexcept {
    const Part& part=r.p->parts[c.part];
    const float tier=TierOf(v,*r.p);
    const float damage=ram::Damage(r.kg*part.share,c.closing,tier,Cfg().vehicleRamDamage);
    if(damage<kLeastDamage || !Due(r,c.object,ms))return;
    const float radius=ram::BlastRadius(part.box);
    const bool dealt=ImpactDamage(v,c.at,damage,radius);
    if(Cfg().debug)Log("RAM v=%p %s %s hit %p at (%.0f,%.0f,%.0f): closing %.1f m/s, %.1f t x %.2f, tier %.2f: %.0f damage, %.1f m part%s",
                       v,r.p->name,part.name,c.object,c.at[0],c.at[1],c.at[2],c.closing,r.kg*0.001f,part.share,tier,damage,radius,
                       dealt ? "" : " (not dealt: no impact charge this mission)");
}

// Debug (kMissLogMs): a part moving fast enough to hit that passed an enemy's body by: how near, against its box and the
// pad (what decides whether the box was right for the enemies it met: docs/vehicle-ram-re.md §5).
void LogMiss(const unsigned char* v,Rammer& r,const Scan& sc,ULONGLONG ms) noexcept {
    if(!Cfg().debug || sc.nearestPart<0 || sc.nearest>kMissLogWithin || ms-r.missLogAt<kMissLogMs)return;
    const PartState& s=r.parts[sc.nearestPart];
    const float speed=std::sqrt(ram::Dot(s.vel,s.vel));
    if(speed<kLeastClosing)return;
    r.missLogAt=ms;
    const float* h=r.p->parts[sc.nearestPart].box.half;
    Log("RAM v=%p %s %s missed: moving %.1f m/s, the nearest enemy body %.1f m from its centre (box half %.1f / %.1f / %.1f, pad %.1f)",
        v,r.p->name,r.p->parts[sc.nearestPart].name,speed,sc.nearest,h[0],h[1],h[2],kBodyPad);
}

bool Driven(unsigned char* v) noexcept {
    if(SeatCount(v)==0)return false;
    const Rider rider=SeatRider(SeatAt(v,0));
    return rider==Rider::player || rider==Rider::dummy;
}

// The extra update hooks (see the file comment): the classes crew.cpp does not hook, each with its stock update's
// first bytes as the signature.
struct Extra { unsigned vtable; std::size_t slot; unsigned stock; unsigned char sig[16]; const char* name; };
const Extra kExtras[]={
    {0x17D98C8,4,0x60AEC0,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48},"501_FortressRobo"},
    {0x17DEC40,4,0x644350,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x30,0x48},"BigBegaruta"},
};
constexpr int kExtraCount=static_cast<int>(sizeof(kExtras)/sizeof(kExtras[0]));
edf::VehicleInputFn nextExtra[kExtraCount]{};

int ExtraFault(const EXCEPTION_POINTERS* e) noexcept {
    static ULONGLONG at=0;
    const ULONGLONG now=GetTickCount64();
    if(now-at>10000) {
        at=now;
        Log("FAULT ram update: %08lX at %p (skipped this frame)",e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

template<int I> void __fastcall ExtraHook(void* v,std::uintptr_t a2,void* a3,void* a4) {
    nextExtra[I](v,a2,a3,a4);
    if(!Cfg().enabled)return;
    __try { VehicleRamFrame(static_cast<unsigned char*>(v)); } __except(ExtraFault(GetExceptionInformation())) {}
}
constexpr edf::VehicleInputFn kExtraHooks[]={&ExtraHook<0>,&ExtraHook<1>};
static_assert(sizeof(kExtraHooks)/sizeof(kExtraHooks[0])==kExtraCount,"one hook per extra class");
}  // namespace

bool InstallVehicleRam() noexcept {
    carMassOk=Matches(kCarMassStoreAt,kCarMassStore,sizeof(kCarMassStore)) && Matches(kCarMassReadAt,kCarMassRead,sizeof(kCarMassRead));
    if(!carMassOk)Log("RAM CarBase mass code not as expected: tanks, bikes and cars ram with their class's stock mass");
    int hooked=0;
    for(int i=0;i<kExtraCount;++i) {
        const Extra& x=kExtras[i];
        if(!Matches(x.stock,x.sig,sizeof(x.sig))){Log("RAM %s update at %#x not as expected: it does not ram",x.name,x.stock);continue;}
        void** const slot=reinterpret_cast<void**>(image+x.vtable)+x.slot;
        if(*slot!=image+x.stock)Log("RAM %s update slot holds %p (another plugin): chaining onto it",x.name,*slot);
        void* next=nullptr;
        if(!edf::ChainVtableSlot(slot,reinterpret_cast<void*>(kExtraHooks[i]),&next)){Log("RAM %s update slot patch failed: it does not ram",x.name);continue;}
        nextExtra[i]=reinterpret_cast<edf::VehicleInputFn>(next);
        ++hooked;
    }
    Log("HOOK vehicle ram: car mass=%d, own updates %d/%d (Barga, Proteus)",carMassOk,hooked,kExtraCount);
    return true;
}

// Once a frame per vehicle: its parts' motion, then what they drive into (see the file comment).
void VehicleRamFrame(unsigned char* v) noexcept {
    const auto& cfg=Cfg();
    if(!cfg.vehicleRam || !(cfg.vehicleRamDamage>0.0f) || v[kDead])return;
    const Profile* const p=ProfileOf(v);
    if(!p || !Driven(v))return;
    const ULONGLONG ms=GameMs();
    Rammer* const r=RammerOf(v,*p,ms);
    if(!r || (r->frame==GameFrame() && r->lastMs==ms))return;   // once a frame (the extra hooks and the input never both)
    const bool fresh=r->lastMs && ms-r->lastMs<=kResyncMs;
    const float dt=GameStep(r->lastMs ? ms-r->lastMs : 0);
    r->frame=GameFrame();r->lastMs=r->seen=ms;
    Bones(*r,v);
    Move(*r,v,dt,fresh);
    Scan sc{v,r,{},0,0.0f,-1};
    VisitEnemies(v,&SeeEnemy,&sc);
    for(int i=0;i<sc.count;++i)Strike(v,*r,sc.found[i],ms);
    if(!sc.count)LogMiss(v,*r,sc,ms);
}

void ResetVehicleRams() noexcept {
    for(auto& r:rammers)r=Rammer{};
}
}  // namespace crew
