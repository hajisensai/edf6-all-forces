// The Epsilon 403's store holders and its slot 48 (src/storerecoil.cpp, docs/recoil-re.md §3.1).
#pragma once
#include <cstdint>

namespace crew {
// Chains the 403's VehicleBase slot 48 (0x5FED60). At load, after InstallGunnerRecoil (which checks that slot is stock).
bool InstallStoreRecoil() noexcept;

namespace store_recoil {
// What the 403's slot 48 does with a fired holder. `stock`: its own code. `hull`: the holder has no seat of its own
// index (a store holder tools/make_stock_stores.py appends after the stock three: the AGM-114s beside the rail gun ride
// seat 0), so the stock code, which takes holder i != 0 as seat i's machine gun (AimRecoil, seat[i]), must not run; the
// holder's own BodyRecoil record pushes the hull instead, as holder 0's does.
enum class Path { stock, hull };
// `index`: the holder's index in veh+0x638 (count `holders`); `seats`: the vehicle's seats (veh+0x618).
constexpr Path Route(std::uint64_t index,std::uint64_t holders,std::uint64_t seats) noexcept {
    return index!=0 && index<holders && index>=seats ? Path::hull : Path::stock;
}
}  // namespace store_recoil
}  // namespace crew
