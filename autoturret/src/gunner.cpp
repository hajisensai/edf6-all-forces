// Tank gunners: the side guns of the Titan (Vehicle404_Tank) and of the Ranger's gunner-seat tanks
// (Vehicle403_Tank): real NPC occupants aim/fire; players receive optional aim assistance.
//
// Seat 0 is the driver's (the main cannon); every other seat with a gun of its own (a weapon holder and
// an aim controller) is a gunner seat, each driving its gun through the same VehicleWeaponAim the flak
// uses (the stock tanks: GUNNER_L and GUNNER_R, seats 1 and 2). Their per-frame input (vtable slot
// 55) writes the turn input of seat i to vehicle+0x2AA0+i*0x10 from that seat's stick and pulls
// the trigger holding the seat's gun (vehicle+0x638, stride 0x48) on its fire button; slot 4 then applies
// every seat's turn input unconditionally. Hooking slot 55 and rewriting the gunner seats after the stock
// code aims them.
//
// The shared seat contract rejects DummyVehicleRider, unknown objects, dead humans
// and expired references. Each seat is controlled by its own occupant's machine.
// Local players retain their trigger and optional GunnerAssist; empty seats stay idle.
//
// The aim is closed on the real barrel: every frame the muzzle frame is rebuilt from its bone's
// world rows and the muzzle's local matrix (the transform fire-time 0x6969A0 runs), so the aim does
// not depend on where the gun's pivot sits or how its axes map, except for one sign per axis
// (axis angle vs geometric angle), which is learned from how the barrel actually moves.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46; see docs/re-notes.md.
#include <cstdarg>
#include <cstdio>
#include "turret.h"
#include "edf/weapon.h"

