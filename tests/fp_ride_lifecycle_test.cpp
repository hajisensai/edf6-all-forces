// Production Ride / Gather / Rebatch on synthetic native layouts: boarding must return a
// surviving on-foot weapon's batch, without writing a weapon removed in this frame.
#include "../src/crew.h"
namespace {
bool keyDown=false,focused=true,mapOwnsKeys=false,onFoot=true,sazabi=false;
SHORT TestGetAsyncKeyState(int) { return keyDown ? static_cast<SHORT>(0x8000) : 0; }
HWND TestGetForegroundWindow() { return nullptr; }
DWORD TestGetWindowThreadProcessId(HWND,LPDWORD pid) { *pid=focused ? 42 : 43;return 0; }
DWORD TestGetCurrentProcessId() { return 42; }
}
#define GetWindowThreadProcessId TestGetWindowThreadProcessId
#define GetCurrentProcessId TestGetCurrentProcessId
#include "../src/firstperson.cpp"
#undef GetWindowThreadProcessId
#undef GetCurrentProcessId
#include <cstdio>
#include <initializer_list>

namespace edf {
bool ChainVtableSlot(void**,void*,void**) noexcept { return false; }
bool Matches(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept { return false; }
bool IsPlayer(const unsigned char*) noexcept { return false; }
bool RedirectCall(unsigned char*,void*,void*,bool&) noexcept { return false; }
}
namespace crew {
unsigned char* image=nullptr;
Config config{};
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
bool IsSazabi(const void*) noexcept { return sazabi; }
bool HighCamOn(const void*) noexcept { return false; }
bool TurretCamHighTransition(const void*) noexcept { return false; }
float SightZoomNow(const void*) noexcept { return 1.0f; }
bool SightZoomMounted(const void*) noexcept { return false; }
bool HumanOnFoot(const unsigned char*) noexcept { return onFoot; }
bool MapHoldsKeys() noexcept { return mapOwnsKeys; }
bool haveEye=true;
unsigned char rideVehicle[0x1200]{};
fpview::M4 rideFrame=fpview::Identity();
bool VehicleEye(unsigned char*,float* eye,RideView* out) noexcept {
    if(!haveEye)return false;
    *out=RideView{};out->vehicle=rideVehicle;
    std::memcpy(out->frame,rideFrame.m,64);std::memcpy(eye,rideFrame.m+12,12);return true;
}
int VehicleScreens(const unsigned char*,int,const float*,CockpitScreens* out) noexcept { out->n=0;return 0; }
}

int main() {
    using namespace crew;
    edf::SetInputSource(&TestGetAsyncKeyState,&TestGetForegroundWindow);   // the plugin's key source (edf/input_source.h)
    unsigned char human[0x2100]{},cam[0x400]{},weapon[0x1400]{};
    void* vt[2]{};
    unsigned char* list[]={weapon};
    auto model=weapon+kWeaponModel;
    int batch=1,replacement=2;
    int failed=0;
    const auto check=[&](bool ok,const char* what){std::printf("%s %s\n",ok ? "ok" : "FAIL",what);if(!ok)++failed;};
    unsigned char nativeImage[1024]{};
    image=nativeImage;
    Put<void*>(weapon,0,nativeImage+64);Put<void*>(nativeImage,56,nativeImage+128);
    Put<std::uint32_t>(nativeImage,128+0xC,256);
    for(const char* type:{".?AVWeapon_Swing@@",".?AVWeapon_ImpactHammer@@",".?AVWeapon_PileBanker@@",".?AVWeapon_Shield@@"}) {
        std::memcpy(nativeImage+272,type,std::strlen(type)+1);
        check(NativeGrip(weapon),"native melee and shield families preserve the authored holding pose");
    }
    const char* gun=".?AVWeapon_BasicShoot@@";std::memcpy(nativeImage+272,gun,std::strlen(gun)+1);
    check(!NativeGrip(weapon),"firearms retain their bore-aligned placement");
    Put<std::uint32_t>(nativeImage,128+0xC,edf::kImageSize);
    check(!NativeGrip(weapon),"a descriptor outside the native image is rejected");
    image=nullptr;
    config.debug=false;
    Put<void*>(model,0,vt);
    Put<void*>(human,kWeapons,list);
    Put<std::uint64_t>(human,kWeaponCount,1);
    const auto reset=[&](){
        st=State{};st.on=true;st.human=human;st.cam=cam;
        st.weapons[0]=model;st.weaponCount=1;st.inHand[0]=model;st.inHandCount=1;
        for(auto& u:unbatched)u=Unbatched{};
        unbatched[0]=Unbatched{model,&batch};Put<void*>(model,kBatch,nullptr);
        Put<std::uint64_t>(human,kWeaponCount,1);haveEye=true;
    };
    reset();
    check(Ride(cam,human),"a fitted vehicle accepts the ride");
    check(At<void*>(model,kBatch)==&batch,"boarding returns the surviving weapon's original batch");
    check(!unbatched[0].model && st.weaponCount==0 && st.inHandCount==0,"boarding retires the batch record and viewmodel lists");
    for(float roll:{0.0f,1.5707963f,3.14159265f,-1.5707963f}) {
        const float axis[3]={0,0,1};rideFrame=fpview::Rotation(axis,std::cos(roll),std::sin(roll));
        rideFrame.m[12]=5;rideFrame.m[13]=2;rideFrame.m[14]=7;
        check(Ride(cam,human) && std::memcmp(cam+kCamMatrix,rideFrame.m,64)==0,
              "the production ride copies the complete seat frame, including bank/inversion and position");
    }
    // Production instrument pose on a synthetic skeleton, then the same restore used after native draw.
    unsigned char inst[0x50]{},bones[2*kBoneStride]{};float pose[32]{},backup[32]{};
    Put<void*>(inst,kInstBones,bones);Put<int>(inst,kInstCount,2);Put<float*>(inst,kInstPose,pose);
    Put<const wchar_t*>(bones,0,L"vc_i_0_0_d_0_0_1");
    Put<const wchar_t*>(bones+kBoneStride,0,L"vc_i_1_0_d_0_0_1");
    const auto id=fpview::Identity();std::memcpy(pose,id.m,64);std::memcpy(pose+16,id.m,64);
    const float originals[32]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    st.ride.seat=0;st.instrumentsReady=true;st.instruments.value[0]=5;
    InstrumentPose(inst,backup);
    check(pose[0]==0 && pose[16]==1,"inactive digit segment collapses, another seat's instrument is unchanged");
    Unpose(inst,backup);
    check(std::memcmp(pose,originals,sizeof(pose))==0,"the native draw pose is restored byte for byte");
    reset();Put<std::uint64_t>(human,kWeaponCount,0);
    check(Ride(cam,human),"boarding after the weapon was removed");
    check(!At<void*>(model,kBatch) && !unbatched[0].model,"a removed weapon is never written and its stale record is dropped");
    reset();Put<void*>(model,kBatch,&replacement);
    check(Ride(cam,human) && At<void*>(model,kBatch)==&replacement,"boarding preserves a replacement batch owned by the game");
    reset();haveEye=false;
    check(!Ride(cam,human) && !At<void*>(model,kBatch) && unbatched[0].model==model,"no cockpit leaves the on-foot state for Leave to restore");
    st.on=true;st.riding=true;st.cam=cam;st.screens.n=0;
    check(!FirstPersonHidesStockHud(cam),"an eye-only passenger seat keeps the stock HUD");
    st.screens.n=1;
    check(FirstPersonHidesStockHud(cam),"a seat with replacement screens hides the stock HUD");
    config.fpHideStockHud=false;
    check(!FirstPersonHidesStockHud(cam),"the hide stock HUD preference still controls fitted screens");
    config.fpHideStockHud=true;
    check(!FirstPersonHidesStockHud(human),"another camera's HUD is never hidden");
    camOk=drawOk=true;
    const auto resetKey=[&](){toggled=false;keyWas=false;keyCancelled=false;keyDown=false;focused=true;mapOwnsKeys=false;onFoot=true;sazabi=false;};
    resetKey();keyDown=true;Toggle(human);keyDown=false;Toggle(human);
    check(toggled,"an uninterrupted physical press and release toggles first person");
    resetKey();keyDown=true;Toggle(human);focused=false;Toggle(human);
    check(!toggled,"losing focus while held does not act as release");
    focused=true;Toggle(human);keyDown=false;Toggle(human);
    check(!toggled,"returning focus while held discards the interrupted gesture");
    keyDown=true;Toggle(human);keyDown=false;Toggle(human);
    check(toggled,"a fresh gesture after focus returns still works");
    resetKey();keyDown=true;Toggle(human);mapOwnsKeys=true;Toggle(human);
    check(!toggled,"map input ownership does not act as release");
    mapOwnsKeys=false;Toggle(human);keyDown=false;Toggle(human);
    check(!toggled,"closing the map while held discards the interrupted gesture");
    resetKey();keyDown=true;Toggle(human);Toggle(nullptr);keyDown=false;Toggle(human);
    check(!toggled,"a missing player cancels without toggling");
    resetKey();keyDown=true;Toggle(human);onFoot=false;keyDown=false;Toggle(human);
    check(!toggled,"holding the view key across boarding does not toggle");
    keyDown=true;Toggle(human);keyDown=false;Toggle(human);
    check(toggled,"a new press and release in the vehicle still toggles");
    resetKey();onFoot=false;sazabi=true;keyDown=true;Toggle(human);keyDown=false;Toggle(human);
    check(!toggled,"the Sazabi melee key keeps ownership of its press");
    toggled=true;camOk=true;drawOk=false;
    check(std::strcmp(NotWanted(cam,human),"hooks unavailable")==0,"a camera hook without a draw hook cannot activate first person");
    camOk=false;drawOk=true;
    check(std::strcmp(NotWanted(cam,human),"hooks unavailable")==0,"a draw hook without a camera hook cannot activate first person");
    return failed ? 1 : 0;
}
