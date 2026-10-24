// Real optional cross-DLL producers and AutoTurret binding/rule consumer; OS key state is a fixture.
#include <Windows.h>
namespace bindingkeys {
bool down[256]{};
SHORT Key(int vk) noexcept{return vk>=0&&vk<256&&down[vk]?static_cast<SHORT>(0x8000):0;}
HWND Window() noexcept{return nullptr;}
DWORD Process(HWND,LPDWORD pid) noexcept{*pid=GetCurrentProcessId();return 1;}
}
#define GetWindowThreadProcessId bindingkeys::Process
#include "../autoturret/src/designate.cpp"
#undef GetWindowThreadProcessId
#include "../src/crew.h"
#include <cstdio>
namespace autoturret {
unsigned char* image=nullptr;Config cfg{};
void Log(const char*,...) noexcept{}
void SeeVehicle(const void*) noexcept{}
void SeeVehicleOnce(const void*) noexcept{}
void ReloadConfigIfChanged() noexcept{}
float Down(const unsigned char*) noexcept{return 0;}
ULONGLONG Frame() noexcept{return 1;}
bool Same(const void*,const void*) noexcept{return false;}
const Enemy* World(int* count) noexcept{*count=0;return nullptr;}
const std::int32_t* Relations(std::int32_t) noexcept{return nullptr;}
}
namespace crew {
bool PayloadAimWeapon(const void*,unsigned,const void*,const unsigned char** out) noexcept{if(out)*out=nullptr;return false;}
unsigned char* image=nullptr;Config testConfig{};unsigned char* human=nullptr;
bool observer=false,transition=false,mounted=false,lens=true;
sightzoom::Kind testKind=sightzoom::Kind::optical;
const Config& Cfg() noexcept{return testConfig;}
unsigned char* PlayerHuman() noexcept{return human;}
sightzoom::Kind SightZoomView(const void*) noexcept{return testKind;}
bool SightZoomCanMount(const void*,unsigned) noexcept{return lens;}
bool SightZoomMounted(const void*) noexcept{return mounted;}
bool HighCamOn(const void*) noexcept{return observer;}
bool TurretCamHighTransition(const void*) noexcept{return transition;}
bool camTurret=true,firstPerson=false;
bool TurretCamTurret(const void*,unsigned) noexcept{return camTurret;}
bool FirstPersonVehicleView(const void*,unsigned) noexcept{return firstPerson;}
bool CameraRay(float*,float*) noexcept{return false;}
bool MapOwnsView() noexcept{return false;}
bool CameraRayOf(const void*,float*,float*) noexcept{return false;}
void Log(const char*,...) noexcept{}
float MapRay(const float*,const float*,float*) noexcept{return -1;}
}
extern "C" bool __cdecl EDF6VehicleCrew_TurretObserverV1(const void*,unsigned);
extern "C" bool __cdecl EDF6VehicleCrew_CameraTurretV2(const void*,unsigned);
int main(){
    using namespace autoturret;int checked=0,failed=0;
    edf::SetInputSource(&bindingkeys::Key,&bindingkeys::Window);   // the plugins' key source (edf/input_source.h)
    auto expect=[&](bool pass,const char* text){++checked;if(!pass){++failed;std::printf("FAIL %s\n",text);}};
    unsigned char v[0x800]{},seats[edf::kSeatStride*2]{},h[0x400]{},ctrl[16]{};
    crew::human=h;crew::testConfig.enabled=crew::testConfig.sightZoom=true;crew::testConfig.sightZoomKey='Z';crew::testConfig.sightZoomButton=128;
    edf::Put<void*>(v,edf::kSelfCtrl,ctrl);edf::Put<void*>(v,edf::kSeats,seats);edf::Put<std::uint64_t>(v,edf::kSeatCount,2);
    edf::Put<void*>(seats,edf::kSeatRider,h);edf::Put<void*>(seats,edf::kSeatRiderCtrl,ctrl);edf::Put<int>(ctrl,8,1);
    h[edf::kHumanPlayer]=1;edf::Put<void*>(h,edf::kHumanPad,h);
    turretObserver=&EDF6VehicleCrew_TurretObserverV1;
    cameraTurret=[](const void*,unsigned)->bool{return true;};
    // One auto-aim (2026-10-25, "这个自动瞄准是不是多余了一个版本，删掉一个"): no mode key is read and the readout always says
    // auto-aim with its lock binding alone; the old V and the legacy Z are no keys of this plugin's (Z is the optic's).
    cfg.lockKey='Q';cfg.lockButton=4;PilotFrame(v,0,seats,nullptr,nullptr,500);
    bindingkeys::down['V']=bindingkeys::down['Z']=true;PilotFrame(v,0,seats,nullptr,nullptr,500);
    bindingkeys::down['V']=bindingkeys::down['Z']=false;PilotFrame(v,0,seats,nullptr,nullptr,500);
    PublishAim(v,true,nullptr,nullptr);
    edf::aimlink::TurretReadoutV1 r{};
    expect(EDF6AutoTurret_TurretReadoutV1(&r) && r.mode==edf::aimlink::Mode::autoAim && r.modeKey==0 && r.modeButton==0 &&
           r.lockKey=='Q' && r.lockButton==4 && !r.lead,"the readout is the one auto-aim and its lock binding, V / Z pressed or not");
    crew::observer=true;auto rule=PlayerControlRule(v,0,true);
    expect(!rule.steer&&!rule.drag,"C observation cannot restore V1 auto steering even with an existing lock");
    crew::observer=false;crew::transition=true;rule=PlayerControlRule(v,0,true);
    expect(!rule.steer&&!rule.drag,"camera return blend retains observation ownership");
    crew::transition=false;rule=PlayerControlRule(v,0,true);
    expect(rule.steer&&!rule.drag,"normal decoupled view still allows explicit locked auto aim");
    rule=PlayerControlRule(v,0,false);expect(!rule.steer&&!rule.drag,"normal mouse intention remains owner without an explicit lock");
    // Entering the mounted optic makes CameraTurret yield. That false must not restore
    // V1 autonomous auto-aim; the scope's native player inputs own the axes instead.
    cameraTurret=[](const void*,unsigned)->bool{return false;};
    crew::mounted=true;
    rule=PlayerControlRule(v,0,false);expect(!rule.steer&&!rule.drag,"scope entry blocks unlocked auto-aim even after CameraTurret yields");
    rule=PlayerControlRule(v,0,true);expect(!rule.steer&&!rule.drag,"scope entry also blocks locked auto-aim from stealing native optic axes");
    crew::mounted=false;rule=PlayerControlRule(v,0,false);
    expect(rule.steer&&rule.drag,"scope exit restores original legacy control rule");
    // 2026-10-25 ("M163火神防空车(BMPT一样)在第一人称时会自动看向一个特定方向 并且人工转向其他方向仍然会回转到那个方向"): first
    // person yields the turret camera; the real CameraTurretV2 producer still says the player's view turns the seat, so
    // the flak is not taken back for V1 (its own pick every frame, a mouse never past DragDeadzone).
    cameraTurret=&EDF6VehicleCrew_CameraTurretV2;crew::camTurret=false;crew::firstPerson=false;
    rule=PlayerControlRule(v,0,false);
    expect(rule.steer&&rule.drag,"negative control: without the turret camera or first person, V1 auto-aim as before");
    crew::firstPerson=true;rule=PlayerControlRule(v,0,false);
    expect(!rule.steer&&!rule.drag,"first person, no lock: the mouse turns the turret, auto-aim never pulls it back");
    rule=PlayerControlRule(v,0,true);
    expect(rule.steer&&!rule.drag,"first person with a lock: auto-aim onto the lock only");
    expect(!EDF6VehicleCrew_CameraTurretV2(v,1),"first person of another seat's rider is not this player's view");
    crew::firstPerson=false;crew::camTurret=true;
    std::printf("autoturret binding: %d checks, %d failures\n",checked,failed);return failed?1:0;
}
