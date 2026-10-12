// First person in a vehicle: where this machine's player's eye is in the seat they hold (fp_vehicle.h; firstperson.cpp
// moves the camera there and hides the rider). The table rows come from the vehicles' own models (Root.cpk, read
// offline: tools/stations/); a vehicle or seat without one keeps the rider's head, the seat's own place.
#include "crew.h"
#include "firstperson.h"
#include "fp_vehicle.h"
#include "vhud.h"
#include "memory.h"
#include "edf/seat.h"
#include <cmath>
#include <cwchar>

namespace crew {
namespace {
using fpview::M4;
constexpr std::size_t kHumanVehicle=0x1548;   // the vehicle a human rides (weak_ptr object; +0x1550 its control block)
constexpr std::size_t kHumanBodyInst=0x900;   // the human's skeleton instance
constexpr std::size_t kVehicleModel=0xEE0;    // the vehicle's skeleton instance (weapon_mount.h)
constexpr std::size_t kInstBones=0x10,kInstCount=0x20,kBoneStride=0x110,kBoneWorld=0xB0;
constexpr int kMostBones=512;

const fpveh::Station kStations[]={
    {nullptr,0,nullptr,{0.0f,0.0f,0.0f}},   // filled from tools/stations (the rows below it)
};
constexpr int kStationCount=static_cast<int>(sizeof(kStations)/sizeof(kStations[0]));

// The bone named `name` in the skeleton instance `inst` (its record), or null.
const unsigned char* Bone(const unsigned char* inst,const wchar_t* name) noexcept {
    if(!name || !Readable(inst,kInstCount+4))return nullptr;
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

M4 WorldOf(const unsigned char* rec) noexcept { M4 m;std::memcpy(m.m,rec+kBoneWorld,sizeof(m.m));return m; }

// The seat `human` holds in `v`, -1: none.
int SeatOf(unsigned char* v,const unsigned char* human) noexcept {
    const unsigned n=edf::SeatCount(v);
    for(unsigned i=0;i<n;++i)if(At<const void*>(edf::SeatAt(v,i),edf::kSeatRider)==human)return static_cast<int>(i);
    return -1;
}

// The name the table knows `v` by: a plugin body's, else its class's.
const char* NameOf(const void* v) noexcept {
    switch(BodyOf(v)) {
        case PluginBody::jet: return "jet";
        case PluginBody::sub: return "sub";
        case PluginBody::playerJet: return "playerJet";
        case PluginBody::sazabi: return "sazabi";
        default: return VehicleClassName(v);
    }
}
}  // namespace

bool VehicleEye(unsigned char* human,float* eye,RideView* out) noexcept {
    *out=RideView{};
    if(HumanOnFoot(human))return false;
    auto v=At<unsigned char*>(human,kHumanVehicle);
    if(!v || !Readable(v,edf::kSeatCount+8))return false;
    out->vehicle=v;
    out->seat=SeatOf(v,human);
    out->name=NameOf(v);
    if(out->seat<0)return false;
    const fpveh::Station* s=fpveh::Find(kStations,kStationCount,out->name,out->seat);
    const unsigned char* rec=s ? Bone(v+kVehicleModel,s->bone) : nullptr;
    if(rec) {
        fpveh::EyeOn(WorldOf(rec),s->at,eye);
        out->station=true;
        return fpview::Finite(WorldOf(rec));
    }
    const unsigned char* head=Bone(human+kHumanBodyInst,L"head");   // no row (or its bone missing): the rider's head
    if(!head)return false;
    std::memcpy(eye,head+kBoneWorld+48,12);
    return std::isfinite(eye[0]+eye[1]+eye[2]);
}
}  // namespace crew
