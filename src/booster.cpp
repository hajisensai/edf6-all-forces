// The exhaust flames (尾焰) and the arrival's smoke (入场尾烟) of the plugin's aircraft: the stock Booster object (the
// stock transport's nozzle flame) on each nozzle, the stock LineStripObject (the missiles' smoke ribbon) behind each.
//
// Where the nozzles are, the model says (exhaust_nozzles.h; the user, 2026-10-10: 「尾烟和实际不对。尾烟应该跟着模型生成，
// 而不是用两段可能不同步的代码维护」): the plugin's own models carry them as bones (pylib/jet_models.py with_nozzles: the
// jets' exits on their body, the carrier's four boosters on its pods, read off the stock V508's SGO at build time), the
// stock models flown as they are have them measured into src/nozzles_gen.h (tools/gen_nozzles.py). The flames and the
// smoke both take each nozzle's world matrix from ExhaustOf, worked out once a frame for the vehicle (one set of numbers:
// the smoke leaves where the flame burns). A model with no nozzles (the Primer fighter, an old install's models made
// before the nozzle bones) has neither.
//
// The Booster (docs/jet-model-re.md §8):
//   op_new 0x12D85B0(0x410); ctor 0x2CB810(obj, &param) (vtable 0x17A6D58, written at 0x2CB851); the ctor
//   attaches the booster to param+0x30 with 0x118AF20(parent, child) (child+0x38 = parent, child appended to
//   the parent's list) and hands a shared reference back in *(param+0x20); the stock caller (0x5E4CA0) then
//   registers it, 0x1195A20(mgr, &out) and 0x1197050(mgr, out[0]), and drops its own reference. (H)
//   Update (vtable slot 5, 0x2CBE30): copies the 64-byte matrix *(+0x3D8) to +0x60 each frame, so the matrix
//   storage outlives the booster (static here). The flame shows while +0x3EC or +0x3F0 is above a threshold;
//   +0x3F0 decays by +0x3E8 a frame, +0x3F4 counts down and zeroes +0x3EC at 0. (H)
//   The flame leaves along its matrix's +z (the Booster's direction (0,0,1), 0x1765B70) from its origin.
// The plugin feeds each nozzle's world matrix (unit rows: a scaled model's scale is in the flame's size) and every frame
// sets +0x3EC = how strongly it burns, +0x3F0 = 1, +0x3F4 = 3; a vehicle gone for kStaleMs, or dead, has its boosters
// deleted (BoosterSweep, once a frame from jet.cpp JetReap: also once the last jet is gone). (H/M)
#include "crew.h"
#include "body506.h"
#include "exhaust_nozzles.h"
#include "exhaust_pose.h"
#include "jet_internal.h"   // FaultLog, BoosterSweep
#include "memory.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <intrin.h>

namespace crew {
static_assert(exhaust::kRecStride==kBoneStride && exhaust::kRecLocal==kBoneLocal506 && exhaust::kRecWorld==kBoneWorld506,
              "exhaust_nozzles.h reads the bone records body506.h describes");
namespace {
constexpr unsigned kOpNew=0x12D85B0,kCtor=0x2CB810,kRegister=0x1195A20,kRegister2=0x1197050,kUpdate=0x2CBE30;
constexpr unsigned kDelete=0x118A1B0,kVtable=0x17A6D58,kVtableLea=0x2CB851;
constexpr unsigned kParamVtable=0x17AE438,kConstA=0x1765B70,kConstB=0x17A8D80,kConstC=0x17A8D70,kConstD=0x1765B90;
constexpr std::size_t kObjectMgr=0x20B2958,kUpdateSlot=5,kSize=0x410;
constexpr std::size_t kLevel=0x3EC,kPulse=0x3F0,kHold=0x3F4,kObjFlags=0x18;
constexpr unsigned char kObjDeleted=4;
constexpr int kMaxCarriers=64,kNozzles=10;  // a vehicle's flames at most: a model's (exhaust::kMaxNozzles), the Sazabi's 10
static_assert(exhaust::kMaxNozzles<=kNozzles,"a model's nozzles all get a flame");
constexpr ULONGLONG kStaleMs=1000;
constexpr float kBurnerLength=1.6f;   // the afterburner (the player's boost): the flame this many times as long

const unsigned char kOpNewSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xEB,0x0F,0x48,0x8B,0xCB,0xE8,0x47};
const unsigned char kCtorSig[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
const unsigned char kRegisterSig[]={0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xFA,0x48,0x8B,0xD9};
const unsigned char kRegister2Sig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0xC1,0x48,0x8B,0x89,0xA8,0x0C,0x00,0x00,0x48,0x85};
const unsigned char kUpdateSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0x81,0xD8,0x03,0x00};
const unsigned char kDeleteSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48};
const unsigned char kVtableLeaSig[]={0x48,0x8D,0x05,0x00,0xB5,0x4D,0x01};

