// The plugin's gravity operations (gravity.h; docs/gravity-re.md, static RE 2026-10-20). How each kind of thing is moved:
//  - a live insect / spider / bee / berserker / nephila: its cling controller (CharacterControl_Cling, obj+0xE00 / +0x1160 /
//    +0xDE0): Launch 0x6DAA10(ctrl, v, frames) sets its ballistic velocity and keeps it off the ground for `frames` steps,
//    the game's own blast knock-back (GiantAnt slot 47 0x40AA60 passes 2). A bee in flight (mode +0x00 set) skips that
//    velocity: its controller's body (+0x10) is given the velocity instead (L: its flight code may fight it).
//  - a live humanoid / monster / human / other walker: its walk controller (CharacterControl_Walk, obj+0xB40 / +0x1120 /
//    +0x680, the walkers' own): the velocity into +0x30 and Detach 0x11B9880 (+0x91 = 0, +0xD0 = 1: off the ground, the
//    velocity kept), the game's own jump (HumanBase 0x5669A8).
//  - a corpse (the same object, +0x2E8 set), a vehicle, an event animation object: its ragdoll's bodies, all at once
//    (Ragdoll SetLinearVelocityAll 0x6ECF20, under the world's lock); its place and speed read off one of them.
//  - anything else the physics simulates (a container, a map object, a debris piece): the rigid body the ray hit,
//    SetLinearVelocity 0x11B18F0, when its motion type (0x11B14A0) is dynamic (a static body shares motion 0: never).
// Its mass: a body's or a ragdoll's own (0x11B13D0, the bodies' sum; a CarBase's veh+0x1710), the characters' estimates
// (gravity_logic.h FamilyKg). Setting a velocity wakes a sleeping body (docs/gravity-re.md §2.2).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "gravity.h"
#include "body506.h"   // GameStep
#include "memory.h"
#include <cmath>
#include <cstring>

namespace crew::gravity {
namespace {
using ::gravmath::Family;
constexpr std::size_t kHavokGlobal=0x20B2958;   // g; the world wrapper W = g+0x10, the hknp world *(g+0x68)
constexpr unsigned kGetBody=0x11AD2E0,kBodyObject=0x108260,kMotionType=0x11B14A0,kMass=0x11B13D0,kGetVel=0x11B1300,
                   kSetVel=0x11B18F0,kGetPos=0x11B15B0,kClingLaunch=0x6DAA10,kWalkDetach=0x11B9880,kRagdollSetVel=0x6ECF20,
                   kRagdollBody=0x6EA430;
const unsigned char kGetBodySig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x8B,0xDA,0x48,0x8B,0x49,0x58,
                                   0x48,0x83,0xC1,0x20,0x48,0x8B,0x01,0xFF,0x50,0x68,0x85,0xC0,0x74};
const unsigned char kBodyObjectSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0xED,0xA6,0xFA,0x01,0x8B,0xD1,0x48,0x8D,0x48};
// The body wrappers share their opening; each is told apart by its tail (docs/gravity-re.md §2.1).
const unsigned char kMotionTypeSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x8B,0x91,0xF0,0x00,0x00,0x00,0x48,0x8B,
                                      0x48,0x58,0x48,0x8B,0x41,0x20,0x48,0x83,0xC1,0x20,0xFF,0x90,0x80,0x00,0x00,0x00,0x8B,0x48,0x54,
                                      0xF6,0xC1,0x01};
const unsigned char kBodyLookup[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x48,0x8B,0xD9,0x48,0x8B,0x48,0x58,
                                   0x48,0x8B,0x41,0x20,0x48,0x83,0xC1,0x20,0x8B,0x93,0xF0,0x00,0x00,0x00,0xFF,0x90,0x80,0x00,0x00,0x00,
                                   0x48,0x8B,0x8B,0x00,0x01,0x00,0x00,0x48,0x8B,0x49,0x58,0x48,0x83,0xC1,0x20,0x48,0x8B,0x11,0x4C,0x8B,
                                   0x82,0xF8,0x00,0x00,0x00,0x8B,0x90,0x80,0x00,0x00,0x00,0x41,0xFF,0xD0};   // GetMass / GetLinearVelocity
const unsigned char kMassTail[]={0x0F,0x57,0xD2,0xF2,0x0F,0x10,0x40,0x40};   // ...then 1 / the inverse mass (motion+0x46)
const unsigned char kGetVelTail[]={0x48,0x83,0xC0,0x60};                     // ...then &motion+0x60
const unsigned char kSetVelSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,
                                  0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xA8,0x00,0x00};  // slot 21 (+0xA8)
