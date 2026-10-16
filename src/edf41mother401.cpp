// EDF4.1's UfoMother401 (the Earth Eater) and its eleven part classes as the plugin's own scene object classes
// (docs/edf41-ufomother401.md; the 4.1 spec jobs/14618f05/tmp/re401/SPEC.md, the EDF6 primitives jobs/14618f05/tmp/re6/PRIMITIVES.md).
//
// 4.1 (EDF41.exe, RVAs): the hull UfoMother401 (ctor 0x25eba0) has no model, collision or durability of its own; it is a 19 x 19
// hexagonal layout controller. Its SGO's action_list is a table of 8 instruction kinds (0x260e10: 0 lay out a map, 1 fly the
// waiting panels in in batches, 2 put them in place at once, 3 make every panel vanish, 4 start a slow spin, 5 / 6 attack on /
// off, 7 the core flies away); the script's ObjectAction(n) runs entry n. A layout (0x25f370) makes one real object per cell from
// panel_definition (row i, column j at x = 172.5 j + (i odd ? 86.25 : 0), z = 150 i, centred), each 1000 above its cell, waiting for
// an instruction 1 / 2 to come down. Every panel shares PanelBase (0x26f1d0: the fly-in, vanish, destroy, burn, fall and fly-away
// states, the weakpoint filter: only hits on the ragdoll body the SGO's weakpoint names count, none without one); the classes:
// Panel01 (armour, hangs gun batteries on its 砲台 locators by parts_define's weights), WeakPanel (on death destroys every panel
// within 200 m in XZ, the Core too), Core (the hull's HP mirrors it; its death is a 50 s sequence and the ship's end),
// BigCannon01 (opens, charges 5 s, fires a downward laser for 10 s), Generator / FinalGenerator (the generator component the
// script's SetEnemyGenerator / SetInstantEnemyGenerator drives through the hull: 0x20100 is forwarded to the N-th of them),
// FinalCannon01 (genocide shot or five aimed lightning bolts); the batteries SmallLaser / Sniper / PlasmaCannon / RapidCannon fire
// at their target once it is within range.
//
// EDF6: every class is an EventAnimationObject (ctor 0x5B5FB0, 0xC90 bytes: a model, the CAS player main, a ragdoll, durability)
// under a cloned vtable (edf41classes.h): slot 0 the destructor, 5 Update, 9 OnMessage, 16 the damage filter (ApplyDamage 0x547C30
// asks it before the HP changes), 18 OnDamaged (after). The plugin keeps each object's 4.1 fields in its own record (Part), keyed
// by the object. The hull's model (a part's, tools/make_edf41_objects.py MOTHER401_HULL_DONORS) is drawn at a 1e-4 scale and takes
// no damage: the hull stays an invisible controller as in 4.1. Positions on the parts' models (battery mounts, muzzles, spawn
// points) come from 4.1's MAB locators, written into the part SGOs by the build (edf41_401_mounts / _muzzle / _spawn); the sounds
// as cue names of EDF6's own TIKYUU4_EN_EARTHEATER.ACB from 4.1's SEPRESET (edf41_401_se).
//
// Online: the classes run where the object's simulation runs (OnlineRunsHere); elsewhere the hull lays out nothing. 4.1's own
// network sync (action numbers, state indices) is not ported.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "crew.h"
#include "edf41classes.h"
#include "heli.h"
#include "memory.h"
#include "online_authority.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace crew {
namespace {
using namespace edf41c;

constexpr int kEaoSlotCount=45;
constexpr int kSlotAccept=16,kSlotDamaged=18;            // ApplyDamage 0x547C30: 16 bool Accept(obj, GDI*) before the HP change, 18 after
constexpr std::size_t kRagdollAt=0xAA0;                 // EventAnimationObject's ragdoll component
constexpr std::size_t kCasAt=0x8F0,kCasPlayerAt=0xC80;  // CAS players (+0x60 the players, 0x160 each: time +0xE8, length +0xF0)
constexpr float kFps=60.0f;
constexpr float kPi=3.14159265f;

enum class Kind : std::uint8_t { hull, panel01, weak, core, bigCannon, generator, finalCannon, finalGenerator,
                                  smallLaser, sniper, plasma, rapid };
const char* KindName(Kind k) noexcept {
    static const char* const names[]={"Hull","Panel01","WeakPanel","Core","BigCannon01","Generator","FinalCannon01","FinalGenerator",
                                      "GunBattery_SmallLaser","GunBattery_Sniper","GunBattery_PlasmaCannon","GunBattery_RapidCannon"};
    return names[static_cast<int>(k)];
}
bool IsBattery(Kind k) noexcept { return k>=Kind::smallLaser; }
bool IsPanel(Kind k) noexcept { return k!=Kind::hull && !IsBattery(k); }

Clone hullClone{"EventAnimationObject (UfoMother401)",kEaoVtable,kEaoSlotCount,kEaoSlots,{},{},{}};
Clone partClone{"EventAnimationObject (UfoMother401 parts)",kEaoVtable,kEaoSlotCount,kEaoSlots,{},{},{}};

void Fail(const char* what) noexcept {
    static std::mutex lock;
    static const char* said[48];
    static int count=0;
    std::lock_guard<std::mutex> hold(lock);
    for(int i=0;i<count;++i)
        if(said[i]==what)return;
    if(count<48)said[count++]=what;
    Log("EDF41M401 check failed: %s",what);
}
bool Rd(const void* p,std::size_t size,const char* what) noexcept {
    if(Readable(p,size))return true;
    Fail(what);
    return false;
}
float* F(void* o,std::size_t at) noexcept { return reinterpret_cast<float*>(static_cast<char*>(o)+at); }
bool Deleted(const void* o) noexcept {
    const char* c=static_cast<const char*>(o);
    return !o || !Readable(c+kFlagsAt,1) || (c[kFlagsAt]&4);
}
float Hp(void* o) noexcept { return Readable(F(o,kDurabilityAt),4) ? *F(o,kDurabilityAt) : 0.0f; }

// ---- small vector / matrix helpers (row vectors, rows x, y, z, position) ----------------------------------------------------
void RotY(float yaw,float* m) noexcept {
    const float c=std::cos(yaw),s=std::sin(yaw);
    const float r[16]={c,0,-s,0, 0,1,0,0, s,0,c,0, 0,0,0,1};
    std::memcpy(m,r,sizeof(r));
}
void Point(const float* p,const float* m,float* out) noexcept {   // out = (p, 1) * m
    const float r[3]={p[0]*m[0]+p[1]*m[4]+p[2]*m[8]+m[12],p[0]*m[1]+p[1]*m[5]+p[2]*m[9]+m[13],p[0]*m[2]+p[1]*m[6]+p[2]*m[10]+m[14]};
    std::memcpy(out,r,12);
}
float Len(const float* v) noexcept { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
float Dist(const float* a,const float* b) noexcept { const float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]}; return Len(d); }
float DistXZ(const float* a,const float* b) noexcept { return std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[2]-b[2])*(a[2]-b[2])); }
bool Norm(float* v) noexcept {
    const float l=Len(v);
    if(!(l>1e-6f))return false;
    v[0]/=l; v[1]/=l; v[2]/=l;
    return true;
}
// A direction within `angle` radians of `dir` (tilt uniform in [0, angle], azimuth uniform: 4.1 0x51b7c0).
void Cone(const float* dir,float angle,std::uint64_t& rng,float* out) noexcept {
    float d[3]={dir[0],dir[1],dir[2]};
    if(!Norm(d)) { d[0]=0; d[1]=-1; d[2]=0; }
    float a[3]={0,1,0};
    if(std::fabs(d[1])>0.9f) { a[0]=1; a[1]=0; }
    float u[3]={d[1]*a[2]-d[2]*a[1],d[2]*a[0]-d[0]*a[2],d[0]*a[1]-d[1]*a[0]};
    Norm(u);
    const float w[3]={d[1]*u[2]-d[2]*u[1],d[2]*u[0]-d[0]*u[2],d[0]*u[1]-d[1]*u[0]};
    const float tilt=Random01(rng)*angle,az=Random01(rng)*2.0f*kPi;
    const float st=std::sin(tilt),ct=std::cos(tilt),sa=std::sin(az),ca=std::cos(az);
    for(int k=0;k<3;++k)out[k]=d[k]*ct+(u[k]*ca+w[k]*sa)*st;
}
int Rand(std::uint64_t& rng,int n) noexcept { return n>0 ? std::min(n-1,static_cast<int>(Random01(rng)*static_cast<float>(n))) : 0; }

// ---- per-object records -------------------------------------------------------------------------------------------------
struct Cue { wchar_t name[48]={}; float volume=1.0f,pitch=1.0f,inner=1.0f,reach=1000.0f; };
struct GenCfg { int type=0,delay=0,count=0,interval=1; float level=1.0f; bool flag=false; wchar_t path[260]={}; };
struct Generator {
    bool hasPersist=false,active=false,needTrigger=false,triggered=false,waveDone=false;
    GenCfg persist,cur;
    int delay=0,interval=0,remaining=0,made=0;
    std::vector<WeakRef> slots;
};
struct Cell { float local[3]; int def; std::uint64_t panel; int group; };

enum State : int {
    sNone=0,
    // PanelBase (4.1 slot 34..37, 0x270870, 0x270940)
    sVanishWait,sShrink,sDestroyWait,sBurn,sFall,sFlyAway,
    // Core death (0x264510, 0x2645d0, 0x264810, 0x264870)
    sCore1,sCore2,sCore3,sCore4,
    // BigCannon01 (0x2631f0 .. 0x2638d0)
    sBcIdle,sBcOpen,sBcRelay,sBcCharge,sBcFire,sBcClose,
    // FinalCannon01 (0x265f60 .. 0x266650)
    sFcIdle,sFcGenoCharge,sFcGenoFire,sFcLightCharge,sFcLightFire,
    // Generator (0x268900 .. 0x268b90), FinalGenerator (0x2678f0, 0x2679a0)
    sGenIdle,sGenOpen,sGenSpawn,sGenWait,sGenClose,sFgIdle,sFgCharge,
    // batteries (base 0x269da0 / 0x269ec0, the classes' idle / charge / fire)
    sGbIdle,sGbCharge,sGbFire,sGbDestroyed,sGbFall,
};

struct Part {
    std::uint64_t id=0;
    void* obj=nullptr;
    Kind kind=Kind::hull;
    std::uint64_t rng=0x9E3779B97F4A7C15ull;
    std::vector<Cue> se;
    // state machine (4.1: one table per object, time +0x920 s, frames +0x924)
    int state=sNone,frames=0;
    float Time() const noexcept { return static_cast<float>(frames)/kFps; }
    bool seen=false;           // first Update done
    std::uint32_t lastTick=0;  // hullTick at its last Update
    bool farDone=false;
    int farTries=0;

    // ---- the hull ----
    std::vector<Cell> cells;
    std::vector<int> hosts;    // cells whose panel is a generator host, in cell order
    std::uint64_t core=0;
    std::uint32_t seed=0;
    bool attackOn=false,spin=false,dying=false;
    float target[3]={},velocity[3]={},yawAccel=0.0f,yaw=0.0f,yawTarget=0.0f,yawNow=0.0f;
    int actionCount[64]={};
    int throttle=0;
    SoundHandle ambient;
    int team=-1;
    bool ragdollOff=false;     // the hull's borrowed ragdoll taken out of the physics world (HullFirst)
    std::uint32_t hullFrames=0;
    double usOurs=0.0,usStock=0.0;
    std::uint32_t partFrames=0;

    // ---- panels ----
    std::uint64_t hull=0;
    float cur[3]={0,1000,0},dst[3]={},vel[3]={},wvel[3]={};
    float yaw60=0.0f;
    int mode=0,delay=0;
    float cap=0.5f;
    bool triggered=false,arrived=false,keep=false,killed=false,attack=false,gone=false,vanishing=false,noDamage=false;
    float vanishAfter=0.0f;
    int fallAt=0,burnUntil=0,destroyIn=0;
    int group=-1;
    float scale=1.0f;
    float muzzle[3]={},spawnAt[3]={},weakPt[3]={};
    bool hasMuzzle=false,hasSpawn=false;
    int weakBody=-2;           // -2 unknown, -1 none (immune unless the class takes every hit)
    wchar_t weakName[32]={};   // the SGO's weakpoint (the ragdoll body is looked up until the ragdoll has its bodies)
    int weakTries=0;
    int weakBone=-2;           // development (DevShoot): the weakpoint bone on the skeleton, -2 not looked up
    bool wholeHits=false;      // FinalCannon01 / FinalGenerator / the batteries: every hit counts (4.1 slot 10 0xb3630)
    struct Mount { float pos[3]; std::uint64_t battery; };
    std::vector<Mount> mounts;
    bool mountsMade=false;
    int countdown=0,minWait=0,maxWait=0;
    SoundHandle loopA,loopB;
    Bullet shot,shot2;
    float spread=0.0f,turn=0.0f,aim[3]={0,-1,0};
    Generator gen;
    LocatorHandle spawnLoc{};    // the generator's spawn node (出現位置 / 排出位置) on the model
    int spawnLocTries=0;

    // ---- batteries ----
    std::uint64_t panel=0;
    int mountIndex=-1;
    bool detached=false,batteryAttack=false;
    int destroyedFor=0,deleteAt=0;
    float range=0.0f,charge=0.0f;

    // ---- every part ----
    int hitSe=-1,hitSe2=-1;      // se_table entries the bullets of shot / shot2 carry as their hit sound (4.1 InitParam +0xB0)
    float lockLocal[3]={};       // the lock-on point (MAB list 1 ターゲット) on the model
    void* lockPoint=nullptr;     // the one the plugin adds (EventAnimationObject registers none)
    MaterialAnims mat;           // material_animation: [0] idle (looped from the start), [1] hit flash, [2] death
    bool encountDone=false;      // Generator: the spawned enemies were woken by the killing hit (4.1 +0xb30)
    struct Glow { void* fx=nullptr; void* ctrl=nullptr; float* m=nullptr; };
    Glow glows[3];               // the charge glows (edf41_401_charge, one per colour); glowOn: the one shown, -1 none
    int glowCount=0,glowOn=-1;
};

