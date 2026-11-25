// The frame rate view's state machine (src/frame_rate_view.cpp, docs/unlimited-fps-re.md §4.3) run on heap fixtures laid
// out as the file reads them: the camera (+0x220 world, +0x24 field, +0x360 soldier), the soldier, the vehicle, their
// models, skeleton instances and pose arrays. The camera step's and the frame end fence's game calls (image+kCameraStep,
// image+kFence, image+kRecordDraw) land on local stand-ins: `image` is set so that image+rva is the stand-in for the call
// being made. Each frame runs as the game does: BeginFrame, the render recording (render_record.cpp's RecordHook; the
// stand-in recording projects through render_record.cpp's ProjectHook, as 0x11978D0 does after copying the poses, and a
// frame without ticks poses at that first projection), the projection (BeforeProject's turn), the steps
// (CameraStepHook), the fence (FrameEndHook).
#include "../src/frame_rate_view.cpp"
#include "../src/render_record.cpp"
#include <cmath>
#include <cstdio>

namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
const unsigned char* riddenHuman=nullptr;   // the soldier HumanOnFoot says is riding
bool HumanOnFoot(const unsigned char* human) noexcept { return human!=riddenHuman; }
namespace scopelens { bool SetBeforeProject(BeforeProjectFn) noexcept { return false; } }
// The other objects' layer (frame_rate_objects.cpp, PR-e) reads the game's object tables: not exercised here, only
// counted, so its calls ride the same frames as the player's root.
namespace frameobjects {
int installs=0,samples=0,untickedSamples=0,poses=0,restores=0,refusals=0;
const float* rootSeen=nullptr;   // the player's root as the last Sample saw it (the test points it at the body's pose)
float rootAtSample[16]{};
void Install() noexcept { ++installs; }
void Sample(bool unticked) noexcept {
    ++samples;
    if(unticked)++untickedSamples;
    if(rootSeen)std::memcpy(rootAtSample,rootSeen,sizeof(rootAtSample));
}
void Pose(float,const void*,const void*,const float*) noexcept { ++poses; }
void Restore() noexcept { ++restores; }
void NoteRootRefusal(framerate::Refusal) noexcept { ++refusals; }
}  // namespace frameobjects

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
int recordCalls=0;
std::uint64_t recordArgs[2]{};
int recordProjections=1;   // the projections the stand-in recording makes (viewports; 0: none, as when loading)
bool sampledInRecord=false;
void* StandInRecord(void*,std::uint64_t a,std::uint64_t b) {
    ++recordCalls;recordArgs[0]=a;recordArgs[1]=b;
    const int before=frameobjects::samples;
    for(int i=0;i<recordProjections;++i)ProjectHook(nullptr,nullptr);
    sampledInRecord=frameobjects::samples!=before;
    return nullptr;
}
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
void Record();
// A frame with ticks up to its steps: BeginFrame, then the render recording (the game records before it steps).
void BeginTick(unsigned ticks) { framerateview::BeginFrame(ticks,1.0f);Record(); }
void Step(void* camera) {
    image=stepImage;
    CameraStepHook(camera,nullptr);
}
void Project(void* camera) { ProjectHook(camera,nullptr); }
// System::Update's render recording call (0x1198FA4), through render_record.cpp's hook with the frame rate's turn
// registered as Install registers it (the call site's patch itself is render_record_test's).
void Record() {
    recordState=1;
    renderrecord::SetProject(renderrecord::Slot::kFrameRate,&BeforeProject);
    image=ImageFor(reinterpret_cast<std::uintptr_t>(&StandInRecord),kRecordDraw);
    RecordHook(nullptr,0x1234,1);
    image=stepImage;
}
// A frame without ticks up to its render recording: BeginFrame, then the record call (which poses).
void Frame0(float alpha) {
    framerateview::BeginFrame(0,alpha);
    Record();
}
void Fence() {
    image=ImageFor(reinterpret_cast<std::uintptr_t>(&StandInFence),kFence);
    FrameEndHook(nullptr,nullptr,nullptr,nullptr);
    image=stepImage;
}
// A tick frame that steps `camera` to `world`: its recording first (it draws the last tick's scene), then the tick moves
// the root (`move(*rootM)`, when given), then the camera step.
void TickTo(void* camera,const M4& world,float fov=1.0f,void (*move)(const M4&)=nullptr,const M4* rootM=nullptr) {
    BeginTick(1);
    Project(camera);
    if(move && rootM)move(*rootM);
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
    // A frame with ticks: the projection comes before the steps and draws the camera as the last step left it.
    BeginTick(2);
    Project(cam);
    Check(Same(World(cam),a.m,64) && Fov(cam)==1.0f,"camera: a frame with ticks does not write");
    Queue(cam,b,1.4f);
    Step(cam);
    Fence();
    Check(Near(World(cam),b.m,0) && Fov(cam)==1.4f,"camera: the step's bytes stand after the tick frame");
    Check(Counted().blended==0,"camera: a frame with ticks counts no blend");

    for(float alpha:{0.25f,0.5f}) {
        Frame0(alpha);
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
    Frame0(0.5f);
    Project(cam);
    Check(Same(World(cam),other.m,64),"camera: a matrix written after the step is not blended over");
    Fence();
    Check(Same(World(cam),other.m,64),"camera: nor touched at the fence");
    c=Counted();
    Check(c.unblended==1 && c.blended==0,"camera: written after the step counts unblended");
    std::memcpy(cam+kWorld,b.m,64);
    Put<float>(cam,kFov,2.0f);   // the field alone written after the step
    Frame0(0.5f);
    Project(cam);
    Check(Same(World(cam),b.m,64) && Fov(cam)==2.0f,"camera: a field written after the step is not blended over");
    Fence();
    Check(Counted().unblended==1,"camera: the field written after the step counts unblended");
    Put<float>(cam,kFov,1.4f);

    // Review 4: the blend overwritten after the projection. Matrix overwritten: neither matrix nor field goes back.
    Frame0(0.5f);
    Project(cam);
    const float shownFov=Fov(cam);
    std::memcpy(cam+kWorld,other.m,64);
    Fence();
    Check(Same(World(cam),other.m,64),"camera: a blend someone overwrote is not put back");
    Check(Fov(cam)==shownFov,"camera: the field is not put back without its matrix (review 4)");
    // Field overwritten, matrix still the blend: the matrix goes back, the field stays theirs.
    std::memcpy(cam+kWorld,b.m,64);Put<float>(cam,kFov,1.4f);
    Frame0(0.5f);
    Project(cam);
    Put<float>(cam,kFov,3.0f);
    Fence();
    Check(Same(World(cam),b.m,64) && Fov(cam)==3.0f,"camera: matrix restored, someone's field kept");
    Put<float>(cam,kFov,1.4f);
    Counted();
}

// 1b. The eye the first person places its Umbra boxes around (FrameRateDrawnEye, at the render recording, before the
// projection): the blend the projection then draws on a frame without ticks; false (the camera's own) otherwise.
void DrawnEye() {
    Reset();
    const M4 a=Pose(0.0f,0,0,0),b=Pose(0.4f,4,0,2);
    TickTo(cam,a,1.0f);
    float eye[3]{};
    BeginTick(1);
    Check(!FrameRateDrawnEye(cam,eye),"drawn eye: a frame with ticks draws the camera as it is");
    Queue(cam,b,1.0f);
    Step(cam);
    Fence();
    Frame0(0.25f);
    const bool before=FrameRateDrawnEye(cam,eye);
    const float early[3]={eye[0],eye[1],eye[2]};
    Check(Same(World(cam),b.m,64),"drawn eye: asking writes nothing");
    Project(cam);
    const float* const w=reinterpret_cast<const float*>(cam+kWorld);
    Check(before && Same(early,w+12,12) && std::fabs(early[0]-1.0f)<1e-4f,"drawn eye: before the projection, the eye it then draws");
    Check(FrameRateDrawnEye(cam,eye) && Same(eye,w+12,12),"drawn eye: after it, the eye written");
    Check(!FrameRateDrawnEye(cam2,eye),"drawn eye: a camera not sampled");
    Fence();
    FrameRateCameraCut(cam);
    Frame0(0.5f);
    Check(FrameRateDrawnEye(cam,eye) && Same(eye,b.m+12,12),"drawn eye: a cut camera, the step's eye (what it is drawn at)");
    Fence();
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
        Frame0(frame ? 0.75f : 0.25f);
        Project(cam);
        Check(Near(World(cam),b.m),"cut: every frame until the next step draws cur");
        Fence();
        Check(Same(World(cam),b.m,64),"cut: cur stands after the fence");
    }
    TickTo(cam,c2);   // the step after the cut: prev is this step's own bytes
    Frame0(0.5f);
    Project(cam);
    Check(Near(World(cam),c2.m),"cut: the next step takes prev from the camera's bytes (no blend across the jump)");
    Fence();
    TickTo(cam,d);
    Frame0(0.5f);
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
    Frame0(0.5f);
    Project(cam);Project(cam2);
    Check(Near(World(cam),b.m) && Near(World(cam2),c2.m),"cut: nullptr cuts every camera");
    Fence();
    // Only the named camera (one step first: the cut's own next step takes prev from its bytes).
    BeginTick(1);Queue(cam,c2);Queue(cam2,c2);Step(cam);Step(cam2);Fence();
    BeginTick(1);Queue(cam,d);Queue(cam2,d);Step(cam);Step(cam2);Fence();
    FrameRateCameraCut(cam2);
    Frame0(0.5f);
    Project(cam);Project(cam2);
    Check(Near(World(cam),Pose(0,5,0,0).m) && Near(World(cam2),d.m),"cut: a named camera cuts that camera only");
    Fence();

    // age > 1: a camera that stopped stepping takes prev from its bytes at its next step.
    Reset();
    TickTo(cam,a);
    TickTo(cam,b);
    BeginTick(1);Fence();   // a tick that does not step it (age 1)
    BeginTick(1);           // and another (age 2)
    Queue(cam,c2);Step(cam);Fence();
    Frame0(0.5f);
    Project(cam);
    Check(Near(World(cam),c2.m),"cut: a camera idle for more than one tick does not blend from its old step");
    Fence();
    // age 1 (stepped the tick before): blends from cur.
    Reset();
    TickTo(cam,a);
    TickTo(cam,b);
    TickTo(cam,c2);
    Frame0(0.5f);
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
    Frame0(0.5f);
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
        Frame0(0.5f);
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
    TickTo(cam,c0,1.0f,&MoveSoldier,&r0);
    TickTo(cam,c1,1.0f,&MoveSoldier,&r1);
}
void MoveVehicle(const M4& rootM) { WritePose(vehiclePose,rootM); }

