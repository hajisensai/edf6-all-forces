// Real turret-camera acquisition + payload classification + stores.cpp resource-node/catalog parsing.
// Link payload.cpp, stores.cpp, rounds.cpp, launcher.cpp, edf6common and user32 (/Gy /OPT:REF).
#include "../src/turretcam.cpp"
#include "../src/stores.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>

namespace crew {
unsigned char* image=nullptr;
Config config{};
unsigned char* currentHuman=nullptr;
ULONGLONG clockMs=1000;
const Config& Cfg() noexcept { return config; }
ULONGLONG GameMs() noexcept { return clockMs; }
unsigned char* PlayerHuman() noexcept { return currentHuman; }
void Log(const char*,...) noexcept {}
bool MapHoldsKeys() noexcept { return false; }
PluginBody BodyOf(const void*) noexcept { return PluginBody::none; }
bool IsPlayerJet(const void*) noexcept { return false; }
bool IsHelicopter(const void*) noexcept { return false; }
bool AiGunner(const unsigned char*,const unsigned char*) noexcept { return false; }
bool IsFuelTank(const unsigned char*) noexcept { return false; }
void ClearWeaponLock(unsigned char*) noexcept {}
int WeaponLock(const unsigned char*,float*,float*) noexcept { return 0; }
namespace audio { void LockTone(int,float) noexcept {} }
void UnexpectedBoundary() noexcept { static volatile bool fail=true;if(fail)std::abort(); }
void PumpAircraftPayloadUi(unsigned char*) noexcept { UnexpectedBoundary(); }
int AutoTurretSteers(const void*,unsigned) noexcept { return 0; }
bool AutoTurretReadout(edf::aimlink::TurretReadoutV1*) noexcept { return false; }
bool StabHeld(const void*,float*,float*,float*) noexcept { return false; }
float SightZoomNow(const void*) noexcept { return 1; }
bool highMode=false;
bool HighCamOn(const void*) noexcept { return highMode; }
int cameraReads=0,loftCalls=0;
const void* loftOwner=nullptr;
bool loftAim=false;
bool CameraRay(float* eye,float* dir) noexcept {
    ++cameraReads;eye[0]=0;eye[1]=30;eye[2]=0;dir[0]=0;dir[1]=-0.6f;dir[2]=0.8f;return true;
}
float MapRay(const float* from,const float* to,float* hit) noexcept {
    if(from[1]<0 || to[1]>0 || from[1]==to[1])return -1;
    const float t=-from[1]/(to[1]-from[1]);
    for(int i=0;i<3;++i)hit[i]=from[i]+t*(to[i]-from[i]);
    return vec::Dist(from,hit);
}
void SetLauncherLoft(const void* vehicle,bool aim,float) noexcept { ++loftCalls;loftOwner=vehicle;loftAim=aim; }
bool LauncherLoft(const void*,LoftReadout*) noexcept { return false; }
bool SazabiCamera(const unsigned char*,const float*,const float*,float*,float*) noexcept { UnexpectedBoundary();return false; }
bool IndirectFireSeat(const unsigned char*) noexcept { return true; }
void StabStep(void*,const float*,AimStepFn) noexcept { UnexpectedBoundary(); }
bool ProteusViewLift(const unsigned char*,float*,float*) noexcept { UnexpectedBoundary();return false; }

namespace {
int checks=0,failed=0;
void Check(bool ok,const char* description) { ++checks;if(!ok){++failed;std::printf("FAIL: %s\n",description);} }
void Identity(void* to,float x=0,float y=0,float z=0) {
    const float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1};std::memcpy(to,m,sizeof(m));
}
const float* __fastcall Gravity(void*) { static const float g[4]={0,-9.8f,0,0};return g; }
struct NativeWeapon {
    unsigned char data[0x1600]{},node[0x40]{},muzzle[edf::kMuzzleStride]{},bone[0x110]{};
    void Set(const wchar_t* key,float x,bool lofted=false) {
        // Real heap-string form of the resource node's MSVC wstring, consumed by stores.cpp FileOf.
        Put<void*>(data,8,node);Put<const wchar_t*>(node,0x20,key);
        const auto length=std::wcslen(key);
        Put<std::size_t>(node,0x30,length);Put<std::size_t>(node,0x38,length);
        Put<void*>(data,edf::kMuzzles,muzzle);Put<std::uint64_t>(data,edf::kMuzzleCount,1);
        Put<void*>(muzzle,0,bone);Put<int>(muzzle,edf::kMuzzleMode,1);
        Identity(data+edf::kWeaponMatrix);Identity(muzzle+edf::kMuzzleLocal);Identity(bone+edf::kBoneRows,x,8,10);
        Put<float>(data,edf::kWeaponAmmoSpeed,2);Put<float>(data,edf::kWeaponAmmoGravity,1);
        Put<int>(data,edf::kWeaponAmmoAlive,1200);Put<int>(data,kWeaponAmmo,10);
        Put<int>(data,0x248,20);Put<int>(data,0x20C,-1);
        Put<int>(data,edf::kWeaponMark,lofted ? edf::kMarkLofted : edf::kMarkGround);
        Put<float>(data,edf::kWeaponAccuracy,0.02f);Put<float>(data,edf::kWeaponAccuracyScale,1);
    }
};
struct VehicleFixture {
    unsigned char vehicle[0x1800]{},seats[edf::kSeatStride*2]{},human[0x500]{};
    unsigned char vehicleCtrl[16]{},humanCtrl[16]{},holders[3][kHolderStride]{},holderCtrl[3][16]{};
    unsigned char* seatList[2][3]{};
    NativeWeapon native,store,partner;
    VehicleFixture(unsigned vtable,const wchar_t* key,bool lofted=false) {
        Put<void*>(vehicle,0,image+vtable);Identity(vehicle+kMatrix);
        Put<void*>(vehicle,kSelfCtrl,vehicleCtrl);Put<int>(vehicleCtrl,8,1);
        Put<void*>(vehicle,kSeats,seats);Put<std::uint64_t>(vehicle,kSeatCount,2);
        Put<void*>(vehicle,kHolders,holders);Put<std::uint64_t>(vehicle,kHolderCount,3);
        Put<void*>(human,0,image+0x17CDF28);Put<void*>(human,kSelfCtrl,humanCtrl);Put<int>(humanCtrl,8,1);
        Put<void*>(human,kHumanPad,human);human[kHumanPlayer]=1;
        native.Set(key,-4,lofted);store.Set(L"WEAPON/EDF6VC_RKT_19.SGO",40);
        partner.Set(L"WEAPON/EDF6VC_PROTEUS_NATIVE_CANNON.SGO",4);
        unsigned char* weapons[]={native.data,partner.data,store.data};
        for(int i=0;i<3;++i){Put<void*>(holders[i],kHolderWeapon,weapons[i]);Put<void*>(holders[i],kHolderCtrl,holderCtrl[i]);Put<int>(holderCtrl[i],8,1);}
        for(unsigned s=0;s<2;++s) {
            auto seat=SeatAt(vehicle,s);seatList[s][0]=holders[0];seatList[s][1]=holders[2];
            Put<void*>(seat,kSeatWeapons,seatList[s]);Put<std::uint64_t>(seat,kSeatWeaponCount,2);
            Put<int>(seat,kSeatCamType,1);Put<unsigned char>(seat,kSeatPad,1);
            Put<float>(seat,kSeatAim+kAimAxes,-tcam::kPi);Put<float>(seat,kSeatAim+kAimAxes+4,tcam::kPi);
            Put<float>(seat,kSeatAim+kAimAxes+kAxisStride,-1.45f);Put<float>(seat,kSeatAim+kAimAxes+kAxisStride+4,0.1f);
        }
        Board(0);
    }
    void Board(unsigned s) { auto seat=SeatAt(vehicle,s);Put<void*>(seat,kSeatRider,human);Put<void*>(seat,kSeatRiderCtrl,humanCtrl); }
    void Leave(unsigned s) { auto seat=SeatAt(vehicle,s);Put<void*>(seat,kSeatRider,nullptr);Put<void*>(seat,kSeatRiderCtrl,nullptr); }
};
void Step(VehicleFixture& v) { ++clockMs;PayloadFrame(v.vehicle);TurretCamFrame(v.vehicle); }
void Reset(VehicleFixture& v) { ResetPayload();ResetTurretCam();ResetLauncher();lookOk=true;currentHuman=v.human;highMode=false; }
bool NativeOnly(VehicleFixture& v,unsigned seat=0) {
    unsigned char* selected[3]{};
    return PayloadSightWeapons(v.vehicle,seat,selected,3)==1 && selected[0]==v.native.data && PayloadSightPicked(v.vehicle,seat)==v.native.data;
}
bool LauncherAt(float x) {
    LauncherReadout r{};
    return PlayerLauncher(&r) && r.reach && std::fabs(r.impact[0]-x)<0.001f && std::fabs(r.impact[1])<0.001f && r.range>10 && r.flight>0 && r.rings>0;
}
void CheckNativeLifecycle(unsigned vt,const wchar_t* path,bool lofted,bool custom=true) {
    VehicleFixture v(vt,path,lofted);Reset(v);
    std::size_t size=0;const wchar_t* file=WeaponFile(v.native.data,&size);
    Check(file && size>7 && (custom ? _wcsnicmp(file,L"EDF6VC_",7)==0 : _wcsnicmp(file,L"V_407",5)==0),"real resource-node parser retains the native weapon filename");
    Check(IsLoadoutWeapon(v.native.data)==custom && !IsStoreWeapon(v.native.data) && !StoreOf(v.native.data),"EDF6VC native weapon is not a catalogued add-on store");
    Check(IsLoadoutWeapon(v.store.data) && IsStoreWeapon(v.store.data) && StoreOf(v.store.data),"RKT_19 resource matches actual catalogued Hydra store");
    Check(NativeOnly(v),"real payload query finds native primary before any camera acquisition");
    Step(v);PayloadReadout payload{};
    Check(PlayerPayload(&payload) && payload.count==2 && payload.entry[0].fire==PayloadFire::primary && payload.entry[1].fire==PayloadFire::store,
          "real FireOf distinguishes native primary from catalogued store in same seat");
    const unsigned take=shared.take;
    Check(TurretCamServes(v.vehicle) && shared.v==v.vehicle && shared.seat==SeatAt(v.vehicle,0),"camera acquires native weapon on first frame");
    bool stable=true;
    for(int i=0;i<8;++i){Step(v);stable=stable && NativeOnly(v) && TurretCamServes(v.vehicle) && shared.take==take;}
    Check(stable,"successive real payload/camera frames retain owner without bind/drop/rebind flicker");
    highMode=true;Step(v);
    Check(shared.high && shared.focusValid && shared.focusHit && std::fabs(shared.focus[0]+4)<0.001f,"high camera follows native weapon's real round endpoint");
    if(lofted) {
        highMode=false;LauncherFrame(v.vehicle);
        Check(LauncherAt(-4) && loftOwner==v.vehicle && loftAim,"Katyusha real payload selection publishes actual launcher impact and spread");
        highMode=true;LauncherFrame(v.vehicle);
        Check(LauncherAt(-4) && !loftAim,"Katyusha high mode retains real preview without camera-driven loft");
    }
    VehicleFixture p2(vt,path,lofted);
    const int reads=cameraReads,lofts=loftCalls;
    Step(p2);if(lofted)LauncherFrame(p2.vehicle);
    Check(shared.v==v.vehicle && shared.take==take && !PayloadSightPicked(p2.vehicle,0),"P2 callback cannot replace camera or supply current player's fire-control weapon");
    Check(cameraReads==reads && loftCalls==lofts,"P2 callback cannot consume owner camera or drive its own launcher");
    if(lofted)Check(LauncherAt(-4),"P2 callback preserves owner's real Katyusha preview");
    v.Leave(0);v.Board(1);Step(v);
    Check(NativeOnly(v,1) && !PayloadSightPicked(v.vehicle,0) && !TurretCamServes(v.vehicle),"moving to seat 1 moves payload ownership and releases seat-0 camera immediately");
    Step(p2);Check(!TurretCamServes(p2.vehicle),"P2 cannot acquire camera after current player moves seats");
    v.Leave(1);v.Board(0);Step(v);const unsigned rebound=shared.take;Step(v);
    Check(NativeOnly(v) && TurretCamServes(v.vehicle) && shared.take==rebound,"returning to seat 0 reacquires once and remains stable next frame");
    Check(!v.native.data[0x139] && !v.store.data[0x139],"classification and camera lifecycle never write firing latches");
}
void Run() {
    config.enabled=true;config.stockStores=true;config.debug=false;config.playerJetSwitchKey=0;config.freeLookKey=0;config.freeLookButton=0;
    std::vector<unsigned char> module(0x20B2960);image=module.data();
    unsigned char world[0x80]{},physics[0x30]{};void* gravityTable[]={reinterpret_cast<void*>(&Gravity)};
    Put<void*>(image,0x20B2958,world);Put<void*>(world,0x68,physics);Put<void*>(physics,0x20,gravityTable);
    CheckNativeLifecycle(0x17DADB0,L"WEAPON\\EDF6VC_DRILL_BIT.SGO",false);
    CheckNativeLifecycle(0x17D8B50,L"WEAPON/EDF6VC_KATYUSHA_ROCKETS.SGO",true);
    CheckNativeLifecycle(0x17DEC40,L"WEAPON/EDF6VC_PROTEUS_NATIVE_CANNON.SGO",false);
    CheckNativeLifecycle(0x17DEC40,L"WEAPON/V_407BIGBEGARUTA_CANNON.SGO",false,false);
    VehicleFixture pair(0x17DC620,L"WEAPON/EDF6VC_NATIVE_FLAK_LEFT.SGO");Reset(pair);
    pair.seatList[0][1]=pair.holders[1];pair.seatList[0][2]=pair.holders[2];Put<std::uint64_t>(SeatAt(pair.vehicle,0),kSeatWeaponCount,3);
    Step(pair);unsigned char* weapons[3]{};
    Check(PayloadSightWeapons(pair.vehicle,0,weapons,3)==2 && weapons[0]==pair.native.data && weapons[1]==pair.partner.data,
          "real catalog classification preserves both native flak cofire weapons and excludes the attached store");
    PayloadReadout r{};Check(PlayerPayload(&r) && r.count==3 && r.entry[0].fire==PayloadFire::primary && r.entry[1].fire==PayloadFire::primary && r.entry[2].fire==PayloadFire::store,
          "native two-gun trigger group and add-on store remain distinct in actual readout");
    ResetPayload();ResetTurretCam();ResetLauncher();image=nullptr;
}
} // namespace
} // namespace crew
int main() { crew::Run();std::printf("camera payload lifecycle: %d checks, %d failures\n",crew::checks,crew::failed);return crew::failed ? 1 : 0; }