std::recursive_mutex lock;
int devTaken[12][3];   // development (Edf41DevShoot): per class, hits [0] taken, [1] refused by the weak-body filter, [2] seen
std::unordered_map<const void*,std::unique_ptr<Part>> byObj;
std::unordered_map<std::uint64_t,Part*> byId;
std::uint64_t nextId=1;
std::atomic<int> hullsMade{0},partsMade{0};

Part* Find(const void* o) noexcept { const auto it=byObj.find(o); return it==byObj.end() ? nullptr : it->second.get(); }
std::uint32_t hullTick=0;   // one per hull Update: the clock of the staleness test (a paused game stops it too)
Part* ById(std::uint64_t id) noexcept {
    if(!id)return nullptr;
    const auto it=byId.find(id);
    // a deleted object keeps its record until the game destroys it, and one the game takes out of its update list (a dead one) is
    // never updated again: either is gone (300 hull frames without an Update)
    if(it==byId.end() || it->second->gone || Deleted(it->second->obj))return nullptr;
    return it->second->seen && hullTick-it->second->lastTick>300u ? nullptr : it->second;
}

int DevBits() noexcept { return Cfg().edf41Mother401Dev; }

// ---- reading the object's own SGO --------------------------------------------------------------------------------------
void ReadCues(Part& p) noexcept {
    const SgoNode* t=Key(p.obj,L"edf41_401_se");
    const int n=Count(t);
    for(int i=0;i<n && i<32;++i) {
        Cue c;
        const SgoNode* e=Child(t,i);
        if(Count(e)>=5 && Text(Child(e,0),c.name,48)) {
            Float(Child(e,1),&c.volume); Float(Child(e,2),&c.pitch); Float(Child(e,3),&c.inner); Float(Child(e,4),&c.reach);
        }
        p.se.push_back(c);
    }
}
void PlaySeAt(Part& p,int i,const float* pos,SoundHandle* loop=nullptr) noexcept {
    if(i<0 || i>=static_cast<int>(p.se.size()) || !p.se[static_cast<std::size_t>(i)].name[0])return;
    const Cue& c=p.se[static_cast<std::size_t>(i)];
    SeParams sp; sp.volume=c.volume; sp.pitch=c.pitch; sp.inner=c.inner; sp.reference=c.reach;
    if(!PlayCue(c.name,pos,loop,sp)) {
        static std::atomic<int> said{0};
        if(said.fetch_add(1)<8)Log("EDF41M401 cue %ls of %s did not play (no bank holds it?)",c.name,KindName(p.kind));
    }
}
void PlaySe(Part& p,int i,SoundHandle* loop=nullptr) noexcept { PlaySeAt(p,i,F(p.obj,kPosAt),loop); }
bool Vec3Key(const void* o,const wchar_t* key,float* out) noexcept { return Vec(Key(o,key),out,3); }
const SgoNode* PersistKey(const void* o,const wchar_t* key) noexcept {
    static std::unordered_map<const SgoNode*,const SgoNode*> cache;
    const SgoNode* n=Key(o,key);
    if(!n)return nullptr;
    const auto it=cache.find(n);
    if(it!=cache.end())return it->second;
    const SgoNode* c=Persist(n);
    cache[n]=c;
    return c;
}

// ---- the state machine -------------------------------------------------------------------------------------------------
void Enter(Part& p,int state) noexcept;
void Exit(Part& p) noexcept {
    switch(p.state) {
        case sBcCharge: case sFcGenoCharge: case sFcLightCharge: case sGbCharge: StopSound(p.loopA,4); break;
        case sBcFire: case sGbFire: StopSound(p.loopB,30); break;
        case sFgCharge: StopSound(p.loopA,180); break;
        default: break;
    }
}

// ---- the panels' common machinery (4.1 PanelBase) -------------------------------------------------------------------------
void Unregister(Part& p) noexcept {
    SetTeam(p.obj,p.team<0 ? 1 : p.team,false);
    if(Rd(F(p.obj,kFlags380),4,"part flags"))*reinterpret_cast<std::uint32_t*>(F(p.obj,kFlags380))|=kNotTarget;
}
// 0x26fce0: vanish after `seconds` (no reward: HP 0 written, no kill).
void Vanish(Part& p,float seconds) noexcept {
    if(p.vanishing || p.killed)return;
    p.vanishing=true; p.noDamage=true;
    Unregister(p);
    *F(p.obj,kDurabilityAt)=0.0f;
    p.vanishAfter=seconds;
    Enter(p,sVanishWait);
}
// 0x26fdb0: destroyed after `frames` (the Core: its own death sequence).
void Destroy(Part& p,int frames) noexcept {
    if(p.killed && p.state>=sDestroyWait && p.state<=sCore4)return;
    if(p.vanishing && p.state==sShrink)return;
    p.killed=true; p.noDamage=true;
    Unregister(p);
    *F(p.obj,kDurabilityAt)=0.0f;
    if(Rd(static_cast<char*>(p.obj)+kDestroyedAt,1,"part destroyed byte"))static_cast<char*>(p.obj)[kDestroyedAt]=1;
    p.destroyIn=frames;
    Enter(p,p.kind==Kind::core ? sCore1 : sDestroyWait);
}
void WorldMatrixOfPanel(const Part& p,const float* hullM,float* out) noexcept {
    float r[16];
    RotY(p.yaw60,r);
    r[12]=p.cur[0]; r[13]=p.cur[1]; r[14]=p.cur[2];
    Multiply(r,hullM,out);
}

// ---- the hull --------------------------------------------------------------------------------------------------------
Part* HullOf(const Part& p) noexcept { return ById(p.hull); }
const float* HullMatrix(const Part& h) noexcept { return F(h.obj,kMatrixAt); }

// One panel object for cell `c` (0x25f370 step 3). Its record is filled here; the object made by the class's factory.
void* MakePanel(Part& h,Cell& c,const wchar_t* path,bool keep) noexcept {
    float m[16];
    std::memcpy(m,HullMatrix(h),sizeof(m));
    float w[3];
    Point(c.local,HullMatrix(h),w);
    m[12]=w[0]; m[13]=w[1]; m[14]=w[2];
    void* o=CreateFromPath(m,path);
    if(!o)return nullptr;
    Part* p=Find(o);
    if(!p || !IsPanel(p->kind)) {
        Log("EDF41M401 cell panel %ls is not a 401 panel class: deleted",path);
        DeleteObject(o);
        return nullptr;
    }
    p->hull=h.id;
    std::memcpy(p->dst,c.local,12);
    p->cur[0]=c.local[0]; p->cur[1]=1000.0f; p->cur[2]=c.local[2];
    p->keep=keep;
    p->attack=h.attackOn || (DevBits()&1);
    p->rng=0x2545F4914F6CDD1Dull*(h.seed+1u);
    p->yaw60=static_cast<float>(Rand(p->rng,6))*(kPi/3.0f);
    p->team=h.team;
    h.seed+=0x796;
    c.panel=p->id;
    c.group=p->group;
    if(p->kind==Kind::core)h.core=p->id;
    if(h.team>=0)SetTeam(o,h.team,true);
    return o;
}

// action 0: lay out the map `key` (0x25f370).
void Layout(Part& h,const wchar_t* key,int action) noexcept {
    const SgoNode* map=Key(h.obj,key);
    const SgoNode* defs=Key(h.obj,L"panel_definition");
    const int rows=Count(map);
    if(rows<=0 || Count(defs)<=0) { Log("EDF41M401 layout %ls: no such map or no panel_definition on %p",key,h.obj); return; }
    const LARGE_INTEGER t0=[]{LARGE_INTEGER t; QueryPerformanceCounter(&t); return t;}();
    int cols=0;
    for(int i=0;i<rows;++i)cols=std::max(cols,Count(Child(map,i)));
    const float ox=static_cast<float>(cols)*172.5f*0.5f,oz=static_cast<float>(rows)*150.0f*0.5f;
    std::vector<Cell> old;
    old.swap(h.cells);
    int made=0,kept=0,failed=0;
    for(int i=0;i<rows;++i) {
        const SgoNode* row=Child(map,i);
        for(int j=0;j<Count(row);++j) {
            std::int32_t def=0;
            Int(Child(row,j),&def);
            Cell c{{static_cast<float>(j)*172.5f+((i&1) ? 86.25f : 0.0f)-ox,0.0f,static_cast<float>(i)*150.0f-oz},def,0,-1};
            const std::size_t k=h.cells.size();
            if(k<old.size()) {    // keep the old panel of this cell (step 2) when it lives and its definition says keep
                Part* op=ById(old[k].panel);
                if(op && op->keep && !op->killed && !op->vanishing && Hp(op->obj)>0.0f) { h.cells.push_back(old[k]); ++kept; continue; }
            }
            const SgoNode* d=Child(defs,def);
            wchar_t path[260];
            std::int32_t keep=0;
            if(def>0 && Count(d)>=2 && Text(Child(d,0),path,260)) {
                Int(Child(d,1),&keep);
                if(MakePanel(h,c,path,keep!=0))++made; else ++failed;
            }
            h.cells.push_back(c);
        }
    }
    // step 4: every panel of the old table vanishes, staggered by its distance to the Core (or the hull) x 0.003 s; one kept in
    // the new table is not touched. 4.1 (H): 0x25f370 copies a kept entry into the new table and then empties the old entry's
    // weak reference (0x25f833..0x25f87b), so the vanish loop over the old table (0x25f140) never reaches a kept panel.
    const Part* core=ById(h.core);
    const float* centre=core ? F(core->obj,kPosAt) : F(h.obj,kPosAt);
    int vanished=0;
    for(const Cell& c:old) {
        Part* op=ById(c.panel);
        if(!op)continue;
        bool still=false;
        for(const Cell& n:h.cells)still=still || n.panel==c.panel;
        if(still)continue;
        Vanish(*op,Dist(F(op->obj,kPosAt),centre)*0.003f);
        ++vanished;
    }
    if(core && core->killed)h.core=0;
    h.hosts.clear();
    for(std::size_t k=0;k<h.cells.size();++k) {
        const Part* p=ById(h.cells[k].panel);
        if(p && (p->kind==Kind::generator || p->kind==Kind::finalGenerator))h.hosts.push_back(static_cast<int>(k));
    }
    LARGE_INTEGER t1,f;
    QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&f);
    Log("EDF41M401 hull %p action %d: layout %ls %dx%d -> %d panels made, %d kept, %d failed, %d old ones vanish, %zu generator hosts, %.1f ms",
        h.obj,action,key,rows,cols,made,kept,failed,vanished,h.hosts.size(),1000.0*static_cast<double>(t1.QuadPart-t0.QuadPart)/static_cast<double>(f.QuadPart));
}

// action 1 / 2 (0x260270): the panels not yet triggered come in.
void FlyIn(Part& h,const float* pointLocal,float a,int spreadFrames,float cap,bool atOnce) noexcept {
    float P[3];
    Point(pointLocal,HullMatrix(h),P);
    std::vector<std::pair<Part*,float>> todo;
    float least=3.4e38f;
    for(const Cell& c:h.cells) {
        Part* p=ById(c.panel);
        if(!p || p->triggered)continue;
        float w[3];
        Point(p->dst,HullMatrix(h),w);
        const float d=DistXZ(w,P);
        least=std::min(least,d);
        todo.push_back({p,d});
    }
    const bool now=atOnce || a<0.0f || (DevBits()&4);
    for(auto& [p,d]:todo) {
        p->triggered=true;
        if(!now) {
            p->cap=cap>0.0f ? cap : 0.5f;
            p->delay=Rand(p->rng,spreadFrames)+static_cast<int>((d-least)*a);
            if(p->delay>0)continue;
        }
        std::memcpy(p->cur,p->dst,12);
        p->delay=0;
        p->arrived=true;
        std::memset(p->vel,0,12);
    }
    Log("EDF41M401 hull %p: %zu panels %s (point %.0f %.0f %.0f, a %.2f, spread %d frames, cap %.2f)",h.obj,todo.size(),
        now ? "put in place" : "fly in",P[0],P[1],P[2],a,spreadFrames,cap);
}

void Action(Part& h,int n) noexcept {
    const SgoNode* list=Key(h.obj,L"action_list");
    const SgoNode* e=Child(list,n);
    std::int32_t kind=-1;
    if(!e || !Int(Child(e,0),&kind)) { Log("EDF41M401 hull %p ObjectAction %d: no such entry (%d entries), ignored",h.obj,n,Count(list)); return; }
    if(n>=0 && n<64)++h.actionCount[n];
    switch(kind) {
        case 0: {
            wchar_t key[96];
            if(Text(Child(e,1),key,96))Layout(h,key,n);
            if(DevBits()&4)FlyIn(h,h.target,0,0,0,true);
            break;
        }
        case 1: {
            float P[3]={},a=0,b=0,c=0;
            Vec(Child(e,1),P,3); Float(Child(e,2),&a); Float(Child(e,3),&b); Float(Child(e,4),&c);
            FlyIn(h,P,a,static_cast<int>(b*kFps),c,false);
            break;
        }
        case 2: { const float zero[3]={}; FlyIn(h,zero,-1.0f,0,0,true); break; }
        case 3: {
            const Part* core=ById(h.core);
            const float* centre=core ? F(core->obj,kPosAt) : F(h.obj,kPosAt);
            int k=0;
            for(const Cell& c:h.cells)
                if(Part* p=ById(c.panel)) { Vanish(*p,Dist(F(p->obj,kPosAt),centre)*0.003f); ++k; }
            Log("EDF41M401 hull %p action %d: every panel vanishes (%d)",h.obj,n,k);
            break;
        }
        case 4: h.spin=true; Log("EDF41M401 hull %p action %d: slow spin",h.obj,n); break;
        case 5: case 6: {
            h.attackOn=kind==5;
            for(const Cell& c:h.cells)
                if(Part* p=ById(c.panel))p->attack=h.attackOn || (DevBits()&1);
            Log("EDF41M401 hull %p action %d: attack %s",h.obj,n,h.attackOn ? "on" : "off");
            break;
        }
        case 7:
            if(Part* core=ById(h.core)) { Enter(*core,sFlyAway); Log("EDF41M401 hull %p action %d: the Core flies away",h.obj,n); }
            break;
        default: Log("EDF41M401 hull %p ObjectAction %d: kind %d does nothing",h.obj,n,kind); break;
    }
}

