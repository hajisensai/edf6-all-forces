// Real optional cross-DLL producers and AutoTurret binding/rule consumer; OS key state is a fixture.
#include <Windows.h>
namespace bindingkeys {
bool down[256]{};
SHORT Key(int vk) noexcept{return vk>=0&&vk<256&&down[vk]?static_cast<SHORT>(0x8000):0;}
HWND Window() noexcept{return nullptr;}
DWORD Process(HWND,LPDWORD pid) noexcept{*pid=GetCurrentProcessId();return 1;}
}
#define GetAsyncKeyState bindingkeys::Key
#define GetForegroundWindow bindingkeys::Window
#define GetWindowThreadProcessId bindingkeys::Process
#include "../autoturret/src/designate.cpp"
#undef GetAsyncKeyState
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#include "../src/crew.h"
#include <cstdio>
namespace autoturret {
unsigned char* image=nullptr;Config cfg{};
void Log(const char*,...) noexcept{}
void SeeVehicle(const void*) noexcept{}
ULONGLONG Frame() noexcept{return 1;}
bool Same(const void*,const void*) noexcept{return false;}
const Enemy* World(int* count) noexcept{*count=0;return nullptr;}
const std::int32_t* Relations(std::int32_t) noexcept{return nullptr;}
}
namespace crew {
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
bool TurretCamTurret(const void*,unsigned) noexcept{return true;}
bool CameraRay(float*,float*) noexcept{return false;}
float MapRay(const float*,const float*,float*) noexcept{return -1;}
}
extern "C" bool __cdecl EDF6VehicleCrew_SightBindingV1(const void*,unsigned,bool,int);
extern "C" bool __cdecl EDF6VehicleCrew_TurretObserverV1(const void*,unsigned);
int main(){
    using namespace autoturret;int checked=0,failed=0;
    auto expect=[&](bool pass,const char* text){++checked;if(!pass){++failed;std::printf("FAIL %s\n",text);}};
    unsigned char v[0x800]{},seats[edf::kSeatStride*2]{},h[0x400]{},ctrl[16]{};
    crew::human=h;crew::testConfig.enabled=crew::testConfig.sightZoom=true;crew::testConfig.sightZoomKey='Z';crew::testConfig.sightZoomButton=128;
    edf::Put<void*>(v,edf::kSelfCtrl,ctrl);edf::Put<void*>(v,edf::kSeats,seats);edf::Put<std::uint64_t>(v,edf::kSeatCount,2);
    edf::Put<void*>(seats,edf::kSeatRider,h);edf::Put<void*>(seats,edf::kSeatRiderCtrl,ctrl);edf::Put<int>(ctrl,8,1);
    h[edf::kHumanPlayer]=1;edf::Put<void*>(h,edf::kHumanPad,h);
    sightBinding=&EDF6VehicleCrew_SightBindingV1;turretObserver=&EDF6VehicleCrew_TurretObserverV1;
    cameraTurret=[](const void*,unsigned)->bool{return true;};
    expect(Reserved(v,0,true,'Z') && !Reserved(v,0,true,'V'),"real current seat reserves configured sight key before first zoom press");
    expect(!Reserved(v,1,true,'Z'),"another seat cannot reserve this player's mode binding");
    cfg.modeKey='Z';cfg.aimMode=0;PilotFrame(v,0,seats,nullptr,nullptr,500);
    bindingkeys::down['Z']=true;PilotFrame(v,0,seats,nullptr,nullptr,500);
    expect(!LeadCircle(),"legacy Z press cannot also toggle automatic aim");
    bindingkeys::down['Z']=false;bindingkeys::down['V']=true;PilotFrame(v,0,seats,nullptr,nullptr,500);
    expect(LeadCircle(),"legacy conflict resolves to V and V toggles the actual mode");
    PilotFrame(v,0,seats,nullptr,nullptr,500);expect(LeadCircle(),"held effective V creates one edge only");
    PublishAim(v,true,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,0);
    edf::aimlink::TurretReadoutV1 r{};edf::aimlink::ModeBindingV1 binding{};
    expect(EDF6AutoTurret_TurretReadoutV1(&r) && r.modeKey=='V',"unchanged V1 readout advertises actual effective V");
    expect(EDF6AutoTurret_ModeBindingV1(&binding) && binding.conflict && binding.requested=='Z' && binding.effective=='V',"optional binding status explains old Z ownership conflict");
    crew::testKind=crew::sightzoom::Kind::none;crew::lens=false;bindingkeys::down['Z']=true;bindingkeys::down['V']=false;
    PilotFrame(v,0,seats,nullptr,nullptr,500);expect(LeadCircle(),"capability/binding change drains a held key instead of making a phantom press");
    bindingkeys::down['Z']=false;PilotFrame(v,0,seats,nullptr,nullptr,500);bindingkeys::down['Z']=true;PilotFrame(v,0,seats,nullptr,nullptr,500);
    expect(!LeadCircle(),"without a real sight capability the configured legacy Z remains usable");
    crew::testKind=crew::sightzoom::Kind::optical;crew::lens=true;cfg.modeButton=128;
    expect(EffectiveModeBinding(v,0,false)==0,"colliding pad binding is disabled instead of silently stealing R3");
    cfg.modeKey='B';expect(EffectiveModeBinding(v,0,true)=='B',"noncolliding user binding is preserved");
    crew::observer=true;auto rule=PlayerControlRule(v,0,false,true);
    expect(!rule.steer&&!rule.drag,"C observation cannot restore V1 auto steering even with an existing lock");
    crew::observer=false;crew::transition=true;rule=PlayerControlRule(v,0,false,true);
    expect(!rule.steer&&!rule.drag,"camera return blend retains observation ownership");
    crew::transition=false;rule=PlayerControlRule(v,0,false,true);
    expect(rule.steer&&!rule.drag,"normal decoupled view still allows explicit locked auto aim");
    rule=PlayerControlRule(v,0,false,false);expect(!rule.steer&&!rule.drag,"normal mouse intention remains owner without an explicit lock");
    // Entering the mounted optic makes CameraTurret yield. That false must not restore
    // V1 autonomous auto-aim; the scope's native player inputs own the axes instead.
    cameraTurret=[](const void*,unsigned)->bool{return false;};
    const bool previousMode=LeadCircle();crew::mounted=true;
    rule=PlayerControlRule(v,0,false,false);expect(!rule.steer&&!rule.drag,"scope entry blocks unlocked AUTO even after CameraTurret yields");
    rule=PlayerControlRule(v,0,false,true);expect(!rule.steer&&!rule.drag,"scope entry also blocks locked AUTO from stealing native optic axes");
    expect(LeadCircle()==previousMode,"entering physical scope does not change AutoTurret mode");
    crew::mounted=false;rule=PlayerControlRule(v,0,false,false);
    expect(rule.steer&&rule.drag&&LeadCircle()==previousMode,"scope exit restores original legacy control rule without changing mode");
    mode=edf::aimlink::Mode::leadCircle;crew::mounted=true;
    rule=PlayerControlRule(v,0,LeadCircle(),false);crew::mounted=false;rule=PlayerControlRule(v,0,LeadCircle(),false);
    expect(!rule.steer&&!rule.drag&&LeadCircle(),"enter/exit cannot restart AUTO when user had selected manual lead mode");
    crew::lens=false;expect(!Reserved(v,0,true,'Z'),"a zoom-kind tag without a real mounted lens does not reserve Z");crew::lens=true;
    sightBinding=nullptr;bindingTried=GetTickCount64();cfg.modeKey='Z';
    expect(EffectiveModeBinding(v,0,true)=='Z',"missing optional peer export preserves legacy standalone key behavior");
    std::printf("autoturret binding: %d checks, %d failures\n",checked,failed);return failed?1:0;
}
