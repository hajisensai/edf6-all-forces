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

Any validation, send, create or later deployment failure cancels the entire
transaction. `ReportSupportFailure` also covers a real crew's boarding/ingress
failure after creation. Cancellation retries until all peers acknowledge; a
temporary full reliable queue cannot leave one peer's objects alive. Native object
deletion must run on every peer: registration itself does not replicate creation
or arbitrary plugin deletion. The adapter must retain ObjRef identities for
rollback and must not delete a newly allocated object at an old address.

Mission challenges are process-random and echoed by the authenticated host.
Monotonic per-process mission/host-era counters reject older handshakes. A change
in the transport generation, readiness, mission, host or roster destroys the
previous support ledger. The host changes the shared epoch when a member enters a
new mission; periodic challenge handshakes recover a temporarily failed welcome
enqueue. Old-epoch packets and committed/cancelled replays cannot recreate units.
Pending transactions time out after 20 seconds. IDs use the native derivation with
monotonic ordinals in `[0x40000000,0x80000000)`; ordinals are never reset across
mission/roster changes and fail closed at exhaustion. Native hash space remains
the game's own finite identity space, not a mathematical collision guarantee.

The host-internal `SubmitPreparedSupportPlan` handles existing mission vehicles
through the same protocol. It accepts reserved catalog 1023 and exactly one
`kExistingVehicle` unit carrying an already registered canonical ID. Adapters
resolve this identity locally and never create or destroy that existing vehicle;
only the attached new human units are spawned/rolled back. Clients cannot request
this reserved catalog. An active migration for the same vehicle is deduplicated.
To replace a dead/failed crew, report failure for its previous transaction first;
the cancelled record permits a fresh transaction with new derived human IDs.

The v1 limits are 1024 remote peers, 16 units per deployment, and 128 transactions
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
ordering. Its v1 readiness requires the complete current Epic room roster to
match authenticated direct peers with the same extension profile. Virtual rejoin
and rooms with direct-only members outside that roster remain unavailable rather
than spawning local-only support. No game process, game installation writes,
injection, real-game physics, native movement convergence or two-machine gameplay
test was performed for this implementation. Native ID creation/registration and
crew/entry adapters have their own private-image tests; these do not establish a
live multiplayer acceptance result.