// 0x261710: every panel within `radius` (XZ) of `at` is destroyed after rand(180) frames.
void AreaDestroy(Part& h,const float* at,float radius) noexcept {
    int k=0;
    for(const Cell& c:h.cells) {
        Part* p=ById(c.panel);
        if(!p || p->killed || DistXZ(F(p->obj,kPosAt),at)>=radius)continue;
        Destroy(*p,Rand(h.rng,180));
        ++k;
    }
    Log("EDF41M401 hull %p: a weak panel's blast destroys %d panels within %.0f m",h.obj,k,radius);
}

// ---- generators (4.1's generic component 0x330c40: SetGenerator 0x330e70, Update 0x331270, one spawn 0x3313c0) ---------------
// EDF6's 0x20100 block is 4.1's moved by 8: +0x08 type (0 SetEnemyGenerator, waves; else SetInstantEnemyGenerator, once), +0x0C the
// delay between waves, +0x10 count, +0x14 interval (frames), +0x18 level, +0x1C flag (activate), +0x20 std::wstring the enemy,
// +0x48 the hull's generator index.
bool ReadGenCfg(const char* d,GenCfg& g) noexcept {
    if(!Rd(d,0x50,"generator message"))return false;
    std::memcpy(&g.type,d+8,4); std::memcpy(&g.delay,d+0xC,4); std::memcpy(&g.count,d+0x10,4); std::memcpy(&g.interval,d+0x14,4);
    std::memcpy(&g.level,d+0x18,4);
    g.flag=d[0x1C]!=0;
    std::uint64_t size=0,capacity=0;
    std::memcpy(&size,d+0x30,8); std::memcpy(&capacity,d+0x38,8);
    const wchar_t* text=capacity<8 ? reinterpret_cast<const wchar_t*>(d+0x20) : *reinterpret_cast<const wchar_t* const*>(d+0x20);
    if(size==0 || size>=260 || !Rd(text,(size+1)*sizeof(wchar_t),"generator enemy path"))return false;
    std::wmemcpy(g.path,text,static_cast<std::size_t>(size));
    g.path[size]=L'\0';
    if(g.count<0 || g.count>512)return false;
    if(g.interval<1)g.interval=1;
    if(g.delay<0)g.delay=0;
    return true;
}
void SetGenerator(Part& p,const GenCfg& g) noexcept {
    Generator& gen=p.gen;
    if(g.type==0) {
        gen.hasPersist=true;
        gen.persist=g;
        for(WeakRef& w:gen.slots)WeakReset(w);
        gen.slots.assign(static_cast<std::size_t>(g.count),WeakRef{});
    }
    if(g.type!=0 || !gen.active || gen.cur.type==0) {
        gen.cur=g;
        gen.active=true;
        gen.delay=0;
        gen.interval=0;
        gen.remaining=g.count;
    }
    Log("EDF41M401 %s %p generator: %s %d x %ls every %d frames, wave delay %d, level %.2f%s",KindName(p.kind),p.obj,
        g.type==0 ? "waves of" : "once",g.count,g.path,g.interval,g.delay,g.level,g.flag ? ", activated" : "");
}
void Down(Part& p,float* out) noexcept;
// A message the way EDF6's SpawnerObject hands one to the enemy it makes (0x6E33C3: slots 10, 9 (OnMessage), 11 in a row).
void SendSpawnMessage(void* o,std::uint32_t msg,void* data) noexcept {
    void* const* vt=Readable(o,8) ? *static_cast<void* const* const*>(o) : nullptr;
    if(!vt || !Readable(vt+9,24))return;
    void* pdata=data;
    std::uint32_t m=msg;
    reinterpret_cast<std::uint64_t(__fastcall*)(void*,std::uint32_t*,void**)>(vt[10])(o,&m,&pdata);
    reinterpret_cast<std::uint64_t(__fastcall*)(void*,std::uint32_t,void*)>(vt[9])(o,m,pdata);
    reinterpret_cast<std::uint64_t(__fastcall*)(void*,std::uint32_t,void*)>(vt[11])(o,m,pdata);
}
bool SpawnOne(Part& p,const GenCfg& g,WeakRef* slot) noexcept {
    // 4.1 0x3313c0: the node's world matrix (0x3232d0 on 出現位置 / 排出位置: the locator x its bone), its position the spawn
    // point; the enemy's matrix a fresh yaw there; SpawnerObject 0x3320a0 then sends the node's row 1 as message 0x20102
    // (EDF6: UfoSmall507 0x52EDE0 takes it x 60 as its velocity, Deiroi 0x31ED40 / DragonSmall 0x335110 their drop state;
    // EDF6's own generator 0x6E2170 sends its node's row 1 the same way). The node from EDF6's locator code (0x6BADD0 /
    // 0x6BB5A0), else the build's point and the panel's down axis.
    float at[3];
    alignas(16) float eject[4]={0,-1,0,0};
    float node[16];
    if(!p.spawnLoc.entry && p.spawnLocTries<3) {
        ++p.spawnLocTries;
        FindLocator(static_cast<char*>(p.obj)+kEaoModel,p.kind==Kind::finalGenerator ? L"排出位置" : L"出現位置",&p.spawnLoc);
    }
    if(p.spawnLoc.entry && LocatorMatrix(p.spawnLoc,node)) {
        std::memcpy(at,node+12,12);
        std::memcpy(eject,node+4,16);
    } else {
        Point(p.spawnAt,F(p.obj,kMatrixAt),at);
        Down(p,eject);
    }
    float m[16];
    RotY(Random01(p.rng)*2.0f*kPi,m);
    m[12]=at[0]; m[13]=at[1]; m[14]=at[2];
    void* o=CreateFromPath(m,g.path);
    ++p.gen.made;
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<40)Log("EDF41M401 %s %p: enemy #%d %ls %s at %.0f %.0f %.0f",KindName(p.kind),p.obj,p.gen.made,g.path,o ? "made" : "NOT made",at[0],at[1],at[2]);
    if(!o)return false;
    SetLevel(o,g.level);
    SetTeam(o,1,true);
    if(g.flag)Activate(o);
    if(slot)WeakSet(*slot,o);
    SendSpawnMessage(o,0x20102,eject);
    if(said.load()<=40)Log("EDF41M401 %s %p: enemy #%d gets 0x20102 (%.2f %.2f %.2f %.2f)%s",KindName(p.kind),p.obj,p.gen.made,eject[0],eject[1],eject[2],eject[3],
                           p.spawnLoc.entry ? "" : " (no locator: the panel's down axis)");
    if(p.kind==Kind::finalGenerator)PlaySeAt(p,7,at);   // 0x267084: se[7] 召喚完了 is the component's per-spawn sound
    return true;
}
// 0x331d10 "encount": every enemy the generator tracks (the wave slots; one-shot spawns are not tracked) that is not yet aware
// becomes aware (4.1 +0x260 = 1 and its slot 15; EDF6's 0x548D50, what CreateEnemy's `active` does).
void Encount(Part& p) noexcept {
    int woken=0;
    for(WeakRef& w:p.gen.slots)
        if(WeakAlive(w) && Readable(static_cast<char*>(w.object)+0x4A0,1) && !static_cast<const char*>(w.object)[0x4A0]) { Activate(w.object); ++woken; }
    int tracked=0;
    for(const WeakRef& w:p.gen.slots)tracked+=WeakAlive(w) ? 1 : 0;
    static std::atomic<int> said{0};
    if((woken || said.load()<4) && said.fetch_add(1)<16)Log("EDF41M401 %s %p is hit: %d of its %d live enemies notice",KindName(p.kind),p.obj,woken,tracked);
}
// Called while the panel lives and has arrived (Generator 0x268810 / FinalGenerator 0x2677d0).
void RunGenerator(Part& p) noexcept {
    Generator& g=p.gen;
    g.waveDone=false;
    if(DevBits()&64)   // development: the wave's enemies are shot too (the refill of dead slots, Edf41DevShoot)
        for(std::size_t i=0;i<g.slots.size();++i)
            if(float aim[3]; WeakAlive(g.slots[i]) && AimPoint(g.slots[i].object,aim))DevShoot(g.slots[i].object,aim,static_cast<unsigned>(i)+2);
    if(!g.active || (g.cur.type==0 && g.persist.count==0))return;
    if(g.delay>0) { --g.delay; return; }
    if(g.interval>0) { --g.interval; return; }
    if(g.needTrigger && !g.triggered)return;
    if(g.remaining>0) {
        --g.remaining;
        if(g.cur.type==0) {
            WeakRef* s=g.remaining<static_cast<int>(g.slots.size()) ? &g.slots[static_cast<std::size_t>(g.remaining)] : nullptr;
            if(!s || !WeakAlive(*s))SpawnOne(p,g.cur,s);
        } else SpawnOne(p,g.cur,nullptr);
        g.interval=g.cur.interval;
    }
    if(g.remaining<=0) {
        g.waveDone=true;
        g.triggered=false;
        if(g.hasPersist) { g.cur=g.persist; g.delay=g.persist.delay; g.remaining=g.persist.count; g.interval=0; }
        else g.active=false;
    }
}

// ---- CAS (the BigCannon and Generator hatches) ----------------------------------------------------------------------------
bool CasDone(Part& p) noexcept {
    char* o=static_cast<char*>(p.obj);
    if(!Rd(o+kCasPlayerAt,4,"CAS player index") || !Rd(o+kCasAt+0x60,8,"CAS players"))return true;
    const int i=*reinterpret_cast<const std::int32_t*>(o+kCasPlayerAt);
    const char* players=*reinterpret_cast<const char* const*>(o+kCasAt+0x60);
    if(i<0 || !players || !Rd(players+static_cast<std::size_t>(i)*0x160,0x160,"CAS player"))return true;
    const char* pl=players+static_cast<std::size_t>(i)*0x160;
    float t,len;
    std::memcpy(&t,pl+0xE8,4); std::memcpy(&len,pl+0xF0,4);
    return !(len>0.0f) || t>=len-1e-3f;
}

// ---- shooting ------------------------------------------------------------------------------------------------------------
// `hitSe`: the se_table entry the bullet plays where it hits (-1 none).
void FireFrom(Part& p,const float* from,const float* dir,const Bullet& b,int hitSe=-1) noexcept {
    float m[16];
    AimMatrix(from,dir,m);
    bool ok;
    if(hitSe>=0 && hitSe<static_cast<int>(p.se.size()) && p.se[static_cast<std::size_t>(hitSe)].name[0] && !(DevBits()&2048)) {   // dev 2048: none
        const Cue& c=p.se[static_cast<std::size_t>(hitSe)];
        SeParams sp; sp.volume=c.volume; sp.pitch=c.pitch; sp.inner=c.inner; sp.reference=c.reach;
        ok=FireHit(p.obj,m,b,c.name,sp);
    } else ok=Fire(p.obj,m,b);
    if(!ok) { Fail("a 401 bullet was not made"); return; }
    static std::atomic<int> said[12];
    if(said[static_cast<int>(p.kind)].fetch_add(1)<2)
        Log("EDF41M401 %s %p fires %ls from %.0f %.0f %.0f toward %.2f %.2f %.2f (damage %.2f, speed %.2f)",KindName(p.kind),p.obj,b.factory,
            from[0],from[1],from[2],dir[0],dir[1],dir[2],b.damage,b.speed);
}
void MuzzleWorld(Part& p,float* out) noexcept { Point(p.muzzle,F(p.obj,kMatrixAt),out); }
void Down(Part& p,float* out) noexcept { const float* y=F(p.obj,kMatrixAt+16); out[0]=-y[0]; out[1]=-y[1]; out[2]=-y[2]; }
bool AimAt(Part& p,const float* from,float* dir) noexcept {
    float t[3];
    void* who=Target(p.obj);
    if(!who || !AimPoint(who,t))return false;
    dir[0]=t[0]-from[0]; dir[1]=t[1]-from[1]; dir[2]=t[2]-from[2];
    return Norm(dir);
}

// The bullet blocks of each class (4.1 slot 29 / 28 at s = 1: the hull's +0x490 is 10.0 and never written, SPEC 1.6; the
// difficulty rows are 1 offline, SPEC2 1.3). EDF6 InitParam offsets: SPEC2 7.
void SetupBullets(Part& p) noexcept {
    const float s=1.0f;
    Bullet& b=p.shot;
    b.hitStop=0.5f;
    switch(p.kind) {
        case Kind::bigCannon:   // 0x262ec0
            b.factory=L"LaserBullet02"; b.speed=30; b.life=20; b.damage=10*s; b.size=20;
            { const float c[4]={0.5f,1.5f,0.5f,1}; std::memcpy(b.color,c,16); }
            b.custom=PersistKey(p.obj,L"BulletCustomParam");
            p.minWait=1200; p.maxWait=1800; p.countdown=Rand(p.rng,1800);
            break;
        case Kind::finalCannon: {   // 0x265940
            b.factory=L"RocketBullet01_LowLight"; b.speed=0.01f; b.life=1800; b.damage=s*70; b.explosion=60; b.size=100;
            const float c[4]={100,10,50,1}; std::memcpy(b.color,c,16);
            b.custom=PersistKey(p.obj,L"GenocideBulletCustomParameter");
            Bullet& l=p.shot2;
            l.factory=L"LightningBullet01"; l.speed=5+0.1f*s; l.life=120; l.damage=s*3; l.size=5+0.1f*s; l.hitStop=0.5f;
            const float c2[4]={1,1,5,1}; std::memcpy(l.color,c2,16);
            l.custom=PersistKey(p.obj,L"LightningBulletCustomParameter");
            p.spread=std::max(0.001f,0.1f-0.001f*s);
            p.minWait=400; p.maxWait=600; p.countdown=Rand(p.rng,600);
            // hit sounds: 4.1 copies se[5] then se[2] into the genocide block (+0xd60, the second copy meant for the lightning
            // block's +0xea0) with its 0..5 indexing (SPEC 1.6); the +6 intent: genocide 11, lightning 8
            p.hitSe=11; p.hitSe2=8;
            break;
        }
        case Kind::smallLaser: {   // 0x26a560
            b.factory=L"LaserBullet02"; b.speed=30; b.life=60; b.damage=s*5; b.size=1; b.speedFactor=0.05f;
            const float c[4]={0.5f,1.5f,0.5f,1}; std::memcpy(b.color,c,16);
            p.spread=std::max(0.05f,0.25f-0.02f*s); p.range=700; p.charge=1.5f; p.minWait=60; p.maxWait=120; p.hitSe=4;
            break;
        }
        case Kind::sniper: {       // 0x26afc0
            b.factory=L"BeamBullet01"; b.speed=std::min(20.0f,5+0.5f*s); b.life=180; b.damage=s*0.5f; b.size=std::max(2.0f,4-0.2f*s);
            b.speedFactor=0.05f;
            const float c[4]={0.5f,0.5f,1.2f,1}; std::memcpy(b.color,c,16);
            p.spread=0.01f; p.range=500; p.charge=3.0f; p.minWait=300; p.maxWait=420; p.hitSe=4;
            break;
        }
        case Kind::plasma: {       // 0x26ba30
            b.factory=L"PlasmaBullet01"; b.speed=std::min(2.0f,0.5f+0.05f*s); b.life=600; b.damage=s*20; b.size=6;
            b.explosion=std::min(20.0f,10+0.05f*(s-10));
            const float c[4]={5,2.5f,5,1}; std::memcpy(b.color,c,16);
            p.spread=0.1f; p.range=500; p.charge=3.0f; p.minWait=900; p.maxWait=1260; p.hitSe=4;
            break;
        }
        case Kind::rapid: {        // 0x26c6a0
            b.factory=L"LaserBullet02"; b.speed=40; b.life=15; b.damage=s*0.25f; b.size=2+0.025f*(s-1); b.speedFactor=0.005f; b.hitStop=0.95f;
            const float c[4]={0.8f,0.2f,0.8f,1}; std::memcpy(b.color,c,16);
            p.spread=std::max(0.05f,0.18f-0.02f*s); p.turn=std::min(0.05f,0.0004f+0.000016f*(s-1));
            p.range=500; p.charge=5.0f; p.minWait=5400; p.maxWait=7560; p.hitSe=5;   // on every 16th shot (TickBattery)
            break;
        }
        default: break;
    }
    if(IsBattery(p.kind)) {
        if(!b.custom)b.custom=PersistKey(p.obj,L"BulletCustomParam");
        p.countdown=Rand(p.rng,p.maxWait);
    }
    if(DevBits()&2) { p.minWait=std::min(p.minWait,60); p.maxWait=std::min(p.maxWait,120); p.countdown=std::min(p.countdown,120); }
}