// 4. The player's root (fps-7).
void Roots() {
    Reset();
    stepImage=ImageFor(reinterpret_cast<std::uintptr_t>(&StandInStep),kCameraStep);
    const M4 r0=Pose(0.0f,10,0,10),r1=Pose(0.2f,10.4f,0,10.2f);
    OnFoot(r0);
    TickTo(cam,Pose(0,0,0,0),1.0f,&MoveSoldier,&r0);
    Frame0(0.5f);
    Check(Counted().roots==0,"root: one sample poses nothing");
    Fence();
    const Snapshot atR0=Snap();
    BeginTick(1);
    Check(Unchanged(atR0),"root: a frame with ticks does not pose");
    Project(cam);
    MoveSoldier(r1);Queue(cam,Pose(0,1,0,0));Step(cam);
    Fence();
    const Snapshot stepped=Snap();
    Frame0(0.5f);
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
    Frame0(0.25f);
    Check(PosedAt(lodPose,Pose(0.05f,10.1f,0,10.05f),0.5f),"root: an instance listed twice is posed once");
    // BeginFrame's fallback: no fence, the next frame's start puts the poses and the camera back.
    Project(cam);
    Frame0(0.5f);
    Check(PosedAt(bodyPose,half),"root: a frame after an unfenced one poses from the step, not from the last blend");
    BeginTick(1);
    Check(Unchanged(stepped),"root: BeginFrame without a fence still restores the poses");
    Check(Same(World(cam),Pose(0,1,0,0).m,64),"root: and the camera");
    Fence();
    Counted();

    // Someone rewrote one bone after the write: that bone is left, the instance's other bones and the others go back.
    Frame0(0.5f);
    weaponPose[17]+=1.0f;
    float weaponTheirs[16];std::memcpy(weaponTheirs,weaponPose+16,sizeof(weaponTheirs));
    Fence();
    Check(Same(weaponPose+16,weaponTheirs,sizeof(weaponTheirs)),"root: a bone rewritten after the write is not put back");
    Check(Same(weaponPose,stepped.weapon,64) && Same(weaponPose+32,stepped.weapon+32,64),
          "root: the same instance's untouched bones go back");
    Check(Same(bodyPose,stepped.body,sizeof(stepped.body)) && Same(lodPose,stepped.lod,sizeof(stepped.lod)),
          "root: the untouched poses go back");
    std::memcpy(weaponPose,stepped.weapon,sizeof(stepped.weapon));
    // The pose pointer or the bone count changed: not put back.
    Frame0(0.5f);
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
    Frame0(0.5f);
    Check(bodyPose[12]==stepped.body[12]+0.5f && Same(lodPose,stepped.lod,sizeof(stepped.lod)),
          "root: a root moved after its step is not posed");
    Fence();
    bodyPose[12]=stepped.body[12];
    Put<const void*>(human,kSelfCtrl,ctrlB);
    Frame0(0.5f);
    Check(Unchanged(stepped),"root: a soldier whose control block changed since the step is not posed");
    Fence();
    c=Counted();
    Check(c.rootsLeft==2 && c.roots==0 && c.rootsRefused==0,"root: both counted as left, not refused");
    Put<const void*>(human,kSelfCtrl,ctrlA);

    // Two instances (the weapon's and the body's) drawing from one pose array: D goes on once.
    Reset();
    OnFoot(r0);
    Instance(weapon+kWeaponModel+kModelInst,bodyPose,kBones);
    TwoSamples(r0,r1);
    const Snapshot shared=Snap();
    Frame0(0.5f);
    Check(PosedAt(bodyPose,half),"root: a pose array two instances share is posed once");
    Fence();
    Check(Unchanged(shared),"root: and put back once");
    Instance(weapon+kWeaponModel+kModelInst,weaponPose,kBones);

    // rootsRefused: a jump past kCutMove between the samples.
    Reset();
    OnFoot(r0);
    TwoSamples(r0,Pose(0,25,0,10));
    const Snapshot jumped=Snap();
    Frame0(0.5f);
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
    TickTo(cam,Pose(0,0,0,0),1.0f,&MoveVehicle,&v0);
    TickTo(cam,Pose(0,1,0,0),1.0f,&MoveVehicle,&v1);
    const Snapshot stepped=Snap();
    Frame0(0.5f);
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
        Frame0(0.5f);
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
        Frame0(0.5f);
        std::snprintf(what,sizeof(what),"%s: the second sample of the new root poses again",names[change]);
        Check(Counted().roots==1,what);
        Fence();
    }
    // A second camera following the same soldier does not take the root over.
    Reset();
    OnFoot(Pose(0,0,0,0));
    Put<const void*>(cam2,kTarget,human);
    BeginTick(1);MoveSoldier(Pose(0,0,0,0));Queue(cam,Pose(0,0,0,0));Queue(cam2,Pose(0,0,0,0));Step(cam);Step(cam2);Fence();
    BeginTick(1);MoveSoldier(Pose(0,1,0,0));Queue(cam,Pose(0,1,0,0));Queue(cam2,Pose(0,1,0,0));Step(cam);Step(cam2);Fence();
    Frame0(0.5f);
    Check(Counted().roots==1,"root: the first camera's samples are kept with a second camera on the soldier");
    Fence();
    // The root's camera stops stepping: the root goes, and the other camera samples.
    BeginTick(1);MoveSoldier(Pose(0,2,0,0));Queue(cam2,Pose(0,2,0,0));Step(cam2);Fence();
    BeginTick(1);Queue(cam2,Pose(0,2,0,0));Step(cam2);Fence();
    Check(root.camera==nullptr || root.camera==cam2,"root: a camera that stopped stepping gives the root up");
    BeginTick(1);MoveSoldier(Pose(0,3,0,0));Queue(cam2,Pose(0,3,0,0));Step(cam2);Fence();
    Frame0(0.5f);
    Check(Counted().roots==1 && PosedAt(bodyPose,Pose(0,2.5f,0,0)),"root: the other camera samples it");
    Fence();
}

