// The frame rate view's state machine (src/frame_rate_view.cpp, docs/unlimited-fps-re.md §4.3) run on heap fixtures laid
// out as the file reads them: the camera (+0x220 world, +0x24 field, +0x360 soldier), the soldier, the vehicle, their
// models, skeleton instances and pose arrays. The camera step's and the frame end fence's game calls (image+kCameraStep,
// image+kFence) land on local stand-ins: `image` is set so that image+rva is the stand-in for the call being made.
// Each frame runs as the game does: BeginFrame, the projection (BeforeProject), the steps (CameraStepHook), the fence
// (FrameEndHook).
#include "../src/frame_rate_view.cpp"
#include <cmath>
#include <cstdio>

namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
const unsigned char* riddenHuman=nullptr;   // the soldier HumanOnFoot says is riding
bool HumanOnFoot(const unsigned char* human) noexcept { return human!=riddenHuman; }
namespace scopelens { bool SetBeforeProject(BeforeProjectFn) noexcept { return false; } }

namespace {
int checks=0,failures=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}

using fpview::M4;
M4 Pose(float yaw,float x,float y,float z) noexcept {
    M4 r=fpview::Identity();
    const float c=std::cos(yaw),s=std::sin(yaw);
    r.m[0]=c;r.m[2]=-s;r.m[8]=s;r.m[10]=c;
    r.m[12]=x;r.m[13]=y;r.m[14]=z;
    return r;
}
bool Near(const float* a,const float* b,float tolerance=1e-4f) noexcept {
    for(int i=0;i<16;++i)if(!(std::fabs(a[i]-b[i])<=tolerance))return false;
    return true;
}
bool Same(const void* a,const void* b,std::size_t n) noexcept { return !std::memcmp(a,b,n); }

// The game's calls behind the hooks: the camera step writes what the test has queued for that camera.
struct Queued { const void* camera; M4 world; float fov; bool writes; };
Queued queued[4]{};
int fenceCalls=0;
void StandInStep(void* camera,void*) {
    for(const Queued& q:queued)
        if(q.camera==camera && q.writes) {
            std::memcpy(static_cast<unsigned char*>(camera)+kWorld,q.world.m,64);
            std::memcpy(static_cast<unsigned char*>(camera)+kFov,&q.fov,4);
        }
}
void* StandInFence(void*,void*,void*,void*) { ++fenceCalls;return nullptr; }
unsigned char* ImageFor(std::uintptr_t function,unsigned rva) noexcept {
    return reinterpret_cast<unsigned char*>(function-rva);
}
unsigned char* stepImage=nullptr;

// Fixtures. Kept for the whole run (Readable's page cache never sees them freed).
alignas(16) unsigned char cam[0x400]{},cam2[0x400]{};
alignas(16) unsigned char human[0x2000]{},human2[0x2000]{},vehicle[0x1200]{},vehicle2[0x1200]{},weapon[0x1200]{};
alignas(16) unsigned char lodInst[0x40]{},lodInst2[0x40]{},lods[2*kLodStride]{};
alignas(16) unsigned char ctrlA[16]{},ctrlB[16]{},ctrlC[16]{},ctrlD[16]{};
constexpr int kBones=3;
alignas(16) float bodyPose[kBones*16],body2Pose[kBones*16],lodPose[kBones*16],lod2Pose[kBones*16],weaponPose[kBones*16],
                  vehiclePose[kBones*16],vehicle2Pose[kBones*16],sparePose[kBones*16];
const unsigned char* weaponList[1]={weapon};

