// A remote gunner's recoil on the vehicle's authority (src/gunnerrecoil.cpp, docs/online-re.md §10).
#pragma once
#include <cstdint>

namespace crew {
// Chains VehicleBase slot 52 (the weapon message, 0x630900) of the classes whose slot 48 adds body recoil. At load.
bool InstallGunnerRecoil() noexcept;
// Whether this machine is the vehicle's authority (online_authority.h IsOnlineAuthority: seat 0's rider's machine, its
// last rider's, the host for an NPC driver with no identity, which every machine that seated one takes for its own).
// True offline.
bool VehicleAuthority(unsigned char* vehicle) noexcept;

namespace gunner_recoil {
struct Shot { int count; };
// How many of a received message's shots this machine adds to the hull: the shots the stock receive left unfired
// (`after` over `before`, the weapon's shot count around the message) of a gun another machine's player operates, a
// BodyRecoil mount (`recoilType` 0), on the authority only; capped.
Shot Decide(std::int32_t before,std::int32_t after,bool remoteOperator,bool authority,std::int32_t recoilType) noexcept;
}  // namespace gunner_recoil
}  // namespace crew
