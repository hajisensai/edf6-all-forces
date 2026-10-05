// The Primer swarm (docs/swarm-plan.md): an ENEMY made of the plugin's 506 bodies, in three sizes (kSizes): a mission
// places a huge core (EDF6VC_SWARM_HUGE, mark 7014), a core (EDF6VC_SWARM_CORE, 7012) or a lone drone
// (EDF6VC_SWARM_UNIT, 7013), CreateFriend like any placed jet. The first time a core is flown it goes over to the
// enemy's team and brings its drones (EDF6VC_SWARM_UNIT, spawned on the enemy's team), which fly as one craft with
// it, a flying wing round the core (the huge one's twice as wide); a lone drone flies as a scattered one:
//  - combined: the core circles the player SwarmRange out and SwarmHeight up, now and then passing low over them;
//    each drone holds its slot (the core's velocity plus a pull onto the slot), its nose (pitch too) on the
//    player, and fires its guns in bursts when on them. The core's cannons fire whenever it is on them. A swarm is
//    one flight: its rounds pass through its own (jet_hooks.cpp).
//  - scattered: the core shot down (or gone), every drone left circles the player on a ring of its own.
//  - wreck (the death behaviour): the stock damage (message 0x10000000, 0x547C30) clamps the HP into [+0x2F0 floor,
//    max] (0x548172) and dies only at HP <= 0 (0x54840B). While the plugin flies a swarm body, its damage message
//    is handled with the floor at kFloorHp (SwarmMessage, through body506's message hook, put back to what it was
//    right after), so a killing hit leaves it at the floor. Seen there it becomes a wreck: where the player is now
//    is its mark, its HP kWreckHpShare of its max, and it tumbles straight at the mark (no homing); on the mark,
//    the ground, stopped, or after kWreckMaxMs its charge (weapon 2, a point charge on the enemy's team) goes off
//    and it is gone (Blast). Shot again on the way, it goes down as the stock 506 does: no blast. Not flown (the
//    plugin off, its pilot gone, a step faulting) its floor stays the stock one: it dies as any 506.
// Its team (docs/swarm-team-re.md): a vehicle's team is its riders' (the vehicle update 0x630250 sets it from its
// seats every frame, 5 with none), and RideAi's dummy pilot is made a friend (team 2, 0x6331CC) whatever the vehicle
// was. So the pilot goes to the enemy's team (unregistered, as RideAi left it), then the vehicle (SwarmTeam), when
// it is made and every frame after: its rounds and its charge take the vehicle's team (weapon +0x214, 0x630421).
// Only the local player is aimed at (player.pos). Game thread, under the input hook's guard.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "jet_internal.h"
#include "swarm_pose.h"
#include <cwchar>

