// Frames above the game's 59.94 Hz, stage 4 (PR-e, docs/unlimited-fps-re.md §4.3.3): every object's root between two
// ticks, as fps-7 does the player's (frame_rate_view.cpp). Installed with it (the frame gate on, ini FrameRateCap other
// than 0); a frame with ticks draws the step as it is and touches nothing here but the sample.
//
// Which objects. System ([image+0x20B2958], scene::ObjectManager) keeps the objects its tick updates in a list (+0x448
// pointers, +0x458 count): 0x1198DA0's object phase walks it each tick (0x11993D6 cleans it right before, 0x1195F20).
// Once a tick (System +0xCE0, the tick count 0x1198FD0 adds one to: the first render recording that sees it changed,
// see below), the sample takes every listed object whose family thermal_models.cpp knows (soldiers
// and NPCs of any machine, vehicles empty or not, insects, humanoids, monsters, UFOs and the other families there):
// pose[0] of its main model's skeleton instance (model+0xA0, the world root). A listed pointer can outlive its object
// until the next clean (0x1195F20 drops expired entries only then), so each sampled object is held by a weak count on its
// own control block (weak-this +0x28 / +0x30; the block's use count +8, weak count +0xC): the record says whether the
// object is still there without reading freed memory, and is forgotten (the weak count given back) the first tick the
// object is not listed, and all of them when the mission's scene is released (SceneOver). Identity: the control block,
// the vtable, the instance; any change starts its samples again.
//
// Where: at the render recording (render_record.cpp, 0x1198FA4), before fps-7 writes the player's root, the first
// recording whose tick count is not the last sample's. The recording runs once a frame before the frame's tick loop, so
// the first one after a tick sees the roots as that tick left them and as they are drawn: after the tick's every job.
// It was the camera step (0x11995A6, once a viewport a tick) before: after it the tick still runs 0x11BEBE0 (Havok
// queries' callbacks), the +0x358 job table (each element's vtbl[1] through 0x118FC00), 0x1193D50 when not paused
// (every [sys+0xCC0] entry's slot 0x28, the +0x370 jobs) and +0x358 again, vtable calls chosen at run time; in M001 at
// FrameRateCap=1 every object drawn as stepped was one of those writes (Left::kPose, 4500-5700 every 5 s against 1800
// blended, #395). A frame with two ticks samples the second only (the pair is then not consecutive: samples start
// again). A root written between the sample and the drawing makes the drawn root differ from cur: that object is drawn
// as stepped (counted, Left::kPose), never blended from a stale pair. After the move, M001 at FrameRateCap=1 still draws
// about 1000-1300 a second as stepped for `pose`: every one a Ranger (vtable EDF+0x17CDF28, AssultSoldier), the player
// and allies, on a frame without ticks after the one that sampled (never in the sampling record call). Their root is
// written again between frames without a tick: not found yet (docs/unlimited-fps-re.md 4.3.3).
//
// What a frame without ticks draws (at the render recording, frame_rate_view.cpp RecordStart). Each object whose drawn
// root is still the last tick's (nothing wrote it since) moves to D = cur^-1 * InterpolateScaled(prev, cur, alpha) (a
// uniform scale is blended, a squash, a mirror, a teleport over kCutMove metres or a turn over kCutTurn radians in one
// tick are drawn as they are): D times every bone of every instance of every model the object draws with (bone poses
// stay the tick's: the skin moves as one) and, for a soldier, of their weapons (+0x1950, models +0xF30), for a vehicle,
// of its trigger weapons' models (+0x638 holders, each live by its control block +8, the weapon +0x10, its model +0xF30
// when that is a registered AnimationModel). A vehicle's riders (every seat's, the vehicle +0x608: other NPC crew, the
// auto turret plugin's gunners, remote players) go with the vehicle's D, never their own: rigid passengers (a fighter
// moves metres a tick, a rider blended apart from it would drift off the seat). The player's soldier and vehicle are
// fps-7's and left to it; the other riders and the trigger weapons (+0x638) of the player's vehicle take fps-7's D (it
// poses the vehicle's own model only). A child object (a parent at +0x38) that
// is listed is blended with its own D like any other object, not its parent's; one that is not listed is drawn as
// stepped. What was written goes back after the frame's end fence (frame_rate_view.cpp RestoreFrame) where the object is
// still there, the model and instance are still its, bone by bone where the bone still holds what was written.
//
// Reads. The sample and the poses read objects the game holds live (listed at the sample; a body whose own control
// block's use count says it is there; a seat's or a holder's weak_ptr alive) without asking Readable, under __try: at
// stress:30 Readable was all of the sample's 4.74 ms a step. Readable is asked for System and its list only.
//
// The tables grow on frames with ticks only (the sample, never throwing: a failed growth skips that tick's sample); a
// frame without ticks that does not fit leaves the rest as stepped and asks for more at the next tick. Online: only what
// is drawn changes, on frames that send nothing.
#include "frame_rate_objects.h"
#include "fp_view.h"
#include "thermal_models.h"
#include "scene_object.h"
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "edf/seat.h"
#include "edf/hash_index.h"
#include <intrin.h>
#include <cstring>
#include <new>
#include <vector>

