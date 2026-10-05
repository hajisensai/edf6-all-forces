// The view distance (视距; the user, 2026-10-05: "raise the game's view distance a little").
// The game draws the world with two cameras (docs/view-distance-re.md): the near one from 0.1 m to the LightEnv's
// FarClipZ (env+0x1A0, 1000 m in every mission) draws the vehicles, soldiers, enemies and everything else; the far one
// from DistantViewNearClipZ (env+0x1A4, 500 m) to DistantViewFarClipZ (env+0x1A8, 20000 m) draws only far-render
// nodes (the ground, the big buildings). So past 1000 m everything but the scenery was gone. The env is
// *(*(image+0x20B2990)+0x258); the game copies its three values into its cameras every frame (0x1230A0), so the
// plugin raises them at the source once a frame (the mission's load writes the stock ones back each mission):
// FarClipZ to Cfg().viewDistance, the far camera's start kept the stock 500 m short of it (the cameras overlap as
// stock), its end no nearer than the near one's. 0: the stock values are left alone. (Static RE: M; untested in
// game. The depth range grows with it: far/near 30000 at 3000 m, the stock 10000.)
#include "crew.h"
#include "memory.h"

// The cameras themselves too (2026-10-05, the user: "a little farther and it is gone", with VIEW logging 1000 -> 3000
// every mission): the copy from the env into the cameras (0x1230A0, at 0x1232B5) has two callers, a setup (0x9D700)
// and a virtual (0x705530), not a per-frame step, so an env raised after the mission's load never reached them. The
// plugin raises the four cameras' own fields every frame: camera i = *(mgr+0x4D8+i*0x188) (0x1195BE0, a shared_ptr's
// object), mgr = *(image+0x20B2958); +0x2C far clip, +0x30 the far pass's start, +0x34 its end (what 0x1230A0 writes).
namespace crew {
namespace {
constexpr unsigned kEnvHolder=0x20B2990,kCameraMgr=0x20B2958;
constexpr std::size_t kEnv=0x258,kFarClip=0x1A0,kDistantNear=0x1A4,kDistantFar=0x1A8;
constexpr std::size_t kCameras=0x4D8,kCameraStride=0x188,kCamFar=0x2C,kCamDistantNear=0x30,kCamDistantFar=0x34;
constexpr int kCameraCount=4;
constexpr float kOverlap=500.0f;   // the stock 1000 m near camera and 500 m far camera overlap by this
const void* loggedEnv=nullptr;
const void* loggedCam[kCameraCount]{};

// One clip set (far, the far pass's start and end) raised to `want` (never lowered); true when it changed.
bool Raise(unsigned char* at,std::size_t farClip,std::size_t distantNear,std::size_t distantFar,float want) noexcept {
    const float was=At<float>(at,farClip);   // (not `far`: a Windows header macro)
    if(!(was>0.0f) || was>=want)return false;
    Put<float>(at,farClip,want);
    if(At<float>(at,distantNear)<want-kOverlap)Put<float>(at,distantNear,want-kOverlap);
    if(At<float>(at,distantFar)<want)Put<float>(at,distantFar,want);
    return true;
}

void RaiseCameras(float want) noexcept {
    const auto mgr=At<unsigned char*>(image,kCameraMgr);
    if(!mgr || !Readable(mgr+kCameras+kCameraCount*kCameraStride,8))return;
    for(int i=0;i<kCameraCount;++i) {
        const auto cam=At<unsigned char*>(mgr,kCameras+static_cast<std::size_t>(i)*kCameraStride);
        if(!cam || !Readable(cam+kCamDistantFar,4,true))continue;
        const float was=At<float>(cam,kCamFar);
        if(Raise(cam,kCamFar,kCamDistantNear,kCamDistantFar,want) && cam!=loggedCam[i]) {
            loggedCam[i]=cam;
            Log("VIEW camera %d far clip %.0f -> %.0f m",i,was,want);
        }
    }
}
}  // namespace

void ViewTick() noexcept {
    const float want=Cfg().viewDistance;
    if(want<=0.0f)return;
    RaiseCameras(want);
    const auto holder=At<const unsigned char*>(image,kEnvHolder);
    if(!holder || !Readable(holder+kEnv,8))return;
    const auto env=At<unsigned char*>(holder,kEnv);
    if(!env || !Readable(env+kDistantFar,4,true))return;
    const float was=At<float>(env,kFarClip);
    if(Raise(env,kFarClip,kDistantNear,kDistantFar,want) && env!=loggedEnv) {
        loggedEnv=env;
        Log("VIEW far clip %.0f -> %.0f m (far camera from %.0f m)",was,want,At<float>(env,kDistantNear));
    }
}
}  // namespace crew
