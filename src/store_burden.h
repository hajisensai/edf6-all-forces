// What stores weigh and drag (test hub #39, the player: 「重量应该和挂载挂钩，某些挂载投完以后应该只有挂架重量（如有的话）」;
// docs/stores-re.md §12「重量和阻力：挂架」). Pure math, no EDF.dll: stores.cpp BurdenOf / BurdenOver and heli.cpp's stock-helicopter
// weighing call it, tests/store_burden_test.cpp checks it.
//  - a holder loaded with `rounds` rounds of a store hangs them on ceil(rounds / perPylon) pylons (a rail, a rack, a pod:
//    StoreSpec::perPylon, pylib/vcobjects.py Store.per_pylon); its rounds left weigh and drag each their own, its pylons
//    their own whether anything is left on them or not;
//  - the craft's burden: (clean + the stores and their pylons) / clean, and the drag's share over the clean craft's.
//  - a stock helicopter (heli.cpp HeliPhysicsHook) flies the game's own law (docs/aircraft-re.md §2), which has no mass:
//    its weight is its lift per rotor speed (veh+0x1610: the same rotor lifts a heavier craft less, the hover it needs
//    rises with it, its climb falls) and its horizontal blend (veh+0x1630: the velocity turns toward the stick that much
//    slower, its acceleration and its turns), both over the mass ratio.
#pragma once
#include "stores.h"

namespace crew::burden {
// The pylons a holder loaded with `rounds` hangs on: ceil(rounds / perPylon); none for perPylon 0 or no rounds.
constexpr int Pylons(std::int32_t rounds,unsigned perPylon) noexcept {
    if(perPylon==0 || rounds<=0)return 0;
    const auto per=static_cast<std::int32_t>(perPylon);
    return (rounds+per-1)/per;
}

struct Load { float kg,drag; };
// What one holder of `spec` loaded with `rounds` and holding `ammo` of them weighs (kg) and drags (a share of the clean
// craft's drag): its rounds left and its pylons. `ammo` beyond `rounds` (a holder loaded past its file's count) counts as
// loaded: its pylons hold what it holds.
constexpr Load HolderLoad(const StoreSpec& spec,std::int32_t rounds,std::int32_t ammo) noexcept {
    const std::int32_t left=ammo>0 ? ammo : 0;
    const int pylons=Pylons(rounds>left ? rounds : left,spec.perPylon);
    const float n=static_cast<float>(left),p=static_cast<float>(pylons);
    return Load{spec.mass*n+spec.pylonMass*p,spec.drag*n+spec.pylonDrag*p};
}

// The craft's burden from what its stores weigh (`kg`) and drag (`drag`) over its clean mass `cleanKg`: mass over clean
// (>= 1), the drag's share (>= 0). A craft without a clean mass: none.
inline Burden Ratio(float cleanKg,float kg,float drag) noexcept {
    if(!(cleanKg>0.0f))return Burden{1.0f,0.0f};
    return Burden{(cleanKg+(kg>0.0f ? kg : 0.0f))/cleanKg,drag>0.0f ? drag : 0.0f};
}

// A stock helicopter's flight params the burden changes (veh+0x1610 lift per rotor speed, +0x1630 the horizontal blend),
// as its physics step reads them for a mass ratio `mass` (>= 1; less is taken as 1: nothing aboard lightens it).
struct HeliFlight { float lift,blend; };
// A stock helicopter class's clean mass, kg (heli.cpp HeliType::mass; docs/stores-re.md §12): the 506 class flies the N9
// Eros and the Heron 602 alike (one runtime class, no field tells them apart), a medium utility helicopter's 5.5 t; the
// 409 Nereid an attack helicopter's 6.8 t; the 410 Brute a heavy gunship's 15 t (it carries no stores: stores.cpp builds
// none for its class). What matters is the ratio its stores make with it.
constexpr float kHeliMass506=5500.0f,kHeliMass409=6800.0f,kHeliMass410=15000.0f;
inline HeliFlight Weighed(HeliFlight own,float mass) noexcept {
    const float m=mass>1.0f ? mass : 1.0f;
    return HeliFlight{own.lift/m,own.blend/m};
}
}  // namespace crew::burden
