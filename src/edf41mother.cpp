// EDF4.1's UfoMother301 (the mothership) and its four part classes as the plugin's own scene object classes
// (docs/edf41-ufomother301.md; the 4.1 behaviour read from EDF41.exe, the EDF6 calls from EDF.dll: src/edf41classes.h).
//
//  - The hull is built by UfoCarrier508's constructor under a cloned vtable (slot 0 dtor, 5 Update, 9 OnMessage, 16 the damage
//    filter): the carrier's base keeps doing what 4.1's base did for the hull (route following, durability, the encounter),
//    the rest is 4.1's: the slow spin (0.0003 rad/frame, ObjectAction 0 / 1 / 2), damage only on the `weak` body and only once
//    the main cannon is gone (4.1 slot 10 0x250FC0), a hit's 64 frames of small fire bursts round the weak point, the parts.
//  - The parts (mp_HatchLow 4, mp_HatchMiddle 4 covers, mp_Cannon 10, mp_BigCannon 1, mp_Panel 4 rings of 22 / 40 / 40 / 23)
//    are made on the hull's first Update with the game's own create-from-path, each an EventAnimationObject (model, CAS
//    "main", ragdoll, durability) under a cloned vtable, and hung on the hull: world = L2 x A x L1 x hull (A from the entry's
//    euler degrees and position; L1 / L2 the class's own: the panels' ring turn and push, the main cannon's spin).
//  - 4.1's per-part state machines, one to one (frame counts at 60 FPS; a state switched to inside a frame sees frame 1 next):
//    the main cannon (close / open / standby / 1200-frame charge / a 61-shot LaserBullet02 sweep / destroyed / falling), the
//    cannon (appear, the 600-frame RocketBullet01 charge every 1800..3599 frames; its beam machine: one BeamBullet01 every
//    4 frames from the 4 hatch muzzles in turn at the target), the panel (push out 100 and turn the ring; a LaserBullet02 every
//    max(30, 120 - 5s) frames or a PlasmaBullet01 every 300 at the target), the hatch (4.1's ObjectGenerator: it opens ahead of
//    a wave, spawns at the locator 発生位置 only once open, closes 240 frames after the last). Flags as 4.1: +0x900 enabled
//    (else invulnerable and unregistered), +0x901 attack, +0x902 the main cannon's one-shot trigger, +0x904 the two static
//    panel rings (never fire, never lockable). Hatches and covers are never enabled: invulnerable for good.
//  - Script interface: ObjectAction (message 0x1000000D) 0..17, GetPartsObject (0x10000017: group 0 the main cannon, 1 the
//    cannons by index), the generators (0x20100, +0x48 the hatch index).
// Sounds are 4.1's SEPRESET entries (the hull SGO's edf41_se, tools/make_edf41_objects.py se41_table) on the cues EDF6 ships in
// tikyuu4_en_MotherShip.acb; effects EDF6's EffectGenUtil. 4.1's sound throttle: one sound of each of 6 kinds per 16 frames.
// Not ported: the hull's glow and smoke emitters (4.1 drives material colours the converted model does not bind), the weak
// point's open / hide animation cycle, 4.1's own death choreography (the carrier's stock death runs), online sync (the parts
// run their machines on every peer; bullets are EDF6's own and sync as they do).
#include "crew.h"
#include "edf41classes.h"
#include "memory.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace crew {
namespace {
using namespace edf41c;
constexpr int kSlots=49,kPartSlots=45;
constexpr int kSlotAccept=16;
constexpr std::size_t kWeakAssign=0x6CEBB0;   // weak_ptr assign (dst, src); the bullet code uses it (edf41classes.cpp)
constexpr float kPi=3.14159265f;

Clone hullClass{"UfoCarrier508",kCarrierVtable,kSlots,kCarrierSlots,{},{},{}};
Clone partClass{"EventAnimationObject",kEaoVtable,kPartSlots,kEaoSlots,{},{},{}};
std::atomic<int> created{0};

void Fail(const char* what) noexcept {
    static std::mutex lock;
    static const char* said[32];
    static int count=0;
    std::lock_guard<std::mutex> hold(lock);
    for(int i=0;i<count;++i)
        if(said[i]==what)return;
    if(count<32)said[count++]=what;
    Log("EDF41MOTHER check failed: %s",what);
}
bool Rd(const void* p,std::size_t size,const char* what) noexcept {
    if(Readable(p,size))return true;
    Fail(what);
    return false;
}
float F(const void* o,std::size_t at) noexcept { float v=0.0f; if(Readable(static_cast<const char*>(o)+at,4))std::memcpy(&v,static_cast<const char*>(o)+at,4); return v; }
void SetF(void* o,std::size_t at,float v) noexcept { if(Readable(static_cast<char*>(o)+at,4))std::memcpy(static_cast<char*>(o)+at,&v,4); }
std::int32_t I(const void* o,std::size_t at) noexcept { std::int32_t v=0; if(Readable(static_cast<const char*>(o)+at,4))std::memcpy(&v,static_cast<const char*>(o)+at,4); return v; }

void RotY(float a,float* m) noexcept {
    const float c=std::cos(a),s=std::sin(a);
    const float r[16]={c,0,-s,0, 0,1,0,0, s,0,c,0, 0,0,0,1};
    std::memcpy(m,r,sizeof(r));
}
void Translate(float x,float y,float z,float* m) noexcept {
    const float r[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, x,y,z,1};
    std::memcpy(m,r,sizeof(r));
}
// p (a point in the matrix's space) in world space.
void Point(const float* m,const float* p,float* out) noexcept {
    for(int k=0;k<3;++k)out[k]=p[0]*m[k]+p[1]*m[4+k]+p[2]*m[8+k]+m[12+k];
}
void Direction(const float* m,const float* d,float* out) noexcept {
    for(int k=0;k<3;++k)out[k]=d[0]*m[k]+d[1]*m[4+k]+d[2]*m[8+k];
}
// A direction within `angle` of `axis` (4.1 0x51B7C0: uniform angle in [0, angle], uniform azimuth).
void Cone(const float* axis,float angle,std::uint64_t& rng,float* out) noexcept {
    float a[3]={axis[0],axis[1],axis[2]};
    const float l=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
    if(l<1e-6f) { out[0]=0; out[1]=-1; out[2]=0; return; }
    for(float& v:a)v/=l;
    float m[16];
    const float zero[3]={0,0,0};
    AimMatrix(zero,a,m);
    const float t=angle*Random01(rng),s=2.0f*kPi*Random01(rng);
    const float local[3]={std::sin(t)*std::cos(s),std::sin(t)*std::sin(s),std::cos(t)};
    Direction(m,local,out);
}

// ---- the records ------------------------------------------------------------------------------------------------
enum Group { kHatch=0, kCover=1, kCannon=2, kBigCannon=3, kPanel=4 };
const char* const kGroupNames[5]={"HatchLow","HatchMiddle","Cannon","BigCannon","Panel"};

struct Wave { wchar_t path[260]; int count=0; int interval=1; int delay=0; float level=1.0f; bool active=false; int kind=1; };
struct Part {
    void* hull=nullptr;
    float local[16];                 // A: the entry's euler and position
    int group=0,index=0,ring=0;
    bool enabled=false,attack=false,boost=false,statik=false;
    bool sized=false,dead=false,falling=false,gone=false;
    bool registered=false;
    std::int32_t team=-99;
    float fallVel[3]={},lastPos[3]={};
    bool haveLast=false;
    int state=0,frames=0;            // the main machine
    int beamState=0,beamFrames=0,beamNext=0;
    bool roundStarted=false;
    float spin=0.0f;                 // main cannon: its own axis (4.1 +0xE54); panel: the ring (+0xB34)
    float ringFactor=0.0f,push=0.0f; // panel +0xB3C, +0xB58
    bool started=false,firstStart=false;
    float explodeDelay=0.0f;         // panel: 3..7 s after its death
    int deadFrames=0;
    int type=0;                      // panel mp_type
    int waitFrames=0;                // cannon +0x11C8
    float phi=0.0f;                  // main cannon sweep plane
    int damageCooldown=0;
    float openFrames=120.0f;         // hatch: its open clip
    int closeWait=0;
    bool permit=false,waveDone=false;
    Wave current,permanent;
    int remaining=0,intervalLeft=0,delayLeft=0,made=0;
    LocatorHandle spawnAt{};
    bool spawnTried=false;
    int cannonBone=-2,hatchBones[4]={-2,-2,-2,-2};
    float beamOffsets[4][3]={};
    SoundHandle chargeLoop;
    std::uint64_t rng=0;
};
struct Hull {
    std::vector<void*> parts;        // creation order
    bool built=false,dead=false;
    std::uint32_t ticks=0;
    float yawRate=0.0f,yawTarget=0.0003f;   // 4.1 +0xA14 / +0xA24 (the ctor queues action 2)
    int throttle[6]={};
    int hitFrames=0;
    float lastDurability=-1.0f;
    int weakBone=-2,weakBody=-2;
    std::vector<std::pair<int,Wave>> pending;   // generators that came before the parts were made
    std::uint64_t rng=0;
};
std::recursive_mutex lock;
int shotCount[5]={},removedCount[5]={};   // bullets fired and parts removed, for the periodic line
std::unordered_map<void*,Hull> hulls;
std::unordered_map<void*,Part> parts;

Hull* HullOf(void* h) noexcept { const auto it=hulls.find(h); return it==hulls.end() ? nullptr : &it->second; }
Part* PartOf(void* p) noexcept { const auto it=parts.find(p); return it==parts.end() ? nullptr : &it->second; }
bool Throttle(Hull& h,int slot) noexcept { const int old=h.throttle[slot]; h.throttle[slot]=16; return old==0; }
// The hull's team as its parts take it (4.1 0x250520): -1 or 3 (neutral) -> 1, else the hull's.
std::int32_t PartTeam(void* hull) noexcept { const std::int32_t t=I(hull,kTeamAt); return t==-1 || t==3 ? 1 : t; }
bool HullAlive(void* hull) noexcept { return F(hull,kDurabilityAt)>0.0f; }
// A part's end (4.1 0x56BBC0): deleted by the game's own DeleteObject; the record goes when its destructor runs.
void Remove(void* obj,Part& p) noexcept {
    DeleteObject(obj);
    p.gone=true;
    ++removedCount[p.group];
    if(p.group==kBigCannon)Log("EDF41MOTHER main cannon %p removed: the hull's weak point can be hit",obj);
}

// A custom parameter node of a part's SGO, copied once for the bullets (keyed by the node: parts of one SGO share it).
const SgoNode* Custom(const SgoNode* node) noexcept {
    static std::unordered_map<const SgoNode*,const SgoNode*> copies;
    if(!node)return nullptr;
    const auto it=copies.find(node);
    if(it!=copies.end())return it->second;
    const SgoNode* c=Persist(node);
    copies[node]=c;
    return c;
}
bool FactoryName(void* obj,const wchar_t* key,wchar_t* out,std::size_t room,const wchar_t* fallback) noexcept {
    if(Text(Key(obj,key),out,room))return true;
    wcsncpy_s(out,room,fallback,_TRUNCATE);
    return false;
}
void PartPos(void* obj,float* out) noexcept { std::memcpy(out,static_cast<char*>(obj)+kPosAt,12); }
void Matrix(void* obj,float* out) noexcept { std::memcpy(out,static_cast<char*>(obj)+kMatrixAt,64); }

// ---- firing -------------------------------------------------------------------------------------------------------
bool AimAt(void* part,float* aim) noexcept {
    void* t=Target(part);
    if(t && AimPoint(t,aim))return true;
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<6) {
        const char* o=static_cast<const char*>(part);
        Log("EDF41MOTHER part %p has no target to fire at: team %d, flags +0x380 %08X, active %u, retarget %d/%d, target ptr %p",part,
            I(part,kTeamAt),static_cast<unsigned>(I(part,kFlags380)),static_cast<unsigned>(static_cast<unsigned char>(o[0x4A0])),I(part,0x510),I(part,0x514),t);
    }
    return false;
}

