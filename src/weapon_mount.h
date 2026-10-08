// Native weapon-to-seat articulation: actual holder/model bone ancestry, never a vehicle-name guess.
#pragma once
#include "crew.h"
#include "edf/weapon.h"
#include <cmath>

namespace crew::weaponmount {
constexpr std::size_t kVehicleModel=0xEE0,kWeaponModel=0xFD0,kRecords=0x10,kCount=0x20,kStride=0x110;
constexpr std::size_t kOwnIndex=0xC,kParent=0x10,kInherits=8,kHolderBone=0x18;
struct Freedom { bool known=false,yaw=false,pitch=false; };

inline int BoneIndex(const unsigned char* instance,const void* bone) noexcept {
    if(!Readable(instance,kCount+4))return -1;
    const auto records=At<const unsigned char*>(instance,kRecords);
    const int count=At<int>(instance,kCount);
    if(count<=0 || count>256 || !Readable(records,static_cast<std::size_t>(count)*kStride))return -1;
    const auto ptr=reinterpret_cast<std::uintptr_t>(bone),base=reinterpret_cast<std::uintptr_t>(records);
    if(ptr<base || (ptr-base)%kStride || (ptr-base)/kStride>=static_cast<std::size_t>(count))return -1;
    const int i=static_cast<int>((ptr-base)/kStride);
    return At<int>(records+i*kStride,kOwnIndex)==i ? i : -1;
}

inline int Chain(const unsigned char* instance,int index,int* chain,bool inherited=false) noexcept {
    const auto records=At<const unsigned char*>(instance,kRecords);
    const int count=At<int>(instance,kCount);int n=0;
    while(index>=0 && index<count && n<count) {
        for(int i=0;i<n;++i)if(chain[i]==index)return 0;
        const auto record=records+index*kStride;
        if(At<int>(record,kOwnIndex)!=index || (inherited && index!=0 && !record[kInherits]))return 0;
        chain[n++]=index;const int parent=At<int>(record,kParent);
        if(parent==-1)return inherited && index!=0 ? 0 : n;
        index=parent;
    }
    return 0;
}

inline bool AxisControls(const unsigned char* axis,const int* chain,int length) noexcept {
    if(!Readable(axis,0x40))return false;
    const float low=At<float>(axis,0),high=At<float>(axis,4);
    if(!std::isfinite(low+high) || high-low<0.001f)return false;
    const auto entries=At<const unsigned char*>(axis,0x28);const auto n=At<std::uint64_t>(axis,0x38);
    if(!n || n>256 || !Readable(entries,n*0x38))return false;
    for(std::uint64_t i=0;i<n;++i) {
        const auto entry=entries+i*0x38;const float a=At<float>(entry,4),b=At<float>(entry,8);
        if(!std::isfinite(a+b) || std::fabs(b-a)<0.001f)continue;
        for(int k=0;k<length;++k)if(At<int>(entry,0)==chain[k])return true;
    }
    return false;
}

inline Freedom Of(const unsigned char* vehicle,const unsigned char* seat,const unsigned char* holder) noexcept {
    __try {
        if(!vehicle || !seat || !Readable(holder,kHolderBone+8) || !Readable(vehicle+kVehicleModel,kCount+4))return {};
        const auto attached=At<const unsigned char*>(holder,kHolderBone);
        const int bone=BoneIndex(vehicle+kVehicleModel,attached);int ancestors[256];
        if(bone<0)return {};
        const int depth=Chain(vehicle+kVehicleModel,bone,ancestors);if(!depth)return {};
        const auto weapon=At<const unsigned char*>(holder,kHolderWeapon);
        if(!Readable(weapon,edf::kMuzzleCount+8))return {};
        const auto muzzles=At<const unsigned char*>(weapon,edf::kMuzzles);const auto n=At<std::uint64_t>(weapon,edf::kMuzzleCount);
        if(!n || n>64 || !Readable(muzzles,n*edf::kMuzzleStride))return {};
        for(std::uint64_t i=0;i<n;++i) {
            const auto muzzle=muzzles+i*edf::kMuzzleStride;
            if(At<int>(muzzle,edf::kMuzzleMode)==edf::kModeWeaponRows)continue;
            if(At<int>(muzzle,edf::kMuzzleMode)!=1)return {};
            // Mode 1's bone belongs to the weapon model, not the vehicle model. Verify the native SetWorld bridge.
            if(!Readable(weapon+kWeaponModel,kCount+4) || At<const void*>(weapon,0xE88)!=attached || !At<const void*>(weapon,0xF40))return {};
            const int own=BoneIndex(weapon+kWeaponModel,At<const void*>(muzzle,0));int path[256];
            if(own<0 || !Chain(weapon+kWeaponModel,own,path,true))return {};
        }
        const auto axes=seat+kSeatAim+kAimAxes;
        return {true,AxisControls(axes,ancestors,depth),AxisControls(axes+kAxisStride,ancestors,depth)};
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
inline Freedom OfWeapon(const unsigned char* v,const unsigned char* seat,const unsigned char* weapon) noexcept {
    __try {
        if(!v || !seat || !weapon || !Readable(seat,kSeatWeaponCount+8))return {};
        const auto holders=At<const unsigned char* const*>(seat,kSeatWeapons);const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
        if(!n || n>16 || !Readable(holders,n*8))return {};
        for(std::uint64_t i=0;i<n;++i)if(Readable(holders[i],kHolderBone+8) && At<const void*>(holders[i],kHolderWeapon)==weapon)
            return Of(v,seat,holders[i]);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return {};
}
} // namespace crew::weaponmount
