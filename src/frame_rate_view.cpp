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
#include "frame_rate_view.h"
#include "frame_rate_logic.h"
#include "scope_lens.h"
#include "crew.h"
#include "memory.h"
#include <cstring>

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
    }
    alphaNow=alpha;
}

void TakeCounts(unsigned& blendedOut,unsigned& unblendedOut) noexcept {
    blendedOut=blended;unblendedOut=unblended;
    blended=unblended=0;
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