const unsigned char kGetPosSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x8B,0x91,0xF0,0x00,0x00,0x00,0x48,0x8B,0x48,
                                  0x58,0x48,0x8B,0x41,0x20,0x48,0x83,0xC1,0x20,0xFF,0x90,0x80,0x00,0x00,0x00,0x48,0x83,0xC0,0x30};
const unsigned char kClingLaunchSig[]={0xC6,0x41,0x70,0x00,0x44,0x89,0x81,0xB8,0x00,0x00,0x00,0x0F,0x10,0x02,0x0F,0x11};
const unsigned char kWalkDetachSig[]={0xC6,0x81,0x91,0x00,0x00,0x00,0x00,0xC6,0x81,0xD0,0x00,0x00,0x00,0x01,0xC3};
const unsigned char kRagdollSetVelSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x41,0x56,0x48,
                                         0x83,0xEC,0x20,0x4C,0x8B,0xF1,0x48,0x8B,0xEA,0x48,0x8B,0x89,0xC0,0x00};
const unsigned char kRagdollBodySig[]={0x4C,0x8B,0x41,0x78,0x45,0x33,0xC9,0x48,0x8B,0x81,0x88,0x00,0x00,0x00,0x4C,0x8D};

using GetBodyFn=void*(__fastcall*)(void*,std::uint32_t);
using BodyObjectFn=void*(__fastcall*)(std::uint32_t);
using BodyIntFn=int(__fastcall*)(void*);
using BodyFloatFn=float(__fastcall*)(void*);
using BodyVecFn=const float*(__fastcall*)(void*);
using BodySetFn=void(__fastcall*)(void*,const float*);
using LaunchFn=void(__fastcall*)(void*,const float*,int);
using DetachFn=void(__fastcall*)(void*);
using RagdollSetFn=void(__fastcall*)(void*,const float*);
using RagdollBodyFn=int(__fastcall*)(void*,std::uint32_t);

// The families (MSVC RTTI type descriptors, docs/gravity-re.md §1) and their controllers / ragdolls.
constexpr std::uint32_t kTdInsect=0x202A270,kTdHumanoid=0x203B5B0,kTdMonster=0x203E308,kTdHuman=0x2016A98,
                        kTdWalkingEnemy=0x202A678,kTdVehicle=0x2006480;
constexpr unsigned kVtNephila=0x17C84C8,kVtBoss=0x17BEC68,kVtEventAnimation=0x17D4D20;
struct Walker { unsigned vtable; std::size_t walk; };   // the other walk users, by their exact class
constexpr Walker kWalkers[]={{0x17B5370,0x1800},{0x17B6270,0x10F0},{0x17B8898,0xF90},{0x17AFEE0,0xFF0},{0x17A9650,0xAE0}};
// Cling: mode +0x00 (set: its velocity skipped), body +0x10, landed +0x70, ballistic velocity +0x80, sphere radius +0xB0.
constexpr std::size_t kClingMode=0x00,kClingBody=0x10,kClingLanded=0x70,kClingVel=0x80,kClingRadius=0xB0;
// Walk: body +0x08, the launched velocity +0x30, support +0x91 (2: standing).
constexpr std::size_t kWalkBody=0x08,kWalkVel=0x30,kWalkSupport=0x91;
// Ragdoll: entries +0x78 (0xC0 each, the Body* at +0x50), count +0x88.
constexpr std::size_t kRagEntries=0x78,kRagCount=0x88,kRagStride=0xC0,kRagEntryBody=0x50;
constexpr std::size_t kBodyIdAt=0xF0;   // xgs::havok::Body: its hknp body id
constexpr std::size_t kVehicleRagdoll=0x1320,kCarMass=0x1710,kHumanoidRagdollPtr=0xA78;
constexpr std::uint64_t kMostRagdollBodies=128;
constexpr int kDynamic=2;               // GetMotionType: 0 static, 1 keyframed, 2 dynamic
constexpr std::uint32_t kGrabLayer=0x0F;// layer 15's filter: ragdolls, vehicles, containers, debris (docs/gravity-re.md §4.1)
constexpr std::uint32_t kNoBody=0xFFFFFFu;

bool ok=false;

void* World() noexcept {
    const auto g=At<unsigned char*>(image,kHavokGlobal);
    if(!g || !Readable(g,0x70) || !At<const void*>(g,0x68))return nullptr;
    return g+0x10;
}
void* BodyOf(std::uint32_t id) noexcept {
    if((id&kNoBody)==kNoBody)return nullptr;
    void* w=World();
    return w ? reinterpret_cast<GetBodyFn>(image+kGetBody)(w,id) : nullptr;
}
bool Dynamic(void* b) noexcept { return b && reinterpret_cast<BodyIntFn>(image+kMotionType)(b)==kDynamic; }
float MassOf(void* b) noexcept { return b ? reinterpret_cast<BodyFloatFn>(image+kMass)(b) : 0.0f; }
bool Vec(void* b,std::size_t fn,float* out) noexcept {
    if(!b)return false;
    const float* v=reinterpret_cast<BodyVecFn>(image+fn)(b);
    if(!v || !Readable(v,12))return false;
    std::memcpy(out,v,12);
    return std::isfinite(out[0]+out[1]+out[2]);
}
std::uint32_t IdOf(const void* b) noexcept { return b && Readable(b,kBodyIdAt+4) ? At<std::uint32_t>(b,kBodyIdAt)&kNoBody : kNoBody; }

}  // namespace