// The main cannon's shot f of the sweep (4.1 0x2570C0): direction (sin q, -cos q cos phi, -cos q sin phi), q = (f/30 - 1) 1.5,
// from local (0, -790, 0); every 8th a heavy one (blast radius 100, custom [2], the hit sound).
void BigCannonShot(void* obj,Part& p,int f) noexcept {
    float w[16];
    Matrix(obj,w);
    const float q=(static_cast<float>(f)/30.0f-1.0f)*1.5f;
    const float d[3]={std::sin(q),-std::cos(q)*std::cos(p.phi),-std::cos(q)*std::sin(p.phi)};
    const float o[3]={0.0f,-790.0f,0.0f};
    float from[3],dir[3],m[16];
    Point(w,o,from);
    Direction(w,d,dir);
    AimMatrix(from,dir,m);
    const bool heavy=f%8==0;
    const SgoNode* custom=Key(obj,L"mp_LaserCustomParameter");
    wchar_t factory[64];
    FactoryName(obj,L"mp_BulletFactory",factory,64,L"LaserBullet02");
    Bullet b;
    b.factory=factory;
    b.speed=20.0f; b.life=60; b.damage=5000.0f; b.size=50.0f;
    b.explosion=heavy ? 100.0f : 0.0f;
    b.custom=Custom(Child(custom,heavy ? 1 : 0));
    if(Fire(obj,m,b))++shotCount[kBigCannon];
}

void CannonRocket(void* obj,Part& p,float s) noexcept {
    if(p.cannonBone==-2) {
        wchar_t bone[32]=L"cannon";
        Text(Child(Key(obj,L"mpc_FireCannonSetting"),0),bone,32);
        p.cannonBone=BoneIndex(static_cast<char*>(obj)+kEaoSkeleton,bone);
    }
    float b[16];
    if(!BoneMatrix(static_cast<char*>(obj)+kEaoSkeleton,p.cannonBone,b)) { Fail("the cannon has no cannon bone"); return; }
    const float o[3]={0.0f,-360.0f,0.0f},down[3]={0.0f,-1.0f,0.0f};
    float from[3],dir[3],m[16];
    Point(b,o,from);
    Direction(b,down,dir);
    AimMatrix(from,dir,m);
    wchar_t factory[64];
    FactoryName(obj,L"mpc_CannonBulletFactory",factory,64,L"RocketBullet01");
    Bullet r;
    r.factory=factory;
    r.speed=0.01f; r.life=2700; r.damage=350.0f*s; r.explosion=80.0f; r.size=80.0f;
    const float color[4]={1.0f,0.5f,0.25f,1.0f};
    std::memcpy(r.color,color,16);
    r.custom=Custom(Key(obj,L"mp_CannonCustomParameter"));
    if(Fire(obj,m,r))++shotCount[kCannon];
}