// ---- state enter actions -------------------------------------------------------------------------------------------------
void Enter(Part& p,int state) noexcept {
    Exit(p);
    if(state==sCore1 || state==sCore2 || state==sCore4 || state==sFlyAway || state==sGbDestroyed || state==sBcCharge || state==sFcGenoCharge
       || state==sFcLightCharge || state==sGenOpen || state==sFgCharge) {
        static std::atomic<int> said{0};
        if(said.fetch_add(1)<60)Log("EDF41M401 %s %p state %d",KindName(p.kind),p.obj,state);
    }
    p.state=state;
    p.frames=0;
    p.glowOn=-1;   // the charge glow shown in this state (4.1: emitter on in the state's enter, off in its exit)
    switch(state) {
        case sShrink: PlaySe(p,2); break;
        case sBurn: PlaySe(p,3); break;
        case sFall: p.mode=1; break;
        case sFlyAway: p.mode=2; break;
        case sCore1: PlaySe(p,6); break;
        case sCore2: PlaySe(p,7); break;
        case sCore4: PlaySe(p,8); break;
        case sBcOpen: PlayState(p.obj,L"open"); break;
        case sBcClose: case sGenClose: PlayState(p.obj,L"close"); break;
        case sGenOpen: PlayState(p.obj,L"open"); break;
        case sBcCharge: PlaySe(p,6,&p.loopA); p.glowOn=0; break;
        case sBcFire: PlaySe(p,7,&p.loopB); p.glowOn=0; break;
        case sFcGenoCharge: PlaySe(p,9,&p.loopA); p.glowOn=0; break;     // 4.1 plays se[3] here; its SGO's charge cue is [9] (SPEC 1.6)
        case sFcGenoFire: PlaySe(p,10); break;
        case sFcLightCharge: PlaySe(p,6,&p.loopA); p.glowOn=1; break;
        case sFcLightFire: PlaySe(p,7); p.glowOn=2; break;
        case sGenSpawn: p.gen.triggered=true; break;           // 0x268a80 sends 0x20101 to itself
        case sFgCharge: PlaySe(p,6,&p.loopA); p.glowOn=0; break;
        case sGbCharge: if(p.kind==Kind::rapid)PlaySe(p,3,&p.loopA); break;
        case sGbFire:
            if(p.kind==Kind::rapid) {
                Down(p,p.aim);
                Cone(p.aim,p.spread,p.rng,p.aim);
                Norm(p.aim);
                PlaySe(p,4,&p.loopB);
            }
            break;
        case sGbDestroyed: PlaySe(p,1); break;
        case sGbFall: p.detached=true; break;
        default: break;
    }
}

// ---- per-frame state logic of the panels ------------------------------------------------------------------------------------
void RandomNear(Part& p,float r,float* out) noexcept {
    const float* pos=F(p.obj,kPosAt);
    const float a=Random01(p.rng)*2.0f*kPi,d=Random01(p.rng)*r;
    out[0]=pos[0]+std::cos(a)*d; out[1]=pos[1]+(Random01(p.rng)-0.5f)*r*0.3f; out[2]=pos[2]+std::sin(a)*d;
}

// Returns true when the object is to be deleted.
bool TickPanel(Part& p) noexcept {
    const float t=p.Time();
    const int f=p.frames;
    switch(p.state) {
        case sVanishWait: if(t>=p.vanishAfter)Enter(p,sShrink); break;
        case sShrink:
            p.scale=std::max(0.0f,1.0f-2.0f*t);
            if(t>0.5f)return true;
            break;
        case sDestroyWait: if(--p.destroyIn<=0)Enter(p,sBurn); break;
        case sBurn:
            if(f<180 && f%8==0) { float q[3]; RandomNear(p,80.0f,q); const float up[3]={0,1,0}; SmallBurst(q,up,8.0f); }   // 0x1b1910
            if(f>p.burnUntil)Enter(p,sFall);
            break;
        case sFall:
            if(f==p.fallAt-60) { PlaySe(p,4); const float down[3]={0,-1,0}; BigExplosion(F(p.obj,kPosAt),down,0.5f,1.5f); }
            if(f>p.fallAt)return true;
            break;
        case sFlyAway: if(p.cur[1]>1000.0f)return true; break;
        case sCore1: if(p.destroyIn>0) { --p.destroyIn; p.frames=0; } else if(t>30.0f)Enter(p,sCore2); break;
        case sCore2: {
            const int n=f/16;
            if(n>=24) { Enter(p,sCore3); break; }
            if(f%16==0) {
                const float* pos=F(p.obj,kPosAt);
                const float a=Random01(p.rng)*2.0f*kPi;
                float v[3]={std::cos(a)*70.0f,0.0f,std::sin(a)*70.0f};
                const float q[3]={pos[0]+v[0],pos[1]+500.0f-static_cast<float>(n)*29.1667f,pos[2]+v[2]};
                Norm(v);
                Explosion(q,v,20.0f);
            }
            break;
        }
        case sCore3: if(t>8.0f)Enter(p,sCore4); break;
        case sCore4: {
            const int n=f/32;
            if(n<3 && f%32==0) {
                const float* pos=F(p.obj,kPosAt);
                const float a=Random01(p.rng)*2.0f*kPi;
                const float d[3]={std::cos(a),0.0f,std::sin(a)};
                const float q[3]={pos[0],pos[1]-200.0f+static_cast<float>(n)*133.333f,pos[2]};
                BigExplosion(q,d,5.0f,1.0f);
            }
            if(n>=3 && t>6.0f)return true;
            break;
        }
        // BigCannon01
        case sBcIdle:
            if(p.attack && p.arrived && --p.countdown<=0) { p.countdown=p.minWait+Rand(p.rng,p.maxWait-p.minWait); Enter(p,sBcOpen); }
            break;
        case sBcOpen: if(CasDone(p) || f>600)Enter(p,sBcRelay); break;
        case sBcRelay: Enter(p,sBcCharge); break;
        case sBcCharge: MoveSound(p.loopA,F(p.obj,kPosAt)); if(t>5.0f)Enter(p,sBcFire); break;
        case sBcFire:
            MoveSound(p.loopB,F(p.obj,kPosAt));
            if((f&1)==0) { float from[3],dir[3]; MuzzleWorld(p,from); Down(p,dir); FireFrom(p,from,dir,p.shot); }
            if(f>600)Enter(p,sBcClose);
            break;
        case sBcClose: if(CasDone(p) || f>600)Enter(p,sBcIdle); break;
        // FinalCannon01
        case sFcIdle:
            if(p.attack && p.arrived && --p.countdown<=0) {
                p.countdown=p.minWait+Rand(p.rng,p.maxWait-p.minWait);
                Enter(p,Rand(p.rng,3)==0 ? sFcGenoCharge : sFcLightCharge);
            }
            break;
        case sFcGenoCharge: MoveSound(p.loopA,F(p.obj,kPosAt)); if(t>5.0f)Enter(p,sFcGenoFire); break;
        case sFcGenoFire: { float from[3],dir[3]; MuzzleWorld(p,from); Down(p,dir); FireFrom(p,from,dir,p.shot,p.hitSe); Enter(p,sFcIdle); break; }
        case sFcLightCharge: MoveSound(p.loopA,F(p.obj,kPosAt)); if(t>5.0f)Enter(p,sFcLightFire); break;
        case sFcLightFire:
            if(f%16==0) {
                float from[3],dir[3],out[3];
                MuzzleWorld(p,from);
                if(!AimAt(p,from,dir))Down(p,dir);
                Cone(dir,p.spread,p.rng,out);
                FireFrom(p,from,out,p.shot2,p.hitSe2);
            }
            if(f/16==5)Enter(p,sFcIdle);
            break;
        // Generator
        case sGenIdle:
            if(p.gen.active && p.arrived && p.gen.delay<=150)Enter(p,sGenOpen);   // the open clip's length: 150 frames (its MAB events, M)
            break;
        case sGenOpen: if(CasDone(p) || f>600)Enter(p,sGenSpawn); break;
        case sGenSpawn: if(p.gen.waveDone)Enter(p,sGenWait); break;
        case sGenWait: if(f>=300)Enter(p,sGenClose); break;
        case sGenClose: if(CasDone(p) || f>600)Enter(p,sGenIdle); break;
        // FinalGenerator
        case sFgIdle: if(p.gen.active && p.arrived && p.gen.delay<=180)Enter(p,sFgCharge); break;
        case sFgCharge: MoveSound(p.loopA,F(p.obj,kPosAt)); if(p.gen.waveDone)Enter(p,sFgIdle); break;
        default: break;
    }
    return false;
}

bool TickBattery(Part& b) noexcept {
    const float t=b.Time();
    const int f=b.frames;
    switch(b.state) {
        case sGbIdle: {
            if(!b.batteryAttack)break;
            float aim[3];
            void* who=Target(b.obj);
            if(!who || !AimPoint(who,aim) || Dist(aim,F(b.obj,kPosAt))>=b.range)break;
            if(--b.countdown<0) { b.countdown=b.minWait+Rand(b.rng,b.maxWait-b.minWait); Enter(b,b.kind==Kind::rapid ? sGbCharge : sGbFire); }
            break;
        }
        case sGbCharge: MoveSound(b.loopA,F(b.obj,kPosAt)); if(t>=b.charge)Enter(b,sGbFire); break;
        case sGbFire: {
            float from[3],dir[3];
            MuzzleWorld(b,from);
            if(b.kind!=Kind::rapid) {
                if(t<b.charge)break;
                if(!AimAt(b,from,dir))Down(b,dir);
                float out[3];
                Cone(dir,b.spread,b.rng,out);
                FireFrom(b,from,out,b.shot,b.hitSe);
                if(b.kind==Kind::smallLaser)PlaySe(b,3);
                Enter(b,sGbIdle);
                break;
            }
            MoveSound(b.loopB,F(b.obj,kPosAt));
            float want[3];
            if(!AimAt(b,from,want))Down(b,want);
            const float d=std::max(-1.0f,std::min(1.0f,want[0]*b.aim[0]+want[1]*b.aim[1]+want[2]*b.aim[2]));
            const float a=std::acos(d);
            if(a>1e-5f) {
                const float step=std::min(a,b.turn);
                float perp[3]={want[0]-b.aim[0]*d,want[1]-b.aim[1]*d,want[2]-b.aim[2]*d};
                if(Norm(perp))
                    for(int k=0;k<3;++k)b.aim[k]=b.aim[k]*std::cos(step)+perp[k]*std::sin(step);
                Norm(b.aim);
            }
            if((f&1)==0)FireFrom(b,from,b.aim,b.shot,f%16==0 ? b.hitSe : -1);   // 0x26cc20: the hit sound on every 16th frame
            if(t>30.0f)Enter(b,sGbIdle);
            break;
        }
        case sGbDestroyed:
            if(f<180 && f%16==0) { float q[3]; RandomNear(b,10.0f,q); const float up[3]={0,1,0}; FireBurst(q,up,4.0f); }   // 0x1b16b0
            if(f>b.destroyedFor)Enter(b,sGbFall);
            break;
        case sGbFall:
            if(f==b.deleteAt-60) { PlaySe(b,2); const float down[3]={0,-1,0}; Explosion(F(b.obj,kPosAt),down,10.0f); }
            if(f>b.deleteAt)return true;
            break;
        default: break;
    }
    return false;
}

