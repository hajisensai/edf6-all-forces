# Real support soldiers: native contract

Supported EDF.dll TimeDateStamp `678CCB46`. Addresses below are RVAs. This work never
starts the game or changes the installation; live constructor/AI/co-op remain unverified.

## Resources and creation

The installed Root.cpk contains `OBJECT/N601_COMMON_RANGER_AF.SGO` and its `_LEADER`
variant. Both declare `AssultSoldier`, load `app:/weapon/AiSoldierRifle01.sgo`, use
`EDF6ArmySoldier.cas` and the Ranger model, and contain the stock weapon slots and
soldier AI configuration. They need no generated model, player identity or Dummy rider.
Their native resource loaders recursively preload those dependencies.

The production API deliberately exposes only these two canonical resources. During the
mission's player-preload phase, call `PreloadSupportSoldiers()` before `WaitPreload`.
After loading, callers provide one validated world matrix per actual soldier; this module
never chooses a point beside the player or adjusts arbitrary terrain heights.

`CreateFriend 1B0310 → 1D8900 → 1D9C20` establishes the minimal native contract:

- `11945E0(objectManager, matrix, path, InitParamBase)` creates the scene-owned object.
  InitParamBase is aligned 16, size `30h`, vtable `1762068`, remaining bytes zero.
  **The matrix itself must also be 16-byte aligned.** InitParam's alignment does not
  align the separate pointer. Spawn copies the caller's 16 floats into an aligned local
  matrix before crossing this native boundary; wire/Plan layouts are unchanged.
- `54EE70(object,2,true)` registers the friendly team; `54E740(object,1.0)` uses the
  stock mission difficulty adjustment. No direct HP or weapon-parameter writes.
- `CreateFriend` writes `object+540` for recruitment. Its `RideAi` call only applies
  after dynamic-casting to a vehicle, so **never call RideAi on the soldier**.
- `CreateFriendSquad 1B08D0` creates the leader and followers, then calls
  `54EC50(follower,leader,false)`. Our four-person squads use this same follow tree.
  Platoons contain three such independent squads; the caller orders their leaders.
- The module retains one weak count at `object+30 → +C`, not an extra strong count.
  Creation/setup/registration failure deletes completed objects with `118A1B0` and
  releases retained weak references. Squad failure rolls back all members.
  Mission reset releases weak records only; scene teardown owns old object deletion.

## Network contract

CreateObject does not transmit creation. Direct `SpawnSupportSoldier/Squad` therefore
rejects online use. The authenticated reliable spawn receiver invokes
`ApplySupportSoldierSpawn(matrix,leader,NativeNetId32,out)` on **every peer**. It owns
epoch validation, canonical resource validation, authorization and duplicate suppression.
Passing null ID is supported offline, including a single leader resource.

Every successfully created soldier starts with its owned registry `held=true` before
the function returns. The NPC Think hook must consume `SupportSoldierHeld` to suppress
stock AI and clear input while retaining normal physics. Only an authenticated transaction's
Active step may call `HoldSupportSoldier(identity,false)` before follow/boarding commands.
The registry rejects arbitrary mission NPCs, dead/expired/reused identities and old scenes;
mission reset removes all holds. Offline callers also explicitly activate their new soldiers.

`DeriveSupportSoldierNetId` reads the registered anchor's `+130` network control block,
then its `+8` entry, and calls `776790(out32,entry,ordinal)`:

- Native output is **32 bytes**, not 16. It has parent hash at `+0`, full unsigned
  32-bit ordinal at `+4`, type 5 at `+C`, and the native computed hash at `+18`.
- Ordinals do not truncate to 16 bits. The network owner must reserve and allocate
  unique ordinals for each anchor. Native parent hashing is finite, not collision-proof.
- Bytes `+14..+17` are padding: neither `776790` nor `774BD0` initializes/copies them.
  Derivation zeroes the output first. Wire IDs must canonicalize this padding to zero;
  compare the first 20 and final 8 bytes against a native record, never all 32 bytes.

