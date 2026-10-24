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
bool turretLook=false;   // turretcam.cpp TurretCamFreeLook: the seat's free look is the turret camera's own
bool TurretCamFreeLook(const void*,unsigned seat) noexcept{return turretLook && seat==0;}
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
        // Third person (War Thunder's view; 2026-10-21: "鼠标中键的时候应该是绕着车转，而不是车后面原地转"): the camera behind
        // and over the vehicle swings round it at its distance, the vehicle staying where the view looked.
        {
            const float at[3]={10,20,30};std::memcpy(vehicle+kPosition,at,12);
            fpview::M4 behind=fpview::Identity();behind.m[12]=10;behind.m[13]=24;behind.m[14]=18;   // looking +z, 12 m back, 4 m up
            float pivot[3];
            check(OrbitPivot(behind.m,at,pivot) && pivot[0]==0 && pivot[1]==0 && pivot[2]==12,"the pivot: on the view's axis, level with the vehicle");
            auto orbit=behind;
            check(VehicleLookCamera(human,orbit.m,true),"third-person look applies");
            const float axis[3]={behind.m[12]+behind.m[8]*12,behind.m[13]+behind.m[9]*12,behind.m[14]+behind.m[10]*12};
            const float was[3]={axis[0]-behind.m[12],axis[1]-behind.m[13],axis[2]-behind.m[14]};
            const float there[3]={axis[0]-orbit.m[12],axis[1]-orbit.m[13],axis[2]-orbit.m[14]};
            check(std::fabs(fpview::Length(there)-fpview::Length(was))<1e-3f,"the eye keeps its distance from the pivot");
            check(fpview::Length(there)>0 && std::fabs(fpview::Dot3(there,orbit.m+8)/fpview::Length(there)-1)<1e-4f,
                  "the view still looks at the pivot: the vehicle stays in the middle");
            const float moved[3]={orbit.m[12]-behind.m[12],orbit.m[13]-behind.m[13],orbit.m[14]-behind.m[14]};
            check(look.yaw!=0 && fpview::Length(moved)>6.0f*std::fmax(std::fabs(look.yaw),std::fabs(look.pitch)),
                  "the eye swings round the vehicle along its arc (not turning in place behind it)");
            auto head=behind;
            check(VehicleLookCamera(human,head.m) && std::memcmp(head.m+12,behind.m+12,16)==0,"first person still turns the head in place");
            auto past=behind;past.m[14]=40;   // the vehicle behind the eye: no pivot, the head turns in place
            check(!OrbitPivot(past.m,at,pivot) && VehicleLookCamera(human,past.m,true) && past.m[14]==40 && past.m[12]==10,
                  "no vehicle ahead: turns in place");
        }
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
    // A turret the turret camera places in third person: its free look is the camera's own (it orbits on the rig and
    // keeps the held point's circle, hud.cpp TurretMark); this one takes neither the stick nor the camera (2026-10-21).
    turretLook=true;++frame;
    {
        VehicleLookInput input(vehicle);
        check(!look.offered && At<float>(seats,kStick)==0.5f && !VehicleLookActive(vehicle),
              "the turret camera's own free look: no stick taken, no in-place turn, the turret camera not suspended");
        auto camera=initial;check(!VehicleLookCamera(human,camera.m,true),"nor its camera turned");
    }
    turretLook=false;++frame;{VehicleLookInput input(vehicle);}
    check(look.offered,"first person / no turret camera: the seat's own look again");
    high=true;++frame;{VehicleLookInput input(vehicle);}
    check(!look.offered,"high view retains its existing observation controller");
    high=false;VehicleLookSetReady(false);++frame;{VehicleLookInput input(vehicle);}
    check(!look.offered && At<float>(seats,kStick)==.5f,"unavailable camera hook never consumes look input");
    config.enabled=false;
    check(!VehicleLookActive(vehicle),"disabling the plugin yields immediately");
    std::printf("vehicle look: %d checks, %d failures\n",checks,failures);return failures ? 1 : 0;
}
