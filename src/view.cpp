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

namespace crew {
namespace {
constexpr unsigned kEnvHolder=0x20B2990;
constexpr std::size_t kEnv=0x258,kFarClip=0x1A0,kDistantNear=0x1A4,kDistantFar=0x1A8;
constexpr float kOverlap=500.0f;   // the stock 1000 m near camera and 500 m far camera overlap by this
const void* loggedEnv=nullptr;
}  // namespace

void ViewTick() noexcept {
    const float want=Cfg().viewDistance;
    if(want<=0.0f)return;
    const auto holder=At<const unsigned char*>(image,kEnvHolder);
    if(!holder || !Readable(holder+kEnv,8))return;
    const auto env=At<unsigned char*>(holder,kEnv);
    if(!env || !Readable(env+kDistantFar,4,true))return;
    const float was=At<float>(env,kFarClip);
    if(!(was>0.0f) || was>=want)return;   // not set up yet, or already as far (a mission's own)
    const float distantNear=want-kOverlap,distantFar=At<float>(env,kDistantFar);
    Put<float>(env,kFarClip,want);
    if(At<float>(env,kDistantNear)<distantNear)Put<float>(env,kDistantNear,distantNear);
    if(distantFar<want)Put<float>(env,kDistantFar,want);
    if(env!=loggedEnv) {
        loggedEnv=env;
        Log("VIEW far clip %.0f -> %.0f m (far camera from %.0f m)",was,want,At<float>(env,kDistantNear));
    }
}
}  // namespace crew
