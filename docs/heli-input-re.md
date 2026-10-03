# EDF6 helicopter input path (static RE)

EDF.dll TimeDateStamp `0x678CCB46`. All addresses are RVAs. Static analysis only; the game was not run.
"veh" = vehicle object, "seat" = `*(veh+0x608) + i*0x340`. Confidence: H = read directly from code,
M = strong inference, L = guess that needs a live cdb check.

## 0. Correction first: the heli has no AI think in "slot 72"

The heli vtables are shorter than the ground-vehicle ones. Checked with the COL in front of each table:

| Class | Primary vtable | Last own slot | What follows |
|---|---|---|---|
| `VehicleHelicopterBase` | `0x17DF790` | 61 (`0x650010`) | slot "62" = COL pointer of the next table |
| `Vehicle506_Helicopter` | `0x17DB238` | 63 (`0x61B8D0`) | slot "64" = COL pointer |
| `Vehicle601_Tank` (comparison) | `0x17DC250` | ≥ 82 | slot 72 = `0x661440` (CarBase AI) |

The table after the primary one is a **secondary vtable for the base subobject at `veh+0x120`**
(`NetworkObject@net` interface; COL offset `0x120`). For HeliBase it starts at `0x17DF988`, and for 506
it starts at `0x17DB440`. Our "slot 62–82" readings for helis are entries of that table, not
CarBase slots. Details (H):

- `0x6559D0` is secondary slot 7 in HeliBase, 410 and 506 (`0x17DF9C0`, `0x17DF568`, `0x17DB478`).
  It is **network replication**, not AI. It takes `this = veh+0x120` and a bitstream `rdx`. It
  returns early if `[this+0x1C8]` is set. It then builds a dirty mask in `bl`:
  `3` if `0x77C1F0(veh+0x1D80)`, `|4` if seat 0 has a live rider (`0x609650`), `|8` if
  `0x5EF880(veh+0x1690)` (fuel), and `|0x10` if `0x77C1F0(veh+0x1D94)`. It writes the mask with
  `0x12B5790` (a 5-bit write into the stream buffer) and serialises through `0x655C10`. It does not
  read `+0xE08`/`+0xE09`, and it does not fire or steer. Its neighbour slot 8 (`0x6501E0`) is the read
  side, and `0x654760` (`0x12B48A0`/`0x12B4BB0` stream reads) is the damage-message receiver.
- Slot 9 (`0x1251A0`) is the shared empty `NetworkObject` stub. It is not an AI stub.
- **Conclusion: there is no built-in helicopter flight AI.** The heli's own vtable has no AI-think slot
  at all; it ends before index 62. The plugin must fly it. (This also corrects the README line
  "heli slots 64/65/68/69/72/75/76/84 are empty".)

## 1. Per-frame order (H)

HeliBase slot 60 `0x652630(veh, phase)`, with `phase == 1`, calls:

1. slot 55 (`+0x1B8`): **input**. Base `0x6543A0` tail-calls slot 56 (`+0x1C0`, `0x656A30`, which
   only computes flags).
2. slot 57 (`+0x1C8`): **physics + weapons**. Base `0x6519A0`; 506 wraps it as `0x61B710`.
3. slot 58 (`+0x1D0`, `0x653EA0`), then the position integrate from `veh+0x1680`.

So a value written **after the stock slot 55 returns** is consumed by slot 57 in the same frame.
Hook point: `Vehicle506_Helicopter` slot 55 = `0x61B8F0` (entry `0x17DB238 + 55*8`). For other heli
classes use `0x6543A0` (base / 410 call it first), `0x64C020` (409) or `0x64E080` (410).

## 2. The input block the plugin should overwrite (H unless noted)

### 2a. Flight: `veh+0x1540` (written by base slot 55 `0x6543A0`, read by slot 57)

`0x6543A0` first resets the block (`0x65451A`–`0x654528`): `+0x1540 = +0x1544 = +0x1548 = 0`,
`+0x154C = 1.0f`, `+0x1550 = 0`. Then, **only if** `0x609650(seat0)` (the rider weak_ptr
`seat+0x268` has use count > 0), it copies the seat stick block:

| veh offset | type | stock source (`0x654543`…) | meaning / consumer |
|---|---|---|---|
| `+0x1540` | float | `-seat.LX` (`seat+0x2C0`) | **lateral**. Roll target `att.roll = lerp(att.roll, -V * maxTilt, tiltRate)` (`0x654E84`). Desired horizontal velocity `+= k*V*headingRow0` (`0x651E0A`). |
| `+0x1544` | float | `seat+0x2E0` (analog 0..1) | **collective / throttle** = target rotor speed. Passed as `xmm1` to the rotor `0x656660(veh+0x1BC8, thr, &veh+0x1580, &flags 0x1534, &fuel 0x1690)` (`0x651A24`). |
| `+0x1548` | float | `-seat.LY` (`seat+0x2C4`) | **forward/back**. Pitch target `att.pitch = lerp(.., V * maxTilt, tiltRate)` (`0x654E69`). Desired velocity `+= k*V*headingRow2` (`0x651E2F`). |
| `+0x154C` | float | constant 1.0 | w component of the vec4. Keep it at 1.0. |
| `+0x1550` | float | `-seat.RX` (`seat+0x2D0`) | **yaw**. Yaw-rate target `att.yawRate = lerp(.., V * maxYawRate, yawSmooth)` (`0x654E48`). Each frame `heading += yawRate` (`0x654ECA`). |

Attitude / controller state (heli controller at `veh+0x1580`, attitude `att` at `veh+0x15C0`; filled
by `0x654A80(veh+0x15C0, &body, &veh+0x1540, &contactByte)`):

- `veh+0x15C0/+0x15D0/+0x15E0`: heading-only basis rows (right, up, forward). Rebuilt with
  `0x4CD10(att, angle)`. (M)
- `veh+0x1600` pitch, `+0x1604` yaw rate, `+0x1608` roll, `+0x160C` = 1.0.
- `veh+0x1634` max yaw rate, `+0x1638` yaw smoothing, `+0x1640` max tilt, `+0x1644` tilt smoothing.
- `veh+0x162C` horizontal speed gain `k`, `veh+0x1630` velocity blend factor.
- `heli_movement` loader `0x6574F0` (`rdi = veh+0x1580`) stores, in this order:
  `+0x1614=0.99` (horizontal velocity damping), `+0x1610=70.0` (lift per rotor speed),
  `+0x1618=0.95` (vertical damping at hover), `+0x161C=1.0`, `+0x1620=0.15`, `+0x1624=0.125`,
  `+0x1628=0.05`. Where `+0x162C`/`+0x1630`/`+0x1634`/`+0x1640` come from was not traced (M/L).

Ranges and signs:

- The stick axes are not clamped by the heli code. The stock writer always gives −1..1. **Write
  −1..1.** Larger values would scale tilt and speed linearly (avoid).
- Throttle: `seat+0x2E0` is the analog trigger 0..1 (keyboard gives exactly 0 or 1.0; see §4). The
  rotor is a first-order lag towards the throttle: up rate `heli_roter[1]=0.001`, down rate `[2]=0.0007`
  per frame (`0x656744`, `0x656760`). Rotor speed is `veh+0x1BF8`. When the engine-on flag is set,
  speed is forced up to idle `[3]=0.13`. If the fuel `veh+0x1698 <= 0`, the throttle is forced to 0
  (`0x6566FE`). The hover speed is computed each frame from mass and gravity (`xmm7` at `0x651D9B`,
  from `veh+0x1610/0x1618/0x161C` and gravity). **Write 0..1 and close the loop on vertical speed
  with a PID.** The rotor lag is seconds long. No constant hover throttle exists.
- Sign conventions in the vehicle frame (H for the formulas; the world mapping is M):
  - `+0x1548 > 0` gives desired velocity along `headingRow2` (forward row) and pitch `+maxTilt`.
    Stock writes `-LY`.
  - `+0x1540 > 0` gives desired velocity along `headingRow0` and roll `-maxTilt`. Stock writes `-LX`.
  - `+0x1550 > 0` gives yaw-rate `+maxYawRate` added to the heading angle. Stock writes `-RX`.
  - Which way is world-left/right was not proved (EDF matrices may be left-handed). Do what the flak
    plugin does: learn the sign online, or test once with cdb.

Gates inside physics (no rider check; H):

- Contact byte `veh+0x1580` (mutex at `+0x1588`, set by the contact callback `0x652B30`, cleared each
  frame in `0x653050` while alive):
  - bit0 (any contact): pitch and roll are reset to level (`0x654E0F`).
  - bit1 (ground contact, normal y > 0): tilt and yaw input are ignored, the horizontal velocity input
    is ignored (`0x651DF2`), and the rotor up rate is scaled by `veh+0x163C`.
  - bit2 (set in slot 59 `0x6558F3`): input is ignored.
- `veh+0x1534` flags (slot 56 `0x656A30`): bit0 = seat 0 has a live rider; bit1 = engine on
  (`word[veh+0x628] & dword[veh+0x624]`, the seat-occupied mask); bit2 = rider and the seat-vacated
  bit; bit3 = upside down (`row up.y < 0`).

