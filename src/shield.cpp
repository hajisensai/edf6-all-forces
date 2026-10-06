// The Shield Bearer's shield (防护者护盾) let through what is slow and stopping what is fast (the user, 2026-10-05:
// "a plane flying too fast hits it head-on and cannot pass, but a man can; the player's attacks the same: a slow one
// passes, a fast one does not"), as a Dune shield.
//
// The bearer is AlienTrailer (E513_SHIELDBEARER*.SGO, vtable kBearerVtable); its shield is AlienTrailer_Barrier at
// +0x10D0, two layers of 0x680 (small at kLayers[0], big at kLayers[1]). Per layer: +0x00 active (byte), +0x580 the
// xgs body (its id at +0xF0), +0x590 BarrierInfo (its team at +8, copied from the bearer's every frame). (H, static)
//  - Rounds: the bullets' candidate collector's addBody (jet_hooks.cpp AddBodyHook, stock 0x232AA0) takes a body
//    with BarrierInfo (key 2) as a shield: the round stops on its face. ShieldLetsThrough leaves a layer's body out
//    of the candidates when the round is slower than kPassSpeed (core = collector+0x88, velocity core+0xB90 in m/s,
//    every BulletBase: H): the round goes through as if no shield were there. The stock team test stays as it is.
//  - Vehicles: the stock shield has no contact with any vehicle (the heli's CheckBarrierCollision 0x64FB20 asks a
//    flag no code sets: M). Every vehicle faster than kBlockSpeed is kept from crossing a hostile layer's face as
//    from a building's wall (the user, 2026-10-05: "it threw me back; it should be a hit like a building's"): the
//    velocity's part across the face is gone, the rest kept, and ShieldBlock returns the speed lost for the owner's
//    crash (the player jet: the crash a building gives, playerjet.cpp Crash). The plugin's jets and the player jet
//    from their physics steps (their own velocity), every other vehicle (stock helis, tanks, ...) from its frame
//    (ShieldVehicle: the physics body's velocity). Slower vehicles, and everyone on foot, cross freely.
// The layer's sphere: the world box of its body's shape, as the heli's check measures it (0x64FC1A..0x64FCD6):
// shape 0x11B15E0(body), transform = the world interface (*(*(body+0x100)+0x58)+0x20) slot 0x80 (id), box = the
// shape type's entry of the table at kAabbTable (+type*0x100+0x18)(shape, transform, out min/max). Centre and the
// half of its x extent. The shields are gathered once a frame from the lock-on registry (the EDF side's enemies).
#include "crew.h"
#include "heli.h"
#include "memory.h"
#include "online_authority.h"
#include <cmath>

namespace crew {
namespace {
constexpr unsigned kBearerVtable=0x17A9650,kBodyObject=0x108260,kBodyShape=0x11B15E0,kAabbTable=0x20F1930;
constexpr std::size_t kLayers[2]={0x18C0,0x1F40};
constexpr std::size_t kLayerBody=0x580,kLayerInfo=0x590,kInfoTeam=8,kBodyId=0xF0,kBodyWorld=0x100,kWorldIface=0x58;
constexpr std::size_t kCollectorCore=0x88,kBulletVelocity=0xB90;
constexpr unsigned kTransformSlot=0x80/8;
constexpr std::size_t kHeliBody=0x1650;   // HelicopterBase's rigid body
constexpr unsigned kGetLinVel=0x11B1300,kSetLinVel=0x11B18F0;   // (body) -> m/s; (body, m/s): physics.cpp
constexpr std::int32_t kSideEdf=0;   // the shields are the EDF side's enemies (the lock-on registry)
// Rounds slower than this (m/s) pass a shield: 2.5 m a frame. Rifles, cannons and missiles in flight are faster;
// grenades lobbed slowly, plasma balls and flames slower.
constexpr float kPassSpeed=150.0f;
// Vehicles faster than this (m/s, about 144 km/h) cannot cross a shield's face.
constexpr float kBlockSpeed=40.0f;
constexpr float kLookAhead=2.0f/60.0f;   // two frames ahead: a fast plane never gets one frame past the face
constexpr int kMaxShields=32;

const unsigned char kBodyShapeSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x8B,0x91,0xF0,0x00,0x00};
const unsigned char kBodyObjectSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x05};

