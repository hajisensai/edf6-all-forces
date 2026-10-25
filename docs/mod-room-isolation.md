# All Forces room isolation (2026-10-08)

All Forces resources are not safe to load on a vanilla installation. Public-room
isolation therefore namespaces the **existing native `SEARCH_TYPE` key**, not a
new key that an unmodified client would ignore. Wire value = `0x41460100 + type`;
the low byte retains vanilla room kinds (`0x91..0x94`) or EDF6Coop's current
capacity family (`0x12..0x15`). Both bounds of the native EOS search are shifted.
The game-facing attribute copy restores the low byte for room labels.

The original EDF.dll search function `0x74AC50` never includes those wire values,
and its invitation predicate `0x749AC0` rejects them. All Forces also checks the
raw EOS room type **before** calling `JoinLobby`, rejecting missing, vanilla and
unknown-version types. This closes direct invitations in the other direction.
Creation and joining remain disabled if namespace setup cannot finish after its
entry gates install. Hook chains preserve previously installed wrappers.

With EDF6Coop, update both plugins. Coop's direct EOS reads recognize the prefix
only when the All Forces DLL is loaded. `AF_PROFILE=1` is additional persisted
metadata for DirectNet's remembered/virtual rooms: it is not the public-search
barrier. It survives game-facing type decoding, prevents joining a remembered
All Forces room after removing the mod, and gates both ordinary rejoin and the
full-EOS-lobby fallback. Synthetic lobby handles are checked by their owner before
All Forces passes anything to EOS, in either plugin load order.

`mod_room_test` executes the production wrappers against recording endpoints. With
an explicit supported EDF.dll, it installs them into that DLL's private IAT and
executes the original search and invitation functions. It covers both room families,
all four room kinds, same/missing/future profiles, callback ownership and copied
attribute release. No DLL entrypoint, game process, network lobby or multiplayer
session is run. The Coop fixture covers the symmetric persisted-room profile
matrix, including already-decoded attributes and removal of All Forces.

Breaking resource compatibility must allocate a new protocol prefix/profile;
this value is a compatibility family, not an arbitrary plugin version string.

Join diagnostics in `EDF6VehicleCrew.log` distinguish namespace readiness, Coop
dependency errors, virtual-room admission, raw SEARCH_TYPE reads and EOS handoff.
`ROOM content check` reports the final lobby ownership answer after chained
plugins, alongside the installed pack ids; an inner plugin's native refusal may
still be overridden by MOD ownership. See
[2026-10-15 room-join feedback](feedback-2026-10-15-room-join.md) for the observed
handshake failure and the limits of host-only logs.

## Required Coop bridge for online play

The real-crew/support protocol now requires the matching EDF6Coop installation.
Before either native CreateLobby or JoinLobby, All Forces checks extension ABI v1
(including every callback), the installed mission-admission hook readiness, and
the mission player/PUID resolver export. Missing, old, incomplete or uninitialized
Coop is rejected before invoking EOS, with a Chinese dependency dialog and a
specific UTF-8 log entry. Offline play is unchanged.

This is a **menu prerequisite**, not a mission quorum check: it never queries
world readiness, participant identities or the mission extension snapshot. Thus a
fully installed bridge can create/join a room before any mission exists. The test
matrix exercises both IAT entry points and verifies that no world/identity query
was made. The ABI header is shared with the support transport consumer.

The existing `EDF6AF_RoomIsolationReady` export includes these same prerequisites,
so Coop cannot bypass the native guard through a synthetic/full-lobby join when
it is the outer wrapper. The check is dynamic on every request: loading Coop later
or finishing its admission-hook installation clears an earlier refusal without
requiring a mission or caching the startup's false result.

## Mission pack identity (2026-10-18)

A room publishes the game's own `DLC` (the mode's content id) and `MISSION_NO` (the
list row) and nothing else about the mission. Before this change neither was the
same on every machine:

- `tools/make_edf5_campaign.py` gave the packs the mode table's highest id + 1
  (at least 3), read from whatever `CONFIG.SGO` another mod had put in `Mods/`.
  With one more mod mode a machine had EDF5 at 5 instead of 3; the guest's
  ownership hook (`src/edf5campaign.cpp`, call site `0x8EC659`) then refused the
  host's id: 「未持有此内容」 ([2026-10-15 feedback](feedback-2026-10-15-room-join.md)).
- A mission whose resources were missing (an EDF4.1 map that did not convert, an
  EDF5 DLC map left out) was dropped from the list, so every later row moved: the
  same `MISSION_NO` named another mission on the other machine.

Now:

- **Fixed ids.** The seven packs are 65..71 (`CONTENT`, `src/mission_packs.h`):
  EDF5 65..67, test range 68, EDF4.1 69..71. A mode of another mod on any of them
  refuses the install with the mode and id named. Every read of the id in EDF.dll
  is a 32-bit int (ModeList `+0x74`, the `DLC` attribute via `movsxd` at
  `0x749CF0`); nothing indexes by it (static scan, M). The ini keys keep naming
  which runs are installed; an older installer's value (3, 6, 7, ...) is logged and
  taken as 0 until the installer runs again, which rewrites it.
- **Stable rows.** A mission this machine cannot install stays at its row as a
  placeholder (title marked, reason as briefing; its scripts not written) and no
  row names it as successor. The plugin's mission-select hook (`0x8A21C3`) sets its
  state to 3 "shown, locked": the select refuses states above 2 (`0x8A8629` →
  `MissionSelectNotOpen`), and the offline scan only stops at a state-4 row
  (`0x8A2330`), so later missions stay reachable. The same hook answers "not
  cleared" for a placeholder (one cleared before its resources went missing would
  otherwise be state 0 and selectable). A pack with placeholders is owned only
  while that hook is installed.