using OpNewFn=void*(*)(std::size_t);
using CtorFn=void*(*)(void*,void*);
using RegisterFn=void(*)(void*,void*);
using DeleteFn=void(*)(void*);
using CtrlFn=void(*)(void*);

struct Nozzle {
    alignas(16) float m[16];   // the matrix the booster copies each frame: never freed
    unsigned char* obj;
    void* ctrl;                // our weak reference
};
struct Carrier {               // a vehicle's flames (the name from when only the carriers had them)
    const unsigned char* v;
    const void* ctrl;
    ULONGLONG seen;
    Nozzle n[kNozzles];
    bool copyLogged;            // FLAME's once-per-jet check of when the booster takes the matrix (JetFrame)
};
Carrier carriers[kMaxCarriers];
bool sigOk=false,broken=false;

bool Live(const Nozzle& z) noexcept {
    return z.obj && z.ctrl && Readable(z.obj,kSize) && Readable(z.ctrl,0x10) && At<const void*>(z.obj,0)==image+kVtable &&
           At<long>(z.ctrl,8)>0 && !(z.obj[kObjFlags]&kObjDeleted);
}

void DropWeak(void* ctrl) noexcept {
    if(!ctrl)return;
    if(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(ctrl)+0xC),-1)==1)
        (*reinterpret_cast<CtrlFn* const*>(ctrl))[1](ctrl);
}

// A shared reference let go as MSVC's _Ref_count_base::_Decref does: the uses down by one, and only when that was
// the last use, the object destroyed and the uses' own weak count dropped. It dropped a weak count every time, one
// too many for every booster made: the control block was freed while the plugin still held its weak reference, and
// the next mission's ResetBoosters wrote into freed memory (2026-10-05 13:37, dump EDF6.exe.20900: DropWeak+0xE
// from ResetBoosters from MissionStart).
void DropShared(void* ctrl) noexcept {
    if(!ctrl)return;
    if(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(ctrl)+8),-1)==1) {
        (*reinterpret_cast<CtrlFn* const*>(ctrl))[0](ctrl);
        DropWeak(ctrl);
    }
}

void Drop(Nozzle& z) noexcept {
    if(Live(z))reinterpret_cast<DeleteFn>(image+kDelete)(z.obj);
    DropWeak(z.ctrl);
    z.obj=nullptr;z.ctrl=nullptr;
}

int MakeFault(const EXCEPTION_POINTERS* e) noexcept {
    const auto r=e->ExceptionRecord;
    Log("FLAME the game faulted on a booster (%08lX at EDF+%llX): flames are off until the game restarts",r->ExceptionCode,
        static_cast<unsigned long long>(static_cast<const unsigned char*>(r->ExceptionAddress)-image));
    broken=true;
    return EXCEPTION_EXECUTE_HANDLER;
}

// The stock caller's sequence (0x5E4CA0): new, construct, register, keep a weak reference.
void Make(Nozzle& z,const unsigned char* v,const float* size) noexcept {
    void* const mgr=At<void*>(image,kObjectMgr);
    if(!mgr)return;
    alignas(16) unsigned char p[0xB0]={};
    alignas(16) void* out[2]={};
    Put<const void*>(p,0x00,image+kParamVtable);
    Put<const void*>(p,0x08,z.m);
    Put<void*>(p,0x20,out);
    Put<const void*>(p,0x30,v);
    Put<const void*>(p,0x38,z.m);
    Put<float>(p,0x40,size[0]);
    Put<float>(p,0x44,size[1]);
    std::memcpy(p+0x50,image+kConstA,16);
    Put<int>(p,0x60,0);Put<float>(p,0x64,0.1f);Put<int>(p,0x68,1);Put<float>(p,0x6C,0.1f);
    std::memcpy(p+0x70,image+kConstB,16);
    std::memcpy(p+0x80,image+kConstC,16);
    Put<float>(p,0x94,5.0f);
    std::memcpy(p+0xA0,image+kConstD,16);
    unsigned char* const o=static_cast<unsigned char*>(reinterpret_cast<OpNewFn>(image+kOpNew)(kSize));
    if(!o)return;
    reinterpret_cast<CtorFn>(image+kCtor)(o,p);
    if(out[0]) {
        reinterpret_cast<RegisterFn>(image+kRegister)(mgr,out);
        reinterpret_cast<RegisterFn>(image+kRegister2)(mgr,out[0]);
    }
    if(At<const void*>(o,0)==image+kVtable && !(o[kObjFlags]&kObjDeleted)) {
        void* const ctrl=At<void*>(o,kSelfCtrl);
        if(ctrl) {
            _InterlockedIncrement(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(ctrl)+0xC));
            z.obj=o;z.ctrl=ctrl;
        }
    }
    DropShared(out[1]);
}