using BodyObjectFn=const void*(__fastcall*)(std::uint32_t);
using BodyShapeFn=const unsigned char*(__fastcall*)(const void*);
using TransformFn=const void*(__fastcall*)(const void*,std::uint32_t);
using AabbFn=void(__fastcall*)(const void*,const void*,float*);
using GetVecFn=const float*(*)(void*);
using SetVecFn=std::uintptr_t(*)(void*,const float*);

struct Sphere { float c[3]; float r; std::int32_t team; };
Sphere shields[kMaxShields];
int shieldCount=0;
ULONGLONG gatheredFrame=~0ull;
bool ok=false;

bool Bearer(const void* object) noexcept {
    return object && Readable(object,kLayers[1]+kLayerInfo+kInfoTeam+4) && At<const void*>(object,0)==image+kBearerVtable;
}

// Layer `i`'s body, when the layer is up.
const unsigned char* LayerBody(const unsigned char* bearer,int i) noexcept {
    const unsigned char* layer=bearer+kLayers[i];
    if(!layer[0])return nullptr;
    const auto body=At<const unsigned char*>(layer,kLayerBody);
    return body && Readable(body,kBodyWorld+8) ? body : nullptr;
}

bool LayerSphere(const unsigned char* bearer,int i,Sphere& s) noexcept {
    const unsigned char* body=LayerBody(bearer,i);
    if(!body)return false;
    const unsigned char* shape=reinterpret_cast<BodyShapeFn>(image+kBodyShape)(body);
    const auto world=At<const unsigned char*>(body,kBodyWorld);
    if(!shape || !world)return false;
    const unsigned char* iface=At<const unsigned char*>(world,kWorldIface)+0x20;
    const auto vt=At<const TransformFn*>(iface,0);
    const void* transform=vt[kTransformSlot](iface,At<std::uint32_t>(body,kBodyId));
    const auto table=At<const unsigned char*>(image,kAabbTable);
    if(!transform || !table)return false;
    const auto aabb=At<AabbFn>(table,static_cast<std::size_t>(shape[0x18])*0x100+0x18);
    alignas(16) float box[8]{};
    aabb(shape,transform,box);
    for(int k=0;k<3;++k)s.c[k]=(box[k]+box[4+k])*0.5f;
    s.r=(box[4]-box[0])*0.5f;
    s.team=At<std::int32_t>(bearer+kLayers[i],kLayerInfo+kInfoTeam);
    // Not set up yet: the bearer's first frames have the layer on team -1 with its body at the origin (2026-10-05:
    // a jet near (0,0,0) ran into it).
    return s.team>=0 && s.r>1.0f && s.r<2000.0f;
}

void Gather(void*,const void* object,const float*) noexcept {
    if(shieldCount>=kMaxShields || !Bearer(object))return;
    const auto bearer=static_cast<const unsigned char*>(object);
    if(bearer[kDead])return;
    for(int i=0;i<2 && shieldCount<kMaxShields;++i)if(LayerSphere(bearer,i,shields[shieldCount]))++shieldCount;
}

void GatherOnce() noexcept {
    const ULONGLONG frame=GameFrame();
    if(frame==gatheredFrame)return;
    gatheredFrame=frame;
    const int was=shieldCount;
    shieldCount=0;
    __try { VisitEnemiesOf(kSideEdf,&Gather,nullptr); }
    __except(EXCEPTION_EXECUTE_HANDLER){shieldCount=0;}
    if(Cfg().debug && shieldCount!=was) {
        Log("SHIELD %d up",shieldCount);
        for(int i=0;i<shieldCount;++i)
            Log("SHIELD %d: centre (%.0f,%.0f,%.0f) radius %.1f team %d",i,shields[i].c[0],shields[i].c[1],shields[i].c[2],shields[i].r,shields[i].team);
    }
}

// Take away the part of `vel` that would carry `pos` across the face of `s` within kLookAhead (as a wall does);
// the speed taken, 0 when it stays on its side.
float Glance(const Sphere& s,const float* pos,float* vel) noexcept {
    const float rel[3]={pos[0]-s.c[0],pos[1]-s.c[1],pos[2]-s.c[2]};
    const float dist=std::sqrt(rel[0]*rel[0]+rel[1]*rel[1]+rel[2]*rel[2]);
    if(dist<1.0f)return 0.0f;
    const float n[3]={rel[0]/dist,rel[1]/dist,rel[2]/dist};
    const float across=vel[0]*n[0]+vel[1]*n[1]+vel[2]*n[2];   // > 0 outward
    const float face=dist-s.r,next=face+across*kLookAhead;
    if((face>=0.0f)==(next>=0.0f))return 0.0f;   // stays on its side
    for(int k=0;k<3;++k)vel[k]-=across*n[k];
    return std::fabs(across);
}