void Queue(const void* camera,const M4& world,float fov=1.0f) {
    for(Queued& q:queued)if(q.camera==camera || !q.camera){q={camera,world,fov,true};return;}
}
// One frame of the game's main loop. `steps`: the cameras stepped (in order) on a frame with ticks.
void BeginTick(unsigned ticks) { framerateview::BeginFrame(ticks,1.0f); }
void Step(void* camera) {
    image=stepImage;
    CameraStepHook(camera,nullptr);
}
void Project(void* camera) { BeforeProject(camera,nullptr); }
void Fence() {
    image=ImageFor(reinterpret_cast<std::uintptr_t>(&StandInFence),kFence);
    FrameEndHook(nullptr,nullptr,nullptr,nullptr);
    image=stepImage;
}
// A tick frame that steps `camera` to `world`.
void TickTo(void* camera,const M4& world,float fov=1.0f) {
    BeginTick(1);
    Project(camera);
    Queue(camera,world,fov);
    Step(camera);
    Fence();
}
const float* World(const void* camera) noexcept { return reinterpret_cast<const float*>(static_cast<const unsigned char*>(camera)+kWorld); }
float Fov(const void* camera) noexcept { return At<float>(camera,kFov); }

struct Counts { unsigned blended,unblended,roots,rootsLeft,rootsRefused; };
Counts Counted() noexcept {
    Counts c{};
    framerateview::TakeCounts(c.blended,c.unblended,c.roots,c.rootsLeft,c.rootsRefused);
    return c;
}

void Reset() {
    RestoreFrame();
    for(Camera& c:cameras)c=Camera{};
    root=Root{};
    alphaNow=1.0f;
    on=true;
    riddenHuman=nullptr;
    for(Queued& q:queued)q=Queued{};
    std::memset(cam,0,sizeof(cam));std::memset(cam2,0,sizeof(cam2));
    Counted();
}

// A skeleton instance: the bone count at +0x20, the pose array at +0x30.
void Instance(unsigned char* inst,float* pose,std::uint64_t count) {
    Put<std::uint64_t>(inst,kInstCount,count);
    Put<float*>(inst,kInstPose,pose);
}
// Bones b = local_b · root, local_b a lift by b metres (b 0: the root itself).
void WritePose(float* pose,const M4& rootM,float lift=0.0f) {
    for(int b=0;b<kBones;++b) {
        const M4 w=fpview::Mul(fpview::Translate(0.0f,lift+static_cast<float>(b),0.0f),rootM);
        std::memcpy(pose+b*16,w.m,64);
    }
}
void Soldier(unsigned char* h,float* pose,const void* ctrl) {
    std::memset(h,0,0x2000);
    Put<const void*>(h,0,stepImage+kSoldierVts[0]);
    Put<const void*>(h,kSelfCtrl,ctrl);
    Instance(h+kBodyModel+kModelInst,pose,kBones);
}
void Vehicle(unsigned char* v,float* pose,const void* ctrl) {
    std::memset(v,0,sizeof(vehicle));
    Put<const void*>(v,kSelfCtrl,ctrl);
    Instance(v+kVehicleModel+kModelInst,pose,kBones);
}
// The full on-foot fixture: body with a base LOD instance and a LOD table (its first entry the base LOD's again), one
// weapon. The camera follows the soldier.
void OnFoot(const M4& rootM) {
    Soldier(human,bodyPose,ctrlA);
    Instance(lodInst,lodPose,kBones);
    Instance(lodInst2,lod2Pose,kBones);
    unsigned char* const model=human+kBodyModel;
    Put<const void*>(model,kBaseLod+kLodInst,lodInst);
    std::memset(lods,0,sizeof(lods));
    Put<const void*>(lods,kLodInst,lodInst);
    Put<const void*>(lods+kLodStride,kLodInst,lodInst2);
    Put<const void*>(model,kLods,lods);
    Put<std::uint64_t>(model,kLodCount,2);
    std::memset(weapon,0,sizeof(weapon));
    Instance(weapon+kWeaponModel+kModelInst,weaponPose,kBones);
    Put<const void*>(human,kWeapons,weaponList);
    Put<std::int32_t>(human,kWeaponCount,1);
    Put<const void*>(cam,kTarget,human);
    WritePose(bodyPose,rootM);WritePose(lodPose,rootM,0.5f);WritePose(lod2Pose,rootM,0.25f);WritePose(weaponPose,rootM,1.5f);
}
// The object phase of a tick moving the soldier's root to rootM.
void MoveSoldier(const M4& rootM) {
    WritePose(bodyPose,rootM);WritePose(lodPose,rootM,0.5f);WritePose(lod2Pose,rootM,0.25f);WritePose(weaponPose,rootM,1.5f);
}
struct Snapshot { float body[kBones*16],lod[kBones*16],lod2[kBones*16],weapon[kBones*16],veh[kBones*16]; };
Snapshot Snap() noexcept {
    Snapshot s{};
    std::memcpy(s.body,bodyPose,sizeof(s.body));std::memcpy(s.lod,lodPose,sizeof(s.lod));
    std::memcpy(s.lod2,lod2Pose,sizeof(s.lod2));std::memcpy(s.weapon,weaponPose,sizeof(s.weapon));
    std::memcpy(s.veh,vehiclePose,sizeof(s.veh));
    return s;
}
bool Unchanged(const Snapshot& s) noexcept {
    return Same(s.body,bodyPose,sizeof(s.body)) && Same(s.lod,lodPose,sizeof(s.lod)) && Same(s.lod2,lod2Pose,sizeof(s.lod2)) &&
           Same(s.weapon,weaponPose,sizeof(s.weapon)) && Same(s.veh,vehiclePose,sizeof(s.veh));
}
// Every bone of `pose` is local_b · shown (written with WritePose(…, lift)).
bool PosedAt(const float* pose,const M4& shown,float lift=0.0f) noexcept {
    float expect[kBones*16];
    WritePose(expect,shown,lift);
    return Near(pose,expect,1e-4f);
}

