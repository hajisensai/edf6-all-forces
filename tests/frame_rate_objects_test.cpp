// The other objects' layer of the unlimited frame rate (src/frame_rate_objects.cpp, docs/unlimited-fps-re.md §4.3.3) run on
// heap fixtures laid out as the file reads them: System (+0x448 update list, +0x458 count, +0xCE0 tick count), objects
// (+0x18 flags, +0x28 / +0x30 weak-this, control blocks with use +8 / weak +0xC), models (+0xA0 the skeleton instance:
// +0x20 bone count, +0x30 poses), seats (+0x608 / +0x618), soldier weapons (+0x1950 / +0x1960) and vehicle trigger
// weapons (+0x638 / +0x648, stride 0x48, +8 control block, +0x10 the weapon; models at +0xF30). The thermal model lookups
// are stand-ins (thermal_models_test covers them); Readable is the real one, counted: the sample asks it a fixed number of
// times however many objects are listed (the review's 4.74 ms a step at stress:30 were all Readable).
#include "../src/frame_rate_objects.cpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}

namespace thermal {
std::map<const void*,std::vector<const void*>> modelsOf;
std::map<const void*,Family> familyOf;
std::set<const void*> animationModels;
std::size_t EnemyModelsLive(const void* object,const void** out,std::size_t capacity) noexcept {
    const auto it=modelsOf.find(object);
    if(it==modelsOf.end())return 0;
    std::size_t n=0;
    for(const void* m:it->second)if(n<capacity)out[n++]=m;
    return n;
}
const void* EnemyModelLive(const void* object) noexcept {
    const void* out[1];
    return EnemyModelsLive(object,out,1) ? out[0] : nullptr;
}
Family EnemyFamilyLive(const void* object) noexcept {
    const auto it=familyOf.find(object);
    return it==familyOf.end() ? Family::kNone : it->second;
}
bool IsAnimationModel(const void* model) noexcept { return animationModels.count(model)!=0; }
}  // namespace thermal

namespace {
int checks=0,failures=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}

// Heap blocks kept for the whole run (Readable's page cache never sees them freed).
unsigned char* Block(std::size_t n) {
    auto* p=static_cast<unsigned char*>(_aligned_malloc(n,16));
    std::memset(p,0,n);
    return p;
}
template<class T> void Put(unsigned char* p,std::size_t off,T v) { std::memcpy(p+off,&v,sizeof(T)); }
template<class T> T Get(const unsigned char* p,std::size_t off) { T v;std::memcpy(&v,p+off,sizeof(T));return v; }

int freedBlocks=0;
void __fastcall CtrlDestroy(void*) {}
void __fastcall CtrlFree(void*) { ++freedBlocks; }
void* const ctrlVt[2]={reinterpret_cast<void*>(&CtrlDestroy),reinterpret_cast<void*>(&CtrlFree)};

// A control block as the object's own shared_ptr keeps it: one use, the uses' one weak.
unsigned char* Ctrl() {
    unsigned char* c=Block(16);
    Put<const void*>(c,0,ctrlVt);
    Put<std::int32_t>(c,8,1);
    Put<std::int32_t>(c,0xC,1);
    return c;
}
std::int32_t Weaks(const unsigned char* c) { return Get<std::int32_t>(c,0xC); }

float* Translation(float x,float y,float z) {
    float* m=static_cast<float*>(_aligned_malloc(64,16));
    std::memset(m,0,64);
    m[0]=m[5]=m[10]=m[15]=1;m[12]=x;m[13]=y;m[14]=z;
    return m;
}
void SetX(float* bone,float x) { bone[12]=x; }