Carrier* Find(const unsigned char* v,ULONGLONG ms) noexcept {
    const void* const ctrl=At<const void*>(v,kSelfCtrl);
    Carrier* slot=nullptr;
    for(auto& c:carriers) {
        if(c.v==v && c.ctrl==ctrl)return &c;
        if(!c.v && !slot)slot=&c;
    }
    if(!slot)return nullptr;
    slot->v=v;slot->ctrl=ctrl;slot->seen=ms;slot->copyLogged=false;
    return slot;
}

void Sweep(ULONGLONG ms) noexcept {
    for(auto& c:carriers) {
        if(!c.v)continue;
        const bool gone=ms-c.seen>kStaleMs || !Readable(c.v,kTeam+4) || At<const void*>(c.v,kSelfCtrl)!=c.ctrl ||
                        (c.v[kObjFlags]&kObjDeleted);
        if(!gone)continue;
        for(auto& z:c.n)Drop(z);
        c.v=nullptr;c.ctrl=nullptr;
    }
}
}  // namespace

namespace {
// --- Each vehicle's nozzles this frame (ExhaustOf), the one place the flames (JetFrame) and the smoke (SmokeFrame) take
// them from: found in its model (exhaust::FindNozzles) once per bone array, their world matrices worked out once per game
// frame (exhaust::NozzleWorld): the parent's record carried to this frame's body (exhaust_pose.h: the records are a frame
// old; the flames burned 3.6..4.1 m behind their nozzles on arrival, 2026-10-06) and its local as it is now (`parentSeen`:
// the one it was posed with, seen here the frame before: the carrier's pods tilt in the input step, before this).
struct Exhaust {
    const unsigned char* v;
    const void* ctrl;
    ULONGLONG seen;
    const unsigned char* bones;      // the bone records the set was found in (a new array: found again)
    exhaust::NozzleSet set;
    exhaust::BodyTrack track;
    std::uint64_t frame;             // the game frame `world` is of
    std::uint64_t seenFrame;         // ...and `parentSeen`
    float parentSeen[exhaust::kMaxNozzles][16];
    float world[exhaust::kMaxNozzles][16];
};
Exhaust exhausts[kMaxCarriers];

// v's model's bone records (its instance's) and their count; false: none readable.
bool BonesOf(const unsigned char* v,const unsigned char** bones,int* count) noexcept {
    const unsigned char* const inst=v+kModelInst506;
    if(!Readable(inst,kInstBoneCount+4))return false;
    const auto b=At<const unsigned char*>(inst,kInstBones506);
    const auto n=At<std::int32_t>(inst,kInstBoneCount);
    if(!b || n<=0 || n>256 || !Readable(b,static_cast<std::size_t>(n)*kBoneStride))return false;
    *bones=b;*count=n;
    return true;
}

void Recompute(Exhaust& e,int count,std::uint64_t frame) noexcept {
    const bool posed=e.seenFrame && e.seenFrame+1==frame;
    for(int i=0;i<e.set.count;++i) {
        const int p=e.set.parent[i];
        if(p<0 || p>=count)continue;
        const unsigned char* const r=e.bones+static_cast<std::size_t>(p)*kBoneStride;
        const float* const now=reinterpret_cast<const float*>(r+kBoneLocal506);
        exhaust::NozzleWorld(e.set.local[i],now,posed ? e.parentSeen[i] : nullptr,reinterpret_cast<const float*>(r+kBoneWorld506),
                             e.track,e.world[i]);
        std::memcpy(e.parentSeen[i],now,64);
    }
    e.seenFrame=frame;
    e.frame=frame;
}

// v's nozzles this frame (nullptr: none in its model, or no slot free).
const Exhaust* ExhaustOf(const unsigned char* v,ULONGLONG ms) noexcept {
    const void* const ctrl=At<const void*>(v,kSelfCtrl);
    Exhaust* e=nullptr;
    Exhaust* slot=nullptr;
    for(auto& x:exhausts) {
        if(x.v==v && x.ctrl==ctrl){e=&x;break;}
        if(!x.v && !slot)slot=&x;
    }
    if(!e) {
        if(!slot)return nullptr;
        *slot=Exhaust{};slot->v=v;slot->ctrl=ctrl;
        e=slot;
    }
    e->seen=ms;
    const unsigned char* bones=nullptr;
    int count=0;
    if(!BonesOf(v,&bones,&count))return nullptr;
    if(bones!=e->bones) {   // a model (again): its nozzles found by their names once
        const exhaust::NozzleSet set=exhaust::FindNozzles(bones,count,[](const wchar_t* name){return name && Readable(name,32);});
        e->bones=bones;e->set=set;e->seenFrame=0;e->frame=0;
        if(Cfg().debug)Log("FLAME v=%p %d nozzles (%s)",v,set.count,
                           set.count ? (set.stock ? "the stock model's: nozzles_gen.h" : "its model's bones") : "none in its model");
    }
    if(!e->set.count)return nullptr;
    const std::uint64_t frame=GameFrame();
    exhaust::Observe(e->track,frame,reinterpret_cast<const float*>(v+kMatrix));
    if(e->frame!=frame)Recompute(*e,count,frame);
    return e;
}

void ExhaustSweep(ULONGLONG ms) noexcept {
    for(auto& e:exhausts)if(e.v && ms-e.seen>kStaleMs)e=Exhaust{};
}

// When the booster takes its matrix (Debug, once a jet moving faster than 30 m/s): its copy (+0x60) against the one
// written last frame, before this frame's is written. The same: it copies after the input step that writes it, and
// the flame is where the body is drawn; another: it copied before, and the flame trails one more frame (that much
// further back than the last frame's flight: logged with it).
void CopyCheck(Carrier& c,const Exhaust& e,const unsigned char* v) noexcept {
    if(!e.track.posedOk)return;
    const float* now=e.track.now+12;
    const float* was=e.track.posed+12;
    const float step=std::sqrt((now[0]-was[0])*(now[0]-was[0])+(now[1]-was[1])*(now[1]-was[1])+(now[2]-was[2])*(now[2]-was[2]));
    if(step<0.5f)return;   // m a frame: 30 m/s
    c.copyLogged=true;
    const float* got=reinterpret_cast<const float*>(c.n[0].obj+0x60)+12;
    const float* wrote=c.n[0].m+12;
    const float off=std::sqrt((got[0]-wrote[0])*(got[0]-wrote[0])+(got[1]-wrote[1])*(got[1]-wrote[1])+(got[2]-wrote[2])*(got[2]-wrote[2]));
    Log("FLAME v=%p the booster holds %s (%.2f m from the matrix written last frame; the body flew %.2f m since)",v,
        off<0.01f ? "the matrix written last frame: it copies after the input step" : "an older matrix: it copies before the input step",
        off,step);
}

// A vehicle's flames (the user, 2026-10-05: the jets have no flame): the Booster on each of its model's nozzles, `intensity`
// how strongly they burn, `burner` the afterburner (kBurnerLength times as long).
void JetFrame(const unsigned char* v,float intensity,bool burner,ULONGLONG ms) noexcept {
    const Exhaust* const e=ExhaustOf(v,ms);
    if(!e)return;
    Carrier* const c=Find(v,ms);
    if(!c)return;
    const bool fresh=!c->n[0].obj && !c->n[0].ctrl;   // no flame made for it yet
    c->seen=ms;
    if(Cfg().debug && !c->copyLogged && Live(c->n[0]))CopyCheck(*c,*e,v);
    if(fresh && Cfg().debug) {
        const float* p=reinterpret_cast<const float*>(v+kPosition);
        const float* w=e->world[0];
        Log("FLAME v=%p nozzle 0 at (%.2f,%.2f,%.2f) from the vehicle's origin, the flame along (%.2f,%.2f,%.2f), %.2f x %.2f m",v,
            w[12]-p[0],w[13]-p[1],w[14]-p[2],w[8],w[9],w[10],e->set.size[0][0],e->set.size[0][1]);
    }
    for(int i=0;i<e->set.count;++i) {
        Nozzle& z=c->n[i];
        std::memcpy(z.m,e->world[i],sizeof z.m);
        if(!Live(z)) {
            DropWeak(z.ctrl);
            z.obj=nullptr;z.ctrl=nullptr;
            const float size[2]={e->set.size[i][0]*(burner ? kBurnerLength : 1.0f),e->set.size[i][1]};
            Make(z,v,size);
            if(!Live(z))continue;
        }
        Put<float>(z.obj,kLevel,intensity);
        Put<float>(z.obj,kPulse,1.0f);
        Put<int>(z.obj,kHold,3);
    }
}
}  // namespace

