// A vehicle gun's fire-control system (the user, 2026-10-25: "应该升级的火控hud应该升级，比如某些现代的可以有落点提示啊，预瞄点
// 之类的应该有的", then: "我只是举例，我的意思是给对应的载具加上符合的火控/瞄具等功能"). The sight's reticle already follows the
// weapon (reticle.h Classify: a tank gun's chevron and stadia, an autocannon's ring, the flak's lead rings); what the
// gunner is told beyond it follows the fire control the weapon really had:
//  - optical: a telescope and its stadia, nothing else (the 88 mm Flak's ZF 20, the Sturmtiger's PaK sight): the range
//    is the gunner's estimate on the stadia and the range ladder; no rangefinder figure, no computed lead mark;
//  - gyro: a lead-computing gyro sight fed by a ranging radar (the M163 VADS and its quad upgrade): the range and the
//    computed lead mark, no solution data;
//  - computer: a laser rangefinder and a ballistic computer (the BMPT, the TOS-1, EDF's own future-war vehicles): the
//    range, the lead mark, and the solution (the round's time of flight, the target's speed);
//  - radar: a tracking radar's fire control (the Tunguska, the GDF-005's Skyguard, the Phalanx, a stock anti-aircraft
//    vehicle): the computer's, a wider acquisition cone (the radar finds the target, the gunner need not centre it), and
//    the lead line from the gun's line to the lead mark (where to swing the gun).
// By the weapon's own SGO file (stores.h WeaponFile): every plugin gun has its own file; a weapon not in kRules (the
// stock vehicles') is the game's future war: radar for an anti-aircraft vehicle (reticle.h AntiAirClass), else computer.
// Pure (no EDF.dll): tools/firecontrol_check.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cwchar>

namespace crew {
namespace fcs {
enum class System : std::uint8_t { optical, gyro, computer, radar };
struct Kit {
    bool rangefinder;   // a measured range is shown (else the stadia are the range)
    bool lead;          // the computed lead mark on a ranged target
    bool solution;      // its time of flight and the target's speed by the lead mark
    bool leadLine;      // a line from the gun's line to the lead mark
    float pickCone;     // rad off the line of sight a target is acquired in
};
constexpr float kOpticalCone=0.087f,kRadarCone=0.26f;   // 5 degrees: the gunner lays on it; 15: the radar acquires it
constexpr Kit KitOf(System s) noexcept {
    return s==System::optical ? Kit{false,false,false,false,kOpticalCone} :
           s==System::gyro ? Kit{true,true,false,false,kOpticalCone} :
           s==System::computer ? Kit{true,true,true,false,kOpticalCone} : Kit{true,true,true,true,kRadarCone};
}
struct Rule { const wchar_t* prefix; System system; };
constexpr Rule kRules[]={
    {L"EDF6VC_FLAK88_",System::optical},
    {L"EDF6VC_STURMTIGER_",System::optical},
    {L"EDF6VC_VULCAN_",System::gyro},
    {L"EDF6VC_QUADVULCAN_",System::gyro},
    {L"EDF6VC_QVULCAN_",System::gyro},
    {L"EDF6VC_TUNGUSKA_",System::radar},
    {L"EDF6VC_GDF005_",System::radar},
    {L"EDF6VC_CIWS_PHALANX_",System::radar},
    {L"EDF6VC_BMPT_",System::computer},
    {L"EDF6VC_TOS1_",System::computer},
};
inline bool Starts(const wchar_t* name,std::size_t length,const wchar_t* prefix) noexcept {
    const std::size_t p=std::wcslen(prefix);
    if(!name || length<p)return false;
    for(std::size_t i=0;i<p;++i) {
        wchar_t c=name[i];
        if(c>=L'a' && c<=L'z')c=static_cast<wchar_t>(c-L'a'+L'A');
        if(c!=prefix[i])return false;
    }
    return true;
}
// The system of the weapon whose SGO file is `name` (`length` characters, no directory; nullptr: unknown), on a vehicle
// whose role is anti-aircraft or not.
inline System Of(const wchar_t* name,std::size_t length,bool antiAir) noexcept {
    for(const Rule& r:kRules)if(Starts(name,length,r.prefix))return r.system;
    return antiAir ? System::radar : System::computer;
}
}  // namespace fcs
}  // namespace crew