bool IsA(const void* obj,std::uint32_t td) noexcept {
    if(!obj || !Readable(obj,8))return false;
    const auto vt=At<const unsigned char*>(obj,0);
    if(!vt || !Readable(vt-8,8))return false;
    const auto col=*reinterpret_cast<const std::uint32_t* const*>(vt-8);
    if(!col || !Readable(col,24) || col[0]!=1 || col[1]!=0)return false;
    const auto chd=reinterpret_cast<const std::uint32_t*>(image+col[4]);
    if(!Readable(chd,16) || chd[2]==0 || chd[2]>64)return false;
    const auto bca=reinterpret_cast<const std::uint32_t*>(image+chd[3]);
    if(!Readable(bca,chd[2]*4))return false;
    for(std::uint32_t i=0;i<chd[2];++i) {
        const auto bcd=reinterpret_cast<const std::uint32_t*>(image+bca[i]);
        if(Readable(bcd,4) && bcd[0]==td)return true;
    }
    return false;
}

namespace {
bool VtIs(const void* obj,unsigned vt) noexcept { return obj && Readable(obj,8) && At<const unsigned char*>(obj,0)==image+vt; }

// The ragdoll at `rg`: its bodies (Body*), up to `most`; how many.
int RagdollBodies(const unsigned char* rg,void** out,int most) noexcept {
    if(!rg || !Readable(rg,kRagCount+8))return 0;
    const auto entries=At<const unsigned char*>(rg,kRagEntries);
    const auto count=At<std::uint64_t>(rg,kRagCount);
    if(!entries || count==0 || count>kMostRagdollBodies || !Readable(entries,static_cast<std::size_t>(count)*kRagStride))return 0;
    int n=0;
    for(std::uint64_t i=0;i<count && n<most;++i) {
        void* b=At<void*>(entries,static_cast<std::size_t>(i)*kRagStride+kRagEntryBody);
        if(b && Readable(b,0x110))out[n++]=b;
    }
    return n;
}
const unsigned char* RagdollOf(const Target& t) noexcept {
    const auto* o=static_cast<const unsigned char*>(t.obj.obj);
    if(!o)return nullptr;
    return t.ragdollPtr ? At<const unsigned char*>(o,t.ctrl) : o+t.ctrl;
}

float SizeOf(float kg) noexcept { return ::gravmath::Clamp(0.5f*std::cbrt(kg/250.0f),0.5f,12.0f); }

// A ragdoll target (a corpse, a vehicle, an event animation object): its dynamic bodies' mass (their sum, or `kg` when
// given), the body read for its place (`hit` when it is one of them, else its heaviest). False: none of its bodies moves.
bool RagdollTarget(const unsigned char* o,std::size_t at,bool ptr,std::uint32_t hit,float kg,Target* out) noexcept {
    Target t;
    t.obj=ObjRef::Of(o);t.ctrl=at;t.ragdollPtr=ptr;t.kind=Kind::ragdoll;
    const unsigned char* rg=RagdollOf(t);
    void* bodies[32];
    const int n=RagdollBodies(rg,bodies,32);
    float sum=0.0f,heaviest=0.0f;
    std::uint32_t read=kNoBody;
    for(int i=0;i<n;++i) {
        if(!Dynamic(bodies[i]))continue;
        const float m=MassOf(bodies[i]);
        if(!(m>0.0f) || !std::isfinite(m))continue;
        sum+=m;
        const std::uint32_t id=IdOf(bodies[i]);
        if(id==(hit&kNoBody))read=id;
        else if(m>heaviest && read!=(hit&kNoBody)){heaviest=m;read=id;}
    }
    if(read==kNoBody || !(sum>0.0f))return false;
    t.body=read;
    t.kg=kg>0.0f ? kg : sum;
    t.radius=SizeOf(t.kg);
    *out=t;
    return true;
}

// A live character's controller target.
Target Controller(const unsigned char* o,Kind kind,std::size_t ctrl,Family f) noexcept {
    Target t;
    t.obj=ObjRef::Of(o);t.kind=kind;t.ctrl=ctrl;t.family=f;
    float r=0.0f;
    if(kind==Kind::cling && Readable(o+ctrl,kClingRadius+4))r=At<float>(o,ctrl+kClingRadius);
    t.kg=::gravmath::FamilyKg(f,r);
    switch(f) {
    case Family::insect:case Family::berserker:t.radius=::gravmath::Clamp(r>0.0f ? r : 1.5f,0.5f,20.0f);break;
    case Family::humanoid:t.radius=2.5f;break;
    case Family::humanoidBoss:t.radius=3.0f;break;
    case Family::monster:t.radius=20.0f;break;
    case Family::human:t.radius=0.6f;break;
    case Family::walker:t.radius=2.0f;break;
    }
    return t;
}

// The ragdoll a dead character of each family keeps (docs/gravity-re.md §5).
bool CorpseTarget(const unsigned char* o,std::uint32_t hit,Target* out) noexcept {
    if(IsA(o,kTdInsect))return RagdollTarget(o,0xB10,false,hit,0.0f,out);
    if(VtIs(o,kVtNephila))return RagdollTarget(o,0xB00,false,hit,0.0f,out);
    if(IsA(o,kTdHumanoid))return RagdollTarget(o,kHumanoidRagdollPtr,true,hit,0.0f,out);
    if(IsA(o,kTdMonster))return RagdollTarget(o,0xDA0,false,hit,0.0f,out);
    if(IsA(o,kTdHuman))return RagdollTarget(o,0xD90,false,hit,0.0f,out);
    return false;
}

bool LiveTarget(const unsigned char* o,Target* out) noexcept {
    if(IsA(o,kTdInsect)){*out=Controller(o,Kind::cling,0xE00,Family::insect);return true;}
    if(VtIs(o,kVtNephila)){*out=Controller(o,Kind::cling,0xDE0,Family::insect);return true;}
    if(IsA(o,kTdWalkingEnemy)){*out=Controller(o,Kind::cling,0x1160,Family::berserker);return true;}
    if(IsA(o,kTdHumanoid)){*out=Controller(o,Kind::walk,0xB40,VtIs(o,kVtBoss) ? Family::humanoidBoss : Family::humanoid);return true;}
    if(IsA(o,kTdMonster)){*out=Controller(o,Kind::walk,0x1120,Family::monster);return true;}
    if(IsA(o,kTdHuman)){*out=Controller(o,Kind::walk,0x680,Family::human);return true;}
    for(const Walker& w:kWalkers)
        if(VtIs(o,w.vtable)){*out=Controller(o,Kind::walk,w.walk,Family::walker);return true;}
    return false;
}

bool RidesIn(const unsigned char* v,const void* who) noexcept {
    if(!who)return false;
    const unsigned n=SeatCount(v);
    for(unsigned i=0;i<n && i<16;++i)
        if(At<const void*>(SeatAt(const_cast<unsigned char*>(v),i),edf::kSeatRider)==who)return true;
    return false;
}

// ---- following what is thrown (Frame) ----
constexpr int kMostFollowed=32,kMostNear=24;
constexpr float kContactPad=2.0f;        // m past its own size an enemy's lock point is touched (vehicleram.cpp's kBodyPad)
constexpr float kLeastClosing=4.0f;      // m/s: slower is a push
constexpr float kLeastBlast=4.0f;        // m: a blow's charge at least this big (the impact charges: 2 / 4 / 8 ... m)
constexpr ULONGLONG kContactGapMs=500;   // a thing's blows (its charges) at most this often
constexpr float kLeastDamage=1.0f;
struct Followed {
    Target t;
    ObjRef by;
    ::gravmath::Flight flight;
    bool held=false;
    float scale=1.0f;
    ULONGLONG lastMs=0,lastBlow=0;
};
Followed followed[kMostFollowed];

Followed* Find(const Target& t) noexcept {
    for(auto& f:followed)
        if(f.t && f.t.kind==t.kind && f.t.obj.obj==t.obj.obj && f.t.obj.ctrl==t.obj.ctrl && (t.obj.obj || f.t.body==t.body))return &f;
    return nullptr;
}

struct Near { const void* object; float at[3]; };
struct NearScan { const float* centre; float reach; Near found[kMostNear]; int count; };
void SeeNear(void* ctx,const void* object,const float* lock) noexcept {
    auto& s=*static_cast<NearScan*>(ctx);
    if(s.count>=kMostNear || !lock)return;
    const float d[3]={lock[0]-s.centre[0],lock[1]-s.centre[1],lock[2]-s.centre[2]};
    if(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>s.reach*s.reach)return;
    for(int i=0;i<s.count;++i)if(s.found[i].object==object)return;   // one lock point an enemy
    Near& n=s.found[s.count++];
    n.object=object;std::memcpy(n.at,lock,12);
}

void Deal(const unsigned char* by,const float* at,float kg,float dv,float radius,float scale,const char* what,const void* object) noexcept {
    const float damage=::gravmath::ImpactDamage(kg,dv,Cfg().gravityDamage*scale);
    if(damage<kLeastDamage)return;
    const bool dealt=ImpactDamage(by,at,damage,radius);
    if(Cfg().debug)Log("GRAVITY %s %p at (%.0f,%.0f,%.0f): %.1f t at %.1f m/s: %.0f damage, %.1f m%s",what,object,at[0],at[1],at[2],kg*0.001f,dv,
                       damage,radius,dealt ? "" : " (not dealt: no impact charge this mission)");
}

void Step(Followed& f,ULONGLONG ms) noexcept {
    if(!Refresh(f.t)){f=Followed{};return;}
    const auto* by=static_cast<const unsigned char*>(f.by.obj);
    if(!f.by.Is(by)){f=Followed{};return;}   // the thrower is gone: nobody's to deal
    const float dt=GameStep(f.lastMs ? ms-f.lastMs : 0);
    f.lastMs=ms;
    float at[3],vel[3];
    if(!Position(f.t,at) || !Velocity(f.t,vel)){f=Followed{};return;}   // nothing left to read: no longer followed
    // What it passes through: the enemies of the thrower's side round it; the fastest closing speed onto one of them.
    NearScan s{at,f.t.radius+kContactPad,{},0};
    VisitEnemiesOf(At<std::int32_t>(by,kTeam),&SeeNear,&s);
    float closing=0.0f;
    const void* onto=nullptr;
    // The blow's centre: the thing's own lock point when it is an enemy (a colonist's is high in its body, its root at its
    // feet: a blast there missed most of it, the 2026-10-20 run), else its centre; at least kLeastBlast across.
    float centre[3]={at[0],at[1],at[2]};
    for(int i=0;i<s.count;++i) {
        const Near& n=s.found[i];
        if(n.object==f.t.obj.obj){std::memcpy(centre,n.at,12);continue;}
        const float c=ram::Closing(at,vel,n.at);
        if(c>closing){closing=c;onto=n.object;}
    }
    float dv=0.0f;
    bool done=false;
    if(!f.held) {
        const bool landed=Landed(f.t);
        dv=f.flight.Step(vel,landed,dt,&done);
        // A thrown character that has landed is on its own feet again: its walking is no impact.
        if(landed && f.flight.age>0.1f)done=true;
    }
    // One blow a frame, at most one a kContactGapMs: the impact, or else the contact (the charge reaches every enemy round
    // it either way, the thing itself too when it is one).
    if(ms-f.lastBlow>=kContactGapMs || !f.lastBlow) {
        const float blast=f.t.radius>kLeastBlast ? f.t.radius : kLeastBlast;
        if(dv>0.0f){Deal(by,centre,f.t.kg,dv,blast,f.scale,"impact",f.t.obj.obj);f.lastBlow=ms;}
        else if(closing>=kLeastClosing){Deal(by,centre,f.t.kg,closing,blast,f.scale,"contact",onto);f.lastBlow=ms;f.flight.sinceHit=0.0f;}
    }
    if(done)f=Followed{};
}
}  // namespace