namespace {
// The flares' fire (FlareFlames): a set of Booster flames on the jet that dropped them, one a burning flare, each
// at its flare and trailing against its motion; a set's flames past its flares burn down (no level).
constexpr int kFlareSets=4,kFlareFlames=8;
constexpr float kFlareSize[2]={4.0f,1.5f};
struct FlareSet { const unsigned char* v; const void* ctrl; ULONGLONG seen; Nozzle n[kFlareFlames]; };
FlareSet flareSets[kFlareSets];

FlareSet* FlareSetOf(const unsigned char* v,ULONGLONG ms) noexcept {
    const void* const ctrl=At<const void*>(v,kSelfCtrl);
    FlareSet* free=nullptr;
    for(auto& f:flareSets) {
        if(f.v==v && f.ctrl==ctrl)return &f;
        if(!f.v && !free)free=&f;
    }
    if(free){free->v=v;free->ctrl=ctrl;free->seen=ms;}
    return free;
}

void FlareFrame(const unsigned char* v,const float (*at)[3],const float (*vel)[3],int n,ULONGLONG ms) noexcept {
    FlareSet* const f=FlareSetOf(v,ms);
    if(!f)return;
    f->seen=ms;
    for(int i=0;i<kFlareFlames;++i) {
        Nozzle& z=f->n[i];
        if(i>=n) {   // no flare for it now: let it burn down
            if(Live(z)){Put<float>(z.obj,kLevel,0.0f);Put<float>(z.obj,kPulse,0.0f);}
            continue;
        }
        float back[3]={-vel[i][0],-vel[i][1],-vel[i][2]};   // the flame's +z: the trail behind the flare
        const float l=std::sqrt(back[0]*back[0]+back[1]*back[1]+back[2]*back[2]);
        if(l>1e-3f){for(auto& x:back)x/=l;}else{back[0]=0.0f;back[1]=1.0f;back[2]=0.0f;}
        float up[3]={0.0f,1.0f,0.0f};
        if(std::fabs(back[1])>0.9f){up[0]=1.0f;up[1]=0.0f;}
        float x[3]={up[1]*back[2]-up[2]*back[1],up[2]*back[0]-up[0]*back[2],up[0]*back[1]-up[1]*back[0]};
        const float xl=std::sqrt(x[0]*x[0]+x[1]*x[1]+x[2]*x[2]);
        for(auto& c:x)c/=xl;
        const float y[3]={back[1]*x[2]-back[2]*x[1],back[2]*x[0]-back[0]*x[2],back[0]*x[1]-back[1]*x[0]};
        for(int k=0;k<3;++k){z.m[k]=x[k];z.m[4+k]=y[k];z.m[8+k]=back[k];z.m[12+k]=at[i][k];}
        z.m[3]=z.m[7]=z.m[11]=0.0f;z.m[15]=1.0f;
        if(!Live(z)) {
            DropWeak(z.ctrl);
            z.obj=nullptr;z.ctrl=nullptr;
            Make(z,v,kFlareSize);
            if(!Live(z))continue;
        }
        Put<float>(z.obj,kLevel,1.0f);
        Put<float>(z.obj,kPulse,1.0f);
        Put<int>(z.obj,kHold,3);
    }
}

void FlareSweep(ULONGLONG ms) noexcept {
    for(auto& f:flareSets) {
        if(!f.v)continue;
        const bool gone=ms-f.seen>kStaleMs || !Readable(f.v,kTeam+4) || At<const void*>(f.v,kSelfCtrl)!=f.ctrl ||
                        (f.v[kObjFlags]&kObjDeleted);
        if(!gone)continue;
        for(auto& z:f.n)Drop(z);
        f.v=nullptr;f.ctrl=nullptr;
    }
}
}  // namespace

