// A vehicle's own turrets: seat_turret.h. A turret is a store with a StoreSpec::turret (pylib/vcobjects.py Turret): its
// holder on its gunner seat (StoreSpec::seat, the row's [bone, seat]: tools/make_tracked_combat.py), its two bones the
// model's leaves (tools/prep_xmorph.py), posed here at the V603's last local step (vulcan_rotor.cpp Build) as the
// dreadnought's turrets are (dreadnought_turrets.inc). The V603 aims, reads and poses seat 0 alone
// (docs/independent-seat-re.md), so nothing native turns these.
#include "seat_turret.h"
#include "crew.h"
#include "body506.h"
#include "heli.h"
#include "layout.h"
#include "memory.h"
#include "npcai.h"
#include "payload.h"
#include "seat_net_transport.h"
#include "seat_turret_net.h"
#include "seat_turret_pose.h"
#include "stores.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
using seat_turret::Aim;
using seat_turret_net::kMostTurrets;
constexpr unsigned kVt603=0x17DC620;
constexpr int kMostVehicles=16;
constexpr ULONGLONG kStaleMs=2000;          // a vehicle's record not seen this long: free for another
constexpr ULONGLONG kTargetFrames=2;        // an NPC gunner's target this many frames old still stands
constexpr float kAimReach=3000.0f;          // m: the aim along the view with no map nearer (dreadnought_turrets.inc's)
constexpr float kOnAim=0.035f;              // rad (~2 degrees)

struct Turret {
    const StoreSpec* spec=nullptr;
    const unsigned char* weapon=nullptr;
    int holder=-1;                          // its weapon's holder in the vehicle's holder array
    unsigned seat=0;
    unsigned char* rec[2]{};                // its bones' records: yaw, pitch
    float bindYaw[16]{},bindGun[16]{};
    Aim now{};                              // where it points (from its bind: 0, 0 ahead, level)
    Aim remote{};ULONGLONG remoteAt=0;      // its trainer's on another machine (Received)
    const void* target=nullptr;ULONGLONG targetFrame=0;   // its NPC gunner's (SeatTurretsTrain)
    bool here=false;                        // trained by this machine last frame
};
struct Vehicle {
    ObjRef ref{};
    const void* bones=nullptr;
    int count=0;
    Turret t[kMostTurrets]{};
    ULONGLONG seen=0,lastMs=0,sentAt=0;
};
Vehicle vehicles[kMostVehicles]{};
// Each vehicle's sender watermarks, apart from its record (seat_net.h StreamGates: a record re-rigged or taken for another
// vehicle keeps them, so a delayed older block is never admitted again); dropped with the vehicle or the mission.
seat_net::StreamGates<kMostVehicles*2,64> gates;
SRWLOCK lock=SRWLOCK_INIT;
bool installed=false,netReady=false;

bool Is603(const unsigned char* v) noexcept { return At<std::uintptr_t>(v,0)==reinterpret_cast<std::uintptr_t>(image+kVt603); }