// 1. The camera: ticks draw as stepped, 0-tick frames blend, the fence puts the step back, others' writes win.
void CameraBlend() {
    Reset();
    const M4 a=Pose(0.0f,0,0,0),b=Pose(0.4f,4,0,2);
    TickTo(cam,a,1.0f);
    TickTo(cam,b,1.4f);
    Check(Near(World(cam),b.m,0) && Fov(cam)==1.4f,"camera: the step's bytes stand after the tick frame");
    // A frame with ticks: the projection comes before the steps and draws the camera as the last step left it.
    BeginTick(2);
    Project(cam);
    Check(Same(World(cam),b.m,64) && Fov(cam)==1.4f,"camera: a frame with ticks does not write");
    Fence();
    Check(Counted().blended==0,"camera: a frame with ticks counts no blend");

    for(float alpha:{0.25f,0.5f}) {
        framerateview::BeginFrame(0,alpha);
        Project(cam);
        const M4 want=Pose(0.4f*alpha,4*alpha,0,2*alpha);
        Check(Near(World(cam),want.m),"camera: a 0-tick frame draws prev->cur at alpha (position linear, yaw slerped)");
        Check(std::fabs(Fov(cam)-(1.0f+0.4f*alpha))<1e-6f,"camera: the field blends linearly");
        float drawn[16];std::memcpy(drawn,World(cam),64);
        Project(cam);   // a second viewport pass in the same frame
        Check(Same(World(cam),drawn,64),"camera: written once a frame (no blend of the blend)");
        Fence();
        Check(Same(World(cam),b.m,64) && Fov(cam)==1.4f,"camera: the frame end fence puts cur back byte for byte");
    }
    Check(fenceCalls>0,"camera: the fence hook calls the game's fence");
    Counts c=Counted();
    Check(c.blended==2 && c.unblended==0,"camera: two blended projections counted");

    // Someone else wrote the camera after its step: left alone.
    const M4 other=Pose(1.0f,9,9,9);
    std::memcpy(cam+kWorld,other.m,64);
    framerateview::BeginFrame(0,0.5f);
    Project(cam);
    Check(Same(World(cam),other.m,64),"camera: a matrix written after the step is not blended over");
    Fence();
    Check(Same(World(cam),other.m,64),"camera: nor touched at the fence");
    c=Counted();
    Check(c.unblended==1 && c.blended==0,"camera: written after the step counts unblended");
    std::memcpy(cam+kWorld,b.m,64);
    Put<float>(cam,kFov,2.0f);   // the field alone written after the step
    framerateview::BeginFrame(0,0.5f);
    Project(cam);
    Check(Same(World(cam),b.m,64) && Fov(cam)==2.0f,"camera: a field written after the step is not blended over");
    Fence();
    Check(Counted().unblended==1,"camera: the field written after the step counts unblended");
    Put<float>(cam,kFov,1.4f);

    // Review 4: the blend overwritten after the projection. Matrix overwritten: neither matrix nor field goes back.
    framerateview::BeginFrame(0,0.5f);
    Project(cam);
    const float shownFov=Fov(cam);
    std::memcpy(cam+kWorld,other.m,64);
    Fence();
    Check(Same(World(cam),other.m,64),"camera: a blend someone overwrote is not put back");
    Check(Fov(cam)==shownFov,"camera: the field is not put back without its matrix (review 4)");
    // Field overwritten, matrix still the blend: the matrix goes back, the field stays theirs.
    std::memcpy(cam+kWorld,b.m,64);Put<float>(cam,kFov,1.4f);
    framerateview::BeginFrame(0,0.5f);
    Project(cam);
    Put<float>(cam,kFov,3.0f);
    Fence();
    Check(Same(World(cam),b.m,64) && Fov(cam)==3.0f,"camera: matrix restored, someone's field kept");
    Put<float>(cam,kFov,1.4f);
    Counted();
}

