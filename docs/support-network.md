# Map support replication

`support_net.cpp` loads the versioned `EDF6CoopGetExtensionApi` export from an
already loaded `EDF6Coop.dll`. There is no second UDP socket and no game identity
fabrication. The transport's authenticated peer identity, room owner, roster and
generation are separate from the support message payload. Native create, register
and delete callbacks run only from the game-thread `SupportNetTick`.

The integration owner registers `support_net::Hooks` once through
`ConfigureSupportNet`; calls `ResetSupportNet` at mission reset; and calls
`SupportNetTick` every game frame, including while the map is closed. The entry
planner supplies the fixed catalog resource IDs, target, and every aircraft,
ground vehicle and real human's matrix. It may return pending while incremental
navigation runs. `deriveId` wraps `DeriveSupportSoldierNetId` with a live registered
host player anchor; it must not generate an ID from a player slot or raw address.
The same hooks provide `ReadMissionParticipants` and
`MissionParticipantGateReady`. The latter must be true only after the native
upper creation gate has installed; Coop independently requires its safe creation
call-site gate before exposing a usable support transport.

Online UI requests contain only a catalog ID and finite target. The host checks
the current room, requester membership, request watermark, rate and outstanding
deployment, then runs the environment/resource planner. All peers receive the
host's full plan as a begin message, at most 16 unit messages and prepare. Every
unit message is 176 explicitly serialized little-endian bytes. Peers validate the
whole plan before acknowledging; only the host can commit it. Each peer then calls
the same native create/register adapter with exactly the same canonical 32-byte
IDs. Host-only AI, steering, native seating and damage authority remain the native
adapter's responsibility. The plan's `role` names the parent vehicle unit by index
plus one; it never carries a pointer or resource path.

Request outcomes have their own authenticated request-number channel, independent
of actor transaction IDs. Accepted, active, refused, timeout, cancelled and
interrupted are bounded enum values; there are no peer-supplied UI strings. A
host refusal before any begin/spawn still returns a terminal outcome. The notice
hook updates only this machine's latest request, so remote work does not replace
the host's own request status. Duplicate replies, non-host replies, old epochs and
an older transaction's cancellation cannot finish a newer request. Host planning
and confirmation deadlines produce timeout replies; a real transport suspension
produces interrupted rather than leaving the UI waiting indefinitely.

Native side effects remain held until `SupportTransactionActive(token)` is true.
The host sets this only after every peer's successful spawn/register result; it
then sends a separate reliable activate message, retried until acknowledged.
Clients become active only on that authenticated message, never merely because
their own creation succeeded. This closes the cross-channel race where native
ride/follow events arrived before another peer knew their object IDs.

Any validation, send, create or later deployment failure cancels the entire
transaction. `ReportSupportFailure` also covers a real crew's boarding/ingress
failure after creation. Cancellation retries until all peers acknowledge; a
temporary full reliable queue cannot leave one peer's objects alive. Native object
deletion must run on every peer: registration itself does not replicate creation
or arbitrary plugin deletion. The adapter must retain ObjRef identities for
rollback and must not delete a newly allocated object at an old address.

Mission challenges are process-random and echoed by the authenticated host.
Monotonic per-process mission/host-era counters reject older handshakes. A change
in transport generation/readiness suspends new work and retains already created
actors and their ledger. It must not delete an occupied or delivered vehicle.
Only an explicit mission reset owns whole-ledger teardown. A changed peer mission
challenge also suspends an established actor set instead of silently replacing
it. Periodic challenge handshakes recover a temporarily failed welcome
enqueue. Old-epoch packets and committed/cancelled replays cannot recreate units.
Host terrain planning has a 120-second deadline; network prepare/commit phases
time out after 20 seconds. IDs use the native derivation with
monotonic ordinals in `[0x40000000,0x80000000)`; ordinals are never reset across
mission/roster changes and fail closed at exhaustion. Native hash space remains
the game's own finite identity space, not a mathematical collision guarantee.
Callback tokens are separate process-monotonic 64-bit identities and never reset
with the wire epoch; a late failure callback cannot cancel a new transaction that
reuses wire slot 1. Both dispatchers retain tokens as keys, not array indexes.