// ---- first update of a part: its 4.1 fields from the SGO -----------------------------------------------------------------
void GlowsMake(Part& p) noexcept;
void InitPart(Part& p) noexcept {
    ReadCues(p);
    p.hasMuzzle=Vec3Key(p.obj,L"edf41_401_muzzle",p.muzzle);
    p.hasSpawn=Vec3Key(p.obj,L"edf41_401_spawn",p.spawnAt);
    Vec3Key(p.obj,L"edf41_401_weak",p.weakPt);
    p.weakBody=Text(Key(p.obj,L"weakpoint"),p.weakName,32) ? RagdollBody(static_cast<char*>(p.obj)+kRagdollAt,p.weakName) : -1;
    if(p.weakBody<0 && p.weakName[0])p.weakBody=-2;   // the ragdoll's bodies may come later: PartUpdate asks again
    p.wholeHits=p.kind==Kind::finalCannon || p.kind==Kind::finalGenerator || IsBattery(p.kind);
    // HP: 4.1 0x26f9f0 / 0x269a80, durability x s (1) x the difficulty rows: EDF6's durability x difficulty x players (0xD7720);
    // EventAnimationObject never sizes itself by level (its slot 6 is a stub), so it is written here.
    const float base=Readable(F(p.obj,kBaseDurabilityAt),4) ? *F(p.obj,kBaseDurabilityAt) : 1000.0f;
    const float hp=Difficulty(base);
    *F(p.obj,kMaxDurabilityAt)=hp;
    *F(p.obj,kDurabilityAt)=hp;
    std::uint32_t* flags=reinterpret_cast<std::uint32_t*>(F(p.obj,kFlags380));
    const bool canHurt=p.wholeHits || p.weakName[0];
    if(canHurt)*flags&=~kNoHpLoss;
    *flags|=kNotCounted;                       // the parts never hold up "every enemy dead"
    if(!canHurt)*flags|=kNotTarget;            // Panel01 armour: nothing aims at what takes no damage
    // the lock-on point (MAB list 1 ターゲット, tools/make_edf41_objects.py MOTHER401_POINTS): EDF6's lock-point component
    // (0x6C74E0) adds one per record of that list with AddLockTarget 0x547A50; EventAnimationObject has none, so it is added
    // here and kept on the model each frame (PartUpdate). Lockable only while the part is registered hostile (lock filter
    // 0x689E40), as it is from MakePanel / MakeBatteries until Unregister.
    if(Vec3Key(p.obj,L"edf41_401_lock",p.lockLocal)) {
        p.lockPoint=AddLockPoint(p.obj);
        if(!p.lockPoint)Fail("a 401 part's lock-on point was not added");
        else { float w[3]; Point(p.lockLocal,F(p.obj,kMatrixAt),w); SetLockPoint(p.lockPoint,w); }
    }
    // material_animation (4.1: every panel class but Panel01 reads its three entries with load modes 0 / 2 / 2 and starts [0]
    // looped in its constructor: 0x32f9e0, play [e]=1, [e+4]=2); EDF6's component takes the same kinds and modes (E508 0 / 2 / 2)
    static const int kKinds[3]={0,2,2};
    if(MaterialAnimsLoad(p.mat,p.obj,Key(p.obj,L"material_animation"),kKinds,3)>0) {
        MaterialAnimsPlay(p.mat,0,2);
        static std::atomic<int> said{0};
        if(said.fetch_add(1)<8)Log("EDF41M401 %s %p: %d material animations, [0] looped",KindName(p.kind),p.obj,p.mat.count);
    }
    p.burnUntil=180+Rand(p.rng,240);              // +0x4c8
    p.fallAt=180+Rand(p.rng,120);                 // +0x4c4
    switch(p.kind) {
        case Kind::bigCannon: SetupBullets(p); Enter(p,sBcIdle); break;
        case Kind::generator: p.gen.needTrigger=true; Enter(p,sGenIdle); break;
        case Kind::finalGenerator: Enter(p,sFgIdle); break;
        case Kind::finalCannon: SetupBullets(p); Enter(p,sFcIdle); break;
        default: break;
    }
    if(p.kind==Kind::finalGenerator && p.hasSpawn)std::memcpy(p.muzzle,p.spawnAt,12);   // its glow sits on 排出位置 (0x266f70)
    if(DevBits()&4194304)GlowsMake(p);   // development: 4194304 charge glows ON (off by default: they make the game's exit crash on some missions, see docs)
    if(IsBattery(p.kind)) {
        p.destroyedFor=120+Rand(p.rng,120);   // +0x438
        p.deleteAt=180+Rand(p.rng,120);       // +0x434
        SetupBullets(p);
        Enter(p,sGbIdle);
    }
}

// Panel01's batteries (0x26e4b0): per 砲台 locator, a weighted pick from parts_define ([0, w] = an empty mount).
void MakeBatteries(Part& p) noexcept {
    p.mountsMade=true;
    const SgoNode* mounts=Key(p.obj,L"edf41_401_mounts");
    const SgoNode* defs=Key(p.obj,L"parts_define");
    const int nm=Count(mounts),nd=Count(defs);
    if(nm<=0 || nd<=0)return;
    float total=0.0f;
    for(int i=0;i<nd;++i) { float w=0; Float(Child(Child(defs,i),1),&w); total+=w; }
    int made=0;
    for(int i=0;i<nm;++i) {
        Part::Mount m{{},0};
        if(!Vec(Child(mounts,i),m.pos,3))continue;
        float r=static_cast<float>(Rand(p.rng,10000))*0.00010001f*total;
        int pick=nd-1;
        for(int k=0;k<nd;++k) { float w=0; Float(Child(Child(defs,k),1),&w); if(r<w) { pick=k; break; } r-=w; }
        wchar_t path[260];
        if(Text(Child(Child(defs,pick),0),path,260)) {
            float mm[16];
            std::memcpy(mm,F(p.obj,kMatrixAt),sizeof(mm));
            Point(m.pos,F(p.obj,kMatrixAt),mm+12);
            void* o=CreateFromPath(mm,path);
            Part* b=o ? Find(o) : nullptr;
            if(b && IsBattery(b->kind)) {
                b->panel=p.id; b->mountIndex=i; b->team=p.team;
                b->rng=p.rng^(0x9E37ull*static_cast<std::uint64_t>(i+1));
                if(p.team>=0)SetTeam(o,p.team,true);
                m.battery=b->id;
                ++made;
            } else if(o) DeleteObject(o);
        }
        p.mounts.push_back(m);
    }
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<12)Log("EDF41M401 Panel01 %p: %d batteries on %d mounts",p.obj,made,nm);
}

// ---- the wrappers -----------------------------------------------------------------------------------------------------
LARGE_INTEGER Now() noexcept { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t; }
double Us(LARGE_INTEGER a,LARGE_INTEGER b) noexcept {
    static const double f=[]{LARGE_INTEGER q; QueryPerformanceFrequency(&q); return static_cast<double>(q.QuadPart);}();
    return 1e6*static_cast<double>(b.QuadPart-a.QuadPart)/f;
}
double partUsOurs=0.0,partUsStock=0.0;
std::uint32_t partCalls=0;

// Development (Edf41Mother401Dev bit 65536): the game's static cache EDF+0x20B38A0 (std::map of 0xA0-byte nodes; the exit-time crash
// EDF+0x7ADCFB walks it) logged: its size and each node's key and first words, to see what a run leaves in it.
void LogCache(const char* tag) noexcept {
    if(!(DevBits()&65536))return;
    const char* m=reinterpret_cast<const char*>(image+0x20B38A0);
    if(!Readable(m,16))return;
    const char* head=*reinterpret_cast<const char* const*>(m);
    const std::size_t size=*reinterpret_cast<const std::size_t*>(m+8);
    Log("EDF41M401 cache %s: %zu nodes",tag,size);
    if(!head || !Readable(head,0x20) || size>200)return;
    int shown=0;
    std::vector<const char*> todo;
    const char* root=*reinterpret_cast<const char* const*>(head+8);   // head->parent is the root
    todo.push_back(root);
    while(!todo.empty() && shown<60) {
        const char* n=todo.back(); todo.pop_back();
        if(!n || n==head || !Readable(n,0xA0) || n[0x19])continue;
        todo.push_back(*reinterpret_cast<const char* const*>(n));
        todo.push_back(*reinterpret_cast<const char* const*>(n+0x10));
        const std::size_t len=*reinterpret_cast<const std::size_t*>(n+0x30),cap=*reinterpret_cast<const std::size_t*>(n+0x38);
        const wchar_t* key=cap>=8 ? *reinterpret_cast<const wchar_t* const*>(n+0x20) : reinterpret_cast<const wchar_t*>(n+0x20);
        wchar_t text[80]={};
        if(len<80 && Readable(key,(len+1)*2))std::wmemcpy(text,key,len);
        Log("EDF41M401 cache %s: node %p key %ls | +40 %llx +48 %llx +50 %llx +58 %llx +98 %d",tag,n,text,
            *reinterpret_cast<const unsigned long long*>(n+0x40),*reinterpret_cast<const unsigned long long*>(n+0x48),
            *reinterpret_cast<const unsigned long long*>(n+0x50),*reinterpret_cast<const unsigned long long*>(n+0x58),
            *reinterpret_cast<const std::int32_t*>(n+0x98));
        ++shown;
    }
}

// Development (bit 65536): the game's own exit-time report of banks that still have references (EDF+0x1750870 calls its log function
// 0x3E4A0 with "audio::SoundBank::Release(error ref count : %d %ls)" for each; the bank cache's destructor then crashes on them).
// Its call site is redirected here to put the line into the plugin's log as well.
using VaLogFn=void(__cdecl*)(const wchar_t*,...);
void __cdecl ExitLeakHook(const wchar_t* fmt,int count,const wchar_t* name) {
    {   // the plugin's log is written by a thread that does not outlive the exit: a marker file
        char path[MAX_PATH+40]={};
        if(GetTempPathA(MAX_PATH,path)) {
            lstrcatA(path,"edf41m401_shutdown.txt");
            const HANDLE f=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(f!=INVALID_HANDLE_VALUE) {
                char line[400];
                const int k=_snprintf_s(line,sizeof(line),_TRUNCATE,"EXIT REPORT: SoundBank::Release error ref count %d %ls\r\n",count,name ? name : L"?");
                DWORD written=0;
                if(k>0)WriteFile(f,line,static_cast<DWORD>(k),&written,nullptr);
                CloseHandle(f);
            }
        }
    }
    reinterpret_cast<VaLogFn>(image+0x3E4A0)(fmt,count,name);
}
void InstallExitLeakHook() noexcept {
    static const unsigned char print[8]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74};
    if(!Readable(image+0x3E4A0,8))return;
    bool changed=false;
    const bool ok=RedirectCall(image+0x17508B5,image+0x3E4A0,reinterpret_cast<void*>(&ExitLeakHook),changed);
    Log("EDF41M401 exit report hook %s",ok ? "in" : "failed");
    (void)print;
}

// ---- charge glows: EDF6's gunbattery::ChargeEffect_StdParticle (tools/make_edf41_objects.py MOTHER401_GLOWS) --------------
// Made by the factory 0x6D92D0(shared_ptr* out, SGO node [[], ['ChargeEffect_StdParticle', [keys, [se]]]], matrix*), the one
// MovingFortress_Battery (0x4D3F22) uses. Its +0xC is the wanted state (0 shown, 1 hidden; slot 1 0x6D9910 turns the emitter's
// +0xE0 on / off when it differs from +8); slot 1 (UpdateContext*, matrix) queues the emitter's update on the scene's job list
// (manager +0x358) and slot 0 its render sync, both capturing the effect: a released effect is deleted a few hull frames
// later (the graveyard), never while a job may still hold it.
constexpr std::size_t kChargeFactory=0x6D92D0,kChargeStdVtable=0x17E7220;
const unsigned char kChargePrint[8]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74};
struct DeadGlow { Part::Glow g; int frames; };
std::vector<DeadGlow> graveyard;
void GlowRelease(Part::Glow& g) noexcept {   // the shared_ptr's use count, then its weak count (MSVC _Ref_count_base)
    if(DevBits()&16384) { g=Part::Glow{}; return; }   // dev: never destroyed (bisecting the exit crash)
    if(g.ctrl && Readable(g.ctrl,16)) {
        auto* uses=reinterpret_cast<std::atomic<std::int32_t>*>(static_cast<char*>(g.ctrl)+8);
        void* const* vt=*reinterpret_cast<void* const* const*>(g.ctrl);
        if(uses->fetch_sub(1)==1) {
            reinterpret_cast<void(__fastcall*)(void*)>(vt[0])(g.ctrl);
            auto* weak=reinterpret_cast<std::atomic<std::int32_t>*>(static_cast<char*>(g.ctrl)+0xC);
            if(weak->fetch_sub(1)==1)reinterpret_cast<void(__fastcall*)(void*)>(vt[1])(g.ctrl);
        }
    }
    delete[] g.m;
    g=Part::Glow{};
}
void GlowsMake(Part& p) noexcept {
    const SgoNode* list=Key(p.obj,L"edf41_401_charge");
    const int n=std::min(3,Count(list));
    if(n<=0)return;
    if(!Readable(image+kChargeFactory,8) || std::memcmp(image+kChargeFactory,kChargePrint,8)!=0) { Fail("the charge effect factory is not what this was written for"); return; }
    for(int i=0;i<n;++i) {
        Part::Glow& g=p.glows[i];
        g.m=new float[16];
        Point(p.muzzle,F(p.obj,kMatrixAt),g.m+12);
        std::memcpy(g.m,F(p.obj,kMatrixAt),48);
        g.m[15]=1.0f;
        alignas(16) void* out[2]={};
        reinterpret_cast<void*(__fastcall*)(void*,const SgoNode*,const float*)>(image+kChargeFactory)(out,Child(list,i),g.m);
        g.fx=out[0]; g.ctrl=out[1];
        if(!g.fx || !Readable(g.fx,0x3B0) || *static_cast<void* const*>(g.fx)!=image+kChargeStdVtable) {
            Fail("a 401 charge glow was not a ChargeEffect_StdParticle");
            GlowRelease(g);
            return;
        }
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(g.fx)+0xC)=1;   // hidden
        p.glowCount=i+1;
        if(DevBits()&131072) { GlowRelease(g); p.glowCount=0; return; }   // dev: created and destroyed at once (bisecting)
        static std::atomic<int> dsaid{0};
        if(dsaid.fetch_add(1)<3) {   // development evidence: what the effect's sound preset (+0x320) and handle (+0x3A0) hold
            const char* f=static_cast<const char*>(g.fx);
            Log("EDF41M401 glow %p: state +8=%d +0xC=%d, preset bank %p cue %d, handle %p %p, flag +0xE0=%d",g.fx,*reinterpret_cast<const std::int32_t*>(f+8),
                *reinterpret_cast<const std::int32_t*>(f+0xC),*reinterpret_cast<void* const*>(f+0x320+0x20),*reinterpret_cast<const std::int32_t*>(f+0x320+0x28),
                *reinterpret_cast<void* const*>(f+0x3A0),*reinterpret_cast<void* const*>(f+0x3A8),f[0xE0]);
        }
    }
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<6)Log("EDF41M401 %s %p: %d charge glows (ChargeEffect_StdParticle)",KindName(p.kind),p.obj,p.glowCount);
}
// The sync-render phase (EventAnimationObject slot 3 0x5B6D10 = the model's 0x6C06A0; MovingFortress_Battery's 0x4D1900 is the same
// slot): the glows' render job is queued here, never from Update. Queued next to the update job (what this did first) the two ran
// concurrently on the job threads and the render side read the particle ring while the update side popped it: the count went
// astray and the render's vector grew until it threw bad_alloc (EDF+0x30316D from 0x302D10, M809 / M018, 2026-10-16).
void __fastcall PartSyncRender(void* self) {
    reinterpret_cast<void(__fastcall*)(void*)>(partClone.orig[3])(self);
    std::lock_guard<std::recursive_mutex> hold(lock);
    Part* p=Find(self);
    if(!p || !p->glowCount)return;
    for(int i=0;i<p->glowCount;++i) {
        void* fx=p->glows[i].fx;
        if(!fx || (DevBits()&4096))continue;
        void* const* vt=*static_cast<void* const* const*>(fx);
        reinterpret_cast<void(__fastcall*)(void*)>(vt[0])(fx);
    }
}

