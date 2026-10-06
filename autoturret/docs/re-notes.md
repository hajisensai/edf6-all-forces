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
pitch is within the axis' stops, else the lower (NPC crews only: see "The Katyusha's camera and pose"). The solve is
one copy, `common/weapon.cpp` `edf::BallisticArc`, which EDF6VehicleCrew's Katyusha loft calls too.

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

Needs an in-game check: the Katyusha's pitch axis really stops at -80 deg (`AIM` debug lines show `pitch`: seen in
the user's log 2026-10-05, `pitch -> -1.386`, within the stop); the
rockets' look (bullet_rocket.rab on a GrenadeBullet01: nose along the flight, the 120-frame smoke trail); the
CCIP cross where the rockets land (EDF6VehicleCrew `LAUNCHER` lines: muzzle count, speed, cone); `weapon+0xE14`
being 1 on the launcher; the high-arc auto-aim hitting.

### The Katyusha's camera and pose (2026-10-05, static)

The user's play (EDF6VehicleCrew.log 18:36-18:38: `LAUNCHER elev=75..79`, `AIM lofted ... pitch -> -1.3..-1.39`):
"the camera stares at the sky, the hydraulic rod is too short and parts from the launcher".

- Aim input. Vehicle402_Rocket slot 55 (`0x5FD8E0`) writes the rider's stick (`seat+0x2D0`) into the turn input
  `+0x2AA0` (`0x5FD957`); slot 4 (`0x5FDB00`) hands it to the seat's aim, `VehicleWeaponAim` (vtable `0x17D8A68`,
  RTTI `.?AVVehicleWeaponAim@@`) at `seat+0xE0`, slot 2 `0x5FBDA0`. Its axes (`+0x10`, stride `0x40`: min, max, angle,
  velocity, ...; params at `+0x90`) step by the input (`0x5FBC00`); mode `+0xC8` 1 (a target angle at `+0xA0/+0xA4`
  the axes chase) is only set when `0x7748F0` says the game is online (`0x672820`, slot 6 `0x6731C0`), so offline the
  axes are the turret's only aim state. Each axis maps its angle onto its bones through entries at `axis+0x28`
  (stride `0x38`, `0x5FC280` -> `0x5FC630`): `[seat+0x158]+0xC` is the launcher's pitch the pose reads. Slot 3
  `0x5FACD0` builds the aim's matrix from the two angles. Common layout: `common/edf/layout.h` kSeatAim / kAimAxes.
- The camera. Not traced to its reader (the human's camera code, `0x572DF0` / `0x54DDF0`, and the scene cameras were
  not followed through). What is known: offline the axes are the one aim state and the launcher's bone follows them,
  and steering the axes (EDF6AutoTurret writes `+0x2AA0`) turned the player's view with the launcher. The fix assumes
  the camera reads the axes (or the aim's matrix), not the launcher's bone: EDF6AutoTurret no longer steers a lofted
  launcher the player rides (`PlayerLofted`), and EDF6VehicleCrew lifts only the bone. **In-game check**: with
  `Debug=1` EDF6VehicleCrew logs `LOFT ... bone=<held> stock(axis)=<axes> camera=<screen centre pitch>`; `camera`
  must follow `stock(axis)`, not `bone`. Were it the bone, the sight would run away (lofted bone -> camera up -> no
  ground -> bone back down), and the fallback would be a camera of the plugin's own.
- The pose. Slot 45 (`0x5FD980`) calls `0x6EDCA0` (the car base), then `0x5FDDA0` (`call` at `0x5FD99C`), then
  `0x1100B90` (the model: the root bone's world = the vehicle matrix, then `0x1100010`: every bone in order with
  `+0x8 == 1`, world `+0xB0` = local `+0x70` x parent's world). `0x5FDDA0`: the turntable's yaw (`0x661C00`), the
  launcher's pitch (`0x661810`: the bind local from the skeleton, `*(veh+0xEE0)` records `+0x58` stride `0xD0`, last
  index `+0x68`, local at `+0x20`, turned by `[seat+0x158]+0xC`; writes world and local), then the prop: angle =
  `asin(dot(turntable up, launcher forward))` (`asinf`), clamped to `[-pitchMax, -pitchMin]` of the seat's pitch axis,
  plus the prop's bind angle (`+0x2AC0`, set in the constructor `0x5FD604`), written into the prop's local as
  `Rx(angle) Ry(90 deg)` (`0x5FDF57..0x5FE095`): the whole prop turns by the launcher's elevation about the cylinder's
  pivot. The weapon aims along `Rocketcannon_main` (`vehicle_weapon_setting`), its muzzles' rows copied from that
  bone (`0x633DD0`). Bone records the constructor caches: `+0x2AC4` (a 4-letter name), `+0x2AC8` base, `+0x2ACC`
  main, `+0x2AD0` prop (looked up by name).
- EDF6VehicleCrew (`src/katyusha.cpp`) redirects the call at `0x5FD99C` (bytes `E8 FF 03 00 00`, checked) through a
  stub near the image to `PoseHook`: the stock `0x5FDDA0`, then (the Katyusha's model only: it has the bone
  `edf6vc_ram_rod`) the launcher's local at the elevation `src/launcher.cpp` LoftWant asks for (the high arc onto the
  ground point under the screen's centre, `edf::BallisticArc`, eased at 1.1 rad/s; no ground under it: the stock
  pose), and the ram: the cylinder (`Rocketcannon_prop`) and the rod (`edf6vc_ram_rod`, at the eye) both turned by
  the angle the line from the cylinder's pivot to the eye (fixed on the launcher) has turned since bind. The world
  pass right after builds the bones (and so the muzzles) from those locals.