// 2. The cuts.
void CameraCuts() {
    Reset();
    const M4 a=Pose(0,0,0,0),b=Pose(0,2,0,0),c2=Pose(0,4,0,0),d=Pose(0,6,0,0);
    TickTo(cam,a);
    TickTo(cam,b);
    FrameRateCameraCut(cam);
    for(int frame=0;frame<2;++frame) {
        framerateview::BeginFrame(0,frame ? 0.75f : 0.25f);
        Project(cam);
        Check(Near(World(cam),b.m),"cut: every frame until the next step draws cur");
        Fence();
        Check(Same(World(cam),b.m,64),"cut: cur stands after the fence");
    }
    TickTo(cam,c2);   // the step after the cut: prev is this step's own bytes
    framerateview::BeginFrame(0,0.5f);
    Project(cam);
    Check(Near(World(cam),c2.m),"cut: the next step takes prev from the camera's bytes (no blend across the jump)");
    Fence();
    TickTo(cam,d);
    framerateview::BeginFrame(0,0.5f);
    Project(cam);
    const M4 mid=Pose(0,5,0,0);
    Check(Near(World(cam),mid.m),"cut: the step after that blends again");
    Fence();

    // nullptr cuts every camera.
    Reset();
    TickTo(cam,a);
    TickTo(cam2,a);
    BeginTick(1);Queue(cam,b);Queue(cam2,c2);Step(cam);Step(cam2);Fence();
    FrameRateCameraCut(nullptr);
    framerateview::BeginFrame(0,0.5f);
    Project(cam);Project(cam2);
    Check(Near(World(cam),b.m) && Near(World(cam2),c2.m),"cut: nullptr cuts every camera");
    Fence();
    // Only the named camera.
    BeginTick(1);Queue(cam,c2);Queue(cam2,d);Step(cam);Step(cam2);Fence();
    FrameRateCameraCut(cam2);
    framerateview::BeginFrame(0,0.5f);
    Project(cam);Project(cam2);
    Check(Near(World(cam),Pose(0,3,0,0).m) && Near(World(cam2),d.m),"cut: a named camera cuts that camera only");
    Fence();

    // age > 1: a camera that stopped stepping takes prev from its bytes at its next step.
    Reset();
    TickTo(cam,a);
    TickTo(cam,b);
    BeginTick(1);Fence();   // a tick that does not step it (age 1)
    BeginTick(1);           // and another (age 2)
    Queue(cam,c2);Step(cam);Fence();
    framerateview::BeginFrame(0,0.5f);
    Project(cam);
    Check(Near(World(cam),c2.m),"cut: a camera idle for more than one tick does not blend from its old step");
    Fence();
    // age 1 (stepped the tick before): blends from cur.
    Reset();
    TickTo(cam,a);
    TickTo(cam,b);
    TickTo(cam,c2);
    framerateview::BeginFrame(0,0.5f);
    Project(cam);
    Check(Near(World(cam),Pose(0,3,0,0).m),"cut: a camera stepped every tick blends prev->cur");
    Fence();
    Counted();
}