void CannonBeam(void* obj,Part& p,float s) noexcept {
    const int k=p.beamNext;
    p.beamNext=(p.beamNext+1)%4;
    float aim[3];
    if(!AimAt(obj,aim))return;
    const SgoNode* muzzles=Key(obj,L"mpc_FireBeamSetting");
    if(p.hatchBones[k]==-2) {
        wchar_t bone[32]=L"";
        const SgoNode* e=Child(muzzles,k);
        if(!Text(Child(e,0),bone,32) || !Vec(Child(e,1),p.beamOffsets[k],3)) { p.hatchBones[k]=-1; Fail("a cannon's mpc_FireBeamSetting entry is not [bone, offset]"); return; }
        p.hatchBones[k]=BoneIndex(static_cast<char*>(obj)+kEaoSkeleton,bone);
        if(p.hatchBones[k]<0)Log("EDF41MOTHER cannon %p has no bone %ls for beam muzzle %d",obj,bone,k);
    }
    float b[16];
    if(!BoneMatrix(static_cast<char*>(obj)+kEaoSkeleton,p.hatchBones[k],b))return;
    float from[3],way[3],dir[3],m[16];
    Point(b,p.beamOffsets[k],from);
    for(int i=0;i<3;++i)way[i]=aim[i]-from[i];
    Cone(way,std::fmax(0.005f,0.2f-0.016f*s),p.rng,dir);
    AimMatrix(from,dir,m);
    wchar_t factory[64];
    FactoryName(obj,L"mpc_BeamBulletFactory",factory,64,L"BeamBullet01");
    Bullet r;
    r.factory=factory;
    r.speed=std::fmin(20.0f,5.0f+0.1f*s); r.life=180; r.damage=0.5f*s; r.size=std::fmax(2.0f,4.0f-0.2f*s);
    const float c=std::fmin(1.0f,1.0f+0.1f*s);
    const float color[4]={0.1f*c,1.2f*c,0.1f*c,1.0f};
    std::memcpy(r.color,color,16);
    r.custom=Custom(Key(obj,L"mpc_BeamBulletCustomParam"));
    if(Fire(obj,m,r))++shotCount[kCannon];
}

void PanelShot(void* obj,Part& p,float s) noexcept {
    float aim[3],from[3],way[3],dir[3],m[16];
    if(!AimAt(obj,aim))return;
    PartPos(obj,from);
    for(int i=0;i<3;++i)way[i]=aim[i]-from[i];
    wchar_t factory[64];
    Bullet r;
    if(p.type==0) {
        FactoryName(obj,L"mp_BulletFactory",factory,64,L"LaserBullet02");
        Cone(way,std::fmax(0.005f,0.1f-0.008f*s),p.rng,dir);
        r.speed=20.0f; r.life=60; r.damage=2.0f*s; r.size=std::fmax(0.5f,2.0f-0.125f*s);
        const float color[4]={2.0f,0.1f,0.2f,1.0f};
        std::memcpy(r.color,color,16);
    } else {
        FactoryName(obj,L"mp_BulletFactory",factory,64,L"PlasmaBullet01");
        Cone(way,0.01f,p.rng,dir);
        r.speed=std::fmin(2.0f,0.5f+0.075f*(s-0.1f)); r.life=1200; r.damage=25.0f*s;
        r.explosion=std::fmin(15.0f,10.0f+0.05f*(s-10.0f)); r.size=5.0f;
        const float color[4]={3.0f,1.0f,20.0f,1.0f};
        std::memcpy(r.color,color,16);
    }
    r.factory=factory;
    r.custom=Custom(Key(obj,L"mp_BulletCustomParameter"));
    AimMatrix(from,dir,m);
    if(Fire(obj,m,r))++shotCount[kPanel];
}

// A point near the part (4.1 picks one inside its physics shell: 0x337A60).
void Near(void* obj,Part& p,float radius,float* out) noexcept {
    PartPos(obj,out);
    for(int k=0;k<3;++k)out[k]+=(Random01(p.rng)*2.0f-1.0f)*radius;
}
void RandomDown(Part& p,float maxAngle,float* out) noexcept {
    const float down[3]={0.0f,-1.0f,0.0f};
    Cone(down,maxAngle,p.rng,out);
}

// ---- the state machines (4.1, one to one) ---------------------------------------------------------------------------
void Switch(Part& p,int state) noexcept { p.state=state; p.frames=0; }
enum { kBigClosed=1,kBigOpen,kBigStandby,kBigCharge,kBigFire,kBigDestroyed,kBigClose,kBigFall };
enum { kCanDormant=1,kCanAppear,kCanStandby,kCanWait,kCanFire,kCanDestroyed,kCanFall };
enum { kPanDormant=1,kPanStart,kPanLaser,kPanPlasma,kPanDestroyed };
enum { kHatClosed=1,kHatOpening,kHatOpen,kHatClosing };

void BigCannonTick(void* obj,Part& p) noexcept {
    const int f=p.frames;
    float pos[3];
    PartPos(obj,pos);
    switch(p.state) {
        case kBigClosed:
            if(p.enabled) { Switch(p,kBigOpen); PlayState(obj,L"open"); PlaySe(p.hull,L"母船UFO砲台出現",pos); }
            break;
        case kBigOpen:
            if(f>1 && AnimDone(obj)) { Switch(p,kBigStandby); PlaySe(p.hull,L"母船UFO砲台出現完了",pos); }
            break;
        case kBigStandby: {
            const bool trigger=p.boost;
            p.boost=false;   // 4.1 clears the one-shot trigger every frame of this state
            if(!p.enabled) { Switch(p,kBigClose); PlayState(obj,L"close"); PlaySe(p.hull,L"母船UFO砲台収納",pos); }
            else if((p.attack || trigger) && !p.statik)Switch(p,kBigCharge);
            break;
        }
        case kBigCharge:
            if(f==720)PlaySe(p.hull,L"母船UFO砲台チャージ",pos,&p.chargeLoop);
            if(p.chargeLoop.instance)MoveSound(p.chargeLoop,pos);
            if(f==1045)PlaySe(p.hull,L"母船UFO砲台チャージ完了",pos);
            if(f==1180)StopSound(p.chargeLoop);
            if(!p.enabled) { StopSound(p.chargeLoop); Switch(p,kBigClose); PlayState(obj,L"close"); }
            else if(f>=1200) { StopSound(p.chargeLoop); Switch(p,kBigFire); p.phi=Random01(p.rng)-0.5f; PlaySe(p.hull,L"母船UFO砲台発射",pos); }
            break;
        case kBigFire:
            if(f>=1)BigCannonShot(obj,p,f);
            if(f>60)Switch(p,kBigStandby);
            break;
        case kBigDestroyed:
            if(f<180 && f%8==0) { float at[3],dir[3]; Near(obj,p,30.0f,at); RandomDown(p,kPi,dir); Blazing(at,dir,0.25f); }
            if(f>300) { Switch(p,kBigFall); PlaySe(p.hull,L"母船UFO砲台墜落",pos); p.falling=true; }
            break;
        case kBigClose:
            if(f>1 && AnimDone(obj)) { Switch(p,kBigClosed); PlayState(obj,L"close_state"); PlaySe(p.hull,L"母船UFO砲台収納完了",pos); }
            break;
        case kBigFall:
            if(f==360 || f==380 || f==400) {
                const float at[3]={pos[0],pos[1]-(f==360 ? 400.0f : f==380 ? 500.0f : 600.0f),pos[2]};
                float dir[3];
                RandomDown(p,0.7f*kPi,dir);
                BigExplosion(at,dir,3.0f,1.0f);
                if(f==360)PlaySe(p.hull,L"母船UFO砲台爆発",at);
            }
            if(f>420) { Remove(obj,p); }
            break;
        default: Switch(p,kBigClosed); break;
    }
}