// A model with one skeleton instance of `bones` bones, each a translation (bone k at x = k).
struct Model { unsigned char* mem;float* pose;std::uint64_t bones; };
Model MakeModel(unsigned char* at,std::uint64_t bones) {
    Model m{at,static_cast<float*>(_aligned_malloc(bones*64,16)),bones};
    for(std::uint64_t k=0;k<bones;++k) {
        std::memset(m.pose+k*16,0,64);
        m.pose[k*16]=m.pose[k*16+5]=m.pose[k*16+10]=m.pose[k*16+15]=1;
        m.pose[k*16+12]=static_cast<float>(k);
    }
    Put<std::uint64_t>(at,kModelInst+kInstCount,bones);
    Put<float*>(at,kModelInst+kInstPose,m.pose);
    return m;
}

// An object with one model of its own (kept in its own block, past the fields read).
constexpr std::size_t kObjSize=0x2000,kOwnModel=0x1A00;
struct Obj { unsigned char* mem;unsigned char* ctrl;Model model; };
Obj MakeObj(thermal::Family family,std::uint64_t bones=2,const void* vt=nullptr) {
    Obj o{Block(kObjSize),Ctrl(),{}};
    Put<const void*>(o.mem,0,vt ? vt : reinterpret_cast<const void*>(0x1000));
    Put<const void*>(o.mem,kSelf,o.mem);
    Put<const void*>(o.mem,kSelfCtrl,o.ctrl);
    o.model=MakeModel(o.mem+kOwnModel,bones);
    thermal::modelsOf[o.mem]={o.model.mem};
    thermal::familyOf[o.mem]=family;
    return o;
}
// The root x the object's last tick left (its pose[0]).
void Root(const Obj& o,float x) { SetX(o.model.pose,x); }

// System and its update list.
unsigned char* sys=Block(0x1000);
const void* sysSlot=nullptr;
std::vector<const void*> list;
void List(std::initializer_list<const Obj*> objs) {
    list.clear();
    for(const Obj* o:objs)list.push_back(o->mem);
    Put<const void* const*>(sys,kUpdateList,list.data());
    Put<std::uint64_t>(sys,kUpdateCount,list.size());
}
std::uint32_t tickNow=0;
// A tick (the roots the test set are what it left), alone (TickOnly), or with the next frame's render recording, a frame
// with ticks (Tick: its sample). A frame without ticks (Frame): its recording's sample, then its poses.
void TickOnly() { Put<std::uint32_t>(sys,kTickCount,++tickNow); }
void Tick() { TickOnly();frameobjects::Sample(false); }
void Frame(float alpha,const void* rootHuman,const void* rootVehicle,const float* rootD) {
    frameobjects::Sample(true);
    frameobjects::Pose(alpha,rootHuman,rootVehicle,rootD);
}

frameobjects::Counts Take() { frameobjects::Counts c;frameobjects::TakeCounts(c);return c; }
// A scene whose frames without ticks already come (the sample not idle: Idle() has the window).
void NewScene() {
    frameobjects::SceneOver();
    ticksSinceUnticked=0;
    Take();
}
unsigned LeftBy(const frameobjects::Counts& c,frameobjects::Left why) { return c.left[static_cast<int>(why)]; }
bool NearX(const float* bone,float x) { return std::fabs(bone[12]-x)<1e-4f; }

// Two ticks of `o` from x0 to x1 (its bones moving with it), then a frame without ticks at alpha.
void Steps(std::initializer_list<const Obj*> objs,float x0,float x1) {
    for(const Obj* o:objs)Root(*o,x0);
    Tick();
    for(const Obj* o:objs)Root(*o,x1);
    Tick();
}

void WeakCounts() {
    const Obj o=MakeObj(thermal::Family::kOther);
    unsigned char* c=TakeWeak(o.mem);
    Check(c==o.ctrl && Weaks(o.ctrl)==2,"TakeWeak: the object's own block, one weak more");
    DropWeak(c);
    Check(Weaks(o.ctrl)==1 && !freedBlocks,"DropWeak: given back, the block kept while the uses' weak holds it");
    Put<const void*>(o.mem,kSelf,nullptr);
    Check(!TakeWeak(o.mem) && Weaks(o.ctrl)==1,"TakeWeak: not whole (weak-this not itself): nothing taken");
    Put<const void*>(o.mem,kSelf,o.mem);
    Put<std::int32_t>(o.ctrl,8,0);
    Check(!TakeWeak(o.mem) && Weaks(o.ctrl)==1,"TakeWeak: no use left: nothing taken");
    Put<std::int32_t>(o.ctrl,8,1);
    c=TakeWeak(o.mem);
    Put<std::int32_t>(o.ctrl,8,0);Put<std::int32_t>(o.ctrl,0xC,1);   // the object freed, its uses' weak given back
    DropWeak(c);
    Check(Weaks(o.ctrl)==0 && freedBlocks==1,"DropWeak: the last weak frees the block (vtable slot 1)");
}