void FlareFlames(const unsigned char* v,const float (*at)[3],const float (*vel)[3],int n,ULONGLONG ms) noexcept {
    if(!sigOk || broken || !v)return;
    __try { FlareFrame(v,at,vel,n,ms); }
    __except(MakeFault(GetExceptionInformation())) {}
}

void JetFlames(const unsigned char* v,float intensity,bool burner,ULONGLONG ms) noexcept {
    if(!sigOk || broken || !v)return;
    __try { JetFrame(v,intensity,burner,ms); }
    __except(MakeFault(GetExceptionInformation())) {}
}

// Flames on nozzles the caller places in the world (the Sazabi's thrust bells and soles, sazabi_arms.inc Flames): each
// matrix's +z the way its flame leaves, rows unit; `size[i]` its length and width (m) when made; `level[i]` how strongly
// they burn this frame (0: they burn down, the boosters kept for the next burst). At most kNozzles a vehicle.
void NozzleFlames(const unsigned char* v,const float (*m)[16],int n,const float (*size)[2],const float* level,ULONGLONG ms) noexcept {
    if(!sigOk || broken || !v || !m || !size || !level)return;
    __try {
        Carrier* const c=Find(v,ms);
        if(!c)return;
        c->seen=ms;
        for(int i=0;i<n && i<kNozzles;++i) {
            Nozzle& z=c->n[i];
            std::memcpy(z.m,m[i],sizeof z.m);
            if(!Live(z)) {
                if(level[i]<=0.0f)continue;   // nothing to light: none made yet
                DropWeak(z.ctrl);
                z.obj=nullptr;z.ctrl=nullptr;
                Make(z,v,size[i]);
                if(!Live(z))continue;
            }
            Put<float>(z.obj,kLevel,level[i]);
            Put<float>(z.obj,kPulse,level[i]>0.0f ? 1.0f : 0.0f);
            Put<int>(z.obj,kHold,level[i]>0.0f ? 3 : 0);
        }
    }
    __except(MakeFault(GetExceptionInformation())) {}
}

void SmokeSweep(ULONGLONG ms) noexcept;   // below, with the arrival's smoke
void BoosterSweep(ULONGLONG ms) noexcept {
    if(!sigOk || broken)return;
    __try { Sweep(ms);FlareSweep(ms);SmokeSweep(ms);ExhaustSweep(ms); }
    __except(FaultLog("FLAME sweep",GetExceptionInformation())) {}
}