// 3. prev / cur across catch-up frames and jumps.
void PrevCur() {
    Reset();
    TickTo(cam,Pose(0,0,0,0));
    BeginTick(3);   // a catch-up frame: three steps
    Project(cam);
    for(float x:{1.0f,2.0f,4.0f}){Queue(cam,Pose(0,x,0,0));Step(cam);}
    Fence();
    framerateview::BeginFrame(0,0.5f);
    Project(cam);
    Check(Near(World(cam),Pose(0,3,0,0).m),"prev/cur: prev is the catch-up frame's second-to-last step");
    Fence();
    Check(Counted().blended==1,"prev/cur: blended counted");

    struct Jump { M4 to; bool blends; const char* what; };
    const Jump jumps[]={
        {Pose(0,9.5f,0,0),true,"prev/cur: 9.5 m in a tick still blends"},
        {Pose(0,10.5f,0,0),false,"prev/cur: past kCutMove (10 m) is drawn as stepped"},
        {Pose(0.9f,0,0,0),true,"prev/cur: 0.9 rad in a tick still blends"},
        {Pose(1.1f,0,0,0),false,"prev/cur: past kCutTurn (1 rad) is drawn as stepped"},
    };
    for(const Jump& j:jumps) {
        Reset();
        TickTo(cam,Pose(0,0,0,0));
        TickTo(cam,j.to);
        framerateview::BeginFrame(0,0.5f);
        Project(cam);
        const bool asStepped=Same(World(cam),j.to.m,64);
        Fence();
        const Counts c=Counted();
        Check(j.blends ? !asStepped && c.blended==1 : asStepped && c.unblended==1 && c.blended==0,j.what);
        Check(Same(World(cam),j.to.m,64),"prev/cur: cur after the fence");
    }
}

// The soldier the camera follows ticks from r0 to r1 (two samples).
void TwoSamples(const M4& r0,const M4& r1,const M4& c0=Pose(0,0,0,0),const M4& c1=Pose(0,1,0,0)) {
    MoveSoldier(r0);TickTo(cam,c0);
    MoveSoldier(r1);TickTo(cam,c1);
}