void CannonTick(void* obj,Part& p,Hull& h,float s) noexcept {
    const int f=p.frames;
    float pos[3];
    PartPos(obj,pos);
    switch(p.state) {
        case kCanDormant:
            if(p.enabled) { Switch(p,kCanAppear); PlayState(obj,L"appear"); PlaySe(p.hull,L"母船UFO中型砲台出現",pos); }
            break;
        case kCanAppear:
            if(f>1 && AnimDone(obj)) { Switch(p,kCanStandby); PlaySe(p.hull,L"母船UFO中型砲台出現残響",pos); }
            break;
        case kCanStandby:
            if(p.attack && !p.statik) { p.roundStarted=true; Switch(p,kCanWait); }
            break;
        case kCanWait:
            if(p.waitFrames<f) { Switch(p,kCanFire); p.waitFrames=1800+static_cast<int>(Random01(p.rng)*1800.0f); }
            break;
        case kCanFire:
            if(f==450 && Throttle(h,3))PlaySe(p.hull,L"母船UFO中型砲台チャージ",pos);
            if(f>=600) {
                Switch(p,kCanStandby);
                CannonRocket(obj,p,s);
                if(Throttle(h,4))PlaySe(p.hull,L"母船UFO中型砲台発射",pos);
            }
            break;
        case kCanDestroyed:
            if(f<180 && f%8==0) { float at[3],dir[3]; Near(obj,p,20.0f,at); RandomDown(p,kPi,dir); Blazing(at,dir,0.25f); }
            if(f>300) { Switch(p,kCanFall); PlaySe(p.hull,L"母船UFO中型砲台墜落",pos); p.falling=true; }
            break;
        case kCanFall:
            if(f>=288 && (f-288)%4==0 && (f-288)/4<8) {
                const int k=(f-288)/4;
                float b[16];
                if(p.cannonBone==-2)p.cannonBone=BoneIndex(static_cast<char*>(obj)+kEaoSkeleton,L"cannon");
                if(BoneMatrix(static_cast<char*>(obj)+kEaoSkeleton,p.cannonBone,b)) {
                    const float off[3]={0.0f,-(100.0f+30.0f*static_cast<float>(k)),0.0f};
                    float at[3],dir[3];
                    Point(b,off,at);
                    RandomDown(p,0.7f*kPi,dir);
                    Explosion(at,dir,10.0f);
                    if(k%2==0)PlaySe(p.hull,L"母船UFO中型砲台爆発",at);
                }
            }
            if(f>320) { Remove(obj,p); }
            break;
        default: Switch(p,kCanDormant); break;
    }
    // The beam machine: only while the cannon lives.
    if(!p.dead) {
        if(p.beamState==0) {
            if(p.attack && !p.statik && p.roundStarted) { p.beamState=1; p.beamFrames=0; }
        } else {
            if(!p.attack || p.statik) { p.beamState=0; p.roundStarted=false; }
            else {
                if(p.beamFrames%4==0) {
                    if(Throttle(h,5))PlaySe(p.hull,L"母船UFO中型砲台ビーム",pos);
                    CannonBeam(obj,p,s);
                }
                ++p.beamFrames;
            }
        }
    }
}

void PanelTick(void* obj,Part& p,Hull& h,float s) noexcept {
    const int f=p.frames;
    const float t=static_cast<float>(f)/60.0f;
    float pos[3];
    PartPos(obj,pos);
    switch(p.state) {
        case kPanDormant:
            if(p.enabled)Switch(p,kPanStart);
            break;
        case kPanStart:
            if(t>2.0f && !p.firstStart) {
                p.firstStart=true;
                float hp[3];
                PartPos(p.hull,hp);
                if(Throttle(h,0))PlaySe(p.hull,L"母船UFOパネル起動",hp);
            }
            if(t>2.0f)p.started=true;
            if(t>6.0f && p.attack && !p.statik)Switch(p,p.type==0 ? kPanLaser : kPanPlasma);
            break;
        case kPanLaser:
        case kPanPlasma: {
            if(!p.attack || p.statik) { Switch(p,kPanStart); break; }
            const int every=p.state==kPanLaser ? (std::max)(30,static_cast<int>(120.0f-5.0f*s)) : 300;
            if(f%every==0) {
                if(Throttle(h,p.state==kPanLaser ? 1 : 2))PlaySe(p.hull,p.state==kPanLaser ? L"母船UFOパネルレーザー" : L"母船UFOパネルキャノン",pos);
                PanelShot(obj,p,s);
            }
            break;
        }
        case kPanDestroyed:
            if(t<2.0f && p.deadFrames%8==0) { float at[3],dir[3]; Near(obj,p,8.0f,at); RandomDown(p,kPi,dir); FireBurst(at,dir,5.0f); }
            ++p.deadFrames;
            if(t>p.explodeDelay) {
                float dir[3];
                RandomDown(p,kPi,dir);
                Explosion(pos,dir,10.0f);
                PlaySe(p.hull,L"母船UFOパネル爆発",pos);
                Remove(obj,p);
            }
            break;
        default: Switch(p,kPanDormant); break;
    }
}

// ---- the hatch: 4.1's ObjectGenerator (0x330E70 / 0x331270 / 0x3313C0) ------------------------------------------------
void SpawnOne(void* obj,Part& p) noexcept {
    if(!p.spawnTried) {
        p.spawnTried=true;
        if(!FindLocator(static_cast<char*>(obj)+kEaoModel,L"発生位置",&p.spawnAt))
            Log("EDF41MOTHER hatch #%d has no locator 発生位置: enemies come out at its origin",p.index);
    }
    float at[16],m[16];
    if(!LocatorMatrix(p.spawnAt,at))Matrix(obj,at);
    RotY(2.0f*kPi*Random01(p.rng),m);
    m[12]=at[12]; m[13]=at[13]; m[14]=at[14];
    void* enemy=CreateFromPath(m,p.current.path);
    ++p.made;
    if(!enemy) {
        static std::atomic<int> said{0};
        if(said.fetch_add(1)<8)Log("EDF41MOTHER hatch #%d: the game made no %ls",p.index,p.current.path);
        return;
    }
    // What EDF6's own generator does to what it makes (0x6E335F..0x6E3381): level, team (registered), activate.
    const float level=p.current.level*F(p.hull,kRawLevelAt);
    SetLevel(enemy,level);
    SetTeam(enemy,PartTeam(p.hull),true);
    Activate(enemy);
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<12)Log("EDF41MOTHER hatch #%d: enemy #%d %ls at %.0f %.0f %.0f, raw level %.2f, team %d",p.index,p.made,p.current.path,
                                m[12],m[13],m[14],level,PartTeam(p.hull));
}