namespace autoturret {
namespace {
constexpr unsigned kTank403Vtable=0x17D8FA0,kTank403Input=0x5FEBE0;   // Vehicle403_Tank, slot 55
constexpr unsigned kTank404Vtable=0x17D9458,kTank404Input=0x5FFC50;   // Vehicle404_Tank (Titan)
constexpr unsigned kPullTrigger=0x62C000;   // (trigger): fire this frame if the weapon is alive
// Triggers: vehicle+0x638 array, +0x648 count, stride 0x48; +8 the weapon's weak_ptr control block,
// +0x10 the weapon. Trigger i belongs to seat i (the Titan's 3..5 are the seats' secondary weapons).
constexpr std::size_t kTriggers=0x638,kTriggerCount=0x648,kTriggerStride=0x48,kTriggerCtrl=0x8,kTriggerWeapon=0x10;
// Weapon muzzles (MeanMuzzle, the weapon matrix): common/edf/weapon.h.
using edf::kWeaponMatrix;
constexpr std::uint64_t kMaxTriggers=16;
using edf::kMaxSeats;
constexpr float kMuzzleReach=30.0f;         // a muzzle farther than this from the vehicle is garbage
// The side guns turn far slower than the flak turret (~0.3 rad/s against ~1.1), so a fixed
// stick-per-radian gain leaves them a second behind a tank that is driving. Their gain comes from
// the learned turn rate instead: an error is asked to close in this many frames.
constexpr float kSettleFrames=6.0f;
constexpr float kHoldCone=2.0f;             // a firing gun keeps firing until it is this many cones off
constexpr std::size_t kWeaponFire=0x139;    // the trigger 0x62C000 sets; the weapon update reads and clears it
// Rounds left: 0x690BB0 fires only while this is above zero; it counts down per shot.
constexpr std::size_t kWeaponAmmo=0xBE8;

using TriggerFn=void(__fastcall*)(void*);
VehicleInputFn next403=nullptr,next404=nullptr;

// The native operator lookup stays authoritative. Empty/dummy/dead side seats
// cannot borrow another seat's driver through the historical LocalOperator fallback.
constexpr unsigned kUserIface403Vtable=0x17D9238,kUserIface404Vtable=0x17D96F0,kWeaponUser=0x62D950;
constexpr std::size_t kUserIface=0x120,kUserSlot=0x58/8;
using UserFn=const void*(__fastcall*)(void*,const void*);
UserFn nextUser[2]{};
bool RealWeaponSeat(void* iface,const void* weapon) noexcept {
    __try {
        const auto vehicle=static_cast<unsigned char*>(iface)-kUserIface;
        for(unsigned s=0;s<edf::SeatCount(vehicle);++s) {
            const auto seat=edf::SeatAt(vehicle,s);
            const auto holders=At<const unsigned char* const*>(seat,kSeatWeapons);
            const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
            if(count>16 || !Readable(holders,count*8))continue;
            for(std::uint64_t i=0;i<count;++i)
                if(Readable(holders[i],kHolderWeapon+8) && At<const void*>(holders[i],kHolderWeapon)==weapon)
                    return s==0 || edf::LivingSoldierInSeat(image,seat);
        }
        return true; // not a seat weapon: preserve the native result
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<int I> const void* __fastcall WeaponUser(void* iface,const void* weapon) {
    if(!RealWeaponSeat(iface,weapon))return nullptr;
    return nextUser[I](iface,weapon);
}

enum class Crew { none, ai, player, remote };   // remote: a rider another machine runs

// A side gun as the aim sees it this frame: its trigger, round and muzzle in the world.
struct Gun {
    void* trigger;
    const unsigned char* weapon;
    float pos[3];      // muzzle position, world
    float dir[3];      // barrel direction, world, unit
    Shot shot;
    float range;       // metres
    float blast;       // AmmoExplosion radius, metres (0 = solid round)
};

// The vehicle's rows: right, up, forward, position.
const float* Frame(const unsigned char* vehicle) noexcept { return reinterpret_cast<const float*>(vehicle+kMatrix); }

void ToFrame(const float* m,const float* v,float* out) noexcept {
    out[0]=Dot(v,m);out[1]=Dot(v,m+4);out[2]=Dot(v,m+8);
}

// Geometric yaw (around the vehicle's up, toward its right) and elevation of a vehicle-frame vector.
void Angles(const float* local,float* geo) noexcept {
    geo[0]=std::atan2(local[0],local[2]);
    geo[1]=std::atan2(local[1],std::sqrt(local[0]*local[0]+local[2]*local[2]));
}

bool AllFinite(const float* v,int n) noexcept {
    for(int i=0;i<n;++i)if(!std::isfinite(v[i]))return false;
    return true;
}

Crew SeatCrew(const unsigned char* seat) noexcept {
    const edf::Rider rider=edf::SeatRider(image,seat);
    if(rider==edf::Rider::none || !edf::LivingSoldierInSeat(image,seat))return Crew::none;
    if(edf::RemoteRider(At<const unsigned char*>(seat,edf::kSeatRider)))return Crew::remote;   // its own machine aims it
    return rider==edf::Rider::player ? Crew::player : Crew::ai;
}

// Only trigger latches raised here are released. Run before stock input so a
// newly seated human can write its own fresh input afterwards. Holder/control
// identity prevents clearing an unrelated weapon after a respawn or replacement.
struct GunnerPull { const unsigned char* vehicle; const void* vehicleCtrl; unsigned seat;
                    unsigned char* holder; const void* ctrl; unsigned char* weapon; };
GunnerPull ownedPulls[128]{};
void ReleaseGunnerPulls(const void* vehicle) noexcept {
    for(auto& p:ownedPulls) {
        if(!p.weapon || (vehicle && p.vehicle!=vehicle))continue;
        __try {
            if(Readable(p.vehicle,kSelfCtrl+8) && At<const void*>(p.vehicle,kSelfCtrl)==p.vehicleCtrl &&
               Readable(p.holder,kTriggerWeapon+8) && At<const void*>(p.holder,kTriggerCtrl)==p.ctrl &&
               At<const void*>(p.holder,kTriggerWeapon)==p.weapon && Readable(p.ctrl,12) && At<int>(p.ctrl,8)>0 &&
               Readable(p.weapon,kWeaponFire+1,true) && p.weapon[kWeaponFire]==1)p.weapon[kWeaponFire]=0;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        p=GunnerPull{};
    }
}
bool PullOwned(unsigned char* vehicle,unsigned seat,unsigned char* holder) noexcept {
    if(!cfg.enabled || !cfg.gunnerAi || seat>=edf::SeatCount(vehicle) || SeatCrew(edf::SeatAt(vehicle,seat))!=Crew::ai)return false;
    const auto ctrl=At<const void*>(holder,kTriggerCtrl);
    const auto weapon=At<unsigned char*>(holder,kTriggerWeapon);
    if(!Readable(ctrl,12) || At<int>(ctrl,8)<=0 || !Readable(weapon,kWeaponFire+1,true))return false;
    GunnerPull* record=nullptr;
    for(auto& p:ownedPulls) {
        if(p.weapon && (!Readable(p.vehicle,kSelfCtrl+8) || At<const void*>(p.vehicle,kSelfCtrl)!=p.vehicleCtrl))p=GunnerPull{};
        if(p.weapon==weapon){record=&p;break;}
        if(!record && !p.weapon)record=&p;
    }
    if(!record)return false;
    if(!weapon[kWeaponFire])*record={vehicle,At<const void*>(vehicle,kSelfCtrl),seat,holder,ctrl,weapon};
    reinterpret_cast<TriggerFn>(image+kPullTrigger)(holder);
    return true;
}

// The barrel: the mean of the gun's muzzles (the Titan's side cannons have two).
bool Barrel(const unsigned char* vehicle,const unsigned char* weapon,float* pos,float* dir) noexcept {
    if(!edf::MeanMuzzle(weapon,8,pos,dir))return false;
    const float* m=Frame(vehicle);
    const float d[3]={pos[0]-m[12],pos[1]-m[13],pos[2]-m[14]};
    return Dot(d,d)<kMuzzleReach*kMuzzleReach;
}

// Says something about a vehicle's seat once (per vehicle and seat, the last 16 of them).
void LogOnce(const void* vehicle,unsigned seat,const char* format,...) noexcept {
    static struct { const void* vehicle; unsigned seat; } logged[16]{};
    static unsigned next=0;
    for(const auto& l:logged)if(l.vehicle==vehicle && l.seat==seat)return;
    logged[next++%16]={vehicle,seat};
    char text[400]{};va_list args;va_start(args,format);vsnprintf_s(text,sizeof(text),_TRUNCATE,format,args);va_end(args);
    Log("GUNNER v=%p seat=%u: %s",vehicle,seat,text);
}

// The trigger that pulls the seat's gun: the one holding the seat's own weapon. Trigger s is that one on
// the stock Titan and 403, but not on every variant: on one (2026-10-03, a 500-round side gun) trigger 2
// held another weapon, so the aim steered by a barrel that seat 2's turn input never moved (its axis
// stayed 0 at full input for two minutes) and the right gun never got on target. A seat whose weapon is
// in no trigger is not steered at all (logged once): guessing a trigger aims one gun by another's barrel.
unsigned char* SeatTrigger(const unsigned char* vehicle,const unsigned char* seat,unsigned s) noexcept {
    const auto triggers=At<unsigned char*>(vehicle,kTriggers);
    const auto count=At<std::uint64_t>(vehicle,kTriggerCount);
    const auto weapon=SeatGun(seat);
    if(!weapon || count>kMaxTriggers || !Readable(triggers,count*kTriggerStride))return nullptr;
    for(std::uint64_t i=0;i<count;++i)
        if(At<const unsigned char*>(triggers+i*kTriggerStride,kTriggerWeapon)==weapon) {
            if(i!=s && cfg.debug)LogOnce(vehicle,s,"its gun is trigger %llu",static_cast<unsigned long long>(i));
            return triggers+i*kTriggerStride;
        }
    LogOnce(vehicle,s,"its gun is in none of the %llu triggers: left stock",static_cast<unsigned long long>(count));
    return nullptr;
}

bool ReadGun(const unsigned char* vehicle,const unsigned char* seat,unsigned s,float down,Gun& gun) noexcept {
    gun.trigger=SeatTrigger(vehicle,seat,s);
    if(!gun.trigger)return false;
    const auto ctrl=At<const unsigned char*>(gun.trigger,kTriggerCtrl);
    if(!ctrl || !Readable(ctrl,0x10) || At<std::int32_t>(ctrl,edf::kCtrlUses)==0)return false;
    gun.weapon=At<const unsigned char*>(gun.trigger,kTriggerWeapon);
    if(!Readable(gun.weapon,kAmmoGravity+4) || !Readable(gun.weapon+kWeaponMatrix,0x40) || !Barrel(vehicle,gun.weapon,gun.pos,gun.dir))return false;
    gun.shot=Shot{};
    gun.shot.speed=At<float>(gun.weapon,kAmmoSpeed);
    const float gravity=At<float>(gun.weapon,kAmmoGravity);
    if(std::isfinite(gravity) && gravity>0.0f)gun.shot.drop=gravity*down/kFramesPerSecondSq;
    const float reach=gun.shot.speed*static_cast<float>(At<std::int32_t>(gun.weapon,kAmmoAlive));
    gun.range=std::isfinite(reach) && reach>0.0f ? (reach<cfg.gunnerRange ? reach : cfg.gunnerRange) : 0.0f;
    const float blast=At<float>(gun.weapon,kAmmoExplosion);
    gun.blast=std::isfinite(blast) && blast>0.0f ? blast : 0.0f;
    return gun.shot.speed>0.01f && std::isfinite(gun.shot.speed) && gun.range>0.0f;
}

// Geometric yaw/elevation the barrel needs to hit `world` from the muzzle; false if out of reach.
bool Solve(const unsigned char* vehicle,const Gun& gun,const float* world,float* want,float& time,float& distance) noexcept {
    const float d[3]={world[0]-gun.pos[0],world[1]-gun.pos[1],world[2]-gun.pos[2]};
    float local[3];ToFrame(Frame(vehicle),d,local);
    distance=std::sqrt(Dot(d,d));
    float elevation;
    if(distance>gun.range || !Ballistic(local,gun.shot,elevation,time))return false;
    want[0]=std::atan2(local[0],local[2]);want[1]=elevation;
    return true;
}

// The seat's two aim axes and where the barrel points now (geometric, vehicle frame).
struct Aim {
    float angle[2],min[2],max[2];
    float barrel[2];
    float sign[2];     // geometric angle change per unit axis angle change, +-1
};

bool ReadAim(const unsigned char* vehicle,const unsigned char* seat,const Gun& gun,Track& track,Aim& aim) noexcept {
    const auto axes=seat+kSeatAim+kAimAxes;
    for(int a=0;a<2;++a) {
        aim.angle[a]=At<float>(axes+a*kAxisStride,kAxisAngle);
        aim.min[a]=At<float>(axes+a*kAxisStride,kAxisMin);
        aim.max[a]=At<float>(axes+a*kAxisStride,kAxisMax);
    }
    float local[3];ToFrame(Frame(vehicle),gun.dir,local);
    Angles(local,aim.barrel);
    if(!AllFinite(aim.angle,2) || !AllFinite(aim.min,2) || !AllFinite(aim.max,2) || !AllFinite(aim.barrel,2))return false;
    // Learn each axis' sign from the barrel's own motion: the pivot geometry is the model's, and a
    // wrong guess would drive the gun into its stop and keep it there.
    const float initial[2]={cfg.gunnerYawSign,cfg.gunnerPitchSign};
    for(int a=0;a<2;++a) {
        if(track.geoSign[a]==0.0f)track.geoSign[a]=initial[a]>=0.0f ? 1.0f : -1.0f;
        const float moved=aim.angle[a]-track.geoAxis[a];
        const float turned=a==0 ? Wrap(aim.barrel[a]-track.geo[a]) : aim.barrel[a]-track.geo[a];
        const float ratio=std::fabs(moved)>0.002f ? turned/moved : 0.0f;
        if(track.geoValid && std::fabs(ratio)>0.3f && std::fabs(ratio)<3.0f)
            track.geoSign[a]+=0.2f*((ratio>0.0f ? 1.0f : -1.0f)-track.geoSign[a]);
        track.geoAxis[a]=aim.angle[a];track.geo[a]=aim.barrel[a];
        aim.sign[a]=track.geoSign[a]>=0.0f ? 1.0f : -1.0f;
    }
    track.geoValid=true;
    return true;
}

bool FullAxis(const Aim& aim,int a) noexcept { return aim.max[a]-aim.min[a]>=2.0f*kPi-0.01f; }
float AxisAngle(const Aim& aim,int a,float angle) noexcept {
    if(!FullAxis(aim,a))return Clamp(angle,aim.min[a],aim.max[a]);
    const float centre=0.5f*(aim.min[a]+aim.max[a]);
    return centre+Wrap(angle-centre);
}

// Geometric error to `want`, and the axis angles that close it; false when an axis can't get there.
bool AxisTargets(const Aim& aim,const float* want,float* error,float* axis) noexcept {
    error[0]=Wrap(want[0]-aim.barrel[0]);error[1]=want[1]-aim.barrel[1];
    for(int a=0;a<2;++a) {
        axis[a]=aim.angle[a]+aim.sign[a]*error[a];
        if(FullAxis(aim,a))axis[a]=AxisAngle(aim,a,axis[a]);
        else if(axis[a]<aim.min[a]-kPitchMargin || axis[a]>aim.max[a]+kPitchMargin)return false;
    }
    return true;
}

// Keep the current target while the gun can still reach it, else the cheapest one in reach:
// distance plus the turn it costs, weighted as the flak weighs it. `only` (the player's lock in their own seat,
// designate.cpp): that one alone, nothing while the gun cannot reach it.
const void* PickGunTarget(const unsigned char* vehicle,const Gun& gun,const Aim& aim,const Nearby& nearby,const void* keep,const void* dropped,
                          const void* only,float* world) noexcept {
    const void* best=nullptr;float bestScore=0.0f;int bestAt=-1;
    if(only)keep=only;
    for(int i=0;i<nearby.count;++i) {
        const Enemy& e=*nearby.e[i];
        if(e.object==dropped || (only && e.object!=only))continue;
        float want[2],error[2],axis[2],time,distance;
        if(!Solve(vehicle,gun,e.pos,want,time,distance) || !AxisTargets(aim,want,error,axis))continue;
        if(e.object==keep){best=keep;bestAt=i;break;}
        const float score=distance*PriorityWeight(e)+(std::fabs(error[0])+std::fabs(error[1]))*cfg.slewWeight;
        if(!best || score<bestScore){best=e.object;bestScore=score;bestAt=i;}
    }
    if(!best)return nullptr;
    for(int i=0;i<=bestAt;++i)   // the object's first lock point, for a steady track
        if(nearby.e[i]->object==best){std::memcpy(world,nearby.e[i]->pos,sizeof(nearby.e[i]->pos));break;}
    return best;
}

// Lead the target by the round's flight time, with its velocity smoothed per frame as the flak does.
void LeadGun(const unsigned char* vehicle,const Gun& gun,Track& track,const void* target,const float* world,float* aim) noexcept {
    const bool same=track.target==target && GetTickCount64()-track.at<200;
    if(!same){track.frames=0;std::memset(track.vel,0,sizeof(track.vel));}
    else {
        for(int i=0;i<3;++i) {
            const float v=world[i]-track.last[i];
            track.vel[i]=track.frames ? track.vel[i]+0.3f*(v-track.vel[i]) : v;
        }
        ++track.frames;
    }
    track.target=target;std::memcpy(track.last,world,sizeof(track.last));
    std::memcpy(aim,world,12);
    if(!cfg.lead || track.frames<2)return;
    for(int pass=0;pass<2;++pass) {
        float want[2],time,distance;
        if(!Solve(vehicle,gun,aim,want,time,distance))return;
        for(int i=0;i<3;++i)aim[i]=world[i]+track.vel[i]*time;
    }
}

// The AI holds fire until the barrel is on the aim point (or within the target's blast size)
// and never fires on something so close the round would hit the tank or its own blast would.
// Once firing it holds the trigger through the wobble of a driving tank (`widen` cones), since the
// weapon reads the trigger as held only while it is pulled every frame.
bool OnTarget(const Gun& gun,const float* error,float distance,float widen) noexcept {
    const float size=gun.blast>1.5f ? gun.blast : 1.5f;
    const float cone=widen*std::fmax(cfg.gunnerCone,std::atan(size/std::fmax(distance,1.0f)));
    const float closest=std::fmax(cfg.gunnerMinDistance,1.5f*gun.blast);
    return std::fabs(error[0])<cone && std::fabs(error[1])<cone && distance>=closest;
}

// A gunner seat's aim. The player in it (this machine's): their bindings and lock (designate.cpp) and the HUD's
// readout; their lock is the only target while it lasts, and in the lead-circle mode the gun is theirs (tracked for the
// circle, never turned). An AI seat takes the lock of the player aboard first when its gun reaches it.
void SteerSeat(unsigned char* vehicle,unsigned s,Crew crew,float down,const Nearby& nearby,Track& track) noexcept {
    const auto seat=edf::SeatAt(vehicle,s);
    Gun gun{};Aim aim{};
    if(!ReadGun(vehicle,seat,s,down,gun))return;
    const auto now=GetTickCount64();
    if(!ReadAim(vehicle,seat,gun,track,aim))return;
    const bool pilot=crew==Crew::player;
    const float life=static_cast<float>(At<std::int32_t>(gun.weapon,kAmmoAlive));
    if(pilot)PilotFrame(vehicle,s,seat,gun.pos,gun.dir,gun.range);
    const void* designated=Designated(vehicle,nullptr);
    const void* only=pilot ? designated : nullptr;
    // Who turns the gun (common/edf/aimlink.h PlayerGunRule): an AI seat's this plugin; the player's as the flak's
    // (plugin.cpp Steer). A player holding the stick (no turret camera on the seat) aims by hand; letting go hands the
    // gun back, never to the target it was dragged away from.
    const edf::aimlink::PlayerGun rule=pilot ? edf::aimlink::PlayerGunRule(CameraTurret(vehicle,s),LeadCircle(),only!=nullptr)
                                             : edf::aimlink::PlayerGun{true,false};
    const float stick[2]={At<float>(seat,kStick),At<float>(seat,kStick+4)};
    const bool drag=rule.drag && cfg.dragDeadzone>0.0f
        && (std::fabs(stick[0])>cfg.dragDeadzone || std::fabs(stick[1])>cfg.dragDeadzone);
    if(drag && !track.dragging && track.target){track.dropped=track.target;track.droppedUntil=now+cfg.dragDropMs;}
    track.dragging=drag;
    if(drag){track.target=nullptr;track.at=now;PublishAim(vehicle,true,nullptr,nullptr,gun.pos,gun.dir,&gun.shot,nullptr,life);return;}
    const void* dropped=now<track.droppedUntil ? track.dropped : nullptr;
    float world[3],aimAt[3];
    const auto target=PickGunTarget(vehicle,gun,aim,nearby,designated ? designated : track.target,dropped,only,world);
    if(!target) {
        track.target=nullptr;track.firing=false;track.at=now;
        if(pilot)PublishAim(vehicle,true,nullptr,nullptr,gun.pos,gun.dir,&gun.shot,nullptr,life);
        return;
    }
    LeadGun(vehicle,gun,track,target,world,aimAt);
    track.at=now;
    if(pilot)PublishAim(vehicle,true,target,world,gun.pos,gun.dir,&gun.shot,track.vel,life);
    if(!rule.steer)return;
    float want[2],error[2],axis[2],time,distance;
    if(!Solve(vehicle,gun,aimAt,want,time,distance) || !AxisTargets(aim,want,error,axis)){track.firing=false;return;}
    // A gun EDF6VehicleCrew's stabilizer holds: its barrel (the last pose) already shows where the stabilizer holds it, so
    // the turn it still needs (axis - angle) is taken from the held axes, and the hull's turn is not counted twice.
    float held[2],hull[2];
    Stabilized(vehicle,s,aim.angle,held,hull);
    for(int a=0;a<2;++a) {
        const float delta=FullAxis(aim,a) ? Wrap(axis[a]-aim.angle[a]) : axis[a]-aim.angle[a];
        axis[a]=AxisAngle(aim,a,held[a]+delta);
    }
    float in[2];
    for(int a=0;a<2;++a) {
        const float k=track.k[a]>0.0f ? track.k[a] : kTurnPerInput;
        const bool full=FullAxis(aim,a);
        const float off=full ? Wrap(axis[a]-held[a]) : axis[a]-held[a];
        in[a]=AxisInput(track,a,axis[a],held[a],off,full,1.0f/(k*kSettleFrames),hull[a]);
    }
    if(!AllFinite(in,2))return;
    Put<float>(vehicle,kTurn+s*kTurnStride,in[0]);Put<float>(vehicle,kTurn+s*kTurnStride+4,in[1]);
    track.steered=autoturret::Frame();
    const bool fire=crew!=Crew::player && OnTarget(gun,error,distance,track.firing ? kHoldCone : 1.0f);
    if(fire) {
        // A pull the weapon has not read by the next frame means this gun is not being updated.
        if(track.firing && gun.weapon[kWeaponFire])++track.stale;
        ++track.pulls;
        PullOwned(vehicle,s,static_cast<unsigned char*>(gun.trigger));
    }
    track.firing=fire;
    if(cfg.debug && now-track.loggedAt>500) {
        track.loggedAt=now;
        Log("GUNNER v=%p seat=%u %s t=%p dist=%.0f flight=%.0ff barrel=(%.3f,%.3f) want=(%.3f,%.3f) axis=(%.3f,%.3f)->(%.3f,%.3f) sign=(%+.0f,%+.0f) k=(%.4f,%.4f) in=(%.2f,%.2f) fire=%d pulls=%u stale=%u ammo=%d",
            vehicle,s,crew==Crew::player?"player":"ai",target,distance,time,aim.barrel[0],aim.barrel[1],want[0],want[1],
            aim.angle[0],aim.angle[1],axis[0],axis[1],aim.sign[0],aim.sign[1],track.k[0],track.k[1],in[0],in[1],fire,track.pulls,track.stale,
            At<std::int32_t>(gun.weapon,kWeaponAmmo));
        track.pulls=0;track.stale=0;
    }
}

// The seat's second weapon (holder 1): the Titan M2/M3 side missiles (Weapon_VehicleShoot,
// MissileBullet01, LockonType 1). The game locks them itself (the lock tick
// 0x6963A0 in every weapon update), along the main turret's yaw (they hang on smorkG_l/r under
// cannon_main), and a pull fires as many rounds as there are locks (0x690C48: burst = +0xC68), then
// cools 720 frames; with no lock the fire step returns at once (0x690C3D). So an AI seat pulls only
// once the locks are full (+0x6DC, min(ammo, burst)) or have stopped growing for kMissileSettleMs,
// never mid-burst (+0xE18), and never with the HoldTime (300 frames) about to drop them.
// The other second weapons (the grenades, LockonType 0) leave along the main turret too, which the
// side gunner can't aim: left to the player.
// The lock count and its timing are kept on the seat's track (Track::locks...).
constexpr std::size_t kLockMax=0x6DC,kLocked=0xC68,kBurstLeft=0xE18;
constexpr std::int32_t kHoming=1;
constexpr ULONGLONG kMissileSettleMs=600,kMissileHoldMs=4000;

void FireMissiles(unsigned char* vehicle,unsigned s,Track& m) noexcept {
    const auto seat=edf::SeatAt(vehicle,s);
    if(SeatCrew(seat)!=Crew::ai || !cfg.gunnerAi)return;
    const auto holders=At<const unsigned char* const*>(seat,kSeatWeapons);
    if(At<std::uint64_t>(seat,kSeatWeaponCount)<2 || !Readable(holders,16) || !Readable(holders[1],kHolderWeapon+8))return;
    const auto weapon=At<const unsigned char*>(holders[1],kHolderWeapon);
    if(!Readable(weapon,kBurstLeft+4) || At<std::int32_t>(weapon,kLockonType)!=kHoming)return;
    const auto triggers=At<unsigned char*>(vehicle,kTriggers);
    const auto count=At<std::uint64_t>(vehicle,kTriggerCount);
    if(count>kMaxTriggers || !Readable(triggers,count*kTriggerStride))return;
    unsigned char* trigger=nullptr;
    for(std::uint64_t i=0;i<count && !trigger;++i)
        if(At<const unsigned char*>(triggers+i*kTriggerStride,kTriggerWeapon)==weapon)trigger=triggers+i*kTriggerStride;
    if(!trigger)return;
    const auto now=GetTickCount64();
    const auto locked=At<std::uint64_t>(weapon,kLocked);
    if(locked==0 || locked>64 || At<std::int32_t>(weapon,kWeaponAmmo)<=0 || At<std::int32_t>(weapon,kBurstLeft)>0) {
        m.locks=0;m.locksFirstAt=0;return;
    }
    if(locked!=m.locks){m.locksGrewAt=now;if(!m.locks)m.locksFirstAt=now;m.locks=locked;}
    const auto full=static_cast<std::uint64_t>(At<std::int32_t>(weapon,kLockMax));
    if(locked<full && now-m.locksGrewAt<kMissileSettleMs && now-m.locksFirstAt<kMissileHoldMs)return;
    PullOwned(vehicle,s,trigger);
    if(cfg.debug)Log("GUNNER v=%p seat=%u missiles: %llu locked (of %llu), ammo=%d",vehicle,s,
                     static_cast<unsigned long long>(locked),static_cast<unsigned long long>(full),At<std::int32_t>(weapon,kWeaponAmmo));
    m.locks=0;m.locksFirstAt=0;
}

// The driver's bindings and lock (designate.cpp), looking along the camera (else the main cannon's barrel).
void DriverFrame(unsigned char* vehicle) noexcept {
    const auto seat=edf::SeatAt(vehicle,0);
    const auto gun=SeatGun(seat);
    float pos[3],dir[3];
    const bool barrel=gun && Readable(gun+kWeaponMatrix,0x40) && edf::MeanMuzzle(gun,8,pos,dir);
    PilotFrame(vehicle,0,seat,barrel ? pos : nullptr,barrel ? dir : nullptr,cfg.gunnerRange);
    float world[3];
    const void* locked=Designated(vehicle,world);
    PublishAim(vehicle,false,locked,locked ? world : nullptr,nullptr,nullptr,nullptr,nullptr,0.0f);
}

bool Wanted(Crew c) noexcept { return c==Crew::player ? cfg.gunnerAssist : c==Crew::ai && cfg.gunnerAi; }

// Debug: say once per vehicle why it has no gunners to steer.
void LogSkip(const unsigned char* vehicle,const char* why) noexcept {
    if(cfg.debug)LogOnce(vehicle,0,"vt=+0x%llX skipped: %s seats=%u triggers=%llu",
        static_cast<unsigned long long>(At<const unsigned char*>(vehicle,0)-image),why,
        edf::SeatCount(vehicle),static_cast<unsigned long long>(At<std::uint64_t>(vehicle,kTriggerCount)));
}

// Online, the stock vehicle update (slot 51 0x672AD0 -> 0x62E6C0, every frame while in a session, 0x7748F0) hands
// every gunner seat that is empty, or whose rider another machine runs, to the network: 0x5FBA20 sets its aim's
// +0xC0, and the aim step (0x5FBDA0, from slot 4) then turns it by the network's input (+0xB0, which nothing on
// the host writes) instead of the seat's turn input (vehicle +0x2AA0). A real seat occupant whose machine runs
// here gets 0x5FB880 instead (stock call 0x62E72B). Apply it only for an admitted local occupant, independently
// of the driver's authority, before writing the local aim inputs.
constexpr std::size_t kAimNetwork=0xC0;
constexpr unsigned kAimLocal=0x5FB880;
void TakeFromNetwork(const unsigned char* vehicle,unsigned s,unsigned char* seat) noexcept {
    const auto aim=seat+kSeatAim;
    if(!Readable(aim,kAimNetwork+8,true) || !aim[kAimNetwork])return;
    reinterpret_cast<void(__fastcall*)(void*)>(image+kAimLocal)(aim);
    if(cfg.debug)LogOnce(vehicle,0x100+s,"seat=%u was the network's (co-op host): steered here",s);
}

// A seat other than the driver's (seat 0, the main cannon, which the driver aims) whose own gun is turned
// by its own aim controller: a gunner seat.
bool GunnerSeat(const unsigned char* seat) noexcept {
    return SeatGun(seat) && Readable(seat+kSeatAim+kAimAxes,2*kAxisStride);
}

void Gunners(unsigned char* vehicle) noexcept {
    SeeVehicle(vehicle);
    const unsigned count=edf::SeatCount(vehicle);
    if(vehicle[kDead] || !Readable(vehicle,kTurn+count*kTurnStride,true))return;
    if(count<2){LogSkip(vehicle,"no gunner seats");return;}
    Crew crew[kMaxSeats]{};
    bool gunner[kMaxSeats]{};   // seat 0 (the driver's) and seats with no gun of their own: never steered
    bool crewed=false,any=false;
    for(unsigned s=0;s<count;++s) {
        const auto seat=edf::SeatAt(vehicle,s);
        crew[s]=SeatCrew(seat);
        crewed=crewed || crew[s]!=Crew::none;
        gunner[s]=s>0 && GunnerSeat(seat);
        any=any || (gunner[s] && Wanted(crew[s]));
    }
    if(!crewed){LogSkip(vehicle,"nobody aboard");return;}   // a parked tank stays quiet
    // The player driving (seat 0, the main cannon, which is theirs): their lock is what the gunners fight; the
    // readout shows it (no mode: their gun is not the plugin's).
    if(crew[0]==Crew::player)DriverFrame(vehicle);
    if(!any)return;
    Nearby nearby;
    ScanEnemies(vehicle,cfg.gunnerRange+kMuzzleReach,nearby);
    const float down=Down(vehicle);
    for(unsigned s=1;s<count;++s) {
        if(!gunner[s] || !Wanted(crew[s]))continue;
        TakeFromNetwork(vehicle,s,edf::SeatAt(vehicle,s)); // authority belongs to this occupant, not the driver
        Track* track=TrackFor(vehicle,s,crew[s]==Crew::player);
        if(!track)continue;
        SteerSeat(vehicle,s,crew[s]==Crew::player ? Crew::player : Crew::ai,down,nearby,*track);
        if(crew[s]!=Crew::player)FireMissiles(vehicle,s,*track);
    }
}

void Run(void* vehicle) noexcept {
    ReloadConfigIfChanged();
    if(!cfg.enabled || (!cfg.gunnerAi && !cfg.gunnerAssist))return;
    __try { Gunners(static_cast<unsigned char*>(vehicle)); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// The stock input runs first in both: it zeroes or fills every seat's turn input and handles the
// riders' own triggers; the gunners only overwrite the gunner seats after it. Slot 55 with all four
// register arguments (edf::VehicleInputFn), as EDF6VehicleCrew's hook on the same slot forwards them.
void __fastcall Hook403(void* vehicle,std::uintptr_t hasInput,void* r8,void* r9) { ReleaseGunnerPulls(vehicle);next403(vehicle,hasInput,r8,r9);Run(vehicle); }
void __fastcall Hook404(void* vehicle,std::uintptr_t hasInput,void* r8,void* r9) { ReleaseGunnerPulls(vehicle);next404(vehicle,hasInput,r8,r9);Run(vehicle); }

struct Signature { std::size_t rva; unsigned char bytes[27]; std::size_t size; };

// The code the gunners depend on; any mismatch leaves the tanks stock.
const Signature kSignatures[]={
    {kTank403Input,{0x48,0x89,0x5C,0x24,0x20,0x55,0x48,0x83,0xEC,0x30,0x0F,0xB6,0xDA},13},
    {kTank404Input,{0x48,0x89,0x5C,0x24,0x20,0x55,0x48,0x83,0xEC,0x30,0x0F,0xB6,0xDA},13},
    {0x5FEC44,{0x48,0x8D,0xBD,0xA4,0x2A,0x00,0x00},7},                    // 403: lea rdi,[rbp+2AA4] (turn)
    {0x5FEC88,{0x48,0x8B,0x85,0x38,0x06,0x00,0x00},7},                    // 403: mov rax,[rbp+638] (triggers)
    {0x5FEC98,{0xF3,0x0F,0x10,0x86,0xD0,0x02,0x00,0x00},8},               // 403: movss xmm0,[rsi+2D0] (stick)
    {0x5FECA5,{0x49,0x81,0xC6,0x40,0x03,0x00,0x00},7},                    // 403: add r14,340 (seat stride)
    {0x5FFCB4,{0x48,0x8D,0xB5,0xA4,0x2A,0x00,0x00},7},                    // 404: lea rsi,[rbp+2AA4]
    {0x5FFCF8,{0x48,0x8B,0x85,0x38,0x06,0x00,0x00},7},                    // 404: mov rax,[rbp+638]
    {0x5FFD3A,{0x49,0x81,0xC6,0x40,0x03,0x00,0x00},7},                    // 404: add r14,340
    {0x5FEF19,{0x4B,0x8B,0x84,0x34,0xE0,0x00,0x00,0x00},8},               // 403 slot 4: seat+E0 aim
    {0x5FEF21,{0x48,0x8D,0x95,0xAA,0x02,0x00,0x00},7},                    // 403 slot 4: lea rdx,[rbp+2AA]
    {0x600049,{0x4B,0x8B,0x84,0x34,0xE0,0x00,0x00,0x00},8},               // 404 slot 4: seat+E0 aim
    {0x600051,{0x48,0x8D,0x95,0xAA,0x02,0x00,0x00},7},                    // 404 slot 4: lea rdx,[rbp+2AA]
    {kAimLocal,{0x8B,0x41,0x58,0x89,0x81,0xA0,0x00,0x00,0x00,0x8B,0x41,0x18},12},  // the aim made this machine's
    {0x5FBDA9,{0x80,0xB9,0xC0,0x00,0x00,0x00,0x00},7},                    // the aim step: cmp byte [rcx+C0],0
    {0x62E72B,{0xE8,0x50,0xD1,0xFC,0xFF},5},                              // the update's call of it for a local rider
    {kPullTrigger,{0x48,0x8B,0x41,0x08,0x48,0x85,0xC0,0x74,0x11,0x83,0x78,0x08,0x00,0x74,0x0B,
                   0x48,0x8B,0x41,0x10,0xC6,0x80,0x39,0x01,0x00,0x00,0x01,0xC3},27},
    {0x6330A9,{0x48,0x8B,0x86,0x48,0x06,0x00,0x00},7},                    // mov rax,[rsi+648] (trigger count)
    {0x63407F,{0x49,0x89,0xAE,0x60,0x02,0x00,0x00},7},                    // mov [r14+260],rbp (seat rider)
    {0x572E49,{0x48,0x83,0xB9,0x40,0x03,0x00,0x00,0x00},8},               // cmp qword [rcx+340],0 (pad)
    {0x572EFF,{0x44,0x38,0xAE,0x54,0x03,0x00,0x00},7},                    // cmp [rsi+354],r13b (player)
    {0x6969AB,{0x0F,0x10,0x41,0x10},4},                                   // muzzle local at +0x10
    {0x696B70,{0x8B,0x89,0xE0,0x00,0x00,0x00,0x85,0xC9,0x0F,0x84,0xD9,0x01,0x00,0x00,0x83,0xE9,0x01},17},   // mode 0 / 1 dispatch
    {0x696DB3,{0x41,0x0F,0x10,0x00},4},                                   // mode 0: rows from the weapon matrix
    {0x6904BE,{0x48,0x8D,0x93,0x50,0x01,0x00,0x00},7},                    // ... passed as weapon+0x150
    {0x633E03,{0x0F,0x29,0x82,0x50,0x01,0x00,0x00},7},                    // weapon+0x150 <- aim bone rows
    {0x69168B,{0x44,0x0F,0x10,0x4B,0x70},5},                              // shot direction: row 2 (+0x70)
    {0x696A03,{0x48,0x8B,0x11},3},                                        // muzzle bone at +0
    {0x696A28,{0x0F,0x10,0xAA,0xC0,0x00,0x00,0x00},7},                    // bone rows +B0..+E0
    {0x696A2F,{0x0F,0x10,0xB2,0xB0,0x00,0x00,0x00},7},
    {0x696A36,{0x0F,0x10,0xBA,0xD0,0x00,0x00,0x00},7},
    {0x696A3D,{0x44,0x0F,0x10,0x82,0xE0,0x00,0x00,0x00},8},
    {0x697019,{0x48,0xF7,0xB1,0xE0,0x01,0x00,0x00},7},                    // fire: muzzle count +1E0
    {0x697022,{0x48,0x69,0xD8,0xF0,0x00,0x00,0x00,0x48,0x03,0x99,0xD0,0x01,0x00,0x00},14},   // stride F0, array +1D0
    // weapon user: seats at iface+0x4E8 (vehicle+0x608), count +0x4F8, stride 0x340
    {kWeaponUser,{0x41,0x57,0x48,0x83,0xEC,0x30,0x4C,0x69,0x99,0xF8,0x04,0x00,0x00,0x40,0x03,0x00,0x00,
                  0x4C,0x8B,0x81,0xE8,0x04,0x00,0x00,0x4C,0x8B,0xD2},27},
    // ... holders seat+0xC8, count +0xD8, the weapon at holder+0x10
    {0x62D980,{0x4D,0x8B,0x88,0xD8,0x00,0x00,0x00,0x49,0x8B,0xD7,0x4D,0x85,0xC9,0x74,0x1C,0x49,0x8B,0x80,
               0xC8,0x00,0x00,0x00,0x48,0x8B,0x08,0x4C,0x3B},27},
    {0x690C0E,{0xFF,0x50,0x58,0x48,0x85,0xC0,0x0F,0x84,0x92,0x00,0x00,0x00},12},   // fire: null user -> no shot
};

bool CheckGunnerProfile() noexcept {
    __try {
        for(const auto& s:kSignatures)if(!edf::Matches(image,s.rva,s.bytes,s.size))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// Hooks one slot on top of whatever it holds: the stock function (checked by the signatures above), or
// another plugin's hook that ends in it (EDF6VehicleCrew chains the same input slots on the first mission
// frame), so the load order does not matter. `next` is what the hook calls through.
template<class Fn> bool Chain(unsigned vtable,std::size_t index,unsigned stock,Fn& next,Fn hook,const char* name) noexcept {
    const auto slot=reinterpret_cast<void**>(image+vtable)+index;
    void* const current=*slot;
    if(!edf::ChainVtableSlot(slot,reinterpret_cast<void*>(hook),reinterpret_cast<void**>(&next)))return false;
    if(current!=image+stock)Log("HOOK gunners %s: chaining onto %p (another plugin)",name,current);
    return true;
}
}  // namespace

int HookGunners() noexcept {
    if(!image || !CheckGunnerProfile()){Log("HOOK gunners: unexpected layout, tank gunners off");return 0;}
    const bool tank=Chain(kTank403Vtable,edf::kSlotInput,kTank403Input,next403,&Hook403,"403 input");
    const bool titan=Chain(kTank404Vtable,edf::kSlotInput,kTank404Input,next404,&Hook404,"404 input");
    const bool user403=Chain(kUserIface403Vtable,kUserSlot,kWeaponUser,nextUser[0],&WeaponUser<0>,"403 weapon user");
    const bool user404=Chain(kUserIface404Vtable,kUserSlot,kWeaponUser,nextUser[1],&WeaponUser<1>,"404 weapon user");
    Log("HOOK gunners tank403=%d titan404=%d user=%d/%d",tank,titan,user403,user404);
    return static_cast<int>(tank)+static_cast<int>(titan)+static_cast<int>(user403)+static_cast<int>(user404);
}
}  // namespace autoturret