- **Identity.** The installer writes `Mods/Plugins/EDF6VehicleCrew.packs.ini`: per
  pack its tag, a definition digest (tag, every row's mission key, the shipped 4.1
  scripts' hashes), the row count and the placeholder mask. The plugin owns exactly
  the packs listed there. When the game publishes `DLC` for one of them, `Add` also
  publishes `AF_PACK = "1;<id>;<tag>;<digest>;<rows>;<mask>"` (EOS string; the structures'
  versions are the game's own call's, `o->version` / `o->data->version`). Before
  the native `JoinLobby` (EOS route) a guest reads `DLC` and `AF_PACK` and refuses,
  with a Chinese dialog naming the pack, when the pack is not installed here, the
  room has no identity (older host), the line does not match the id, the definition
  or row count differs, or a row the host can choose is a placeholder here (the
  host may switch missions after the guest is in). Rooms on 0..2 and on other
  mods' ids are untouched. A guest whose native ownership check already refuses one
  of our ids (pack not installed) gets the same dialog from the ownership hook.

`AF_PROFILE` and the SEARCH_TYPE prefix are **not** bumped: EDF6Coop admits only
`AF_PROFILE==1` and decodes only this prefix, so either change would close every
All Forces room until Coop is updated, while stock-content rooms between old and new
All Forces are still resource compatible. The pack rooms are separated instead by
the moved ids (an older client owns none of 65..71 and is refused by the native
ownership check; a newer client owns none of the old ids) and by the required
`AF_PACK`.

Logs: `ROOM publish: AF_PACK=...`, `ROOM join refused: reason=pack-not-installed |
pack-identity-missing | pack-identity-malformed | pack-identity-other-pack |
pack-definition-differs | pack-rows-missing-here | content-read-failed` with both
sides' tag/digest/rows, `PACK placeholders: content N (TAG), k of m rows locked`,
`CONFIG mission packs: N installed`.

**Coop's virtual / remembered rooms.** A synthetic lobby handle is Coop's; All Forces
cannot read its persisted attributes and logs `route=coop-virtual (the mission pack is
EDF6Coop's to check: EDF6AF_RoomPackCompatible)`. Coop persists string attributes
(`LobbyAttribute::text`), so `AF_PACK` survives REJOIN, and since
hajisensai/edf-coop-stable#36 it calls the All Forces export
`int EDF6AF_RoomPackCompatible(int64 dlc, const char* afPack)` with the persisted `DLC`
and `AF_PACK` (nullptr when absent) where it admits such a room (`CompatibleLocalRoom`:
REJOIN `startVirtualJoinRoom`, the virtual room's `hookVirtualJoin`, the full-lobby
fallback from `epicJoined`, and `addRememberedRoom`, the remembered room it adds to a
failed room search) and refuses on 0. An older Coop does not check.

**The refusal dialog, every route (2026-10-18).** The export refuses with the same
Chinese dialog as the EOS route (`mission_packs.h` `Explain`, `mod_room.cpp`
`ExplainPackRefusal`), and logs both sides' tag/digest/rows. The caller may be EOS's
tick thread (Coop answers EOS callbacks there), so no route shows the dialog on the
calling thread: `src/pack_notice.h` shows it on a thread of its own (`MessageBoxW`
without an owner, topmost) and returns at once. The same refusal (by content id,
reason, host digest and rows) is never shown twice at once; through the export it is
not shown again within a minute (Coop asks at every room search), while a player's own
EOS join shows it again at each try once the last one is closed. The ownership hook's
refusal at the lobby join (`0x8EC659`, a pack not installed here) goes through the same
notice. That is the only dialog there: on the not-owned branch of `0x8EC270` the game's
own message (`Lobby_WarnWeaponEquip`, "not purchased, you can play but not equip its
weapons", then the join anyway through the continuation `0x8E7060`, whose `_Do_call`
is `0x8F06D0`) needs the content's name from `0xE3F80`, which returns nullptr past id
2 (`0xE3FA4`); the branch then just returns (`0x8EC6C6` → `0x8EC7F6`). 「未持有此内容」
(`Online_NotHaveContents`) is no dialog: it is the room list's mission name for a
content id this machine has no list for (`0x8EB710`, referenced once at `0x8EB836`).
Static (H); not seen in the game. A refusal on the EOS route completes the game's join
with `EOS_InvalidParameters`, which the game answers with its own 「无法参加房间。」
(`OnlineError_RoomError`, `0x8EFEC0` for any result but 0 and 3): that one stays, after
ours, as it does for every other refusal of this file (static, M: the handler's wiring
to JoinLobby's callback was not traced).

Tests: `tests/mod_room_test.cpp` (`PackIdentity`: publish beside `DLC`, same
identity joins, every mismatch refused with no `JoinLobby` call and a dialog, the
host lacking more rows is fine, stock and other mods' rooms untouched, the export,
the wire format's bounds, the placeholder lock); `tests/edf5_campaign_native_test.cpp`
(ownership by the packs file and ini runs, older ids not owned, placeholder packs
gated on the select hook, the join dialog; in the private EDF.dll mapping the select
hook locks placeholders with and without `PortedMissionsUnlocked` and never reports
one cleared); `tests/test_edf5_campaign_lifecycle.py`
(`test_content_ids_do_not_depend_on_other_mods`,
`test_two_machines_agree_on_every_room_row`) and `tools/selftest.py`
`edf5_campaign_two_machines` (the real game: a machine with another mod's modes and
missing maps maps every online (id, row) to the same mission as a plain one).
None of this ran in the game or between two machines.

### Saves after the id and row change (2026-10-18, static)

What the game keys by the content id, and what by the row (EDF.dll `0x678CCB46`):

- **Content id: nothing persisted.** A mode's save is the file named by its ModeList
  member 5 (`DEFP_<MST>`: `E5M0` / `E5D1` / `E5D2` / `TR00` / `E41M` / `E4P1` /
  `E4P2`, unchanged); the save job reads and writes it at `GS+0x57A4`
  (`0x7C0C72` → `0x7BC310`, `0x7C3542` → `0x7BD0A0`, length `0x16C4`). `MAIN.GST`
  (`GS+0x6E68..0x14B50`: weapons by weapon-table row, armour, ...) and `COMMON.CFG`
  (`GS+0x318..0x57A4`) hold no mode id. Every mode-node `+0x74` read (36, listed by
  `tools/edfre.py`: `0xDCA47`, `0x708A02`, `0x70C32D`, `0x70CC47`, `0x74836E`,
  `0x787922`, `0x78BC48`, `0x78F27C`, `0x8A2114`, `0x8BEB79`, `0x8BEF3D`, `0x8C3D7F`,
  `0x8C4693/4CFF` (+ compares `0x8C46AC/4D1C`), `0x8C4CBC`, `0x8EBFC2`, `0x8ED9D9`,
  `0x8F649D`, `0x8F651A`, `0x8FD942`, `0x8FE3AC`, `0x900697/6DB/7CC/906`,
  `0x9017BF`, `0x904DE0`, `0x913BB2`, `0x9158A3`, `0x916577`, `0x91A64E/6CB`,
  `0x91DC4E`, `0x925CB4`, `0x92671E`, `0x926A30`, and the compares `0xD841F`,
  `0x789288`, `0x905F8B`) keeps it in a register, a stack local, a UI object or
  `GS+0x14FD0` (runtime, past the saved blocks; read once, `0x747F2B`, as a
  compare). None shifts it, tests a bit by it or indexes with it (the two candidate
  hits are a vector size `sar` at `0x78F293` and a mode-pointer index at `0x8C4D10`);
  `0x78F27C` hands it to the stream writer `0x12B5580`, which encodes any int.
  Ownership is the `std::set<int>` at `mgr+0xE0` (`0xD92B0`), not a bitmask. The
  weapon drop filter `0xD81E0` compares a weapon row's column 8 with the mode's id;
  the ported weapons write 0 there (`tools/ported_weapons.py` `PACK`). The
  achievements and clear ratio count content 0 only (`0xDC9E0`). So moving the
  packs from 3..9 to 65..71 loses no progress and attaches none to another mode.
- **Row: the save is keyed by it.** `clear[class][row]` (`0xDFA40` / `0xDFAB0`), the
  byte per row at `+0x14A0`, and the current row at `+0`. An older install that
  left a mission out had the rows after it one higher up; the placeholder rows put
  them back where the definition has them. The installer therefore records the list
  it wrote last, when that differs from the definition, as the pack's legacy layout
  (manifest `legacy`, kept by later installs and past an uninstall in
  `Mods/.edf5campaign-legacy.json`) and writes `Legacy=<old rows>:<each one's row
  now>` into `EDF6VehicleCrew.packs.ini`.

### Save layout markers (2026-10-18, after review)

Each pack's save is marked with the layout its rows are in, and every decision is made
from the save itself (its marker and what it holds), never from the install's history
alone (`src/mission_packs.h` `MigrateModeSave`):

- **The marker** is the rows' mission keys in order, hashed (`tools/make_edf5_campaign.py`
  `layout_id`, `packs.ini` `Layout`): not the definition digest, which also hashes the
  4.1 scripts, so a script's fix keeps every save's marker. It sits in row bytes 508..511
  (`GS+0x57A4+0x169C`), which no pack of ours reaches.
- **Older layouts.** `edf5campaign/pack_layouts.json` keeps every layout a release had,
  per pack, oldest first (now one each, 2026-10-18's); a test fails when a pack's
  definition has a layout not in it. The installer writes each older one with its map to
  the rows now (`Older=<marker>,...`, `From<marker>=<rows>:<map>`, at most 4 with
  `Legacy`): a save marked by any release moves straight to the rows now, however many
  releases it skipped. A save holding a row past its layout's map, or a marker without a
  map (another, e.g. newer, version's), is left exactly as it is (`other-layout`).
- **Unmarked saves** (from before the markers, or new): nothing in them, or nothing past
  `StableRows`, are marked as they are. `StableRows` is how many leading rows every
  older install had where they are now: rows of the first released layout whose script
  and everything it loads is in the game's own packages (row `stable`: `Root.cpk`,
  `Chunk01/02.cpk`; nothing `tools/edf5_dlc.py` adds), up to the first that is not.
  With this game: 110 of 110 for EDF5's story, 3 of 15 for its Mission Pack 1 (DM018,
  DM019, DM020, DM015 need `tools/edf5_dlc.py`'s parts: the four rows older installs
  left out), 14 of 14 for Mission Pack 2, 0 for the three EDF4.1 packs (their maps are
  converted at install: any row may have been left out), 1 of 1 for the test range.
  Past them only this machine's record of its older list (`Legacy`) can say which rows
  the save had, and only when the save fits it (nothing past its rows);
  otherwise (`layout-unknown`: an older version uninstalled before this one, so no
  record) the save is left unmarked and unmoved: the player is told once a session
  (the former「全军出击：任务包存档」popup, now only in the log) that clears past that row may be off. Nothing is guessed,
  so nothing wrong is made permanent; the game writes the next save with the rows now,
  still unmarked. Not covered: a save copied from another machine whose older list
  differed from this one's record (it is moved by this machine's record).
- **When it runs.** Only from the script's `UnlockDownloadContents` (`0x70FF87`,
  `airstrike.cpp` `ScriptUnlockDlcHook`): MAINSCRIPT calls it only after `SetMode` and a
  `LoadSaveData_ModeSelect` that succeeded (MAINSCRIPT.AS 363, 852, 1691, 1801, 1851,
  1889, 1913), so `GS+0x57A4` is the new mode's save and `GS+0x38` that mode. The other
  entry of `0xDC550`, the call at `0xDC348`, is the reset's (`0xDB580` bit 8 via
  `0xDB920` → `0xDC170`): it runs after bit 4 has put the template into `GS+0x57A4` and,
  inside `SetMode` (`0xDC4B8`), before `0xDC4C5` sets `GS+0x38`, so there the mode is the
  old one and the block a blank: marking it there (as the first version of this did)
  would give a new pack's first save the previous pack's marker. That entry only grants
  the call weapons now. A pack is owned only while the script's entry is hooked (an
  unmarked save played with the rows now could not be told from an older install's).