bool Install() noexcept {
    ok=Matches(kGetBody,kGetBodySig,sizeof(kGetBodySig)) && Matches(kBodyObject,kBodyObjectSig,sizeof(kBodyObjectSig)) &&
       Matches(kMotionType,kMotionTypeSig,sizeof(kMotionTypeSig)) &&
       Matches(kMass,kBodyLookup,sizeof(kBodyLookup)) && Matches(kMass+sizeof(kBodyLookup),kMassTail,sizeof(kMassTail)) &&
       Matches(kGetVel,kBodyLookup,sizeof(kBodyLookup)) && Matches(kGetVel+sizeof(kBodyLookup),kGetVelTail,sizeof(kGetVelTail)) &&
       Matches(kSetVel,kSetVelSig,sizeof(kSetVelSig)) && Matches(kGetPos,kGetPosSig,sizeof(kGetPosSig)) &&
       Matches(kClingLaunch,kClingLaunchSig,sizeof(kClingLaunchSig)) && Matches(kWalkDetach,kWalkDetachSig,sizeof(kWalkDetachSig)) &&
       Matches(kRagdollSetVel,kRagdollSetVelSig,sizeof(kRagdollSetVelSig)) && Matches(kRagdollBody,kRagdollBodySig,sizeof(kRagdollBodySig));
    Log("HOOK gravity: %s",ok ? "on (bodies, ragdolls, cling and walk controllers)" : "the physics code is not as expected: off");
    return ok;
}

