// Frames above the game's 59.94 Hz, stage 3: the camera between two steps (docs/unlimited-fps-re.md §4.3; the review's
// fps-5, fps-6, fps-8). Developer only: installed with the frame gate (frame_rate.cpp, ini FrameRateDev=1 and a cap).
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
// picture. Right before the next System::Update the step's own state goes back (BeginFrame), so the step, the plugin's
// logic in it and the camera's own next step read the simulation's camera, never a blend. A camera something else wrote
// after its step (the bytes differ from cur) is left alone; a blend something else overwrote is not put back.
//
// The cuts (fps-8). InterpolateRigid already draws the step as it is across a teleport (kCutMove metres or kCutTurn
// radians in one tick). A plugin camera switch is a smaller jump (first person to over the shoulder is a metre or two;
// a scope's field) that a blend would smear over a tick: FrameRateCameraCut makes the step at the switch draw as it is.
//
// The player's root (fps-7, docs/unlimited-fps-re.md §4.3.2). A camera blended a part of a tick behind would draw the
// player's soldier, who moves only on ticks, sliding back and forth against the picture: the root goes with the camera.
// The camera step comes after every object's phase in the tick (0x11995A6 is the last call of the loop), so the camera
// step's hook samples the root after the tick: the soldier's body skeleton's pose[0] ([h+0x900]; h = [camera+0x360]),
// or, riding, the vehicle's ([v+0xEE0], v = [h+0x1548]). On a frame without ticks, right before System::Update (the
// render thread is idle: the frame end's fence 0x11A305A waited for it), D = cur^-1 * InterpolateRigid(prev, cur,
// alpha) is multiplied onto every bone of the draw pose ([inst+0x30], world matrices) of every instance of the body,
// every weapon of [h+0x1950] (+0xF30) and, riding, the vehicle (the rider and their weapons with the vehicle's D: rigid
// passengers). A skin is drawn from each bone's own matrix, so all bones move, not the root only. The next frame puts
// the poses back where they still hold what was written. The first person's view model (firstperson.cpp DrawTransforms)
// takes its anchor from the drawn pose, so D cancels there; its riding path (stepT) does not and is a known gap.
// Not covered: other soldiers in the same vehicle, and frames with ticks (the render thread can read a pose the step
// is writing, the stock race the 60 Hz frame hides; R1 in the plan).
#include "frame_rate_view.h"
#include "frame_rate_logic.h"
#include "fp_view.h"
#include "heli.h"
#include "scope_lens.h"
#include "crew.h"
#include "memory.h"
#include <cstring>
#include <vector>