Vehicle* Find(const unsigned char* v) noexcept {
    for(auto& x:vehicles)if(x.ref.Is(v))return &x;
    return nullptr;
}
// Its turrets' records, found again whenever the instance's bone array moves. False: it has none.
bool Rig(Vehicle& x,unsigned char* v) noexcept {
    const unsigned char* inst=v+kModelInst506;
    const void* bones=At<const void*>(inst,kInstBones506);
    if(!bones)return false;
    if(bones==x.bones)return x.count>0;
    const ObjRef ref=x.ref;const ULONGLONG seen=x.seen;
    x=Vehicle{};x.ref=ref;x.seen=seen;x.bones=bones;
    const auto holders=At<const unsigned char*>(v,kHolders);
    const auto count=At<std::uint64_t>(v,kHolderCount);
    if(!holders || count>16 || !Readable(holders,count*kHolderStride))return false;
    for(std::uint64_t i=0;i<count && x.count<kMostTurrets;++i) {
        const auto* w=At<const unsigned char*>(holders+i*kHolderStride,kHolderWeapon);
        const StoreSpec* spec=w && Readable(w,0x200) ? StoreOf(w) : nullptr;
        if(!spec || !spec->seat || !spec->turret.yaw || !spec->turret.pitch)continue;
        Turret& t=x.t[x.count];
        t.rec[0]=BoneRecord506(inst,spec->turret.yaw);t.rec[1]=BoneRecord506(inst,spec->turret.pitch);
        if(!t.rec[0] || !t.rec[1] || !Readable(t.rec[0]+kBoneWorld506,64,true) || !Readable(t.rec[1]+kBoneWorld506,64,true) ||
           !BoneBind506(inst,t.rec[0],t.bindYaw) || !BoneBind506(inst,t.rec[1],t.bindGun)) {
            // An installed vehicle carries its model's bones (tools/make_tracked_combat.py): an install left broken.
            Log("SEATTURRET v=%p: its model lacks turret bones %ls / %ls: that turret stays still",v,spec->turret.yaw,spec->turret.pitch);
            t=Turret{};continue;
        }
        t.spec=spec;t.weapon=w;t.holder=static_cast<int>(i);t.seat=spec->seat;
        ++x.count;
    }
    if(x.count)Log("SEATTURRET v=%p rig: %d turrets",v,x.count);
    return x.count>0;
}
Vehicle* Take(unsigned char* v,ULONGLONG now) noexcept {
    if(Vehicle* x=Find(v))return Rig(*x,v) ? x : nullptr;
    for(auto& x:vehicles)if(!x.ref || !x.ref.Is(x.ref.obj) || now-x.seen>kStaleMs) {
        x=Vehicle{};x.ref=ObjRef::Of(v);x.seen=now;
        return Rig(x,v) ? &x : nullptr;
    }
    return nullptr;
}

seat_turret::Limits LimitsOf(const StoreSpec& s) noexcept {
    return {s.turret.low,s.turret.high,s.turret.arc,s.turret.yawRate,s.turret.pitchRate};
}
// Its guns' pivot (the pitch bone's world origin as last built).
const float* PivotOf(const Turret& t) noexcept { return reinterpret_cast<const float*>(t.rec[1]+kBoneWorld506)+12; }
// The aim at world point `at`: its direction in the vehicle's frame (its matrix's rows its axes).
Aim AimAt(const unsigned char* v,const Turret& t,const float* at) noexcept {
    const float* p=PivotOf(t);
    const float d[3]={at[0]-p[0],at[1]-p[1],at[2]-p[2]};
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float local[3];
    for(int r=0;r<3;++r)local[r]=d[0]*m[4*r]+d[1]*m[4*r+1]+d[2]*m[4*r+2];
    return seat_turret::AnglesOf(local);
}
// Where the screen's centre meets the map, or kAimReach out along it.
bool AimPoint(float* aim) noexcept {
    float eye[3],dir[3];
    if(!CameraRay(eye,dir))return false;
    const float end[3]={eye[0]+dir[0]*kAimReach,eye[1]+dir[1]*kAimReach,eye[2]+dir[2]*kAimReach};
    if(MapFixedRay(eye,end,aim)<0.0f)std::memcpy(aim,end,12);
    return true;
}
const float* TargetPoint(const void* object) noexcept {
    return Readable(object,kPosition+12) ? reinterpret_cast<const float*>(static_cast<const unsigned char*>(object)+kPosition) : nullptr;
}

// The nearest enemy an unmanned turret may engage: within its weapon's reach (its SGO's), its traverse and elevations,
// and of its kind (an anti-air store: an aircraft; a ground store: no aircraft).
struct Nearest { const unsigned char* v; const Turret* t; float reach,best; Aim aim; const void* object; };
void NearestVisit(void* ctx,const void* object,const float* at) {
    auto& n=*static_cast<Nearest*>(ctx);
    const float* p=PivotOf(*n.t);
    const float dx=at[0]-p[0],dy=at[1]-p[1],dz=at[2]-p[2];
    const float d=std::sqrt(dx*dx+dy*dy+dz*dz);
    if(!(d>1.0f) || d>n.reach || d>=n.best)return;
    const StoreRole role=n.t->spec->role;
    if(role==StoreRole::air || role==StoreRole::ground || role==StoreRole::bomb || role==StoreRole::rocket) {
        const bool air=GunnerAirTarget(object);
        if(air!=(role==StoreRole::air))return;
    }
    const seat_turret::Limits l=LimitsOf(*n.t->spec);
    const Aim a=AimAt(n.v,*n.t,at);
    if(std::fabs(seat_turret::Wrap(a.yaw))>l.arc+1e-4f || a.pitch<l.low || a.pitch>l.high)return;
    n.best=d;n.aim=a;n.object=object;
}

