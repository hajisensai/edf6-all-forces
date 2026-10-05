# Reverse-engineering notes

All addresses are RVAs into `EDF.dll` with TimeDateStamp `0x678CCB46`, SizeOfImage `0x22CE000`.
`src/plugin.cpp` checks the bytes it relies on (`CheckProfile`) and refuses to load otherwise.
The layout facts EDF6VehicleCrew relies on too (vehicle, seat, rider, slot 55's signature) and the
memory / patch / seat code are one copy in `common/` at the repository root (`edf6common`, linked by
both DLLs).

## Vehicle603_Flak (Kepler / Bohr chassis)

| What | Where |
|---|---|
| vtable | `0x17DC620`; slot 55 = per-frame input `0x621460` (hooked) |
| vehicle matrix / position | `+0x60` (4x4, rows = right, up, forward, position) / `+0x90` |
| dead flag | `+0x2E8` |
| team | `+0x314` |
| seats | `+0x608` array, `+0x618` count, stride `0x340` |
| turret turn input written by the input slot | `+0x2AA0` (yaw, pitch) |
| seat: weapon holders / count | `+0xC8` / `+0xD8`; holder `+0x10` = weapon |
| seat: aim controller | `+0xE0`; axes at `+0x10`, stride `0x40`, `{min, max, angle, ...}` |
| seat: rider aim stick | `+0x2D0` (x, y) |

The input slot copies the stick into `+0x2AA0`; the plugin overwrites it after the stock code
runs. Turret rate is about 1.1–1.3 rad/s per unit input with the DLC turret parameters
`[65, 0.3, 0.3]`; the plugin learns the real rate per axis online. Pitch is negative-up
(-1.047 .. +0.087).

## Vehicle403_Tank / Vehicle404_Tank (Ranger gunner-seat tanks / Titan): the side guns

`src/gunner.cpp` checks its own byte list (`CheckGunnerProfile`); a mismatch leaves only the tanks stock.

| What | Where |
|---|---|
| vtables | 403 `0x17D8FA0`, 404 `0x17D9458` (both `Vehicle_TankBase`); slot 55 = input `0x5FEBE0` / `0x5FFC50` (hooked) |
| seats | 0 driver (main cannon), 1 GUNNER_L, 2 GUNNER_R; same seat layout as the flak |
| turn input of seat i | `+0x2AA0 + i*0x10` (yaw, pitch); slot 55 writes `(-stick.x, stick.y)`, or zeroes all three with no input |
| apply | slot 4 (403 `0x5FEE90`, 404 `0x5FFFC0`) feeds every seat's turn input to its `seat+0xE0` aim controller, unconditionally |
| triggers | `+0x638` array, `+0x648` count, stride `0x48`; `+8` weapon weak_ptr ctrl, `+0x10` weapon. Trigger i = seat i's gun (404: i+3 = its secondary weapon) |
| pull trigger | `0x62C000(trigger)`: if the weapon is alive, `weapon+0x139 = 1` (fire this frame); what slot 55 calls on the fire button (`seat+0x2E4` past threshold) |
| seat rider | `seat+0x260` object, `+0x268` weak_ptr ctrl (stored by `0x63407F`); occupied while the ctrl's use count is non-zero |
| player rider | rider `+0x340` (pad) set and `+0x354` (player-controlled) on: the test the human code makes before copying its pad into its seat (`0x572EFF`) and the vehicle makes before reading a pad (`0x673AC2`). NPC soldiers and the `DummyVehicleRider` (vtable `0x17D7320`, seated by `0x633030`, slot 50) fail it |

Muzzles (fire builds them at `0x6969A0`): weapon `+0x1D0` array, `+0x1E0` count, stride `0xF0`.
Muzzle `+0` bone (world rows right/up/forward/position at `+0xB0..+0xEF`, live every frame), `+0x10`
local 4x4, `+0xE0` mode. Mode 0: rotation = the weapon's world rows `weapon+0x150` (copied from its
aim bone every frame by `0x633DD0`); mode 1: local x bone. Position = row 3 of local x bone in both.
The round leaves along row 2 (`0x69168B` reads `+0x70` of the built matrix). The muzzle is only
rebuilt when firing, so the plugin rebuilds it every frame itself.

Game data: the Titan's side cannons are subCannon (RocketBullet01, 4 m/f, 600 f, gravity 2.0, blast
8 m), subCannonSolid (SolidBullet01Rail, 6 m/f, 150 f, gravity 1.0) or the DLC meltCannon
(AcidBullet01, 2 m/f, 300 f); roll axes -3..180 deg (left) / -180..3 deg (right), aim -30..12 deg. The
403's side guns are SolidBullet01 machine guns (6 m/f, 35 f, no gravity), roll -5..160 / -160..5 deg.
None reloads (ReloadTime -1).

## Who operates a weapon (why an empty gunner seat never fired)

