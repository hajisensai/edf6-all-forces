// The drill tank (钻头战车, docs/drill-re.md): EDF6VC_DRILL.SGO (tools/make_drill.py), the Blacker's class
// (Vehicle505_Tank) in the drill tank's model, its drill on a bone of its own (kDrillBone) that the plugin spins.
// Melee: the drill has no rounds. Holding fire spins it up (DrillInput takes the trigger off the seat before the
// stock input sees it, so the stock cannon path never fires), letting go spins it down; the RPM sets how fast the
// drill turns, the damage it deals and how fast it breaks what it bores into. What it touches (an enemy's lock point
// round its axis, or the map along it) gets a drill charge every kBiteSec (jet_bay.cpp DrillCharge): a stock
// DemoIndirectFire round with a kChargeRadius blast fired by the tank, so the damage is the game's own: the enemies of
// its side only (the round's team is the tank's: no friendly fire), its kills, and the map's buildings and rocks hurt
// through the stock break-building path (a blast of 3 m or more, GameDamageInfo +0x60 bit 0, takes its damage off the
// map object's HP: docs/drill-re.md §3).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "body506.h"
#include "memory.h"
#include <cmath>
#include <cwchar>

namespace crew {
namespace {
// Vehicle505_Tank (the Blacker): its vtable, and its input (slot 55, 0x61ACD0) reads seat 0's primary trigger
// (seat+0x2E4) through 0x62DE50 (>= 0.8) and pulls weapon holder 0 with it (0x61AD14..0x61AD33).
constexpr unsigned kVt505=0x17DADB0;
constexpr std::size_t kSeatTrigger=0x2E4;
constexpr float kTriggerOn=0.8f;
const unsigned char kTriggerSig[]={0xF3,0x0F,0x10,0x8B,0xE4,0x02,0x00,0x00,0x48,0x8D,0x8B,0xC0,0x02,0x00,0x00,0xE8};
constexpr unsigned kTriggerRead=0x61AD14;
// The drill's bone (pylib/drill_model.py DRILL_BONE): its origin on the drill's axis at the drill's base, its local
// +Z the axis (the hull's forward), so turning its local matrix about Z spins the drill in place. The drill's length
// and base radius (m, as the model is built: pylib/drill_model.py DRILL_LENGTH / DRILL_RADIUS; tools/selftest.py
// holds them equal).
const wchar_t kDrillBone[]=L"edf6vc_drill";
constexpr float kDrillLength=3.77f,kDrillRadius=0.97f;
constexpr std::size_t kModelInst=kModelInst506,kBoneLocal=kBoneLocal506,kBoneWorld=kBoneWorld506;
// What it reaches: an enemy lock point within kDrillRadius + kEnemyReach of the axis from kBehind behind the base to
// kAhead past the tip (a lock point sits inside the enemy's body, not on its skin); the map along the axis to kAhead
// past the tip.
constexpr float kEnemyReach=2.5f,kBehind=0.5f,kAhead=1.0f;
constexpr float kLowRay=0.8f;      // the second map ray: this share of the base radius under the axis
constexpr float kBiteSec=0.2f;      // a charge this often while it touches something and turns at kWorkShare or more
constexpr float kWorkShare=0.15f;   // of the top RPM: slower, it neither hurts nor breaks anything
constexpr float kInto=1.0f;         // m past the map hit the charge is aimed (it meets the wall on its way)
constexpr float kPi=3.14159265f;
// An NPC driver has no trigger: its drill spins while it touches something (a probe every kBiteSec), kNpcHoldMs on.
constexpr ULONGLONG kNpcHoldMs=1500;
constexpr ULONGLONG kStaleMs=2000;   // a drill tank not seen this long is gone: its slot is free
constexpr int kMaxDrills=8;

struct Drill {
    ObjRef ref;
    ULONGLONG seen,lastMs,touchAt,loggedAt;
    float rpm,angle,bite;
    bool held,player;
    const void* bones;            // the model's bone array the record below is in (looked up again when it changes)
    unsigned char* rec;
    float bind[16],set[16];
    bool written,rewritten;
    int bites,misses;
};
Drill drills[kMaxDrills]{};
bool triggerOk=false;

// The local player's drill now (game thread writes, the HUD's draw thread reads).
SRWLOCK cueLock=SRWLOCK_INIT;
DrillCue cue{};
ULONGLONG cueAt=0;
constexpr ULONGLONG kCueFreshMs=300;

float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

bool Is505(const void* v) noexcept { return At<const unsigned char*>(v,0)==image+kVt505; }

// v's drill bone record (nullptr: no drill: not the drill tank's model); the bind pose is taken from it the first time.
unsigned char* DrillBone(Drill& d,const unsigned char* v) noexcept {
    const unsigned char* inst=v+kModelInst;
    const auto bones=At<const void*>(inst,kInstBones506);
    if(!bones)return nullptr;
    if(bones==d.bones)return d.rec;
    d.bones=bones;d.written=false;d.rewritten=false;
    d.rec=BoneRecord506(inst,kDrillBone);
    if(d.rec)std::memcpy(d.bind,d.rec+kBoneLocal,64);
    return d.rec;
}

// The drill tank's state: a new one in a free slot (or a gone vehicle's at the same address, or a stale one's).
Drill* DrillOf(const void* v,ULONGLONG ms) noexcept {
    Drill* slot=nullptr;
    for(auto& d:drills) {
        if(d.ref.Is(v))return &d;
        if(!slot && (!d.ref || d.ref.obj==v || ms-d.seen>kStaleMs))slot=&d;
    }
    if(slot)*slot=Drill{ObjRef::Of(v),ms};
    return slot;
}

// The drill tank v is (505 class with the drill bone), its state; nullptr for any other vehicle.
Drill* DrillTank(unsigned char* v) noexcept {
    if(!Is505(v) || v[kDead] || SeatCount(v)==0)return nullptr;
    // Probe the bone before taking a slot: the drill is only ever in the drill tank's model.
    if(!BoneRecord506(v+kModelInst,kDrillBone))return nullptr;
    const ULONGLONG ms=GameMs();
    Drill* const d=DrillOf(v,ms);
    if(!d || !DrillBone(*d,v))return nullptr;
    return d;
}

// The drill spun `angle` rad about its own axis: local = Rz(angle) x bind (row vectors: rows 0 and 1, the bone's
// x and y, turn in their plane; row 2, the axis, and the translation stay).
void Pose(Drill& d) noexcept {
    if(d.written && std::memcmp(d.rec+kBoneLocal,d.set,64)!=0 && !d.rewritten) {
        d.rewritten=true;   // something else (an animation) writes it every frame: the spin would not show. Said once.
        Log("DRILL v=%p: its bone's local matrix was rewritten by the game between frames",d.ref.obj);
    }
    const float co=std::cos(d.angle),si=std::sin(d.angle);
    const float* b=d.bind;
    for(int x=0;x<4;++x) {
        d.set[x]=co*b[x]+si*b[4+x];
        d.set[4+x]=-si*b[x]+co*b[4+x];
        d.set[8+x]=b[8+x];
        d.set[12+x]=b[12+x];
    }
    std::memcpy(d.rec+kBoneLocal,d.set,64);
    d.written=true;
}

// The drill's axis in the world, from its bone's world matrix (last frame's): the base, the unit axis, the tip.
bool Axis(const Drill& d,float* base,float* axis,float* tip) noexcept {
    const float* w=reinterpret_cast<const float*>(d.rec+kBoneWorld);
    std::memcpy(base,w+12,12);std::memcpy(axis,w+8,12);
    const float n=std::sqrt(Dot(axis,axis));
    if(!std::isfinite(n+base[0]+base[1]+base[2]) || n<1e-3f)return false;
    for(int i=0;i<3;++i){axis[i]/=n;tip[i]=base[i]+axis[i]*kDrillLength;}
    return true;
}

// The nearest enemy lock point the drill reaches (EnemyVisitor).
struct Reach { float from[3],to[3],best,at[3]; bool found; };
void SeeEnemy(void* ctx,const void*,const float* aim) noexcept {
    auto& r=*static_cast<Reach*>(ctx);
    if(!NearLine(r.from,r.to,aim,kDrillRadius+kEnemyReach))return;
    const float d[3]={aim[0]-r.from[0],aim[1]-r.from[1],aim[2]-r.from[2]};
    const float dist=Dot(d,d);
    if(r.found && dist>=r.best)return;
    r.found=true;r.best=dist;std::memcpy(r.at,aim,12);
}

// What the drill touches now: an enemy (true, `enemy` set, `at` its lock point) or the map (true, `at` a point
// kInto past the surface along the axis); false with nothing.
bool Touch(const unsigned char* v,const Drill& d,float* from,float* at,bool* enemy) noexcept {
    float base[3],axis[3],tip[3];
    if(!Axis(d,base,axis,tip))return false;
    Reach r{};
    for(int i=0;i<3;++i){r.from[i]=base[i]-axis[i]*kBehind;r.to[i]=tip[i]+axis[i]*kAhead;}
    VisitEnemies(v,&SeeEnemy,&r);
    std::memcpy(from,base,12);
    if(r.found){*enemy=true;std::memcpy(at,r.at,12);return true;}
    // The map: along the axis, else along the drill's underside (kLowRay of its radius under the axis, by the hull's
    // up: the bone's own rows turn with the spin), which meets a rock or a low wall under the axis.
    float hit[3];
    if(MapRay(base,r.to,hit)<0.0f) {
        const float* up=reinterpret_cast<const float*>(v+kMatrix)+4;
        float a[3],b[3];
        for(int i=0;i<3;++i){a[i]=base[i]-up[i]*kDrillRadius*kLowRay;b[i]=r.to[i]-up[i]*kDrillRadius*kLowRay;}
        if(MapRay(a,b,hit)<0.0f)return false;
    }
    *enemy=false;
    for(int i=0;i<3;++i)at[i]=hit[i]+axis[i]*kInto;
    return true;
}

// One bite: a drill charge onto what it touches, its damage the RPM's share of the per-second value times kBiteSec.
void Bite(unsigned char* v,Drill& d,float share,ULONGLONG ms) noexcept {
    float from[3],at[3];
    bool enemy=false;
    if(!Touch(v,d,from,at,&enemy))return;
    d.touchAt=ms;
    if(share<kWorkShare)return;
    const float perSec=enemy ? Cfg().drillDamage : Cfg().drillBreak;
    const float damage=perSec*share*kBiteSec;
    if(!(damage>0.0f))return;
    if(DrillCharge(v,from,at,damage))++d.bites;
    else ++d.misses;
    if(Cfg().debug && ms-d.loggedAt>2000) {
        d.loggedAt=ms;
        Log("DRILL v=%p %.0f rpm: %s at (%.0f,%.0f,%.0f), %.0f damage a bite (%d bites, %d not fired)",v,d.rpm,enemy ? "enemy" : "map",at[0],
            at[1],at[2],damage,d.bites,d.misses);
    }
}

void Publish(const Drill& d) noexcept {
    AcquireSRWLockExclusive(&cueLock);
    cue=DrillCue{d.rpm,Cfg().drillMaxRpm,GameMs()-d.touchAt<=500};
    cueAt=GetTickCount64();
    ReleaseSRWLockExclusive(&cueLock);
}
}  // namespace

bool InstallDrill() noexcept {
    triggerOk=Matches(kTriggerRead,kTriggerSig,sizeof(kTriggerSig));
    Log("HOOK drill trigger=%d%s",triggerOk,triggerOk ? "" : " (unexpected EDF.dll code: the drill tank's drill is off)");
    return triggerOk;
}

bool IsDrillTank(const void* v) noexcept {
    return v && Is505(v) && BoneRecord506(static_cast<const unsigned char*>(v)+kModelInst,kDrillBone)!=nullptr;
}

// Before the stock input (crew.cpp InputHook): the player's trigger is the drill's, taken off the seat so the
// stock input never pulls weapon holder 0.
void DrillInput(unsigned char* v) noexcept {
    if(!triggerOk || !Cfg().drill)return;
    Drill* const d=DrillTank(v);
    if(!d)return;
    unsigned char* const seat=SeatAt(v,0);
    d->player=SeatRider(seat)==Rider::player;
    if(!d->player)return;
    float* const trigger=reinterpret_cast<float*>(seat+kSeatTrigger);
    d->held=*trigger>=kTriggerOn;
    *trigger=0.0f;
}

// After the stock input: the RPM toward the top (held) or nothing, the drill turned by it, a bite every kBiteSec.
void DrillFrame(unsigned char* v) noexcept {
    if(!triggerOk || !Cfg().drill)return;
    Drill* const d=DrillTank(v);
    if(!d)return;
    const ULONGLONG ms=GameMs();
    const float dt=GameStep(d->lastMs ? ms-d->lastMs : 0);
    d->lastMs=d->seen=ms;
    const float top=Cfg().drillMaxRpm;
    if(!d->player)d->held=d->touchAt && ms-d->touchAt<kNpcHoldMs;   // an NPC's drill spins while it touches something
    const float rate=d->held ? top/Cfg().drillSpinUpSec : -top/Cfg().drillSpinDownSec;
    d->rpm+=rate*dt;
    d->rpm=d->rpm<0.0f ? 0.0f : d->rpm>top ? top : d->rpm;
    d->angle=std::fmod(d->angle+d->rpm/60.0f*2.0f*kPi*dt,2.0f*kPi);
    Pose(*d);
    d->bite+=dt;
    if(d->bite>=kBiteSec) {
        d->bite=0.0f;
        // An NPC probes for something to bore into even standing still (that is what spins it up).
        if(d->rpm>0.0f || !d->player)Bite(v,*d,top>0.0f ? d->rpm/top : 0.0f,ms);
    }
    if(d->player)Publish(*d);
}

bool PlayerDrillCue(DrillCue* out) noexcept {
    AcquireSRWLockShared(&cueLock);
    const bool fresh=cueAt && GetTickCount64()-cueAt<=kCueFreshMs;
    if(fresh)*out=cue;
    ReleaseSRWLockShared(&cueLock);
    return fresh;
}

void ResetDrills() noexcept {
    for(auto& d:drills)d=Drill{};
}
}  // namespace crew