// Who trains turret `t` here this frame, and toward what: true when this machine does (its aim `want`; `fire` its
// unmanned auto turret's own fire).
bool Trainer(unsigned char* v,Turret& t,bool runs,bool autoOn,Aim* want,bool* fire) noexcept {
    *fire=false;
    if(t.seat>=SeatCount(v))return false;
    const unsigned char* seat=SeatAt(v,t.seat);
    const Rider rider=SeatRider(seat);
    if(rider==Rider::player && At<const void*>(seat,kSeatRider)==PlayerHuman()) {
        float at[3];
        if(AimPoint(at))*want=AimAt(v,t,at);
        return true;
    }
    if(rider!=Rider::none) {   // a dummy rider too: an NPC gunner's seat (AiGunner)
        if(!AiGunner(v,seat))return false;
        const float* at=t.target && GameFrame()-t.targetFrame<=kTargetFrames ? TargetPoint(t.target) : nullptr;
        if(at){const float aim[3]={at[0],at[1]+0.5f,at[2]};*want=AimAt(v,t,aim);}
        return true;
    }
    if(!t.spec->autoTurret || !runs)return false;
    if(autoOn) {
        float reach=At<float>(t.weapon,0x224);
        if(!std::isfinite(reach) || reach<=0.0f)reach=kAimReach;
        Nearest n{v,&t,reach,reach+1.0f,{},nullptr};
        VisitEnemies(v,&NearestVisit,&n);
        if(n.object){*want=n.aim;*fire=true;}
    }
    return true;
}
void FrameLocked(unsigned char* v) noexcept {
    const ULONGLONG now=GameMs();
    Vehicle* const x=Take(v,now);
    if(!x)return;
    const float dt=x->lastMs && now>x->lastMs ? std::fmin(static_cast<float>(now-x->lastMs)*0.001f,0.1f) : 1.0f/60.0f;
    x->seen=now;x->lastMs=now;
    const bool runs=RunsUnmannedTurrets(v),autoOn=VehicleAutoTurretsOn(v);
    seat_turret_net::State out{};
    unsigned fired=0;
    for(int n=0;n<x->count;++n) {
        Turret& t=x->t[n];
        Aim want{};bool fire=false;
        t.here=Trainer(v,t,runs,autoOn,&want,&fire);
        if(!t.here && now-t.remoteAt<=seat_turret_net::kRemoteMs)want=t.remote;
        seat_turret::Swing(t.now,want,LimitsOf(*t.spec),dt,kOnAim);
        // Its own fire after the turn (HasShot: the muzzle's line as last built), once a seat.
        if(fire && !(fired&(1u<<t.seat))){fired|=1u<<t.seat;UnmannedTurretsFire(v,t.seat);}
        if(t.here && t.holder<static_cast<int>(seat_net::kMostHolders)) {
            out.trained|=static_cast<std::uint8_t>(1u<<n);
            out.holder[n]=static_cast<std::int16_t>(t.holder);out.yaw[n]=t.now.yaw;out.pitch[n]=t.now.pitch;
        }
    }
    if(!netReady || !out.trained || !seat_net::Replicated(InSession(),At<std::uint16_t>(v,0x128)) ||
       now-x->sentAt<seat_turret_net::kSendMs)return;
    seat_net::ChainVehicle(v);   // this class hears its copies' turrets too
    if(seat_net::Stamp(&out,sizeof(out)) && seat_turret_net::Valid(out) && seat_net::Send(v,&out,sizeof(out)))x->sentAt=now;
}

