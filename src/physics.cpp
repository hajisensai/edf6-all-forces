// Stock EDF6 physics defects the plugin corrects at load (docs/re-notes.md, "vanilla collision").
// EDF5 ran Havok hkp: vehicles were hkpVehicleInstance bodies and the terrain was welded. EDF6 moved to
// hknp with game-side wheels, and the vehicle chassis lost the motion welding on the way.
#include "crew.h"

namespace crew {
namespace {
// Overwrite `size` code bytes at image+rva with `value`, after the `expect` bytes at image+checkRva
// (covering the patched bytes) still read as the stock code. False and untouched otherwise.
bool PatchCode(std::size_t checkRva,const unsigned char* expect,std::size_t expectSize,
               std::size_t rva,const unsigned char* value,std::size_t size) noexcept {
    if(rva<checkRva || rva+size>checkRva+expectSize)return false;
    if(!Matches(checkRva,expect,expectSize))return false;
    unsigned char* at=image+rva;
    DWORD old=0;
    if(!VirtualProtect(at,size,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(at,value,size);
    VirtualProtect(at,size,old,&old);
    FlushInstructionCache(GetCurrentProcess(),at,size);
    return true;
}

// The chassis body build (0x656E90, called from 0x64E9B6 / 0x650AA6 for every wheeled vehicle) sets the
// hknp body quality to CHARACTER (10): `mov byte [rbp+0x86],0x0A` at 0x6571AD. In the default quality
// library (0xE13BA0) CHARACTER only asks for NEIGHBOR welding (flags 0x80), VEHICLE (9) asks for
// NEIGHBOR|MOTION (0x180); solver iterations are the same. Without motion welding a fast chassis sliding
// over the triangle seams of uneven ground meets the inner edges as ghost contacts and is thrown up.
// The fix is the quality the engine provides for exactly this body: VEHICLE.
constexpr std::size_t kChassisQualityAt=0x6571AD;
constexpr unsigned char kChassisQualityCode[]={0xC6,0x85,0x86,0x00,0x00,0x00,0x0A};
constexpr unsigned char kQualityVehicle=0x09;

bool WeldVehicleChassis() noexcept {
    const std::size_t immediate=kChassisQualityAt+sizeof(kChassisQualityCode)-1;
    return PatchCode(kChassisQualityAt,kChassisQualityCode,sizeof(kChassisQualityCode),
                     immediate,&kQualityVehicle,1);
}
}  // namespace

bool InstallPhysics() noexcept {
    if(!cfg.vehicleWelding){Log("PHYSICS vehicleWelding off");return false;}
    const bool welded=WeldVehicleChassis();
    Log("PHYSICS vehicleWelding=%d",welded);
    return welded;
}
}  // namespace crew
