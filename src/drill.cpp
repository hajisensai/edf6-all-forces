// The drill tank (钻头战车, docs/drill-re.md): EDF6VC_DRILL.SGO (tools/make_drill.py), the Blacker's class
// (Vehicle505_Tank) in the drill tank's model, its drill on a bone of its own (kDrillBone) that the plugin spins.
// Melee: the drill has no rounds. Holding fire spins it up (DrillInput takes the trigger off the seat before the
// stock input sees it, so the stock cannon path never fires), letting go spins it down; the RPM sets how fast the
// drill turns, the damage it deals and how fast it breaks what it bores into. What it touches (an enemy's lock point
// round its axis, or the map along it) gets a drill charge every kBiteSec (jet_bay.cpp DrillCharge): a stock
// DemoIndirectFire round with a kChargeRadius blast fired by the tank, so the damage is the game's own: the enemies of
// its side only (the round's team is the tank's: no friendly fire), its kills, and the map's buildings and rocks hurt
// through the stock break-building path (a blast of 3 m or more, GameDamageInfo +0x60 bit 0, takes its damage off the
// map object's HP: docs/drill-re.md §3). Turning heats the drill (more the faster, more biting); overheated it stops,
// no bite, until it has cooled to DrillResumeHeat (the player's and an NPC's alike).
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
// +Z the axis (the hull's forward), so turning its local matrix about Z spins the drill in place. The drill's length,
// base radius and base in the model (= the vehicle's frame: the 505's slot 45 0x61AD70 roots the model at veh+0x60,
// SetWorld 0x1100B90), as built (m: pylib/drill_model.py DRILL_LENGTH / DRILL_RADIUS / DRILL_BASE; tools/selftest.py
// holds them equal). The bone record layout is the engine's model instance (0x1110FC0 builds it, 0x1100010 composes
// world = local x parent.world), no class's own: the 505's instance is at veh+0xEE0 like the 506's (0x61AE51).
const wchar_t kDrillBone[]=L"edf6vc_drill";
constexpr float kDrillLength=3.77f,kDrillRadius=0.97f;
constexpr float kDrillBaseY=3.37f,kDrillBaseZ=4.19f;
constexpr std::size_t kModelInst=kModelInst506,kBoneLocal=kBoneLocal506,kBoneWorld=kBoneWorld506;
// The hull's front at the drill's height (m along the vehicle's forward: the model's hull vertices under the drill
// end at z 3.0; the Blacker's collision shapes reach ~3.4). The drill reaches past it: pressed against a wall, the
// hull stops at the wall with the drill's base (kDrillBaseZ) already ~0.8 m inside it. A map ray started at the base
// then starts inside the building's shape and finds nothing (the rays did start there until 2026-10-05: not one bite
// in that play), and a charge fired from there leaves the wall without meeting it. So the rays start on the axis over
// the vehicle's origin (inside the hull: the hull keeps the walls out) and a charge no farther back than kHullFront.
constexpr float kHullFront=3.0f;
// What it reaches: an enemy lock point within kDrillRadius + kEnemyReach of the axis from kBehind behind the base to
// kAhead past the tip (a lock point sits inside the enemy's body, not on its skin); the map along the axis to kAhead
// past the tip.
constexpr float kEnemyReach=2.5f,kBehind=0.5f,kAhead=1.0f;
constexpr float kLowRay=0.8f;      // the second map ray: this share of the base radius under the axis
constexpr float kBiteSec=0.2f;      // a charge this often while it touches something and turns at kWorkShare or more
constexpr ULONGLONG kBiteMs=200,kFrameMs=50;   // the same in ms; "biting" (heat) = touched within a bite and a bit
constexpr float kWorkShare=0.15f;   // of the top RPM: slower, it neither hurts nor breaks anything
constexpr float kInto=1.0f;         // m past the map hit the charge is aimed (it meets the wall on its way)
constexpr float kLead=2.0f;         // m short of what it touches the charge starts (on the axis, not behind kHullFront)
constexpr float kPi=3.14159265f;
// How it looks turning. The drill's mesh repeats every 1/16 turn (its flutes; with the texture every 1/4: measured on
// the OBJ, docs/drill-re.md §4). 300 RPM at 60 frames a second is 30 deg a frame, 1.33 of that repeat: frame after
// frame the eye sees the flutes creep or flicker, not a drill turning (the user, 2026-10-05: "the drill does not
// turn"). So the drawn turn takes at most kSpinStepMost a frame (0.4 of the repeat: always seen turning forward), in
// proportion to the RPM; the RPM itself (damage, the HUD) is not capped.
constexpr float kSpinRepeat=2.0f*kPi/16.0f;
constexpr float kSpinStepMost=0.4f*kSpinRepeat;
// Heat (the user, 2026-10-05: the drill heats up and must stop when it overheats): per second +share x (1 + kBiteHeat
// while biting) / DrillHeatSec, -(1 - share) / DrillCoolSec (share = RPM / top RPM): the top RPM idling heats it from
// cold in DrillHeatSec, standing still cools it in DrillCoolSec. At 1 it overheats: no spin, no bite, until it is
// down to DrillResumeHeat.
constexpr float kBiteHeat=0.5f;
// An NPC driver has no trigger: its drill spins while it touches something (a probe every kBiteSec), kNpcHoldMs on.
constexpr ULONGLONG kNpcHoldMs=1500;
constexpr ULONGLONG kStaleMs=2000;   // a drill tank not seen this long is gone: its slot is free
constexpr ULONGLONG kLogMs=1000;     // Debug: a drill's contact / bite line at most this often, its pose 3x rarer
constexpr int kMaxDrills=8;