void BlendAndRestore() {
    NewScene();
    const Obj a=MakeObj(thermal::Family::kOther,3);
    List({&a});
    Steps({&a},0,1);
    // The bones move with the root: bone k at root + k.
    for(std::uint64_t k=1;k<a.model.bones;++k)SetX(a.model.pose+k*16,1.0f+static_cast<float>(k));
    Frame(0.25f,nullptr,nullptr,nullptr);
    Check(NearX(a.model.pose,0.25f) && NearX(a.model.pose+16,1.25f) && NearX(a.model.pose+32,2.25f),
          "pose: every bone times D (the root blended to alpha 0.25)");
    auto c=Take();
    Check(c.posed==1 && !c.AsStepped() && c.objects==1,"pose: counted blended");
    // Someone writes bone 2 between the write and the restore: theirs stays, the others go back.
    SetX(a.model.pose+32,7.0f);
    frameobjects::Restore();
    Check(NearX(a.model.pose,1.0f) && NearX(a.model.pose+16,2.0f) && NearX(a.model.pose+32,7.0f),
          "restore: bone by bone, a bone written since is left as written");
    // The object freed before the restore: nothing goes back.
    Root(a,2);SetX(a.model.pose+16,3);SetX(a.model.pose+32,4);
    Tick();
    Frame(0.5f,nullptr,nullptr,nullptr);
    Check(NearX(a.model.pose,1.5f),"pose: the next tick's pair");
    Put<std::int32_t>(a.ctrl,8,0);
    frameobjects::Restore();
    Check(NearX(a.model.pose,1.5f),"restore: a freed object (its block's uses 0) is not written");
    Put<std::int32_t>(a.ctrl,8,1);
    // Another object at the address (weak-this another block) since the write: not written either.
    Root(a,2);Tick();Root(a,3);Tick();
    Frame(0.5f,nullptr,nullptr,nullptr);
    unsigned char* other=Ctrl();
    Put<const void*>(a.mem,kSelfCtrl,other);
    frameobjects::Restore();
    Check(NearX(a.model.pose,2.5f),"restore: another object at the address is not written");
    Put<const void*>(a.mem,kSelfCtrl,a.ctrl);
    frameobjects::SceneOver();
    Check(Weaks(a.ctrl)==1,"scene over: the weak given back");
    Take();
}

