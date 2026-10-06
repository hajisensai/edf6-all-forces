# EDF6: Anti-Air Vehicles Must Actually Anti-Air

[简体中文](README.zh-CN.md)

The stock KG6 Kepler is EDF6's anti-air vehicle, yet it fires slow, non-exploding rounds that
mostly miss anything flying, with half the durability of a same-level tank. This mod makes it do
its job, and turns the DLC KG7 Bohr into a self-aiming ground-attack launcher.

It has two parts:

- **EDF6AutoTurret.dll**, an [EDFModLoader](https://github.com/BlueAmulet/EDFModLoader) plugin. The
  turret aims itself: it picks targets from the game's own enemy list, leads them, solves the
  round's ballistic arc and drives the turret with feed-forward so rounds stop trailing crossing
  targets. Flak rounds get a time fuse at the target's range, a proximity fuse and a contact fuse.
  Hold the aim stick to aim by hand; let go and the turret takes over again.
  It also crews the **side guns of the Titan and of the Ranger's gunner-seat tanks**: an empty
  gunner seat (or one an NPC sits in) aims and fires its gun by itself, on player- and NPC-driven
  tanks alike; a player in a gunner seat gets the auto-aim and keeps the trigger. This part needs
  no weapon files and works on the stock vehicles.
- **Weapon data** overriding the stock vehicles' own files. No weapon rows are added. The
  vehicles' descriptions in `WEAPONTEXT` are rewritten with the new numbers; only their own rows
  change, on top of whatever tables are already in `Mods`, so mods that edit the tables keep theirs.
  The data works on its own: the guns are ordinary guns the stock game fires, carrying a mark only
  the plugin reads. Without the plugin (removed, disabled, or refused after a game update) the
  Keplers and Bohrs still fire their new rounds, just unaimed and with the flak bursting at full range.

## What changes

| Vehicle | Change |
|---|---|
| KG6 Kepler, E, F, YE, YF | Flak: exploding rounds (8 m blast), proximity / time / contact fuses, 480 m range. Half the fire rate at twice the damage per round (same damage per second on paper, half the bursts on screen). Durability x2. DLC YF-HV turret speed. Air targets first. |
| Keplers the missions place (NPC-crewed, and the boardable mission ones) | The modded KG6 Kepler: its flak guns (in place of the NPC Keplers' own 1-damage guns), durability x2 on top of the mission's own multiplier, the fast turret. |
| KG6 Kepler YF-HV (DLC) | Auto-aim only; keeps its high-velocity solid shot, durability and turret. |
| KG7 Bohr, Bohr B (DLC) | Auto-aim in ground mode: ground targets first, lobbed rounds aimed on their arc, stock impact fuse. Durability x2, blast 4 m -> 6 m, and the blasts now wreck buildings. |
| Titan (all, incl. DLC side cannons) | Plugin only: both side cannons aim themselves; with no player in a gunner seat they also fire, as the driver's (player or NPC). Main cannon untouched. |
| NPC Titan (e.g. mission 64) | Data: the stock NPC Titan has empty side-cannon mounts; `build.py` gives it the player Titan's two side cannons, which the plugin then aims and fires. |
| Ranger tanks with gunner seats (Vehicle403) | Plugin only: both side machine guns, as above. Single-seat tanks (Air Raider's, Vehicle601) have no side guns. |
| Katyusha rocket truck (EDF6VehicleCrew's vehicle, `tools/make_katyusha.py`) | Plugin only, **NPC crews only**: its launcher carries the lofted mark (7303): ground targets first, on the **high arc** (the root above 45 deg); the low one only when the high one is past the launcher's 80 deg elevation stop (a target too close). A Katyusha the player rides is left alone (`PlayerLofted`): the camera follows the seat's aim axes, so steering them turned the player's view to the sky; the player aims with the camera and EDF6VehicleCrew lifts only the launcher's bone onto the arc (`src/katyusha.cpp`). |

Why: the stock Keplers deal a third to a half of the damage per second of same-level tanks and
helicopters, with under half their durability, and the shortest range of any of them. The Bohr
already out-damages the same-level Barrias TZ4 but has well under half its durability.

## Your own turret: auto-aim or a lead circle, lock what you look at

In a gun position the plugin aims (the Kepler, Bohr and howitzer turrets; the Titan's and the Ranger tanks' gunner
seats; driving those tanks you get the lock too):

- **Two modes**, switched with **Z** (pad: none by default, since L3 is EDF6VehicleCrew's free look; ini `AimModeKey` / `AimModeButton`, the starting one `AimMode`):
  - **Auto-aim** (default, as before): the turret turns itself onto the target, leading it on the round's arc.
  - **Lead circle**: the turret is yours alone; the HUD draws a green **lead circle**: put the gun's line through its
    centre and the round meets the target where the target will be (the gun's real round speed and drop, the target's
    tracked velocity: the auto-aim's own solve). A white cross shows where the gun's line points now at that range:
    the cross in the circle hits. Range and flight time under it; dim with `OUT OF RANGE` when the round's life falls
    short. The flak's time fuse still bursts at the target's range.
- **Lock by look**: **Q** (pad: X, the jets' next-target button; ini `LockKey` / `LockButton`) locks the enemy **nearest
  the screen's centre**: within `LockCone` (20 deg) of the view, within `LockRange` (0 = the gun's range), not behind
  terrain or a building (while EDF6VehicleCrew's map is open, Q turns the map: this plugin reads none of its keys then).
  Press again for the next one out from the centre, round to the nearest after the last. **Hold**
  it (`LockClearMs`, 0.6 s) to let the lock go. Locked: a yellow square closing in while it settles (~0.4 s), then the
  jets' red diamond; auto-aim fights **that target only** (it waits when the gun cannot reach it); the lead circle is on
  it; the AI gunners of the same vehicle take it first when they can reach it. It goes when the target dies or stops
  being lockable, gets 1.5x the lock range away, or you leave the seat. Unlocked, the auto-aim picks its own targets as
  before and the lead circle shows on its pick.
- Two lines low on the screen (EDF6VehicleCrew's `TurretAimHud=1`): whether auto-aim is on or off (green `AUTO-AIM ON`; amber `AUTO-AIM OFF (LEAD CIRCLE)`), and under it the switch key (`[Z] auto-aim off` / `on`; on a pad with no `AimModeButton` set, a hint to set one) and the lock key (red while locked). Pressing the switch key shows the new state in large letters over the screen's middle for about 1.5 s.
- **With EDF6VehicleCrew's turret camera** (its `DecoupledTurretCam=1`, the default: the mouse / right stick turns the
  camera and your turret follows the screen's centre), the gun you sit at is the camera's by default: this plugin never
  turns it onto a target of its own picking. In auto-aim it turns it onto **your lock** only (lock with Q / X); the
  camera stays yours meanwhile, and when the lock goes the turret follows the view again. In the lead-circle mode it
  never turns it. The stick is the camera's there, so it is never read as you dragging the gun (`DragDeadzone` does
  not apply to that seat). The two plugins tell each other who turns which seat (`common/edf/aimlink.h` V2); without
  EDF6VehicleCrew, with its turret camera off, or with an EDF6VehicleCrew older than that link, everything here works
  as described above (auto-aim on its own pick, the stick dragging the gun). The flak's time fuse still bursts at the
  tracked target's range either way. NPC gunners are unchanged.
  EDF6VehicleCrew's gun stabilizer (its `GunStabilizer`) holds a stabilized gun on its line in the world while the hull
  bumps and turns; this plugin then steers that gun (the flak's, a gunner seat's) from where the stabilizer holds it and
  leaves the hull's turn to it (`common/edf/aimlink.h` V3), so the hull's turn is never compensated twice.

The HUD, the camera's view ray and the line-of-sight test come from EDF6VehicleCrew (ini `TurretAimHud`). With this
plugin alone the modes and the lock still work, without anything drawn; the lock then looks along the barrel and does
not test line of sight. A Katyusha the player rides stays out of it (left alone, see above).

## Install

Requires EDF6 (Steam) with [EDFModLoader](https://github.com/BlueAmulet/EDFModLoader) installed.

1. Copy `EDF6AutoTurret.dll` and `EDF6AutoTurret.ini` from a release (or a CI build artifact) into
   `<EDF6>\Mods\Plugins\`.
2. Build the weapon files from your own game data (they are derived from it, so they are not
   distributed) with Python 3.10+, straight into the game's `Mods` folder (from the repository root;
   the game is found through `EDF6_DIR`, else the Steam libraries):

   ```
   set EDF6_DIR=C:\Program Files (x86)\Steam\steamapps\common\EARTH DEFENSE FORCE 6
   python autoturret\tools\build.py install
   ```

   It writes the vehicles' own call and gun files under `Mods\WEAPON\`, the mission Keplers and
   the NPC Titan under `Mods\OBJECT\`, and their eight rows of the `WEAPONTEXT.*.SGO` tables
   there; it reads the game's `Root.cpk` and never modifies it. It refuses while the game runs, and
   will not overwrite a `Mods` file another mod put there (`--force` backs it up and overwrites it).
   What it wrote and replaced is recorded in `Mods\.edf6at_data.json` (replaced files are backed up
   in `Mods\.edf6at_backup\`). Run it again after installing another mod that replaces `WEAPONTEXT`.
   `--no-text` leaves the text tables alone; `check` reports what is installed.

An interrupted install or upgrade can be rerun without `--force`, or uninstalled: the manifest records
both sides of each pending file/text replacement and retains the first backups. Subsequent edits by
other mods are still protected. `check` reports missing or pending files as incomplete.

To uninstall, run `python autoturret\tools\build.py uninstall` (game closed), then delete the plugin.
It restores the files it replaced, deletes the ones it created and puts the original text back in
its eight `WEAPONTEXT` rows; other mods' files and rows stay (anything changed since the install is
left as it is unless you add `--force`). It also cleans up an install made by the old `build.py`
(no record), removing only what matches the build byte for byte. Removing just the plugin is safe
too: the data keeps working without it. Settings are in `EDF6AutoTurret.ini` and apply while the
game runs; `Debug=1` writes what the turret is doing to `EDF6AutoTurret.log`.

## Build the plugin

Visual Studio 2022 with the C++ x64 tools (CMake and Ninja come with it):

```
build.cmd
```

The DLL lands in `build\Mods\Plugins\` (a build product, not in the repository). CI builds it on every push.

## Compatibility

Built against EDF.dll with TimeDateStamp `0x678CCB46`. The plugin checks the code it patches and
turns itself off if the game has changed (the weapon data keeps working without it). Weapon data
built by `build.py` before 0.3.0 still works with this plugin, which then patches the game's
fire check as the old one did; rerun `build.py install` to replace it.

The tank gunners were tested on the Titan (NPC driver, both gunner seats empty): both side
cannons fired at ants, 40 -> 28 rounds. The Ranger tanks' side guns have not been tested against
live enemies yet; send a `Debug=1` log if a side gun misbehaves. The stock game leaves an empty
gunner seat's gun silent because the gun asks the vehicle who operates it and an empty seat answers
no one; the plugin answers with the driver (see the re-notes). The input hooks chain onto whatever
another plugin (e.g. EDF6VehicleCrew) put in the same slot, so load order does not matter. In co-op, a remote
player in a gunner seat may look like an empty seat to your machine, so set `GunnerAI=0` online.

Online play is untested. The mod adds no weapon rows, so players without it never meet a row they
do not have; but each machine simulates the vehicles from its own files, so in a mixed lobby the
Keplers will not behave the same for everyone. Have every player install it. Reverse-engineering notes: [docs/re-notes.md](docs/re-notes.md).

## License

MIT, see [LICENSE](LICENSE). Bundled: `third_party/EDFModLoader/PluginAPI.h` at the repository root (MIT) and,
in `pylib/` at the repository root, `cpk.py` / `crilayla.py` (CPK / CRILAYLA readers from EDF6MultiSlot by
momotori01, public domain, `pylib/LICENSE.edf6-cpk`).
