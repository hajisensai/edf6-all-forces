# Real tank gunners (2026-10-08)

Three independent AutoTurret paths created ghost gunners: `Wanted(Crew::none)` accepted empty seats; `SeatCrew` treated `DummyVehicleRider` as an NPC soldier; `WeaponUser` replaced a null side-weapon operator with another seat's `LocalOperator`.

All three are removed. The shared `edf::LivingSoldierInSeat` combines the native occupied-seat weak-reference check with one of the supported four Human soldier vtables, non-dead state and finite positive HP. A local real NPC may aim/fire its own seat. A local human gets only explicitly enabled `GunnerAssist`. Remote occupants are left to their own machine. Empty/dummy/dead/unknown occupants cannot receive a side weapon's native operator, even if another seat has a driver.

Trigger latches raised by AutoTurret are tracked by vehicle, holder, control-block and weapon identity. Both tank input hooks release their preceding owned writes **before** invoking original input, including after configuration changes, dismount, death, removal and authority change. Pre-existing non-plugin latches are neither claimed nor cleared. The stock input can then write a player's new trigger normally. Secondary missile pulls use the same admission and ledger. This does not implement dynamic DLL unloading: the existing plugin remains loaded while chained vtable hooks point into it, as its loader contract requires.

Validation: complete AutoTurret `/W4 /WX` build, production `real_turret_crew_test` (25 cases, executing the known native holder-pull byte sequence), and existing embedded-aim regression. The fixtures cover actual shared seat parsing, operator callback, authority boundaries and lifecycle, without running or modifying the game. No live or two-machine verification was performed.
