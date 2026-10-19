// Game-thread optical contact collector. The HUD publishes this value with its snapshot;
// native object reads, team relationships and terrain/building rays never run on the draw thread.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "sight_contacts.h"
#include "vecmath.h"
#include "stores.h"
#include <cwchar>
#include <iterator>
#include <cstring>

namespace crew {
namespace {
int SensorPlayerSeatOf(unsigned char* vehicle) noexcept {
    const unsigned count=SeatCount(vehicle);
    for(unsigned i=0;i<count;++i) {
        const auto seat=SeatAt(vehicle,i);
        if(SeatRider(seat)==Rider::player && At<const void*>(seat,kSeatRider)==PlayerHuman())return static_cast<int>(i);
    }
    return -1;
}
bool SensorSeatArmed(const unsigned char* seat) noexcept {
    const auto holders=At<const unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>16 || !Readable(holders,count*8))return false;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto control=At<const unsigned char*>(holders[i],kHolderCtrl);
        const auto weapon=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(control,12) || At<int>(control,8)<=0 || !Readable(weapon,kWeaponAlive+4))continue;
        const float speed=At<float>(weapon,kWeaponSpeed);
        const int life=At<int>(weapon,kWeaponAlive);
        if(std::isfinite(speed) && speed>0.0f && life>0 && speed*static_cast<float>(life)>=10.0f)return true;
    }
    return false;
}
bool LegacyVulcanOptic(const unsigned char* seat) noexcept {
    const auto holders=At<const unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>16 || !Readable(holders,count*8))return false;
    constexpr wchar_t prefix[]=L"EDF6VC_VULCAN_";
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        std::size_t length=0;
        const auto name=WeaponFile(At<const unsigned char*>(holders[i],kHolderWeapon),&length);
        if(name && length>=std::size(prefix)-1 && _wcsnicmp(name,prefix,std::size(prefix)-1)==0)return true;
    }
    return false;
}
void ContactVisit(void* ctx,const void* object,const float* aim) noexcept {
    static_cast<vehiclecontacts::Scan*>(ctx)->Add(object,aim);
}

bool VehicleContacts(VehicleSensorReadout& result) noexcept {
    if(!Cfg().enabled || MapHoldsKeys() || !MapRayReady())return false;
    const auto human=PlayerHuman();
    if(!Readable(human,kHumanVehicleCtrl+8) || human[kDead] || HumanOnFoot(human))return false;
    const auto ctrl=At<const unsigned char*>(human,kHumanVehicleCtrl);
    const auto v=At<unsigned char*>(human,kHumanVehicleCtrl-8);
    if(!Readable(ctrl,12) || At<int>(ctrl,8)<=0 || !Readable(v,kSeatCount+8) ||
       At<const void*>(v,kSelfCtrl)!=ctrl || v[kDead] || !KnownVehicle(v))return false;
    const int seat=SensorPlayerSeatOf(v);
    if(seat<0)return false;
    const bool gunship=GunshipCrewSeats(v) && static_cast<unsigned>(seat)==kGunnerSeat;
    if(!gunship && (!Cfg().stockVehicleHud || BodyOf(v)!=PluginBody::none ||
                   !vehiclecontacts::OpticalClass(VehicleClassName(v)) ||
                   !SensorSeatArmed(SeatAt(v,static_cast<unsigned>(seat)))))return false;
    vehiclecontacts::Scan scan{};
    if(!CameraRay(scan.eye,scan.look) || !vec::Normalize(scan.look))return false;
    if(!VisitEnemies(v,&ContactVisit,&scan))return false;
    result.kind=VehicleSensorKind::optical;result.gunship=gunship;result.range=vehiclecontacts::kRange;
    // One map ray per bounded candidate; all live object reads stay on this game-thread path.
    for(int i=0;i<scan.count && result.count<kVehicleContacts;++i) {
        const auto& point=scan.point[i];
        float hit[3];
        if(!vehiclecontacts::Visible(point.range,MapRay(scan.eye,point.at,hit)))continue;
        auto& contact=result.contact[result.count++];
        std::memcpy(contact.at,point.at,12);contact.range=point.range;
    }
    return true;
}
} // namespace

bool VehicleHasThermal(const unsigned char* v,unsigned index) noexcept {
    __try {
        if(!v || !Readable(v,kSeatCount+8) || v[kDead] || !KnownVehicle(v) || index>=SeatCount(v))return false;
        const auto seat=SeatAt(const_cast<unsigned char*>(v),index);
        if(GunshipCrewSeats(v))return index==kGunnerSeat;
        return BodyOf(v)==PluginBody::none && vehiclecontacts::ThermalClass(VehicleClassName(v)) &&
               SensorSeatArmed(seat) && !LegacyVulcanOptic(seat);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool PlayerVehicleContacts(VehicleSensorReadout* out) noexcept {
    if(!out)return false;
    *out=VehicleSensorReadout{};
    __try { return VehicleContacts(*out); }
    __except(EXCEPTION_EXECUTE_HANDLER) { *out=VehicleSensorReadout{};return false; }
}
} // namespace crew
