# Authoritative NPC map commands

Map commands use the existing authenticated Coop extension transport and the
single `support_net.cpp` poll. There is no second consumer, socket, player slot
fabrication, or client-side write to host NPC state. Command messages have a
separate `NCMD` magic, version 1, and explicit 760-byte little-endian encoding.
They fit the existing 1024-byte extension ABI; Coop changes are not required.

The command capability is negotiated with the host inside the already accepted
support mission epoch. A host without the new command executor never advertises
the capability; an older host simply leaves the new command feature unavailable.
The support v2 protocol remains compatible. Future incompatible command changes
must bump the command version, independently of the support spawn wire format.

Each request carries the current player's canonical native object ID, one to
sixteen distinct squad IDs, an order and finite world point, and an explicit
enemy native ID for focus. Counts above sixteen are rejected, never truncated.
No resource paths, object addresses, player indexes or caller-selected PUID are
accepted on the wire. The existing native all-team walker resolves the live
objects in one pass, including their current weak identities. Requester must be
a genuine current-mission player. Its native User PUID must equal the transport's
authenticated sender and appear in the sealed world participant set.

Only the host invokes the configured `NpcSquadCommandForRequester` executor. It
receives the actual requester ObjRef, not `PlayerHuman()` on the host. That
executor revalidates authority, script control, faction and squad ownership at
the mutation point. Following/recruiting another player's squad is refused;
legal free squads remain usable. Focus targets belong to the command, not the
host's global mark. Non-soldier remote units return unsupported instead of
pretending to run a vehicle command.

Request numbers are monotonic per client and mission. Host caches the latest
request and result per authenticated peer/PUID. Identical retries return the
cached result; a reused number with different content, an older number, another
sender, or a previous mission epoch cannot execute the order again. The cache is
bounded at 1024 peers. A 200 ms sender limit prevents command spam. Requests are
retried once a second while awaiting the result, and reach an explicit 10-second
acknowledgement deadline. A timeout means the execution result was not received,
not proof that the order never ran. Late results cannot reopen that deadline.

`SubmitMapCommand` returning an ID means queued, not executed. The map polls
`ReadMapCommandNetworkResult` and correlates that ID before displaying the
per-unit native reason and affected count. Partial acceptance remains partial.
For boarding, accepted means real walking/seat assignments were made, not that
every soldier is already seated. Session suspension interrupts pending commands
without replaying them in the next world.

## Integration and checks

`InstallNpcAi` registers the executor once through `ConfigureNpcCommandNetwork`,
before the map is opened. `support_net` supplies the current authenticated
context, routes packets, and resets command state with the mission. The UI passes
its actual viewport player, selection ObjRefs and explicit focus target.

`command_protocol_test` covers encoding bounds, epoch/sender/capability checks,
at-most-once execution, changed-content replay, partial results, rate limits,
result loss/retry and deadlines. `command_identity_test` executes the production
resolver and ID reader against fixture objects and the real EDF.dll private
all-team walker with no DllMain. `support_net_runtime_test` verifies the shared
poll dispatch. Native command semantics are checked against the production NPC
executor separately and through the command dispatch regression.

No running game, game installation write or two-machine gameplay test is part of
these checks. Real movement, boarding animations and remote convergence remain
gameplay acceptance boundaries.
