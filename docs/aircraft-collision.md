# Aircraft collision matching (2026-10-06)

The reported missing wing collision had two independent causes. NPC variants used
`fuselage_box`, excluding the wings on purpose; player/parked variants used a whole
model box but still retained `RAGDOLL_V506_HELI.SHKT`. Moving that helicopter
ragdoll's centre did not make it the new aircraft's shape.

There are two real Havok consumers, not a plugin-only raycast fix:

- `HelicopterBase` creates the moving rigid body in `EDF+0x656E90` (calls at
  `0x64E9B6`, `0x650AA6`). Its box comes from `heli_rigid_body`; `0x65713C` calls
  the box factory `0x11A63F0`.
- VehicleBase's animation ragdoll supplies character/projectile contacts and the
  crash parts. Its part records are `vehicle+0x1398`, count `+0x13A8`, stride
  `0xC0`, body wrapper `+0x50` (`0x650119`, `0x6E82F3`). `0x11B15E0` returns
  the wrapper's native body shape. The derived 506 vtable is assigned only at
  `0x61B5B2`, after its base constructor has already created the flight body.

`pylib/aircraft_collision.py` clips the rendered model's triangles against a
24 by 32 X/Z grid and makes a convex box from each occupied cell's actual bounds.
The result preserves thin wings and empty space beside the nose/tail. This is
a conservative compound approximation, not a triangle-perfect collision mesh.
The maximum horizontal error is one cell (model width/24, length/32), and a
zero-thickness surface gets a 3 cm skin. Gear is included in its extended bind
pose, so collision does not follow gear retraction or animated elevons.

Every shape's coordinates are relative to the full model's existing measured
centre; the SGO frame and forward/inverse ragdoll bindings use that same centre.
NPC, parked and requested copies share exactly the same resource. Seven generated
SHKTs cover the grounded bomber, interceptor, multirole, carrier, drone and the
two unmodified airstrike bomber models. Primer creatures/fighter and the submarine
carrier keep their separate shapes. A legacy direct caller requesting the raw
stock bomber without the generated model keeps the stock path; the standalone
test-range installer now supplies the generated model.

The body compound replaces only the `RagDollProxys.body` shape. Other required
proxy names and constraints remain, with tiny shapes inside the main compound,
so helicopter rotor collision no longer sticks out beyond the plane. Each convex
keeps the donor box's valid face/edge topology and rewrites vertices/planes. The
compound's shape-key width, instance count, AABB, bounding radius and four-way BVH
are rebuilt. Old geometry's cached shape mass properties are cleared.

`src/body506.cpp` intercepts that one box creation call and retains the generated
ragdoll compound for the moving body too. It checks the call/function bytes, the
construction/final vehicle vtable, aircraft mark, part array bounds, compound
vtable and `hknpShape::userData = 0x4544463641495231` (`EDF6AIR1`). Native userData
is at `+0x28`, verified in the shape copy constructor `0xD89340`; the compact
SHKT field is at `+12`. Reference ownership matches the factory's output handle.
Old resources, unsupported shapes or signature mismatch retain the stock box.

Runtime evidence to check:

- Hook installation: `HOOK body506 ... airframe=1`.
- Actual use, per aircraft creation: `BODY506 airframe compound: ... mark=...`.
- Compatibility fallback: `BODY506 airframe fallback: ... (no tagged compound; stock box retained)`.
- Generated resources: `EDF6VC_AIRFRAME_BOMBER.SHKT`,
  `EDF6VC_INTERCEPTOR_AIRFRAME.SHKT`, `EDF6VC_MULTIROLE_AIRFRAME.SHKT`,
  `EDF6VC_CARRIER_AIRFRAME.SHKT`, `EDF6VC_DRONE_AIRFRAME.SHKT`,
  `EDF6VC_AIRFRAME_BOMBER401.SHKT`, `EDF6VC_AIRFRAME_BOMBER501_2.SHKT`.

Validation performed before game testing:

- `python tools/aircraft_collision_check.py`: synthetic long-triangle coverage,
  empty space under a wing, and multi-level BVH tests.
- `python tools/aircraft_collision_check.py --game`: reads Root.cpk, rebuilds and
  re-reads all seven SHKTs (338–466 convex cells each), verifies every BVH leaf,
  hull plane/topology/index/bounds and 22,026 model vertex/triangle-centre samples;
  builds all jet SGOs and verifies resource inclusion and NPC/player parity.
- `python tools/selftest.py`: 104/104 passed; release DLL compilation passed.

These checks do not execute Havok's deserializer or solver. Live loading, walking
on both outer wings, projectile contact, wing/building contact in flight, carrier
drone launch/docking, crash behavior and frame cost still require game testing.
