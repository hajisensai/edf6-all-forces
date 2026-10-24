// The view distance (视距; the user, 2026-10-05: "raise the game's view distance a little").
// The game draws the world with two cameras (docs/view-distance-re.md, src/view_clip.h): the near one from 0.1 m to
// the LightEnv's FarClipZ (env+0x1A0, 1000 m in every mission) draws the vehicles, soldiers, enemies and the near
// ground; the far one from DistantViewNearClipZ (env+0x1A4, 500 m) to DistantViewFarClipZ (env+0x1A8, 20000 m) draws
// only far-render nodes (the far-only scenery: the horizon's mountains, the far ground, the simulator's sky dome).
// So past 1000 m everything but the scenery was gone. The env is *(*(image+0x20B2990)+0x258); the plugin raises
// FarClipZ to Cfg().viewDistance once a frame (the mission's load writes the stock values back each mission) and the
// far camera's end to no nearer than it. The far camera's start is left as the mission set it (view_clip.h: moving it
// out cut the far-only mountains nearer than it, 2026-10-06). 0: the stock values are left alone. -1 (the default since
// 2026-10-10, the user: "改一下上限，改成无限"): unlimited -- past the world's diagonal and the mission's far pass end
// (crew.h UnlimitedViewDistance), so the plugin's clip is never what cuts a vehicle or a soldier off. (Static RE: M;
// untested in game. The depth range grows with it: far/near 30000 at 3000 m, the stock 10000.)
#include "crew.h"
#include "memory.h"
#include "view_clip.h"
#include "decal_depth.h"
#include <atomic>

