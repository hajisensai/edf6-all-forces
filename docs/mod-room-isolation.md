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
  publishes `AF_PACK = "1;<id>;<tag>;<digest>;<rows>;<mask>"` (EOS string). Before
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

**Coop's virtual / remembered rooms are not pack-checked yet.** A synthetic lobby
handle is Coop's; All Forces cannot read its persisted attributes and logs
`route=coop-virtual (mission pack identity not checked on this route)`. Coop already
persists string attributes (`LobbyAttribute::text`), so `AF_PACK` survives REJOIN.
What Coop needs to do (not done here): where it admits a remembered/virtual room
(`CompatibleLocalRoom` / `EDF6Coop_AllForcesVirtualRoom`), call the All Forces
export `int EDF6AF_RoomPackCompatible(int64 dlc, const char* afPack)` with the
persisted `DLC` and `AF_PACK` (nullptr when absent) and refuse on 0.

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