// Every frame of a part with glows: the wanted state and the muzzle matrix to each, its update and render sync queued.
void GlowsUpdate(Part& p,void* ctx) noexcept {
    for(int i=0;i<p.glowCount;++i) {
        Part::Glow& g=p.glows[i];
        if(!g.fx)continue;
        std::memcpy(g.m,F(p.obj,kMatrixAt),48);
        Point(p.muzzle,F(p.obj,kMatrixAt),g.m+12);
        g.m[3]=g.m[7]=g.m[11]=0.0f; g.m[15]=1.0f;
        *reinterpret_cast<std::int32_t*>(static_cast<char*>(g.fx)+0xC)=(i==p.glowOn && !(DevBits()&524288)) ? 0 : 1;   // dev 524288: never shown (bisecting)
        void* const* vt=*static_cast<void* const* const*>(g.fx);
        if(!(DevBits()&8192))reinterpret_cast<void(__fastcall*)(void*,void*,const float*)>(vt[1])(g.fx,ctx,g.m);   // dev 8192: no update job
        // what the battery's Update does after it (MovingFortress_Battery 0x4D1B96): the effect's current state follows the wanted one
        // and its +0x10 (a per-frame amount slot 1 reads) is cleared
        {
            char* f=static_cast<char*>(g.fx);
            const std::int32_t want=*reinterpret_cast<std::int32_t*>(f+0xC);
            if(*reinterpret_cast<std::int32_t*>(f+8)!=want) { *reinterpret_cast<std::uint64_t*>(f+0x10)=0; *reinterpret_cast<std::int32_t*>(f+8)=want; }
        }
        (void)vt;   // its render job (slot 0) goes from the object's sync-render slot (PartSyncRender), the phase the game runs it in
    }
}
void GlowsBury(Part& p) noexcept {
    // The part is being destroyed (MovingFortress_Battery's own destructor drops its effects in the same place): its glows go now.
    // Burying them for a while (what this did first, and later at the game's shutdown) left them alive when the main loop ended,
    // which crashed the exit; destroyed with their part the same runs end clean.
    for(int i=0;i<p.glowCount;++i)
        if(p.glows[i].fx) { *reinterpret_cast<std::int32_t*>(static_cast<char*>(p.glows[i].fx)+0xC)=1; GlowRelease(p.glows[i]); }
    p.glowCount=0;
}
void GraveyardTick() noexcept {   // once per hull Update
    for(std::size_t i=0;i<graveyard.size();)
        if(--graveyard[i].frames<=0) { GlowRelease(graveyard[i].g); graveyard[i]=graveyard.back(); graveyard.pop_back(); }
        else ++i;
}

// Development: what the sound system looks like when this DLL detaches (before EDF's own static destructors, which crash walking
// the cache EDF+0x20B38A0 on a dead sound system EDF+0x20B2948: EDF+0x7ADCFB).
struct ExitProbe {
    ~ExitProbe() {
        if(!(Cfg().edf41Mother401Dev&65536) || !image)return;
        const char* g=Readable(image+0x20B2948,8) ? *reinterpret_cast<const char* const*>(image+0x20B2948) : nullptr;
        Log("EDF41M401 exit probe: sound system %p, list head %p size %zu, %d glows alive in the graveyard",g,
            g && Readable(g+0x40,16) ? *reinterpret_cast<const void* const*>(g+0x40) : nullptr,
            g && Readable(g+0x40,16) ? *reinterpret_cast<const std::size_t*>(g+0x48) : static_cast<std::size_t>(0),static_cast<int>(graveyard.size()));
        LogCache("at DLL detach");
    }
} exitProbe;

// The game's own shutdown (EDF+0x7021F0, the application object's destructor, which destroys the singletons one after the other, the
// sound system EDF+0x20B2948 among them): every charge glow still alive goes first. Real runs (2026-10-16, M018): one live
// ChargeEffect at that point makes the sound bank cache's static destructor (EDF+0x7ADCFB) touch the dead sound system; created and
// destroyed at once, or destroyed before the shutdown, the same runs exit clean.
void ShutdownGlows() noexcept {
    std::lock_guard<std::recursive_mutex> hold(lock);
    int n=0;
    for(auto& kv:byObj) {
        Part& p=*kv.second;
        for(int i=0;i<p.glowCount;++i)
            if(p.glows[i].fx) { GlowRelease(p.glows[i]); ++n; }
        p.glowCount=0;
    }
    for(DeadGlow& d:graveyard) { GlowRelease(d.g); ++n; }
    graveyard.clear();
    if(Cfg().edf41Mother401Dev&65536) {   // the plugin's log is written by a thread that does not outlive this: a marker file instead
        char path[MAX_PATH+40]={};
        if(GetTempPathA(MAX_PATH,path)) {
            lstrcatA(path,"edf41m401_shutdown.txt");
            const HANDLE f=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(f!=INVALID_HANDLE_VALUE) {
                char line[80];
                const int k=_snprintf_s(line,sizeof(line),_TRUNCATE,"shutdown hook ran: %d glows destroyed\r\n",n);
                DWORD written=0;
                if(k>0)WriteFile(f,line,static_cast<DWORD>(k),&written,nullptr);
                CloseHandle(f);
            }
        }
    }
    if(n)Log("EDF41M401 the game is shutting down: %d charge glows destroyed first",n);
}

void Forget(const void* self) noexcept {
    std::lock_guard<std::recursive_mutex> hold(lock);
    const auto it=byObj.find(self);
    if(it==byObj.end())return;
    Part& p=*it->second;
    StopSound(p.loopA,0); StopSound(p.loopB,0); StopSound(p.ambient,0);
    MaterialAnimsFree(p.mat);   // before the model it is bound to goes
    GlowsBury(p);
    for(WeakRef& w:p.gen.slots)WeakReset(w);
    byId.erase(p.id);
    byObj.erase(it);
    // the last of the classes' objects gone (the mission's end): nothing ticks the graveyard any more, so it is emptied here
    // (the scene destroys objects after the frame's jobs ran). A ChargeEffect left alive keeps its sound handle registered in
    // the sound system's static table (EDF+0x20B38A0), which then crashes in its exit-time destructor (EDF+0x7ADCFB reads the
    // sound system 0x20B2948 after it is gone: a real run, 2026-10-15, m401f_M018_ds5).
    if(byObj.empty()) {
        LogCache("when the last 401 object is gone");
    }
}

std::uint64_t __fastcall HullDtor(void* self,unsigned flags) {
    Log("EDF41M401 hull %p destroyed",self);
    Forget(self);
    return reinterpret_cast<DtorFn>(hullClone.orig[kSlotDtor])(self,flags);
}
std::uint64_t __fastcall PartDtor(void* self,unsigned flags) {
    Forget(self);
    return reinterpret_cast<DtorFn>(partClone.orig[kSlotDtor])(self,flags);
}

void HullFirst(Part& h) noexcept {
    ReadCues(h);
    const float* m=F(h.obj,kMatrixAt);
    h.yaw=h.yawTarget=h.yawNow=std::atan2(m[8],m[10]);   // only the yaw (4.1: atan2(m[0x80], m[0x88]))
    std::memcpy(h.target,m+12,12);
    float yawOnly[16];   // a spawn point whose face looks down (M012, M308 ...) does not tilt the ship: 4.1 builds a yaw matrix
    RotY(h.yaw,yawOnly);
    std::memcpy(yawOnly+12,h.target,12);
    std::memcpy(F(h.obj,kMatrixAt),yawOnly,sizeof(yawOnly));
    h.seed=static_cast<std::uint32_t>(static_cast<std::int32_t>(m[12]+m[14]));
    h.rng=0x9E3779B97F4A7C15ull^h.seed;
    h.team=Readable(F(h.obj,kTeamAt),4) ? *reinterpret_cast<const std::int32_t*>(F(h.obj,kTeamAt)) : -1;
    *reinterpret_cast<std::uint32_t*>(F(h.obj,kFlags380))|=kNotTarget;   // nothing aims at the invisible controller
    PlaySe(h,0,&h.ambient);
    // the borrowed part model's ragdoll out of the physics world: 4.1's hull has no body at all. Ragdoll::Disable 0x6E97C0 (every
    // body's +0xA4 cleared and its mode 0, 0x6ED480 -> 0x11B1710 removes it from the world; called from an object's virtual by
    // EDF6_Jormungand_AdditionalParts 0x38B321, no world lock around it).
    static const unsigned char kDisablePrint[8]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83};
    char* ragdoll=static_cast<char*>(h.obj)+kRagdollAt;
    if(Readable(image+0x6E97C0,8) && std::memcmp(image+0x6E97C0,kDisablePrint,8)==0 && Rd(ragdoll,0x90,"hull ragdoll")) {
        reinterpret_cast<void(__fastcall*)(void*)>(image+0x6E97C0)(ragdoll);
        h.ragdollOff=true;
    } else Fail("the ragdoll switch is not what this was written for: the hull keeps a body at its origin");
    LogCache("at the hull's first update");
    Log("EDF41M401 hull %p first update: at %.0f %.0f %.0f yaw %.1f deg, team %d, level %.2f, %d actions, ini Edf41Mother401Dev=%d",h.obj,
        m[12],m[13],m[14],h.yaw*57.29578f,h.team,*F(h.obj,kLevelAt),Count(Key(h.obj,L"action_list")),DevBits());
}

void HullLogic(Part& h) noexcept {
    ++h.hullFrames;
    // the hull's team as the script left it (the class's constructor registers 6; a script's SetTeam may come after the first
    // touch): the parts follow (4.1 slot 13 0x260900 hands the team to every panel, Panel01 to its batteries)
    const std::int32_t team=*reinterpret_cast<const std::int32_t*>(F(h.obj,kTeamAt));
    if(team!=h.team) {
        Log("EDF41M401 hull %p team %d -> %d: its parts follow",h.obj,h.team,team);
        h.team=team;
        for(const Cell& c:h.cells) {
            Part* p=ById(c.panel);
            if(!p || p->killed || p->vanishing)continue;
            p->team=team;
            SetTeam(p->obj,team,true);
            for(const Part::Mount& m:p->mounts)
                if(Part* b=ById(m.battery); b && !b->killed) { b->team=team; SetTeam(b->obj,team,true); }
        }
    }
    if(h.throttle>0)--h.throttle;
    // spin (0x261170): yaw speed +2e-7 per frame up to 0.0002 rad per frame
    if(h.spin) { h.yawAccel=std::min(0.0002f,h.yawAccel+2e-7f); h.yawTarget+=h.yawAccel; }
    // the move toward the target (4.1 +0x3c0, the route's next point; no 401 script gives it a route: it stays)
    float d[3]={h.target[0]-F(h.obj,kPosAt)[0],h.target[1]-F(h.obj,kPosAt)[1],h.target[2]-F(h.obj,kPosAt)[2]};
    const float dl=Len(d);
    if(dl>1e-3f) {
        for(int k=0;k<3;++k)h.velocity[k]+=d[k]/dl*0.001f;
        const float vl=Len(h.velocity),cap=std::min(dl*0.001f,1.0f);
        if(vl>cap)for(float& v:h.velocity)v*=cap/vl;
    } else std::memset(h.velocity,0,12);
    h.yawNow+=(h.yawTarget-h.yawNow)*0.01f;
    float m[16];
    RotY(h.yawNow,m);
    for(int k=0;k<3;++k)m[12+k]=F(h.obj,kPosAt)[k]+h.velocity[k];
    if(Cfg().edf41Mother401Place>0.0f && h.hullFrames>30) {
        float eye[3];
        if(NearestPlayer(F(h.obj,kPosAt),eye)) { m[12]=eye[0]; m[13]=eye[1]+Cfg().edf41Mother401Place; m[14]=eye[2]; std::memcpy(h.target,m+12,12); }
    }
    std::memcpy(F(h.obj,kMatrixAt),m,sizeof(m));
    // HP mirrors the Core (0x261170); the Core's death is the ship's (0x2618a0 -> 0x261950)
    Part* core=ById(h.core);
    if(core) {
        *F(h.obj,kMaxDurabilityAt)=*F(core->obj,kMaxDurabilityAt);
        *F(h.obj,kDurabilityAt)=std::max(0.0f,*F(core->obj,kDurabilityAt));
        if(!h.dying && (core->killed || Hp(core->obj)<=0.0f)) {
            h.dying=true;
            StopSound(h.ambient,120);
            *F(h.obj,kDurabilityAt)=0.0f;
            static_cast<char*>(h.obj)[kDestroyedAt]=1;   // the script's "destroyed" (9201) sees the ship die with its Core
            int n=0;
            for(const Cell& c:h.cells) {
                Part* p=ById(c.panel);
                if(!p || p==core)continue;
                Destroy(*p,static_cast<int>(Random01(h.rng)*1200.0f-600.0f+Dist(F(h.obj,kPosAt),F(p->obj,kPosAt))*1.5f));
                ++n;
            }
            Log("EDF41M401 hull %p: the Core is dead, %d panels go down after it",h.obj,n);
        }
    }
    if(Rd(F(h.obj,kDurabilityAt),4,"hull hp") && h.dying)*F(h.obj,kDurabilityAt)=0.0f;
    // the sound follows the panels' average place
    float avg[3]={},cnt=0;
    for(const Cell& c:h.cells)
        if(const Part* p=ById(c.panel)) { for(int k=0;k<3;++k)avg[k]+=F(p->obj,kPosAt)[k]; cnt+=1; }
    if(cnt>0) { for(float& v:avg)v/=cnt; MoveSound(h.ambient,avg); }
    // development: the player kept alive (the autopilot stands under the ship and cannot dodge; a run that has to see the end)
    if(DevBits()&128)
        if(unsigned char* me=PlayerHuman(); me && Readable(me+kMaxDurabilityAt,8) && *F(me,kMaxDurabilityAt)>0.0f)*F(me,kDurabilityAt)=*F(me,kMaxDurabilityAt);
    // the dev switches at Edf41Mother401DevKill seconds
    if((DevBits()&32) && h.hullFrames==static_cast<std::uint32_t>(std::max(1.0f,Cfg().edf41Mother401DevKill*kFps))) {
        Log("EDF41M401 dev: ObjectAction %d",Cfg().edf41Mother401DevAction);
        Action(h,Cfg().edf41Mother401DevAction);
    }
    if((DevBits()&24) && !h.dying && h.hullFrames==static_cast<std::uint32_t>(std::max(1.0f,Cfg().edf41Mother401DevKill*kFps))) {
        if((DevBits()&8) && core) { Log("EDF41M401 dev: the Core is destroyed"); Destroy(*core,0); }
        if(DevBits()&16)
            for(const Cell& c:h.cells)
                if(Part* p=ById(c.panel); p && p->kind==Kind::weak && !p->killed) { Log("EDF41M401 dev: a weak panel is destroyed"); p->killed=true; Unregister(*p);
                    *F(p->obj,kDurabilityAt)=0.0f; const float down[3]={0,-1,0}; float q[3]; std::memcpy(q,F(p->obj,kPosAt),12); q[1]-=20.0f;
                    Explosion(q,down,15.0f); AreaDestroy(h,F(p->obj,kPosAt),200.0f); Destroy(*p,0); break; }
    }
}