void HatchTick(void* obj,Part& p) noexcept {
    float pos[3];
    PartPos(obj,pos);
    if(p.current.active) {   // the generator, each frame
        if(p.delayLeft>0)--p.delayLeft;
        else if(p.intervalLeft>0)--p.intervalLeft;
        else if(p.permit) {
            SpawnOne(obj,p);
            p.intervalLeft=p.current.interval;
            if(--p.remaining<=0) {
                p.waveDone=true;
                p.permit=false;
                p.current.active=false;
                if(p.permanent.active) { p.current=p.permanent; p.remaining=p.current.count; p.delayLeft=p.current.delay; p.intervalLeft=0; }
            }
        }
    }
    switch(p.state) {
        case kHatClosed:
            if(p.current.active && static_cast<float>(p.delayLeft)<=p.openFrames) {
                Switch(p,kHatOpening);
                PlayState(obj,L"open");
                PlaySe(p.hull,L"母船UFO発進口開く",pos);
            }
            break;
        case kHatOpening: {
            float len=0.0f;
            if(p.frames>1 && AnimDone(obj,&len)) {
                if(len>1.0f)p.openFrames=len;
                Switch(p,kHatOpen);
                p.permit=true;
                p.closeWait=240;
                p.waveDone=false;
                PlaySe(p.hull,L"母船UFO発進口開く完了",pos);
            }
            break;
        }
        case kHatOpen:
            if(p.waveDone && --p.closeWait<=0) {
                Switch(p,kHatClosing);
                p.waveDone=false;
                PlayState(obj,L"close");
                PlaySe(p.hull,L"母船UFO発進口閉じる",pos);
            }
            break;
        case kHatClosing:
            if(p.frames>1 && AnimDone(obj)) { Switch(p,kHatClosed); PlaySe(p.hull,L"母船UFO発進口閉じる完了",pos); }
            break;
        default: Switch(p,kHatClosed); break;
    }
}

// The 0x20100 block (EDF6 = 4.1's offsets + 8, SPEC2 5.2): +0x08 kind (2100: 0, standing; 2101: 1, once), +0x0C the delay
// between waves, +0x10 count, +0x14 interval (frames), +0x18 level, +0x20 std::wstring the SGO, +0x48 the hatch.
void AssignWave(Hull& h,int index,const Wave& w) noexcept;
bool QueueGenerator(void* hullObj,const char* d) noexcept {
    if(!Rd(d,0x50,"generator message block"))return false;
    std::int32_t kind,delay,count,frames,index;
    float level;
    std::memcpy(&kind,d+8,4); std::memcpy(&delay,d+0xC,4); std::memcpy(&count,d+0x10,4); std::memcpy(&frames,d+0x14,4);
    std::memcpy(&level,d+0x18,4); std::memcpy(&index,d+0x48,4);
    std::uint64_t size=0,capacity=0;
    std::memcpy(&size,d+0x30,8); std::memcpy(&capacity,d+0x38,8);
    const wchar_t* text=capacity<8 ? reinterpret_cast<const wchar_t*>(d+0x20) : *reinterpret_cast<const wchar_t* const*>(d+0x20);
    if(size==0 || size>=260 || !Rd(text,(size+1)*sizeof(wchar_t),"generator enemy path")) { Fail("a generator message without a readable enemy path"); return false; }
    if(count<=0 || count>4096 || index<0 || index>=64) { Fail("a generator message with a count or hatch index out of range"); return false; }
    Hull* h=HullOf(hullObj);
    if(!h)return false;
    Wave w;
    std::wmemcpy(w.path,text,static_cast<std::size_t>(size)); w.path[size]=L'\0';
    w.count=count; w.interval=(std::max)(1,frames); w.delay=(std::max)(0,delay); w.level=level>0.0f && std::isfinite(level) ? level : 1.0f;
    w.active=true; w.kind=kind;
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<24)Log("EDF41MOTHER generator on hatch %d: %d x %ls every %d frames, kind %d, wave delay %d, level %.2f%s",index,count,w.path,
                                w.interval,kind,delay,level,h->built ? "" : " (before the parts are made: kept until they are)");
    if(!h->built) { h->pending.emplace_back(index,w); return true; }   // the script asks on the creation frame, before the parts
    AssignWave(*h,index,w);
    return true;
}
void AssignWave(Hull& h,int index,const Wave& w) noexcept {
    Part* hatch=nullptr;
    for(void* o:h.parts) { Part* p=PartOf(o); if(p && p->group==kHatch && p->index==index) { hatch=p; break; } }
    if(!hatch) { Log("EDF41MOTHER generator for hatch %d: the hull has no such hatch, dropped",index); return; }
    const int kind=w.kind;
    if(kind==0)hatch->permanent=w;
    if(kind!=0 || !hatch->current.active) {
        hatch->current=w;
        hatch->remaining=w.count;
        hatch->delayLeft=kind==0 ? w.delay : 0;
        hatch->intervalLeft=0;
    }
}

// ---- ObjectAction (4.1 0x250870, 18 entries) and GetPartsObject ------------------------------------------------------
const char* const kActionNames[18]={"glow off, spin stops at once","glow white, spin stops","glow slow red, spin 0.0003",
    "BigCannon enable","BigCannon disable","BigCannon attack on","BigCannon attack off","BigCannon fire once",
    "Cannon enable","Cannon disable","Cannon attack on","Cannon attack off","Cannon boost",
    "Panel enable","Panel disable","Panel attack on","Panel attack off","Panel boost"};
void ApplyAction(void* hullObj,Hull& h,int action) noexcept {
    if(action==0) { h.yawTarget=0.0f; h.yawRate=0.0f; }
    else if(action==1)h.yawTarget=0.0f;
    else if(action==2)h.yawTarget=0.0003f;
    int touched=0;
    // 4.1 0x251BC0: the group's flag is set only while the HULL lives (not the part).
    if(action>=3 && action<=17 && HullAlive(hullObj)) {
        const int group=action<8 ? kBigCannon : action<13 ? kCannon : kPanel;
        const int what=(action-3)%5;
        for(void* o:h.parts) {
            Part* p=PartOf(o);
            if(!p || p->group!=group)continue;
            ++touched;
            if(what==0)p->enabled=true; else if(what==1)p->enabled=false; else if(what==2)p->attack=true; else if(what==3)p->attack=false; else p->boost=true;
        }
    }
    Log("EDF41MOTHER ObjectAction %d (%s) on %p: %d parts",action,action>=0 && action<18 ? kActionNames[action] : "unknown to 4.1",hullObj,touched);
}

bool GetParts(Hull& h,char* data) noexcept {
    if(!Rd(data,0x18,"GetPartsObject block"))return false;
    std::int32_t group,index;
    std::memcpy(&group,data,4);
    std::memcpy(&index,data+4,4);
    void* found=nullptr;
    for(void* o:h.parts) {
        Part* p=PartOf(o);
        if(!p || p->gone)continue;
        if(group==0 && p->group==kBigCannon) { found=o; break; }
        if(group==1 && p->group==kCannon && p->index==index) { found=o; break; }
    }
    Log("EDF41MOTHER GetPartsObject group %d index %d -> %p",group,index,found);
    if(found)reinterpret_cast<void*(__fastcall*)(void*,const void*)>(image+kWeakAssign)(data+8,static_cast<char*>(found)+kWeakSelfAt);
    return true;
}