`RegisterSupportObject` uses generic `781950(networkManager,weakArgument,id32)`, which
extracts the object's NetworkObject through `22FCA0`, writes the native ID with `774BD0`,
and selects `remoteOwned = !IsHost` before `782720`. It is not vehicle-specific.
It consumes exactly one input weak reference; the wrapper increments that reference
first. The returned pointer is the registered entry's ID at `entry+8`. The wrapper
checks that ID, native ownership flags (host 2/client 1), and network entry presence.
Already registered objects and IDs of types other than 5 are rejected.

Soldier Apply registers once internally; aircraft/ground support transactions may call
the same generic registration wrapper. On clients the original remote ownership suppresses
local AI authority. Follow relationships are applied on each peer after all members exist.
Deletion must be part of the reliable support transaction too: `118A1B0` is native
SceneObject teardown, not a request to create/delete matching objects on other peers.

## Evidence and boundaries

`support_soldier_test` executes production orchestration against recording native
services: scene lifetime, weak counts, exact resources/InitParam, team/level, squads,
batch rollback, wrong-class rejection, transform validation, online direct-spawn rejection,
host/client registered Apply, and registration failure.

`support_soldier_native_audit.py <EDF.dll>` executes the unmodified native ID derivation
functions and the native registration wrapper in a private `DONT_RESOLVE_DLL_REFERENCES`
mapping. Registration's manager/interface/ID-store/logging services are recording stubs;
native host selection, reference ownership and returned ID address are exercised. It also
reads the actual Root templates and dependency files. Missing DLL/Root/platform is skip 77.
This does **not** execute the complete Soldier constructor, physics, weapon rendering,
resource loader completion, or a real two-machine session. A constructor SEH fault disables
further spawning for this process; a partial engine object not returned by a faulting
constructor cannot be recovered by this wrapper and remains a runtime verification risk.

## Observed creation fault: unaligned Plan matrix

The supplied session logged module installation (`SUPPORT soldiers=1`) at 20:54:33,
then the first soldier creation exception at 20:54:58. The installation log is not a
successful creation or completed resource preload. The old log omitted stage/address,
so it cannot alone distinguish a constructor fault from a later team/level failure.

The deterministic ABI defect is independently reproduced against the installed EDF.dll:

- `support_net::Unit.matrix` follows two uint32 fields (offset 8) and is only float-aligned.
  In an aligned `Plan`, `units` starts at offset 20, so every matrix has address mod16 4 or 12.
- `1194854` assigns the incoming pointer verbatim to `InitParam+8`; the remaining
  `1194858..119486A` instructions fill resource, SGO and temporary shared-output pointers.
- Ranger factory `54FE50 → 54FF10 → 58D020 → 56A5C0 → 545670 → 1189C00` reaches the
  common SceneObject constructor. `1189CAC` loads `InitParam+8`; `1189CB0/CB7/CBF/CCA`
  load the four matrix rows using **MOVAPS**, which faults on the unaligned source.
- The four-argument CreateFn and 0x30 InitParamBase match the original CreateFriend path.
  SetTeam uses RCX/object, EDX/team and R8B/children; SetLevel uses RCX/object and XMM1/float,
  also matching `1D8A6D` and `1D8A99`. Expanding InitParam or inventing a new constructor
  signature would not fix the matrix alignment contract.

`support_soldier_create_native_test` uses the real Plan layout and exact native
CreateObject parameter-wiring instructions, then executes the original SceneObject
constructor from its entry through all four matrix loads. Only after those loads does
the private mapping jump to the original epilogue, bypassing resource/ownership/world
initialization. The real `52710` initializer and matrix instructions are not mocked.
A vectored handler records only the exact expected native `1189CB0` access violation
and resumes at that epilogue for the negative controls.

All 16 old Plan matrices generate that actual MOVAPS fault. The production
`ApplySupportSoldierSpawn` aligned-copy path generates none and preserves all 16 floats,
for both ordinary and leader resources: **153 checks pass**. The fixture intentionally
returns null after this boundary and never fabricates a successfully constructed Soldier.
This is not complete world/weapon/AI initialization or a live-game retest.

Fault logging now records create/identity/team/level/recruit/network stage, exception code,
native offset, access address, source alignment and returned object. Post-construction
faults are setup failures rather than mislabeled creation failures. Fail-closed behavior
remains: `119486E` increments the manager's construction depth before the factory call,
and `119487D` decrements only after normal return. An old constructor exception may leave
native partial state; clearing the disabled flag and retrying in that process is not safe.