// Another machine's turrets of `v` (game thread): each one this machine does not train itself turned as it says.
void Received(unsigned char* v,const void* block,std::size_t size) noexcept {
    seat_turret_net::State s;
    if(!installed || !netReady || !Cfg().enabled || !InSession() || !seat_turret_net::Decode(block,size,s))return;
    __try {
        if(!Readable(v,kHolderCount+8) || v[kDead] || !Is603(v))return;
        AcquireSRWLockExclusive(&lock);
        __try {
            Vehicle* const x=Take(v,GameMs());
            if(!x)return;
            auto* const gate=gates.For({x->ref.obj,x->ref.ctrl,0},[](const void* obj,const void* ctrl) noexcept {
                return Readable(obj,kSelfCtrl+8) && ObjRef{obj,ctrl}.Is(obj);
            });
            if(!gate || !gate->Admit(s.head,true,false,-1))return;
            const ULONGLONG now=GameMs();
            for(int n=0;n<kMostTurrets;++n) {
                if(!(s.trained&(1u<<n)))continue;
                for(int k=0;k<x->count;++k) {
                    Turret& t=x->t[k];
                    if(t.holder!=s.holder[n] || t.here)continue;
                    t.remote={s.yaw[n],s.pitch[n]};t.remoteAt=now;
                }
            }
        } __finally { ReleaseSRWLockExclusive(&lock); }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
}  // namespace

void SeatTurretsFrame(unsigned char* v) noexcept {
    if(!installed || !Cfg().enabled)return;
    __try {
        if(!Readable(v,kHolderCount+8) || v[kDead] || !Is603(v))return;
        AcquireSRWLockExclusive(&lock);
        __try { FrameLocked(v); }
        __finally { ReleaseSRWLockExclusive(&lock); }
    } __except(EXCEPTION_EXECUTE_HANDLER) { Log("SEATTURRET v=%p: unreadable vehicle, its turrets left",v); }
}

void SeatTurretsPose(unsigned char* v) noexcept {
    if(!installed || !Cfg().enabled)return;
    __try {
        AcquireSRWLockExclusive(&lock);
        __try {
            Vehicle* const x=Find(v);
            if(!x || x->bones!=At<const void*>(v+kModelInst506,kInstBones506))return;   // the frame rigs it again first
            for(int n=0;n<x->count;++n) {
                Turret& t=x->t[n];
                float yaw[16],gun[16];
                seat_turret::Pose(t.bindYaw,t.bindGun,t.now,yaw,gun);
                std::memcpy(t.rec[0]+kBoneLocal506,yaw,64);std::memcpy(t.rec[1]+kBoneLocal506,gun,64);
                // 1100010 tests this dirty/local-ownership flag before local * parent (vulcan_rotor.cpp).
                Put<std::int32_t>(t.rec[0],8,1);Put<std::int32_t>(t.rec[1],8,1);
            }
        } __finally { ReleaseSRWLockExclusive(&lock); }
    } __except(EXCEPTION_EXECUTE_HANDLER) { Log("SEATTURRET pose rejected unreadable native instance"); }
}

void SeatTurretsTrain(unsigned char* v,unsigned seat,const void* target) noexcept {
    if(!installed)return;
    AcquireSRWLockExclusive(&lock);
    for(auto& x:vehicles)if(x.ref.Is(v))
        for(int n=0;n<x.count;++n)if(x.t[n].seat==seat){x.t[n].target=target;x.t[n].targetFrame=GameFrame();}
    ReleaseSRWLockExclusive(&lock);
}

bool InstallSeatTurrets() noexcept {
    installed=true;
    netReady=seat_net::Install() && seat_net::Register(seat_turret_net::kMagic,&Received);
    Log("HOOK seat turrets=1 online=%d (the gunner seats' own turrets, tag 15 'STUR')",netReady);
    return true;
}

void ResetSeatTurrets() noexcept {
    AcquireSRWLockExclusive(&lock);
    for(auto& x:vehicles)x=Vehicle{};
    gates.Reset();
    ReleaseSRWLockExclusive(&lock);
}
}  // namespace crew