struct PassLog { ULONGLONG at; unsigned passed,stopped; };
PassLog passLog{};
}  // namespace

bool ShieldLetsThrough(void* collector,std::uint32_t body) noexcept {
    if(!ok)return false;
    const auto target=static_cast<const unsigned char*>(reinterpret_cast<BodyObjectFn>(image+kBodyObject)(body));
    if(!Bearer(target))return false;
    bool layer=false;
    for(int i=0;i<2;++i) {
        const unsigned char* b=LayerBody(target,i);
        layer=layer || (b && At<std::uint32_t>(b,kBodyId)==body);
    }
    if(!layer)return false;   // the bearer's own body: hit as ever
    const auto core=At<const unsigned char*>(collector,kCollectorCore);
    if(!core)return false;
    const float* v=reinterpret_cast<const float*>(core+kBulletVelocity);
    const bool pass=v[0]*v[0]+v[1]*v[1]+v[2]*v[2]<kPassSpeed*kPassSpeed;
    ++(pass ? passLog.passed : passLog.stopped);
    const ULONGLONG now=GetTickCount64();
    if(Cfg().debug && now-passLog.at>2000) {
        Log("SHIELD rounds: %u slow ones through, %u fast ones to its face (last %.0f m/s)",passLog.passed,passLog.stopped,
            std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]));
        passLog=PassLog{now,0,0};
    }
    return pass;
}

float ShieldBlock(const unsigned char* v,float* vel) noexcept {
    if(!ok || !v)return 0.0f;
    if(vel[0]*vel[0]+vel[1]*vel[1]+vel[2]*vel[2]<kBlockSpeed*kBlockSpeed)return 0.0f;
    float lost=0.0f;
    __try {
        GatherOnce();
        const float* pos=reinterpret_cast<const float*>(v+kPosition);
        const std::int32_t team=At<std::int32_t>(v,kTeam);
        for(int i=0;i<shieldCount;++i) {
            if(shields[i].team==team)continue;
            const float taken=Glance(shields[i],pos,vel);
            if(taken<=0.0f)continue;
            lost=taken>lost ? taken : lost;
            static ULONGLONG at=0;
            const ULONGLONG now=GetTickCount64();
            if(Cfg().debug && now-at>1000)
                {at=now;Log("SHIELD v=%p at (%.0f,%.0f,%.0f) ran into shield %d (centre (%.0f,%.0f,%.0f) radius %.0f) at %.0f m/s across its face",
                     v,pos[0],pos[1],pos[2],i,shields[i].c[0],shields[i].c[1],shields[i].c[2],shields[i].r,taken);}
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return lost;
}

void ShieldVehicle(unsigned char* v) noexcept {
    // The plugin's bodies: from their own steps. The helicopters only (stock and called): their rigid body is at
    // +0x1650 (docs/aircraft-re.md); a ground vehicle's is not known yet, and only a bike gets near kBlockSpeed.
    if(!ok || BodyOf(v)!=PluginBody::none || v[kDead] || !IsHelicopter(v))return;
    // Online, only where the heli is run (online_authority.h): a copy run elsewhere is pulled to the pose it is sent, and
    // a velocity written here would fight that pull.
    if(!OnlineRunsHere(v))return;
    void* const body=At<void*>(v,kHeliBody);
    if(!body)return;
    const float* now=reinterpret_cast<GetVecFn>(image+kGetLinVel)(body);
    if(!now)return;
    alignas(16) float vel[4]={now[0],now[1],now[2],0.0f};
    if(ShieldBlock(v,vel)>0.0f)reinterpret_cast<SetVecFn>(image+kSetLinVel)(body,vel);
}

bool InstallShields() noexcept {
    __try {
        ok=Matches(kBodyShape,kBodyShapeSig,sizeof(kBodyShapeSig)) && Matches(kBodyObject,kBodyObjectSig,sizeof(kBodyObjectSig));
    } __except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    Log("HOOK shields=%d (slow rounds and vehicles through the Shield Bearer's shield)",ok);
    return ok;
}

void ResetShields() noexcept { shieldCount=0; gatheredFrame=~0ull; }
}  // namespace crew