// ---- making the parts -----------------------------------------------------------------------------------------------
bool MakePart(void* hullObj,Hull& h,const SgoNode* entry,int group,int index,int ring) noexcept {
    wchar_t path[260];
    float euler[3],position[3];
    if(!Text(Child(entry,0),path,260) || !Vec(Child(entry,1),euler,3) || !Vec(Child(entry,2),position,3)) {
        Fail("a part entry of the hull's SGO is not [path, euler, position]");
        return false;
    }
    Part rec;
    rec.hull=hullObj; rec.group=group; rec.index=index; rec.ring=ring;
    rec.statik=group==kPanel && ring>=2;   // 4.1 0x24FFF0: the last two panel rings are hung with r9b = 0 (+0x904)
    EulerMatrix(euler,position,rec.local);
    rec.rng=(static_cast<std::uint64_t>(std::fabs(position[0]*7.0f+position[1]*13.0f+position[2]*31.0f)*1000.0f)^0xDEADBEEFBEEFDEADull)+static_cast<std::uint64_t>(index);
    float world[16],hullM[16];
    Matrix(hullObj,hullM);
    Multiply(rec.local,hullM,world);
    void* part=CreateFromPath(world,path);
    if(!part) { Log("EDF41MOTHER part %s #%d (%ls): the game made nothing",kGroupNames[group],index,path); return false; }
    rec.state=group==kBigCannon ? kBigClosed : group==kCannon ? kCanDormant : group==kPanel ? kPanDormant : kHatClosed;
    parts[part]=rec;
    h.parts.push_back(part);
    return true;
}
int MakeGroup(void* hullObj,Hull& h,const wchar_t* key,int group,bool nested) noexcept {
    const SgoNode* node=Key(hullObj,key);
    if(Count(node)<0) { Log("EDF41MOTHER hull SGO has no array %ls: no %s",key,kGroupNames[group]); return 0; }
    int made=0,index=0;
    for(int i=0;i<node->a;++i) {
        const SgoNode* c=Child(node,i);
        if(!c)break;
        if(!nested) { made+=MakePart(hullObj,h,c,group,index++,0) ? 1 : 0; continue; }
        for(int j=0;j<Count(c);++j)made+=MakePart(hullObj,h,Child(c,j),group,index++,i) ? 1 : 0;
    }
    return made;
}
void SpawnParts(void* hullObj,Hull& h) noexcept {
    if(h.built)return;
    h.built=true;
    const int mask=Cfg().edf41MotherParts;
    const ULONGLONG t0=GetTickCount64();
    int n[5]={};
    if(mask&1)n[kHatch]=MakeGroup(hullObj,h,L"mp_HatchLow",kHatch,false);
    if(mask&2)n[kCover]=MakeGroup(hullObj,h,L"mp_HatchMiddle",kCover,false);
    if(mask&4)n[kCannon]=MakeGroup(hullObj,h,L"mp_Cannon",kCannon,false);
    if(mask&8)n[kBigCannon]=MakeGroup(hullObj,h,L"mp_BigCannon",kBigCannon,false);
    if(mask&16)n[kPanel]=MakeGroup(hullObj,h,L"mp_Panel",kPanel,true);
    Log("EDF41MOTHER parts of %p made in %llu ms (ini Edf41MotherParts=%d): %d hatches, %d covers, %d cannons, %d main cannon, %d panels",
        hullObj,static_cast<unsigned long long>(GetTickCount64()-t0),mask,n[0],n[1],n[2],n[3],n[4]);
}

// ---- the part's frame -----------------------------------------------------------------------------------------------
// Where it hangs (4.1 0x25E840): world = L2 x A x L1 x hull; a falling part leaves the hull with the step it last moved by and
// drops 0.002 per frame squared, its orientation frozen.
void Place(void* obj,Part& p) noexcept {
    float w[16];
    if(p.falling) {
        Matrix(obj,w);
        p.fallVel[1]-=0.002f;
        for(int k=0;k<3;++k)w[12+k]+=p.fallVel[k];
    } else {
        float l1[16],l2[16],hullM[16],a[16];
        RotY(p.group==kPanel ? p.spin : 0.0f,l1);
        if(p.group==kBigCannon)RotY(p.spin,l2);
        else Translate(0.0f,0.0f,p.group==kPanel ? p.push : 0.0f,l2);
        Matrix(p.hull,hullM);
        Multiply(l2,p.local,a);
        Multiply(a,l1,a);
        Multiply(a,hullM,w);
        if(p.haveLast)for(int k=0;k<3;++k)p.fallVel[k]=w[12+k]-p.lastPos[k];
        for(int k=0;k<3;++k)p.lastPos[k]=w[12+k];
        p.haveLast=true;
    }
    std::memcpy(static_cast<char*>(obj)+kMatrixAt,w,sizeof(w));
}

// 4.1 0x250520 each frame: the part's team is the hull's (neutral -> 1); a part not enabled (or the neutral ship's main cannon)
// takes no damage and is not registered (not a target, not counted, not lockable); the static panels are never registered.
void Masking(void* obj,Part& p) noexcept {
    const std::int32_t hullTeam=I(p.hull,kTeamAt);
    const bool immune=!p.enabled || (p.group==kBigCannon && hullTeam==3) || p.dead;
    SetFlag(obj,kNoHpLoss,immune);
    const bool reg=!immune && !p.statik;
    const std::int32_t team=PartTeam(p.hull);
    const std::int32_t now=I(obj,kTeamAt);
    const bool registeredNow=(static_cast<std::uint32_t>(I(obj,kFlags380))&0x40u)!=0;
    if(reg!=p.registered || team!=p.team || now!=team || registeredNow!=reg) {
        if(p.team==team && now!=team) {   // set before, changed since by someone else: say so once
            static std::atomic<int> said{0};
            if(said.fetch_add(1)<4)Log("EDF41MOTHER part %p: its team was %d, set back to %d",obj,now,team);
        }
        SetTeam(obj,team,reg);
        p.registered=reg;
        p.team=team;
    }
    SetFlag(obj,kNotTarget|kNotCounted,!reg);
}

void PartFrame(void* obj,Part& p,Hull& h) noexcept {
    if(!p.sized) {
        p.sized=true;
        SetLevel(obj,F(p.hull,kRawLevelAt));
        SizeDurability(obj);
        if(p.group==kPanel) {
            std::int32_t t=0;
            p.type=Int(Key(obj,L"mp_type"),&t) ? t : 0;
            p.explodeDelay=3.0f+Random01(p.rng)*4.0f;
        }
    }
    Masking(obj,p);
    const float s=LevelS(obj);
    const bool died=!p.dead && (F(obj,kDurabilityAt)<=0.0f || static_cast<const char*>(obj)[kDestroyedAt]);
    if(died && p.group!=kHatch && p.group!=kCover) {
        p.dead=true;
        p.attack=false;
        float pos[3];
        PartPos(obj,pos);
        StopSound(p.chargeLoop);
        if(p.group==kBigCannon) { Switch(p,kBigDestroyed); PlaySe(p.hull,L"母船UFO砲台破壊",pos); }
        else if(p.group==kCannon) { Switch(p,kCanDestroyed); PlaySe(p.hull,L"母船UFO中型砲台破壊",pos); }
        else if(p.group==kPanel) {
            Switch(p,kPanDestroyed);
            PlaySe(p.hull,L"母船UFOパネル破壊",pos);
            float dir[3];
            RandomDown(p,kPi,dir);
            Explosion(pos,dir,5.0f);
            p.falling=true;
        }
        Log("EDF41MOTHER part %s #%d %p destroyed",kGroupNames[p.group],p.index,obj);
    }
    if(p.group==kBigCannon)p.spin-=0.0003f;
    if(p.group==kPanel && p.started && HullAlive(p.hull) && !p.dead) {
        p.push+=(100.0f-p.push)*0.02f;
        p.ringFactor+=(1.0f-p.ringFactor)*0.001f;
    }
    if(p.group==kPanel && !p.dead)p.spin-=0.001f*p.ringFactor;
    if(p.damageCooldown>0)--p.damageCooldown;
    switch(p.group) {
        case kBigCannon: BigCannonTick(obj,p); break;
        case kCannon: CannonTick(obj,p,h,s); break;
        case kPanel: PanelTick(obj,p,h,s); break;
        case kHatch: HatchTick(obj,p); break;
        default: break;
    }
    ++p.frames;
}

