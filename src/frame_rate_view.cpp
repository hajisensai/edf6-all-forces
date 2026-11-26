// Frames above the game's 59.94 Hz, stage 3: the camera between two steps (docs/unlimited-fps-re.md §4.3; the review's
// fps-5, fps-6, fps-8). Installed with the frame gate (frame_rate.cpp, ini FrameRateCap other than 0).
//
// The snapshot (fps-5). In the step loop of System::Update, each viewport's camera steps through 0x118DF40 (called at
// 0x11995A6 only, rcx the camera): its vtable slot 4 (+0x20) writes the camera's world matrix cam+0x220 (rows right, up,
// forward, position) and its field cam+0x24, and every plugin camera feature chained on that slot (map, first person,
// sight zoom, infantry scope, the turret and enemy cameras) has written its own by the time it returns. The hook keeps
// per camera the step before (prev) and this step (cur); a catch-up frame (n = 2, 3) steps it more than once, so prev is
// the one before the last.
//
// The projection (fps-6). The render recording, at the start of System::Update and before the steps, projects each
// viewport's camera (0x1197B50 -> 0x118DFF0: the view +0x1A0 from +0x220, the field +0x24, then the camera goes to Umbra).
// On a frame with ticks the phase is 1 and the camera is drawn as the last step left it, as the game does. On a frame
// without, the camera is drawn at InterpolateRigid(prev, cur, alpha) and the field at the same blend: the picture is one
// tick behind the simulation, never ahead of it. The HUD after System::Update (0x7058B7) sees the same camera as the
// picture. The step's own state goes back right after the same frame's end fence (0x11A305A -> 0x111CC20: the render
// thread has drawn the frame), before the next frame's Application::Update (the network, EOS / EDF6Coop's room, the
// wind) and its step: the blend lives only while the frame is drawn (the review of #386, 3). A camera something else
// wrote after its step (the bytes differ from cur) is left alone; a blend something else overwrote is not put back (the
// field only with its matrix).
//
// The cuts (fps-8). InterpolateRigid already draws the step as it is across a teleport (kCutMove metres or kCutTurn
// radians in one tick). A plugin camera switch is a smaller jump (first person to over the shoulder is a metre or two;
// a scope's field) that a blend would smear over a tick: FrameRateCameraCut makes the step at the switch draw as it is.
//
// The player's root (fps-7, docs/unlimited-fps-re.md §4.3.2). A camera blended a part of a tick behind would draw the
// player's soldier, who moves only on ticks, sliding back and forth against the picture: the root goes with the camera.
// The camera step comes after every object's phase in the tick (0x11995A6 is the last call of the loop), so the camera
// step's hook knows the root after the tick and counts its ticks: the soldier's body skeleton ([h+0x900]; h =
// [camera+0x360]), or, riding, the vehicle's ([v+0xEE0], v = [h+0x1548]); its pose[0] is sampled at the recording's
// first projection, as the recording drew it (StepRoot / SampleRoot). On a frame without ticks, inside System::Update's render
// recording (the call 0x1198FA4 -> 0x11978D0, after the frame's object deletion 0x1195F20; the render thread is idle: the
// last frame's end fence 0x11A305A waited for it; the tick loop after it is skipped) at its first projection (0x1197B50,
// after the recording copied the skeletons' poses again: ProjectStart), D = cur^-1 * InterpolateRigid(prev,
// cur, alpha) is multiplied onto every bone of the draw pose ([inst+0x30], world matrices) of every instance of the body,
// every weapon of [h+0x1950] (+0xF30) and, riding, the vehicle (the rider and their weapons with the vehicle's D: rigid
// passengers). A skin is drawn from each bone's own matrix, so all bones move, not the root only. The same frame's end
// fence puts the poses back where they still hold what was written. A new root (the soldier, the vehicle, their
// control blocks or the instance changed: boarding, a seat, down / revived, a cutscene) cuts its camera too. The first person's view model (firstperson.cpp DrawTransforms)
// takes its anchor from the drawn pose, so D cancels there; its riding path (stepT) does not and is a known gap.
// Not covered: other soldiers in the same vehicle, and frames with ticks (the render thread can read a pose the step
// is writing, the stock race the 60 Hz frame hides; R1 in the plan).
#include "frame_rate_view.h"
#include "frame_rate_objects.h"
#include "render_record.h"
#include "render_pose.h"
#include "frame_rate_logic.h"
#include "fp_view.h"
#include "heli.h"
#include "crew.h"
#include "memory.h"
#include <cstring>
#include <new>
#include <vector>