namespace {
// --- The arrival's smoke (the user, 2026-10-05: "飞机入场 感觉还能更帅点 要不要放烟 有人想要尾迹"; ini JetEntrySmoke):
// a called jet arriving (jet_internal.h Entering) trails smoke from each exhaust; the smoke stays in the air where it
// was laid and fades. The stock LineStripObject, the emitter of the missiles' smoke ribbon as an object of its own
// (static RE, EDF.dll 0x678CCB46; H unless noted):
//   factory 0x842E0(mgr, out[2], matrix16, InitParam*): new, construct (0x2F47F0), register; *out = a weak
//   reference {obj, ctrl} (0x84412: lock inc [ctrl+0xC]). InitParam: vtable 0x1762138 (InitParam@LineStripObject),
//   the emitter's parameters at +0x30 (0x2F4490 fills the defaults; laid out below), +0x150..+0x160 its warm-up
//   (count 0: none).
//   The object (vtable 0x17A8930): its emitter at +0x190 (+0x40 emitting, +0x50 where, +0x60 which way); its update
//   task (0x2F64F0) runs the emitter (0x2F6110) and deletes the object once it is off with no node left alive
//   (+0x2D0): stopped, the smoke laid fades over its life and the object goes by itself.
//   The texture: the missiles' smoke, L"噴射煙_01.dds" (0x17A1CC8), got from EffectGenUtil (0x20B2980) by 0x2E8510
//   (a name it cannot find is dereferenced as null: the name's bytes are checked), copied (0xF5E10), freed (0x1120820).
// Its look in a mission is unconfirmed (M): the factory's one stock caller is a debug scene. A jet's arrival makes a
// new object for each exhaust (one stopped and restarted would join its old tail to the new start).
constexpr unsigned kTrailMake=0x842E0,kTrailParams=0x2F4490,kTrailTex=0x2E8510,kTexCopy=0xF5E10,kTexFree=0x1120820;
constexpr unsigned kTrailUpdate=0x2F64F0,kTrailEmit=0x2F6110,kTrailVtable=0x17A8930,kTrailParamVtable=0x1762138,kSmokeName=0x17A1CC8;
constexpr std::size_t kEffectGen=0x20B2980,kTrailCore=0x190,kTrailParamAt=0x30,kTrailParamSize=0x180,kTrailSize=0x3D0;
// The emitter's parameters (core +0x00..+0x120): life min / max frames +0x04 / +0x08 (and +0x00, +0x0C as the
// missile's), per-frame drift +0x20, emitting +0x40, where +0x50, which way +0x60, the cone +0x84, speeds +0x88 / +0x8C,
// width mode +0x90 (0: grows by +0x9C a frame), width +0x94..+0x98, colour from +0xA0 to +0xB0 over its life, the
// smoke's flags +0xC0 / +0xC1 (as the missile's: 1, 1), the texture +0xD0, the strip's UV +0x10C / +0x114 (0.5, 0.3).
constexpr int kSmokeLife=150;                              // frames: 2.5 s, 500 m behind a jet at 200 m/s
constexpr float kSmokeWidth=2.0f,kSmokeGrow=0.04f,kSmokeRise=0.003f;   // m, m a frame, m/s^2-ish drift up a frame
alignas(16) const float kSmokeFrom[4]={0.92f,0.92f,0.95f,0.55f},kSmokeTo[4]={0.95f,0.95f,0.97f,0.0f};
constexpr int kSmokeSets=16,kSmokeTrails=exhaust::kMaxNozzles;   // a trail from every nozzle (the carrier's four too)
constexpr ULONGLONG kSmokeStaleMs=500;
const unsigned char kTrailMakeSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57};
const unsigned char kTrailParamsSig[]={0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0xC7,0x41,0x2C,0x00,0x00};
const unsigned char kTrailTexSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x40,0x33};
const unsigned char kTrailUpdateSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8B,0x49,0x08,0x48,0x81,0xC1,0x90,0x01,
                                       0x00,0x00,0x48,0x8B,0x53,0x10,0xE8,0x03,0xFC,0xFF,0xFF,0x48,0x8B,0x4B,0x08,0x48,0x83,0xB9,
                                       0xD0,0x02,0x00,0x00,0x00,0x75,0x13,0x80,0xB9,0xD0,0x01,0x00,0x00,0x00};
const unsigned char kTrailEmitSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x80,0x79,0x40,0x00};
const unsigned char kTexCopySig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57};
const unsigned char kTexFreeSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8D,0x05,0x00,0xE5,0x67,0x00};
const unsigned char kSmokeNameBytes[]={0x74,0x56,0x04,0x5C,0x59,0x71,0x5F,0x00,0x30,0x00,0x31,0x00};
bool smokeOk=false;

struct Trail { unsigned char* obj; void* ctrl; };
struct SmokeSet { const unsigned char* v; const void* ctrl; ULONGLONG seen; Trail t[kSmokeTrails]; };
SmokeSet smokeSets[kSmokeSets];