// 4. The player's root (fps-7).
void Roots() {
    Reset();
    stepImage=ImageFor(reinterpret_cast<std::uintptr_t>(&StandInStep),kCameraStep);
    const M4 r0=Pose(0.0f,10,0,10),r1=Pose(0.2f,10.4f,0,10.2f);
    OnFoot(r0);
    MoveSoldier(r0);TickTo(cam,Pose(0,0,0,0));
    framerateview::BeginFrame(0,0.5f);
    Check(Counted().roots==0,"root: one sample poses nothing");
    Fence();
    const Snapshot atR0=Snap();
    BeginTick(1);
    Check(Unchanged(atR0),"root: a frame with ticks does not pose");
    Project(cam);
    MoveSoldier(r1);Queue(cam,Pose(0,1,0,0));Step(cam);
    Fence();
    const Snapshot stepped=Snap();
    framerateview::BeginFrame(0,0.5f);
    const M4 half=Pose(0.1f,10.2f,0,10.1f);
    Check(PosedAt(bodyPose,half),"root: every body bone moves to local · blend(prev, cur, alpha)");
    Check(PosedAt(lodPose,half,0.5f) && PosedAt(lod2Pose,half,0.25f),"root: the base LOD and the LOD table's instances too");
    Check(PosedAt(weaponPose,half,1.5f),"root: the weapon's bones too");
    Project(cam);
    Fence();
    Check(Unchanged(stepped),"root: the fence puts every pose back byte for byte");
    Counts c=Counted();
    Check(c.roots==1 && c.rootsLeft==0 && c.rootsRefused==0,"root: one posed root counted");
    // The base LOD listed twice (base and table) is posed once: D applied once.
    framerateview::BeginFrame(0,0.25f);
    Check(PosedAt(lodPose,Pose(0.05f,10.1f,0,10.05f),0.5f),"root: an instance listed twice is posed once");
    // BeginFrame's fallback: no fence, the next frame's start puts the poses and the camera back.
    Project(cam);
    framerateview::BeginFrame(0,0.5f);
    Check(PosedAt(bodyPose,half),"root: a frame after an unfenced one poses from the step, not from the last blend");
    BeginTick(1);
    Check(Unchanged(stepped),"root: BeginFrame without a fence still restores the poses");
    Check(Same(World(cam),Pose(0,1,0,0).m,64),"root: and the camera");
    Fence();
    Counted();

    // Someone rewrote one instance after the write: that instance is left, the others go back.
    framerateview::BeginFrame(0,0.5f);
    weaponPose[17]+=1.0f;
    float weaponTheirs[kBones*16];std::memcpy(weaponTheirs,weaponPose,sizeof(weaponTheirs));
    Fence();
    Check(Same(weaponPose,weaponTheirs,sizeof(weaponTheirs)),"root: a pose rewritten after the write is not put back");
    Check(Same(bodyPose,stepped.body,sizeof(stepped.body)) && Same(lodPose,stepped.lod,sizeof(stepped.lod)),
          "root: the untouched poses go back");
    std::memcpy(weaponPose,stepped.weapon,sizeof(stepped.weapon));
    // The pose pointer or the bone count changed: not put back.
    framerateview::BeginFrame(0,0.5f);
    float lodWritten[kBones*16];std::memcpy(lodWritten,lodPose,sizeof(lodWritten));
    Put<float*>(lodInst,kInstPose,sparePose);
    float lod2Written[kBones*16];std::memcpy(lod2Written,lod2Pose,sizeof(lod2Written));
    Put<std::uint64_t>(lodInst2,kInstCount,2);
    Fence();
    Check(Same(lodPose,lodWritten,sizeof(lodWritten)),"root: an instance whose pose array moved is not put back");
    Check(Same(lod2Pose,lod2Written,sizeof(lod2Written)),"root: an instance whose bone count changed is not put back");
    Check(Same(bodyPose,stepped.body,sizeof(stepped.body)),"root: the others are");
    Put<float*>(lodInst,kInstPose,lodPose);Put<std::uint64_t>(lodInst2,kInstCount,kBones);
    std::memcpy(lodPose,stepped.lod,sizeof(stepped.lod));std::memcpy(lod2Pose,stepped.lod2,sizeof(stepped.lod2));
    Counted();

    // rootsLeft: the live root differs from the sample (moved after the step), or the control block changed.
    bodyPose[12]+=0.5f;
    framerateview::BeginFrame(0,0.5f);
    Check(bodyPose[12]==stepped.body[12]+0.5f && Same(lodPose,stepped.lod,sizeof(stepped.lod)),
          "root: a root moved after its step is not posed");
    Fence();
    bodyPose[12]=stepped.body[12];
    Put<const void*>(human,kSelfCtrl,ctrlB);
    framerateview::BeginFrame(0,0.5f);
    Check(Unchanged(stepped),"root: a soldier whose control block changed since the step is not posed");
    Fence();
    c=Counted();
    Check(c.rootsLeft==2 && c.roots==0 && c.rootsRefused==0,"root: both counted as left, not refused");
    Put<const void*>(human,kSelfCtrl,ctrlA);

    // rootsRefused: a jump past kCutMove between the samples.
    Reset();
    OnFoot(r0);
    TwoSamples(r0,Pose(0,25,0,10));
    const Snapshot jumped=Snap();
    framerateview::BeginFrame(0,0.5f);
    Check(Unchanged(jumped),"root: a jump is not posed");
    Fence();
    c=Counted();
    Check(c.rootsRefused==1 && c.rootsLeft==0 && c.roots==0,"root: a jump counts refused, not left");
}