struct Drill {
    ObjRef ref;
    ULONGLONG seen,lastMs,touchAt,loggedAt,biteLogAt,poseLogAt,frame;
    float rpm,angle,bite,heat;
    bool held,player,npc,overheated;   // seat 0: the player, an NPC (RideAi's dummy), or (neither) empty
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
// x and y, turn in their plane; row 2, the axis, and the translation stay). The engine composes the world matrix
// from it in the 505's slot 45 (0x61AD70 -> SetWorld 0x1100B90 -> 0x1100010: every bone whose rec+8 flag is 1, as
// 0x1110FC0 sets it), which VehicleBase's update (slot 5, 0x630250) calls every frame.
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

// A point on the drill's axis in the world, from the vehicle's matrix (veh+0x60: rows right, up, forward, then the
// position; the model's root): `z` m along the axis (0 over the vehicle's origin, kDrillBaseZ the drill's base),
// `drop` m under it.
void OnAxis(const unsigned char* v,float z,float drop,float* out) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    for(int i=0;i<3;++i)out[i]=m[12+i]+m[4+i]*(kDrillBaseY-drop)+m[8+i]*z;
}

// Debug (3 kLogMs): the drawn spin as the engine composed it: the bone's world x row in the vehicle's frame (its angle
// about the axis: the written one, a frame late, when the world follows the local matrix) and how far the bone's
// world origin is from the model's drill base (0 when the bone rides the hull as built).
void LogPose(const unsigned char* v,Drill& d,ULONGLONG ms) noexcept {
    if(!Cfg().debug || ms-d.poseLogAt<kLogMs*3)return;
    d.poseLogAt=ms;
    const float* w=reinterpret_cast<const float*>(d.rec+kBoneWorld);
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float base[3];OnAxis(v,kDrillBaseZ,0.0f,base);
    const float off[3]={w[12]-base[0],w[13]-base[1],w[14]-base[2]};
    const float seen=std::atan2(Dot(w,m+4),Dot(w,m))*180.0f/kPi;
    Log("DRILL v=%p pose: %.0f rpm, written %.0f deg, the engine's world %.0f deg; bone origin %.2f m off the drill base, "
        "its axis . forward %.2f",v,d.rpm,d.angle*180.0f/kPi,seen,std::sqrt(Dot(off,off)),Dot(w+8,m+8)/std::sqrt(Dot(w+8,w+8)+1e-12f));
}

// The enemy lock point the drill reaches nearest its axis (EnemyVisitor), in the vehicle's frame: `along` the axis
// (0 over the vehicle's origin), `off` it. Also the nearest of all (to the drill's segment), for the log.
struct Reach { float origin[3],side[3],up[3],fwd[3]; float off,along,at[3],nearest,nearAlong; int seen; bool found; };
void SeeEnemy(void* ctx,const void*,const float* aim) noexcept {
    auto& r=*static_cast<Reach*>(ctx);
    const float p[3]={aim[0]-r.origin[0],aim[1]-r.origin[1],aim[2]-r.origin[2]};
    const float z=Dot(p,r.fwd),x=Dot(p,r.side),y=Dot(p,r.up);
    const float off=std::sqrt(x*x+y*y);
    const float past=z<kDrillBaseZ ? kDrillBaseZ-z : z>kDrillBaseZ+kDrillLength ? z-kDrillBaseZ-kDrillLength : 0.0f;
    const float dist=std::sqrt(off*off+past*past);
    if(r.seen++==0 || dist<r.nearest){r.nearest=dist;r.nearAlong=z;}
    if(z<kDrillBaseZ-kBehind || z>kDrillBaseZ+kDrillLength+kAhead || off>kDrillRadius+kEnemyReach)return;
    if(r.found && off>=r.off)return;
    r.found=true;r.off=off;r.along=z;std::memcpy(r.at,aim,12);
}