// ---- development switches (ini Edf41MotherForce): what the autopilot cannot make happen (it does not shoot) --------------
// 1: after 5 s every group enabled with attack on (the main cannon's 20 s charge, then its sweep); 2: after 60 s the main
// cannon destroyed (its fall, then the weak point takes damage); 4: after 90 s the cannons destroyed; 8: after 90 s a quarter
// of the panels destroyed.
void Force(void* hullObj,Hull& h) noexcept {
    const int force=Cfg().edf41MotherForce;
    if(!force)return;
    if((force&1) && h.ticks==300) { for(int a:{3,5,8,10,13,15})ApplyAction(hullObj,h,a); Log("EDF41MOTHER dev force: every group enabled, attack on"); }
    const auto kill=[&](int group,int every) {
        int n=0,killed=0;
        for(void* o:h.parts) { Part* p=PartOf(o); if(p && p->group==group && !p->dead && (n++%every)==0) { SetF(o,kDurabilityAt,0.0f); ++killed; } }
        Log("EDF41MOTHER dev force: %d %s destroyed",killed,kGroupNames[group]);
    };
    if((force&2) && h.ticks==3600)kill(kBigCannon,1);
    if((force&4) && h.ticks==5400)kill(kCannon,1);
    if((force&8) && h.ticks==5400)kill(kPanel,4);
}

// ---- the wrappers -------------------------------------------------------------------------------------------------
std::uint64_t __fastcall HullDtor(void* self,unsigned flags) {
    {
        std::lock_guard<std::recursive_mutex> hold(lock);
        Hull* h=HullOf(self);
        if(h)Log("EDF41MOTHER hull %p gone: its %zu parts go with it",self,h->parts.size());
        hulls.erase(self);
    }
    return reinterpret_cast<DtorFn>(hullClass.orig[kSlotDtor])(self,flags);
}

// The hull's spin (4.1: a10 += (a20 - a10) 0.01, the yaw a04 += a10 into the matrix): applied as a turn of this frame's rows.
void Spin(void* self,Hull& h) noexcept {
    h.yawRate+=(h.yawTarget-h.yawRate)*0.01f;
    if(std::fabs(h.yawRate)<1e-7f)return;
    float m[16],r[16],out[16];
    Matrix(self,m);
    RotY(h.yawRate,r);
    Multiply(m,r,out);
    std::memcpy(static_cast<char*>(self)+kMatrixAt,out,48);
}

// A hit on the hull: 64 frames of 8 fire bursts round the weak point (4.1 0x251DE0 / 0x2512C0).
void HullHits(void* self,Hull& h) noexcept {
    const float hp=F(self,kDurabilityAt);
    if(h.lastDurability>=0.0f && hp<h.lastDurability && hp>0.0f && h.hitFrames==0) {
        h.hitFrames=64;
        float pos[3];
        PartPos(self,pos);
        PlaySe(self,L"母船UFOダメージ",pos);
    }
    h.lastDurability=hp;
    if(h.hitFrames<=0)return;
    if((h.hitFrames&7)==0) {
        if(h.weakBone==-2)h.weakBone=BoneIndex(static_cast<char*>(self)+kCarrierSkeleton,L"weak");
        float b[16];
        if(!BoneMatrix(static_cast<char*>(self)+kCarrierSkeleton,h.weakBone,b))Matrix(self,b);
        const float a=2.0f*kPi*Random01(h.rng),r=80.0f*Random01(h.rng);
        const float local[3]={r*std::sin(a),-40.0f,r*std::cos(a)},down[3]={0.0f,-1.0f,0.0f};
        float at[3],dir[3];
        Point(b,local,at);
        Direction(b,down,dir);
        FireBurst(at,dir,20.0f);
    }
    --h.hitFrames;
}

std::uint64_t __fastcall HullUpdate(void* self,void* dt) {
    std::lock_guard<std::recursive_mutex> hold(lock);
    Hull& h=hulls[self];
    if(Cfg().edf41MotherPlace>0.0f) {   // development: held in front of the camera (ini Edf41MotherPlace)
        float eye[3],dir[3];
        if(CameraRay(eye,dir)) {
            const float d=Cfg().edf41MotherPlace;
            const float p[3]={eye[0]+dir[0]*d,eye[1]+dir[1]*d+300.0f,eye[2]+dir[2]*d};
            std::memcpy(static_cast<char*>(self)+kPosAt,p,12);
        }
    }
    const std::uint64_t result=reinterpret_cast<UpdateFn>(hullClass.orig[kSlotUpdate])(self,dt);
    ++h.ticks;
    if(h.ticks==1) {
        h.rng=reinterpret_cast<std::uintptr_t>(self)*0x9E3779B97F4A7C15ull;
        float pos[3];
        PartPos(self,pos);
        Log("EDF41MOTHER hull %p first update: durability %.0f / %.0f, level %.2f (raw %.2f), team %d, pos %.0f %.0f %.0f",self,
            F(self,kDurabilityAt),F(self,kMaxDurabilityAt),F(self,kLevelAt),F(self,kRawLevelAt),I(self,kTeamAt),pos[0],pos[1],pos[2]);
        SpawnParts(self,h);
        for(const auto& job:h.pending)AssignWave(h,job.first,job.second);
        h.pending.clear();
    }
    for(int& t:h.throttle)if(t>0)--t;
    if(HullAlive(self))Spin(self,h);
    HullHits(self,h);
    if(!h.dead && !HullAlive(self)) {
        h.dead=true;
        float pos[3];
        PartPos(self,pos);
        PlaySe(self,L"母船UFO本体墜落前爆発",pos);
        // 4.1 0x251F30: the cannons and panels stop attacking; the main cannon is left as it is.
        for(void* o:h.parts) { Part* p=PartOf(o); if(p && (p->group==kCannon || p->group==kPanel))p->attack=false; }
        Log("EDF41MOTHER hull %p destroyed at its frame %u",self,h.ticks);
    }
    Force(self,h);
    if(h.ticks%1800==0) {
        int alive[5]={},dead[5]={};
        for(void* o:h.parts) { Part* p=PartOf(o); if(p)(p->dead ? dead : alive)[p->group]++; }
        Log("EDF41MOTHER hull %p frame %u: durability %.0f, yaw rate %.5f; parts alive / destroyed: cannons %d/%d, main %d/%d, panels %d/%d; fired: main %d, cannons %d, panels %d; removed: main %d, cannons %d, panels %d",self,h.ticks,
            F(self,kDurabilityAt),h.yawRate,alive[kCannon],dead[kCannon],alive[kBigCannon],dead[kBigCannon],alive[kPanel],dead[kPanel],
            shotCount[kBigCannon],shotCount[kCannon],shotCount[kPanel],removedCount[kBigCannon],removedCount[kCannon],removedCount[kPanel]);
    }
    return result;
}