// 4b. Where the poses are written: not at BeginFrame (System::Update deletes objects after it), not at the render
// recording's call (0x1198FA4: the recording copies the poses again before it draws), at its first projection, once; the
// end fence puts them back.
void RecordPoint() {
    Reset();
    stepImage=ImageFor(reinterpret_cast<std::uintptr_t>(&StandInStep),kCameraStep);
    const M4 r0=Pose(0.0f,10,0,10),r1=Pose(0.2f,10.4f,0,10.2f);
    OnFoot(r0);
    TwoSamples(r0,r1);
    const Snapshot stepped=Snap();
    const int objectPoses=frameobjects::poses;
    const int samples=frameobjects::samples,untickedSamples=frameobjects::untickedSamples;
    frameobjects::rootSeen=bodyPose;
    framerateview::BeginFrame(0,0.5f);
    Check(Unchanged(stepped) && frameobjects::poses==objectPoses,"record: BeginFrame of a frame without ticks writes nothing");
    const int records=recordCalls;
    Record();
    Check(recordCalls==records+1 && recordArgs[0]==0x1234 && recordArgs[1]==1,
          "record: the hook calls the game's recording with its arguments");
    Check(PosedAt(bodyPose,Pose(0.1f,10.2f,0,10.1f)) && frameobjects::poses==objectPoses+1,
          "record: the hook poses the root and the other objects");
    Check(frameobjects::samples==samples+1 && frameobjects::untickedSamples==untickedSamples+1 &&
          !std::memcmp(frameobjects::rootAtSample,stepped.body,sizeof(frameobjects::rootAtSample)),
          "record: the other objects' sample first (a frame without ticks), before the player's root is written");
    Check(sampledInRecord,"record: the sample and the poses come at the recording's projection, not before it");
    Record();   // a second record call in the same frame: posed once
    Check(PosedAt(bodyPose,Pose(0.1f,10.2f,0,10.1f)) && frameobjects::poses==objectPoses+1,"record: posed once a frame");
    Fence();
    Check(Unchanged(stepped),"record: the end fence puts the poses back");
    // A frame with ticks: the record call (BeginTick's) poses nothing.
    BeginTick(1);
    Check(Unchanged(stepped) && frameobjects::poses==objectPoses+1,"record: a frame with ticks poses nothing");
    Check(frameobjects::samples==samples+3 && frameobjects::untickedSamples==untickedSamples+1,
          "record: every record call samples (Sample counts the ticks), only a frame posing says unticked");
    frameobjects::rootSeen=nullptr;
    Fence();
    // A frame without ticks whose record call never came: the next frame's fence and record pose nothing stale.
    framerateview::BeginFrame(0,0.5f);
    Fence();
    BeginTick(1);
    Check(Unchanged(stepped) && frameobjects::poses==objectPoses+1,"record: a pose due is dropped at the fence");
    Fence();
    // Off: the hook only calls through.
    on=false;
    framerateview::BeginFrame(0,0.5f);
    on=false;
    Record();
    Check(Unchanged(stepped) && recordCalls>records+3,"record: off, it only calls the game's recording");
    on=true;
    Fence();
    // A recording with two projections (two viewports): sampled and posed at the first only. One with none: nothing,
    // and the projection of a later recording call does not take over a recording that never projected.
    recordProjections=2;
    const int twoSamples=frameobjects::samples;
    framerateview::BeginFrame(0,0.5f);
    Record();
    Check(frameobjects::samples==twoSamples+1 && frameobjects::poses==objectPoses+2,
          "record: two projections, sampled and posed once");
    Fence();
    Check(Unchanged(stepped),"record: and put back");
    recordProjections=0;
    framerateview::BeginFrame(0,0.5f);
    Record();
    Check(frameobjects::samples==twoSamples+1 && frameobjects::poses==objectPoses+2 && Unchanged(stepped),
          "record: no projection, nothing sampled or posed");
    Project(cam);   // another caller's projection before the fence (0x11978D0 has four more)
    Check(frameobjects::samples==twoSamples+1 && frameobjects::poses==objectPoses+2,
          "record: closed when it returned, a later projection before the fence samples and poses nothing");
    Fence();
    recordProjections=1;
    Project(cam);   // a projection outside a recording
    Check(frameobjects::samples==twoSamples+1,"record: a projection outside the recording samples nothing");
    Counted();
    BeginTick(1);
    const int stepSamples=frameobjects::samples;
    Queue(cam,Pose(0,0,0,0));Step(cam);Fence();
    Check(frameobjects::samples==stepSamples,"record: the camera step does not sample the other objects");
    Counted();
}

