// Execute the production launch, heat and kill-cooling functions without EDF.dll. In particular, a launch that
// reaches the cap must latch before the same frame's low-RPM cooling can drop heat below 1.
#include "../src/drill.cpp"
#include <cstdio>

namespace crew {
unsigned char* image=nullptr;
Config config{};
int overheatLogs=0;
const Config& Cfg() noexcept { return config; }
void Log(const char* format,...) noexcept {
    if(std::strstr(format,"overheated ("))++overheatLogs;
}
// The remaining production entry points are linked too; none of these game services is needed by the heat path.
ULONGLONG GameMs() noexcept { return 1000; }
ULONGLONG GameFrame() noexcept { return 1; }
float GameStep(ULONGLONG ms) noexcept { return static_cast<float>(ms)/1000.0f; }
bool DrillCharge(const unsigned char*,const float*,const float*,float) noexcept { return false; }
void FlareFlames(const unsigned char*,const float (*)[3],const float (*)[3],int,ULONGLONG) noexcept {}
float MapRay(const float*,const float*,float*) noexcept { return -1.0f; }
bool VisitEnemies(const unsigned char*,EnemyVisitor,void*) noexcept { return false; }
bool MapHoldsKeys() noexcept { return true; }
unsigned char* BoneRecord506(const unsigned char*,const wchar_t*) noexcept { return nullptr; }
bool InSession() noexcept { return false; }
bool IsOnlineAuthority(const void*) noexcept { return true; }
bool InstallDrillNet() noexcept { return true; }
bool DrillNetSend(unsigned char*,drill_net::State) noexcept { return false; }
std::int32_t DrillNetController(unsigned char*) noexcept { return -1; }
}

namespace {
int checks=0,failures=0;
void Check(bool ok,const char* what) noexcept {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
}

int main() {
    using namespace crew;
    alignas(16) unsigned char vehicle[kMatrix+64]{};
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    std::memcpy(vehicle+kMatrix,identity,sizeof(identity));
    Drill d{};
    d.player=true;d.heat=0.95f;
    Launch(vehicle,d);
    Check(d.flight==Flight::out && d.heat==1.0f && d.overheated,"launch reaching cap latches overheat immediately");
    const float dt=1.0f/60.0f;
    Heat(vehicle,d,dt/config.drillSpinUpSec,false,dt);
    Check(d.heat<1.0f && d.overheated,"low-RPM cooling in the launch frame preserves the overheat lock");
    Check(overheatLogs==1,"the following heat step does not log overheat twice");
    Heat(vehicle,d,0.0f,false,1.0f);
    Check(d.heat>config.drillResumeHeat && d.overheated,"ordinary cooling above resume keeps the lock");
    Shed(vehicle,d,1.0f);
    Check(d.heat==0.0f && !d.overheated,"kill cooling through resume releases the lock");

    config.drillLaunchHeat=1.0f;
    d=Drill{};
    Launch(vehicle,d);
    Heat(vehicle,d,0.0f,false,dt);
    Check(d.overheated && d.heat<1.0f,"a full-heat launch from cold still latches before cooling");
    Heat(vehicle,d,0.0f,false,config.drillCoolSec);
    Check(d.heat==0.0f && !d.overheated,"ordinary cooling through resume releases the lock");

    config=Config{};
    d=Drill{};
    Launch(vehicle,d);
    Check(!d.overheated && std::fabs(d.heat-config.drillLaunchHeat)<1e-6f,"a launch below the cap stays usable");
    d.heat=0.999f;
    Heat(vehicle,d,1.0f,false,0.1f);
    Check(d.overheated && d.heat==1.0f,"continuous spinning still latches when it reaches the cap");
    Shed(vehicle,d,1.0f-config.drillResumeHeat);
    Check(!d.overheated,"the resume boundary still unlocks after a kill");
    std::printf("drill_heat_test: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