bool TrailLive(const Trail& t) noexcept {
    return t.obj && t.ctrl && Readable(t.obj,kTrailSize) && Readable(t.ctrl,0x10) && At<const void*>(t.obj,0)==image+kTrailVtable &&
           At<long>(t.ctrl,8)>0 && !(t.obj[kObjFlags]&kObjDeleted);
}
// Off: it lays no more; what it laid fades and the object deletes itself (kTrailUpdate). Our weak reference goes.
void TrailStop(Trail& t) noexcept {
    if(TrailLive(t))t.obj[kTrailCore+0x40]=0;
    DropWeak(t.ctrl);
    t=Trail{};
}
void TrailMake(Trail& t,const float* m) noexcept {
    void* const mgr=At<void*>(image,kObjectMgr);
    void* const gen=At<void*>(image,kEffectGen);
    if(!mgr || !gen)return;
    alignas(16) unsigned char p[kTrailParamSize]={};
    alignas(16) unsigned char tex[0x30]={};
    alignas(16) void* out[2]={};
    unsigned char* const e=p+kTrailParamAt;
    reinterpret_cast<void(*)(void*)>(image+kTrailParams)(e);
    Put<const void*>(p,0,image+kTrailParamVtable);
    reinterpret_cast<void*(*)(void*,void*,const wchar_t*)>(image+kTrailTex)(gen,tex,reinterpret_cast<const wchar_t*>(image+kSmokeName));
    reinterpret_cast<void(*)(void*,const void*)>(image+kTexCopy)(e+0xD0,tex);
    reinterpret_cast<void(*)(void*)>(image+kTexFree)(tex);
    Put<int>(e,0x00,kSmokeLife);Put<int>(e,0x04,kSmokeLife*9/10);Put<int>(e,0x08,kSmokeLife*11/10);Put<int>(e,0x0C,1);
    Put<float>(e,0x20,0.0f);Put<float>(e,0x24,kSmokeRise);Put<float>(e,0x28,0.0f);
    e[0x40]=1;
    const float at[4]={m[12],m[13],m[14],1.0f},back[4]={-m[8],-m[9],-m[10],0.0f};
    std::memcpy(e+0x50,at,16);std::memcpy(e+0x60,back,16);
    Put<float>(e,0x80,0.0f);Put<float>(e,0x84,0.2f);Put<float>(e,0x88,0.0f);Put<float>(e,0x8C,0.0f);   // laid, not blown
    Put<int>(e,0x90,0);Put<float>(e,0x94,kSmokeWidth*0.6f);Put<float>(e,0x98,kSmokeWidth);Put<float>(e,0x9C,kSmokeGrow);
    std::memcpy(e+0xA0,kSmokeFrom,16);std::memcpy(e+0xB0,kSmokeTo,16);
    e[0xC0]=1;e[0xC1]=1;Put<float>(e,0x10C,0.5f);Put<float>(e,0x114,0.3f);
    reinterpret_cast<void*(*)(void*,void**,const float*,void*)>(image+kTrailMake)(mgr,out,m,p);
    reinterpret_cast<void(*)(void*)>(image+kTexFree)(e+0xD0);   // the parameters' copy (the stock's 0x268EFC)
    t.obj=static_cast<unsigned char*>(out[0]);t.ctrl=out[1];
    if(!TrailLive(t))TrailStop(t);
}

SmokeSet* SmokeSetOf(const unsigned char* v,bool make,ULONGLONG ms) noexcept {
    const void* const ctrl=At<const void*>(v,kSelfCtrl);
    SmokeSet* slot=nullptr;
    for(auto& s:smokeSets) {
        if(s.v==v && s.ctrl==ctrl)return &s;
        if(!s.v && !slot)slot=&s;
    }
    if(!make || !slot)return nullptr;
    *slot=SmokeSet{v,ctrl,ms,{}};
    return slot;
}
void SmokeEnd(SmokeSet& s) noexcept {
    for(auto& t:s.t)TrailStop(t);
    s=SmokeSet{};
}

void SmokeFrame(const unsigned char* v,bool on,ULONGLONG ms) noexcept {
    SmokeSet* const s=SmokeSetOf(v,on,ms);
    if(!s)return;
    if(!on || v[kDead]){SmokeEnd(*s);return;}
    const Exhaust* const e=ExhaustOf(v,ms);   // the flames' nozzles, this frame's (JetFrame takes the same)
    if(!e){SmokeEnd(*s);return;}
    s->seen=ms;
    for(int i=0;i<e->set.count && i<kSmokeTrails;++i) {
        alignas(16) float m[16];   // the game's matrices are 16-aligned
        std::memcpy(m,e->world[i],sizeof m);
        Trail& t=s->t[i];
        if(!t.obj && !t.ctrl) {
            TrailMake(t,m);
            if(t.obj && Cfg().debug)Log("SMOKE v=%p exhaust %d: trail %p laid",v,i,t.obj);
            continue;
        }
        if(!TrailLive(t))continue;   // gone with the mission's objects: none again for this arrival
        const float at[4]={m[12],m[13],m[14],1.0f},back[4]={m[8],m[9],m[10],0.0f};   // m's z: the flame's, rearward
        std::memcpy(t.obj+kTrailCore+0x50,at,16);std::memcpy(t.obj+kTrailCore+0x60,back,16);
    }
}