// 5. Off: nothing at all.
void Off() {
    Reset();
    TickTo(cam,Pose(0,0,0,0));
    TickTo(cam,Pose(0,1,0,0));
    on=false;
    Frame0(0.5f);
    Project(cam);
    Check(Same(World(cam),Pose(0,1,0,0).m,64),"off: no blend");
    Fence();
    on=true;
}

// 4c. The root sampled at the recording's first projection, not at the camera step: what the recording drew after the
// tick (it makes the pose arrays again before it projects; a vehicle's differs from the step's), and a catch-up pair.
void RootSampling() {
    Reset();
    stepImage=ImageFor(reinterpret_cast<std::uintptr_t>(&StandInStep),kCameraStep);
    const M4 r0=Pose(0,10,0,10),r1=Pose(0,10.4f,0,10),r1drawn=Pose(0,10.6f,0,10),r2=Pose(0,10.8f,0,10);
    OnFoot(r0);
    TwoSamples(r0,r1);
    MoveSoldier(r1drawn);   // the recording's copy after the step, not the step's value
    Frame0(0.5f);
    Check(PosedAt(bodyPose,Pose(0,10.3f,0,10)),"root sample: the pose the recording made, at its first projection");
    Fence();
    Counts c=Counted();
    Check(c.roots==1 && c.rootsLeft==0,"root sample: posed, not left as changed since its step");
    // Catching up: a frame with two ticks (two steps of the root's camera) samples once, the pair two ticks apart.
    Reset();
    OnFoot(r0);
    TickTo(cam,Pose(0,0,0,0),1.0f,&MoveSoldier,&r0);
    BeginTick(2);
    MoveSoldier(r1);Queue(cam,Pose(0,1,0,0));Step(cam);
    MoveSoldier(r2);Queue(cam,Pose(0,2,0,0));Step(cam);
    Fence();
    Frame0(0.5f);
    Check(PosedAt(bodyPose,Pose(0,10.6f,0,10)),"root gap: two ticks apart, drawn alpha into the tick after the one before cur");
    Fence();
    c=Counted();
    Check(c.roots==1 && c.rootsLeft==0 && c.rootsRefused==0,"root gap: one posed root");
    // More than three ticks between samples: a new pair (nothing posed until the next tick's sample).
    BeginTick(4);
    for(int i=0;i<4;++i){Queue(cam,Pose(0,3.0f+static_cast<float>(i),0,0));Step(cam);}
    Fence();
    const Snapshot longGap=Snap();
    Frame0(0.5f);
    Check(Unchanged(longGap),"root gap: past three ticks the pair starts again, nothing posed");
    Fence();
    Check(Counted().roots==0,"root gap: and none counted");
}
}  // namespace
}  // namespace crew

int main() {
    using namespace crew;
    stepImage=ImageFor(reinterpret_cast<std::uintptr_t>(&StandInStep),kCameraStep);
    image=stepImage;
    CameraBlend();
    DrawnEye();
    CameraCuts();
    PrevCur();
    Roots();
    RecordPoint();
    Riding();
    RootSampling();
    RootIdentity();
    Off();
    std::printf("frame_rate_view: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