namespace crew {
namespace jet {
constexpr std::size_t kHpFloor=0x2F0;
constexpr float kFloorHp=1.0f;
constexpr ULONGLONG kFlownMs=500;      // a body flown within this long has its damage held at the floor
namespace {
constexpr std::size_t kOwnTeam=0x318;
constexpr float kWreckHpShare=0.04f;    // a wreck's HP: one more hit brings it down
// Drones: at most kMaxUnits (SwarmUnits), kSpawnPerFrame a frame, once the core is kSpawnClear over the ground
// (or kSpawnWaitMs on), each kUnitClear over the ground at least.
constexpr int kMaxUnits=16,kSpawnPerFrame=3;
constexpr float kSpawnClear=30.0f,kUnitClear=15.0f;
constexpr ULONGLONG kSpawnWaitMs=4000;
// The swarm takes at most kSwarmEntries of the jets' table (kMaxJets): the friends' jets and drones keep the rest.
// A core that could not bring its drones (the table full) tries again kRetryMs on.
constexpr int kSwarmEntries=kMaxJets-16;
constexpr ULONGLONG kRetryMs=5000;
// The formation: slots (right, up, forward) m in the core's heading, filled in this order. A flying wing: the
// core in its middle (41.5 m across, its box 20.75 m out to each side, z -17.3..7), the drones (9.4 m) 12 m apart
// at least, none inside the core's box.
constexpr float kSlots[kMaxUnits][3]={
    {30.0f,0.0f,-4.0f},{-30.0f,0.0f,-4.0f},{44.0f,-2.0f,-14.0f},{-44.0f,-2.0f,-14.0f},
    {0.0f,-8.0f,22.0f},{0.0f,10.0f,-46.0f},{58.0f,-4.0f,-24.0f},{-58.0f,-4.0f,-24.0f},
    {16.0f,6.0f,-32.0f},{-16.0f,6.0f,-32.0f},{26.0f,-10.0f,20.0f},{-26.0f,-10.0f,20.0f},
    {72.0f,-6.0f,-34.0f},{-72.0f,-6.0f,-34.0f},{30.0f,6.0f,-46.0f},{-30.0f,6.0f,-46.0f},
};
constexpr float kFormGain=2.0f;         // 1/s: the pull onto the slot
constexpr float kFormCatch=45.0f;       // m/s at most of that pull
constexpr float kFormTop=90.0f;         // m/s at most in all
// The core: round the player at kOrbitSpeed; every kPassEveryMs a pass at kPassSpeed over them to as far out on
// the other side, kPassLow of its height, ended there or after kPassMaxMs. Its station kStationClear over the ground.
constexpr float kOrbitSpeed=18.0f,kPassSpeed=38.0f,kPassLow=0.55f,kPassDone=40.0f,kStationClear=40.0f;
constexpr ULONGLONG kPassEveryMs=30000,kPassMaxMs=20000;
// Scattered: a ring round the player kRingBase out plus kRingStep per slot%4, kRingUp plus kRingUpStep per slot%3 up,
// at kRingSpeed, every other one the other way round.
constexpr float kRingBase=90.0f,kRingStep=20.0f,kRingUp=30.0f,kRingUpStep=8.0f,kRingSpeed=22.0f;
// Aim: the nose (pitch too, at most kMaxPitch) on the player's chest (kChest over their feet) within kAimReach
// times the gun's reach, else along the core's heading; it fires within kFireCone of them and the reach. A drone's
// guns go in bursts (kBurstOnMs of every kBurstMs, its slot's phase); the core's cannons whenever they bear.
constexpr float kMaxPitch=0.8f,kChest=1.2f,kAimReach=1.4f,kFireCone=0.09f,kAimGain=4.0f;
constexpr float kUnitReach=400.0f,kCoreReach=580.0f;   // pylib/vcobjects.py SWARM_GUN_FILES: 6x70, 5x120 m
constexpr ULONGLONG kBurstOnMs=1600,kBurstMs=4000,kSlotPhaseMs=733;
// A wreck: it speeds up at kWreckAccel to kWreckSpeed (a drone's, the core's), tumbling; its charge goes off
// within kWreckTrigger of its mark, kWreckGround over the ground (its box's widest half and a margin: a body
// resting on its side has its origin that high: drone 4.7 m, core 20.75 m out, 10.9 m under its origin), stopped
// (its real speed under kWreckStuck of what it is told for kWreckStuckMs, whichever way it is held), or
// kWreckMaxMs on.
constexpr float kWreckAccel=35.0f,kWreckStuck=0.3f;
constexpr ULONGLONG kWreckMaxMs=12000,kWreckStuckMs=400,kWreckSettleMs=800,kLogMs=2000;

// What differs by size: the drone (a core's member or alone), the core (the Imperial drone x 0.5: box 20.75 m out
// to each side, 10.9 m under its origin) and the huge core (x 1: 41.5 m, 21.75 m). Slots: kSlots times this
// (the core's box is twice the size). Orbit / height: SwarmRange / SwarmHeight times these. The wreck's top speed,
// how near its mark and how low over the ground its charge goes off: a drone at its widest half and a margin, a
// core just under where it rests level (more would burst it in the air); one that came down on its side is
// caught as stopped.
struct Size { float slots,spawnClear,orbit,height,wreckSpeed,wreckTrigger,wreckGround; };
constexpr Size kSizes[3]={
    {1.0f,0.0f,1.0f,1.0f,55.0f,8.0f,6.0f},      // drone
    {1.0f,30.0f,1.0f,1.0f,40.0f,16.0f,14.0f},   // core
    {2.0f,50.0f,1.6f,1.5f,30.0f,30.0f,24.0f},   // huge core
};
bool IsCore(const Jet& j) noexcept { return j.role==Role::swarmCore || j.role==Role::swarmHuge; }
const Size& SizeOf(const Jet& j) noexcept { return kSizes[j.role==Role::swarmHuge ? 2 : j.role==Role::swarmCore ? 1 : 0]; }

// The player's chest, if they were seen lately (`at`); false with no player.
bool Target(float* at,ULONGLONG ms) noexcept {
    if(!player.at || ms-player.at>2000)return false;
    at[0]=player.pos[0];at[1]=player.pos[1]+kChest;at[2]=player.pos[2];
    return true;
}

// `p` raised to `clear` over the ground under it (terrain, buildings).
void OverGround(float* p,float clear) noexcept {
    const float top[3]={p[0],p[1]+600.0f,p[2]},bottom[3]={p[0],p[1]-1500.0f,p[2]};
    float hit[3];
    if(MapRay(top,bottom,hit)>=0.0f && p[1]<hit[1]+clear)p[1]=hit[1]+clear;
}

// v's heading (its forward, level).
void Heading(const unsigned char* v,float* fwd) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    fwd[0]=m[8];fwd[1]=0.0f;fwd[2]=m[10];
    if(!Normalize(fwd)){fwd[0]=0;fwd[2]=1;}
}

// The world point of slot `s` of a core at `at` heading `fwd`, its slots `scale` times kSlots.
void SlotAt(const float* at,const float* fwd,int s,float scale,float* out) noexcept {
    const float* o=kSlots[s];
    const float right[3]={fwd[2],0.0f,-fwd[0]};
    for(int i=0;i<3;++i)out[i]=at[i]+(right[i]*o[0]+fwd[i]*o[2])*scale;
    out[1]+=o[1]*scale;
}

// The core entry of drone `j` (alive, flown, not a wreck), or nullptr.
Jet* CoreOf(const Jet& j) noexcept {
    if(!j.swarm.core)return nullptr;
    for(auto& c:jets)
        if(c.ref && c.ref.ctrl==j.swarm.core && IsCore(c) && !c.swarm.wreck && !c.reap && Alive(c.ref) && !c.Vehicle()[kDead])return &c;
    return nullptr;
}

bool OnEnemySide(const unsigned char* o) noexcept {
    return At<std::int32_t>(o,kTeam)==kTeamEnemy && At<std::int32_t>(o,kOwnTeam)==kTeamEnemy;
}

// Its HP times SwarmHpScale (max and current), the floor set, its team the enemy's, and a flight: once, on its
// first frame. A core gets a flight of its own (its drones join it), and its orbit starts where it is.
void Init(Jet& j,unsigned char* v,const float* pos,ULONGLONG ms) noexcept {
    j.swarm.init=true;
    SwarmTeam(v);
    const float scale=Cfg().swarmHpScale,max=At<float>(v,kHpMax)*scale;
    if(max>kFloorHp*2.0f){Put<float>(v,kHpMax,max);Put<float>(v,kHp,max);}
    j.mode=Mode::patrol;
    float at[3];
    const bool seen=Target(at,ms);
    if(IsCore(j)) {
        JoinFlight(j,NewFlight());
        j.swarm.passAt=ms;
        j.swarm.orbit=seen ? std::atan2(pos[2]-at[2],pos[0]-at[0]) : 0.0f;
    } else if(!j.swarm.core) {
        // A lone drone a mission placed: scattered from the start, a ring of its own (by its address).
        j.swarm.scattered=true;
        j.swarm.slot=static_cast<int>((reinterpret_cast<std::uintptr_t>(v)>>4)%kMaxUnits);
        j.swarm.orbit=seen ? std::atan2(pos[2]-at[2],pos[0]-at[0]) : 0.0f;
        JoinFlight(j,NewFlight());
    }
    Log("SWARM v=%p %s: enemy team %d, hp %.0f, flight %u",v,KindOf(j).name,At<std::int32_t>(v,kTeam),At<float>(v,kHp),j.flight);
    Publish(true);
}

int SwarmEntries() noexcept {
    int n=0;
    for(const auto& o:jets)if(o.ref && IsSwarm(o))++n;
    return n;
}

// A core brings its drones: at most kSpawnPerFrame this frame, until SwarmUnits are out (none at all without the
// drone body; with the table full, again kRetryMs on).
void Spawn(Jet& c,const unsigned char* v,const float* pos,ULONGLONG ms) noexcept {
    const Size& size=SizeOf(c);
    const int units=c.role==Role::swarmHuge ? Cfg().swarmHugeUnits : Cfg().swarmUnits;
    const int want=units<kMaxUnits ? units : kMaxUnits;
    if(c.swarm.noUnits || c.swarm.spawned>=want || ms<c.swarm.retryAt)return;
    const float clear=GroundClearance(pos);
    if(clear!=kNoGround && clear<size.spawnClear && ms-c.bornAt<kSpawnWaitMs)return;
    if(!SpawnReady() || !Preloaded(Body::swarmUnit)) {
        c.swarm.noUnits=true;
        Log("SWARM core %p: no drones (EDF6VC_SWARM_UNIT.SGO not preloaded: run the installer)",v);
        return;
    }
    float fwd[3];
    Heading(v,fwd);
    for(int n=0;n<kSpawnPerFrame && c.swarm.spawned<want;++n) {
        if(SwarmEntries()>=kSwarmEntries || !SlotFree()) {
            c.swarm.retryAt=ms+kRetryMs;
            Log("SWARM core %p: no room in the jet table (%d swarm entries): %d drones so far, again in %llu ms",v,SwarmEntries(),
                c.swarm.spawned,kRetryMs);
            return;
        }
        const int s=c.swarm.spawned++;
        float at[3];
        SlotAt(pos,fwd,s,size.slots,at);
        OverGround(at,kUnitClear);
        alignas(16) float m[16];
        Facing(fwd,at,m);
        unsigned char* const u=SpawnJet(Body::swarmUnit,m,kTeamEnemy);
        Jet* const d=u ? NewEntry(u,ms) : nullptr;
        if(!d) {
            if(u)reinterpret_cast<DeleteFn>(image+kDelete)(u);
            c.swarm.noUnits=true;
            Log("SWARM core %p: drone %d not made: no more",v,s);
            return;
        }
        d->swarm.core=c.ref.ctrl;d->swarm.slot=s;
        std::memcpy(d->anchor,at,12);
        std::memcpy(d->m.vel,c.m.vel,12);
        JoinFlight(*d,c.flight);
        Init(*d,u,at,ms);   // an enemy from now on (SpawnJet's RideAi seated a friend), its HP scaled
        Log("SWARM core %p: drone %d v=%p at (%.0f,%.0f,%.0f)",v,s,u,at[0],at[1],at[2]);
    }
    Publish(true);
}

// The nose on `aim` (pitch at most kMaxPitch) when there is one, else along `fwd`; up as level as that allows.
void PointAt(Jet& j,const unsigned char* v,const float* pos,const float* aim,bool hasAim,const float* fwd) noexcept {
    float nose[3]={fwd[0],0.0f,fwd[2]};
    if(hasAim) {
        float to[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
        const float flat=std::sqrt(to[0]*to[0]+to[2]*to[2]);
        if(flat>1.0f) {
            const float pitch=Clamp(std::atan2(to[1],flat),-kMaxPitch,kMaxPitch);
            nose[0]=to[0]/flat*std::cos(pitch);nose[1]=std::sin(pitch);nose[2]=to[2]/flat*std::cos(pitch);
        }
    }
    if(!Normalize(nose)){nose[0]=0;nose[1]=0;nose[2]=1;}
    float up[3]={0.0f,1.0f,0.0f};
    const float along=Dot(up,nose);
    for(int i=0;i<3;++i)up[i]-=nose[i]*along;
    if(!Normalize(up)){up[0]=0;up[1]=1;up[2]=0;}
    BodyAttitude(v,nose,up,kAimGain,KindOf(j).roll,j.m.omega);
}

// A drone's moving parts (src/swarm_pose.h): its wings beat, its abdomen sways, armed (`arm`) curls under it,
// a wreck's hang. Its bone records are looked up by name in the model instance (veh+0xEE0) and found again when
// its bone array changes; each frame local = R x bind is written into them (+0x70), as the jets' elevons are.
// Every part found or none posed (a drone in another model: logged once).
static_assert(swarm::kDroneBoneCount<=8,"SwarmState holds 8 parts");
void SwarmPose(Jet& j,unsigned char* v,bool arm,float dt,ULONGLONG ms) noexcept {
    const unsigned char* const inst=v+kModelInst506;
    const auto bones=At<const unsigned char*>(inst,kInstBones506);
    if(!bones)return;
    if(bones!=j.swarm.poseModel) {
        j.swarm.poseModel=bones;j.swarm.posed=true;
        for(int i=0;i<swarm::kDroneBoneCount;++i) {
            j.swarm.poseRec[i]=BoneRecord506(inst,swarm::kDroneBones[i].name);
            if(!j.swarm.poseRec[i]){j.swarm.posed=false;continue;}
            std::memcpy(j.swarm.poseBind[i],j.swarm.poseRec[i]+kBoneLocal506,64);
        }
        if(!j.swarm.posed)Log("SWARM v=%p: its model has not the dragonfly's parts: not posed",v);
    }
    if(!j.swarm.posed)return;
    const swarm::DroneInput in{static_cast<float>(ms%600000)*0.001f,arm,j.swarm.wreck};
    j.swarm.curl=swarm::CurlStep(j.swarm.curl,in,dt);
    float angles[swarm::kDroneBoneCount];
    swarm::DroneAngles(in,j.swarm.curl,angles);
    for(int i=0;i<swarm::kDroneBoneCount;++i) {
        alignas(16) float local[16];
        swarm::TurnLocal(j.swarm.poseBind[i],swarm::kDroneBones[i].axis,angles[i],local);
        std::memcpy(j.swarm.poseRec[i]+kBoneLocal506,local,64);
    }
}

// The guns (0x2020): on the target within the reach and kFireCone, a drone in its burst with its abdomen curled
// (its warning: SwarmPose).
void Guns(Jet& j,unsigned char* v,const float* pos,const float* aim,bool hasAim,ULONGLONG ms) noexcept {
    v[kFireMissile]=0;
    bool fire=false;
    float d=0.0f,off=0.0f;
    if(hasAim && Cfg().swarmFire) {
        float to[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
        d=Len(to);
        const float* m=reinterpret_cast<const float*>(v+kMatrix);
        float fwd[3]={m[8],m[9],m[10]};
        if(Normalize(to) && Normalize(fwd))off=std::acos(Clamp(Dot(to,fwd),-1.0f,1.0f));
        const bool burst=IsCore(j) || ((ms+static_cast<ULONGLONG>(j.swarm.slot)*kSlotPhaseMs)%kBurstMs<kBurstOnMs &&
                                       (!j.swarm.posed || j.swarm.curl>=swarm::kCurlFire));
        fire=burst && d<(IsCore(j) ? kCoreReach : kUnitReach) && off<kFireCone;
    }
    v[kFireGun]=fire ? 1 : 0;
    if(Cfg().debug && ms-j.swarm.fireLogAt>kLogMs) {
        j.swarm.fireLogAt=ms;
        Log("SWARM v=%p %s%s hp=%.0f/%.0f target %.0f m off %.2f rad fire=%d vel=(%.0f,%.0f,%.0f)",v,KindOf(j).name,
            j.swarm.scattered ? " scattered" : "",At<float>(v,kHp),At<float>(v,kHpMax),d,off,fire,j.m.vel[0],j.m.vel[1],j.m.vel[2]);
    }
}

// The core's station: round the player (or where it was placed), now and then a pass over them.
void CoreFly(Jet& j,const Kind& k,unsigned char* v,const float* pos,const float* aim,bool hasAim,float dt,ULONGLONG ms) noexcept {
    const float r=Cfg().swarmRange*SizeOf(j).orbit,h=Cfg().swarmHeight*SizeOf(j).height;
    const float* centre=hasAim ? player.pos : j.anchor;
    float goal[3],speed=kOrbitSpeed;
    if(j.swarm.pass) {
        if(HorizDist(pos,j.swarm.passTo)<kPassDone || ms-j.swarm.passAt>kPassMaxMs) {
            j.swarm.pass=false;
            j.swarm.orbit=std::atan2(pos[2]-centre[2],pos[0]-centre[0]);
            Log("SWARM core %p: pass done",v);
        }
    } else if(hasAim && ms-j.swarm.passAt>kPassEveryMs) {
        // Over the player to as far out on the other side, low.
        float across[3]={centre[0]-pos[0],0.0f,centre[2]-pos[2]};
        if(Normalize(across)) {
            j.swarm.pass=true;j.swarm.passAt=ms;
            j.swarm.passTo[0]=centre[0]+across[0]*r;j.swarm.passTo[2]=centre[2]+across[2]*r;
            j.swarm.passTo[1]=centre[1]+h*kPassLow;
            Log("SWARM core %p: pass over the player",v);
        }
    }
    if(j.swarm.pass) {
        std::memcpy(goal,j.swarm.passTo,12);
        speed=kPassSpeed;
    } else {
        j.swarm.orbit+=kOrbitSpeed/r*dt;
        goal[0]=centre[0]+std::cos(j.swarm.orbit)*r;goal[1]=centre[1]+h;goal[2]=centre[2]+std::sin(j.swarm.orbit)*r;
        // Not far behind its point on the circle: it flies there at its cruise, then keeps up at kOrbitSpeed.
        if(HorizDist(pos,goal)>r*0.25f)speed=k.cruise;
    }
    OverGround(goal,kStationClear);
    Hover(j,k,v,pos,goal,hasAim ? aim : goal,speed,kHoverClimb,dt);
}

// A drone in the formation: the core's velocity plus the pull onto its slot.
void Hold(Jet& j,const Jet& core,const float* pos) noexcept {
    const unsigned char* cv=core.Vehicle();
    const float* cp=reinterpret_cast<const float*>(cv+kPosition);
    float fwd[3],slot[3];
    Heading(cv,fwd);
    SlotAt(cp,fwd,j.swarm.slot,SizeOf(core).slots,slot);
    float pull[3]={(slot[0]-pos[0])*kFormGain,(slot[1]-pos[1])*kFormGain,(slot[2]-pos[2])*kFormGain};
    const float p=Len(pull);
    if(p>kFormCatch)for(int i=0;i<3;++i)pull[i]*=kFormCatch/p;
    for(int i=0;i<3;++i)j.m.vel[i]=core.m.vel[i]+pull[i];
    const float s=Len(j.m.vel);
    if(s>kFormTop)for(int i=0;i<3;++i)j.m.vel[i]*=kFormTop/s;
}

// A scattered drone: round the player on its own ring.
void Ring(Jet& j,const Kind& k,unsigned char* v,const float* pos,const float* aim,bool hasAim,float dt) noexcept {
    const int s=j.swarm.slot;
    const float r=kRingBase+kRingStep*static_cast<float>(s%4),way=s%2 ? -1.0f : 1.0f;
    const float* centre=hasAim ? player.pos : j.anchor;
    j.swarm.orbit+=way*kRingSpeed/r*dt;
    float goal[3]={centre[0]+std::cos(j.swarm.orbit)*r,centre[1]+kRingUp+kRingUpStep*static_cast<float>(s%3),
                   centre[2]+std::sin(j.swarm.orbit)*r};
    OverGround(goal,kUnitClear);
    Hover(j,k,v,pos,goal,hasAim ? aim : goal,k.cruise,kHoverClimb,dt);
}

// Shot down to its floor: a wreck from now on (see the file's head).
void Wreck(Jet& j,unsigned char* v,const float* pos,ULONGLONG ms) noexcept {
    j.swarm.wreck=true;j.swarm.wreckAt=ms;
    float at[3];
    if(Target(at,ms)){at[1]-=kChest;std::memcpy(j.swarm.aimAt,at,12);}
    else {   // no player: straight down
        std::memcpy(j.swarm.aimAt,pos,12);
        const float clear=GroundClearance(pos);
        j.swarm.aimAt[1]-=clear!=kNoGround && clear>0.0f ? clear : 50.0f;
    }
    const float max=At<float>(v,kHpMax),hp=max*kWreckHpShare;
    Put<float>(v,kHp,hp>kFloorHp ? hp : kFloorHp);
    // A tumble of its own (a pseudo-random sign and rate per body).
    const unsigned seed=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(v)>>4)^static_cast<unsigned>(ms);
    for(int i=0;i<3;++i)j.swarm.spin[i]=(((seed>>(i*5))&1) ? 1.0f : -1.0f)*(1.5f+static_cast<float>((seed>>(i*5+1))&7)*0.4f);
    v[kFireGun]=0;v[kFireMissile]=0;
    Log("SWARM v=%p %s shot down: wreck diving at (%.0f,%.0f,%.0f), %.0f m off",v,KindOf(j).name,j.swarm.aimAt[0],j.swarm.aimAt[1],
        j.swarm.aimAt[2],std::sqrt((pos[0]-j.swarm.aimAt[0])*(pos[0]-j.swarm.aimAt[0])+(pos[1]-j.swarm.aimAt[1])*(pos[1]-j.swarm.aimAt[1])+
                                    (pos[2]-j.swarm.aimAt[2])*(pos[2]-j.swarm.aimAt[2])));
}

// A wreck's frame: straight at its mark, tumbling; its charge at the mark, the ground, a wall or the time's end.
void WreckFly(Jet& j,unsigned char* v,const float* pos,float dt,ULONGLONG ms) noexcept {
    const Size& size=SizeOf(j);
    const bool walled=Sense(j,pos,ms);
    float to[3]={j.swarm.aimAt[0]-pos[0],j.swarm.aimAt[1]-pos[1],j.swarm.aimAt[2]-pos[2]};
    const float d=Len(to),clear=GroundClearance(pos);
    // Stopped: held by the ground, a roof or a wall whichever way (Sense's slide misses a body held from below).
    const bool slow=ms-j.swarm.wreckAt>kWreckSettleMs && j.m.real<Len(j.m.vel)*kWreckStuck;
    if(!slow)j.swarm.slowSince=0;
    else if(!j.swarm.slowSince)j.swarm.slowSince=ms;
    const bool stuck=slow && ms-j.swarm.slowSince>=kWreckStuckMs;
    const char* why=d<size.wreckTrigger ? "on its mark" : clear!=kNoGround && clear<size.wreckGround ? "the ground" :
                    walled ? "blocked" : stuck ? "stopped" : ms-j.swarm.wreckAt>kWreckMaxMs ? "time" : nullptr;
    if(why) {
        Log("SWARM v=%p %s wreck bursts (%s), %.0f m from its mark",v,KindOf(j).name,why,d);
        Detonate(j,nullptr,d,ms);
        Blast(j,v,ms);
        return;
    }
    if(!IsCore(j))SwarmPose(j,v,false,dt,ms);
    Normalize(to);
    const float s=Clamp(Len(j.m.vel)+kWreckAccel*dt,0.0f,size.wreckSpeed);
    for(int i=0;i<3;++i){j.m.vel[i]=to[i]*s;j.m.omega[i]=j.swarm.spin[i];}
    v[kFireGun]=0;v[kFireMissile]=0;
}
}  // namespace

void SwarmTeam(unsigned char* v) noexcept {
    for(unsigned i=0;i<SeatCount(v);++i) {
        unsigned char* const seat=SeatAt(v,i);
        if(SeatRider(seat)!=Rider::dummy)continue;
        const auto rider=At<unsigned char*>(seat,kSeatRider);
        if(rider && !OnEnemySide(rider))reinterpret_cast<SetTeamFn>(image+kSetTeam)(rider,kTeamEnemy,false);
    }
    if(!OnEnemySide(v))SetObjectTeam(v,kTeamEnemy);
}

}  // namespace jet

// Its damage message (body506's hook, before the stock handler): while the plugin flies it (and it is no wreck yet)
// the HP floor is kFloorHp for this message only, so a killing hit leaves it at the floor (see the file's head).
bool SwarmMessage(unsigned char* v,std::uint32_t msg,void* data,MessageRestore* restore) noexcept {
    (void)data;
    if(msg!=kMsgDamage || !Cfg().enabled || !Cfg().jetPilot || !Cfg().swarm || v[kDead])return false;
    const jet::Jet* const j=jet::FindJet(v);
    if(!j || !jet::IsSwarm(*j) || !j->swarm.init || j->swarm.wreck || j->drone.blastAt || j->reap ||
       GameMs()-j->seen>jet::kFlownMs)return false;
    float* const floor=reinterpret_cast<float*>(v+jet::kHpFloor);
    restore->at=floor;restore->was=*floor;
    *floor=jet::kFloorHp;
    return false;
}

namespace jet {
void SwarmFrame(Jet& j,unsigned char* v,const float* pos,float dt,ULONGLONG ms) noexcept {
    if(!Cfg().swarm) {
        if(!j.reap){j.reap=true;j.why="Swarm off";}
        v[kFireGun]=0;v[kFireMissile]=0;
        return;
    }
    if(!j.swarm.init)Init(j,v,pos,ms);
    else SwarmTeam(v);
    j.m.ready=true;
    if(j.swarm.wreck){WreckFly(j,v,pos,dt,ms);return;}
    // Shot down to the floor its damage was held at (SwarmMessage).
    if(At<float>(v,kHp)<=kFloorHp+0.01f){Wreck(j,v,pos,ms);WreckFly(j,v,pos,dt,ms);return;}

    const Kind& k=KindOf(j);
    float aim[3];
    const bool hasAim=Target(aim,ms);
    float fwd[3];
    Heading(v,fwd);
    if(IsCore(j)) {
        Spawn(j,v,pos,ms);
        CoreFly(j,k,v,pos,aim,hasAim,dt,ms);
    } else if(const Jet* core=CoreOf(j)) {
        Hold(j,*core,pos);
        Heading(core->Vehicle(),fwd);   // off its target, it faces as the core does
    } else {
        if(!j.swarm.scattered) {
            j.swarm.scattered=true;
            const float* c=hasAim ? player.pos : j.anchor;
            j.swarm.orbit=std::atan2(pos[2]-c[2],pos[0]-c[0]);
            Log("SWARM v=%p drone %d: its core is gone: scattered",v,j.swarm.slot);
        }
        Ring(j,k,v,pos,aim,hasAim,dt);
    }
    HoldOffGround(j,pos,GroundClearance(pos),dt,ms);
    const float reach=(IsCore(j) ? kCoreReach : kUnitReach)*kAimReach;
    const bool inReach=hasAim && (aim[0]-pos[0])*(aim[0]-pos[0])+(aim[1]-pos[1])*(aim[1]-pos[1])+(aim[2]-pos[2])*(aim[2]-pos[2])<reach*reach;
    PointAt(j,v,pos,aim,inReach,fwd);
    // A drone arms (curls its abdomen) with its target in reach.
    if(!IsCore(j))SwarmPose(j,v,inReach && Cfg().swarmFire,dt,ms);
    Guns(j,v,pos,aim,inReach,ms);
}
}  // namespace jet
}  // namespace crew
