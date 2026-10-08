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
