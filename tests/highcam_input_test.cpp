// Production high-camera input on native-layout memory fixtures, with no game or real input interaction.
#include <Windows.h>
namespace wallclock { ULONGLONG now=1000; ULONGLONG Tick() noexcept{return now;} }
#define GetTickCount64 wallclock::Tick
#include "../src/highcam.cpp"
#undef GetTickCount64
#include <cstdio>
namespace crew {
unsigned char* image=nullptr;
Config config{};
unsigned char vehicle[0x800]{},seat[edf::kSeatStride]{},human[0x500]{},other[0x500]{},control[16]{},vehicleControl[16]{};
unsigned char weapon[0x1000]{},holder[0x100]{};
unsigned char* holders[]={holder};
bool mapHeld=false;
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
bool MapHoldsKeys() noexcept { return mapHeld; }
bool TurretCamServes(const void*) noexcept { return true; }
bool TurretCamLarge(const void*) noexcept { return false; }
unsigned char* PlayerHuman() noexcept { return human; }
int checks=0,failed=0;
void Check(bool value,const char* description) { ++checks;if(!value){++failed;std::printf("FAIL %s\n",description);} }
void Press(unsigned short buttons) { Put<unsigned short>(seat,kSeatButtons,buttons);HighCamFrame(vehicle); }
}
int main() {
    using namespace crew;
    Put<void*>(vehicle,kSelfCtrl,vehicleControl);Put<void*>(vehicle,kSeats,seat);Put<std::uint64_t>(vehicle,kSeatCount,1);
    Put<void*>(seat,kSeatRider,human);Put<void*>(seat,kSeatRiderCtrl,control);Put<int>(control,8,1);
    Put<void*>(human,kHumanPad,human);human[kHumanPlayer]=1;seat[kSeatPad]=1;
    Put<void*>(seat,kSeatWeapons,holders);Put<std::uint64_t>(seat,kSeatWeaponCount,1);Put<void*>(holder,kHolderWeapon,weapon);
    Put<int>(weapon,edf::kWeaponMark,edf::kMarkLofted);Put<int>(weapon,edf::kWeaponAmmoAlive,1500);
    Press(0);Check(HighCamOffered(vehicle),"actual indirect weapon offers the high view");
    mapHeld=true;Press(0x80);Check(!HighCamOn(vehicle),"map owns R3: no high-view toggle");
    mapHeld=false;Press(0x80);Check(!HighCamOn(vehicle),"held map press is not replayed on close");
    Press(0);Press(0x80);Check(HighCamOn(vehicle),"fresh press after close enables high view");
    wallclock::now+=250;
    Check(HighCamOn(vehicle),"slow input frame must retain latched high mode before next cue refresh");
    bool visible=false,keys=false;Check(!PlayerHighCam(&visible,&keys),"stale HUD cue still expires independently of mode");
    Press(0);Put<void*>(seat,kSeatRider,nullptr);HighCamFrame(vehicle);
    Check(!HighCamOn(vehicle),"leaving current vehicle revokes high mode immediately");
    Put<void*>(seat,kSeatRider,human);Press(0x80);
    Check(HighCamOn(vehicle),"held button on same-vehicle reboarding does not toggle remembered choice");
    config.highCam=false;Check(!HighCamOn(vehicle),"disabled high view cannot leave an active publication");
    HighCamFrame(vehicle);config.highCam=true;
    Put<void*>(other,kHumanPad,other);other[kHumanPlayer]=1;Put<void*>(seat,kSeatRider,other);Press(0);Press(0x80);
    Check(!HighCamOn(vehicle),"another local player's seat cannot publish this player's high view");
    Put<void*>(seat,kSeatRider,human);Put<std::uint64_t>(seat,kSeatWeaponCount,0);Press(0);Press(0x80);
    Check(!HighCamOn(vehicle),"weapon removed: no indirect high view");
    std::printf("highcam_input: %d checks, %d failed\n",checks,failed);return failed ? 1 : 0;
}
