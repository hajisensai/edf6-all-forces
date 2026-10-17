// EDF4.1 Hector's attack behaviour on EDF6_Berserker_Large (src/hector_ai.h has the rules and where they come from in
// EDF41.exe). Only an object whose SGO carries the key `edf41_hector` is touched (tools/make_edf41_objects.py writes
// it into every Hector variant); EDF6's own Berserkers go through unchanged.
//
//   edf41_hector: { ranges: [arms_l range, arms_r range], same_weapon: 0 | 1, ai: [720, 1440, 540, 720, 150, 20, 80] }
//
// The key is read through the game's own SGO accessors (the class reads its keys the same way, EDF.dll 0x34C99A):
// the object's tree is the member at +0xA8 and a handle is {tree, node index}. 0xD7480 (handle*, out*, wchar_t* key)
// is the child by name (a missing key gives tree == null), 0x53060 a node's child count, 0x52DE0 (handle*, i) the
// node index of child i, 0x52F30 a number node as float. The class never looks the key up, so it is read-only data.
//
// Hooks (EDF.dll, TimeDateStamp 0x678CCB46; each is a call site, RedirectCall verifies the callee it replaces):
//  - 0x34F2A0, the call of the base update 0x343DE0 in the Large's update (vtable 0x17ADA78 slot 5 = 0x34F280): the
//    per-frame tick of the back-off cycle, before the game's own update so this frame's walk sees it.
//  - 0x34D8D8 and 0x34DA65, the calls of the walk 0x33E3B0 (MoveTo(this, &goal, a, b, speed, c): writes the walk vector
//    at +0x1290 as normalize(goal - position) * speed * clamp((dist - a) / (b / a), .., 1), nothing when dist <= a, no
//    division for a zero vector, 0x33E563 / 0x33E5DB) in the two walking states 0x34D890 (follow: a = b = 0, speed
//    0.9..1.2 x linear_speed) and 0x34D9C0 (chase: a 1, b 20, speed 0.125); both then face the target with 0x33E840.
//    A Hector's speed is always 4.1's (hector::Steer: 1, 0.85 backing off, 0 standing; the class multiplies the walk
//    vector by MoveSpeed[0] = 6 units/s, 0x53D7CC). Inside `near` the goal becomes the target itself (a = b = 0: 4.1
//    walks straight at full speed); in a back-off the goal is the point opposite the target, so the Hector walks
//    backward facing the player (the walk animation follows the walk vector against the facing).
//  - After the base update (the skeleton is built from the bones' locals at its start, 0x317B88 -> 0x1100B90, which
//    writes only the world matrices, bone record +0xB0), the arms aim as 4.1's weapons did (hector::ArmAim): the arm
//    bone's local (+0x70) becomes Rx(pitch) Ry(yaw) x the animation's, and its world and every bone below it (the guns'
//    bones, mdb.graft_rigid / graft_skinned put them there; the muzzle locators hang on the arm bone) are moved to match
//    now, so this frame's guns, muzzles and draw see it whichever comes first, the CAS's next write or the next build.
//    An arm aims while its gun battery attacks: the condition the class's own per-gun aim (0x34F900) runs under.
//    Then the weapon's own animation (SGO edf41_hector_anim, hector::StepOpenClose / StepRoll): its bones' locals from the
//    4.1 clip and the world of every bone below the arm rebuilt from its parent's. And the head player (`head` of Hector.CAS)
//    opens and closes as 4.1's head does (hector::StepHead; 0x1166FB0 finds the player, 0x116C2D0 sets its state). The
//    functions only this calls are checked by their first bytes (kAnimPrints): on another build it is all left off.
//  - 0x34FEFE (the gun loop of StartAttack(1) = 0x34FE90) and 0x34E123 (0x34E0D0): the call of the per-gun attack start
//    0x34E180(gun). An attack picks its arms once (PickArms) and the arms not picked are not started.
// The gun object is {GunBattery*, ...} with its owner at +0x10; the owner's guns are the vector of 16-byte
// {gun*, control} at +0x1AA8 (count +0x1AB8), in gun_control's order. The target is 0x30C0F0(owner) (null: none), its
// position the vec4 at +0x90 (the vec4 at *(*(+0x368)) + 0x10 when +0x378 is set), the owner's at +0x90.
#include "crew.h"
#include "edf41classes.h"
#include "hector_ai.h"
#include "memory.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace crew {
namespace {
constexpr std::size_t kBaseUpdate=0x343DE0;
constexpr std::size_t kBaseUpdateSite=0x34F2A0;
constexpr std::size_t kMoveTo=0x33E3B0;
constexpr std::size_t kMoveToSites[]={0x34D8D8,0x34DA65};
constexpr std::size_t kGunStart=0x34E180;
constexpr std::size_t kGunStartSites[]={0x34FEFE,0x34E123};
constexpr std::size_t kGetTarget=0x30C0F0;
constexpr std::size_t kChildByName=0xD7480;
constexpr std::size_t kChildCount=0x53060;
constexpr std::size_t kChildIndex=0x52DE0;
constexpr std::size_t kNodeFloat=0x52F30;
constexpr std::size_t kNodeText=0x551E0;       // const wchar_t* (handle*): a string node (0x53B832 reads InterpolateNode so)
constexpr std::size_t kCasPlayerByName=0x1166FB0;  // int (CasPlayers*, const wchar_t*): -1 none (0x33D392 "additive_action")
constexpr std::size_t kCasSetState=0x116C2D0;  // bool (CasPlayers*, int player, const wchar_t* state, int)
// first bytes of the functions only the weapon / head animation calls: on any other build those stay off
struct Print { std::size_t rva; unsigned char bytes[8]; };
constexpr Print kAnimPrints[]={
    {kNodeText,{0x48,0x83,0xEC,0x28,0x4C,0x8B,0xD1,0xE8}},
    {kCasPlayerByName,{0x48,0x89,0x5C,0x24,0x08,0x48,0x83,0x79}},
    {kCasSetState,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},
    {kChildCount,{0x48,0x8B,0x11,0x44,0x8B,0x49,0x08,0x4C}},
    {kChildIndex,{0x4C,0x8B,0x09,0x44,0x8B,0x51,0x08,0x44}},
};

constexpr std::size_t kTreeAt=0xA8;          // the object's SGO tree
constexpr std::size_t kPosAt=0x90;           // vec4
constexpr std::size_t kHasTargetAt=0x4A0;    // byte
constexpr std::size_t kGunsAt=0x1AA8;        // {gun*, control*}[]
constexpr std::size_t kGunCountAt=0x1AB8;
constexpr std::size_t kGunOwnerAt=0x10;
constexpr std::size_t kSkeletonAt=0x940;     // model component +0x8A0, skeleton = model + 0xA0 (0x53B86C, 0x317B7A)
constexpr std::size_t kDestroyedAt=0x2E8;    // byte
constexpr std::size_t kBoneSize=0x110;       // bone records at [skeleton+0x10], count u64 [skeleton+0x20]
constexpr std::size_t kBoneParentAt=0x10,kBoneLocalAt=0x70,kBoneWorldAt=0xB0;
// the gun battery's attack state, as 0x34F913..0x34F94E reads it: GunControl {GunBattery*, ...}
constexpr std::size_t kBatteryFlagsAt=0x2C0,kBatteryLeftAt=0x2E0,kBatteryCountAt=0x30;
// the CAS players (0x53BB17: +0xBD0, main player [+0xFD0]); each player 0x160 bytes at [+0x60], count [+0x70] (0x1166FB0
// reads them so); a player's clip clock +0xE8, length +0xF0 (edf41classes ClipNow, real-run with the dragon)
constexpr std::size_t kCasAt=0xBD0,kCasPlayers=0x60,kCasPlayerCount=0x70,kCasPlayerSize=0x160,kClipTime=0xE8,kClipLength=0xF0;
constexpr ULONGLONG kStaleMs=5000;           // an object not ticked for this long is a new one (or the game was paused)

struct Handle {
    void* tree;
    std::uint32_t index;
    std::uint32_t pad;
};
using ChildByNameFn=Handle*(__fastcall*)(const Handle* parent,Handle* out,const wchar_t* key);
using ChildCountFn=int(__fastcall*)(const Handle*);
using ChildIndexFn=std::uint32_t(__fastcall*)(const Handle*,std::uint32_t);
using NodeFloatFn=float(__fastcall*)(const Handle*);
using GetTargetFn=void*(__fastcall*)(void* self);
using BaseUpdateFn=std::uint64_t(__fastcall*)(void* self,void* dt);
using MoveToFn=bool(__fastcall*)(void* self,const float* goal,float a,float b,float speed,float c);
using GunStartFn=void(__fastcall*)(void* gun);

struct Entry {
    ObjRef ref;
    hector::Config cfg;
    hector::Cycle cycle;
    hector::Rng rng;
    ULONGLONG last=0;
    std::uint64_t tick=0;
    std::uint64_t pickTick=~0ull;
    unsigned pickMask=0;
    bool haveTarget=false;
    float dist=0.0f;                 // horizontal distance to the target at the last tick
    float target[3]{};
    // the arms' aim (arms_l, arms_r): bone, the bones below it, the eased angles, the animation's local of the arm
    // bone and what this file last wrote there (the CAS may or may not write it again before the next build)
    bool bonesTried=false;
    int arm[hector::kArms]{-1,-1};
    std::vector<int> below[hector::kArms];
    hector::ArmAim aim[hector::kArms];
    float base[hector::kArms][16]{};
    float written[hector::kArms][16]{};
    bool haveWritten[hector::kArms]{};
    // the weapons' own animations (SGO edf41_hector_anim) and the head
    struct ArmAnim {
        hector::WeaponKind kind=hector::WeaponKind::None;
        std::vector<int> bone;           // skeleton index of each animated bone
        std::vector<float> clip[3];      // open, close, roll: frames, step, keys
        hector::OpenClose oc;
        hector::Barrels roll;
    } anim[hector::kArms];
    bool animRead=false;
    int headPlayer=-2;                   // -2: not looked up, -1: none
    hector::Head head;
};

std::atomic<int> animState{0};          // 0 unchecked, 1 the functions are this build's, 2 they are not: no animation

std::mutex tableLock;
std::unordered_map<const void*,Entry> table;
std::uint64_t ticks=0;

// The child `key` of `parent`; false when there is none.
bool Child(const Handle& parent,const wchar_t* key,Handle& out) noexcept {
    out={nullptr,0xFFFFFFFFu,0};
    reinterpret_cast<ChildByNameFn>(image+kChildByName)(&parent,&out,key);
    return out.tree!=nullptr;
}

bool Floats(const Handle& node,float* values,int want) noexcept {
    if(reinterpret_cast<ChildCountFn>(image+kChildCount)(&node)!=want)return false;
    for(int i=0;i<want;++i) {
        Handle e{node.tree,reinterpret_cast<ChildIndexFn>(image+kChildIndex)(&node,static_cast<std::uint32_t>(i)),0};
        values[i]=reinterpret_cast<NodeFloatFn>(image+kNodeFloat)(&e);
    }
    return true;
}

bool AnimReady() noexcept {
    const int s=animState.load();
    if(s)return s==1;
    for(const Print& p:kAnimPrints)
        if(!Readable(image+p.rva,8) || std::memcmp(image+p.rva,p.bytes,8)!=0) {
            Log("EDF41AI the code at EDF.dll+0x%zX is not what this was written for: Hector's weapon and head animations are off",p.rva);
            animState=2;
            return false;
        }
    animState=1;
    return true;
}

Handle At(const Handle& node,int i) noexcept {
    return Handle{node.tree,reinterpret_cast<ChildIndexFn>(image+kChildIndex)(&node,static_cast<std::uint32_t>(i)),0};
}
int CountOf(const Handle& node) noexcept { return reinterpret_cast<ChildCountFn>(image+kChildCount)(&node); }

// The object's edf41_hector_anim (tools/make_edf41_objects.py hector_anim); an arm whose data does not fit stays None.
void ReadAnim(void* self,Entry& e,void* skel) noexcept {
    const Handle root{static_cast<char*>(self)+kTreeAt,0,0};
    Handle key{};
    if(!Child(root,L"edf41_hector_anim",key) || CountOf(key)!=hector::kArms)return;
    for(int i=0;i<hector::kArms;++i) {
        Entry::ArmAnim& a=e.anim[i];
        const Handle arm=At(key,i);
        if(CountOf(arm)!=5)continue;
        const Handle kind=At(arm,0),names=At(arm,1);
        const int k=static_cast<int>(reinterpret_cast<NodeFloatFn>(image+kNodeFloat)(&kind));
        const int nb=CountOf(names);
        if(k<1 || k>2 || nb<=0 || nb>16)continue;
        a.bone.assign(static_cast<std::size_t>(nb),-1);
        bool ok=true;
        for(int b=0;b<nb;++b) {
            const Handle n=At(names,b);
            const wchar_t* text=reinterpret_cast<const wchar_t*(__fastcall*)(const Handle*)>(image+kNodeText)(&n);
            a.bone[static_cast<std::size_t>(b)]=text ? edf41c::BoneIndex(skel,text) : -1;
            ok=ok && a.bone[static_cast<std::size_t>(b)]>=0;
        }
        for(int c=0;c<3 && ok;++c) {
            const Handle clip=At(arm,2+c);
            const int n=CountOf(clip);
            if(n<=0)continue;
            a.clip[c].resize(static_cast<std::size_t>(n));
            for(int j=0;j<n;++j) {
                const Handle v=At(clip,j);
                a.clip[c][static_cast<std::size_t>(j)]=reinterpret_cast<NodeFloatFn>(image+kNodeFloat)(&v);
            }
            const float frames=a.clip[c][0];
            if(!(frames>=1.0f) || static_cast<std::size_t>(n)!=2+static_cast<std::size_t>(frames)*static_cast<std::size_t>(nb)*hector::kKeyFloats)
                ok=false;
        }
        const bool need=k==1 ? !a.clip[0].empty() && !a.clip[1].empty() : !a.clip[2].empty();
        if(!ok || !need) {
            Log("EDF41AI hector %p arm %d: its weapon animation does not fit the model (%d bones), left still",self,i,nb);
            a=Entry::ArmAnim{};
            continue;
        }
        a.kind=static_cast<hector::WeaponKind>(k);
        Log("EDF41AI hector %p arm %d weapon animation %s, %d bones",self,i,k==1 ? "open/close" : "roll",nb);
    }
}

hector::Clip ClipOf(const std::vector<float>& v) noexcept {
    hector::Clip c;
    if(v.size()<2)return c;
    c.frames=static_cast<int>(v[0]); c.step=v[1]; c.keys=v.data()+2;
    return c;
}

// The head: 4.1's wait / open / wait / close on the CAS player `head` (Hector.CAS has it; the class never plays it).
void StepHead(void* self,Entry& e) noexcept {
    char* cas=static_cast<char*>(self)+kCasAt;
    if(!Readable(cas,kCasPlayerCount+8))return;
    const std::uint64_t players=*reinterpret_cast<const std::uint64_t*>(cas+kCasPlayerCount);
    char* array=*reinterpret_cast<char**>(cas+kCasPlayers);
    if(e.headPlayer==-2) {
        e.headPlayer=reinterpret_cast<int(__fastcall*)(void*,const wchar_t*)>(image+kCasPlayerByName)(cas,L"head");
        if(e.headPlayer>=0 && static_cast<std::uint64_t>(e.headPlayer)>=players)e.headPlayer=-1;
        Log("EDF41AI hector %p head player %d",self,e.headPlayer);
    }
    if(e.headPlayer<0 || !array)return;
    const char* p=array+static_cast<std::size_t>(e.headPlayer)*kCasPlayerSize;
    if(!Readable(p,kCasPlayerSize))return;
    const float time=*reinterpret_cast<const float*>(p+kClipTime),length=*reinterpret_cast<const float*>(p+kClipLength);
    const bool done=std::isfinite(time) && std::isfinite(length) && time+1.0f>=length;
    if(const wchar_t* state=hector::StepHead(e.head,e.rng,done))
        reinterpret_cast<bool(__fastcall*)(void*,int,const wchar_t*,int)>(image+kCasSetState)(cas,e.headPlayer,state,0);
}

// The object's edf41_hector key. false: the object is not a 4.1 Hector (or its key is malformed).
bool ReadConfig(void* self,hector::Config& cfg) noexcept {
    const Handle root{static_cast<char*>(self)+kTreeAt,0,0};
    Handle key{},ranges{},same{},ai{};
    if(!Child(root,L"edf41_hector",key))return false;
    float r[2]={},s[1]={},a[7]={};
    if(!Child(key,L"ranges",ranges) || !Floats(ranges,r,2))return false;
    if(!Child(key,L"same_weapon",same))return false;
    s[0]=reinterpret_cast<NodeFloatFn>(image+kNodeFloat)(&same);
    if(!Child(key,L"ai",ai) || !Floats(ai,a,7))return false;
    cfg.range[0]=r[0]; cfg.range[1]=r[1];
    cfg.sameWeapon=s[0]>0.5f;
    cfg.dwellBase=a[0]; cfg.dwellRand=a[1]; cfg.retreatBase=a[2]; cfg.retreatRand=a[3];
    cfg.closeIn=a[4]; cfg.stopMin=a[5]; cfg.stopMax=a[6];
    return true;
}

// The target's position, or false when the object has none.
bool TargetOf(void* self,float* out) noexcept {
    char* me=static_cast<char*>(self);
    if(!*reinterpret_cast<std::uint8_t*>(me+kHasTargetAt))return false;
    char* t=static_cast<char*>(reinterpret_cast<GetTargetFn>(image+kGetTarget)(self));
    if(!t)return false;
    const float* p;
    if(*reinterpret_cast<void**>(t+0x378))p=reinterpret_cast<const float*>(**reinterpret_cast<char***>(t+0x368)+0x10);
    else p=reinterpret_cast<const float*>(t+kPosAt);
    out[0]=p[0]; out[1]=p[1]; out[2]=p[2];
    return true;
}

float FlatDistance(void* self,const float* target) noexcept {
    const float* me=reinterpret_cast<const float*>(static_cast<char*>(self)+kPosAt);
    const float dx=target[0]-me[0],dz=target[2]-me[2];
    return std::sqrt(dx*dx+dz*dz);
}

// The entry of a 4.1 Hector, or null. Call with tableLock held.
Entry* Find(const void* self) noexcept {
    const auto it=table.find(self);
    return it==table.end() || !hector::CacheMatches(it->second.ref.ctrl,ObjRef::Of(self).ctrl,true)
        ? nullptr : &it->second;
}

void Tick(void* self) noexcept {
    hector::Config cfg;
    const bool marked=ReadConfig(self,cfg);
    const ObjRef ref=ObjRef::Of(self);
    if(!marked || !ref.ctrl) {
        std::lock_guard<std::mutex> hold(tableLock);
        table.erase(self);
        return;
    }
    float target[3];
    const bool have=TargetOf(self,target);
    const ULONGLONG now=GetTickCount64();
    std::lock_guard<std::mutex> hold(tableLock);
    auto cached=table.find(self);
    if(cached!=table.end() && !hector::CacheMatches(cached->second.ref.ctrl,ref.ctrl,marked))table.erase(cached);
    Entry* e=Find(self);
    if(e && now-e->last>kStaleMs) {      // another object at this address, or a long pause: start again
        table.erase(self);
        e=nullptr;
    }
    if(!e) {
        e=&table[self];
        e->ref=ref;
        e->cfg=cfg;
        e->rng.Seed(reinterpret_cast<std::uintptr_t>(self)^(now*0x9E3779B97F4A7C15ull));
        e->cycle=hector::Begin(cfg,e->rng);
        Log("EDF41AI hector %p: ranges %.0f/%.0f same=%d ai=%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f first counter %.0f stop radius %.1f",self,
            cfg.range[0],cfg.range[1],cfg.sameWeapon ? 1 : 0,cfg.dwellBase,cfg.dwellRand,cfg.retreatBase,cfg.retreatRand,cfg.closeIn,
            cfg.stopMin,cfg.stopMax,e->cycle.counter,e->cycle.stopRadius);
    }
    e->last=now;
    ++e->tick;
    e->haveTarget=have;
    if(have) {
        e->target[0]=target[0]; e->target[1]=target[1]; e->target[2]=target[2];
        e->dist=FlatDistance(self,target);
        const hector::Event ev=hector::Step(e->cycle,e->cfg,e->rng,e->dist);
        if(ev==hector::Event::BackoffStart)
            Log("EDF41AI backoff start %p dist %.1f stop radius %.1f walks backward for %d frames",self,e->dist,e->cycle.stopRadius,e->cycle.retreatLeft);
        else if(ev==hector::Event::BackoffEnd)
            Log("EDF41AI backoff end %p dist %.1f next counter %.0f stop radius %.1f",self,e->dist,e->cycle.counter,e->cycle.stopRadius);
    }
    if((++ticks&0x3FFF)==0)       // forget the objects that are gone
        for(auto it=table.begin();it!=table.end();)
            it=now-it->second.last>kStaleMs ? table.erase(it) : std::next(it);
}

// Whether gun `i` of the owner attacks now: the class's own gate for its per-gun aim (0x34F913..0x34F94E).
bool Attacking(const void* owner,int i) noexcept {
    const char* base=static_cast<const char*>(owner);
    void* const* guns=*reinterpret_cast<void* const* const*>(base+kGunsAt);
    const std::uint64_t count=*reinterpret_cast<const std::uint64_t*>(base+kGunCountAt);
    if(!guns || count!=hector::kArms || !Readable(guns,sizeof(void*)*2*hector::kArms))return false;
    const char* control=static_cast<const char*>(guns[2*i]);
    if(!control || !Readable(control,sizeof(void*)))return false;
    const char* battery=*reinterpret_cast<const char* const*>(control);
    if(!battery || !Readable(battery,kBatteryLeftAt+4))return false;
    const std::uint32_t flags=*reinterpret_cast<const std::uint32_t*>(battery+kBatteryFlagsAt);
    const std::uint32_t left=*reinterpret_cast<const std::uint32_t*>(battery+kBatteryLeftAt);
    const std::uint32_t v=(flags&1u) && left==0 ? *reinterpret_cast<const std::uint32_t*>(battery+kBatteryCountAt) : left;
    return v!=0;
}

bool Finite16(const float* m) noexcept {
    for(int i=0;i<16;++i)
        if(!std::isfinite(m[i]))return false;
    return true;
}

// The arm's weapon pose (its own CAS clips, hector::StepOpenClose / StepRoll): the animated bones' locals, then the world of
// every bone below the arm from its parent's (the arm's world is already the aimed one). Bones come after their parents in
// the model (mdb.insert_bone); one that does not keeps the moved world.
void PoseWeapon(Entry& e,int i,char* recs,std::int64_t count,bool attacking) noexcept {
    Entry::ArmAnim& a=e.anim[i];
    if(a.kind==hector::WeaponKind::None)return;
    const int nb=static_cast<int>(a.bone.size());
    int which=2;
    float time=0.0f;
    if(a.kind==hector::WeaponKind::OpenClose)
        which=hector::StepOpenClose(a.oc,attacking,ClipOf(a.clip[0]).Length(),ClipOf(a.clip[1]).Length(),time);
    else
        time=hector::StepRoll(a.roll,attacking,ClipOf(a.clip[2]).Length());
    const hector::Clip clip=ClipOf(a.clip[which]);
    std::vector<int> changed;
    for(const int b:e.below[i]) {
        char* rec=recs+static_cast<std::size_t>(b)*kBoneSize;
        const std::int32_t parent=*reinterpret_cast<const std::int32_t*>(rec+kBoneParentAt);
        if(parent<0 || parent>=count || parent>=b)continue;
        int k=-1;
        for(int j=0;j<nb;++j)
            if(a.bone[static_cast<std::size_t>(j)]==b)k=j;
        const bool underChanged=std::find(changed.begin(),changed.end(),parent)!=changed.end();
        if(k<0 && !underChanged)continue;
        float local[16],world[16];
        if(k>=0) {
            float pose[hector::kKeyFloats];
            hector::SamplePose(clip,nb,k,time,which==2,pose);
            hector::PoseMatrix(pose,local);
        } else {
            std::memcpy(local,rec+kBoneLocalAt,sizeof(local));
        }
        hector::Mul4(local,reinterpret_cast<const float*>(recs+static_cast<std::size_t>(parent)*kBoneSize+kBoneWorldAt),world);
        if(!Finite16(local) || !Finite16(world))continue;
        if(k>=0)std::memcpy(rec+kBoneLocalAt,local,sizeof(local));
        std::memcpy(rec+kBoneWorldAt,world,sizeof(world));
        changed.push_back(b);
    }
}

// The arms' aim, after the base update (see the header). Call with tableLock held.
void AimArms(void* self,Entry& e) noexcept {
    char* skel=static_cast<char*>(self)+kSkeletonAt;
    if(!Readable(skel,0x28))return;
    char* recs=*reinterpret_cast<char**>(skel+0x10);
    const std::int64_t count=*reinterpret_cast<const std::int64_t*>(skel+0x20);
    if(!recs || count<=0 || count>1024 || !Readable(recs,static_cast<std::size_t>(count)*kBoneSize))return;
    const auto parentOf=[&](std::int64_t b) { return *reinterpret_cast<const std::int32_t*>(recs+b*kBoneSize+kBoneParentAt); };
    if(!e.bonesTried) {
        e.bonesTried=true;
        const wchar_t* names[hector::kArms]={L"arms_l",L"arms_r"};
        for(int i=0;i<hector::kArms;++i) {
            e.arm[i]=edf41c::BoneIndex(skel,names[i]);
            e.below[i].clear();
            if(e.arm[i]<0)continue;
            for(std::int64_t b=0;b<count;++b) {                // every bone whose parent chain passes the arm bone
                std::int64_t p=parentOf(b);
                for(int guard=0;p>=0 && p<count && guard<256;++guard,p=parentOf(p))
                    if(p==e.arm[i]) { e.below[i].push_back(static_cast<int>(b)); break; }
            }
        }
        Log("EDF41AI hector %p arm bones %d (%zu below) / %d (%zu below)",self,e.arm[0],e.below[0].size(),e.arm[1],e.below[1].size());
    }
    if(!e.animRead) {
        e.animRead=true;
        if(AnimReady())ReadAnim(self,e,skel);
    }
    const bool dead=*reinterpret_cast<const std::uint8_t*>(static_cast<char*>(self)+kDestroyedAt)!=0;
    for(int i=0;i<hector::kArms;++i) {
        const int bone=e.arm[i];
        if(bone<0 || bone>=count)continue;
        char* rec=recs+static_cast<std::size_t>(bone)*kBoneSize;
        const std::int32_t parent=parentOf(bone);
        float cur[16];
        std::memcpy(cur,rec+kBoneLocalAt,sizeof(cur));
        if(!Finite16(cur) || parent<0 || parent>=count)continue;
        if(!(e.haveWritten[i] && std::memcmp(cur,e.written[i],sizeof(cur))==0))std::memcpy(e.base[i],cur,sizeof(cur));
        if(dead) {                                          // the ragdoll has it: leave the animation's pose
            if(e.haveWritten[i])std::memcpy(rec+kBoneLocalAt,e.base[i],sizeof(cur));
            e.haveWritten[i]=false;
            e.aim[i]=hector::ArmAim{};
            continue;
        }
        float parentWorld[16],animWorld[16];
        std::memcpy(parentWorld,recs+static_cast<std::size_t>(parent)*kBoneSize+kBoneWorldAt,sizeof(parentWorld));
        hector::Mul4(e.base[i],parentWorld,animWorld);
        float pitch=0.0f,yaw=0.0f;
        bool attacking=e.haveTarget && Attacking(self,i);
        if(attacking) {
            float l[3];
            hector::InFrame(animWorld,e.target,l);
            attacking=hector::AimAngles(l,i,pitch,yaw);
            // 4.1 aims back to 0 at the same ease when the target is out of the arm's reach
            if(!attacking) { pitch=0.0f; yaw=0.0f; attacking=true; }
        }
        hector::StepAim(e.aim[i],attacking,pitch,yaw);
        float rot[16],local[16],world[16],built[16],inv[16],move[16];
        hector::AimRotation(e.aim[i].pitch,e.aim[i].yaw,rot);
        hector::Mul4(rot,e.base[i],local);
        hector::Mul4(local,parentWorld,world);
        std::memcpy(built,rec+kBoneWorldAt,sizeof(built));
        if(!Finite16(world) || !Finite16(built) || !hector::InverseAffine(built,inv))continue;
        hector::Mul4(inv,world,move);                       // the arm as built -> the arm as aimed, for the bones below
        for(const int b:e.below[i]) {
            float* w=reinterpret_cast<float*>(recs+static_cast<std::size_t>(b)*kBoneSize+kBoneWorldAt);
            float moved[16];
            hector::Mul4(w,move,moved);
            if(Finite16(moved))std::memcpy(w,moved,sizeof(moved));
        }
        std::memcpy(rec+kBoneWorldAt,world,sizeof(world));
        PoseWeapon(e,i,recs,count,e.haveTarget && Attacking(self,i));
        std::memcpy(rec+kBoneLocalAt,local,sizeof(local));
        std::memcpy(e.written[i],local,sizeof(local));
        e.haveWritten[i]=true;
    }
}

std::uint64_t __fastcall BaseUpdateHook(void* self,void* dt) {
    Tick(self);
    const std::uint64_t result=reinterpret_cast<BaseUpdateFn>(image+kBaseUpdate)(self,dt);
    std::lock_guard<std::mutex> hold(tableLock);
    if(Entry* e=Find(self)) {
        AimArms(self,*e);
        if(AnimReady() && !*reinterpret_cast<const std::uint8_t*>(static_cast<char*>(self)+kDestroyedAt))StepHead(self,*e);
    }
    return result;
}

bool __fastcall MoveToHook(void* self,const float* goal,float a,float b,float speed,float c) {
    float aim[4];
    {
        std::lock_guard<std::mutex> hold(tableLock);
        const Entry* e=Find(self);
        if(e) {
            const hector::Walk walk=hector::Steer(e->cycle,e->cfg,e->dist,e->haveTarget);
            speed=walk.speed;
            if(walk.takeover) {
                const float* me=reinterpret_cast<const float*>(static_cast<char*>(self)+kPosAt);
                if(walk.retreat) {              // the point opposite the target: backward, still facing it
                    aim[0]=2.0f*me[0]-e->target[0]; aim[1]=me[1]; aim[2]=2.0f*me[2]-e->target[2];
                } else {                        // straight at the target
                    aim[0]=e->target[0]; aim[1]=e->target[1]; aim[2]=e->target[2];
                }
                aim[3]=goal[3];
                goal=aim;
                a=0.0f; b=0.0f;                 // 4.1 walks at full speed to the end (no slow-down ramp)
            }
        }
    }
    return reinterpret_cast<MoveToFn>(image+kMoveTo)(self,goal,a,b,speed,c);
}

void __fastcall GunStartHook(void* gun) {
    void* owner=*reinterpret_cast<void**>(static_cast<char*>(gun)+kGunOwnerAt);
    unsigned mask=hector::kArmLeft|hector::kArmRight;
    if(owner) {
        std::lock_guard<std::mutex> hold(tableLock);
        Entry* e=Find(owner);
        const char* base=static_cast<char*>(owner);
        void** guns=*reinterpret_cast<void***>(const_cast<char*>(base)+kGunsAt);
        const std::uint64_t count=*reinterpret_cast<const std::uint64_t*>(base+kGunCountAt);
        if(e && e->haveTarget && count==hector::kArms && guns) {
            int index=-1;
            for(int i=0;i<hector::kArms;++i)
                if(guns[2*i]==gun)index=i;
            if(e->pickTick!=e->tick) {          // one pick per attack: the loop starts the arms in the same tick
                e->pickTick=e->tick;
                e->pickMask=hector::PickArms(e->cfg,e->rng,e->dist);
                Log("EDF41AI gate %p dist %.1f ranges %.0f/%.0f -> arms %s%s%s",owner,e->dist,e->cfg.range[0],e->cfg.range[1],
                    (e->pickMask&hector::kArmLeft) ? "L" : "",(e->pickMask&hector::kArmRight) ? "R" : "",e->pickMask ? "" : "none");
            }
            if(index>=0)mask=e->pickMask;
            if(index>=0 && !(mask&(1u<<index)))return;
        }
    }
    reinterpret_cast<GunStartFn>(image+kGunStart)(gun);
}
}  // namespace

bool InstallEdf41Ai() noexcept {
    if(!Cfg().edf41Ai) {
        Log("EDF41AI off (ini Edf41Ai=0)");
        return false;
    }
    bool changed=false;
    bool ok=RedirectCall(image+kBaseUpdateSite,image+kBaseUpdate,reinterpret_cast<void*>(&BaseUpdateHook),changed);
    int walks=0,guns=0;
    for(const std::size_t site:kMoveToSites)
        walks+=RedirectCall(image+site,image+kMoveTo,reinterpret_cast<void*>(&MoveToHook),changed) ? 1 : 0;
    for(const std::size_t site:kGunStartSites)
        guns+=RedirectCall(image+site,image+kGunStart,reinterpret_cast<void*>(&GunStartHook),changed) ? 1 : 0;
    Log("EDF41AI hooked: update=%d walk=%d/2 gun start=%d/2",ok ? 1 : 0,walks,guns);
    return ok && walks==2 && guns==2;
}
}  // namespace crew