// What one probe saw (the bite's and the log's): an enemy or the map, the charge's start and aim, the map rays' hits
// (m along the axis from over the vehicle's origin, -1: none) and the enemies.
struct Contact { bool enemy,map; float from[3],at[3],axisHit,lowHit; Reach reach; };

// The map along the axis, `drop` m under it, from over the vehicle's origin to kAhead past the tip: the hit's
// distance along the axis, or -1.
float MapAlong(const unsigned char* v,float drop,float* hit) noexcept {
    float a[3],b[3];
    OnAxis(v,0.0f,drop,a);OnAxis(v,kDrillBaseZ+kDrillLength+kAhead,drop,b);
    const float d=MapRay(a,b,hit);
    return d<0.0f ? -1.0f : d;
}

// What the drill touches now: an enemy (its lock point the charge's aim) or the map (a point kInto past the surface
// along the axis); `from` on the axis kLead short of it, never behind kHullFront. False with nothing.
bool Touch(const unsigned char* v,Contact& c) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    c=Contact{};c.axisHit=c.lowHit=-1.0f;
    Reach& r=c.reach;
    std::memcpy(r.side,m,12);std::memcpy(r.up,m+4,12);std::memcpy(r.fwd,m+8,12);
    OnAxis(v,0.0f,0.0f,r.origin);
    VisitEnemies(v,&SeeEnemy,&r);
    float z=-1.0f,drop=0.0f;
    if(r.found) {
        c.enemy=true;z=r.along;std::memcpy(c.at,r.at,12);
    } else {
        // The map: along the axis, else along the drill's underside (kLowRay of its radius under the axis), which
        // meets a rock or a low wall under the axis.
        float hit[3];
        c.axisHit=MapAlong(v,0.0f,hit);
        if(c.axisHit<0.0f){drop=kDrillRadius*kLowRay;c.lowHit=MapAlong(v,drop,hit);}
        z=c.axisHit>=0.0f ? c.axisHit : c.lowHit;
        if(z<0.0f)return false;
        c.map=true;
        for(int i=0;i<3;++i)c.at[i]=hit[i]+r.fwd[i]*kInto;
    }
    OnAxis(v,z-kLead>kHullFront ? z-kLead : kHullFront,drop,c.from);
    return true;
}

// Debug (kLogMs): what a probe decided and why.
void LogTouch(const unsigned char* v,Drill& d,const Contact& c,bool touched,float share,ULONGLONG ms) noexcept {
    if(!Cfg().debug || ms-d.loggedAt<kLogMs)return;
    d.loggedAt=ms;
    const Reach& r=c.reach;
    if(!touched) {
        Log("DRILL v=%p touch: nothing (%.0f rpm, heat %.0f%%): no map hit along the axis or under it (rays 0..%.1f m along); "
            "%d enemies seen, the nearest %.1f m from the drill (%.1f m along), reach %.1f m",v,d.rpm,d.heat*100.0f,
            kDrillBaseZ+kDrillLength+kAhead,r.seen,r.seen ? r.nearest : -1.0f,r.seen ? r.nearAlong : 0.0f,kDrillRadius+kEnemyReach);
        return;
    }
    Log("DRILL v=%p touch: %s at (%.1f,%.1f,%.1f), %s %.1f m along (drill %.2f..%.2f), charge from (%.1f,%.1f,%.1f); %.0f rpm%s, heat %.0f%%%s",
        v,c.enemy ? "enemy" : "map",c.at[0],c.at[1],c.at[2],c.enemy ? "lock point" : c.axisHit>=0.0f ? "axis ray hit" : "low ray hit",
        c.enemy ? r.along : c.axisHit>=0.0f ? c.axisHit : c.lowHit,kDrillBaseZ,kDrillBaseZ+kDrillLength,c.from[0],c.from[1],c.from[2],d.rpm,
        share<kWorkShare ? " (too slow to bite)" : "",d.heat*100.0f,d.overheated ? " OVERHEATED: no bite" : "");
}

