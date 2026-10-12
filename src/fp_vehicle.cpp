// First person in a vehicle: where this machine's player's eye is in the seat they hold (firstperson.cpp moves the
// camera there and hides the rider). The eye is a marker bone of the vehicle's own model, `vc_eye_<seat>` (no
// vertices, a child of the bone the seat's cockpit rides: a gunner's turret, a heli's body), put there with the
// cockpit by the installer (pylib/cockpits.py; the stock models through vehicle_optics.py, the plugin's own in their
// generators). One place holds where each seat's eye and cockpit are: the model. A seat without a marker (no cockpit
// installed, or another mod's model) keeps the stock camera: no first person sitting in the air.
#include "crew.h"
#include "firstperson.h"
#include "vhud.h"
#include "memory.h"
#include "edf/seat.h"
#include <cmath>
#include <cstdio>
#include <cwchar>

namespace crew {
namespace {
constexpr std::size_t kHumanVehicle=0x1548;   // the vehicle a human rides (weak_ptr object; +0x1550 its control block)
constexpr std::size_t kVehicleModel=0xEE0;    // the vehicle's skeleton instance (weapon_mount.h)
constexpr std::size_t kInstBones=0x10,kInstCount=0x20,kBoneStride=0x110,kBoneWorld=0xB0;
constexpr int kMostBones=512;

// The bone named `name` in the skeleton instance `inst` (its record), or null.
const unsigned char* Bone(const unsigned char* inst,const wchar_t* name) noexcept {
    if(!Readable(inst,kInstCount+4))return nullptr;
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    const auto n=At<std::int32_t>(inst,kInstCount);
    if(!bones || n<=0 || n>kMostBones || !Readable(bones,static_cast<std::size_t>(n)*kBoneStride))return nullptr;
    for(std::int32_t i=0;i<n;++i) {
        const unsigned char* rec=bones+static_cast<std::size_t>(i)*kBoneStride;
        const auto nm=At<const wchar_t*>(rec,0);
        if(nm && std::wcscmp(nm,name)==0)return rec;
    }
    return nullptr;
}

// The seat `human` holds in `v`, -1: none.
int SeatOf(unsigned char* v,const unsigned char* human) noexcept {
    const unsigned n=edf::SeatCount(v);
    for(unsigned i=0;i<n;++i)if(At<const void*>(edf::SeatAt(v,i),edf::kSeatRider)==human)return static_cast<int>(i);
    return -1;
}
}  // namespace

int VehicleScreens(const unsigned char* v,int seat,const float* eye,CockpitScreens* out) noexcept {
    out->n=0;
    const unsigned char* inst=v+kVehicleModel;
    if(!Readable(inst,kInstCount+4))return 0;
    const auto bones=At<const unsigned char*>(inst,kInstBones);
    const auto n=At<std::int32_t>(inst,kInstCount);
    if(!bones || n<=0 || n>kMostBones || !Readable(bones,static_cast<std::size_t>(n)*kBoneStride))return 0;
    wchar_t prefix[24];
    const int len=std::swprintf(prefix,24,L"vc_scr_%d_",seat);
    static const wchar_t* const kinds[]={L"flight",L"weapons",L"status",L"hud",L"map"};
    for(std::int32_t i=0;i<n && out->n<kMostScreens;++i) {
        const unsigned char* rec=bones+static_cast<std::size_t>(i)*kBoneStride;
        const auto name=At<const wchar_t*>(rec,0);
        if(!name || std::wcsncmp(name,prefix,static_cast<std::size_t>(len))!=0)continue;
        const wchar_t* rest=name+len;
        int kind=-1;
        for(int k=0;k<5;++k) {
            const std::size_t kl=std::wcslen(kinds[k]);
            if(std::wcsncmp(rest,kinds[k],kl)==0 && rest[kl]==L'_'){kind=k;rest+=kl+1;break;}
        }
        wchar_t* end=nullptr;
        const long wmm=kind<0 ? 0 : std::wcstol(rest,&end,10);
        const long hmm=end && *end==L'x' ? std::wcstol(end+1,nullptr,10) : 0;
        if(wmm<=0 || hmm<=0)continue;
        const float* m=reinterpret_cast<const float*>(rec+kBoneWorld);
        CockpitScreen& c=out->s[out->n];
        c.kind=static_cast<ScreenKind>(kind);
        for(int k=0;k<3;++k){c.right[k]=m[k];c.up[k]=m[4+k];c.off[k]=m[12+k]-eye[k];}
        c.w=wmm*0.001f;c.h=hmm*0.001f;
        if(std::isfinite(c.off[0]+c.off[1]+c.off[2]+c.right[0]+c.up[1]))++out->n;
    }
    return out->n;
}

bool VehicleEye(unsigned char* human,float* eye,RideView* out) noexcept {
    *out=RideView{};
    if(HumanOnFoot(human))return false;
    auto v=At<unsigned char*>(human,kHumanVehicle);
    if(!v || !Readable(v,edf::kSeatCount+8))return false;
    out->vehicle=v;
    out->seat=SeatOf(v,human);
    out->name=VehicleClassName(v);
    if(out->seat<0)return false;
    wchar_t marker[16];
    std::swprintf(marker,16,L"vc_eye_%d",out->seat);
    const unsigned char* rec=Bone(v+kVehicleModel,marker);
    if(!rec)return false;
    std::memcpy(eye,rec+kBoneWorld+48,12);
    out->station=true;
    return std::isfinite(eye[0]+eye[1]+eye[2]);
}
}  // namespace crew