// 4b. Riding: the vehicle's root, its D on the vehicle, the rider and the rider's weapons.
void Riding() {
    Reset();
    const M4 r=Pose(0,0,0,0),v0=Pose(0.0f,50,0,50),v1=Pose(0.3f,52,0,50);
    OnFoot(r);
    Vehicle(vehicle,vehiclePose,ctrlC);
    Put<const void*>(human,kVehicle,vehicle);
    riddenHuman=human;
    WritePose(vehiclePose,v0);
    TickTo(cam,Pose(0,0,0,0));
    WritePose(vehiclePose,v1);
    TickTo(cam,Pose(0,1,0,0));
    const Snapshot stepped=Snap();
    framerateview::BeginFrame(0,0.5f);
    const M4 half=Pose(0.15f,51,0,50);
    Check(PosedAt(vehiclePose,half),"ride: the vehicle's bones move to the blend of its root");
    M4 cur;std::memcpy(cur.m,v1.m,64);
    M4 inverse;fpview::InverseAffine(cur,&inverse);
    const M4 dv=fpview::Mul(inverse,half);
    float expect[16];
    {const M4 m=fpview::Mul(fpview::Mul(fpview::Translate(0,1,0),r),dv);std::memcpy(expect,m.m,64);}
    Check(Near(bodyPose+16,expect),"ride: the rider's bones move with the vehicle's D");
    {const M4 m=fpview::Mul(fpview::Mul(fpview::Translate(0,1.5f,0),r),dv);std::memcpy(expect,m.m,64);}
    Check(Near(weaponPose,expect),"ride: the rider's weapons too");
    Fence();
    Check(Unchanged(stepped),"ride: the fence puts every pose back");
    Check(Counted().roots==1,"ride: one posed root");
}