int SmokeFault(const EXCEPTION_POINTERS* e) noexcept {
    const auto r=e->ExceptionRecord;
    Log("SMOKE the game faulted on an arrival's smoke (%08lX at EDF+%llX): the smoke is off until the game restarts",r->ExceptionCode,
        static_cast<unsigned long long>(static_cast<const unsigned char*>(r->ExceptionAddress)-image));
    smokeOk=false;
    return EXCEPTION_EXECUTE_HANDLER;
}
}  // namespace

void JetSmoke(const unsigned char* v,bool on,ULONGLONG ms) noexcept {
    if(!smokeOk || !v)return;
    __try { SmokeFrame(v,on && Cfg().jetEntrySmoke,ms); }
    __except(SmokeFault(GetExceptionInformation())) {}
}

// Once a frame (BoosterSweep): a jet not seen for kSmokeStaleMs (gone, reaped, flown by the player) has its smoke stopped.
void SmokeSweep(ULONGLONG ms) noexcept {
    if(!smokeOk)return;
    __try { for(auto& s:smokeSets)if(s.v && ms-s.seen>kSmokeStaleMs)SmokeEnd(s); }
    __except(SmokeFault(GetExceptionInformation())) {}
}

bool InstallBoosters() noexcept {
    __try {
        sigOk=Matches(kOpNew,kOpNewSig,sizeof(kOpNewSig)) && Matches(kCtor,kCtorSig,sizeof(kCtorSig)) &&
              Matches(kRegister,kRegisterSig,sizeof(kRegisterSig)) && Matches(kRegister2,kRegister2Sig,sizeof(kRegister2Sig)) &&
              Matches(kUpdate,kUpdateSig,sizeof(kUpdateSig)) && Matches(kDelete,kDeleteSig,sizeof(kDeleteSig)) &&
              Matches(kVtableLea,kVtableLeaSig,sizeof(kVtableLeaSig)) &&
              Readable(image+kVtable,(kUpdateSlot+1)*8) && At<const unsigned char*>(image,kVtable+kUpdateSlot*8)==image+kUpdate &&
              Readable(image+kParamVtable,8) && Readable(image+kConstA,16) && Readable(image+kConstB,16) &&
              Readable(image+kConstC,16) && Readable(image+kConstD,16);
        Log("HOOK carrier flames=%d%s",sigOk,sigOk ? "" : " (off: unexpected EDF.dll code)");
        smokeOk=sigOk && Matches(kTrailMake,kTrailMakeSig,sizeof(kTrailMakeSig)) && Matches(kTrailParams,kTrailParamsSig,sizeof(kTrailParamsSig)) &&
                Matches(kTrailTex,kTrailTexSig,sizeof(kTrailTexSig)) && Matches(kTrailUpdate,kTrailUpdateSig,sizeof(kTrailUpdateSig)) &&
                Matches(kTrailEmit,kTrailEmitSig,sizeof(kTrailEmitSig)) && Matches(kTexCopy,kTexCopySig,sizeof(kTexCopySig)) &&
                Matches(kTexFree,kTexFreeSig,sizeof(kTexFreeSig)) && Matches(kSmokeName,kSmokeNameBytes,sizeof(kSmokeNameBytes)) &&
                Readable(image+kTrailVtable,8) && Readable(image+kTrailParamVtable,8);
        Log("HOOK arrival smoke=%d%s",smokeOk,smokeOk ? "" : " (off: unexpected EDF.dll code)");
        return sigOk;
    } __except(FaultLog("FLAME install",GetExceptionInformation())){return false;}
}
// A new mission (mission.cpp MissionStart): the carriers and their boosters were the last mission's, gone with
// it: forgotten, not deleted. Each nozzle's weak reference is dropped: it kept the control block alive, so the
// block is there to drop it from (jet.cpp ResetJets), and keeping it would leak the block every mission.
void ResetBoosters() noexcept {
    for(auto& c:carriers) {
        for(auto& z:c.n)DropWeak(z.ctrl);
        c=Carrier{};
    }
    for(auto& f:flareSets) {
        for(auto& z:f.n)DropWeak(z.ctrl);
        f=FlareSet{};
    }
    for(auto& s:smokeSets) {   // gone with the mission's objects: only our weak references let go
        for(auto& t:s.t)DropWeak(t.ctrl);
        s=SmokeSet{};
    }
    for(auto& e:exhausts)e=Exhaust{};
}
}  // namespace crew