void Reasons() {
    NewScene();
    const Obj one=MakeObj(thermal::Family::kOther),late=MakeObj(thermal::Family::kOther),
              swapped=MakeObj(thermal::Family::kOther),gone=MakeObj(thermal::Family::kOther),
              jump=MakeObj(thermal::Family::kOther),still=MakeObj(thermal::Family::kOther);
    List({&late,&swapped,&gone,&jump,&still});
    Root(late,0);Root(swapped,0);Root(gone,0);Root(jump,0);Root(still,5);
    Tick();
    List({&one,&late,&swapped,&gone,&jump,&still});
    Root(one,0);Root(late,1);Root(swapped,1);Root(gone,1);Root(jump,100);
    Tick();
    SetX(late.model.pose,1.5f);                                      // written after the sample
    Put<const void*>(swapped.mem,0,reinterpret_cast<const void*>(0x2000));   // another class at the address
    Put<std::int32_t>(gone.ctrl,8,0);                                // freed
    Frame(0.5f,nullptr,nullptr,nullptr);
    auto c=Take();
    Check(LeftBy(c,Left::kSamples)==1,"left: one tick seen (samples)");
    Check(LeftBy(c,Left::kPose)==1,"left: the root written after the sample (pose)");
    Check(LeftBy(c,Left::kVtable)==1,"left: another class at the address (vtable)");
    Check(LeftBy(c,Left::kGone)==1,"left: freed (gone)");
    Check(LeftBy(c,Left::kRefused)==1 && c.refused[static_cast<int>(framerate::Refusal::kMove)]==1,
          "left: a move over kCutMove in a tick (refused, by its reason)");
    Check(c.still==1 && !c.posed && c.AsStepped()==5,"still: nothing moved; the five as stepped by reason");
    frameobjects::Restore();
    // A tick without the camera step's sample: every body left as stepped.
    Put<std::uint32_t>(sys,kTickCount,++tickNow);
    frameobjects::Pose(0.5f,nullptr,nullptr,nullptr);
    c=Take();
    Check(LeftBy(c,Left::kUnsampled)==6 && !c.posed,"left: a tick without its sample (unsampled)");
    Put<std::int32_t>(gone.ctrl,8,1);
    frameobjects::SceneOver();
    Take();
}

void Forgetting() {
    NewScene();
    const Obj a=MakeObj(thermal::Family::kOther),b=MakeObj(thermal::Family::kOther),d=MakeObj(thermal::Family::kOther);
    List({&a,&b,&d});
    Steps({&a,&b,&d},0,1);
    Check(bodies.size()==3 && Weaks(b.ctrl)==2,"sample: three bodies, each holding a weak");
    List({&a,&d});
    Root(a,2);Root(d,2);
    Tick();
    Check(bodies.size()==2 && Weaks(b.ctrl)==1 && bodies[1].object==d.mem && bodyIndex.Find(d.mem)==1 &&
          bodyIndex.Find(b.mem)<0,"forget: not listed, swap-removed, its weak given back, the index follows");
    Frame(0.5f,nullptr,nullptr,nullptr);
    Check(NearX(d.model.pose,1.5f) && NearX(a.model.pose,1.5f),"forget: the moved body keeps its samples");
    frameobjects::Restore();
    // Listed twice in one tick: one body.
    List({&a,&d,&a});
    Tick();
    Check(bodies.size()==2 && Weaks(a.ctrl)==2,"sample: listed twice, sampled once");
    // Being deleted (+0x18 bit 4), or without a model: not sampled, forgotten.
    Put<unsigned char>(a.mem,kObjFlags,kObjDeleted);
    thermal::modelsOf[d.mem].clear();
    Tick();
    Check(bodies.empty() && Weaks(a.ctrl)==1 && Weaks(d.ctrl)==1,"sample: deleted or modelless objects forgotten");
    Put<unsigned char>(a.mem,kObjFlags,0);
    thermal::modelsOf[d.mem]={d.model.mem};
    Take();
}