// The cameras themselves too (2026-10-05, the user: "a little farther and it is gone", with VIEW logging 1000 -> 3000
// every mission): the copy from the env into the cameras (0x1230A0, at 0x1232B5) has two callers, a setup (0x9D700)
// and a virtual (0x705530), not a per-frame step, so an env raised after the mission's load never reached them. The
// plugin raises the four cameras' own fields every frame: camera i = *(mgr+0x4D8+i*0x188) (0x1195BE0, a shared_ptr's
// object), mgr = *(image+0x20B2958); +0x2C far clip, +0x30 the far pass's start, +0x34 its end (what 0x1230A0 writes).
namespace crew {
namespace {
constexpr unsigned kEnvHolder=0x20B2990,kCameraMgr=0x20B2958;
constexpr std::size_t kEnv=0x258,kFarClip=0x1A0,kDistantNear=0x1A4,kDistantFar=0x1A8;
constexpr std::size_t kCameras=0x4D8,kCameraStride=0x188,kCamFar=0x2C,kCamDistantFar=0x34,kCamNear=0x28;
constexpr int kCameraCount=4;
const void* loggedEnv=nullptr;
const void* loggedCam[kCameraCount]{};
struct Saved { unsigned char* at;float values[4]; };
struct MapClip { bool on;Saved env;Saved cam[kCameraCount]; };
MapClip mapClip{};
// One lease owns only the fields this module actually wrote. Its baseline is captured before the first write,
// including before ViewDistance raises it; a native overwrite becomes the new baseline on the next plugin write.
struct ClipLease { unsigned char* at;float baseline[4],last[4];bool wrote[4]; };
ClipLease envLease{},cameraLease[kCameraCount]{};

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
std::size_t Field(std::size_t farClip,int i) noexcept { return i==3 ? kCamNear : farClip+static_cast<std::size_t>(i)*4; }
ClipLease* LeaseFor(unsigned char* at,bool camera) noexcept {
    ClipLease* out=nullptr;
    if(!camera)out=&envLease;
    else for(int i=0;i<kCameraCount;++i)if(CameraNow(i)==at){out=&cameraLease[i];break;}
    if(out && out->at!=at){*out=ClipLease{};out->at=at;}
    return out;
}
void WriteClip(unsigned char* at,std::size_t farClip,int field,float value,bool camera) noexcept {
    ClipLease* const lease=LeaseFor(at,camera);
    if(!lease)return;
    const float current=At<float>(at,Field(farClip,field));
    if(!lease->wrote[field] || current!=lease->last[field])lease->baseline[field]=current;
    if(current==value)return;
    lease->last[field]=value;lease->wrote[field]=value!=lease->baseline[field];
    Put<float>(at,Field(farClip,field),value);
}
bool Raise(unsigned char* at,std::size_t farClip,float want,bool camera) noexcept {
    viewclip::Clip c{At<float>(at,farClip),At<float>(at,farClip+4),At<float>(at,farClip+8)};
    if(!viewclip::Raise(c,want))return false;
    WriteClip(at,farClip,0,c.farClip,camera);WriteClip(at,farClip,2,c.distantFar,camera);return true;
}
void SaveClip(Saved& s,unsigned char* at,std::size_t farClip,bool camera) noexcept {
    s.at=at;
    for(int i=0;i<3;++i)s.values[i]=At<float>(at,Field(farClip,i));
    s.values[3]=camera ? At<float>(at,kCamNear) : 0.0f;
}
void PutBack(const Saved& s,std::size_t farClip,bool camera) noexcept {
    if(!s.at || !Readable(s.at,farClip+12,true))return;
    const ClipLease* const lease=LeaseFor(s.at,camera);
    if(!lease)return;
    // The near plane goes back to the lease's native baseline, not the map's snapshot: the snapshot is whatever the
    // camera held when the map opened, first person's .03 among them. First person re-applies its own every camera
    // step while it is still on; one that left while the map was open must not have its value resurrected.
    for(int i=0;i<(camera ? 4 : 3);++i)
        if(i!=1 && lease->wrote[i] && At<float>(s.at,Field(farClip,i))==lease->last[i])
            WriteClip(s.at,farClip,i,i==3 ? lease->baseline[i] : s.values[i],camera);
}
void RestoreMap() noexcept {
    if(!mapClip.on)return;
    if(mapClip.env.at==EnvNow())PutBack(mapClip.env,kFarClip,false);
    for(int i=0;i<kCameraCount;++i)if(mapClip.cam[i].at && mapClip.cam[i].at==CameraNow(i))PutBack(mapClip.cam[i],kCamFar,true);
    mapClip=MapClip{};
}
void RestoreLease(ClipLease& lease,unsigned char* current,std::size_t farClip,bool camera) noexcept {
    if(lease.at && lease.at==current && Readable(current,farClip+12,true))
        for(int i=0;i<(camera ? 4 : 3);++i)
            if(lease.wrote[i] && At<float>(current,Field(farClip,i))==lease.last[i])
                Put<float>(current,Field(farClip,i),lease.baseline[i]);
    lease=ClipLease{};
}
void RestoreAllClips() noexcept {
    // Map snapshots may contain the normal view-distance extension. Return the map's lease first, then the
    // original native leases, so a lost shader never leaves an old unlimited far clip in either env or camera.
    RestoreMap();RestoreLease(envLease,EnvNow(),kFarClip,false);
    for(int i=0;i<kCameraCount;++i)RestoreLease(cameraLease[i],CameraNow(i),kCamFar,true);
}
} // namespace

void ViewWriteNear(void* camera,float value) noexcept {
    auto cam=static_cast<unsigned char*>(camera);
    if(!cam || !Readable(cam+kCamNear,4,true))return;
    // Callers gate new overrides on readiness, but may return a lease after readiness was revoked.
    WriteClip(cam,kCamFar,3,value,true);
}
void ViewReturnNear(void* camera,float written) noexcept {
    auto cam=static_cast<unsigned char*>(camera);
    if(!cam || !Readable(cam+kCamNear,4,true) || At<float>(cam,kCamNear)!=written)return;
    ClipLease* const lease=LeaseFor(cam,true);
    if(!lease || !lease->wrote[3] || lease->last[3]!=written)return;
    WriteClip(cam,kCamFar,3,lease->baseline[3],true);
}
void ViewMapClip(bool on,float farClip,float nearClip) noexcept {
    if(!ViewDepthReady()){RestoreAllClips();return;}
    if(!on) {
        if(mapClip.on){RestoreMap();Log("VIEW map closed: the clip planes as they were");}
        return;
    }
    if(!mapClip.on) {
        mapClip.on=true;
        if(auto env=EnvNow())SaveClip(mapClip.env,env,kFarClip,false);
        for(int i=0;i<kCameraCount;++i)if(auto cam=CameraNow(i))SaveClip(mapClip.cam[i],cam,kCamFar,true);
        Log("VIEW map open: far clip %.0f m, near %.2f m",farClip,nearClip);
    }
    if(farClip>0.0f) {
        if(auto env=EnvNow())Raise(env,kFarClip,farClip,false);
        for(int i=0;i<kCameraCount;++i)if(auto cam=CameraNow(i))Raise(cam,kCamFar,farClip,true);
    }
    for(int i=0;i<kCameraCount;++i) {
        Saved& s=mapClip.cam[i];
        if(!s.at || s.at!=CameraNow(i) || !(s.values[3]>0.0f) || s.values[3]>=1.0f)continue;
        WriteClip(s.at,kCamFar,3,nearClip>s.values[3] ? nearClip : s.values[3],true);
    }
}

void ViewTick() noexcept {
    if(!ViewDepthReady()){RestoreAllClips();return;}
    float want=ViewDistanceWanted();
    if(want<=0.0f)return;
    auto env=EnvNow();
    if(Cfg().viewDistance<0.0f && env && At<float>(env,kDistantFar)>want)want=At<float>(env,kDistantFar);
    for(int i=0;i<kCameraCount;++i)if(auto cam=CameraNow(i)) {
        const float was=At<float>(cam,kCamFar);
        if(Raise(cam,kCamFar,want,true) && cam!=loggedCam[i]) {
            loggedCam[i]=cam;Log("VIEW camera %d far clip %.0f -> %.0f m",i,was,want);
        }
    }
    if(!env)return;
    const float was=At<float>(env,kFarClip);
    if(Raise(env,kFarClip,want,false) && env!=loggedEnv) {
        loggedEnv=env;Log("VIEW far clip %.0f -> %.0f m (far camera from %.0f m)",was,want,At<float>(env,kDistantNear));
    }
}
} // namespace crew
// Blood, splashes and bullet/explosion marks all go through DecalManager. Its embedded compute shader used
// stock near/far constants while FirstPerson, the map and ViewDistance changed the world's projection. The
// existing per-draw inverse view-projection already supplies the right depth coefficients, including split views.
namespace crew {
namespace {
constexpr unsigned kDecalShaderCall=0x2D17B6,kComputeCreate=0x11614D0,kDecalShaderBlob=0x1F68FF0;
using ComputeCreateFn=bool(__fastcall*)(void*,void*,const void*,std::size_t,void*);
std::vector<unsigned char> decalShader;
bool decalDepthInstalled=false;
std::atomic<bool> decalDepthReady{false},decalDepthFailureLogged{false};
void DepthFailed(const char* reason) noexcept {
    decalDepthReady.store(false,std::memory_order_release);
    if(!decalDepthFailureLogged.exchange(true,std::memory_order_relaxed))
        Log("VIEW depth disabled: %s; retaining native near/far clip planes",reason);
}

bool NativeDepthContract() noexcept {
    // The constructor's argument setup, native CreateComputeShader ABI, and the exact per-draw inverse-VP
    // builder/upload layout. Refuse a changed CPU contract as well as a changed shader blob.
    struct Span { unsigned rva,size;std::uint64_t fingerprint; };
    constexpr Span spans[]={{0x2D1796,0x25,0xD3E96EDC77F8C94Cull},
                            {0x116153F,0x28,0x21DADB6CAAACF2B1ull},
                            {0x2D2D88,0x29E,0xCF1CAE2B03DF316Eull}};
    for(const Span& span:spans) {
        if(!Readable(image+span.rva,span.size))return false;
        std::uint64_t hash=14695981039346656037ull;
        for(unsigned i=0;i<span.size;++i)hash=(hash^image[span.rva+i])*1099511628211ull;
        if(hash!=span.fingerprint)return false;
    }
    return true;
}
bool __fastcall DecalShaderCreateHook(void* shader,void* context,const void* bytes,std::size_t size,void* linkage) {
    const bool rewritten=bytes==image+kDecalShaderBlob && size==decaldepth::kNativeSize && !decalShader.empty();
    decalDepthReady.store(false,std::memory_order_release);
    if(rewritten){bytes=decalShader.data();size=decalShader.size();}
    const bool created=reinterpret_cast<ComputeCreateFn>(image+kComputeCreate)(shader,context,bytes,size,linkage);
    if(rewritten && created)decalDepthReady.store(true,std::memory_order_release);
    else DepthFailed(rewritten ? "modified compute shader creation failed" : "unknown decal shader at creation");
    return created;
}
} // namespace

bool ViewDepthReady() noexcept { return decalDepthReady.load(std::memory_order_acquire); }

bool InstallViewDepth() noexcept {
    if(decalDepthInstalled)return true;
    if(!NativeDepthContract() || !Readable(image+kDecalShaderBlob,decaldepth::kNativeSize) ||
       !decaldepth::Rewrite(image+kDecalShaderBlob,decaldepth::kNativeSize,&decalShader)) {
        DepthFailed("native shader/CPU contract mismatch");return false;
    }
    bool changed=false;
    decalDepthInstalled=RedirectCall(image+kDecalShaderCall,image+kComputeCreate,reinterpret_cast<void*>(&DecalShaderCreateHook),changed);
    if(!decalDepthInstalled)DepthFailed("compute shader creation hook unavailable");
    Log("HOOK decal depth=%d (awaiting native compute creation)",decalDepthInstalled);
    return decalDepthInstalled;
}
} // namespace crew