`EDF6AF_GetMissionParticipants` publishes an atomic frozen current-world PUID
set and local world epoch. It is sealed only after the native all-team visitor
finds every expected player Soldier (including dead players); split-screen PUIDs
are deduplicated only after that completeness check. The lifetime must also have
observed player creation begin, so preload cannot seal the previous world actors.
Every index must additionally match the exact `(object, weak-control)` identity
reported by the successful native creation wrappers in this epoch. An attempted
creation or a leftover object at an old address is insufficient. The Coop wrapper
notifies `EDF6AF_MissionPlayerCreated`; AF's upper wrapper uses
`NoteSupportMissionPlayerCreated`. `MissionParticipantCreationsMatch` checks the
final complete all-team enumeration against these records.
A new lobby user is not a
world player. Coop validates and routes only this frozen subset, so a lobby-only
join or leave does not change the support generation or ACK quorum. A genuine
participant link/identity/host failure still suspends new requests and retains
existing actors until explicit mission reset; it does not erase occupied hulls.

Admission is checked before native player allocation. Coop guards the verified
null-safe call at `1DC525`; AF guards the `22B626` call to the upper weak-result
constructor. Both call the same `SupportMissionPlayerAllowed` policy: before
sealing, ordinary initial player creation proceeds; after sealing, the dynamic
User roster resolves only the requested mission index's PUID, which must be in
the frozen actual-world set. On a sealed machine, a new lobby member's actor is
refused for this mission, while existing players may respawn. Neither `591130` nor `5A3F90` is
globally made to return null, since another original caller dereferences that
result. A fresh joiner's own first world is not sealed yet; these local guards
alone do not prove that the joiner cannot load the host's already running world.
That requires a verified upper mission-entry rule or trusted host world policy
before entry. No mid-world actor/seat catchup is claimed or substituted with
invisible local-only actors.

`EDF6AF_GetMissionAdmissionState` exposes lifecycle independently of extension
readiness. It directly reads the verified `Network_SetLocation` state, available
in menus before dispatcher preload: MENU_LOBBY(2)/MENU_ROOM(3) mean Lobby,
GAME_LOADING(5) means Loading, GAME_PLAYING(4) becomes Sealed only after matching
created actors. Unreadable/boot state is Unknown. Actual Root `MAINSCRIPT.AS`
sets MENU_ROOM only after Mission returns, FreeGroup(Scene) and Network_Session_End.
The helper checks both native setter signatures and reads the aligned state only
while its lock is clear, without modifying the game. Missing actors or a false
readiness bit are never interpreted as a lobby. Consumers keep fresh joiners
waiting when this lifecycle state is unknown or unverified.

The host-internal `SubmitPreparedSupportPlan` handles existing mission vehicles
through the same protocol. It accepts reserved catalog 1023 and exactly one
`kExistingVehicle` unit carrying an already registered canonical ID. Adapters
resolve this identity locally and never create or destroy that existing vehicle;
only the attached new human units are spawned/rolled back. Clients cannot request
this reserved catalog. An active migration for the same vehicle is deduplicated.
To replace a dead/failed crew, report failure for its previous transaction first;
the cancelled record permits a fresh transaction with new derived human IDs.

The protocol v2 limits are 1024 remote peers, 16 units per deployment, and 128 transactions
per mission epoch. Cancelled records remain tombstones; they are not evicted to
make room for replayed IDs. Player requests are limited to one per two seconds,
and only one transaction plans or commits at a time. Existing live support may
remain active while the next request deploys.

## Validation and limits

`support_protocol_test` uses the production serializer/state machine for three
independent peers and tests matching IDs, 16-unit requests, unauthorized senders,
replay, stale mission handshakes, incomplete plans, missing resources, incremental
planning, native partial failure, queue failures, cancellation retransmission and
existing-vehicle migration. `support_net_runtime_test` executes the production
dynamic-ABI bridge against a local recording provider, including generation races
and teardown. The main DLL is compiled with MSVC `/W4 /WX`.

The companion transport additionally tests real localhost UDP reliability and
ordering. Its readiness requires every frozen current-world participant to match
authenticated direct peers with the `af-support/2` profile. Lobby-only users are
excluded. Virtual rejoin and participants whose identity/profile cannot be
verified remain unavailable rather than spawning local-only support. No game process, game installation writes,
injection, real-game physics, native movement convergence or two-machine gameplay
test was performed for this implementation. Native ID creation/registration and
crew/entry adapters have their own private-image tests; these do not establish a
live multiplayer acceptance result.