bool Ready() noexcept { return ok && World(); }

void Reset() noexcept {
    for(auto& f:followed)f=Followed{};
}

bool TargetOf(const void* object,std::uint32_t body,Target* out) noexcept {
    *out=Target{};
    if(!Ready())return false;
    __try {
        const auto* o=static_cast<const unsigned char*>(object);
        if(!o) {   // an ownerless body: a debris piece
            void* b=BodyOf(body);
            if(!Dynamic(b))return false;
            Target t;
            t.body=body&kNoBody;t.kind=Kind::body;t.kg=MassOf(b);
            if(!(t.kg>0.0f) || !std::isfinite(t.kg))return false;
            t.radius=SizeOf(t.kg);
            *out=t;
            return true;
        }
        if(!Readable(o,0x320))return false;
        if(IsA(o,kTdVehicle)) {
            float kg=At<float>(o,kCarMass);
            if(!std::isfinite(kg) || kg<50.0f || kg>1.0e7f)kg=0.0f;   // not a CarBase's: the bodies' sum
            return RagdollTarget(o,kVehicleRagdoll,false,body,kg,out);
        }
        if(VtIs(o,kVtEventAnimation))return RagdollTarget(o,0xAA0,false,body,0.0f,out);
        if(o[kDead]) {
            if(CorpseTarget(o,body,out))return true;
        } else if(LiveTarget(o,out)) {
            out->body=body&kNoBody;
            return true;
        }
        // Anything else: the body the ray hit, if the physics moves it (a container, a map object).
        void* b=BodyOf(body);
        if(!Dynamic(b))return false;
        Target t;
        t.obj=ObjRef::Of(o);t.body=body&kNoBody;t.kind=Kind::body;t.kg=MassOf(b);
        if(!(t.kg>0.0f) || !std::isfinite(t.kg))return false;
        t.radius=SizeOf(t.kg);
        *out=t;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){*out=Target{};return false;}
}