std::uint64_t __fastcall HullUpdate(void* self,void* dt) {
    const LARGE_INTEGER t0=Now();
    ++hullTick;
    bool del=false;
    {
        std::lock_guard<std::recursive_mutex> hold(lock);
        Part* h=Find(self);
        if(h && OnlineRunsHere(self)) {
            if(!h->seen) { h->seen=true; HullFirst(*h); }
            h->lastTick=hullTick;
            HullLogic(*h);
            if(h->dying) {
                bool any=false;
                for(const Cell& c:h->cells)any=any || ById(c.panel)!=nullptr;
                del=!any;
            }
            if(h->hullFrames%600==0) {
                int alive=0;
                for(const Cell& c:h->cells)alive+=ById(c.panel)!=nullptr;
                Log("EDF41M401 hull %p frame %u: %d of %zu cells hold a panel, %zu objects in all; plugin %.0f us/frame on the parts, their stock updates %.0f us/frame (%u calls)",
                    self,h->hullFrames,alive,h->cells.size(),byObj.size(),partUsOurs/600.0,partUsStock/600.0,partCalls);
                if(h->hullFrames==600)LogCache("at hull frame 600");
                if(h->hullFrames==3600)LogCache("at hull frame 3600");
                if(h->hullFrames==600 && h->ragdollOff) {   // the hull's bodies stay out of the world (+0xA4 0, mode +0x9C 0)
                    const char* r=static_cast<const char*>(self)+kRagdollAt;
                    const char* bodies=*reinterpret_cast<char* const*>(r+0x78);
                    const std::size_t n=*reinterpret_cast<const std::size_t*>(r+0x88);
                    int in=0;
                    for(std::size_t i=0;i<n && i<32 && bodies && Readable(bodies+i*0xC0,0xC0);++i)in+=bodies[i*0xC0+0xA4]!=0 || *reinterpret_cast<const std::int32_t*>(bodies+i*0xC0+0x9C)!=0;
                    Log("EDF41M401 hull %p: %d of its %zu ragdoll bodies are back in the world",self,in,n);
                }
                if(Cfg().edf41DevShoot>0) {
                    char text[640]; int at=0;
                    for(int k=1;k<12;++k)
                        if(devTaken[k][2] || devTaken[k][0] || devTaken[k][1])
                            at+=std::snprintf(text+at,sizeof(text)-static_cast<std::size_t>(at),"%s %d taken / %d refused; ",KindName(static_cast<Kind>(k)),
                                              devTaken[k][0],devTaken[k][1]);
                    if(at)Log("EDF41M401 dev: hits so far: %s",text);
                }
                partUsOurs=partUsStock=0.0; partCalls=0;
            }
        }
    }
    const std::uint64_t r=reinterpret_cast<UpdateFn>(hullClone.orig[kSlotUpdate])(self,dt);
    static const float hidden[16]={1e-4f,0,0,0, 0,1e-4f,0,0, 0,0,1e-4f,0, 0,0,0,1};
    float m[16];
    std::memcpy(m,hidden,sizeof(m));
    std::memcpy(m+12,F(self,kPosAt),12);
    ModelMatrix(self,m);
    if(del) {
        Log("EDF41M401 hull %p: every panel is gone, the hull goes",self);
        { std::lock_guard<std::recursive_mutex> hold(lock); if(Part* h=Find(self))h->gone=true; }
        DeleteObject(self);
    }
    (void)t0;
    return r;
}

std::uint64_t __fastcall HullMessage(void* self,std::uint32_t msg,void* data) {
    {
        std::lock_guard<std::recursive_mutex> hold(lock);
        Part* h=Find(self);
        if(h && OnlineRunsHere(self)) {
            if(!h->seen) { h->seen=true; HullFirst(*h); }
            if(msg==kMsgAction && data && Rd(data,4,"ObjectAction data")) {
                const std::int32_t n=*static_cast<const std::int32_t*>(data);
                Log("EDF41M401 hull %p ObjectAction %d",self,n);
                if(!h->dying)Action(*h,n);
                return 1;
            }
            if(msg==kMsgParts && data && Rd(data,0x18,"GetPartsObject data")) {
                const std::int32_t* q=static_cast<const std::int32_t*>(data);
                int seen=0;
                for(const Cell& c:h->cells) {
                    Part* p=ById(c.panel);
                    if(!p || c.group!=q[0])continue;
                    if(seen++==q[1]) {
                        const bool ok=WeakOut(p->obj,static_cast<char*>(data)+8);
                        Log("EDF41M401 hull %p GetPartsObject group %d index %d -> %s %p%s",self,q[0],q[1],KindName(p->kind),p->obj,ok ? "" : " (weak pointer failed)");
                        return ok ? 1 : 0;
                    }
                }
                Log("EDF41M401 hull %p GetPartsObject group %d index %d: none",self,q[0],q[1]);
                return 0;
            }
            if(msg==kMsgGenerator && data) {
                std::int32_t index=-1;
                if(Rd(static_cast<char*>(data)+0x48,4,"generator index"))std::memcpy(&index,static_cast<char*>(data)+0x48,4);
                GenCfg g;
                if(index>=0 && index<static_cast<int>(h->hosts.size()) && ReadGenCfg(static_cast<const char*>(data),g)) {
                    if(Part* p=ById(h->cells[static_cast<std::size_t>(h->hosts[static_cast<std::size_t>(index)])].panel))SetGenerator(*p,g);
                } else Log("EDF41M401 hull %p generator %d: the layout has %zu generators, ignored",self,index,h->hosts.size());
                return 1;
            }
            if(msg==kMsgDamage)return 0;
        }
    }
    return reinterpret_cast<MessageFn>(hullClone.orig[kSlotMessage])(self,msg,data);
}
bool __fastcall HullAccept(void*,void*) { return false; }   // the hull takes no damage (4.1: it has no body)

// The panel's place (4.1 slot 1 0x270110): flies in, arrives, falls or flies away.
void MovePanel(Part& p,const Part& h) noexcept {
    const float* hm=HullMatrix(h);
    if(p.mode==1) {   // free fall in world space
        float* pos=F(p.obj,kPosAt);
        for(int k=0;k<3;++k)pos[k]+=p.wvel[k];
        p.wvel[1]-=0.002f;
        return;
    }
    if(p.triggered && p.delay>0)--p.delay;
    if(p.triggered && p.delay<=0) {
        Part* hh=ById(p.hull);
        if(hh && !p.arrived && p.mode==0 && p.cur[1]==1000.0f && hh->throttle==0) { PlaySe(p,0); hh->throttle=180; }
        if(p.mode==0 && !p.arrived) {
            const float d[3]={p.dst[0]-p.cur[0],p.dst[1]-p.cur[1],p.dst[2]-p.cur[2]};
            if(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>1e-4f) {
                for(int k=0;k<3;++k)p.vel[k]=d[k]*0.02f;
                const float l=Len(p.vel);
                if(l>p.cap)for(float& v:p.vel)v*=p.cap/l;
                else if(l<0.01f && l>0.0f)for(float& v:p.vel)v*=0.01f/l;
                if(Len(d)<=Len(p.vel)) { std::memset(p.vel,0,12); std::memcpy(p.cur,p.dst,12); }
            } else {
                std::memset(p.vel,0,12);
                std::memcpy(p.cur,p.dst,12);
                if(!p.arrived) { p.arrived=true; PlaySe(p,1); }
            }
        }
        if(p.mode==2)p.vel[1]+=0.004f;
        for(int k=0;k<3;++k)p.cur[k]+=p.vel[k];
    }
    float before[3];
    std::memcpy(before,F(p.obj,kPosAt),12);
    float w[16];
    WorldMatrixOfPanel(p,hm,w);
    std::memcpy(F(p.obj,kMatrixAt),w,sizeof(w));
    for(int k=0;k<3;++k)p.wvel[k]=w[12+k]-before[k];
}

void MoveBattery(Part& b,const Part* panel) noexcept {
    float* pos=F(b.obj,kPosAt);
    if(b.detached || !panel || b.mountIndex<0 || b.mountIndex>=static_cast<int>(panel->mounts.size())) {
        for(int k=0;k<3;++k)pos[k]+=b.wvel[k];
        b.wvel[1]-=0.002f;
        return;
    }
    float m[16];
    std::memcpy(m,F(panel->obj,kMatrixAt),sizeof(m));
    Point(panel->mounts[static_cast<std::size_t>(b.mountIndex)].pos,F(panel->obj,kMatrixAt),m+12);
    for(int k=0;k<3;++k)b.wvel[k]=m[12+k]-pos[k];
    std::memcpy(F(b.obj,kMatrixAt),m,sizeof(m));
    b.batteryAttack=panel->arrived && (panel->attack || (DevBits()&1)) && !panel->killed && !panel->vanishing;
}

std::uint64_t __fastcall PartUpdate(void* self,void* dt) {
    const LARGE_INTEGER t0=Now();
    bool del=false,scaled=false;
    float scale=1.0f;
    {
        std::lock_guard<std::recursive_mutex> hold(lock);
        Part* p=Find(self);
        if(p && OnlineRunsHere(self)) {
            if(!p->seen) { p->seen=true; InitPart(*p); }
            if(!p->farDone && p->farTries<600) { ++p->farTries; p->farDone=FarRender(self); }
            if(IsBattery(p->kind)) {
                Part* panel=ById(p->panel);
                if(p->panel && !panel && !p->detached)del=true;   // its panel is gone: so is it (4.1: a child object)
                else { MoveBattery(*p,panel); del=TickBattery(*p); }
            } else {
                Part* h=HullOf(*p);
                if(!h)del=true;                                   // the hull is gone (the script's DeleteObject): its panels go
                else {
                    if(p->kind==Kind::panel01 && !p->mountsMade && p->triggered)MakeBatteries(*p);
                    MovePanel(*p,*h);
                    del=TickPanel(*p);
                    if((p->kind==Kind::generator || p->kind==Kind::finalGenerator) && p->arrived && !p->killed && !p->vanishing)RunGenerator(*p);
                    scale=p->scale; scaled=p->state==sShrink;
                }
            }
            ++p->frames;
            p->lastTick=hullTick;
            if(p->glowCount && !del)GlowsUpdate(*p,dt);
        }
    }
    const LARGE_INTEGER t1=Now();
    const std::uint64_t r=reinterpret_cast<UpdateFn>(partClone.orig[kSlotUpdate])(self,dt);
    if(scaled) {   // the shrink (0x270870): x = z = 1 - 2t, y = 1 + 2t, drawn only
        float m[16];
        std::memcpy(m,F(self,kMatrixAt),sizeof(m));
        const float sx=std::max(1e-4f,scale),sy=2.0f-scale;
        for(int k=0;k<3;++k) { m[k]*=sx; m[4+k]*=sy; m[8+k]*=sx; }
        ModelMatrix(self,m);
    }
    float weakAt[3];
    bool weakShot=false,canHurt=false,devInside=false,weakGated=false;
    std::uint32_t hullFramesNow=0;
    {
        std::lock_guard<std::recursive_mutex> hold(lock);
        if(Part* p=Find(self); p && p->seen) {
            MaterialAnimsUpdate(p->mat,dt);   // 4.1: in Update after PanelBase's (0x263100 ...), with its frame info
            if(p->lockPoint) { float w[3]; Point(p->lockLocal,F(self,kMatrixAt),w); SetLockPoint(p->lockPoint,w); }
            if(p->weakBody==-2 && p->weakTries<600) {   // the weak body by name, once the ragdoll has its bodies
                ++p->weakTries;
                // the body's name is its SHKT proxy's, RagDollProxys.<weakpoint> (4.1 0x3348f0 takes the bare name)
                wchar_t proxy[64];
                std::swprintf(proxy,64,L"RagDollProxys.%ls",p->weakName);
                int b=RagdollBody(static_cast<char*>(self)+kRagdollAt,proxy);
                if(b<0)b=RagdollBody(static_cast<char*>(self)+kRagdollAt,p->weakName);
                const std::size_t bodies=Readable(static_cast<char*>(self)+kRagdollAt+0x88,8) ? *reinterpret_cast<const std::size_t*>(static_cast<char*>(self)+kRagdollAt+0x88) : 0;
                if(b>=0 || p->weakTries==600) {
                    p->weakBody=b>=0 ? b : -1;
                    static std::atomic<int> said{0};
                    if(said.fetch_add(1)<16 || b<0)Log("EDF41M401 %s %p: weak body %ls = %d after %d frames (the ragdoll has %zu bodies)",KindName(p->kind),self,
                                                       p->weakName,b,p->weakTries,bodies);
                }
            }
            if(p->weakBody>=0 && !p->wholeHits) {   // the weak body's bone where the CAS has it now (a Generator's core moves as it opens)
                if(p->weakBone==-2)p->weakBone=BoneIndex(static_cast<char*>(self)+kEaoSkeleton,p->weakName);
                float bm[16];
                if(p->weakBone>=0 && BoneMatrix(static_cast<char*>(self)+kEaoSkeleton,p->weakBone,bm))std::memcpy(weakAt,bm+12,12);
                else Point(p->weakPt,F(self,kMatrixAt),weakAt);
                weakShot=true;
            }
            // a Generator's weak body (core) is its hatch: closed it is covered by the flat panel's shell (body 0, where every hit lands), so
            // the dev shot waits until the hatch is open (4.1 plays the same: the hatch is the weak point of an open generator)
            devInside=p->kind==Kind::generator;
            if(p->kind==Kind::generator && !(p->state==sGenOpen || p->state==sGenSpawn || p->state==sGenWait))weakGated=true;
            canHurt=(p->wholeHits || p->weakBody>=0) && (!IsPanel(p->kind) || (p->triggered && p->delay<=0));
            const Part* owner=IsPanel(p->kind) ? p : ById(p->panel);
            if(const Part* h=owner ? HullOf(*owner) : nullptr)hullFramesNow=h->hullFrames;
        }
    }
    // development (ini Edf41DevShoot): the lock-on point (the aim point), and for a weak-body part its weak body's bone too
    // bit 256: not before Edf41Mother401DevKill seconds of the hull (M809's script watches its weak panels only from its OA(0) on)
    const bool devLater=(DevBits()&256) && static_cast<float>(hullFramesNow)<Cfg().edf41Mother401DevKill*kFps;
    if(!del && canHurt && !devLater && Alive(self)) {   // (the Panel01 armour takes no hit at all)
        float aim[3];
        if(AimPoint(self,aim) && !weakGated)DevShoot(self,aim);
        // a Generator's weak body (core) sits inside its flat panel's shell (body 0): the shot starts 1.5 m short of the bone
        if(weakShot && !weakGated)DevShoot(self,weakAt,1,devInside ? 20.0f : 20.0f);
    }
    const LARGE_INTEGER t2=Now();
    {
        std::lock_guard<std::recursive_mutex> hold(lock);
        partUsOurs+=Us(t0,t1); partUsStock+=Us(t1,t2); ++partCalls;
    }
    if(del) {
        { std::lock_guard<std::recursive_mutex> hold(lock); if(Part* p=Find(self))p->gone=true; }
        DeleteObject(self);
    }
    return r;
}

