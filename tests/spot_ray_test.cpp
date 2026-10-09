// The stock spot (原版 Q 标记) from a vehicle on the production src/turretaim.cpp hook (src/spot_ray.h): the native cast
// 0x5A1120 is a recorder; the soldier is stand-in memory. The local player riding gets the camera actually drawn (the
// segment 0x5A1120 casts starts on the eye and runs through the screen's centre); on foot, another soldier, the map's view
// and no camera keep the stock arguments untouched (fb 2026-10-09 aim #3: "那个指向像是玩家默认视角指向的q").
#include "../src/turretaim.cpp"
#include <cstdio>

namespace crew {
unsigned char* image=nullptr;
namespace {
Config testConfig{};
unsigned char* human=nullptr;
bool mapView=false,haveCamera=true;
float camEye[3]={10,25,-37},camDir[3]={0,-0.2f,0.98f};
}  // namespace
const Config& Cfg() noexcept { return testConfig; }
unsigned char* PlayerHuman() noexcept { return human; }
bool SightZoomCanMount(const void*,unsigned) noexcept { return false; }
bool SightZoomMounted(const void*) noexcept { return false; }
bool HighCamOn(const void*) noexcept { return false; }
bool TurretCamHighTransition(const void*) noexcept { return false; }
bool TurretCamTurret(const void*,unsigned) noexcept { return false; }
bool MapOwnsView() noexcept { return mapView; }
bool CameraRay(float* eye,float* dir) noexcept {
    if(!haveCamera)return false;
    std::memcpy(eye,camEye,12);std::memcpy(dir,camDir,12);return true;
}
float MapRay(const float*,const float*,float*) noexcept { return -1; }
void Log(const char*,...) noexcept {}
}  // namespace crew

namespace {
using namespace crew;
int checks=0,failures=0;
void Check(bool ok,const char* what) { ++checks;if(!ok){++failures;std::printf("FAIL %s\n",what);} }
const void* gotSoldier=nullptr;float gotOrigin[4],gotDir[4];int casts=0;
void __fastcall Recorder(void* soldier,const float* origin,const float* dir) {
    ++casts;gotSoldier=soldier;std::memcpy(gotOrigin,origin,16);std::memcpy(gotDir,dir,16);
}
// The point at distance `t` along the drawn camera's centre line, and its distance from the segment 0x5A1120 casts.
float OffSegment(float t) {
    float start[3],end[3];spotray::NativeSegment(gotOrigin,gotDir,start,end);
    float l=0;for(float v:camDir)l+=v*v;l=std::sqrt(l);
    const float p[3]={camEye[0]+camDir[0]/l*t,camEye[1]+camDir[1]/l*t,camEye[2]+camDir[2]/l*t};
    float d[3],w[3],dd=0,wd=0;
    for(int i=0;i<3;++i){d[i]=end[i]-start[i];w[i]=p[i]-start[i];dd+=d[i]*d[i];wd+=w[i]*d[i];}
    const float k=wd/dd;float e=0;
    for(int i=0;i<3;++i){const float r=w[i]-d[i]*k;e+=r*r;}
    return std::sqrt(e);
}
}  // namespace

int main() {
    stockSpot=&Recorder;testConfig.enabled=true;
    alignas(16) unsigned char soldier[0x1600]{},other[0x1600]{},ctrl[16]{};
    human=soldier;
    soldier[edf::kHumanPlayer]=1;edf::Put<void*>(soldier,edf::kHumanPad,soldier);
    other[edf::kHumanPlayer]=1;edf::Put<void*>(other,edf::kHumanPad,other);
    edf::Put<int>(ctrl,8,1);
    alignas(16) const float stockOrigin[4]={3,4,5,1},stockDir[4]={0,0,1,0};
    const auto stockKept=[&](const void* who){return casts>0 && gotSoldier==who && std::memcmp(gotOrigin,stockOrigin,16)==0 && std::memcmp(gotDir,stockDir,16)==0;};
    // On foot: the stock spot as it was.
    SpotHook(soldier,stockOrigin,stockDir);Check(stockKept(soldier),"on foot: the stock eye and aim untouched");
    // Riding (soldier+0x1550's control block alive, the stock test): the drawn camera.
    edf::Put<void*>(soldier,0x1550,ctrl);edf::Put<void*>(other,0x1550,ctrl);
    casts=0;SpotHook(soldier,stockOrigin,stockDir);
    float start[3],end[3];spotray::NativeSegment(gotOrigin,gotDir,start,end);
    Check(casts==1 && std::fabs(start[0]-camEye[0])<1e-4f && std::fabs(start[1]-camEye[1])<1e-4f && std::fabs(start[2]-camEye[2])<1e-4f,
          "riding: the cast starts on the drawn camera's eye (the native 2 m lift taken back)");
    Check(OffSegment(50)<1e-3f && OffSegment(300)<1e-3f && OffSegment(900)<1e-3f,"riding: the cast runs through the screen's centre");
    Check(gotOrigin[3]==1.0f && gotDir[3]==0.0f,"riding: a point and a direction");
    casts=0;SpotHook(other,stockOrigin,stockDir);Check(stockKept(other),"another soldier riding (an NPC, a remote player): stock");
    mapView=true;casts=0;SpotHook(soldier,stockOrigin,stockDir);Check(stockKept(soldier),"the map's view: stock");
    mapView=false;haveCamera=false;casts=0;SpotHook(soldier,stockOrigin,stockDir);Check(stockKept(soldier),"no camera drawn yet: stock");
    haveCamera=true;testConfig.enabled=false;casts=0;SpotHook(soldier,stockOrigin,stockDir);Check(stockKept(soldier),"plugin off: stock");
    testConfig.enabled=true;edf::Put<int>(ctrl,8,0);casts=0;SpotHook(soldier,stockOrigin,stockDir);
    Check(stockKept(soldier),"the vehicle reference expired: stock (the stock code takes the foot branch too)");
    Check(!InstallSpotRay(),"no EDF.dll image: not installed");
    std::printf("spot_ray_test: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
