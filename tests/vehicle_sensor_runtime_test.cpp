// Production game-thread optical collector; synthetic native seat and hostile lock registry.
#include "../src/sight_contacts.cpp"
#include <cstdio>
namespace crew {
unsigned char* image=nullptr;
Config sensorConfig{};
unsigned char* sensorHuman=nullptr;
bool sensorMap=false,sensorRay=true,sensorOnFoot=false,sensorGunship=false,sensorRegistry=true,sensorCamera=true;
const char* sensorClass="505_Tank";
const wchar_t* sensorWeaponFile=L"V603_FLAK_GUNH01_DLC_L.SGO";
const wchar_t* WeaponFile(const unsigned char*,std::size_t* length) noexcept { *length=std::wcslen(sensorWeaponFile);return sensorWeaponFile; }
int sensorRays=0;
const Config& Cfg() noexcept { return sensorConfig; }
bool MapHoldsKeys() noexcept { return sensorMap; }
bool MapRayReady() noexcept { return sensorRay; }
unsigned char* PlayerHuman() noexcept { return sensorHuman; }
bool HumanOnFoot(const unsigned char*) noexcept { return sensorOnFoot; }
bool KnownVehicle(const void*) noexcept { return true; }
bool GunshipCrewSeats(const void*) noexcept { return sensorGunship; }
PluginBody BodyOf(const void*) noexcept { return sensorGunship ? PluginBody::jet : PluginBody::none; }
const char* VehicleClassName(const void*) noexcept { return sensorClass; }
bool CameraRay(float* eye,float* look) noexcept {
    eye[0]=eye[1]=eye[2]=look[0]=look[1]=0;look[2]=1;return sensorCamera;
}
struct SensorTarget { float at[3]; int object; } sensorTargets[64]{};
int sensorTargetCount=0,sensorObjects[64]{};
bool VisitEnemies(const unsigned char*,EnemyVisitor visitor,void* ctx) noexcept {
    if(!sensorRegistry)return false;
    for(int i=0;i<sensorTargetCount;++i)visitor(ctx,&sensorObjects[sensorTargets[i].object],sensorTargets[i].at);
    return true;
}
float MapRay(const float*,const float* point,float*) noexcept {
    ++sensorRays;return point[0]<0 ? 10.0f : -1.0f; // a wall in front of the left-hand lock point
}
}
int main() {
    using namespace crew;
    int failed=0,checks=0;
    const auto check=[&](bool value,const char* message){++checks;if(!value){++failed;std::printf("FAIL: %s\n",message);}};
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!image)return 2;
    unsigned char human[0x1800]{},other[0x1800]{},vehicle[0x3000]{},seats[2*kSeatStride]{},control[16]{},humanControl[16]{};
    unsigned char weapon[0x1000]{},holder[kHolderStride]{},weaponControl[16]{};
    unsigned char* seatWeapons[]={holder};
    Put<void*>(holder,kHolderCtrl,weaponControl);Put<int>(weaponControl,8,1);Put<void*>(holder,kHolderWeapon,weapon);
    Put<float>(weapon,kWeaponSpeed,10);Put<int>(weapon,kWeaponAlive,100);
    Put<void*>(seats,kSeatWeapons,seatWeapons);Put<std::uint64_t>(seats,kSeatWeaponCount,1);
    sensorHuman=human;sensorConfig.enabled=true;sensorConfig.stockVehicleHud=true;
    Put<void*>(human,kHumanVehicleCtrl,control);Put<void*>(human,kHumanVehicleCtrl-8,vehicle);
    Put<void*>(human,edf::kHumanPad,human);human[edf::kHumanPlayer]=1;
    Put<void*>(vehicle,kSelfCtrl,control);Put<int>(control,8,1);Put<int>(humanControl,8,1);
    Put<void*>(vehicle,kSeats,seats);Put<std::uint64_t>(vehicle,kSeatCount,2);
    Put<void*>(seats,kSeatRider,human);Put<void*>(seats,kSeatRiderCtrl,humanControl);
    sensorTargets[0]={{0,0,100},0};sensorTargets[1]={{0,0,110},0}; // one object, two lock points
    sensorTargets[2]={{-20,0,100},1};sensorTargets[3]={{30,0,100},2};sensorTargetCount=4;
    VehicleSensorReadout result{};
    check(PlayerVehicleContacts(&result) && result.kind==VehicleSensorKind::optical && result.count==2 && sensorRays==3,
          "native local tank seat gets distinct hostile visible lock points, with one LOS ray per object");
    check(result.contact[0].at[2]==100 && result.contact[1].at[0]==30,"blocked target is absent and native world positions remain exact");
    check(VehicleHasThermal(vehicle,0),"supported armed tank has an explicit thermal trait");
    sensorClass="603_Flak";sensorWeaponFile=L"EDF6VC_VULCAN_HE_2000.SGO";
    check(!VehicleHasThermal(vehicle,0),"M163 sharing the 603 class retains historical optical-only equipment");
    sensorWeaponFile=L"EDF6VC_BMPT_L.SGO";
    check(VehicleHasThermal(vehicle,0),"BMPT sharing the 603 class keeps its modern thermal equipment");
    sensorClass="505_Tank";sensorWeaponFile=L"V603_FLAK_GUNH01_DLC_L.SGO";
    sensorRay=false;sensorRays=0;
    check(!PlayerVehicleContacts(&result) && result.count==0 && sensorRays==0,"unavailable native rays fail closed and clear previous contacts");
    sensorRay=true;sensorMap=true;
    check(!PlayerVehicleContacts(&result),"map owns its own enemy display and does not inherit vehicle contact marks");
    sensorMap=false;sensorOnFoot=true;
    check(!PlayerVehicleContacts(&result),"getting out cannot retain the vehicle optic");
    sensorOnFoot=false;sensorClass="503_Bike";
    check(!PlayerVehicleContacts(&result),"bike has no invented optical sensor");
    sensorClass="Car";Put<std::uint64_t>(seats,kSeatWeaponCount,0);
    check(!PlayerVehicleContacts(&result),"unarmed car sharing the IFV class has no invented fire-control sensor");
    Put<std::uint64_t>(seats,kSeatWeaponCount,1);
    sensorClass="505_Tank";sensorRegistry=false;
    check(!PlayerVehicleContacts(&result) && result.count==0,"unavailable hostility registry clears recognition");
    sensorRegistry=true;Put<void*>(seats,kSeatRider,other);
    check(!PlayerVehicleContacts(&result),"another rider's seat is never a local sensor context");
    Put<void*>(seats,kSeatRider,nullptr);Put<void*>(seats+kSeatStride,kSeatRider,human);Put<void*>(seats+kSeatStride,kSeatRiderCtrl,humanControl);
    sensorGunship=true;
    check(PlayerVehicleContacts(&result) && result.gunship,"the real gunship gunner seat supplies its optical target view");
    check(VehicleHasThermal(vehicle,kGunnerSeat) && !VehicleHasThermal(vehicle,0),"gunship thermal sensor belongs to the gunner seat");
    sensorTargetCount=64;sensorRays=0;
    for(int i=0;i<64;++i)sensorTargets[i]={{-float(i+1),0,100},i};
    check(PlayerVehicleContacts(&result) && result.count==0 && sensorRays==vehiclecontacts::kCandidates,
          "large blocked hostile registry cannot exceed the per-frame native ray budget");
    sensorCamera=false;
    check(!PlayerVehicleContacts(&result) && result.count==0,"stale camera does not publish optical marks");
    check(!PlayerVehicleContacts(nullptr),"null output is rejected");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("vehicle sensor runtime: %d checks, %d failures\n",checks,failed);return failed ? 1 : 0;
}
