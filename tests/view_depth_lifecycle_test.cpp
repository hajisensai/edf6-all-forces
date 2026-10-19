// Production shader readiness and clip-plane ownership on private native-layout fixtures.
// This test never reads EDF.dll or opens a game process. Include the implementation so the
// lazy shader-construction boundary, map calls and frame tick execute the shipped code.
#include "../src/view.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace edf {
bool Readable(const void* pointer,std::size_t size,bool writable) noexcept {
    const auto start=reinterpret_cast<std::uintptr_t>(pointer);
    if(!pointer || size>(std::numeric_limits<std::uintptr_t>::max)()-start)return false;
    const auto end=start+size;
    for(auto at=start;at<end;) {
        MEMORY_BASIC_INFORMATION memory{};
        if(!VirtualQuery(reinterpret_cast<const void*>(at),&memory,sizeof(memory)) ||
           memory.State!=MEM_COMMIT || (memory.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
        if(writable && !(memory.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
        const auto next=reinterpret_cast<std::uintptr_t>(memory.BaseAddress)+memory.RegionSize;
        if(next<=at)return false;
        at=next<end ? next : end;
    }
    return true;
}
bool RedirectCall(unsigned char*,void*,void*,bool&) noexcept { return false; }
}
namespace crew {
unsigned char* image=nullptr;
Config testConfig{};
const Config& Cfg() noexcept { return testConfig; }
void Log(const char*,...) noexcept {}
}

namespace {
using namespace crew;
int checks=0,failures=0;
void Check(bool success,const char* description) {
    ++checks;
    if(!success){++failures;std::printf("FAIL: %s\n",description);}
}
struct ClipState { float farClip,distantNear,distantFar,nearClip; };
constexpr ClipState kNative{1000.0f,500.0f,20000.0f,0.1f};
constexpr float kNear[kCameraCount]={0.1f,0.03f,0.25f,2.0f};
ClipState ReadClip(const unsigned char* at,bool camera) {
    const auto offset=camera ? kCamFar : kFarClip;
    return {At<float>(at,offset),At<float>(at,offset+4),At<float>(at,offset+8),camera ? At<float>(at,kCamNear) : 0.0f};
}
bool Same(const ClipState& a,const ClipState& b,bool camera) {
    return a.farClip==b.farClip && a.distantNear==b.distantNear && a.distantFar==b.distantFar &&
           (!camera || a.nearClip==b.nearClip);
}
void WriteNative(unsigned char* at,bool camera,const ClipState& clip) {
    const auto offset=camera ? kCamFar : kFarClip;
    Put<float>(at,offset,clip.farClip);Put<float>(at,offset+4,clip.distantNear);Put<float>(at,offset+8,clip.distantFar);
    if(camera)Put<float>(at,kCamNear,clip.nearClip);
}
bool nativeCreateResult=true,readyDuringCreate=false;
const void* createdBytes=nullptr;
std::size_t createdSize=0;
bool __fastcall NativeCreate(void*,void*,const void* bytes,std::size_t size,void*) {
    readyDuringCreate=ViewDepthReady();createdBytes=bytes;createdSize=size;return nativeCreateResult;
}
struct Fixture {
    static constexpr std::size_t kImageSize=0x22CE000;
    unsigned char* allocation=nullptr;
    unsigned char env[0x1B0]{},replacementEnv[0x1B0]{},holder[0x260]{};
    unsigned char manager[kCameras+kCameraCount*kCameraStride+8]{};
    unsigned char cameras[kCameraCount+1][0x40]{};
    Fixture() {
        allocation=static_cast<unsigned char*>(VirtualAlloc(nullptr,kImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(!allocation){std::puts("FAIL: cannot allocate private image fixture");std::exit(1);}
        DWORD previous=0;
        if(!VirtualProtect(allocation+kComputeCreate,12,PAGE_EXECUTE_READWRITE,&previous)) {
            std::puts("FAIL: cannot prepare private native-create stub");std::exit(1);
        }
        // mov rax, NativeCreate; jmp rax. Arguments stay in the native x64 ABI registers.
        unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
        const auto target=reinterpret_cast<std::uintptr_t>(&NativeCreate);
        std::memcpy(jump+2,&target,sizeof(target));
        std::memcpy(allocation+kComputeCreate,jump,sizeof(jump));
        FlushInstructionCache(GetCurrentProcess(),allocation+kComputeCreate,sizeof(jump));
        image=allocation;Reset();
    }
    ~Fixture() {
        decalDepthReady.store(false,std::memory_order_release);ViewMapClip(false,0,0);ViewTick();
        image=nullptr;VirtualFree(allocation,0,MEM_RELEASE);
    }
    Fixture(const Fixture&)=delete;
    Fixture& operator=(const Fixture&)=delete;
    void Reset() {
        decalDepthReady.store(false,std::memory_order_release);ViewMapClip(false,0,0);ViewTick();
        testConfig=Config{};testConfig.viewDistance=-1;testConfig.bigWorld=3000;
        std::memset(env,0,sizeof(env));std::memset(replacementEnv,0,sizeof(replacementEnv));
        std::memset(holder,0,sizeof(holder));std::memset(manager,0,sizeof(manager));std::memset(cameras,0,sizeof(cameras));
        Put<void*>(allocation,kEnvHolder,holder);Put<void*>(holder,kEnv,env);Put<void*>(allocation,kCameraMgr,manager);
        WriteNative(env,false,kNative);
        for(int i=0;i<kCameraCount;++i) {
            Put<void*>(manager,kCameras+static_cast<std::size_t>(i)*kCameraStride,cameras[i]);
            auto clip=kNative;clip.nearClip=kNear[i];WriteNative(cameras[i],true,clip);
        }
        decalDepthInstalled=false;decalDepthFailureLogged.store(false,std::memory_order_relaxed);decalShader.clear();
        nativeCreateResult=true;readyDuringCreate=false;createdBytes=nullptr;createdSize=0;
    }
    bool Create(bool success=true,const void* bytes=nullptr,std::size_t size=decaldepth::kNativeSize) {
        nativeCreateResult=success;
        if(decalShader.empty())decalShader.assign(8,0xA5);
        return DecalShaderCreateHook(nullptr,nullptr,bytes ? bytes : allocation+kDecalShaderBlob,size,nullptr);
    }
    void Ready() {
        Check(Create(),"the private native shader-creation stub succeeds");
        Check(ViewDepthReady() && !readyDuringCreate,"readiness is published only after modified shader creation returns");
        Check(createdBytes==decalShader.data() && createdSize==decalShader.size(),"native creation receives the replacement shader bytes and length");
    }
    void All(const char* description,float farClip,float distantNear,float distantFar,bool mapNear=false) {
        bool success=Same(ReadClip(env,false),{farClip,distantNear,distantFar,0},false);
        for(int i=0;i<kCameraCount;++i) {
            const float nearClip=mapNear && kNear[i]<1.0f ? 1.0f : kNear[i];
            success=success && Same(ReadClip(cameras[i],true),{farClip,distantNear,distantFar,nearClip},true);
        }
        Check(success,description);
    }
    void NativeAll(const char* description) { All(description,1000,500,20000); }
};

void InstalledWithoutCreation(Fixture& fixture) {
    fixture.Reset();
    decalDepthInstalled=true;
    Check(InstallViewDepth() && !ViewDepthReady(),"an installed creation hook is not proof that its shader has been created");
    ViewTick();fixture.NativeAll("normal view distance cannot write clips before shader creation");
    ViewMapClip(true,35000,1);ViewTick();
    fixture.NativeAll("map-open then frame tick cannot change near or far clips before shader creation");
    ViewMapClip(false,0,0);fixture.NativeAll("closing an unready map leaves every native clip unchanged");
}
void MapOrder(Fixture& fixture,bool normalFirst) {
    fixture.Reset();fixture.Ready();
    if(normalFirst)ViewTick();
    ViewMapClip(true,35000,1);ViewTick();ViewMapClip(true,35000,1);ViewTick();
    fixture.All(normalFirst ? "normal then map then tick preserves the map clip request" : "map then tick preserves the map clip request",35000,500,35000,true);
    ViewMapClip(false,0,0);ViewTick();
    fixture.All("map-close then tick restores normal distance and each camera's native near plane",20000,500,20000);
    decalDepthReady.store(false,std::memory_order_release);ViewTick();
    fixture.NativeAll("readiness loss after map-close restores native clips without retaining unlimited distance");
}
void CreationFailureWithOpenMap(Fixture& fixture) {
    fixture.Reset();fixture.Ready();ViewTick();ViewMapClip(true,35000,1);ViewTick();
    Check(!fixture.Create(false) && !ViewDepthReady(),"a failed modified shader creation revokes readiness");
    ViewMapClip(true,35000,1);
    fixture.NativeAll("readiness loss through the map call unwinds map and normal clip leases to native baselines");
    ViewTick();ViewMapClip(false,0,0);fixture.NativeAll("later unready ticks and map-close cannot reapply an old unlimited clip");
    fixture.Ready();testConfig.viewDistance=3000;ViewTick();fixture.All("successful recreation enables configured normal distance again",3000,500,20000);
    decalDepthReady.store(false,std::memory_order_release);ViewTick();fixture.NativeAll("recreation starts a fresh lease with the native baseline");
}
void UnknownCreation(Fixture& fixture) {
    fixture.Reset();fixture.Ready();ViewTick();
    unsigned char unknown[4]{};
    Check(fixture.Create(true,unknown,sizeof(unknown)) && !ViewDepthReady(),"an unknown DecalManager shader revokes readiness even when native creation succeeds");
    Check(createdBytes==unknown && createdSize==sizeof(unknown),"unknown shader bytes reach native creation unchanged");
    ViewTick();fixture.NativeAll("unknown shader creation rolls an already extended view back to native clips");
    fixture.Ready();ViewTick();
    Check(fixture.Create(true,nullptr,decaldepth::kNativeSize+4) && !ViewDepthReady(),"the native shader address with an unknown length cannot publish readiness");
    ViewTick();fixture.NativeAll("unknown shader length also rolls back extended clips");
    fixture.Ready();ViewTick();decalShader.clear();nativeCreateResult=true;
    Check(DecalShaderCreateHook(nullptr,nullptr,image+kDecalShaderBlob,decaldepth::kNativeSize,nullptr) && !ViewDepthReady(),
          "missing replacement bytes cannot publish readiness");
    ViewTick();fixture.NativeAll("missing replacement bytes also roll back extended clips");
}
void NativeMissionOverwrite(Fixture& fixture,bool mapOpen) {
    fixture.Reset();fixture.Ready();ViewTick();
    if(mapOpen)ViewMapClip(true,35000,1);
    const ClipState newNative{1600,650,27000,0.2f};
    WriteNative(fixture.env,false,newNative);
    for(int i=0;i<kCameraCount;++i)WriteNative(fixture.cameras[i],true,newNative);
    if(!mapOpen)ViewTick();
    decalDepthReady.store(false,std::memory_order_release);
    if(mapOpen)ViewMapClip(true,35000,1);else ViewTick();
    bool success=Same(ReadClip(fixture.env,false),newNative,false);
    for(int i=0;i<kCameraCount;++i)success=success && Same(ReadClip(fixture.cameras[i],true),newNative,true);
    Check(success,mapOpen ? "native mission writes at reused pointers survive rollback while the map is open" :
                           "a mission's new clips at reused pointers become the next native rollback baseline");
}
void CameraReplacement(Fixture& fixture) {
    fixture.Reset();fixture.Ready();ViewTick();ViewMapClip(true,35000,1);
    const ClipState retired=ReadClip(fixture.cameras[0],true),replacement{1800,700,28000,0.07f};
    WriteNative(fixture.cameras[kCameraCount],true,replacement);
    Put<void*>(fixture.manager,kCameras,fixture.cameras[kCameraCount]);
    ViewMapClip(true,35000,1);ViewTick();
    Check(At<float>(fixture.cameras[kCameraCount],kCamFar)==35000 && At<float>(fixture.cameras[kCameraCount],kCamNear)==replacement.nearClip,
          "a replacement camera receives a far lease without adopting the retired camera's near-plane snapshot");
    decalDepthReady.store(false,std::memory_order_release);ViewTick();
    Check(Same(ReadClip(fixture.cameras[0],true),retired,true),"rollback never writes an object retired from the current camera slot");
    Check(Same(ReadClip(fixture.cameras[kCameraCount],true),replacement,true),"the replacement camera rolls back to its own native clip baseline");
    Check(Same(ReadClip(fixture.env,false),kNative,false),"camera replacement does not prevent the environment's native rollback");
}
void EnvironmentReplacement(Fixture& fixture) {
    fixture.Reset();fixture.Ready();ViewTick();ViewMapClip(true,35000,1);
    const ClipState retired=ReadClip(fixture.env,false),replacement{1900,750,29000,0};
    WriteNative(fixture.replacementEnv,false,replacement);Put<void*>(fixture.holder,kEnv,fixture.replacementEnv);
    ViewMapClip(true,35000,1);ViewTick();
    decalDepthReady.store(false,std::memory_order_release);ViewMapClip(false,0,0);
    Check(Same(ReadClip(fixture.env,false),retired,false),"rollback never writes a retired environment pointer");
    Check(Same(ReadClip(fixture.replacementEnv,false),replacement,false),"a replacement environment rolls back to its own native baseline");
}
void FirstPersonMapLease(Fixture& fixture,bool mapCallFirst) {
    fixture.Reset();fixture.Ready();
    ViewWriteNear(fixture.cameras[0],0.03f);ViewTick();
    Check(At<float>(fixture.cameras[0],kCamNear)==0.03f,"first-person near clip is tracked by the shared native clip lease");
    ViewMapClip(true,35000,0.8f);ViewTick();
    Check(At<float>(fixture.cameras[0],kCamNear)==0.8f,"the map temporarily owns the first-person camera's near plane");
    // First-person leaves while the map owns the camera, so it must not overwrite the map's .8 near plane.
    // Revocation must unwind both owners all the way to native .1, not the map's saved first-person .03.
    decalDepthReady.store(false,std::memory_order_release);
    if(mapCallFirst)ViewMapClip(true,35000,0.8f);else ViewTick();
    fixture.NativeAll(mapCallFirst ? "map-first revocation returns the first-person/map near lease to native .1" :
                                    "tick-first revocation returns the first-person/map near lease to native .1");
    ViewMapClip(false,0,0);ViewTick();
    fixture.NativeAll("later map-close and frame ticks cannot resurrect the departed first-person .03 near plane");
}
void NativeDistantStartWithMap(Fixture& fixture) {
    fixture.Reset();fixture.Ready();ViewTick();ViewMapClip(true,35000,1);
    Put<float>(fixture.env,kDistantNear,725);
    for(int i=0;i<kCameraCount;++i)Put<float>(fixture.cameras[i],kCamFar+4,725);
    ViewMapClip(false,0,0);ViewTick();
    fixture.All("map-close preserves a native far-pass start changed while the map was open",20000,725,20000);
    decalDepthReady.store(false,std::memory_order_release);ViewTick();
    fixture.All("readiness rollback also preserves the native far-pass start that the plugin never owned",1000,725,20000);
}
void NativeNearFarWithMap(Fixture& fixture,bool readinessLost) {
    fixture.Reset();fixture.Ready();ViewMapClip(true,6000,0.8f);
    const ClipState nativeOverride{3000,500,20000,0.2f};
    WriteNative(fixture.env,false,nativeOverride);
    for(int i=0;i<kCameraCount;++i)WriteNative(fixture.cameras[i],true,nativeOverride);
    if(readinessLost) {
        decalDepthReady.store(false,std::memory_order_release);ViewTick();
    } else ViewMapClip(false,0,0);
    bool success=Same(ReadClip(fixture.env,false),nativeOverride,false);
    for(int i=0;i<kCameraCount;++i)success=success && Same(ReadClip(fixture.cameras[i],true),nativeOverride,true);
    Check(success,readinessLost ? "readiness loss immediately preserves native far/near writes made after the map override" :
                                 "map-close immediately preserves native far/near writes made after the map override");
}
void ContractFailure(Fixture& fixture) {
    fixture.Reset();fixture.Ready();ViewTick();
    Check(!InstallViewDepth() && !ViewDepthReady(),"a native CPU/shader contract mismatch revokes readiness");
    ViewTick();fixture.NativeAll("an installation failure restores active clip extensions to native values");
}
}

int main() {
    Fixture fixture;
    InstalledWithoutCreation(fixture);MapOrder(fixture,false);MapOrder(fixture,true);
    CreationFailureWithOpenMap(fixture);UnknownCreation(fixture);
    NativeMissionOverwrite(fixture,false);NativeMissionOverwrite(fixture,true);
    CameraReplacement(fixture);EnvironmentReplacement(fixture);
    FirstPersonMapLease(fixture,false);FirstPersonMapLease(fixture,true);
    NativeDistantStartWithMap(fixture);
    NativeNearFarWithMap(fixture,false);NativeNearFarWithMap(fixture,true);ContractFailure(fixture);
    std::printf("view_depth_lifecycle_test: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
