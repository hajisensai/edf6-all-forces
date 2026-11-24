// Frames above the game's 59.94 Hz, stage 4 (PR-e, docs/unlimited-fps-re.md §4.3.3): every object's root between two
// ticks, as fps-7 does the player's (frame_rate_view.cpp). Installed with it (the frame gate on, ini FrameRateCap other
// than 0); a frame with ticks draws the step as it is and touches nothing here but the sample.
//
// Which objects. System ([image+0x20B2958], scene::ObjectManager) keeps the objects its tick updates in a list (+0x448
// pointers, +0x458 count): 0x1198DA0's object phase walks it each tick (0x11993D6 cleans it right before, 0x1195F20),
// and the camera step (0x11995A6, frame_rate_view.cpp's hook) comes after that phase. Once a tick (System +0xCE0, the
// tick count 0x1198FD0 adds one to), the hook samples every listed object whose family thermal_models.cpp knows (soldiers
// and NPCs of any machine, vehicles empty or not, insects, humanoids, monsters, UFOs and the other families there):
// pose[0] of its main model's skeleton instance (model+0xA0, the world root). A listed pointer can outlive its object
// until the next clean (0x1195F20 drops expired entries only then), so each sampled object is held by a weak count on its
// own control block (weak-this +0x28 / +0x30; the block's use count +8, weak count +0xC): the record says whether the
// object is still there without reading freed memory, and is forgotten (the weak count given back) the first tick the
// object is not listed. Identity: the control block, the vtable, the instance; any change starts its samples again.
//
// What a frame without ticks draws. Each object whose drawn root is still the last tick's (nothing wrote it since) moves
// to D = cur^-1 * InterpolateScaled(prev, cur, alpha) (a uniform scale is blended, a squash, a mirror, a teleport over
// kCutMove metres or a turn over kCutTurn radians in one tick are drawn as they are): D times every bone of every
// instance of every model the object draws with (bone poses stay the tick's: the skin moves as one) and, for a soldier,
// of their weapons (+0x1950, models +0xF30). A vehicle's riders (every seat's, the vehicle +0x608: other NPC crew, the
// auto turret plugin's gunners, remote players) go with the vehicle's D, never their own: rigid passengers (a fighter
// moves metres a tick, a rider blended apart from it would drift off the seat). The player's soldier and vehicle are
// fps-7's and left to it; the other riders of the player's vehicle take fps-7's D. What was written goes back after the
// frame's end fence (frame_rate_view.cpp RestoreFrame) where the object is still there, the model and instance are still
// its, and the pose still holds the sum of what was written.
//
// The tables grow on frames with ticks only (the sample); a frame without ticks that does not fit leaves the rest as
// stepped and asks for more at the next tick. Online: only what is drawn changes, on frames that send nothing.
#include "frame_rate_objects.h"
#include "fp_view.h"
#include "thermal_models.h"
#include "scene_object.h"
#include "crew.h"
#include "memory.h"
#include "edf/seat.h"
#include "edf/hash_index.h"
#include <intrin.h>
#include <cstring>
#include <vector>