### 2b. Fire (506 / V602): `veh+0x2020`, `veh+0x2021` (bytes)

- Written by 506 slot 55 `0x61B8F0` after the base call.
  - If the seat 0 rider is alive: `0x656860(veh+0x2020, seat+0x2C0)` sets
    `+0x2020 = (seat+0x2E4 >= 0.8f)` (`0x62DE50`, constant `0x176B014`) and
    `+0x2021 = (word seat+0x2E8 >> 5) & 1`.
  - Otherwise: `0x64FFD0` clears both bytes.
- Consumed by 506 slot 57 `0x61B710`, after physics:
  - `+0x2020` triggers holder 0 and holder 1 (gatling L and R):
    `0x62C000(*(veh+0x638) + 0x00)` and `0x62C000(*(veh+0x638) + 0x48)`.
  - `+0x2021` triggers holder 2 (missile): `0x62C000(*(veh+0x638) + 0x90)`.
- The V602 SGO `vehicle_setup` weapon order is: `[0]` gatling L, `[1]` gatling R, `[2]` missile, and
  `[3]` `v_fuel01` (setting `-1`, never fired by input).
- 409 slot 55 `0x64C020` does the same, plus `+0x2090` from the authority check `0x630DF0`. 410 uses
  per-gunner-seat blocks at `veh+0x2030 + i*0x20` (seat index at `+0x2044`; trigger byte at
  `+0x2040`). Neither is used by V602.

## 3. Generic vehicle fire mechanism (H)

- Holder array: `veh+0x638` = pointer, `veh+0x648` = count, stride `0x48`. Holder `+0x08` is the
  control block (must be alive) and holder `+0x10` is the weapon. This matches the
  `seat+0xC8/+0xD8` layout in the flak notes. Iterated by `0x62DE60` and `0x633030`.
- `0x62C000(holder)`: if `holder+8 != 0` and `*(holder+8)+8` (use count) `!= 0`, set
  `*(holder+0x10)+0x139 = 1`.
- The weapon per-frame update `0x6934F0` latches the trigger:
  - `+0x13D = (!+0x13A && +0x139)` (press edge)
  - `+0x13A = +0x139` (held)
  - `+0x139 = 0` (cleared every frame)
  So **hold fire = set it again every frame**.
- Seat input convention used by all vehicles:
  - `seat+0x2E4 >= 0.8` is the primary trigger (`0x62DE50`; also used by the flak `0x621460`).
  - `word seat+0x2E8` is the button mask. The heli uses bit `0x20` as secondary fire. Ground vehicles
    also read bits `0x01`, `0x10` and `0x40` (`0x65A548`).
- For the plugin, either:
  - (preferred) write `veh+0x2020/0x2021` in the slot 55 post-hook. Stock slot 57 then fires L+R
    gatlings / the missile with all stock gating (ammo, reload). Or
  - set `weapon+0x139 = 1` directly for one holder (e.g. only one gatling), any time before that
    weapon's update in the frame. The stock 506 path always fires L and R together.

## 4. Where the seat block comes from, and player vs AI rider (H)

- The only writer of the seat stick block for a human rider is `HumanBase` vtable slot 4 `0x572DF0`
  (shared by `HumanBase`, `SoldierBase`, `People`, `HeavyArmor`). It runs only if
  `byte human+0x354 != 0` **and** `human+0x340` (pad object) `!= 0` (`0x572EFF`/`0x572F0C`).
  Otherwise it jumps past the block (to `0x573A4D`).
- When riding, `human+0x1540` = pointer to its seat (`human+0x1550` = vehicle ctrl block). It writes:
  - `seat+0x2C0/+0x2C4` = left stick (from `0x56D920`), `+0x2C8 = 0`, `+0x2CC = 1.0`
  - `seat+0x2D0/+0x2D4` = right stick (from `0x56DBC0`; per-player invert options flip the sign),
    `+0x2D8 = 0`, `+0x2DC = 1.0`
  - Pad path (`dil = 1`):
    - `seat+0x2B0 = 1`
    - `seat+0x2E0 = pad+0xCC`, `seat+0x2E4 = pad+0xE4` (analog triggers)
    - `seat+0x2E8` bits 0..7 = pad buttons at `pad+0x70/0x88/0xA0/0xB8/0x100/0x118/0x130/0x148`
  - Keyboard path:
    - `seat+0x2B0 = 0`
    - `+0x2E0/+0x2E4` = 1.0 or 0 from the key-config table (`+0x940`, `+0x958`)
    - bits from `+0x9A0/+0x970/+0x988/+0x9B8/+0x9D0` and others
