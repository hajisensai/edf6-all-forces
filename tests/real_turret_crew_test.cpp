// Execute the production tank-gunner admission/operator/trigger paths with real seat memory.
#include "../autoturret/src/gunner.cpp"
#include <cstdio>
namespace autoturret {
unsigned char* image=nullptr;Config cfg{};
int tracked=0,scans=0,userCalls=0;bool clearedBeforeStock=false;
void Log(const char*,...) noexcept{}
void SeeVehicle(const void*) noexcept{}
ULONGLONG Frame() noexcept{return 1;}
void ScanEnemies(const unsigned char*,float,Nearby& out) noexcept{++scans;out.count=0;}
Track* TrackFor(const unsigned char*,unsigned,bool) noexcept{++tracked;return nullptr;}
float Down(const unsigned char*) noexcept{return 9.8f;}
bool Ballistic(const float*,const Shot&,float&,float&) noexcept{return false;}
float AxisInput(Track&,int,float,float,float,bool,float,float) noexcept{return 0;}
void ReloadConfigIfChanged() noexcept{}
void PilotFrame(const unsigned char*,unsigned,const unsigned char*,const float*,const float*,float) noexcept{}
const void* Designated(const unsigned char*,float*) noexcept{return nullptr;}
bool LeadCircle() noexcept{return false;}
bool CameraTurret(const unsigned char*,unsigned) noexcept{return false;}
edf::aimlink::PlayerGun PlayerControlRule(const unsigned char*,unsigned,bool lead,bool locked) noexcept{return edf::aimlink::PlayerGunRule(false,lead,locked);}
bool Stabilized(const unsigned char*,unsigned,const float*,float*,float*) noexcept{return false;}
float PriorityWeight(const Enemy&) noexcept{return 1;}
void PublishAim(const unsigned char*,bool,const void*,const float*,const float*,const float*,const Shot*,const float*,float) noexcept{}
const unsigned char* SeatGun(const unsigned char* s) noexcept {
    const auto list=At<const unsigned char* const*>(s,kSeatWeapons);
    return list && At<std::uint64_t>(s,kSeatWeaponCount) ? At<const unsigned char*>(list[0],kHolderWeapon) : nullptr;
}
const void* __fastcall UserRec(void*,const void*) {++userCalls;return reinterpret_cast<void*>(42);}
void __fastcall StockRec(void* v,std::uintptr_t,void*,void*) {
    const auto seat=edf::SeatAt(static_cast<unsigned char*>(v),1);
    clearedBeforeStock=SeatGun(seat)[kWeaponFire]==0;
}
}
int main() {
    using namespace autoturret;
    int checks=0,fail=0;auto check=[&](bool ok,const char* msg){++checks;if(!ok){++fail;std::printf("FAIL %s\n",msg);}};
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    // Actual native holder pull bytes, profile checked in the installer.
    const unsigned char pull[]={0x48,0x8B,0x41,0x08,0x48,0x85,0xC0,0x74,0x11,0x83,0x78,0x08,0x00,0x74,0x0B,0x48,0x8B,0x41,0x10,0xC6,0x80,0x39,0x01,0x00,0x00,0x01,0xC3};
    std::memcpy(image+kPullTrigger,pull,sizeof(pull));
    unsigned char vehicle[0x3000]{},seats[edf::kSeatStride*2]{},rider[0x400]{},ctrl[16]{},vehicleCtrl[16]{},weapon[0x1800]{},holder[kTriggerStride]{};
    unsigned char* holders[]={holder};
    Put<void*>(vehicle,kSelfCtrl,vehicleCtrl);Put<void*>(vehicle,edf::kSeats,seats);Put<std::uint64_t>(vehicle,edf::kSeatCount,2);
    Put<void*>(vehicle,kTriggers,holder);Put<std::uint64_t>(vehicle,kTriggerCount,1);
    auto seat=edf::SeatAt(vehicle,1);
    Put<void*>(seat,kSeatWeapons,holders);Put<std::uint64_t>(seat,kSeatWeaponCount,1);
    Put<void*>(holder,kTriggerCtrl,ctrl);Put<void*>(holder,kTriggerWeapon,weapon);Put<int>(ctrl,8,1);
    Put<void*>(rider,0,image+0x17CDF28);Put<float>(rider,0x2F8,100);
    nextUser[0]=&UserRec;next403=&StockRec;
    cfg.enabled=cfg.gunnerAi=cfg.gunnerAssist=true;
    check(SeatCrew(seat)==Crew::none && !Wanted(Crew::none),"empty seat never admitted to AI");
    check(!WeaponUser<0>(vehicle+kUserIface,weapon) && userCalls==0,"empty gun cannot proxy a driver or native fallback operator");
    check(!PullOwned(vehicle,1,holder) && !weapon[kWeaponFire],"empty seat cannot raise a trigger");
    Put<void*>(seat,edf::kSeatRider,rider);Put<void*>(seat,edf::kSeatRiderCtrl,ctrl);
    Put<void*>(rider,0,image+edf::kDummyRiderVtable);
    check(SeatCrew(seat)==Crew::none && !PullOwned(vehicle,1,holder),"dummy occupant is not a real gunner");
    check(!WeaponUser<0>(vehicle+kUserIface,weapon),"dummy cannot operate side weapon through native callback");
    Put<void*>(rider,0,image+0x17CDF28);
    check(SeatCrew(seat)==Crew::ai && Wanted(Crew::ai),"live real NPC gunner admitted");
    check(WeaponUser<0>(vehicle+kUserIface,weapon)==reinterpret_cast<void*>(42) && userCalls==1,"real NPC preserves native operator answer");
    check(PullOwned(vehicle,1,holder) && weapon[kWeaponFire]==1,"real local NPC can execute native holder pull");
    rider[kDead]=1;ReleaseGunnerPulls(vehicle);
    check(!weapon[kWeaponFire] && SeatCrew(seat)==Crew::none && !WeaponUser<0>(vehicle+kUserIface,weapon),"dead occupant releases trigger and loses operator");
    rider[kDead]=0;Put<float>(rider,0x2F8,0);
    check(SeatCrew(seat)==Crew::none,"zero HP does not operate while death flag catches up");
    Put<float>(rider,0x2F8,100);rider[0x128]=1;
    check(SeatCrew(seat)==Crew::remote && !Wanted(Crew::remote) && !PullOwned(vehicle,1,holder),"remote NPC copy cannot aim or fire locally");
    rider[0x128]=0;PullOwned(vehicle,1,holder);rider[0x128]=1;ReleaseGunnerPulls(vehicle);
    check(!weapon[kWeaponFire],"authority transfer releases locally raised latch");
    rider[0x128]=0;rider[edf::kHumanPlayer]=1;Put<void*>(rider,edf::kHumanPad,rider);
    check(SeatCrew(seat)==Crew::player && Wanted(Crew::player) && !PullOwned(vehicle,1,holder),"human gets optional aim but retains trigger ownership");
    cfg.gunnerAssist=false;check(!Wanted(Crew::player),"player may disable aim assistance");cfg.gunnerAssist=true;
    rider[edf::kHumanPlayer]=0;Put<void*>(rider,edf::kHumanPad,nullptr);
    PullOwned(vehicle,1,holder);cfg.enabled=false;Hook403(vehicle,0,nullptr,nullptr);
    check(clearedBeforeStock && !weapon[kWeaponFire],"global disable releases before original input runs");cfg.enabled=true;
    PullOwned(vehicle,1,holder);Put<void*>(seat,edf::kSeatRiderCtrl,nullptr);ReleaseGunnerPulls(vehicle);
    check(!weapon[kWeaponFire] && !PullOwned(vehicle,1,holder),"dismount releases pending shot");
    Put<void*>(seat,edf::kSeatRiderCtrl,ctrl);PullOwned(vehicle,1,holder);cfg.gunnerAi=false;Hook403(vehicle,0,nullptr,nullptr);
    check(!weapon[kWeaponFire],"gunner AI toggle releases pending shot");cfg.gunnerAi=true;
    weapon[kWeaponFire]=1;PullOwned(vehicle,1,holder);ReleaseGunnerPulls(vehicle);
    check(weapon[kWeaponFire]==1,"pre-existing foreign trigger is not claimed or cleared");weapon[kWeaponFire]=0;
    PullOwned(vehicle,1,holder);Put<void*>(vehicle,kSelfCtrl,ctrl);ReleaseGunnerPulls(vehicle);
    check(weapon[kWeaponFire]==1,"reused vehicle identity cannot clear an unrelated weapon");weapon[kWeaponFire]=0;Put<void*>(vehicle,kSelfCtrl,vehicleCtrl);
    Put<void*>(rider,0,image+0x1000);check(SeatCrew(seat)==Crew::none,"arbitrary non-human object is not a crew member");
    Put<void*>(rider,0,image+0x17CDF28);Put<int>(ctrl,8,0);check(SeatCrew(seat)==Crew::none,"expired occupant control rejected");Put<int>(ctrl,8,1);
    // Empty driver plus occupied side seat is valid; driver ownership cannot take over this seat.
    Gunners(vehicle);check(tracked==1 && scans==1,"real side gunner admitted independently of empty driver");
    tracked=scans=0;Put<void*>(seat,edf::kSeatRiderCtrl,nullptr);Gunners(vehicle);
    check(!tracked && !scans,"empty side seat is not scanned or steered");
    Put<void*>(seat,edf::kSeatRiderCtrl,ctrl);PullOwned(vehicle,1,holder);
    Put<std::uint64_t>(seat,kSeatWeaponCount,0);ReleaseGunnerPulls(vehicle);
    check(!weapon[kWeaponFire],"removed mount releases its still-live pending trigger");
    Put<std::uint64_t>(seat,kSeatWeaponCount,1);PullOwned(vehicle,1,holder);ReleaseGunnerPulls(nullptr);
    check(!weapon[kWeaponFire],"global release clears outstanding plugin latches");
    VirtualFree(image,0,MEM_RELEASE);std::printf("real turret crew: %d checks, %d failures\n",checks,fail);return fail?1:0;
}