void RidersAndWeapons() {
    NewScene();
    const Obj vehicle=MakeObj(thermal::Family::kVehicle);
    const void* soldierVt=image+kSoldierVts[0];
    const Obj rider=MakeObj(thermal::Family::kHuman,2,soldierVt);
    // The seat: rider +0x260, its control block +0x268.
    unsigned char* seats=Block(kSeatStride*2);
    Put<unsigned char*>(vehicle.mem,kSeats,seats);
    Put<std::uint64_t>(vehicle.mem,kSeatCount,2);
    Put<const void*>(seats+kSeatStride,kSeatRider,rider.mem);
    Put<const void*>(seats+kSeatStride,kSeatRiderCtrl,rider.ctrl);
    // The rider's weapon (+0x1950 list, +0x1960 count), its model +0xF30.
    unsigned char* gun=Block(kWeaponModel+0x300);
    const Model gunModel=MakeModel(gun+kWeaponModel,1);
    unsigned char** guns=reinterpret_cast<unsigned char**>(Block(16));
    guns[0]=gun;
    Put<unsigned char**>(rider.mem,kWeapons,guns);
    Put<std::int32_t>(rider.mem,kWeaponCount,1);
    // The vehicle's trigger weapons: holder 0 a registered AnimationModel, holder 1 not.
    unsigned char* holders=Block(kHolderStride*2);
    unsigned char* cannon=Block(kWeaponModel+0x300);unsigned char* other=Block(kWeaponModel+0x300);
    const Model cannonModel=MakeModel(cannon+kWeaponModel,1),otherModel=MakeModel(other+kWeaponModel,1);
    unsigned char* cannonCtrl=Ctrl();unsigned char* otherCtrl=Ctrl();
    Put<unsigned char*>(holders,kHolderCtrl,cannonCtrl);Put<unsigned char*>(holders,kHolderWeapon,cannon);
    Put<unsigned char*>(holders+kHolderStride,kHolderCtrl,otherCtrl);Put<unsigned char*>(holders+kHolderStride,kHolderWeapon,other);
    Put<unsigned char*>(vehicle.mem,kHolders,holders);
    Put<std::uint64_t>(vehicle.mem,kHolderCount,2);
    thermal::animationModels.insert(cannon+kWeaponModel);

    List({&vehicle,&rider});
    Root(rider,5);
    Steps({&vehicle},0,1);
    Frame(0.5f,nullptr,nullptr,nullptr);
    auto c=Take();
    Check(NearX(vehicle.model.pose,0.5f),"vehicle: blended");
    Check(NearX(rider.model.pose,4.5f) && NearX(gunModel.pose,-0.5f),
          "rider: the vehicle's D (not its own: it did not move), its weapon with it");
    Check(NearX(cannonModel.pose,-0.5f) && NearX(otherModel.pose,0.0f),
          "vehicle weapons: a registered AnimationModel moves with the vehicle, another +0xF30 is not written");
    Check(c.posed==2 && c.riders==1 && !c.AsStepped(),"rider counted once, with the vehicle");
    // The rider leaves the seat and the cannon's holder empties before the restore: theirs are not written back.
    Put<const void*>(seats+kSeatStride,kSeatRider,nullptr);
    Put<unsigned char*>(holders,kHolderWeapon,nullptr);
    frameobjects::Restore();
    Check(NearX(vehicle.model.pose,1.0f),"restore: the vehicle back");
    Check(NearX(rider.model.pose,4.5f) && NearX(gunModel.pose,-0.5f),"restore: a rider no longer seated is not written");
    Check(NearX(cannonModel.pose,-0.5f),"restore: a holder no longer holding the weapon is not written");
    // Seated again, the holder's control block another: the rider goes back, the cannon does not.
    Put<const void*>(seats+kSeatStride,kSeatRider,rider.mem);
    Put<unsigned char*>(holders,kHolderWeapon,cannon);
    Root(rider,5);SetX(gunModel.pose,0);SetX(cannonModel.pose,0);
    Root(vehicle,2);
    Tick();
    Frame(0.5f,nullptr,nullptr,nullptr);
    Put<unsigned char*>(holders,kHolderCtrl,otherCtrl);
    frameobjects::Restore();
    Check(NearX(rider.model.pose,5.0f) && NearX(gunModel.pose,0.0f),"restore: the seated rider and their weapon back");
    Check(NearX(cannonModel.pose,-0.5f),"restore: a holder with another control block is not written");
    Put<unsigned char*>(holders,kHolderCtrl,cannonCtrl);
    SetX(cannonModel.pose,0);
    // The player's vehicle (fps-7's): its riders and its trigger weapons (+0x638) take fps-7's D; without it they are left
    // as stepped with it.
    Root(vehicle,3);Tick();
    const float* fps7=Translation(-0.25f,0,0);
    Take();
    Frame(0.75f,nullptr,vehicle.mem,fps7);
    c=Take();
    Check(NearX(vehicle.model.pose,3.0f) && NearX(rider.model.pose,4.75f) && c.riders==1,
          "the player's vehicle: left to fps-7, its riders with fps-7's D");
    Check(NearX(cannonModel.pose,-0.25f) && !c.AsStepped(),"the player's vehicle: its trigger weapon with fps-7's D");
    frameobjects::Restore();
    Check(NearX(cannonModel.pose,0.0f),"the player's vehicle: its trigger weapon back after the frame");
    Frame(0.75f,nullptr,vehicle.mem,nullptr);
    c=Take();
    Check(NearX(rider.model.pose,5.0f) && LeftBy(c,Left::kRefused)==1 && !c.posed,
          "the player's vehicle refused: its rider as stepped (refused)");
    Check(NearX(cannonModel.pose,0.0f),"the player's vehicle refused: its trigger weapon as stepped with it");
    frameobjects::Restore();
    frameobjects::SceneOver();
    Take();
}