// One probe and, turning fast enough and not overheated, a bite: a drill charge onto what it touches, its damage the
// RPM's share of the per-second value times kBiteSec.
void Bite(unsigned char* v,Drill& d,float share,ULONGLONG ms) noexcept {
    Contact c;
    const bool touched=Touch(v,c);
    LogTouch(v,d,c,touched,share,ms);
    if(!touched)return;
    d.touchAt=ms;
    if(share<kWorkShare || d.overheated)return;
    const float perSec=c.enemy ? Cfg().drillDamage : Cfg().drillBreak;
    const float damage=perSec*share*kBiteSec;
    if(!(damage>0.0f))return;
    const bool fired=DrillCharge(v,c.from,c.at,damage);
    if(fired)++d.bites;
    else ++d.misses;
    if(Cfg().debug && (ms-d.biteLogAt>=kLogMs || !fired)) {
        d.biteLogAt=ms;
        Log("DRILL v=%p bite: %s, %.0f damage, charge %s (%d fired, %d not)",v,c.enemy ? "enemy" : "map",damage,
            fired ? "fired" : "NOT fired",d.bites,d.misses);
    }
}

// The heat over `dt` s at RPM share `share` (`biting`: touched within the last bite): see kBiteHeat. It overheats at 1
// and cools to DrillResumeHeat before it turns again; both said in the log.
void Heat(const unsigned char* v,Drill& d,float share,bool biting,float dt) noexcept {
    const auto& c=Cfg();
    d.heat+=(share*(1.0f+(biting ? kBiteHeat : 0.0f))/c.drillHeatSec-(1.0f-share)/c.drillCoolSec)*dt;
    d.heat=d.heat<0.0f ? 0.0f : d.heat>1.0f ? 1.0f : d.heat;
    if(!d.overheated && d.heat>=1.0f) {
        d.overheated=true;
        Log("DRILL v=%p overheated (%s): it stops until it cools to %.0f%%",v,d.player ? "player" : d.npc ? "NPC" : "empty",
            c.drillResumeHeat*100.0f);
    } else if(d.overheated && d.heat<=c.drillResumeHeat) {
        d.overheated=false;
        Log("DRILL v=%p cooled to %.0f%%: it turns again",v,d.heat*100.0f);
    }
}

void Publish(const Drill& d) noexcept {
    AcquireSRWLockExclusive(&cueLock);
    cue=DrillCue{d.rpm,Cfg().drillMaxRpm,d.heat,GameMs()-d.touchAt<=500,d.overheated};
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
    const Rider rider=SeatRider(seat);
    d->player=rider==Rider::player;d->npc=rider==Rider::dummy;
    if(!d->player)return;
    float* const trigger=reinterpret_cast<float*>(seat+kSeatTrigger);
    d->held=*trigger>=kTriggerOn;
    *trigger=0.0f;
}

// After the stock input: the RPM toward the top (held, not overheated) or nothing, the heat, the drill turned by it
// (once a frame, the drawn step capped: kSpinStepMost), a probe / bite every kBiteSec.
void DrillFrame(unsigned char* v) noexcept {
    if(!triggerOk || !Cfg().drill)return;
    Drill* const d=DrillTank(v);
    if(!d)return;
    const ULONGLONG ms=GameMs(),frame=GameFrame();
    if(d->frame==frame)return;   // once a frame (a second call would step the spin twice)
    d->frame=frame;
    const float dt=GameStep(d->lastMs ? ms-d->lastMs : 0);
    d->lastMs=d->seen=ms;
    const float top=Cfg().drillMaxRpm;
    // An NPC's drill spins while it touches something; an empty tank's never (it once bored on by itself, left
    // against a wall or with the drill in a slope, until the mission's end). Overheated, none.
    if(!d->player)d->held=d->npc && d->touchAt && ms-d->touchAt<kNpcHoldMs;
    if(d->overheated)d->held=false;
    const float rate=d->held ? top/Cfg().drillSpinUpSec : -top/Cfg().drillSpinDownSec;
    d->rpm+=rate*dt;
    d->rpm=d->rpm<0.0f ? 0.0f : d->rpm>top ? top : d->rpm;
    const float share=top>0.0f ? d->rpm/top : 0.0f;
    Heat(v,*d,share,d->touchAt && ms-d->touchAt<=kBiteMs+kFrameMs,dt);
    const float step=d->rpm/60.0f*2.0f*kPi*dt;
    d->angle=std::fmod(d->angle+(step<kSpinStepMost*share ? step : kSpinStepMost*share),2.0f*kPi);
    Pose(*d);
    if(d->rpm>0.0f)LogPose(v,*d,ms);
    d->bite+=dt;
    if(d->bite>=kBiteSec) {
        d->bite=0.0f;
        // An NPC probes for something to bore into even standing still (that is what spins it up).
        if(d->rpm>0.0f || (d->npc && !d->overheated))Bite(v,*d,share,ms);
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
