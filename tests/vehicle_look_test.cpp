// Production seat-input transaction and camera ownership, with native-layout fixtures.
#include "../src/crew.h"
namespace {
bool focused=true;
DWORD TestWindowProcess(HWND,LPDWORD pid){*pid=focused ? 42 : 43;return 0;}
DWORD TestProcess(){return 42;}
}
#define GetWindowThreadProcessId TestWindowProcess
#define GetCurrentProcessId TestProcess
#include "../src/vehicle_look.cpp"
#undef GetWindowThreadProcessId
#undef GetCurrentProcessId
#include <cstdio>
namespace crew {
unsigned char* image=nullptr;
Config config{};
ULONGLONG frame=0,now=1000;
unsigned char human[0x2000]{},vehicle[0x2000]{},seats[2*kSeatStride]{};
bool onFoot=false,map=false,paused=false,sazabi=false,high=false;
bool HighCamOn(const void*) noexcept{return high;}
bool TurretCamHighTransition(const void*) noexcept{return high;}
const Config& Cfg() noexcept{return config;}
ULONGLONG GameFrame() noexcept{return frame;}
ULONGLONG GameMs() noexcept{return now;}
bool GamePaused() noexcept{return paused;}
bool MapHoldsKeys() noexcept{return map;}
bool IsSazabi(const void*) noexcept{return sazabi;}
bool HumanOnFoot(const unsigned char*) noexcept{return onFoot;}
unsigned char* PlayerHuman() noexcept{return human;}
void Log(const char*,...) noexcept{}
}
int main(){
    using namespace crew;
    int failures=0,checks=0;
    VehicleLookSetReady(true);
    const auto check=[&](bool ok,const char* what){++checks;if(!ok){++failures;std::printf("FAIL %s\n",what);}};
    int ctrl[4]={0,0,1,0},humanCtrl[4]={0,0,1,0};
    Put<void*>(vehicle,kSelfCtrl,ctrl);Put<void*>(human,kSelfCtrl,humanCtrl);
    Put<void*>(vehicle,kSeats,seats);Put<std::uint64_t>(vehicle,kSeatCount,2);
    Put<void*>(human,0x1548,vehicle);Put<void*>(human,kHumanPad,human);human[kHumanPlayer]=1;
    Put<void*>(seats,kSeatRider,human);Put<void*>(seats,kSeatRiderCtrl,humanCtrl);
    seats[kPad]=1;Put<std::uint16_t>(seats,kButtons,0x40);
    Put<float>(seats,kStick,0.5f);Put<float>(seats,kStick+4,-0.25f);
    Put<float>(seats,0x2C0,0.8f);Put<float>(seats,0x2C4,1.0f);Put<float>(seats,0x2E0,0.6f);
    const auto initial=fpview::Mul(fpview::MotionMatrix(0,0,0,0,0,1.2f,fpview::Identity().m+12),fpview::Translate(10,20,30));
    {
        VehicleLookInput input(vehicle);
        check(VehicleLookActive(vehicle),"unarmed local seat offers free look");
        check(At<float>(seats,kStick)==0 && At<float>(seats,kStick+4)==0,"native and plugin controllers see neutral look input");
        check(At<float>(seats,0x2C0)==0.8f && At<float>(seats,0x2C4)==1 && At<float>(seats,0x2E0)==0.6f && At<std::uint16_t>(seats,kButtons)==0x40,
              "steering throttle collective and all buttons remain unchanged");
        auto camera=initial;
        check(VehicleLookCamera(human,camera.m) && camera.m[8]!=initial.m[8],"look rotates the camera relative to the rolled seat");
        check(std::memcmp(camera.m+12,initial.m+12,16)==0,"observation never moves the eye");
        check(std::fabs(fpview::Dot3(camera.m,camera.m+4))<1e-5f && std::fabs(fpview::Length(camera.m)-1)<1e-5f,"camera axes remain orthonormal");
        unsigned char stranger[0x2000]{};
        check(!VehicleLookCamera(stranger,camera.m),"another local player's camera is not rotated");
    }
    check(At<float>(seats,kStick)==0.5f && At<float>(seats,kStick+4)==-0.25f,"both source axes restored byte-for-byte after all controllers");
    const float yaw=look.yaw;
    {VehicleLookInput duplicate(vehicle);}
    check(look.yaw==yaw,"duplicate input traversal cannot turn the view twice in a frame");
    for(int i=0;i<500;++i){++frame;now+=16;VehicleLookInput input(vehicle);}
    check(std::fabs(look.yaw)<=kYawLimit && std::fabs(look.pitch)<=kPitchLimit,"head turn bounded in all seats");
    Put<std::uint16_t>(seats,kButtons,0);
    for(int i=0;i<60;++i){++frame;now+=16;VehicleLookInput input(vehicle);}
    check(!VehicleLookActive(vehicle),"release returns to seat direction and gives input back");
    Put<std::uint16_t>(seats,kButtons,0x40);++frame;
    {VehicleLookInput input(vehicle);}
    onFoot=true;
    check(!VehicleLookActive(vehicle),"getting out invalidates the last seat immediately");
    onFoot=false;Put<void*>(vehicle,kSelfCtrl,humanCtrl);
    check(!VehicleLookActive(vehicle),"same address with another control block cannot own the old view");
    Put<void*>(vehicle,kSelfCtrl,ctrl);map=true;++frame;
    {VehicleLookInput input(vehicle);}
    check(!look.offered && At<float>(seats,kStick)==0.5f,"map cancels observation without consuming controls");
    map=false;focused=false;++frame;{VehicleLookInput input(vehicle);}
    check(!look.offered,"loss of focus cannot rotate or suppress a vehicle");
    focused=true;sazabi=true;config.freeLookButton=config.sazabiLockButton;++frame;{VehicleLookInput input(vehicle);}
    check(!look.offered,"existing Sazabi lock binding remains reserved");
    config.freeLookButton=0x20;++frame;{VehicleLookInput input(vehicle);}
    check(look.offered,"a distinct binding enables Sazabi observation");
    high=true;++frame;{VehicleLookInput input(vehicle);}
    check(!look.offered,"high view retains its existing observation controller");
    high=false;VehicleLookSetReady(false);++frame;{VehicleLookInput input(vehicle);}
    check(!look.offered && At<float>(seats,kStick)==.5f,"unavailable camera hook never consumes look input");
    config.enabled=false;
    check(!VehicleLookActive(vehicle),"disabling the plugin yields immediately");
    std::printf("vehicle look: %d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