Every weapon step asks its owner who operates the weapon: `owner = weapon+0x120` (the vehicle),
`iface = owner+0x120` (its second base, vtables 403 `0x17D9238` / 404 `0x17D96F0`), virtual
`+0x58` (slot 11) = `0x62D950(iface, weapon)`. It walks the seats (`iface+0x4E8` = vehicle+0x608,
count `+0x4F8`, stride `0x340`), finds the seat whose holders (`seat+0xC8`, count `+0xD8`,
weapon at `holder+0x10`) contain the weapon, and returns that seat's rider (`seat+0x260`, weak_ptr
ctrl `+0x268`) as `object+0x120`, else the seat's `+0x300` object (weak_ptr ctrl `+0x308`), else null.

Fire-start `0x690BB0` (and the round spawn `0x690CC0`) return without firing when the answer is
null, or when bit 0 of `answer+8` is set: an object another machine runs online. The other askers
(`0x690420`, `0x691240`, `0x691DC0`, `0x694730`, `0x695240`, `0x6963A0`, `0x696FD0`, `0x6B2EC0`)
read the same bit, falling back to a global network check (`0x784210`) on null.

An empty gunner seat answers null, so a pulled trigger was consumed (`weapon+0x139` -> held
`+0x13A`) and the gun still never fired: measured over 4344 log lines, ammo `+0xBE8` stayed at 40
(SubCannon's count; it falls by one per shot). The plugin hooks slot 11 of both interfaces: a null
answer for any weapon becomes the answer for the driver's gun (seat 0, first holder). A local
driver's machine fires the side guns; a remote driver's answer carries bit 0, so this machine
leaves the shot to theirs. With the hook, the user's hand-played round logged 334 lines all
answering 0, ammo 40 -> 28.

Other gates the fire path checks, all clear on the side guns: ammo `+0xBE8 > 0`, `+0x144 == 0`,
cooldown `+0xE0C` spent, `+0x145C` bit 0 clear, and LockonType `+0x6B0` 0 or 5 (or a lock).

## Weapon (fields filled from the SGO at `0x68D4A0`)

| Field | Offset |
|---|---|
| LockonType / LockonTargetType | `0x6B0` / `0x6B4` |
| LockonRange | `0x6D0` |
| AmmoClass factory (looked up by FNV-1a name hash, `0x1195C50`) | `0x7F8` |
| AmmoSpeed (m/frame) / AmmoAlive (frames) / AmmoDamage | `0x894` / `0x898` / `0x89C` |
| AmmoExplosion | `0x8B0` |
| AmmoGravityFactor | `0x8E0` |

- Fire-start `0x690BB0` refuses a lock-on weapon with an empty lock list unless LockonType is 0 or
  5. The mod's guns are type 0 with LockonRange 0: the stock game fires them with or without a lock,
  they never lock and never show lock markers, and they need no patch, so the data works without the
  plugin.
- The only stock reader of LockonTargetType (`0x696792`) maps it to a lock class (0 -> 3, 1 -> 2);
  a range-0 gun never makes the lock query, so the field has no effect on it. The mod's mark lives
  there: 7301 = anti-air gun, 7302 = ground-attack gun (`src/turret.h` kMarkAir / kMarkGround,
  `tools/build.py` MARK_AIR / MARK_GROUND). Needs an in-game check that the SGO's 7301.0 arrives at
  `+0x6B4` as the int 7301 (the plugin logs `DIAG ... weapons=0` on a flak if it does not).
- Before 0.3.0 the mark was LockonType 4 (no stock weapon uses it; 1 in LockonTargetType = ground),
  which only fired with the gate at `0x690C2E` (`cmp eax,5 / je`) patched to `cmp eax,4 / jae`. The
  plugin still recognizes such data and patches that gate only once it sees a type 4 gun
  (`LegacyFireGate`); remove with the next data break.
- Weapon vtable slot 17 = "round spawned" `(weapon, bullet)`, called once per round by fire
  `0x696FD0`, which spawns rounds through the factory at `0x7F8` (`0x6970A5`).

## Lock-target registry (the enemy list)

`*(0x20B2AB0)` holds an MSVC `std::list` at `+8`; each node's `+0x10` is a lock point `T*`:
`+0` kind (0 = enemy kind), `+8` owning object, `+0x10` aim point (refreshed every frame from the
bone matrix by `0x6C7700`), `+0x29` valid, `+0x2A` lockable. The lock query `0x696710` walks the
same list on the game thread.

Team relations: `rows = *(*(0x20B2978) + 0x38)`, `relation = *(rows + team*0x38 + 0x18)`;
`relation[otherTeam] == 2` means enemy.

## Gravity and the ballistic solve

The game's vehicle aim (`0x622640`) takes the world gravity vector from
`*(*(0x20B2958) + 0x68) + 0x20` (virtual slot 0 returns a pointer to it, m/s^2), rotates it into
the vehicle frame and drops a round by `AmmoGravityFactor * gravity / 3600` metres per frame^2
(`0x622B65`), then solves the two launch angles at `0x50350`. Measured gravity: about 14.7 m/s^2.
The plugin does the same and takes the lower arc; a lofted gun (mark 7303, the Katyusha) the higher one while its
pitch is within the axis' stops, else the lower.

### Rounds in flight (2026-10-05, static)

- Spawn (`0x231CC0` -> `0x231D97..0x231F14`): position `C+0xB80` = the muzzle matrix row 3; velocity `C+0xB90` =
  row 2 x `C+0xA04` (AmmoSpeed, m/frame) (+ owner velocity x AmmoOwnerMove at `C+0x9C0`), then divided by the
  vector 1/60 at `0x176B040` (so m/s); gravity `C+0xBA0` = AmmoGravityFactor (`param+0xB0`) x the world gravity
  vector (`0x231E7B`, the same virtual call as above); `C+0xBB0` the start position.
- Step (BulletControl `0x233CB0`, also `0x2349D0`): `v += g * 1/60` (`0x233DC4`), then `p += v * 1/60`
  (`0x233E18..`), the swept segment then ray- and shape-cast (docs/bullet-pass-re.md). Semi-implicit Euler: after n
  frames a round has fallen `g/3600 * n(n+1)/2`, `g/3600 * n/2` more than the parabola. `Ballistic` aims that much
  over the point (three passes); `pylib/ballistics.py` is the same model in Python, and `tools/selftest.py
  lofted_arc_solver` checks both roots against the per-frame step (miss < 5 cm).
- The request's tier does not touch the speed: a vehicle request's setup `[0]` is `[durability, damage]`
  (tools/call_weapons.py request_tier). The plugins read AmmoSpeed (`+0x894`) from the live weapon anyway.
- FireAccuracy (`weapon+0x378`, filled at `0x68CF1F`): fire `0x691B02` passes `weapon+0xE14 x FireAccuracy` to
  `0x4E820`, which draws the polar angle uniformly in `[0, it]` and the azimuth in `[0, 2pi)`: a cone half angle in
  radians. `weapon+0xE14`'s only writer found is `0x69DB11` (another weapon class); for Weapon_VehicleShoot it is
  assumed 1 (EDF6VehicleCrew's `LAUNCHER` debug line logs it).
- The launcher's elevation stop: `car_base_constraint_data`'s hinge limit `[1, min, max]` (degrees, pitch
  negative-up) is the aim axis' range: the V603 flak's `[1, -60, 5]` is its `-1.047..0.087` rad. The Naegling's
  `Rocketcannon_main` `[1, -50, 0]`; tools/make_katyusha.py writes `-80`.

Needs an in-game check: the Katyusha's pitch axis really stops at -80 deg (`AIM` debug lines show `pitch`); the
rockets' look (bullet_rocket.rab on a GrenadeBullet01: nose along the flight, the 120-frame smoke trail); the
CCIP cross where the rockets land (EDF6VehicleCrew `LAUNCHER` lines: muzzle count, speed, cone); `weapon+0xE14`
being 1 on the launcher; the high-arc auto-aim hitting.

## GrenadeBullet01 (the flak round)

| What | Where |
|---|---|
| vtable | `0x17A17E0`; slot 1 deleting dtor `0x265B10`, slot 5 update `0x264AB0` (both hooked) |
| factory vtable | `0x17A1688` (`GrenadeBullet01_MapNoDamage`, the stock Bohr's round that spares buildings, is a separate class: `0x17A16E8`, factory `0x17A16C8`; the mod switches the Bohr to `GrenadeBullet01` and tells it apart from flak by LockonTargetType 1) |
| weak-this control block | `+0x30` |
| flight control block C | `+0x140` |
| C: flags / age / lifetime | `+0xAF4` / `+0xAF8` / `+0xA08`; expires when age >= lifetime (`0x236899`) |
| C: position / velocity (m/s) / stuck | `+0xB80` / `+0xB90` / `+0xC00` |
| C: blast radius / burst effect size | `+0x788` (damage sphere) / `+0xA20` (drawn at `/5`, `0x264B92`) |

`Ammo_CustomParameter[0] = 1` bursts on expiry (flag `0x20`); bounce 0 sticks the round to what it
hits. The fuses set age = lifetime before the stock update runs, so the round bursts that frame at
its (possibly moved) position.

The time fuse is per round: each frame every flak's input stamps its flak guns with the flight time
to the tracked target and the vehicle's team; the weapon's spawn slot (17) tags the new round with
its gun's stamp, and the round's first update sets its own lifetime `C+0xA08` to it (never above
what the data gave it). The weapon's `AmmoAlive` (`+0x898`) is never written: it is shared by every
round the gun fires and was left shortened when a rider got out. The proximity fuse checks the
round's flight segment against the enemies of the team that fired it, from one snapshot of the
lock-target registry taken per frame (all of it, no range limit).
