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

// The map view (map.cpp; docs/camera-re.md §8): looking down from up to 3 km the near camera's 1000-3000 m far clip
// cuts the ground the view is on (the far pass draws only far-render nodes: the ground and the big buildings, no
// vehicles or enemies). While the map is open its far clip is raised to `farClip` on the four cameras (and the env, so
// a camera the env is copied into afresh agrees) and the near clip, 0.1 m stock (camera +0x28, written only by the
// camera's construction 0x118AFC0), to `nearClip`: the depth range of a view whose nearest ground is 200 m away. What
// the map changed is kept and put back when it closes (a far clip raised only for the map must not stay raised).
namespace {
constexpr std::size_t kCamNear=0x28;
struct Saved { unsigned char* at; float values[4]; };   // far, far pass start, far pass end, near (cameras)
struct MapClip { bool on; Saved env; Saved cam[kCameraCount]; };
MapClip mapClip{};

void SaveClip(Saved& s,unsigned char* at,std::size_t farClip,bool camera) noexcept {
    s.at=at;
    for(int i=0;i<3;++i)s.values[i]=At<float>(at,farClip+static_cast<std::size_t>(i)*4);
    s.values[3]=camera ? At<float>(at,kCamNear) : 0.0f;
}
void PutBack(const Saved& s,std::size_t farClip,bool camera) noexcept {
    if(!s.at || !Readable(s.at,farClip+12,true))return;
    for(int i=0;i<3;++i)Put<float>(s.at,farClip+static_cast<std::size_t>(i)*4,s.values[i]);
    if(camera)Put<float>(s.at,kCamNear,s.values[3]);
}
unsigned char* EnvNow() noexcept {
    const auto holder=At<const unsigned char*>(image,kEnvHolder);
    if(!holder || !Readable(holder+kEnv,8))return nullptr;
    const auto env=At<unsigned char*>(holder,kEnv);
    return env && Readable(env+kDistantFar,4,true) ? env : nullptr;
}
unsigned char* CameraNow(int i) noexcept {
    const auto mgr=At<unsigned char*>(image,kCameraMgr);
    if(!mgr || !Readable(mgr+kCameras+kCameraCount*kCameraStride,8))return nullptr;
    const auto cam=At<unsigned char*>(mgr,kCameras+static_cast<std::size_t>(i)*kCameraStride);
    return cam && Readable(cam+kCamDistantFar,4,true) ? cam : nullptr;
}
}  // namespace

void ViewMapClip(bool on,float farClip,float nearClip) noexcept {
    if(!on) {
        if(!mapClip.on)return;
        // The values from before the map, on the objects they came from (the near clip only where the map changed it).
        if(mapClip.env.at==EnvNow())PutBack(mapClip.env,kFarClip,false);
        for(int i=0;i<kCameraCount;++i)if(mapClip.cam[i].at && mapClip.cam[i].at==CameraNow(i))PutBack(mapClip.cam[i],kCamFar,true);
        mapClip=MapClip{};
        Log("VIEW map closed: the clip planes as they were");
        return;
    }
    if(!mapClip.on) {   // the first frame: what is there now, to put back
        mapClip.on=true;
        if(auto env=EnvNow())SaveClip(mapClip.env,env,kFarClip,false);
        for(int i=0;i<kCameraCount;++i)if(auto cam=CameraNow(i))SaveClip(mapClip.cam[i],cam,kCamFar,true);
        Log("VIEW map open: far clip %.0f m, near %.2f m",farClip,nearClip);
    }
    if(farClip>0.0f) {
        if(auto env=EnvNow())Raise(env,kFarClip,kDistantNear,kDistantFar,farClip);
        for(int i=0;i<kCameraCount;++i)if(auto cam=CameraNow(i))Raise(cam,kCamFar,kCamDistantNear,kCamDistantFar,farClip);
    }
    // The near clip only on the cameras the map saw at its start (a camera made since has no saved value to go back to)
    // and only up from the stock sub-metre one (a camera with its own larger near clip is left alone).
    for(int i=0;i<kCameraCount;++i) {
        Saved& s=mapClip.cam[i];
        if(!s.at || s.at!=CameraNow(i) || !(s.values[3]>0.0f) || s.values[3]>=1.0f)continue;
        Put<float>(s.at,kCamNear,nearClip>s.values[3] ? nearClip : s.values[3]);
    }
}

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
