# Current-world participants and pre-creation admission

Supported EDF.dll `678CCB46`. No game or installation changes were performed.

## Actor roster, not lobby membership

`5E0C80(TeamManager*, EnumCallback*)` is the native all-team enumerator. The manager
is `*(EDF+20B2978)`. It locks the manager at `+8`, walks the seven team records from
`manager+38` with stride `38h` until offset `188h`, and invokes callback slot 1 with
each node's `+20` GameObject. No team relationship, HP, or dead-state filter is applied.
This differs from `5E11D0`, which walks only friendly teams, and `775950`, which finds
a mission-index player only in team 0.

The callback in `mission_participants.cpp` recognizes the four existing Soldier classes
and collects the bound `Soldier+1ED0` User's EOS PUID at `User+18` when `User+48>=0`.
The User shared control block at Soldier `+1ED8` must be alive. Scene-deleted actors
(`+18 bit2`) are excluded; **dead/revivable players (`+2E8`) are included**.
The result counts actors. Split-screen actors may share a PUID; the transport de-duplicates
PUIDs only after deciding whether the actor snapshot is complete. Duplicate actor visits
are ignored, duplicate mission indices with different actors invalidate the snapshot.

`ReadMissionParticipants` returns the actual actor count and the native mission-start
expected count at `*(EDF+20B2890)+14FF8`. Capacity exhaustion, unreadable bound User,
or a manager/status/count change during traversal fails with both counts cleared.
An actor whose index exceeds the expected range invalidates the entire snapshot. Together
with unique mission indices, a matching actor count then proves coverage of every index
from zero through expected count minus one; indices such as `{0,9}` cannot seal a two-player world.

No pointer/address serves as an epoch. The support transport must reset its frozen cohort
from the genuine MissionStart/ResetScene lifecycle and allocate its own epoch. Manager
addresses may be reused. A user joining only the lobby creates no Soldier in these trees
and therefore does not alter the current-world cohort.

## Safe admission before creation

`591130(int missionIndex,matrix*,int flags)` looks up the session User by mission index,
calls `5A3F90`, and binds User to the created Soldier at `591254`.
Returning null from this function globally is unsafe:

- Caller `1DC525` checks null at `1DC544` and has a supported empty-output branch.
- Caller `22AC2F` immediately reads `[rax+30]` at `22AC3F` without a null check.
- Replacing `5A3F90` with null is also unsafe: `591254` unconditionally writes the result.

The second path is protected one level above: `22AB90` has exactly one direct caller,
`22B626`. It takes an explicit weak-result buffer in RDX and returns that buffer address
at `22B190`. Its caller checks `out.ctrl` at `22B637`, then the locked pointer at `22B694`.
`mission_participant_gate.cpp` wraps **this call**, preserves all ten Win64 arguments for
admitted users, and rejects by zeroing the output weak pair and returning its non-null
address. No Soldier is constructed, no reference is invented, and no destructor is needed
for that empty result.

`InstallMissionParticipantGate(bool(*allowed)(int) noexcept)` receives the shared transport
admission policy. Pre-seal setup must be allowed. Once the world roster is sealed, the
policy resolves a session User by mission index and compares its PUID with the frozen
actual-world set. Looking up that User before creation may use session metadata; it must
not turn the dynamic lobby roster into the quorum. This module deliberately does not own
the transport cache, EOS resolver, or its reset semantics.

`MissionParticipantGateReady()` confirms only the unsafe caller's upper guard. The
cooperating module separately protects the safe `1DC525` path. Online support must not
advertise full admission readiness until **both** guards and the resolver are available.

## Executed evidence

- `mission_participants_test`: production visitor, 10 checks covering dead/other-team
  actors, split-screen duplicate PUIDs, lobby/unbound NPC exclusion, invalid references,
  duplicate indices, overflow and output clearing.
- `mission_participant_gate_test`: production installer and actual redirected near thunk,
  12 checks including denial before native allocation, a valid weak output address,
  successful forwarding of all ten ABI arguments, and unchanged offline behavior.
- `mission_participants_native_audit.py <EDF.dll>`: real native all-seven-team enumerator
  (mutex operations alone stubbed) and exact native weak-output consumer block under
  an ABI adapter. Empty weak and successful strong-lock paths both pass.

These are private fixtures, not a real EOS room, complete player constructor, scene
transition, death/revival game session or live multiplayer validation.