- The stock ram measured (`pylib/katyusha_model.py ram_report`, the built model): turned with the launcher, the eye
  stays inside the launcher's box up to 50 deg, is 0.14 m out at 55, 0.35 at 60, 1.09 at 75, 1.35 at 80. The
  telescopic one: 1.49 m long at 0 deg, 1.77 at 80 (stroke 0.28 m, the rod lengthened 0.36 m into the cylinder), the
  rod's front inside the cylinder and the eye inside the launcher at every 5 deg (`check_ram`).

Needs an in-game check: the `LOFT` line above (camera vs bone); the rockets leaving along the lifted launcher and the
CCIP cross near the screen's centre on the ground; the ram drawn joined; the launcher's rigid body (`car_base_rigid_body`
Rocketcannon_main, 0.25) following the lifted bone rather than holding the axis' pose, and nothing after the world
pass rewriting the prop (it is a `car_base_simulation_node`, as the wheels are); the pose hook not fighting
another plugin's (the bytes are checked, a mismatch leaves the stock pose and logs `KATYUSHA pose call ... changed`).

## The player's turret: the mode, the lock, the lead circle (2026-10-06, static)

`src/designate.cpp`; the math in `src/aimmath.h`, checked offline by `tools/turret_lead_check.cpp`.

- Bindings. The seat's rider input: `seat+0x2B0` byte 1 = a pad, 0 = keyboard and mouse; `seat+0x2E8` the pad's
  buttons as a word (A 0x01, B 0x02, X 0x04, Y 0x08, LB 0x10, RB 0x20, L3 0x40, R3 0x80). Confidence M: measured on the
  506 (EDF6VehicleCrew `docs/stores-re.md` §4) and read the same way by its heli and artillery code; every vehicle
  seat is the one class (stride `0x340`), whose input the human code copies before the vehicle's slot 55 runs. The
  keyboard is read with `GetAsyncKeyState` while one of the game's windows is in front (the keys' mapping to those
  bits depends on the game's key setup).
- Lock by look. The view is EDF6VehicleCrew's camera ray (its HUD's view-projection inverted, `hud.cpp CameraRay`) and
  the line of sight its map ray (`heli.cpp MapRay`: the game's Havok cast, layer 22 with the ground collector:
  terrain and buildings, never units), found through `common/edf/aimlink.h`. The candidates are this frame's
  registry snapshot (the same one the aim and the proximity fuse use), one per object (its lock point nearest the
  view). A lock press acts on its release; held `LockClearMs` it lets the lock go instead.
- The lock and the aim: `PickTarget` / `PickGunTarget` take the lock as `only`: kept while reachable, nothing else
  while it is not. An AI gunner seat of the player's vehicle takes it as its `keep` (first when reachable).
- The lead circle: the target's velocity is the aim's own (`Track::vel`, smoothed per frame); the solve
  (`aim::LeadSolve`) is `edf::BallisticArc` from the gun's real muzzle (`edf::MeanMuzzle`) in the vehicle's frame, the
  flight time taken to the lead point itself until it moves under 1 cm. The circle sits on the required direction at
  the lead point's range, the bore cross on the barrel's: both at the same distance, so the cross in the circle is the
  barrel on the required direction whatever the camera's offset from the gun. In the lead-circle mode the input
  (`vehicle+0x2AA0`) is not written at all; the flak's fuse stamp still uses the flight time.
- Needs an in-game check: the bindings reaching `+0x2E8` in a flak / tank seat (the `PILOT` and `LOCK` log lines say
  each press), the lock picking what is under the crosshair (the camera ray is the HUD's last frame), the lead circle
  on a crossing air target with the flak (the cross in the circle should burst the shell at it).

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