bool Live(const Target& t) noexcept {
    if(!t || !ok)return false;
    __try {
        if(t.obj.obj && !t.obj.Is(t.obj.obj))return false;
        if(t.kind!=Kind::body)return true;
        // A body: still dynamic, and (an owned one) still its owner's (a freed id may be another's by now).
        if(!Dynamic(BodyOf(t.body)))return false;
        return !t.obj.obj || reinterpret_cast<BodyObjectFn>(image+kBodyObject)(t.body)==t.obj.obj;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool Refresh(Target& t) noexcept {
    if(!Live(t))return false;
    if(t.kind!=Kind::cling && t.kind!=Kind::walk)return true;
    __try {
        const auto* o=static_cast<const unsigned char*>(t.obj.obj);
        if(!o[kDead])return true;
        Target corpse;
        if(!CorpseTarget(o,t.body,&corpse))return false;
        t=corpse;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool Position(const Target& t,float* at) noexcept {
    if(!Live(t))return false;
    __try {
        const auto* o=static_cast<const unsigned char*>(t.obj.obj);
        switch(t.kind) {
        case Kind::body:case Kind::ragdoll:return Vec(BodyOf(t.body),kGetPos,at);
        case Kind::cling:
            if(Vec(At<void*>(o,t.ctrl+kClingBody),kGetPos,at))return true;
            break;
        case Kind::walk:
            if(Vec(At<void*>(o,t.ctrl+kWalkBody),kGetPos,at))return true;
            break;
        case Kind::none:return false;
        }
        std::memcpy(at,o+kPosition,12);
        return std::isfinite(at[0]+at[1]+at[2]);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool Velocity(const Target& t,float* vel) noexcept {
    if(!Live(t))return false;
    __try {
        const auto* o=static_cast<const unsigned char*>(t.obj.obj);
        switch(t.kind) {
        case Kind::body:case Kind::ragdoll:return Vec(BodyOf(t.body),kGetVel,vel);
        case Kind::cling:
            // Airborne its ballistic velocity is what moves it (the body gets it each step); on the ground the body's own.
            if(!o[t.ctrl+kClingLanded] && At<std::int32_t>(o,t.ctrl+kClingMode)==0) {
                std::memcpy(vel,o+t.ctrl+kClingVel,12);
                return std::isfinite(vel[0]+vel[1]+vel[2]);
            }
            return Vec(At<void*>(o,t.ctrl+kClingBody),kGetVel,vel);
        case Kind::walk:return Vec(At<void*>(o,t.ctrl+kWalkBody),kGetVel,vel);
        case Kind::none:return false;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool Landed(const Target& t) noexcept {
    if(!Live(t))return false;
    __try {
        const auto* o=static_cast<const unsigned char*>(t.obj.obj);
        if(t.kind==Kind::cling)return o[t.ctrl+kClingLanded]!=0;
        if(t.kind==Kind::walk)return o[t.ctrl+kWalkSupport]==2;
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool SetVelocity(const Target& t,const float* vel) noexcept {
    if(!vel || !std::isfinite(vel[0]+vel[1]+vel[2]) || !Live(t))return false;
    alignas(16) const float v[4]={vel[0],vel[1],vel[2],0.0f};
    __try {
        auto* o=static_cast<unsigned char*>(const_cast<void*>(t.obj.obj));
        switch(t.kind) {
        case Kind::body: {
            void* b=BodyOf(t.body);
            if(!Dynamic(b))return false;
            reinterpret_cast<BodySetFn>(image+kSetVel)(b,v);
            return true;
        }
        case Kind::ragdoll: {
            auto* rg=const_cast<unsigned char*>(RagdollOf(t));
            void* bodies[kMostRagdollBodies];
            const int n=RagdollBodies(rg,bodies,static_cast<int>(kMostRagdollBodies));
            // 0x6ECF20 walks every entry under the world's lock, reading its body without a check: each must have one.
            if(n<=0 || static_cast<std::uint64_t>(n)!=At<std::uint64_t>(rg,kRagCount))return false;
            reinterpret_cast<RagdollSetFn>(image+kRagdollSetVel)(rg,v);
            return true;
        }
        case Kind::cling: {
            unsigned char* c=o+t.ctrl;
            if(At<std::int32_t>(c,kClingMode)!=0) {   // a bee in flight: its controller skips the ballistic velocity
                void* b=At<void*>(c,kClingBody);
                if(!b)return false;
                reinterpret_cast<BodySetFn>(image+kSetVel)(b,v);
                return true;
            }
            alignas(16) const float w[4]={vel[0],vel[1],vel[2],1.0f};
            reinterpret_cast<LaunchFn>(image+kClingLaunch)(c,w,2);   // two steps off the ground, as the game's blast gives
            return true;
        }
        case Kind::walk: {
            unsigned char* w=o+t.ctrl;
            std::memcpy(w+kWalkVel,v,16);
            reinterpret_cast<DetachFn>(image+kWalkDetach)(w);
            return true;
        }
        case Kind::none:return false;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool AddVelocity(const Target& t,const float* dv) noexcept {
    float v[3];
    if(!dv || !Velocity(t,v))return false;
    for(int k=0;k<3;++k)v[k]+=dv[k];
    return SetVelocity(t,v);
}

bool Impulse(const Target& t,const float* impulse) noexcept {
    if(!impulse || !(t.kg>0.0f))return false;
    const float dv[3]={impulse[0]/t.kg,impulse[1]/t.kg,impulse[2]/t.kg};
    return AddVelocity(t,dv);
}

bool HoldAt(const Target& t,const float* goal,const float* goalVel) noexcept {
    float at[3],v[3];
    if(!goal || !Position(t,at) || !::gravmath::HoldVelocity(at,goal,goalVel,t.kg,v))return false;
    return SetVelocity(t,v);
}

bool Throw(const Target& t,const float* dir,float baseSpeed,const float* carry,const unsigned char* by,float scale,float* vel) noexcept {
    if(!dir)return false;
    const float len=::gravmath::Len(dir);
    const float speed=::gravmath::ThrowSpeed(t.kg,baseSpeed);
    if(!(len>1e-4f) || !(speed>0.0f) || t.kg>::gravmath::kMostLiftKg)return false;
    float v[3];
    for(int k=0;k<3;++k)v[k]=dir[k]/len*speed+(carry ? carry[k] : 0.0f);
    if(!SetVelocity(t,v))return false;
    if(vel)std::memcpy(vel,v,12);
    Track(t,by,false,scale);
    if(Cfg().debug)Log("GRAVITY throw %p (kind %d, %.1f t) at %.1f m/s",t.obj.obj,static_cast<int>(t.kind),t.kg*0.001f,::gravmath::Len(v));
    return true;
}

void Track(const Target& t,const unsigned char* by,bool held,float scale) noexcept {
    if(!t || !by || !(::gravmath::HoldCap(t.kg)>0.0f))return;   // too heavy: never moved, hits nothing
    Followed* f=Find(t);
    if(!f) {
        for(auto& slot:followed)if(!slot.t){f=&slot;break;}
        if(!f) {   // full: the oldest throw gives way
            f=&followed[0];
            for(auto& slot:followed)if(slot.flight.age>f->flight.age)f=&slot;
        }
        *f=Followed{};
    }
    const bool wasHeld=f->held;
    f->t=t;f->by=ObjRef::Of(by);f->held=held;f->scale=scale>0.0f && std::isfinite(scale) ? scale : 1.0f;
    if(!held && (wasHeld || !f->lastMs))f->flight=::gravmath::Flight{};   // a new flight: its readings start over
}

void Untrack(const Target& t) noexcept {
    if(Followed* f=Find(t))*f=Followed{};
}

int Radial(const float* centre,float radius,float speed,const unsigned char* by,float scale) noexcept {
    if(!centre || !by || !(radius>0.0f) || !Ready())return 0;
    NearScan s{centre,radius,{},0};
    __try { VisitEnemiesOf(At<std::int32_t>(by,kTeam),&SeeNear,&s); } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
    int moved=0;
    for(int i=0;i<s.count;++i) {
        Target t;
        if(!TargetOf(s.found[i].object,kNoBody,&t) || !(::gravmath::HoldCap(t.kg)>0.0f))continue;
        float at[3];
        if(!Position(t,at))continue;
        float d[3]={at[0]-centre[0],at[1]-centre[1],at[2]-centre[2]};
        const float len=::gravmath::Len(d);
        const float falloff=1.0f-::gravmath::Clamp(len/radius,0.0f,1.0f);
        if(len<1e-3f){d[0]=0.0f;d[1]=1.0f;d[2]=0.0f;}
        else for(float& c:d)c/=len;
        const float dv[3]={d[0]*speed*falloff,d[1]*speed*falloff,d[2]*speed*falloff};
        if(AddVelocity(t,dv)){Track(t,by,false,scale);++moved;}
    }
    return moved;
}

bool Pick(const float* a,const float* b,const void* self,Target* out,float* hit) noexcept {
    *out=Target{};
    if(!a || !b || !Ready())return false;
    float end[3]={b[0],b[1],b[2]};
    // Not through the map: the ray stops at the first wall or ground.
    float wall[3];
    const float mapAt=MapRay(a,b,wall);
    if(mapAt>=0.0f)std::memcpy(end,wall,12);
    float from[3]={a[0],a[1],a[2]};
    for(int tries=0;tries<4;++tries) {
        float at[3];std::uint32_t body=kNoBody;
        if(LayerRay(from,end,kGrabLayer,at,&body)<0.0f)break;
        void* owner=nullptr;
        __try { owner=reinterpret_cast<BodyObjectFn>(image+kBodyObject)(body); } __except(EXCEPTION_EXECUTE_HANDLER){owner=nullptr;}
        bool own=owner && owner==self;
        __try { if(!own && owner && self && IsA(owner,kTdVehicle))own=RidesIn(static_cast<const unsigned char*>(owner),self); }
        __except(EXCEPTION_EXECUTE_HANDLER){own=true;}
        if(!own && TargetOf(owner,body,out)) {
            if(hit)std::memcpy(hit,at,12);
            return true;
        }
        // Past it and on along the ray (the thrower's own body, or a fixed one).
        float d[3]={end[0]-at[0],end[1]-at[1],end[2]-at[2]};
        const float left=::gravmath::Len(d);
        if(left<0.5f)break;
        for(int k=0;k<3;++k)from[k]=at[k]+d[k]/left*0.3f;
    }
    // The aim's help: an enemy's lock point near the ray (a character's own body is not always on the grab layer).
    struct Best { const float* a; float dir[3]; float len; const void* self; const void* object; float at[3]; float score; };
    Best best{a,{end[0]-a[0],end[1]-a[1],end[2]-a[2]},0.0f,self,nullptr,{},0.0f};
    best.len=::gravmath::Len(best.dir);
    if(!(best.len>0.5f))return false;
    for(float& c:best.dir)c/=best.len;
    auto see=[](void* ctx,const void* object,const float* lock) noexcept {
        auto& s=*static_cast<Best*>(ctx);
        if(!lock || object==s.self)return;
        const float d[3]={lock[0]-s.a[0],lock[1]-s.a[1],lock[2]-s.a[2]};
        const float along=d[0]*s.dir[0]+d[1]*s.dir[1]+d[2]*s.dir[2];
        if(along<1.0f || along>s.len)return;
        const float off[3]={d[0]-s.dir[0]*along,d[1]-s.dir[1]*along,d[2]-s.dir[2]*along};
        const float miss=::gravmath::Len(off);
        const float reach=along*0.04f>2.0f ? along*0.04f : 2.0f;
        if(miss>reach)return;
        const float score=miss/reach+along/s.len*0.25f;
        if(s.object && score>=s.score)return;
        s.object=object;s.score=score;std::memcpy(s.at,lock,12);
    };
    __try { VisitLockPoints(see,&best); } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    if(!best.object || !TargetOf(best.object,kNoBody,out))return false;
    if(hit)std::memcpy(hit,best.at,12);
    return true;
}

void Frame() noexcept {
    if(!Ready())return;
    const ULONGLONG ms=GameMs();
    for(auto& f:followed) {
        if(!f.t)continue;
        __try { Step(f,ms); } __except(EXCEPTION_EXECUTE_HANDLER){f=Followed{};}
    }
}
}  // namespace crew::gravity
