// Physical sight anchors authored from vehicle model geometry. A weapon does not gain
// an optic just because it has ammunition or shares another vehicle's class.
#pragma once
#include "memory.h"
#include "weapon_mount.h"
#include "body506.h"
#include "sightzoom.h"
#include <cwchar>

namespace crew::optic {
constexpr unsigned kMaxAnchors=16;
// An anchor's name says what the gunner looks through there (sightzoom.h Eyepiece; pylib/vehicle_optics.py
// EYEPIECES, the installer's names): <prefix><nn>, nn 00..kMaxAnchors-1, the numbers shared by the families.
struct Family { const wchar_t* prefix; sightzoom::Eyepiece eyepiece; };
constexpr Family kFamilies[]={{L"vc_optic_",sightzoom::Eyepiece::binocular},{L"vc_monoc_",sightzoom::Eyepiece::monocular},
                              {L"vc_camera_",sightzoom::Eyepiece::camera}};
struct Pose { float eye[3]{},direction[3]{}; sightzoom::Eyepiece eyepiece=sightzoom::Eyepiece::binocular; };

inline bool Mounted(const unsigned char* vehicle,const unsigned char* seat,const unsigned char* weapon,Pose* out) noexcept {
    if(!vehicle || !seat || !weapon || !out)return false;
    const auto holders=At<const unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!count || count>16 || !Readable(holders,count*sizeof(void*)))return false;
    const unsigned char* holder=nullptr;
    for(std::uint64_t i=0;i<count;++i)
        if(Readable(holders[i],weaponmount::kHolderBone+8) && At<const void*>(holders[i],kHolderWeapon)==weapon){holder=holders[i];break;}
    if(!holder)return false;
    const auto inst=vehicle+weaponmount::kVehicleModel;
    const int attached=weaponmount::BoneIndex(inst,At<const void*>(holder,weaponmount::kHolderBone));
    int chain[256];
    if(attached<0)return false;
    const int depth=weaponmount::Chain(inst,attached,chain);
    if(!depth)return false;
    const unsigned char* best=nullptr;int nearest=depth;bool ambiguous=false;
    sightzoom::Eyepiece eyepiece=sightzoom::Eyepiece::binocular;
    for(const Family& family:kFamilies)for(unsigned i=0;i<kMaxAnchors;++i) {
        wchar_t name[24];std::swprintf(name,24,L"%ls%02u",family.prefix,i);
        const auto anchor=BoneRecord506(inst,name);
        const int index=weaponmount::BoneIndex(inst,anchor);
        if(index<0)continue;
        const int parent=At<int>(anchor,weaponmount::kParent);
        // The scene root is shared by unrelated guns and passenger seats.
        if(parent<=0)continue;
        for(int j=0;j<depth;++j)if(chain[j]==parent) {
            if(j<nearest){best=anchor;nearest=j;ambiguous=false;eyepiece=family.eyepiece;}
            else if(j==nearest)ambiguous=true;
            break;
        }
    }
    if(!best || ambiguous)return false;
    float muzzle[3],dir[3];
    if(!edf::MeanMuzzle(weapon,64,muzzle,dir))return false;
    float norm=0;
    for(int i=0;i<3;++i) {
        out->eye[i]=At<float>(best,kBoneWorld506+0x30+i*4);
        if(!std::isfinite(out->eye[i]) || !std::isfinite(dir[i]))return false;
        norm+=dir[i]*dir[i];
    }
    if(!(norm>1e-8f) || !std::isfinite(norm))return false;
    norm=std::sqrt(norm);
    for(int i=0;i<3;++i)out->direction[i]=dir[i]/norm;
    out->eyepiece=eyepiece;
    return true;
}
} // namespace crew::optic