namespace crew {
namespace {
constexpr unsigned kCameraStepCall=0x11995A6,kCameraStep=0x118DF40;
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
// Projections on frames without ticks: drawn blended, and left as they were (written after the step, or refused).
unsigned blended=0,unblended=0;

// fps-7: the soldier, the vehicle, models, skeleton instances (firstperson.cpp has the same layout).
constexpr std::size_t kTarget=0x360,kVehicle=0x1548,kWeapons=0x1950,kWeaponCount=0x1960,kWeaponModel=0xF30,
                      kBodyModel=0x860,kVehicleModel=0xE40,kModelInst=0xA0,kBaseLod=0x160,kLods=0x250,kLodCount=0x260,
                      kLodStride=0xF0,kLodInst=0x58,kInstCount=0x20,kInstPose=0x30;
constexpr unsigned kSoldierVts[]={0x17CDF28,0x17D0FF8,0x17CF5B8,0x17CF100};   // Ranger, Wing Diver, Fencer, Air Raider
constexpr int kMostInsts=4,kMostWeapons=16;
constexpr std::uint64_t kMostBones=256;
// The root the camera's steps sample: whose (the soldier and their vehicle with their control blocks, the instance),
// its pose[0] after the last tick (cur) and after the one before (prev).
struct Root {
    const void* camera=nullptr;
    const unsigned char* human=nullptr;const void* humanCtrl=nullptr;
    const unsigned char* vehicle=nullptr;const void* vehicleCtrl=nullptr;
    const unsigned char* inst=nullptr;
    float prev[16]{},cur[16]{};
    int samples=0;
};
Root root;
// The poses a frame without ticks wrote: the instance, its pose array and count, where its copies start.
struct Posed { const unsigned char* inst; float* pose; std::uint64_t count; std::size_t at; };
std::vector<Posed> posed;
std::vector<float> posedBefore,posedAfter;
unsigned rootsPosed=0,rootsLeft=0;

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

// The camera step's hook, after the tick: the root of the soldier the camera follows.
void SampleRoot(const void* camera) noexcept {
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
        root=Root{};
        if(!pose)return;
        root.camera=camera;root.human=h;root.humanCtrl=hc;root.vehicle=v;root.vehicleCtrl=vc;root.inst=inst;
    }
    std::memcpy(root.prev,root.cur,sizeof(root.prev));
    std::memcpy(root.cur,pose,sizeof(root.cur));
    if(root.samples<2)++root.samples;
}

// The poses the last frame wrote go back where they still hold what was written.
void RestorePoses() noexcept {
    for(const Posed& p:posed) {
        __try {
            std::uint64_t n=0;
            if(PoseOf(p.inst,&n)!=p.pose || n!=p.count)continue;
            if(!std::memcmp(p.pose,&posedAfter[p.at],static_cast<std::size_t>(n)*64))
                std::memcpy(p.pose,&posedBefore[p.at],static_cast<std::size_t>(n)*64);
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
        const std::size_t at=posedBefore.size();
        posedBefore.insert(posedBefore.end(),pose,pose+n*16);
        for(std::uint64_t b=0;b<n;++b) {
            fpview::M4 m;
            std::memcpy(m.m,pose+b*16,64);
            const fpview::M4 r=fpview::Mul(m,d);
            std::memcpy(pose+b*16,r.m,64);
        }
        posedAfter.insert(posedAfter.end(),pose,pose+n*16);
        posed.push_back(Posed{insts[i],pose,n,at});
    }
}

// A frame without ticks: the root at alpha between the last two ticks, with everything it carries.
void PoseRoot(float alpha) {
    if(root.samples<2)return;
    const unsigned char* const h=root.human;
    const unsigned char* const v=root.vehicle;
    std::uint64_t n=0;
    const float* const live=PoseOf(root.inst,&n);
    float shown[16];
    if(!live || At<const void*>(h,kSelfCtrl)!=root.humanCtrl || RiddenBy(h)!=v ||
       (v && At<const void*>(v,kSelfCtrl)!=root.vehicleCtrl) || std::memcmp(live,root.cur,sizeof(root.cur)) ||
       !framerate::InterpolateRigid(root.prev,root.cur,alpha,shown,kCutMove,kCutTurn)) {
        ++rootsLeft;
        return;
    }
    fpview::M4 cur,inverse,to;
    std::memcpy(cur.m,root.cur,64);
    std::memcpy(to.m,shown,64);
    if(!fpview::InverseAffine(cur,&inverse)) {
        ++rootsLeft;
        return;
    }
    const fpview::M4 d=fpview::Mul(inverse,to);
    ++rootsPosed;
    if(v)PoseModel(v+kVehicleModel,d);
    PoseModel(h+kBodyModel,d);
    const auto list=At<const unsigned char* const*>(h,kWeapons);
    const auto weapons=At<std::int32_t>(h,kWeaponCount);
    if(list && weapons>0 && weapons<=kMostWeapons && Readable(list,static_cast<std::size_t>(weapons)*8))
        for(int i=0;i<weapons;++i)if(list[i])PoseModel(list[i]+kWeaponModel,d);
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
        SampleRoot(camera);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void BeforeProject(void* camera,const int*) {
    if(!on || alphaNow>=1.0f || !camera)return;
    Camera* const c=Find(camera);
    if(!c || !c->havePrev || c->written)return;
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
    for(Camera& c:cameras) {
        if(!c.camera)continue;
        if(c.written) {
            auto* const bytes=static_cast<unsigned char*>(const_cast<void*>(c.camera));
            __try {
                float fov=0.0f;
                std::memcpy(&fov,bytes+kFov,4);
                if(!std::memcmp(bytes+kWorld,c.shown,sizeof(c.shown)))std::memcpy(bytes+kWorld,c.cur,sizeof(c.cur));
                if(fov==c.shownFov)std::memcpy(bytes+kFov,&c.curFov,4);
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            c.written=false;
        }
        if(ticks && c.age<1000)++c.age;
        if(c.camera==root.camera && c.age>1)root=Root{};   // its camera stopped stepping: the next one samples
    }
    RestorePoses();
    alphaNow=alpha;
    if(ticks || alpha>=1.0f)return;
    __try {
        PoseRoot(alpha);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ++rootsLeft;
    }
}

void TakeCounts(unsigned& blendedOut,unsigned& unblendedOut,unsigned& rootsOut,unsigned& rootsLeftOut) noexcept {
    blendedOut=blended;unblendedOut=unblended;rootsOut=rootsPosed;rootsLeftOut=rootsLeft;
    blended=unblended=rootsPosed=rootsLeft=0;
}

bool Install() noexcept {
    if(on)return true;
    if(!edf::Matches(image,kCameraStepCall-5,kCameraStepSig,sizeof(kCameraStepSig))) {
        Log("FRAMERATE the camera step's call (0x%X) is not where docs/unlimited-fps-re.md has it: no interpolation",kCameraStepCall);
        return false;
    }
    if(!scopelens::SetBeforeProject(&BeforeProject)) {
        Log("FRAMERATE the projection call 0x1197B50 could not be shared: no interpolation");
        return false;
    }
    bool changed=false;
    if(!edf::RedirectCall(image+kCameraStepCall,image+kCameraStep,reinterpret_cast<void*>(&CameraStepHook),changed)) {
        Log("FRAMERATE the camera step's call could not be redirected: no interpolation");
        return false;
    }
    posed.reserve(kMostInsts*(kMostWeapons+2));
    posedBefore.reserve(kMostInsts*(kMostWeapons+2)*kMostBones*16);
    posedAfter.reserve(kMostInsts*(kMostWeapons+2)*kMostBones*16);
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
}  // namespace crew