- **The marker survives the game's save cycle** (`tests/mission_pack_save_native_test.cpp`,
  real EDF.dll code in a private mapping, only the CRT imports resolved): the write job
  (`0x7BD0A0` → job `0x7BD910`, run by `0x7C1CB0`) writes a 0x14-byte header (`MDB0`,
  version 3, length, checksum `0x7C1270` over the live block) and then `GS+0x57A4`
  whole, 0x16C4 bytes; the read job (`0x7BC310` → `0x7BD720`, run by `0x7C1670`) takes
  only a file of exactly that length with matching magic, version and checksum; the
  load's apply `0x7BE970` (from the save UI's completion `0x945680` at `0x945741`)
  copies the whole block over `GS+0x57A4`, or nothing on any failure; `0xDBCE0` after
  it only copies the current row and difficulty out. The reset (`0xDF790`'s template,
  `0xDBA00`, inline in `0xDB580` bit 4) zeroes the block, marker included, at every
  `SetMode` and the mode select's `Reset`, and the next load brings the file's back;
  a mode without a file keeps the zeroed template (a new save, then marked). The only
  writer of the row bytes is `0xDFA90` (`0x70DB63`, the current row); none clears rows
  past the list's. Run: reset, the game's own writers over rows 0..507, checksum,
  apply, two rounds of load → save → load, a failed and a short read: 32 checks. Not
  run: the file I/O itself (`0x7B9990` / `0x7B8DE0`, platform calls; their callers'
  bytes are checked), whether the file has another layer of encryption (the header and
  checksum are over the plain block), and a save job queued before a `SetMode` and run
  after it (it would write the new mode's block under the old mode's file name; on the
  path at MAINSCRIPT.AS 345 a `WaitSaveLoad` comes first, on 799..842 none was found).