- **A RideAi rider never writes the seat block.** `Vehicle_RideAi` = `0x633030`:
  - spawns a soldier and calls `0x54E740` on it, which sets `human+0x338 = -1` and
    `human+0x340 = 0` (no pad)
  - seats it (`0x633C10`) and sets `veh+0xE30 = 1`
  - clears `weapon+0x8B6` on every holder (`0x6330C9`)
  - A player instead gets `0x54ED40`: `+0x338` = player index, `+0x340` = pad.
  So for an AI pilot the seat block keeps whatever it held: the ctor zeros (`0x6291B0`), or the last
  player's values if a player rode it before. The heli then copies those stale values every frame.
- How to tell player and AI apart (in decreasing reliability):
  1. Rider `R = *(seat0+0x260)` (weak_ptr object; ctrl block `+0x268` must have use count > 0; M on
     `+0x260` being the object base). Player ⇔ `*(R+0x340) != 0 && byte(R+0x354) != 0`. AI ⇔
     `*(R+0x340) == 0`. (H for the fields; M for the pointer base.)
  2. `veh+0xE30 == 1` ⇔ RideAi was used. It is a serialised "ride mode" (state machine in `0x62FBF0`;
     `0x633280` sets 2). Good enough for our spawned NPC helis. (H)
  3. Heli slot 55 itself makes **no** player/AI distinction. Its only gate is "rider alive"
     (`0x609650`). (H)

## 5. Can anything stop an AI-ridden heli from obeying our writes?

- Slot 55 has no early return for AI riders. With a live AI rider it copies the stale seat block and
  then runs slot 56. Without a rider it zeros the block. Either way, **overwriting
  `veh+0x1540..0x1550` and `veh+0x2020/0x2021` in the slot 55 post-hook is final for this frame.**
  Slot 56 does not touch them, and slot 57 reads them. (H)
- Things that still limit the outcome (not input rejection):
  - Fuel: `veh+0x1690` struct, `+8` remaining (`veh+0x1698`), `+0xC` usage rate (multiplied by
    rotor speed in `0x5EF8E0`). If remaining ≤ 0, the throttle is forced to 0. The V602 capacity
    `999900` should last a mission.
  - The engine-on flag (`veh+0x1534` bit1) comes from the seat-occupied mask. RideAi seats the AI, so
    it should be set (M).
  - Contact bits (§2a) freeze horizontal input on the ground. To take off, raise the throttle first.
  - The world ceiling clamp in slot 55 (`0x65445B`…, Y limited to `*(*(0x20B2998)-8)+0x44`).
  - Weapon gating: ammo and reload inside the weapon. `weapon+0x8B6` is cleared by RideAi and set by
    the player ride path (`0x633950`). No reader was found by displacement scan, so its effect is
    unknown (L). Check in a live session whether an AI-ridden heli's gatling actually spawns rounds
    when `+0x139` is set.
  - Network authority: `0x630DF0` (used by 409 and the replication slot) queries `veh+0x120`
    authority. In co-op, only the authority's writes matter (M).

## 6. Plugin recipe

1. Hook `Vehicle506_Helicopter` vtable `0x17DB238` slot 55 (`0x61B8F0`). Call the original, then, for
   NPC helis (`veh+0xE30 == 1`, or the rider has `+0x340 == 0`), write:
   - `float[veh+0x1540]` lateral −1..1
   - `float[veh+0x1544]` throttle 0..1
   - `float[veh+0x1548]` forward −1..1
   - `float[veh+0x154C] = 1.0`
   - `float[veh+0x1550]` yaw −1..1
   - `byte[veh+0x2020]` gatlings (L+R)
   - `byte[veh+0x2021]` missile
2. Don't write for a player-ridden heli. Skip dead vehicles (`veh+0x2E8`).
3. Read back for control: rotor speed `veh+0x1BF8`, attitude `veh+0x1600/1604/1608`, heading basis
   `veh+0x15C0..`, contact byte `veh+0x1580`, matrix `veh+0x60`, position `veh+0x90`.

## 7. Open items for a live cdb session

- World-axis sign of `+0x1540` and `+0x1550`, and the actual gains `veh+0x162C/0x1630/0x1634/0x1640`.
- Rotor speed at hover for V602, and the time from idle to lift-off.
- That `*(seat0+0x260)` is the HumanBase object with vtable at +0.
- That an AI-ridden V602 really has engine-on (`veh+0x1534 & 2`), and that its weapons fire
  (`weapon+0x8B6`).
- That the slot 60 call order holds for 506 (it inherits HeliBase slot 60 `0x652630`).
