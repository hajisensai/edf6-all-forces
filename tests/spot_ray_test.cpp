// The stock spot (原版 Q 标记) on the production src/turretaim.cpp hook (src/spot_ray.h): the native cast 0x5A1120 is a
// recorder; the soldier is stand-in memory. VanillaSpot=0 (the default): a local player's spot is never cast (the custom Q,
// qmark.cpp, replaces it; the user, 2026-10-10), an NPC's / another machine's soldier's still is; the plugin off: stock.
// VanillaSpot=1: the local player riding gets the camera actually drawn (the
// segment 0x5A1120 casts starts on the eye and runs through the screen's centre); on foot, another soldier, the map's view
// and no camera keep the stock arguments untouched (fb 2026-10-09 aim #3: "那个指向像是玩家默认视角指向的q").
#include "../src/turretaim.cpp"
#include "../src/player_view.h"
#include <cstdio>

namespace crew {
unsigned char* image=nullptr;
namespace {
Config testConfig{};
unsigned char* human=nullptr;
bool mapView=false,haveCamera=true;
float camEye[3]={10,25,-37},camDir[3]={0,-0.2f,0.98f};
// Split screen: each soldier's own camera; `second` the 2P soldier, whose camera looks elsewhere.
const void* second=nullptr;
float eye2[3]={-400,60,90},dir2[3]={1,0,0};
}  // namespace
const Config& Cfg() noexcept { return testConfig; }
unsigned char* PlayerHuman() noexcept { return human; }
bool SightZoomCanMount(const void*,unsigned) noexcept { return false; }
bool SightZoomMounted(const void*) noexcept { return false; }
bool HighCamOn(const void*) noexcept { return false; }
bool TurretCamHighTransition(const void*) noexcept { return false; }
bool TurretCamTurret(const void*,unsigned) noexcept { return false; }
bool MapOwnsView() noexcept { return mapView; }
bool CameraRay(float*,float*) noexcept { return false; }   // the last pass drawn: never the spot's source
bool CameraRayOf(const void* who,float* eye,float* dir) noexcept {
    if(!haveCamera || !who)return false;
    if(who==second){std::memcpy(eye,eye2,12);std::memcpy(dir,dir2,12);return true;}
    if(who!=human)return false;
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
    Check(!Config{}.vanillaSpot,"VanillaSpot defaults to 0: the stock spot is off after an upgrade");
    alignas(16) unsigned char soldier[0x1600]{},other[0x1600]{},ctrl[16]{};
    human=soldier;
    soldier[edf::kHumanPlayer]=1;edf::Put<void*>(soldier,edf::kHumanPad,soldier);
    other[edf::kHumanPlayer]=1;edf::Put<void*>(other,edf::kHumanPad,other);
    edf::Put<int>(ctrl,8,1);
    alignas(16) const float stockOrigin[4]={3,4,5,1},stockDir[4]={0,0,1,0};
    const auto stockKept=[&](const void* who){return casts>0 && gotSoldier==who && std::memcmp(gotOrigin,stockOrigin,16)==0 && std::memcmp(gotDir,stockDir,16)==0;};
    {   // VanillaSpot=0: no cast for a local player, on foot or riding, whatever the camera; an NPC's goes through.
        alignas(16) unsigned char npc[0x1600]{};
        casts=0;SpotHook(soldier,stockOrigin,stockDir);Check(casts==0,"VanillaSpot=0: a local player's spot on foot is not cast");
        edf::Put<void*>(soldier,0x1550,ctrl);
        casts=0;SpotHook(soldier,stockOrigin,stockDir);Check(casts==0,"VanillaSpot=0: a local player's spot riding is not cast");
        casts=0;SpotHook(other,stockOrigin,stockDir);Check(casts==0,"VanillaSpot=0: 2P's spot is not cast either");
        casts=0;SpotHook(npc,stockOrigin,stockDir);Check(stockKept(npc),"VanillaSpot=0: a soldier no local player drives: stock");
        testConfig.enabled=false;casts=0;SpotHook(soldier,stockOrigin,stockDir);
        Check(stockKept(soldier),"VanillaSpot=0 with the plugin off: the stock spot as it was");
        testConfig.enabled=true;testConfig.npcMarkKey=0;casts=0;SpotHook(soldier,stockOrigin,stockDir);
        Check(casts==1 && gotSoldier==soldier,"VanillaSpot=0 with the custom Q off (NpcMarkKey=0): nothing replaces the stock spot, it is cast");
        testConfig.npcMarkKey=Config{}.npcMarkKey;edf::Put<void*>(soldier,0x1550,nullptr);
    }
    testConfig.vanillaSpot=true;   // the stock spot asked back: the drawn camera's ray, as before
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
    casts=0;SpotHook(other,stockOrigin,stockDir);Check(stockKept(other),"a soldier with no view of its own drawn (an NPC, a remote player): stock");
    // Split screen (integration review 2026-10-09): 2P's spot uses 2P's camera, 1P's stays 1P's whichever pass drew last.
    second=other;casts=0;SpotHook(other,stockOrigin,stockDir);spotray::NativeSegment(gotOrigin,gotDir,start,end);
    Check(casts==1 && std::fabs(start[0]-eye2[0])<1e-4f && std::fabs(start[1]-eye2[1])<1e-4f && end[0]>start[0]+999.0f,"2P riding: 2P's own camera");
    casts=0;SpotHook(soldier,stockOrigin,stockDir);spotray::NativeSegment(gotOrigin,gotDir,start,end);
    Check(std::fabs(start[0]-camEye[0])<1e-4f && OffSegment(300)<1e-3f,"1P riding after 2P's pass: still 1P's camera");
    second=nullptr;
    mapView=true;casts=0;SpotHook(soldier,stockOrigin,stockDir);Check(stockKept(soldier),"the map's view: stock");
    mapView=false;haveCamera=false;casts=0;SpotHook(soldier,stockOrigin,stockDir);Check(stockKept(soldier),"no camera drawn yet: stock");
    haveCamera=true;testConfig.enabled=false;casts=0;SpotHook(soldier,stockOrigin,stockDir);Check(stockKept(soldier),"plugin off: stock");
    testConfig.enabled=true;edf::Put<int>(ctrl,8,0);casts=0;SpotHook(soldier,stockOrigin,stockDir);
    Check(stockKept(soldier),"the vehicle reference expired: stock (the stock code takes the foot branch too)");
    Check(!InstallSpotRay(),"no EDF.dll image: not installed");
    {   // The per-player view store (player_view.h) as hud.cpp keeps it: one pass per HUiHud camera, alternating.
        pview::Store st{};float a[16]{},b2[16]{},got[16];a[0]=1;b2[0]=2;
        const int r1=0,s1=0,r2=0,s2=0;const void* ref1=&r1;const void* sol1=&s1;const void* ref2=&r2;const void* sol2=&s2;
        st.Keep(ref1,sol1,a,1000);st.Keep(ref2,sol2,b2,1001);
        Check(st.Find(sol1,1002,got) && got[0]==1,"store: 1P's view after 2P's pass is still 1P's");
        Check(st.Find(ref2,1002,got) && got[0]==2,"store: 2P found by its reference too");
        st.Keep(ref1,sol1,b2,1003);Check(st.Find(sol1,1004,got) && got[0]==2,"store: a new pass replaces that player's own slot");
        Check(!st.Find(sol1,1003+pview::kFreshMs+1,got),"store: a view not drawn for a second is no one's");
        Check(!st.Find(&got,1004,got) && !st.Find(nullptr,1004,got),"store: an unknown soldier has none");
        int extra[6];for(int i=0;i<6;++i)st.Keep(&extra[i],nullptr,a,2000+i);
        Check(!st.Find(sol1,2005,got) && st.Find(&extra[5],2005,got),"store: four viewports at most, the oldest goes");
    }
    std::printf("spot_ray_test: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