namespace crew {
namespace {
using framerate::Refusal;
// System (the object manager): its update list (pointers, count) and its tick count. An object's flags (+0x18 bit 4:
// being deleted, 0x1195F20 deletes it at its next clean).
constexpr std::size_t kUpdateList=0x448,kUpdateCount=0x458,kTickCount=0xCE0,kObjFlags=0x18,kCtrlWeaks=0xC;
constexpr unsigned char kObjDeleted=4;
constexpr std::uint64_t kMostObjects=1u<<16;
// Models, skeleton instances, poses (frame_rate_view.cpp, firstperson.cpp have the same layout).
constexpr std::size_t kWeapons=0x1950,kWeaponCount=0x1960,kWeaponModel=0xF30,kModelInst=0xA0,kBaseLod=0x160,kLods=0x250,
                      kLodCount=0x260,kLodStride=0xF0,kLodInst=0x58,kInstCount=0x20,kInstPose=0x30;
constexpr int kMostInsts=4,kMostWeapons=16,kMostModels=32;
// The soldier classes (frame_rate_view.cpp's): AssultSoldier, PaleWing, HeavyArmor, Engineer, the player's and NPCs'.
// Their weapons are [h+0x1950]; another human's +0x1950 is not read.
constexpr unsigned kSoldierVts[]={0x17CDF28,0x17D0FF8,0x17CF5B8,0x17CF100};
constexpr std::uint64_t kMostBones=256;
constexpr float kCutMove=10.0f,kCutTurn=1.0f;   // in one tick: 600 m/s, 57 degrees (fps-7's)
// The tables' first sizes (they grow on frames with ticks): poses written a frame, their floats (4 MB), objects.
constexpr std::size_t kFirstPoses=4096,kFirstPoseFloats=1u<<20,kFirstBodies=1024;
constexpr int kRefusals=static_cast<int>(Refusal::kCount);

// An object's root as the last two ticks left it (see the top). tick: the last sample's; samples: 2 when prev is the
// tick before. This frame (without ticks): fresh (still there, the same, its drawn root what the tick left), moving
// with d, still (nothing moved).
struct Body {
    const unsigned char* object=nullptr;unsigned char* ctrl=nullptr;const void* vtable=nullptr;
    const unsigned char* inst=nullptr;
    thermal::Family family=thermal::Family::kNone;
    std::uint32_t tick=0;
    int samples=0;
    bool fresh=false,moving=false,still=false;
    float prev[16]{},cur[16]{};
    fpview::M4 d;
};
std::vector<Body> bodies;
edf::PtrIndex bodyIndex;    // object -> bodies index
edf::PtrIndex riders;       // this frame: a seated human -> 1 (posed or left with their vehicle)
std::uint32_t sampledTick=0;
bool sampledAny=false,on=false;

// Who a written pose is drawn for, checked again before it goes back: the body (index and object: bodies change at a
// sample only, never between the write and the restore), a rider of its seat `seat` (the human and their control
// block), the model's holder (the body's object or the rider) and its weapon slot (-1: one of its own models).
struct Who {
    int body=-1;const unsigned char* owner=nullptr;
    const unsigned char* rider=nullptr;const unsigned char* riderCtrl=nullptr;unsigned seat=0;
};
struct Posed {
    Who who;const unsigned char* holder;int weapon;const unsigned char* model;
    const unsigned char* inst;float* pose;std::uint64_t count;std::size_t at;std::uint64_t sum;
};
std::vector<Posed> posed;
std::vector<float> posedBefore;
edf::PtrIndex posedIndex;                     // pose array -> 1 (a pose is written once a frame)
std::size_t posedWant=0,posedFloatsWant=0;    // what a frame wanted and did not have: grown at the next sample

frameobjects::Counts counts;

// System's update list, false when there is none to read.
bool UpdateList(const unsigned char*& sys,const unsigned char* const*& list,std::uint64_t& count) noexcept {
    sys=static_cast<const unsigned char*>(scene::ObjectManager());
    if(!sys || !Readable(sys,kTickCount+4))return false;
    list=At<const unsigned char* const*>(sys,kUpdateList);
    count=At<std::uint64_t>(sys,kUpdateCount);
    return count<=kMostObjects && (!count || (list && Readable(list,static_cast<std::size_t>(count)*8)));
}

// A skeleton instance's pose array when it can be read and written; the bone count in *count.
float* PoseOf(const unsigned char* inst,std::uint64_t* count) noexcept {
    if(!inst || !Readable(inst,kInstPose+8))return nullptr;
    const auto n=At<std::uint64_t>(inst,kInstCount);
    const auto pose=At<float*>(inst,kInstPose);
    if(!n || n>kMostBones || !pose || !Readable(pose,static_cast<std::size_t>(n)*64,true))return nullptr;
    *count=n;
    return pose;
}
// The skeleton instances a model draws with (its LODs'), each once (frame_rate_view.cpp InstancesOf).
int InstancesOf(const unsigned char* model,const unsigned char** out) noexcept {
    int n=0;
    auto add=[&](const unsigned char* inst){
        if(!inst || n>=kMostInsts)return;
        for(int i=0;i<n;++i)if(out[i]==inst)return;
        out[n++]=inst;
    };
    if(!Readable(model,kLodCount+8))return 0;
    add(model+kModelInst);
    add(At<const unsigned char*>(model,kBaseLod+kLodInst));
    const auto lods=At<const unsigned char*>(model,kLods);
    const auto count=At<std::uint64_t>(model,kLodCount);
    if(lods && count<=8 && Readable(lods,static_cast<std::size_t>(count)*kLodStride))
        for(std::uint64_t i=0;i<count;++i)add(At<const unsigned char*>(lods+i*kLodStride,kLodInst));
    return n;
}

// A weak count on the object's own control block (the block: vtable {destroy, delete}, use +8, weak +0xC;
// edf41classes.cpp WeakSet takes the same). Null when the object is not whole (being freed).
unsigned char* TakeWeak(const unsigned char* obj) noexcept {
    if(At<const unsigned char*>(obj,kSelf)!=obj)return nullptr;
    const auto ctrl=At<unsigned char*>(obj,kSelfCtrl);
    if(!ctrl || !Readable(ctrl,16,true) || At<std::int32_t>(ctrl,edf::kCtrlUses)<=0)return nullptr;
    _InterlockedIncrement(reinterpret_cast<volatile long*>(ctrl+kCtrlWeaks));
    return ctrl;
}
void DropWeak(unsigned char* ctrl) noexcept {
    if(!ctrl)return;
    if(_InterlockedDecrement(reinterpret_cast<volatile long*>(ctrl+kCtrlWeaks))==0) {   // the last reference: free the block
        const auto vt=At<void* const*>(ctrl,0);
        if(vt && Readable(vt,16))reinterpret_cast<void(__fastcall*)(void*)>(vt[1])(ctrl);
    }
}
// The body's object still there (its use count; the weak count keeps the block), the same, not being deleted.
bool Alive(const Body& b) noexcept {
    return b.ctrl && At<std::int32_t>(b.ctrl,edf::kCtrlUses)>0 && At<const unsigned char*>(b.object,kSelfCtrl)==b.ctrl &&
           !(At<unsigned char>(b.object,kObjFlags)&kObjDeleted);
}

// The human in a vehicle's seat while it is there (the seat's weak_ptr alive, the human's own block the same).
const unsigned char* SeatHuman(unsigned char* vehicle,unsigned seat,const unsigned char** ctrlOut) noexcept {
    const unsigned char* const s=edf::SeatAt(vehicle,seat);
    if(!Readable(s,kSeatRiderCtrl+8))return nullptr;
    const auto rider=At<const unsigned char*>(s,kSeatRider);
    const auto ctrl=At<const unsigned char*>(s,kSeatRiderCtrl);
    if(!rider || !ctrl || !Readable(ctrl,12) || At<std::int32_t>(ctrl,edf::kCtrlUses)<=0 || !Readable(rider,kWeaponCount+8) ||
       At<const unsigned char*>(rider,kSelfCtrl)!=ctrl)return nullptr;
    *ctrlOut=ctrl;
    return rider;
}

// Whether a written pose's object, model and instance are still the ones it was written for.
bool Holds(const Posed& p) noexcept {
    const auto b=static_cast<std::size_t>(p.who.body);
    if(b>=bodies.size() || bodies[b].object!=p.who.owner || !Alive(bodies[b]))return false;
    if(p.who.rider) {
        auto* const vehicle=const_cast<unsigned char*>(p.who.owner);
        const unsigned char* ctrl=nullptr;
        if(p.who.seat>=edf::SeatCount(vehicle) || SeatHuman(vehicle,p.who.seat,&ctrl)!=p.who.rider || ctrl!=p.who.riderCtrl)
            return false;
    }
    if(p.weapon>=0) {
        const auto list=At<const unsigned char* const*>(p.holder,kWeapons);
        const auto weapons=At<std::int32_t>(p.holder,kWeaponCount);
        if(!list || p.weapon>=weapons || weapons>kMostWeapons || !Readable(list,static_cast<std::size_t>(weapons)*8) ||
           !list[p.weapon] || list[p.weapon]+kWeaponModel!=p.model)return false;
    } else {
        const void* models[kMostModels];
        const std::size_t m=thermal::EnemyModels(p.holder,models,kMostModels);
        bool found=false;
        for(std::size_t k=0;k<m;++k)found=found || models[k]==p.model;
        if(!found)return false;
    }
    const unsigned char* insts[kMostInsts];
    const int n=InstancesOf(p.model,insts);
    bool found=false;
    for(int i=0;i<n;++i)found=found || insts[i]==p.inst;
    std::uint64_t bones=0;
    return found && PoseOf(p.inst,&bones)==p.pose && bones==p.count;
}

void RestoreAll() noexcept {
    for(std::size_t i=posed.size();i-->0;) {
        const Posed& p=posed[i];
        __try {
            if(Holds(p) && framerate::PoseSum(p.pose,p.count)==p.sum)
                std::memcpy(p.pose,&posedBefore[p.at],static_cast<std::size_t>(p.count)*64);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    posed.clear();posedBefore.clear();posedIndex.Clear();
}

// Every instance of `model` (held by `holder`, its weapon slot or -1) times d. False when one did not fit the frame's
// tables (left as stepped; they grow at the next sample).
bool PoseModel(const Who& who,const unsigned char* holder,int weapon,const unsigned char* model,const float* d) noexcept {
    const unsigned char* insts[kMostInsts];
    const int count=InstancesOf(model,insts);
    bool fit=true;
    for(int i=0;i<count;++i) {
        std::uint64_t n=0;
        float* const pose=PoseOf(insts[i],&n);
        if(!pose || posedIndex.Find(pose)>=0)continue;
        const std::size_t floats=static_cast<std::size_t>(n)*16;
        if(posed.size()>=posed.capacity() || posedBefore.size()+floats>posedBefore.capacity() ||
           (posedIndex.Size()+1)*2>posedIndex.Capacity()) {
            if(posed.size()+1>posedWant)posedWant=posed.size()+1;
            if(posedBefore.size()+floats>posedFloatsWant)posedFloatsWant=posedBefore.size()+floats;
            fit=false;
            continue;
        }
        const std::size_t at=posedBefore.size();
        posedBefore.insert(posedBefore.end(),pose,pose+floats);
        framerate::MulBones(pose,n,d);
        posedIndex.Set(pose,1);
        posed.push_back(Posed{who,holder,weapon,model,insts[i],pose,n,at,framerate::PoseSum(pose,n)});
    }
    return fit;
}
bool IsSoldier(const unsigned char* h) noexcept {
    if(!h || !Readable(h,kWeaponCount+8))return false;
    const auto vt=At<const unsigned char*>(h,0);
    for(unsigned v:kSoldierVts)if(vt==image+v)return true;
    return false;
}
// Everything `holder` draws times d: its models and, for a soldier, their weapons ([h+0x1950], models +0xF30).
bool PoseObject(const Who& who,const unsigned char* holder,thermal::Family family,const float* d) noexcept {
    const void* models[kMostModels];
    const std::size_t m=thermal::EnemyModels(holder,models,kMostModels);
    bool fit=true;
    for(std::size_t k=0;k<m;++k)fit=PoseModel(who,holder,-1,static_cast<const unsigned char*>(models[k]),d) && fit;
    if(family!=thermal::Family::kHuman || !IsSoldier(holder))return fit;
    const auto list=At<const unsigned char* const*>(holder,kWeapons);
    const auto weapons=At<std::int32_t>(holder,kWeaponCount);
    if(list && weapons>0 && weapons<=kMostWeapons && Readable(list,static_cast<std::size_t>(weapons)*8))
        for(int i=0;i<weapons;++i)
            if(list[i] && Readable(list[i],kWeaponModel+kLodCount+8))
                fit=PoseModel(who,holder,i,list[i]+kWeaponModel,d) && fit;
    return fit;
}

void Forget(std::size_t i) noexcept {
    DropWeak(bodies[i].ctrl);
    bodyIndex.Erase(bodies[i].object);
    if(i+1!=bodies.size()) {
        bodies[i]=bodies.back();
        bodyIndex.Set(bodies[i].object,static_cast<int>(i));
    }
    bodies.pop_back();
}

// One listed object's sample (false: nothing of it this tick).
bool SampleOne(const unsigned char* obj,std::uint32_t tick,bool next) noexcept {
    if(!obj || !Readable(obj,kSelfCtrl+8) || (At<unsigned char>(obj,kObjFlags)&kObjDeleted))return false;
    const auto model=static_cast<const unsigned char*>(thermal::EnemyModel(obj));
    if(!model)return false;
    const unsigned char* const inst=model+kModelInst;
    std::uint64_t n=0;
    const float* const pose=PoseOf(inst,&n);
    if(!pose)return false;
    const void* const vt=At<const void*>(obj,0);
    int at=bodyIndex.Find(obj);
    if(at>=0) {
        const Body& b=bodies[static_cast<std::size_t>(at)];
        if(b.tick==tick && b.samples)return true;   // listed twice
        if(At<const unsigned char*>(obj,kSelfCtrl)!=b.ctrl) {   // another object at the address
            Forget(static_cast<std::size_t>(at));
            at=-1;
        }
    }
    if(at<0) {
        if(bodies.size()>=bodies.capacity())return false;   // grown before the walk; never past it mid-walk
        unsigned char* const ctrl=TakeWeak(obj);
        if(!ctrl)return false;
        at=static_cast<int>(bodies.size());
        bodies.push_back(Body{});
        bodies.back().object=obj;
        bodies.back().ctrl=ctrl;
        bodyIndex.Set(obj,at);
    }
    Body& b=bodies[static_cast<std::size_t>(at)];
    if(!next || b.tick!=sampledTick || b.vtable!=vt || b.inst!=inst)b.samples=0;
    if(!b.samples)b.family=thermal::EnemyFamily(obj);
    b.vtable=vt;b.inst=inst;b.tick=tick;
    std::memcpy(b.prev,b.cur,sizeof(b.prev));
    std::memcpy(b.cur,pose,sizeof(b.cur));
    if(b.samples<2)++b.samples;
    return true;
}

void SampleAll() noexcept {
    const unsigned char* sys=nullptr;const unsigned char* const* list=nullptr;std::uint64_t count=0;
    if(!UpdateList(sys,list,count))return;
    const auto tick=At<std::uint32_t>(sys,kTickCount);
    if(sampledAny && tick==sampledTick)return;
    const bool next=sampledAny && tick==sampledTick+1;
    // A frame with ticks: the tables grow here, never on a frame without.
    const auto want=static_cast<std::size_t>(count);
    if(bodies.capacity()<want)bodies.reserve(want+want/4+64);
    bodyIndex.Reserve(bodies.capacity());
    riders.Reserve(bodies.capacity());
    if(posedWant>posed.capacity())posed.reserve(posedWant+posedWant/4);
    if(posedFloatsWant>posedBefore.capacity())posedBefore.reserve(posedFloatsWant+posedFloatsWant/4);
    posedIndex.Reserve(posed.capacity());
    for(std::uint64_t i=0;i<count;++i) {
        __try {
            SampleOne(list[i],tick,next);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    // Objects not sampled this tick (gone from the list, deleted, no model) are forgotten.
    for(std::size_t i=0;i<bodies.size();) {
        if(bodies[i].tick==tick && bodies[i].samples)++i;
        else Forget(i);
    }
    sampledTick=tick;
    sampledAny=true;
    counts.objects=static_cast<unsigned>(bodies.size());
}

// A body's own state this frame (see Body).
void OwnState(Body& b,float alpha) noexcept {
    b.fresh=b.moving=b.still=false;
    if(!Alive(b) || At<const void*>(b.object,0)!=b.vtable)return;
    std::uint64_t n=0;
    const float* const live=PoseOf(b.inst,&n);
    if(!live || std::memcmp(live,b.cur,sizeof(b.cur)))return;
    b.fresh=true;
    if(b.samples<2)return;
    if(!std::memcmp(b.prev,b.cur,sizeof(b.cur))) {
        b.still=true;
        return;
    }
    float shown[16];
    fpview::M4 cur,inverse,to;
    std::memcpy(cur.m,b.cur,64);
    if(!framerate::InterpolateScaled(b.prev,b.cur,alpha,shown,kCutMove,kCutTurn)) {
        ++counts.refused[static_cast<int>(framerate::ScaledRefusal(b.prev,b.cur,alpha,kCutMove,kCutTurn))];
        return;
    }
    if(!fpview::InverseAffine(cur,&inverse)) {
        ++counts.refused[static_cast<int>(Refusal::kNotRigid)];
        return;
    }
    std::memcpy(to.m,shown,64);
    b.d=fpview::Mul(inverse,to);
    b.moving=true;
}

// A vehicle's seats: each human there goes with the vehicle's d (null: the vehicle is left or still, and so are they).
void PoseRiders(int body,const void* rootHuman,const float* d,bool still) noexcept {
    auto* const vehicle=const_cast<unsigned char*>(bodies[static_cast<std::size_t>(body)].object);
    const unsigned seats=edf::SeatCount(vehicle);
    for(unsigned s=0;s<seats;++s) {
        __try {
            const unsigned char* ctrl=nullptr;
            const unsigned char* const h=SeatHuman(vehicle,s,&ctrl);
            if(!h || h==rootHuman || riders.Find(h)>=0 || thermal::EnemyFamily(h)!=thermal::Family::kHuman)continue;
            if((riders.Size()+1)*2>riders.Capacity())continue;   // past this frame's table: posed as its own body
            riders.Set(h,1);
            if(!d) {
                if(still)++counts.still;
                else ++counts.left;
                continue;
            }
            Who who;
            who.body=body;who.owner=vehicle;who.rider=h;who.riderCtrl=ctrl;who.seat=s;
            if(PoseObject(who,h,thermal::Family::kHuman,d)) {
                ++counts.posed;
                ++counts.riders;
            } else ++counts.left;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            ++counts.left;
        }
    }
}

void PoseAll(float alpha,const void* rootHuman,const void* rootVehicle,const float* rootD) noexcept {
    if(!sampledAny || bodies.empty())return;
    const unsigned char* sys=nullptr;const unsigned char* const* list=nullptr;std::uint64_t count=0;
    if(!UpdateList(sys,list,count) || At<std::uint32_t>(sys,kTickCount)!=sampledTick) {   // a tick without its sample
        counts.left+=static_cast<unsigned>(bodies.size());
        return;
    }
    for(Body& b:bodies) {
        __try {
            OwnState(b,alpha);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            b.fresh=b.moving=b.still=false;
        }
    }
    // The riders first: every vehicle's seats (the player's with fps-7's D) claim theirs.
    riders.Clear();
    for(std::size_t i=0;i<bodies.size();++i) {
        const Body& b=bodies[i];
        if(b.family!=thermal::Family::kVehicle || !Alive(b))continue;
        const float* d=nullptr;
        bool still=false;
        if(b.object==rootVehicle)d=rootD;
        else if(b.fresh && b.moving)d=b.d.m;
        else still=b.still;
        PoseRiders(static_cast<int>(i),rootHuman,d,still);
    }
    for(std::size_t i=0;i<bodies.size();++i) {
        const Body& b=bodies[i];
        if(b.object==rootHuman || b.object==rootVehicle || riders.Find(b.object)>=0)continue;   // fps-7's, a rider
        if(!b.fresh || (!b.moving && !b.still)) {
            ++counts.left;
            continue;
        }
        if(b.still) {
            ++counts.still;
            continue;
        }
        __try {
            Who who;
            who.body=static_cast<int>(i);who.owner=b.object;
            if(PoseObject(who,b.object,b.family,b.d.m))++counts.posed;
            else ++counts.left;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            ++counts.left;
        }
    }
    if(posed.size()>counts.mostPoses)counts.mostPoses=static_cast<unsigned>(posed.size());
}
}  // namespace

namespace frameobjects {
void Install() noexcept {
    if(on)return;
    bodies.reserve(kFirstBodies);
    bodyIndex.Reserve(kFirstBodies);
    riders.Reserve(kFirstBodies);
    posed.reserve(kFirstPoses);
    posedBefore.reserve(kFirstPoseFloats);
    posedIndex.Reserve(kFirstPoses);
    on=true;
}

void Sample() noexcept {
    if(on)SampleAll();
}

void Pose(float alpha,const void* rootHuman,const void* rootVehicle,const float* rootD) noexcept {
    if(on && alpha>=0.0f && alpha<1.0f)PoseAll(alpha,rootHuman,rootVehicle,rootD);
}

void Restore() noexcept {
    if(on && !posed.empty())RestoreAll();
}

void NoteRootRefusal(Refusal why) noexcept {
    const int i=static_cast<int>(why);
    if(on && i>=0 && i<kRefusals)++counts.rootRefused[i];
}

void TakeCounts(Counts& out) noexcept {
    out=counts;
    const unsigned objects=counts.objects;
    counts=frameobjects::Counts{};
    counts.objects=objects;
}
}  // namespace frameobjects
}  // namespace crew