namespace crew {
namespace {
using framerate::Refusal;
using frameobjects::Left;
// System (the object manager): its update list (pointers, count) and its tick count. An object's flags (+0x18 bit 4:
// being deleted, 0x1195F20 deletes it at its next clean).
constexpr std::size_t kUpdateList=0x448,kUpdateCount=0x458,kTickCount=0xCE0,kObjFlags=0x18,kCtrlWeaks=0xC;
constexpr unsigned char kObjDeleted=4;
constexpr std::uint64_t kMostObjects=1u<<16;
// Models, skeleton instances, poses (frame_rate_view.cpp, firstperson.cpp have the same layout).
constexpr std::size_t kWeapons=0x1950,kWeaponCount=0x1960,kWeaponModel=0xF30,kModelInst=0xA0,kBaseLod=0x160,kLods=0x250,
                      kLodCount=0x260,kLodStride=0xF0,kLodInst=0x58,kInstCount=0x20,kInstPose=0x30;
constexpr int kMostInsts=4,kMostWeapons=16,kMostModels=32;
constexpr std::uint64_t kMostHolders=64;   // seat_net.h's bound on a vehicle's weapon holders
// The soldier classes (frame_rate_view.cpp's): AssultSoldier, PaleWing, HeavyArmor, Engineer, the player's and NPCs'.
// Their weapons are [h+0x1950]; another human's +0x1950 is not read.
constexpr unsigned kSoldierVts[]={0x17CDF28,0x17D0FF8,0x17CF5B8,0x17CF100};
constexpr std::uint64_t kMostBones=256;
// In one tick: 10 m (600 m/s), 1 rad (57 degrees). Not a game value: fps-8 chose them for the player's root
// (frame_rate_view.cpp kCutMove / kCutTurn) as "a teleport, a respawn, a cut", and they are reused here so an object and
// the player's root agree. A real move past them is drawn as stepped and counted (refused, move / turn): the FRAMERATE
// line says whether they cut real motion (docs/unlimited-fps-re.md §4.3.3).
constexpr float kCutMove=10.0f,kCutTurn=1.0f;
// The tables' first sizes (they grow on frames with ticks): poses written a frame, their floats (4 MB each copy), objects.
constexpr std::size_t kFirstPoses=4096,kFirstPoseFloats=1u<<20,kFirstBodies=1024;
constexpr int kRefusals=static_cast<int>(Refusal::kCount);

// An object's root as the last two ticks left it (see the top). tick: the last sample's; samples: 2 when prev is the
// tick before. This frame (without ticks): fresh (still there, the same, its drawn root what the tick left), moving
// with d, still (nothing moved); why: what left it as stepped.
struct Body {
    const unsigned char* object=nullptr;unsigned char* ctrl=nullptr;const void* vtable=nullptr;
    const unsigned char* inst=nullptr;
    thermal::Family family=thermal::Family::kNone;
    std::uint32_t tick=0;
    int samples=0;
    bool fresh=false,moving=false,still=false;
    Left why=Left::kSamples;
    float prev[16]{},cur[16]{};
    fpview::M4 d;
};
std::vector<Body> bodies;
edf::PtrIndex bodyIndex;    // object -> bodies index
edf::PtrIndex riders;       // this frame: a seated human -> 1 (posed or left with their vehicle)
std::uint32_t sampledTick=0;
unsigned untracked=0;       // listed objects the last sample had no room for
bool sampledAny=false,on=false;
// Ticks since the last frame without ticks (frameobjects::Sample): from kIdleTicks on the sample is skipped. Starts idle
// (nothing samples before the first frame without ticks). The bodies known stay (their weak counts too, bounded by what
// the last sample listed) until the sample resumes and forgets those no longer listed, or the scene ends. Counted by
// System's tick count (seenTick: the last Sample's), however often Sample is called a tick.
unsigned ticksSinceUnticked=frameobjects::kIdleTicks;
std::uint32_t seenTick=0;
bool seenAny=false;

// Who a written pose is drawn for, checked again before it goes back: the body (index and object: bodies change at a
// sample only, never between the write and the restore), a rider of its seat `seat` (the human and their control
// block).
struct Who {
    int body=-1;const unsigned char* owner=nullptr;
    const unsigned char* rider=nullptr;const unsigned char* riderCtrl=nullptr;unsigned seat=0;
};
// Which model of its holder (the body's object or the rider): one of its own, a soldier's weapon (+0x1950 slot index),
// a vehicle's trigger weapon (+0x638 holder index, the weapon and its control block).
enum class Slot { kOwn,kSoldierWeapon,kVehicleWeapon };
struct Part {
    const unsigned char* holder=nullptr;Slot slot=Slot::kOwn;std::uint64_t index=0;
    const unsigned char* weapon=nullptr;const unsigned char* weaponCtrl=nullptr;const unsigned char* model=nullptr;
};
struct Posed { Who who;Part part;const unsigned char* inst;float* pose;std::uint64_t count;std::size_t at; };
std::vector<Posed> posed;
std::vector<float> posedBefore,posedAfter;    // each pose before and after the write, at Posed::at
edf::PtrIndex posedIndex;                     // pose array -> 1 (a pose is written once a frame)
std::size_t posedWant=0,posedFloatsWant=0;    // what a frame wanted and did not have: grown at the next sample

frameobjects::Counts counts;
void Leave(Left why) noexcept { ++counts.left[static_cast<int>(why)]; }

// System's update list, false when there is none to read.
bool UpdateList(const unsigned char*& sys,const unsigned char* const*& list,std::uint64_t& count) noexcept {
    sys=static_cast<const unsigned char*>(scene::ObjectManager());
    if(!sys || !Readable(sys,kTickCount+4))return false;
    list=At<const unsigned char* const*>(sys,kUpdateList);
    count=At<std::uint64_t>(sys,kUpdateCount);
    return count<=kMostObjects && (!count || (list && Readable(list,static_cast<std::size_t>(count)*8)));
}

// A skeleton instance's pose array and bone count (in *count) of a model whose owner the caller holds live (see Reads at
// the top: no Readable, the caller is under __try).
float* PoseOf(const unsigned char* inst,std::uint64_t* count) noexcept {
    if(!inst)return nullptr;
    const auto n=At<std::uint64_t>(inst,kInstCount);
    const auto pose=At<float*>(inst,kInstPose);
    if(!n || n>kMostBones || !pose)return nullptr;
    *count=n;
    return pose;
}
// The skeleton instances a model draws with (its LODs'), each once (frame_rate_view.cpp InstancesOf), the same reads.
int InstancesOf(const unsigned char* model,const unsigned char** out) noexcept {
    int n=0;
    auto add=[&](const unsigned char* inst){
        if(!inst || n>=kMostInsts)return;
        for(int i=0;i<n;++i)if(out[i]==inst)return;
        out[n++]=inst;
    };
    if(!model)return 0;
    add(model+kModelInst);
    add(At<const unsigned char*>(model,kBaseLod+kLodInst));
    const auto lods=At<const unsigned char*>(model,kLods);
    const auto count=At<std::uint64_t>(model,kLodCount);
    if(lods && count<=8)
        for(std::uint64_t i=0;i<count;++i)add(At<const unsigned char*>(lods+i*kLodStride,kLodInst));
    return n;
}

// A weak count on a listed object's own control block (the block: vtable {destroy, delete}, use +8, weak +0xC;
// edf41classes.cpp WeakSet takes the same). Null when the object is not whole (being freed).
unsigned char* TakeWeak(const unsigned char* obj) noexcept {
    if(At<const unsigned char*>(obj,kSelf)!=obj)return nullptr;
    const auto ctrl=At<unsigned char*>(obj,kSelfCtrl);
    if(!ctrl || At<std::int32_t>(ctrl,edf::kCtrlUses)<=0)return nullptr;
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

// A live vehicle's seat count (edf::SeatCount's bound, read without Readable).
unsigned Seats(const unsigned char* vehicle) noexcept {
    const auto n=At<std::uint64_t>(vehicle,kSeatCount);
    return n<=edf::kMaxSeats && At<const unsigned char*>(vehicle,kSeats) ? static_cast<unsigned>(n) : 0;
}
// The human in a live vehicle's seat while it is there (the seat's weak_ptr alive, the human's own block the same).
const unsigned char* SeatHuman(unsigned char* vehicle,unsigned seat,const unsigned char** ctrlOut) noexcept {
    const unsigned char* const s=edf::SeatAt(vehicle,seat);
    const auto rider=At<const unsigned char*>(s,kSeatRider);
    const auto ctrl=At<const unsigned char*>(s,kSeatRiderCtrl);
    if(!rider || !ctrl || At<std::int32_t>(ctrl,edf::kCtrlUses)<=0 || At<const unsigned char*>(rider,kSelfCtrl)!=ctrl)
        return nullptr;
    *ctrlOut=ctrl;
    return rider;
}
// A live vehicle's trigger weapon in holder `i` while its holder keeps it (the control block +8 used), with that block.
const unsigned char* HolderWeapon(const unsigned char* vehicle,std::uint64_t i,const unsigned char** ctrlOut) noexcept {
    const auto holders=At<const unsigned char*>(vehicle,kHolders);
    const auto count=At<std::uint64_t>(vehicle,kHolderCount);
    if(!holders || i>=count || count>kMostHolders)return nullptr;
    const unsigned char* const h=holders+i*kHolderStride;
    const auto ctrl=At<const unsigned char*>(h,kHolderCtrl);
    const auto weapon=At<const unsigned char*>(h,kHolderWeapon);
    if(!ctrl || !weapon || At<std::int32_t>(ctrl,edf::kCtrlUses)<=0)return nullptr;
    *ctrlOut=ctrl;
    return weapon;
}
bool IsSoldier(const unsigned char* h) noexcept {
    if(!h)return false;
    const auto vt=At<const unsigned char*>(h,0);
    for(unsigned v:kSoldierVts)if(vt==image+v)return true;
    return false;
}
// A live soldier's weapon in slot `i`, null when the list does not hold one there.
const unsigned char* SoldierWeapon(const unsigned char* h,std::uint64_t i) noexcept {
    const auto list=At<const unsigned char* const*>(h,kWeapons);
    const auto weapons=At<std::int32_t>(h,kWeaponCount);
    if(!list || weapons<=0 || weapons>kMostWeapons || i>=static_cast<std::uint64_t>(weapons))return nullptr;
    return list[i];
}

// Whether a written pose's object, model and instance are still the ones it was written for.
bool Holds(const Posed& p) noexcept {
    const auto b=static_cast<std::size_t>(p.who.body);
    if(b>=bodies.size() || bodies[b].object!=p.who.owner || !Alive(bodies[b]))return false;
    if(p.who.rider) {
        auto* const vehicle=const_cast<unsigned char*>(p.who.owner);
        const unsigned char* ctrl=nullptr;
        if(p.who.seat>=Seats(vehicle) || SeatHuman(vehicle,p.who.seat,&ctrl)!=p.who.rider || ctrl!=p.who.riderCtrl)
            return false;
    }
    const Part& part=p.part;
    switch(part.slot) {
        case Slot::kSoldierWeapon:
            if(SoldierWeapon(part.holder,part.index)!=part.weapon || part.weapon+kWeaponModel!=part.model)return false;
            break;
        case Slot::kVehicleWeapon: {
            const unsigned char* ctrl=nullptr;
            if(HolderWeapon(part.holder,part.index,&ctrl)!=part.weapon || ctrl!=part.weaponCtrl ||
               part.weapon+kWeaponModel!=part.model)return false;
            break;
        }
        case Slot::kOwn: {
            const void* models[kMostModels];
            const std::size_t m=thermal::EnemyModelsLive(part.holder,models,kMostModels);
            bool found=false;
            for(std::size_t k=0;k<m;++k)found=found || models[k]==part.model;
            if(!found)return false;
            break;
        }
    }
    const unsigned char* insts[kMostInsts];
    const int n=InstancesOf(part.model,insts);
    bool found=false;
    for(int i=0;i<n;++i)found=found || insts[i]==p.inst;
    std::uint64_t bones=0;
    return found && PoseOf(p.inst,&bones)==p.pose && bones==p.count;
}

// What the frame wrote goes back: each bone that still holds what was written (a bone someone else wrote since is theirs).
void RestoreAll() noexcept {
    for(std::size_t i=posed.size();i-->0;) {
        const Posed& p=posed[i];
        __try {
            if(Holds(p))
                for(std::uint64_t b=0;b<p.count;++b)
                    if(!std::memcmp(p.pose+b*16,&posedAfter[p.at+b*16],64))
                        std::memcpy(p.pose+b*16,&posedBefore[p.at+b*16],64);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    posed.clear();posedBefore.clear();posedAfter.clear();posedIndex.Clear();
}

// Every instance of `part`'s model times d. False when one did not fit the frame's tables (left as stepped; they grow
// at the next sample). Each pose's record and both copies go in before its first byte is written: a fault part way
// leaves nothing the restore cannot put back.
bool PoseModel(const Who& who,const Part& part,const float* d) noexcept {
    const unsigned char* insts[kMostInsts];
    const int count=InstancesOf(part.model,insts);
    bool fit=true;
    for(int i=0;i<count;++i) {
        std::uint64_t n=0;
        float* const pose=PoseOf(insts[i],&n);
        if(!pose || posedIndex.Find(pose)>=0)continue;
        const std::size_t floats=static_cast<std::size_t>(n)*16;
        if(posed.size()>=posed.capacity() || posedBefore.size()+floats>posedBefore.capacity() ||
           posedAfter.size()+floats>posedAfter.capacity() || (posedIndex.Size()+1)*2>posedIndex.Capacity()) {
            if(posed.size()+1>posedWant)posedWant=posed.size()+1;
            if(posedBefore.size()+floats>posedFloatsWant)posedFloatsWant=posedBefore.size()+floats;
            fit=false;
            continue;
        }
        const std::size_t at=posedBefore.size();
        posedBefore.insert(posedBefore.end(),pose,pose+floats);
        posedAfter.insert(posedAfter.end(),pose,pose+floats);
        framerate::MulBones(&posedAfter[at],n,d);
        posedIndex.Set(pose,1);
        posed.push_back(Posed{who,part,insts[i],pose,n,at});
        std::memcpy(pose,&posedAfter[at],floats*sizeof(float));
    }
    return fit;
}
// A vehicle's (live) trigger weapons (+0x638) whose +0xF30 is a registered AnimationModel, times d.
bool PoseVehicleWeapons(const Who& who,const unsigned char* holder,const float* d) noexcept {
    bool fit=true;
    const auto count=At<std::uint64_t>(holder,kHolderCount);
    for(std::uint64_t i=0;i<count && i<kMostHolders;++i) {
        Part part;
        part.holder=holder;part.slot=Slot::kVehicleWeapon;part.index=i;
        part.weapon=HolderWeapon(holder,i,&part.weaponCtrl);
        if(!part.weapon || !thermal::IsAnimationModel(part.weapon+kWeaponModel))continue;
        part.model=part.weapon+kWeaponModel;
        fit=PoseModel(who,part,d) && fit;
    }
    return fit;
}
// Everything `holder` (live) draws times d: its models; a soldier's weapons ([h+0x1950], models +0xF30); a vehicle's
// trigger weapons (PoseVehicleWeapons).
bool PoseObject(const Who& who,const unsigned char* holder,thermal::Family family,const float* d) noexcept {
    const void* models[kMostModels];
    const std::size_t m=thermal::EnemyModelsLive(holder,models,kMostModels);
    bool fit=true;
    for(std::size_t k=0;k<m;++k) {
        Part part;
        part.holder=holder;part.model=static_cast<const unsigned char*>(models[k]);
        fit=PoseModel(who,part,d) && fit;
    }
    if(family==thermal::Family::kVehicle)fit=PoseVehicleWeapons(who,holder,d) && fit;
    if(family!=thermal::Family::kHuman || !IsSoldier(holder))return fit;
    for(std::uint64_t i=0;i<static_cast<std::uint64_t>(kMostWeapons);++i) {
        Part part;
        part.holder=holder;part.slot=Slot::kSoldierWeapon;part.index=i;
        part.weapon=SoldierWeapon(holder,i);
        if(!part.weapon)continue;
        part.model=part.weapon+kWeaponModel;
        fit=PoseModel(who,part,d) && fit;
    }
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

// One listed object's sample (the game holds it live: no Readable). kSkip: nothing of it this tick; kFull: no room.
enum class Sampled { kYes,kSkip,kFull };
Sampled SampleOne(const unsigned char* obj,std::uint32_t tick,bool next) noexcept {
    if(!obj || (At<unsigned char>(obj,kObjFlags)&kObjDeleted))return Sampled::kSkip;
    const auto model=static_cast<const unsigned char*>(thermal::EnemyModelLive(obj));
    if(!model)return Sampled::kSkip;
    const unsigned char* const inst=model+kModelInst;
    std::uint64_t n=0;
    const float* const pose=PoseOf(inst,&n);
    if(!pose)return Sampled::kSkip;
    const void* const vt=At<const void*>(obj,0);
    int at=bodyIndex.Find(obj);
    if(at>=0) {
        const Body& b=bodies[static_cast<std::size_t>(at)];
        if(b.tick==tick && b.samples)return Sampled::kYes;   // listed twice
        if(At<const unsigned char*>(obj,kSelfCtrl)!=b.ctrl) {   // another object at the address
            Forget(static_cast<std::size_t>(at));
            at=-1;
        }
    }
    if(at<0) {
        // Grown before the walk; never past it mid-walk (the index's room is the bodies' capacity).
        if(bodies.size()>=bodies.capacity() || (bodyIndex.Size()+1)*2>bodyIndex.Capacity())return Sampled::kFull;
        unsigned char* const ctrl=TakeWeak(obj);
        if(!ctrl)return Sampled::kSkip;
        at=static_cast<int>(bodies.size());
        bodies.push_back(Body{});
        bodies.back().object=obj;
        bodies.back().ctrl=ctrl;
        bodyIndex.Set(obj,at);
    }
    Body& b=bodies[static_cast<std::size_t>(at)];
    if(!next || b.tick!=sampledTick || b.vtable!=vt || b.inst!=inst)b.samples=0;
    if(!b.samples)b.family=thermal::EnemyFamilyLive(obj);
    b.vtable=vt;b.inst=inst;b.tick=tick;
    std::memcpy(b.prev,b.cur,sizeof(b.prev));
    std::memcpy(b.cur,pose,sizeof(b.cur));
    if(b.samples<2)++b.samples;
    return Sampled::kYes;
}

// The tables' growth (a frame with ticks only): false when the memory is not there (nothing sampled this tick; the frames
// until the next draw as stepped). No throw leaves here: the sample runs inside noexcept frames.
bool Grow(std::size_t want) noexcept {
    try {
        if(bodies.capacity()<want)bodies.reserve(want+want/4+64);
        bodyIndex.Reserve(bodies.capacity());
        riders.Reserve(bodies.capacity());
        if(posedWant>posed.capacity())posed.reserve(posedWant+posedWant/4);
        if(posedFloatsWant>posedBefore.capacity())posedBefore.reserve(posedFloatsWant+posedFloatsWant/4);
        if(posedFloatsWant>posedAfter.capacity())posedAfter.reserve(posedFloatsWant+posedFloatsWant/4);
        posedIndex.Reserve(posed.capacity());
        return true;
    } catch(const std::bad_alloc&) {
        return false;
    }
}

void SampleAll() noexcept {
    const unsigned char* sys=nullptr;const unsigned char* const* list=nullptr;std::uint64_t count=0;
    if(!UpdateList(sys,list,count))return;
    const auto tick=At<std::uint32_t>(sys,kTickCount);
    if(sampledAny && tick==sampledTick)return;
    const bool next=sampledAny && tick==sampledTick+1;
    if(!Grow(static_cast<std::size_t>(count)))return;
    unsigned full=0;
    for(std::uint64_t i=0;i<count;++i) {
        __try {
            if(SampleOne(list[i],tick,next)==Sampled::kFull)++full;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    // Objects not sampled this tick (gone from the list, deleted, no model) are forgotten.
    for(std::size_t i=0;i<bodies.size();) {
        if(bodies[i].tick==tick && bodies[i].samples)++i;
        else Forget(i);
    }
    sampledTick=tick;
    sampledAny=true;
    untracked=full;
    counts.objects=static_cast<unsigned>(bodies.size());
}

// A body's own state this frame (see Body).
void OwnState(Body& b,float alpha) noexcept {
    b.fresh=b.moving=b.still=false;
    if(!Alive(b)) {
        b.why=Left::kGone;
        return;
    }
    if(At<const void*>(b.object,0)!=b.vtable) {
        b.why=Left::kVtable;
        return;
    }
    std::uint64_t n=0;
    const float* const live=PoseOf(b.inst,&n);
    if(!live || std::memcmp(live,b.cur,sizeof(b.cur))) {   // written after the sample (see the top), or gone
        b.why=Left::kPose;
        return;
    }
    b.fresh=true;
    if(b.samples<2) {
        b.why=Left::kSamples;
        return;
    }
    if(!std::memcmp(b.prev,b.cur,sizeof(b.cur))) {
        b.still=true;
        return;
    }
    b.why=Left::kRefused;
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

// A vehicle's seats: each human there goes with the vehicle's d (null: the vehicle is still, or left as stepped for
// `why`, and so are they).
void PoseRiders(int body,const void* rootHuman,const float* d,bool still,Left why) noexcept {
    auto* const vehicle=const_cast<unsigned char*>(bodies[static_cast<std::size_t>(body)].object);
    const unsigned seats=Seats(vehicle);
    for(unsigned s=0;s<seats;++s) {
        __try {
            const unsigned char* ctrl=nullptr;
            const unsigned char* const h=SeatHuman(vehicle,s,&ctrl);
            if(!h || h==rootHuman || riders.Find(h)>=0 || thermal::EnemyFamilyLive(h)!=thermal::Family::kHuman)continue;
            if((riders.Size()+1)*2>riders.Capacity())continue;   // past this frame's table: posed as its own body
            riders.Set(h,1);
            if(!d) {
                if(still)++counts.still;
                else Leave(why);
                continue;
            }
            Who who;
            who.body=body;who.owner=vehicle;who.rider=h;who.riderCtrl=ctrl;who.seat=s;
            if(PoseObject(who,h,thermal::Family::kHuman,d)) {
                ++counts.posed;
                ++counts.riders;
            } else Leave(Left::kFull);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Leave(Left::kFault);
        }
    }
}

void PoseAll(float alpha,const void* rootHuman,const void* rootVehicle,const float* rootD) noexcept {
    if(!sampledAny || bodies.empty())return;
    const unsigned char* sys=nullptr;const unsigned char* const* list=nullptr;std::uint64_t count=0;
    if(!UpdateList(sys,list,count) || At<std::uint32_t>(sys,kTickCount)!=sampledTick) {   // a tick without its sample
        counts.left[static_cast<int>(Left::kUnsampled)]+=static_cast<unsigned>(bodies.size());
        return;
    }
    counts.left[static_cast<int>(Left::kFull)]+=untracked;
    for(Body& b:bodies) {
        __try {
            OwnState(b,alpha);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            b.fresh=b.moving=b.still=false;
            b.why=Left::kFault;
        }
    }
    // The player's weapons: fps-7 wrote their models (frame_rate_view.cpp PoseRoot, [h+0x1950] +0xF30) before this, so
    // a listed weapon's root is not cur any more: fps-7's, left to it like the player (not counted).
    const unsigned char* rootWeapons[kMostWeapons];
    int rootWeaponCount=0;
    __try {
        const auto* const h=static_cast<const unsigned char*>(rootHuman);
        if(IsSoldier(h))
            for(std::uint64_t i=0;i<static_cast<std::uint64_t>(kMostWeapons);++i)
                if(const unsigned char* const w=SoldierWeapon(h,i))rootWeapons[rootWeaponCount++]=w;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    auto rootWeapon=[&](const unsigned char* obj){
        for(int k=0;k<rootWeaponCount;++k)if(rootWeapons[k]==obj)return true;
        return false;
    };
    // The riders first: every vehicle's seats (the player's with fps-7's D) claim theirs.
    riders.Clear();
    for(std::size_t i=0;i<bodies.size();++i) {
        const Body& b=bodies[i];
        if(b.family!=thermal::Family::kVehicle || !Alive(b))continue;
        const float* d=nullptr;
        bool still=false;
        Left why=b.why;
        if(b.object==rootVehicle) {
            d=rootD;
            why=Left::kRefused;   // fps-7 refused or left its root (its own count says which)
        } else if(b.fresh && b.moving)d=b.d.m;
        else still=b.still;
        PoseRiders(static_cast<int>(i),rootHuman,d,still,why);
    }
    for(std::size_t i=0;i<bodies.size();++i) {
        const Body& b=bodies[i];
        // The player's vehicle: fps-7 posed its model (frame_rate_view.cpp PoseRoot, v+0xE40 only); its trigger weapons
        // go with fps-7's D here (left as stepped, with the vehicle, when fps-7 did not pose it).
        if(b.object==rootVehicle && b.family==thermal::Family::kVehicle && rootD && Alive(b)) {
            __try {
                Who who;
                who.body=static_cast<int>(i);who.owner=b.object;
                if(!PoseVehicleWeapons(who,b.object,rootD))Leave(Left::kFull);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                Leave(Left::kFault);
            }
            continue;
        }
        if(b.object==rootHuman || b.object==rootVehicle || rootWeapon(b.object) || riders.Find(b.object)>=0)
            continue;   // fps-7's, a rider
        if(!b.fresh || (!b.moving && !b.still)) {
            Leave(b.why);
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
            else Leave(Left::kFull);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            Leave(Left::kFault);
        }
    }
    if(posed.size()>counts.mostPoses)counts.mostPoses=static_cast<unsigned>(posed.size());
}

// Every body forgotten (its weak count given back), the samples with them.
void ForgetAll() noexcept {
    for(Body& b:bodies) {
        __try {
            DropWeak(b.ctrl);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    bodies.clear();bodyIndex.Clear();riders.Clear();
    sampledAny=false;untracked=0;
    counts.objects=0;
}

bool InstallTables() noexcept {
    try {
        bodies.reserve(kFirstBodies);
        bodyIndex.Reserve(kFirstBodies);
        riders.Reserve(kFirstBodies);
        posed.reserve(kFirstPoses);
        posedBefore.reserve(kFirstPoseFloats);
        posedAfter.reserve(kFirstPoseFloats);
        posedIndex.Reserve(kFirstPoses);
        return true;
    } catch(const std::bad_alloc&) {
        return false;
    }
}
}  // namespace

namespace frameobjects {
void Install() noexcept {
    if(on)return;
    if(!InstallTables()) {
        Log("FRAMERATE objects: no memory for the tables: other objects are drawn as stepped");
        return;
    }
    on=true;
}

void Sample(bool unticked) noexcept {
    if(!on)return;
    const auto* const sys=static_cast<const unsigned char*>(scene::ObjectManager());
    if(!sys || !Readable(sys,kTickCount+4))return;
    const auto tick=At<std::uint32_t>(sys,kTickCount);
    // The ticks since the last call (System's count: a call a frame, or more, never counts a tick twice).
    const std::uint32_t ticks=seenAny ? tick-seenTick : 1u;
    seenTick=tick;
    seenAny=true;
    if(unticked) {   // the ticks before a frame without ticks are not counted towards idle: the window opens here
        ticksSinceUnticked=0;
        if(ticks)SampleAll();
        return;
    }
    if(!ticks)return;   // this tick sampled (or skipped) already
    if(ticksSinceUnticked>=kIdleTicks) {
        counts.idleTicks+=ticks;
        return;
    }
    ticksSinceUnticked=ticks>kIdleTicks-ticksSinceUnticked ? kIdleTicks : ticksSinceUnticked+ticks;
    SampleAll();
}

void Pose(float alpha,const void* rootHuman,const void* rootVehicle,const float* rootD) noexcept {
    if(!on || !(alpha>=0.0f && alpha<1.0f))return;
    PoseAll(alpha,rootHuman,rootVehicle,rootD);
}

void Restore() noexcept {
    if(on && !posed.empty())RestoreAll();
}

void SceneOver() noexcept {
    if(!on)return;
    if(!posed.empty())RestoreAll();
    ForgetAll();
    ticksSinceUnticked=kIdleTicks;   // the next scene samples from its first frame without ticks
    seenAny=false;                   // and counts its ticks from its own System's
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
