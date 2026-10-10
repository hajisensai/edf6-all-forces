# Sea rescue: RE notes and design

EDF.dll TimeDateStamp 0x678CCB46. Confidence: H = read from the code or the data; M = inferred from
code plus precedent; L = assumed, not checked.

## Board button (H)

- 0x59B417: `cmp byte [rbx+0xD78],r15b` → `call 0x56D700` with rcx = human. It takes one argument and returns void.
- 0x56D700 returns early in any of these cases:
  - the vehicle weak pointer at human+0x1550 is live;
  - human+0x128 has bit 0 set;
  - human+0x5D0 has bit 2 set;
  - human+0x39C != 0.
- Otherwise it visits group 5 (0x5E0D60) and then the human's team (0x5E11D0) with functor 0x17CFCF0.
  - Slot 1 (0x572610) dynamic-casts each object to a vehicle and calls vtable+0x188 (slot 49, FindSeat).
  - On success it sets human+0x1540 to the seat and human+0x1548 to the vehicle (via 0x56A3D0), then stops.
  - Once the human rides, it calls 0x5763E0 (the ride call) and an animation on human+0x1150 (0x551C30).
- The visitor has no range check of its own. The reach lives in CanRideSeat, below.

## CanRideSeat 0x6346D0 (H)

- **Team.** If the teams differ and the vehicle's team != 5, the seat needs bit 7 of (seat+0x34 & seat+0x30).
- **Class.** (human+0x31C & seat+0x34) & seat+0x30 must be non-zero. The code is at 0x6346FC.
- **Free.** The seat's rider ctrl (+0x268) must be null or dead.
- **Reach.** The human's position must be within the riding point's radius plus 0.5 (the float at 0x1C36990).
  - It compares the 3D distance against that sum.
  - The riding point comes from 0x6BB420(seat+0x1E0):
    - the MAB record is *(seat+0x1E0);
    - the local vec4 is at record + *(int*)(record+0xC);
    - the bone is *(seat+0x1E8);
    - world = l.x·B0 + l.y·C0 + l.z·D0 + l.w·E0, over the bone's rows.
  - The radius is *(float*)(record+0x10).

The plugin reads exactly this. If the bytes at 0x6346FC, 0x634726, 0x634758 or 0x6BB420 differ, or the constant is not 0.5, it never computes the reach. In that case the heli only hovers beside the swimmer.

## Seats (data H, meaning M)

- V506_HELI vehicle_riding_position has only 506_HELI_DRIVER (mask 9). A player boarding it would bump the NPC pilot out.
- VEHICLE410_HELI has a driver plus 410_HELI_GUNNER_L and 410_HELI_GUNNER_R (mask 15).
- So the rescue uses the 410, a deviation from the 506 in the first design.
- Spawned helis are team 2 (friend). With masks 9/15 there is no bit 7, so the player could not board one through the team test.
  - While the rescue runs, the heli is put on team 5 (kTeamVehicle). It gets its own team back when it leaves.
  - Precedent: crew.cpp writes kTeam the same way. M: other team-5 side effects in combat are not checked.

## Behaviour

**Trigger:** all of the following:
- SeaRescue=1;
- a live submarine carrier (SubDeck);
- the local player on foot (human+0x1550 empty);
- the player's y < RescueBelow for 1.5 s.

