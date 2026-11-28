// The Vehicler on foot (docs/newclass-re/vehicler.md §4, §5 M5; the pure part src/vehicler_foot_logic.h): no dash (its button held
// back over AssultSoldier's frame for a Vehicler), and the bullet core's falloff gate widened so AmmoDamageReduce [a > 1, b] grows
// a round's damage with its flight (the Gyrojet, tools/zw_weapons.py). The Gyrojet as the class's only hand weapon is the class
// data (src/soldier_classes_logic.h: the vehicle slot is a support slot), roll is the stock one.
#pragma once

namespace crew {
bool InstallVehiclerFoot() noexcept;   // module "vehicler foot" (src/module_table.h), kBeforeGameStatus after "soldier factory"
}  // namespace crew