namespace crew {
namespace {
constexpr unsigned kCameraStepCall=0x11995A6,kCameraStep=0x118DF40;
// The main loop's frame end: the game thread waits for the render thread (frame_pacing.cpp has its loop).
constexpr unsigned kFenceCall=0x11A305A,kFence=0x111CC20;
// System::Update's render recording (System_RecordDraw 0x11978D0, its call 0x1198FA4 after the object deletion
// 0x1195F20 and before the tick loop): the frame rate's turn of render_record.cpp (the call site's single owner; the
// first person places its boxes after) opens the recording; a frame without ticks poses at its first projection
// (docs/unlimited-fps-re.md §4.3.2).
// lea rdx,[rsp+38h]; call 0x118DF40
const unsigned char kCameraStepSig[]={0x48,0x8D,0x54,0x24,0x38,0xE8,0x95,0x49,0xFF,0xFF};
constexpr unsigned kWorld=0x220,kFov=0x24;
constexpr float kCutMove=10.0f,kCutTurn=1.0f;   // in one tick: 600 m/s, 57 degrees
constexpr unsigned kCameras=8;

struct Camera {
    const void* camera=nullptr;
    float prev[16]{},cur[16]{},shown[16]{};
    float prevFov=0.0f,curFov=0.0f,shownFov=0.0f;
    unsigned age=0;              // frames with ticks since its last step
    bool havePrev=false,cut=false,written=false;
};
Camera cameras[kCameras];
float alphaNow=1.0f;
bool on=false;
bool poseDue=false;      // set by BeginFrame on a frame without ticks: the recording's first projection poses, once
// Projections on frames without ticks: drawn blended, and left as they were (written after the step, or refused).
unsigned blended=0,unblended=0;

// fps-7: the soldier, the vehicle, models, skeleton instances (firstperson.cpp has the same layout).
constexpr std::size_t kTarget=0x360,kVehicle=0x1548,kWeapons=0x1950,kWeaponCount=0x1960,kWeaponModel=0xF30,
                      kBodyModel=0x860,kVehicleModel=0xE40,kModelInst=0xA0,kBaseLod=0x160,kLods=0x250,kLodCount=0x260,
                      kLodStride=0xF0,kLodInst=0x58,kInstCount=0x20,kInstPose=0x30;
constexpr unsigned kSoldierVts[]={0x17CDF28,0x17D0FF8,0x17CF5B8,0x17CF100};   // Ranger, Wing Diver, Fencer, Air Raider
constexpr int kMostInsts=4,kMostWeapons=16;
constexpr std::uint64_t kMostBones=256;
// The player's root: whose (the soldier the camera's steps follow and their vehicle with their control blocks, the
// instance), its pose[0] as the recording drew it after the last tick sampled (cur) and after the one before (prev).
struct Root {
    const void* camera=nullptr;
    const unsigned char* human=nullptr;const void* humanCtrl=nullptr;
    const unsigned char* vehicle=nullptr;const void* vehicleCtrl=nullptr;
    const unsigned char* inst=nullptr;
    float prev[16]{},cur[16]{};
    int samples=0;
    std::uint32_t pending=0;   // the camera's steps (ticks) since the last sample
    std::uint32_t gap=1;       // ticks from prev to cur (1 to kMostRootGap)
};
// A frame with two or three ticks (catching up) samples its last only: the pair then spans that many ticks and goes on
// (as frame_rate_objects.cpp's kMostGap); a longer gap starts a new pair.
constexpr std::uint32_t kMostRootGap=3;
Root root;
// The poses a frame without ticks wrote: the instance, its pose array and count, where its copies start.
struct Posed { const unsigned char* inst; float* pose; std::uint64_t count; std::size_t at; };
std::vector<Posed> posed;
std::vector<float> posedBefore,posedAfter;
unsigned rootsPosed=0,rootsLeft=0,rootsRefused=0;   // left: the root changed since its tick; refused: a jump or no rigid

bool IsSoldier(const unsigned char* h) noexcept {
    if(!h || !Readable(h,kWeaponCount+8))return false;
    const auto vt=At<const unsigned char*>(h,0);
    for(unsigned v:kSoldierVts)if(vt==image+v)return true;
    return false;
}
const unsigned char* RiddenBy(const unsigned char* h) noexcept {
    if(HumanOnFoot(h))return nullptr;
    const auto v=At<const unsigned char*>(h,kVehicle);
    return v && Readable(v,kVehicleModel+kLodCount+8) ? v : nullptr;
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
// The skeleton instances a model draws with (its LODs'), each once (firstperson.cpp InstancesOf).
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

// The camera step's hook, after the tick: whose root the camera follows (a change cuts the camera), one more tick for
// it. Its pose is not read here: the recording re-makes the pose arrays before it draws (0x1197950's loop, slot 3), and
// a vehicle's is not the camera step's then (M001-swarm riding a 403 tank, 2026-11-24: the root changed since its tick
// on 1100 of 1110 frames without ticks a 5 s window; on foot 0). SampleRoot takes it at the recording's first
// projection, as the other objects are taken (frame_rate_objects.cpp).
void StepRoot(const void* camera) noexcept {
    if(root.camera && root.camera!=camera)return;   // another camera's steps sample the root
    const auto h=At<const unsigned char*>(camera,kTarget);
    if(!IsSoldier(h)) {
        root=Root{};
        return;
    }
    const auto v=RiddenBy(h);
    const unsigned char* const inst=v ? v+kVehicleModel+kModelInst : h+kBodyModel+kModelInst;
    std::uint64_t n=0;
    const float* const pose=PoseOf(inst,&n);
    const void* const hc=At<const void*>(h,kSelfCtrl);
    const void* const vc=v ? At<const void*>(v,kSelfCtrl) : nullptr;
    if(!pose || root.human!=h || root.humanCtrl!=hc || root.vehicle!=v || root.vehicleCtrl!=vc || root.inst!=inst) {
        // Boarding, a seat, down / revived, a cutscene: the game's own camera switch is a jump a blend would smear over.
        if(root.samples)FrameRateCameraCut(camera);
        root=Root{};
        if(!pose)return;
        root.camera=camera;root.human=h;root.humanCtrl=hc;root.vehicle=v;root.vehicleCtrl=vc;root.inst=inst;
    }
    if(root.pending<=kMostRootGap)++root.pending;
}

// The recording's first projection, its ticks stepped since the last: the root's pose[0] as the recording drew it.
void SampleRoot() noexcept {
    if(!root.camera || !root.pending)return;
    const std::uint32_t gap=root.pending;
    root.pending=0;
    std::uint64_t n=0;
    const float* const pose=PoseOf(root.inst,&n);
    if(!pose || gap>kMostRootGap)root.samples=0;
    if(!pose)return;
    std::memcpy(root.prev,root.cur,sizeof(root.prev));
    std::memcpy(root.cur,pose,sizeof(root.cur));
    root.gap=root.samples ? gap : 1u;
    if(root.samples<2)++root.samples;
}

// The poses the last frame wrote go back where they still hold what was written.
void RestorePoses() noexcept {
    for(const Posed& p:posed) {
        __try {
            std::uint64_t n=0;
            if(PoseOf(p.inst,&n)!=p.pose || n!=p.count)continue;
            // Bone by bone: a bone someone else wrote since is theirs.
            for(std::uint64_t b=0;b<n;++b)
                if(!std::memcmp(p.pose+b*16,&posedAfter[p.at+b*16],64))
                    std::memcpy(p.pose+b*16,&posedBefore[p.at+b*16],64);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    posed.clear();posedBefore.clear();posedAfter.clear();
}

void PoseModel(const unsigned char* model,const fpview::M4& d) {
    const unsigned char* insts[kMostInsts];
    const int count=InstancesOf(model,insts);
    for(int i=0;i<count;++i) {
        std::uint64_t n=0;
        float* const pose=PoseOf(insts[i],&n);
        if(!pose)continue;
        bool done=false;
        for(const Posed& p:posed)done=done || p.pose==pose;
        if(done)continue;
        if(renderpose::stagePose && renderpose::stagePose(insts[i],d.m))continue;
        // The record and both copies before the first write: a fault part way leaves nothing it cannot put back.
        const std::size_t at=posedBefore.size();
        posedBefore.insert(posedBefore.end(),pose,pose+n*16);
        posedAfter.insert(posedAfter.end(),pose,pose+n*16);
        for(std::uint64_t b=0;b<n;++b) {
            fpview::M4 m;
            std::memcpy(m.m,pose+b*16,64);
            const fpview::M4 r=fpview::Mul(m,d);
            std::memcpy(&posedAfter[at+b*16],r.m,64);
        }
        posed.push_back(Posed{insts[i],pose,n,at});
        std::memcpy(pose,&posedAfter[at],static_cast<std::size_t>(n)*64);
    }
}

// A frame without ticks: the root at alpha between the last two ticks, with everything it carries.
// True when it posed them, with their D in `d` (stage 4's riders of the player's vehicle take it).
bool PoseRoot(float alpha,fpview::M4& d) {
    if(root.samples<2)return false;
    const unsigned char* const h=root.human;
    const unsigned char* const v=root.vehicle;
    std::uint64_t n=0;
    const float* const live=PoseOf(root.inst,&n);
    float shown[16];
    if(!live || At<const void*>(h,kSelfCtrl)!=root.humanCtrl || RiddenBy(h)!=v ||
       (v && At<const void*>(v,kSelfCtrl)!=root.vehicleCtrl) || std::memcmp(live,root.cur,sizeof(root.cur))) {
        ++rootsLeft;
        return false;
    }
    // A pair `gap` ticks apart (catching up): alpha into the tick after the one before cur, the cuts times the gap.
    const float gap=static_cast<float>(root.gap);
    const float along=(gap-1.0f+alpha)/gap;
    if(!framerate::InterpolateRigid(root.prev,root.cur,along,shown,kCutMove*gap,kCutTurn*gap)) {
        ++rootsRefused;
        frameobjects::NoteRootRefusal(framerate::RigidRefusal(root.prev,root.cur,along,kCutMove*gap,kCutTurn*gap));
        return false;
    }
    fpview::M4 cur,inverse,to;
    std::memcpy(cur.m,root.cur,64);
    std::memcpy(to.m,shown,64);
    if(!fpview::InverseAffine(cur,&inverse)) {
        ++rootsRefused;
        frameobjects::NoteRootRefusal(framerate::Refusal::kNotRigid);
        return false;
    }
    d=fpview::Mul(inverse,to);
    ++rootsPosed;
    if(v)PoseModel(v+kVehicleModel,d);
    PoseModel(h+kBodyModel,d);
    const auto list=At<const unsigned char* const*>(h,kWeapons);
    const auto weapons=At<std::int32_t>(h,kWeaponCount);
    if(list && weapons>0 && weapons<=kMostWeapons && Readable(list,static_cast<std::size_t>(weapons)*8))
        for(int i=0;i<weapons;++i)if(list[i])PoseModel(list[i]+kWeaponModel,d);
    return true;
}

// The cameras this frame drew blended get their step's state back: the matrix where it still holds the blend, the field
// only with it (a field equal by chance under someone else's matrix is theirs).
void RestoreCameras() noexcept {
    for(Camera& c:cameras) {
        if(!c.camera || !c.written)continue;
        auto* const bytes=static_cast<unsigned char*>(const_cast<void*>(c.camera));
        __try {
            float fov=0.0f;
            std::memcpy(&fov,bytes+kFov,4);
            if(!std::memcmp(bytes+kWorld,c.shown,sizeof(c.shown))) {
                std::memcpy(bytes+kWorld,c.cur,sizeof(c.cur));
                if(fov==c.shownFov)std::memcpy(bytes+kFov,&c.curFov,4);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        c.written=false;
    }
}
void RestoreFrame() noexcept {
    RestoreCameras();
    frameobjects::Restore();   // stage 4: written after fps-7's, back before it
    RestorePoses();
}

using FenceFn=void*(*)(void*,void*,void*,void*);
// The frame end's fence: once the render thread has drawn the frame, the blends go (the review of #386, 3).
void* FrameEndHook(void* a,void* b,void* c,void* d) {
    void* const r=reinterpret_cast<FenceFn>(image+kFence)(a,b,c,d);
    poseDue=false;
    if(on)RestoreFrame();
    return r;
}

// A frame without ticks: the player's root and every other object, at the recording's first projection.
void PoseFrame() noexcept {
    fpview::M4 d;
    bool posedRoot=false;
    __try {
        posedRoot=PoseRoot(alphaNow,d);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ++rootsLeft;
    }
    frameobjects::Pose(alphaNow,root.human,root.vehicle,posedRoot ? d.m : nullptr);   // stage 4: every other object
}

// Not before the render recording: the recording itself, before it projects and draws, makes the skeletons' pose arrays again
// (0x11978D0's loop at 0x1197950: vtable slot 3 of every object in System's +0x428 list; a soldier's
// 0x59A780 -> 0x5750D0 -> 0x6C06A0(h+0x860) -> 0x1100F10(h+0x900) copies each bone record's world, rec+0xB0, over
// inst+0x30), then runs the +0x358 jobs (0x1197A0B). A pose written here was copied over before it was drawn (M001,
// FrameRateCap=1: every pose the frame wrote, 12 a frame, all soldiers, the player's root too), and a sample here read
// the copy the last recording made, before the tick. So both wait for the recording's first projection
// (ProjectStart, from BeforeProject: after the copy and the jobs, before the first draw).
//
// The recording's first projection (render_record.cpp's first, viewport 0's 0x1197B50): the sample first (stage 4:
// every other object's root as the last tick left it and the recording copied it, before fps-7 writes the player's),
// then the poses when due.
void ProjectStart() noexcept {
    __try {
        SampleRoot();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        root.samples=0;
    }
    frameobjects::Sample(poseDue);
    if(poseDue) {
        poseDue=false;
        PoseFrame();
    }
}

using CameraStepFn=void(*)(void*,void*);

Camera* Find(const void* camera) noexcept {
    for(Camera& c:cameras)if(c.camera==camera)return &c;
    return nullptr;
}
Camera* Take() noexcept {
    Camera* stale=nullptr;
    for(Camera& c:cameras) {
        if(!c.camera)return &c;
        if(!stale || c.age>stale->age)stale=&c;
    }
    return stale;
}

void CameraStepHook(void* camera,void* arg) {
    reinterpret_cast<CameraStepFn>(image+kCameraStep)(camera,arg);
    if(!on || !camera)return;
    const auto* const bytes=static_cast<const unsigned char*>(camera);
    __try {
        Camera* c=Find(camera);
        if(!c) {
            c=Take();
            *c=Camera{};
            c->camera=camera;
        }
        if(c->cut || c->age>1 || !c->havePrev) {
            std::memcpy(c->prev,bytes+kWorld,sizeof(c->prev));
            std::memcpy(&c->prevFov,bytes+kFov,4);
        } else {
            std::memcpy(c->prev,c->cur,sizeof(c->prev));
            c->prevFov=c->curFov;
        }
        std::memcpy(c->cur,bytes+kWorld,sizeof(c->cur));
        std::memcpy(&c->curFov,bytes+kFov,4);
        c->havePrev=true;
        c->cut=false;
        c->age=0;
        StepRoot(camera);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void BeforeProject(void* camera,const int*,bool first) noexcept {
    if(!on)return;
    if(first)ProjectStart();
    if(alphaNow>=1.0f || !camera)return;
    Camera* const c=Find(camera);
    if(!c || !c->havePrev || c->written)return;
    // A camera that did not step on the last frame with ticks (age 1 or more) holds the tick before's pair: blending it
    // would draw it going back. Left as it is (counted unblended).
    if(c->age) {
        ++unblended;
        return;
    }
    auto* const bytes=static_cast<unsigned char*>(camera);
    __try {
        float fov=0.0f;
        std::memcpy(&fov,bytes+kFov,4);
        if(std::memcmp(bytes+kWorld,c->cur,sizeof(c->cur)) || fov!=c->curFov ||
           !framerate::InterpolateRigid(c->prev,c->cur,alphaNow,c->shown,kCutMove,kCutTurn)) {
            ++unblended;
            return;
        }
        ++blended;
        c->shownFov=c->prevFov+(c->curFov-c->prevFov)*alphaNow;
        std::memcpy(bytes+kWorld,c->shown,sizeof(c->shown));
        std::memcpy(bytes+kFov,&c->shownFov,4);
        c->written=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
}  // namespace

namespace framerateview {
void BeginFrame(std::uint32_t ticks,float alpha) noexcept {
    if(!on)return;
    RestoreFrame();   // normally done at the last frame's end fence already: nothing left
    for(Camera& c:cameras) {
        if(!c.camera)continue;
        if(ticks && c.age<1000)++c.age;
        if(c.camera==root.camera && c.age>1)root=Root{};   // its camera stopped stepping: the next one samples
    }
    alphaNow=alpha;
    // Nothing is posed here: System::Update deletes objects (0x1195F20) before its render recording, whose first
    // projection poses.
    poseDue=!ticks && alpha<1.0f;
}

void TakeCounts(unsigned& blendedOut,unsigned& unblendedOut,unsigned& rootsOut,unsigned& rootsLeftOut,
                unsigned& rootsRefusedOut) noexcept {
    blendedOut=blended;unblendedOut=unblended;rootsOut=rootsPosed;rootsLeftOut=rootsLeft;rootsRefusedOut=rootsRefused;
    blended=unblended=rootsPosed=rootsLeft=rootsRefused=0;
}

// The pose tables' sizes (a frame's most: the player's soldier, vehicle and weapons), before anything is patched; no
// throw leaves here (Install is noexcept).
bool ReserveTables() noexcept {
    try {
        posed.reserve(kMostInsts*(kMostWeapons+2));
        posedBefore.reserve(kMostInsts*(kMostWeapons+2)*kMostBones*16);
        posedAfter.reserve(kMostInsts*(kMostWeapons+2)*kMostBones*16);
        return true;
    } catch(const std::bad_alloc&) {
        return false;
    }
}

bool Install() noexcept {
    if(on)return true;
    if(!ReserveTables()) {
        Log("FRAMERATE no memory for the pose tables: no interpolation");
        return false;
    }
    if(!edf::Matches(image,kCameraStepCall-5,kCameraStepSig,sizeof(kCameraStepSig))) {
        Log("FRAMERATE the camera step's call (0x%X) is not where docs/unlimited-fps-re.md has it: no interpolation",kCameraStepCall);
        return false;
    }
    if(!renderrecord::Install()) {
        Log("FRAMERATE the render recording is not hooked: no interpolation");
        return false;
    }
    bool changed=false;
    // The restore first: nothing is blended until the camera step's hook samples.
    if(!edf::RedirectCall(image+kFenceCall,image+kFence,reinterpret_cast<void*>(&FrameEndHook),changed)) {
        Log("FRAMERATE the frame end's fence call (0x%X) could not be redirected: no interpolation",kFenceCall);
        return false;
    }
    if(!edf::RedirectCall(image+kCameraStepCall,image+kCameraStep,reinterpret_cast<void*>(&CameraStepHook),changed)) {
        Log("FRAMERATE the camera step's call could not be redirected: no interpolation");
        return false;
    }
    renderrecord::SetProject(renderrecord::Slot::kFrameRate,&BeforeProject);
    frameobjects::Install();
    on=true;
    return true;
}
}  // namespace framerateview

void FrameRateCameraCut(const void* camera) noexcept {
    if(!on)return;
    for(Camera& c:cameras) {
        if(!c.camera || (camera && c.camera!=camera))continue;
        // This pair too (the cut may come after the camera's step this tick): the frames until the next step draw cur.
        std::memcpy(c.prev,c.cur,sizeof(c.prev));
        c.prevFov=c.curFov;
        c.cut=true;
    }
}

bool FrameRateDrawnEye(const void* camera,float eye[3]) noexcept {
    if(!on || alphaNow>=1.0f || !camera || !eye)return false;
    const Camera* const c=Find(camera);
    if(!c || !c->havePrev || c->age)return false;
    if(c->written){std::memcpy(eye,c->shown+12,12);return true;}
    float shown[16];
    __try {
        const auto* const bytes=static_cast<const unsigned char*>(camera);
        float fov=0.0f;
        std::memcpy(&fov,bytes+kFov,4);
        if(std::memcmp(bytes+kWorld,c->cur,sizeof(c->cur)) || fov!=c->curFov ||
           !framerate::InterpolateRigid(c->prev,c->cur,alphaNow,shown,kCutMove,kCutTurn))return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    std::memcpy(eye,shown+12,12);
    return true;
}
}  // namespace crew