**Launch (2026-10-09):** the support catalog's last entry, `RESCUE` (support_call.h SupportRescueAt), requested at the
swimmer through the ordinary support request path (offline / a one-player world's host: planned here; any other online
machine: through the host's transaction, every peer announcing `kCapSeaRescue`).
- Planned as air support (`PlanAirSupport`): an entry at the map's edge, a clear corridor, open sky over the swimmer.
- One 410 made at the takeoff point, its real pilot and one door gunner made inside it and seated at once
  (`BoardAirborne`, `NpcSeatCrewNow` in seat order: pilot seat 0, gunner seat 1 = `410_HELI_GUNNER_L`), all registered on
  every peer. Seat 2 (`410_HELI_GUNNER_R`) stays free for the swimmer; `DoorSeat` finds it.
- Takeoff point (2026-10-10): the nearest of the candidates whose 30 m climb column and level corridor to over the
  swimmer are clear (`support_entry.h TakeoffRoute`), else the map's edge (`PlanAirSupport`). Candidates: the carrier
  deck point nearest the swimmer, 20 m into the deck; and every pad of the mission (`helipad.h`: where a stock-bodied
  helicopter rested on solid ground, within 4 m of it and not over water, for 3 s; 25 m apart, at most 16; none a
  helicopter stands on now). Made 2 m over the spot. Over the hull footprint (or within 15 m of it) the heli keeps 8 m
  over the deck. The game's data has no airfield: no runway / apron / hangar / helipad piece in any of the 50 .MAC
  archives, and the only mission helicopters (M017, M031D, M031E) are route-flying event helicopters.
- Protocol (2026-10-10): its own channel when every peer has `kCapRescueChannel` (one rescue in flight per requester,
  beside the map's single one, no 2 s rate) and the host's per-requester cooldown (`SeaRescueCooldownSec`, from the
  transaction going active; a cancelled one starts none). The swimmer's machine holds its own cooldown too (from the
  heli handed to its call).
- Not held to the mission's support policy, nor to the map's queue / 30 s cooldown / status line.
- The dispatcher hands it to heli.cpp (`RescueHeliDeployed`) with its requester: offline / a one-player host this
  machine's player, a guest's by the transaction's requester PUID resolved to its mission player actor
  (`SupportTransactionRequester`). The requester gone, dead, ashore 3 s, in another vehicle or in another rescue's heli
  calls it off (`rescue_logic.h PickupCancel`, logged `RESCUE cancelled: ...`) and the heli leaves (`StartLeave`). With no
  requester known it leaves at once. On the swimmer's machine the call presses the board button.
- No heli (refused, cooldown, no sky / corridor, an older peer, nothing within 150 s): logged
  `RESCUE request failed`, shown on the HUD, asked again 10 s later.
- Before this, one 410 was made 60 m above the carrier's deck with an empty pilot seat (HeliLaunch): since the real
  crews (c21f499) nobody sat in it and it fell. That spawner is gone (tests/rescue_support_guard.py).

**Pickup:**
- It flies to the player at sea level + 20 m.
- Within 40 m it brings the nearest free door seat's riding point to 0.5 m over the player.
  - The heli origin stays at least 1.5 m over the sea (assumed y = 0, L).
  - Once it is within 10 m of that spot, Avoid's 6 m ground floor and wall rays are off.
- It then **hovers and waits**, and the player boards with their own button.
- With RescueAutoBoard=1, the plugin calls 0x56D700 for the player every 0.5 s, but only while the computed riding-point distance is within the stock reach.
  - Bump is off during the call, so it can only take a free seat, never the pilot's.
  - Every press is logged with its distance and reach.

**Ferry:**
- The player is in seat ≥ 1. The heli flies at ≤ 15 m/s to the carrier's deck point and hovers 8 m above it.
- While it is lower than deck + 5 m and within 80 m of the hull footprint, it only climbs (map rays do not see the hull).
- The player jumps off themselves. Then the heli gets its team back and leaves (StartLeave).

**End:** the rescue ends in any of these cases:
- the player is out of the sea for 3 s;
- the player is in another vehicle;
- 120 s pass near the player without boarding;
- the player is not seen;
- the heli is lost;
- the player takes seat 0 (the heli becomes theirs and is left as is).

The flight (where it is flown) and the call (the swimmer's machine) each test these on their own copy; offline both run
on the same heli.

After it ends, there is a 30 s pause before another rescue.

The player's position is never written. Only the stock boarding seats them, and they get off themselves.

## Unverified (L)

- The sea surface y and whether map rays hit the water.
- The actual radius of the 410 door seats' riding points, and whether a heli 1.5 m over the sea brings them into reach of a swimmer. The logs report both.
- Whether a swimming human passes the 0x56D700 state gates (+0x128, +0x5D0, +0x39C).
- Whether the 410 is preloaded on sea missions.
- Online: whether the host sees a guest's swimmer among the mission player actors (SupportMissionPlayerObjects), whether
  a guest's board press takes a door seat of the host's registered copy, and whether team 5 set on a peer's copy holds.