// Readable asks of a sample and a frame's poses: the same for 20 objects and 2000 (System and its list only).
struct Cost { unsigned long long sampleAsks,poseAsks;double sampleMs; };
Cost Run(int n) {
    NewScene();
    static std::vector<Obj> pool;
    while(static_cast<int>(pool.size())<n)pool.push_back(MakeObj(thermal::Family::kOther,8));
    list.clear();
    for(int i=0;i<n;++i)list.push_back(pool[static_cast<std::size_t>(i)].mem);
    Put<const void* const*>(sys,kUpdateList,list.data());
    Put<std::uint64_t>(sys,kUpdateCount,list.size());
    for(int i=0;i<n;++i)Root(pool[static_cast<std::size_t>(i)],0);
    Tick();
    for(int i=0;i<n;++i)Root(pool[static_cast<std::size_t>(i)],1);
    edf::TakeReadableStats();
    const auto t0=std::chrono::steady_clock::now();
    Tick();
    const auto t1=std::chrono::steady_clock::now();
    Cost cost{edf::TakeReadableStats().asks,0,std::chrono::duration<double,std::milli>(t1-t0).count()};
    Frame(0.5f,nullptr,nullptr,nullptr);
    cost.poseAsks=edf::TakeReadableStats().asks;
    const auto c=Take();
    Check(c.posed==static_cast<unsigned>(n) && c.objects==static_cast<unsigned>(n),"cost: every object posed");
    frameobjects::Restore();
    return cost;
}
void Costs() {
    const Cost few=Run(20),many=Run(2000);
    std::printf("Readable asks: sample %llu (20 objects) / %llu (2000), poses %llu / %llu; sample of 2000 %.3f ms\n",
                few.sampleAsks,many.sampleAsks,few.poseAsks,many.poseAsks,many.sampleMs);
    Check(few.sampleAsks==many.sampleAsks && many.sampleAsks<=4,"cost: the sample asks Readable a fixed few times");
    Check(few.poseAsks==many.poseAsks && many.poseAsks<=4,"cost: a frame's poses ask Readable a fixed few times");
    frameobjects::SceneOver();
    Take();
}