std::uint64_t __fastcall HullMessage(void* self,std::uint32_t msg,void* data) {
    {
        std::lock_guard<std::recursive_mutex> hold(lock);
        Hull* h=HullOf(self);
        if(h && msg==kMsgAction && data && Rd(data,4,"action message data")) {
            ApplyAction(self,*h,*static_cast<const std::int32_t*>(data));
            return 1;
        }
        if(h && msg==kMsgParts && data) { if(GetParts(*h,static_cast<char*>(data)))return 1; }
        if(h && msg==kMsgGenerator && data && (Cfg().edf41MotherParts&1)) { if(QueueGenerator(self,static_cast<const char*>(data)))return 1; }
    }
    return reinterpret_cast<MessageFn>(hullClass.orig[kSlotMessage])(self,msg,data);
}

// 4.1 slot 10 (0x250FC0): the hull takes damage only on its `weak` body, and only once the main cannon object is gone.
bool __fastcall HullAccept(void* self,const void* gdi) {
    std::lock_guard<std::recursive_mutex> hold(lock);
    Hull* h=HullOf(self);
    if(!h)return false;
    for(void* o:h->parts) { Part* p=PartOf(o); if(p && p->group==kBigCannon && !p->gone)return false; }
    void* ragdoll=static_cast<char*>(self)+kCarrierRagdoll;
    if(h->weakBody==-2) {
        h->weakBody=RagdollBody(ragdoll,L"weak");
        Log("EDF41MOTHER hull %p: weak body %d",self,h->weakBody);
    }
    if(h->weakBody<0)return true;   // no such body in the shell: any hit counts (logged once above)
    return HitBody(ragdoll,gdi)==h->weakBody;
}

std::uint64_t __fastcall PartDtor(void* self,unsigned flags) {
    {
        std::lock_guard<std::recursive_mutex> hold(lock);
        Part* p=PartOf(self);
        if(p) {
            Hull* h=HullOf(p->hull);
            if(h)h->parts.erase(std::remove(h->parts.begin(),h->parts.end(),self),h->parts.end());
            parts.erase(self);
        }
    }
    return reinterpret_cast<DtorFn>(partClass.orig[kSlotDtor])(self,flags);
}

// M123 (2026-10-15) crashed in the stock retarget (0x54C4F9 -> 0x5E0F20) on a part whose team field +0x314 held a float (0.93):
// until the writer is found, a part's team is checked around each step of its frame, logged where it went wrong and put back
// (a team out of range indexes the team table out of bounds).
void TeamGuard(void* self,Part& p,const char* where) noexcept {
    const std::int32_t t=I(self,kTeamAt);
    if(t>=-1 && t<16)return;
    const std::int32_t fix=p.team>=-1 && p.team<16 ? p.team : 1;
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<12) {
        float asFloat;
        std::memcpy(&asFloat,&t,4);
        Log("EDF41MOTHER part %s #%d %p: team field %08X (as a float %.4f) %s; frame %d state %d; +0x310 %.4f +0x318 %d; put back to %d",
            kGroupNames[p.group],p.index,self,static_cast<unsigned>(t),asFloat,where,p.frames,p.state,F(self,0x310),I(self,0x318),fix);
    }
    std::memcpy(static_cast<char*>(self)+kTeamAt,&fix,4);
}
std::uint64_t __fastcall PartUpdate(void* self,void* dt) {
    std::lock_guard<std::recursive_mutex> hold(lock);
    Part* p=PartOf(self);
    if(!p)return reinterpret_cast<UpdateFn>(partClass.orig[kSlotUpdate])(self,dt);
    Hull* h=HullOf(p->hull);
    if(!h) {   // the hull is gone (destroyed, or the script deleted it): the part goes with it (4.1 0x56BBC0)
        if(!p->gone) { DeleteObject(self); p->gone=true; }
        return reinterpret_cast<UpdateFn>(partClass.orig[kSlotUpdate])(self,dt);
    }
    TeamGuard(self,*p,"before its update");
    if(!p->gone)Place(self,*p);
    TeamGuard(self,*p,"after Place");
    const std::uint64_t r=reinterpret_cast<UpdateFn>(partClass.orig[kSlotUpdate])(self,dt);
    p=PartOf(self);
    h=p ? HullOf(p->hull) : nullptr;
    if(p)TeamGuard(self,*p,"after the class's update");
    if(p && h && !p->gone)PartFrame(self,*p,*h);
    if(p)TeamGuard(self,*p,"after PartFrame");
    return r;
}

std::uint64_t __fastcall PartMessage(void* self,std::uint32_t msg,void* data) {
    const float before=F(self,kDurabilityAt);
    const std::uint64_t r=reinterpret_cast<MessageFn>(partClass.orig[kSlotMessage])(self,msg,data);
    if(msg!=kMsgDamage || !data)return r;
    std::lock_guard<std::recursive_mutex> hold(lock);
    Part* p=PartOf(self);
    const float after=F(self,kDurabilityAt);
    if(p && after<before && after>0.0f && p->damageCooldown<=0 && (p->group==kBigCannon || p->group==kCannon)) {
        p->damageCooldown=60;
        float at[3];
        std::memcpy(at,static_cast<const char*>(data)+kGdiCentre,12);
        PlaySe(p->hull,p->group==kBigCannon ? L"母船UFO砲台ダメージ" : L"母船UFO中型砲台ダメージ",at);
    }
    return r;
}

// ---- the creation ---------------------------------------------------------------------------------------------------
void CreatePart(void* info) noexcept { CloneObject(partClass,kEaoCtor,kEaoSize,info); }
void CreateHull(void* info) noexcept {
    void* obj=CloneObject(hullClass,kCarrierCtor,kCarrierSize,info);
    if(!obj)return;
    Log("EDF41MOTHER create #%d: UfoMother301 %p on UfoCarrier508's constructor",++created,obj);
    // The parts are made here, as 4.1's constructor makes them (0x24FFF0) and EDF6's UfoMother511 makes its ring units in its own
    // (0x4FE740 -> 0x4FEF00): M603 and M993 send ObjectAction / GetPartsObject on the creation frame, before any Update.
    std::lock_guard<std::recursive_mutex> hold(lock);
    SpawnParts(obj,hulls[obj]);
}
}  // namespace

bool InstallEdf41Mother() noexcept {
    if(!Cfg().edf41Mother) {
        Log("EDF41MOTHER off (ini Edf41Mother=0)");
        return false;
    }
    if(!BuildClone(hullClass,&HullDtor,&HullUpdate,&HullMessage) || !BuildClone(partClass,&PartDtor,&PartUpdate,&PartMessage)) {
        Log("EDF41MOTHER the vtable clones could not be built: off");
        return false;
    }
    SetSlot(hullClass,kSlotAccept,reinterpret_cast<void*>(&HullAccept));
    AllowAnyTeam(partClass);   // the parts take the hull's team (EventAnimationObject's slot 22 takes team 6 only)
    const bool ok=AddFactory(L"UfoMother301",&CreateHull) && AddFactory(L"UfoMother301_Hatch",&CreatePart)
        && AddFactory(L"UfoMother301_Panel",&CreatePart) && AddFactory(L"UfoMother301_Cannon",&CreatePart)
        && AddFactory(L"UfoMother301_BigCannon",&CreatePart);
    Log("EDF41MOTHER on: UfoMother301 and its four part classes %s (dev: Edf41MotherParts=%d, Edf41MotherForce=%d)",
        ok ? "queued for the game's registration" : "NOT registered",Cfg().edf41MotherParts,Cfg().edf41MotherForce);
    return ok;
}
}  // namespace crew