// slot 16: does this hit count (4.1 slot 10: 0x26fe60, only the weakpoint body; FinalCannon / FinalGenerator / batteries every hit)
bool __fastcall PartAccept(void* self,void* gdi) {
    std::lock_guard<std::recursive_mutex> hold(lock);
    Part* p=Find(self);
    if(!p)return true;
    if(p->noDamage || p->vanishing || p->killed || p->state==sGbDestroyed || p->state==sGbFall)return false;
    // a panel has no physics shell until its flight starts (4.1 slot 1 0x270110 builds it with 0x26fab0 when the delay runs
    // out): nothing hits one still waiting 1000 above its cell
    if(IsPanel(p->kind) && (!p->triggered || p->delay>0))return false;
    if(Cfg().edf41DevShoot>0)++devTaken[static_cast<int>(p->kind)][p->wholeHits ? 0 : 2];
    if(p->wholeHits)return true;
    if(p->weakBody<0)return false;
    const int body=HitBody(static_cast<char*>(self)+kRagdollAt,gdi);
    if(Cfg().edf41DevShoot>0) {   // development: which body the hits land on, per class (the filter's evidence)
        ++devTaken[static_cast<int>(p->kind)][body==p->weakBody ? 0 : 1];
        static std::atomic<int> said{0};
        const int n=said.fetch_add(1);
        if(n<24 || n%500==0)Log("EDF41M401 dev: a hit on %s %p lands on body %d (weak body %d): %s",KindName(p->kind),self,body,p->weakBody,
                               body==p->weakBody ? "taken" : "refused");
    }
    return body==p->weakBody;
}

// slot 18 (after the HP change): the kill (4.1 slot 11 / 32) or the damage sound.
void __fastcall PartDamaged(void* self,void* gdi) {
    reinterpret_cast<void(__fastcall*)(void*,void*)>(partClone.orig[kSlotDamaged])(self,gdi);
    std::lock_guard<std::recursive_mutex> hold(lock);
    Part* p=Find(self);
    if(!p || p->killed || p->vanishing)return;
    // spawned enemies notice the hit (0x331d10): Generator 0x268720 on a hit it survives and once on the killing one,
    // FinalGenerator 0x2676e0 on every hit
    if(p->kind==Kind::finalGenerator || (p->kind==Kind::generator && (Hp(self)>0.0f || !p->encountDone))) {
        if(Hp(self)<=0.0f)p->encountDone=true;
        Encount(*p);
    }
    if(Hp(self)>0.0f) {
        PlaySe(*p,IsBattery(p->kind) ? 0 : 5);
        MaterialAnimsPlay(p->mat,1,0);   // the hit flash, once (0x2639a0 ...: [e+4] = 0)
        static std::atomic<int> said{0};
        if(said.fetch_add(1)<12)Log("EDF41M401 %s %p takes a hit: %.0f of %.0f left",KindName(p->kind),self,Hp(self),*F(self,kMaxDurabilityAt));
        return;
    }
    if(IsBattery(p->kind)) {   // 0x269b40
        p->killed=true;
        Unregister(*p);
        Enter(*p,sGbDestroyed);
        static std::atomic<int> said{0};
        if(said.fetch_add(1)<12)Log("EDF41M401 %s %p destroyed by damage",KindName(p->kind),self);
        return;
    }
    MaterialAnimsPlay(p->mat,2,1);   // OnKilled (slot 32): the death track, to its end and held ([e+4] = 1)
    Log("EDF41M401 %s %p destroyed by damage",KindName(p->kind),self);
    Part* h=HullOf(*p);
    if(p->kind==Kind::weak && h) {   // 0x2715a0
        float q[3];
        std::memcpy(q,F(self,kPosAt),12);
        q[1]-=20.0f;
        const float down[3]={0,-1,0};
        Explosion(q,down,15.0f);
        const float grey[4]={0.5f,0.5f,0.5f,2.5f};   // ten grey light flashes, +0x60 = i x 300, 180 frames (0x271642)
        if(!(DevBits()&1024))for(int i=0;i<10;++i)StencilLight(q,grey,static_cast<float>(i)*300.0f,180);   // dev 1024: none
        q[1]-=20.0f;
        Smoke(q,1.0f);                                // 0x1b2d90: the upward smoke puff (煙_01)
        p->killed=true;   // its own destroy below, not the blast's
        AreaDestroy(*h,F(self,kPosAt),200.0f);
        p->killed=false;
    }
    if(p->kind==Kind::core) {        // 0x264490: an explosion where it was hit
        const char* g=static_cast<const char*>(gdi);
        if(Rd(g+0x30,16,"core hit point")) { const float up[3]={0,1,0}; BigExplosion(reinterpret_cast<const float*>(g+0x30),up,2.5f,1.0f); }
    }
    Destroy(*p,0);
}

std::uint64_t __fastcall PartMessage(void* self,std::uint32_t msg,void* data) {
    return reinterpret_cast<MessageFn>(partClone.orig[kSlotMessage])(self,msg,data);
}

// ---- creation ------------------------------------------------------------------------------------------------------
void Register(void* obj,Kind kind) noexcept {
    std::lock_guard<std::recursive_mutex> hold(lock);
    auto rec=std::make_unique<Part>();
    rec->id=nextId++;
    rec->lastTick=hullTick;
    rec->obj=obj;
    rec->kind=kind;
    // GetPartsObject's group (+0x4d0): BigCannon01 1, WeakPanel 2, Core 3, the generators 0, the rest -1 (SPEC 1.5 / notes A, B)
    rec->group=kind==Kind::bigCannon ? 1 : kind==Kind::weak ? 2 : kind==Kind::core ? 3 : (kind==Kind::generator || kind==Kind::finalGenerator) ? 0 : -1;
    byId[rec->id]=rec.get();
    byObj[obj]=std::move(rec);
}
void CreateHull(void* info) noexcept {
    void* obj=CloneObject(hullClone,kEaoCtor,kEaoSize,info);
    if(!obj)return;
    Register(obj,Kind::hull);
    Log("EDF41M401 create #%d: UfoMother401 hull %p (EventAnimationObject, model hidden)",++hullsMade,obj);
}
template<Kind K> void CreatePart(void* info) noexcept {
    void* obj=CloneObject(partClone,kEaoCtor,kEaoSize,info);
    if(!obj)return;
    Register(obj,K);
    if(++partsMade<=3)Log("EDF41M401 create %s %p",KindName(K),obj);
}

}  // namespace

// Development bit 32768: Edf41DevShoot also shoots the mission's ordinary enemies (every live lockable hostile object that is not
// one of ours), so the scripts' kill counts / waits advance and a mission that creates the ship late gets there. Once a frame.
void Edf41Mother401DevTick() noexcept {
    static ULONGLONG lastFrame=~0ull;   // called from the on-foot frame and the riding frame: once per game frame
    if(GameFrame()==lastFrame)return;
    lastFrame=GameFrame();
    {   // the buried charge glows' clock: one tick per game frame, whether or not a hull exists (the render side may still hold a
        // buried effect a frame or two after its last job, so it is freed 120 frames later)
        std::lock_guard<std::recursive_mutex> hold(lock);
        if(!graveyard.empty())GraveyardTick();
    }
    if(!(Cfg().edf41Mother401Dev&32768) || Cfg().edf41DevShoot<=0)return;
    static unsigned frame=0;
    // a slow kill (one enemy every 30 frames, the window rotating) so a script that waits for the enemy count to fall to a figure
    // sees it fall; bit 262144 shoots them all at once, 48 a tick (a mission whose ship comes late is cleared before it comes)
    const bool fast=(Cfg().edf41Mother401Dev&262144)!=0;
    if(++frame%(fast ? 6u : 30u)!=0)return;
    struct Ctx { int n; const void* list[256]; float aim[256][3]; } c{};
    {
        std::lock_guard<std::recursive_mutex> hold(lock);
        VisitEnemiesOf(0,[](void* ctx,const void* object,const float* aim) {
            Ctx* k=static_cast<Ctx*>(ctx);
            if(k->n<256 && !Find(object)) { k->list[k->n]=object; std::memcpy(k->aim[k->n],aim,12); ++k->n; }
        },&c);
    }
    // 48 a tick, the window rotating over the whole list (a fixed first 48 would be the same ones every time)
    const int take=c.n<(fast ? 48 : 1) ? c.n : (fast ? 48 : 1),start=c.n ? static_cast<int>((frame/6u*37u)%static_cast<unsigned>(c.n)) : 0;
    for(int k=0;k<take;++k) { const int at=(start+k)%c.n; DevShoot(c.list[at],c.aim[at],1000000u+static_cast<unsigned>(at)); }
    static std::atomic<int> said{0};
    if(c.n && said.fetch_add(1)<4)Log("EDF41M401 dev: shooting %d ordinary enemies",c.n);
}

bool InstallEdf41Mother401() noexcept {
    if(!Cfg().edf41Mother401) { Log("EDF41M401 off (ini Edf41Mother401=0)"); return false; }
    if(Cfg().edf41Mother401Dev&65536)InstallExitLeakHook();
    AtGameShutdown(&ShutdownGlows);
    if(!BuildClone(hullClone,&HullDtor,&HullUpdate,&HullMessage) || !BuildClone(partClone,&PartDtor,&PartUpdate,&PartMessage)) {
        Log("EDF41M401 the vtable clones could not be built: off");
        return false;
    }
    SetSlot(hullClone,kSlotAccept,reinterpret_cast<void*>(&HullAccept));
    SetSlot(partClone,kSlotAccept,reinterpret_cast<void*>(&PartAccept));
    SetSlot(partClone,3,reinterpret_cast<void*>(&PartSyncRender));   // the glows' render job
    SetSlot(partClone,kSlotDamaged,reinterpret_cast<void*>(&PartDamaged));
    AllowAnyTeam(hullClone);   // EventAnimationObject's slot 22 allows only team 6: CreateEnemy's SetTeam(1) and the parts' would not take
    AllowAnyTeam(partClone);
    struct Reg { const wchar_t* name; CreateFn fn; };
    const Reg regs[]={
        {L"UfoMother401",&CreateHull},
        {L"UfoMother401_Panel01",&CreatePart<Kind::panel01>},{L"UfoMother401_WeakPanel",&CreatePart<Kind::weak>},
        {L"UfoMother401_Core",&CreatePart<Kind::core>},{L"UfoMother401_BigCannon01",&CreatePart<Kind::bigCannon>},
        {L"UfoMother401_Generator",&CreatePart<Kind::generator>},{L"UfoMother401_FinalCannon01",&CreatePart<Kind::finalCannon>},
        {L"UfoMother401_FinalGenerator",&CreatePart<Kind::finalGenerator>},
        {L"UfoMother401_GunBattery_SmallLaser",&CreatePart<Kind::smallLaser>},{L"UfoMother401_GunBattery_Sniper",&CreatePart<Kind::sniper>},
        {L"UfoMother401_GunBattery_PlasmaCannon",&CreatePart<Kind::plasma>},{L"UfoMother401_GunBattery_RapidCannon",&CreatePart<Kind::rapid>},
    };
    bool ok=true;
    for(const Reg& r:regs)ok=AddFactory(r.name,r.fn) && ok;
    Log("EDF41M401 on: UfoMother401 and its 11 part classes %s (ini Edf41Mother401Dev=%d)",ok ? "queued for the game's registration" : "NOT all registered",
        Cfg().edf41Mother401Dev);
    return ok;
}
}  // namespace crew