// The sample only while frames without ticks come (frameobjects::kIdleTicks): none yet, none for the window, the gate not
// in (Pose never called): skipped and counted; the first frame without ticks after it poses nothing, counts every object
// known left for its samples, and the sample resumes (two ticks to blend again).
void Idle() {
    frameobjects::SceneOver();Take();
    const Obj a=MakeObj(thermal::Family::kOther),b=MakeObj(thermal::Family::kOther);
    List({&a,&b});
    auto both=[&](float x){Root(a,x);Root(b,x);};
    for(unsigned i=0;i<frameobjects::kIdleTicks+5;++i){both(static_cast<float>(i));Tick();}
    auto c=Take();
    Check(bodies.empty() && c.idleTicks==frameobjects::kIdleTicks+5 && Weaks(a.ctrl)==1,
          "idle: a scene without a frame without ticks (stress:30, or the gate not in) samples nothing");
    // The first frame without ticks: its recording is the first after the last tick, and samples it at once.
    both(0);TickOnly();
    Frame(0.5f,nullptr,nullptr,nullptr);
    c=Take();
    Check(!c.posed && !c.idleTicks && bodies.size()==2 && LeftBy(c,frameobjects::Left::kSamples)==2 &&
          !LeftBy(c,frameobjects::Left::kUnsampled),
          "idle: the first frame without ticks samples at once; one tick seen, nothing posed yet");
    both(1);Tick();
    Frame(0.5f,nullptr,nullptr,nullptr);
    c=Take();
    Check(c.posed==2 && !c.idleTicks,"idle: blended from the next tick on");
    frameobjects::Restore();
    // In the window: kIdleTicks ticks sampled after the last frame without ticks, then skipped.
    for(unsigned i=0;i<frameobjects::kIdleTicks;++i){both(2.0f+static_cast<float>(i));Tick();}
    c=Take();
    Check(!c.idleTicks && bodies.size()==2 && bodies[0].samples==2,"idle: the window's ticks sampled");
    both(100);Tick();
    c=Take();
    Check(c.idleTicks==1 && bodies.size()==2,"idle: the tick after the window skipped, the objects known kept");
    both(101);TickOnly();
    Frame(0.5f,nullptr,nullptr,nullptr);
    c=Take();
    Check(!c.posed && LeftBy(c,frameobjects::Left::kSamples)==2 && c.AsStepped()==2,
          "idle: the frame without ticks that resumes samples, the tick before it was not: every object under samples");
    both(102);Tick();
    Frame(0.5f,nullptr,nullptr,nullptr);
    c=Take();
    Check(c.posed==2,"idle: two consecutive samples: blended again");
    frameobjects::Restore();
    frameobjects::SceneOver();
    both(0);Tick();
    c=Take();
    Check(c.idleTicks==1 && bodies.empty(),"idle: a new scene starts idle");

    // Several recordings a tick (two viewports, or frames without ticks that do not pose): each tick counts once.
    frameobjects::SceneOver();Take();
    Frame(0.5f,nullptr,nullptr,nullptr);   // a frame without ticks: the window opens
    Take();
    for(unsigned i=0;i<frameobjects::kIdleTicks;++i){both(static_cast<float>(i));Tick();frameobjects::Sample(false);}
    c=Take();
    Check(!c.idleTicks && bodies.size()==2 && bodies[0].samples==2,
          "two calls a tick: the window is kIdleTicks ticks, not half of it");
    both(200);Tick();frameobjects::Sample(false);
    c=Take();
    Check(c.idleTicks==1,"two calls a tick: a skipped tick counted once");
    TickOnly();TickOnly();frameobjects::Sample(false);
    c=Take();
    Check(c.idleTicks==2,"a recording after two ticks: both counted");
    frameobjects::SceneOver();Take();
}

void Off() {
    // Before Install nothing runs (a fresh state: `on` false).
    on=false;
    const Obj a=MakeObj(thermal::Family::kOther);
    List({&a});
    Tick();
    Check(bodies.empty() && Weaks(a.ctrl)==1,"off: Sample does nothing");
    frameobjects::SceneOver();
    Frame(0.5f,nullptr,nullptr,nullptr);
    Check(!Take().posed,"off: Pose does nothing");
}
}  // namespace
}  // namespace crew

int main() {
    using namespace crew;
    // image+0x20B2958 (System's slot) is sysSlot.
    image=reinterpret_cast<unsigned char*>(reinterpret_cast<std::uintptr_t>(&sysSlot)-scene::kObjectManager);
    sysSlot=sys;
    Off();
    frameobjects::Install();
    Check(on && bodies.capacity()>=kFirstBodies && posedAfter.capacity()>=kFirstPoseFloats,"install: the tables reserved");
    WeakCounts();
    BlendAndRestore();
    Reasons();
    Forgetting();
    RidersAndWeapons();
    Costs();
    Idle();
    std::printf("frame_rate_objects: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