// 4c. A new root (soldier, vehicle, control block, instance) restarts the samples and cuts its camera (review 5).
void RootIdentity() {
    stepImage=ImageFor(reinterpret_cast<std::uintptr_t>(&StandInStep),kCameraStep);
    enum Change { otherSoldier,soldierCtrl,boards,vehicleCtrl,otherVehicle,alights };
    const char* const names[]={"root identity: another soldier","root identity: the soldier's control block",
                               "root identity: boarding (vehicle and instance)","root identity: the vehicle's control block",
                               "root identity: another vehicle","root identity: alighting (instance back to the body)"};
    for(int change=otherSoldier;change<=alights;++change) {
        Reset();
        const M4 r0=Pose(0,0,0,0),r1=Pose(0,1,0,0);
        OnFoot(r0);
        Soldier(human2,body2Pose,ctrlA);
        WritePose(body2Pose,r1);
        Vehicle(vehicle,vehiclePose,ctrlC);Vehicle(vehicle2,vehicle2Pose,ctrlD);
        WritePose(vehiclePose,r0);WritePose(vehicle2Pose,r1);
        const bool ridingFirst=change==vehicleCtrl || change==otherVehicle || change==alights;
        if(ridingFirst){Put<const void*>(human,kVehicle,vehicle);riddenHuman=human;}
        TwoSamples(r0,r0,Pose(0,0,0,0),Pose(0,1,0,0));
        switch(change) {
        case otherSoldier: Put<const void*>(cam,kTarget,human2);break;
        case soldierCtrl: Put<const void*>(human,kSelfCtrl,ctrlB);break;
        case boards: Put<const void*>(human,kVehicle,vehicle);riddenHuman=human;break;
        case vehicleCtrl: Put<const void*>(vehicle,kSelfCtrl,ctrlD);break;
        case otherVehicle: Put<const void*>(human,kVehicle,vehicle2);break;
        case alights: riddenHuman=nullptr;break;
        }
        // The switching step: the camera jumps 3 m, under kCutMove, so only the cut keeps it from blending.
        const Snapshot before=Snap();
        float before2[kBones*16];std::memcpy(before2,body2Pose,sizeof(before2));
        TickTo(cam,Pose(0,4,0,0));
        framerateview::BeginFrame(0,0.5f);
        Project(cam);
        char what[160];
        std::snprintf(what,sizeof(what),"%s: its camera is cut (draws the step, not a blend)",names[change]);
        Check(Near(World(cam),Pose(0,4,0,0).m),what);
        std::snprintf(what,sizeof(what),"%s: the samples restart (nothing posed)",names[change]);
        Check(Unchanged(before) && Same(body2Pose,before2,sizeof(before2)),what);
        Fence();
        const Counts c=Counted();
        std::snprintf(what,sizeof(what),"%s: no root counted on the restart",names[change]);
        Check(c.roots==0,what);
        // The next tick samples again: posing resumes.
        TickTo(cam,Pose(0,5,0,0));
        framerateview::BeginFrame(0,0.5f);
        std::snprintf(what,sizeof(what),"%s: the second sample of the new root poses again",names[change]);
        Check(Counted().roots==1,what);
        Fence();
    }
    // A second camera following the same soldier does not take the root over.
    Reset();
    OnFoot(Pose(0,0,0,0));
    Put<const void*>(cam2,kTarget,human);
    MoveSoldier(Pose(0,0,0,0));
    BeginTick(1);Queue(cam,Pose(0,0,0,0));Queue(cam2,Pose(0,0,0,0));Step(cam);Step(cam2);Fence();
    MoveSoldier(Pose(0,1,0,0));
    BeginTick(1);Queue(cam,Pose(0,1,0,0));Queue(cam2,Pose(0,1,0,0));Step(cam);Step(cam2);Fence();
    framerateview::BeginFrame(0,0.5f);
    Check(Counted().roots==1,"root: the first camera's samples are kept with a second camera on the soldier");
    Fence();
    // The root's camera stops stepping: the root goes, and the other camera samples.
    MoveSoldier(Pose(0,2,0,0));
    BeginTick(1);Queue(cam2,Pose(0,2,0,0));Step(cam2);Fence();
    BeginTick(1);Queue(cam2,Pose(0,2,0,0));Step(cam2);Fence();
    Check(root.camera==nullptr || root.camera==cam2,"root: a camera that stopped stepping gives the root up");
    MoveSoldier(Pose(0,3,0,0));
    BeginTick(1);Queue(cam2,Pose(0,3,0,0));Step(cam2);Fence();
    framerateview::BeginFrame(0,0.5f);
    Check(Counted().roots==1 && PosedAt(bodyPose,Pose(0,2.5f,0,0)),"root: the other camera samples it");
    Fence();
}

// 5. Off: nothing at all.
void Off() {
    Reset();
    TickTo(cam,Pose(0,0,0,0));
    TickTo(cam,Pose(0,1,0,0));
    on=false;
    framerateview::BeginFrame(0,0.5f);
    Project(cam);
    Check(Same(World(cam),Pose(0,1,0,0).m,64),"off: no blend");
    Fence();
    on=true;
}
}  // namespace
}  // namespace crew

int main() {
    using namespace crew;
    stepImage=ImageFor(reinterpret_cast<std::uintptr_t>(&StandInStep),kCameraStep);
    image=stepImage;
    CameraBlend();
    CameraCuts();
    PrevCur();
    Roots();
    Riding();
    RootIdentity();
    Off();
    std::printf("frame_rate_view: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
