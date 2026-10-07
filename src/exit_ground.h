// A soldier put down off a vehicle: on the floor under them, not in it (the user, 2026-10-07: "after getting out you
// can even fall under the ground, maybe because it stood tilted on uneven ground").
//
// The stock get-off (docs/boarding-re.md, 0x5701B0 from the seat's kick 0x62E1A0) puts the rider on the seat's riding
// point turned with the whole vehicle (its roll and pitch too), then two rays: one from the vehicle's body toward the
// point (a hit cuts it short) and one 1 m straight up from the point (a hit puts it 1 m under that). Nothing looks
// down for the floor: the plugin vehicles' points are at their collision box's bottom (pylib/vcobjects.py door_point),
// so a vehicle rolled a few degrees toward its door, or a door side on higher ground, puts the point under the surface;
// the upward ray then takes the floor's underside and puts the rider a further metre under it, and they fall through.
// The sidecar's step-off (sidecar.cpp StepOff) had no ray at all.
//
// First preserve any floor below the exit point: a nearer floor above may be a bridge or a ceiling. Only with no
// floor below, use the floor nearest the soldier's own height (heli.cpp MapGroundNear), and lift onto it
// when they are more than kSunk and no more than kMostLift under it (deeper is another floor: the deck of a bridge the
// vehicle stood under, a roof; a get-off point is no more than a few metres off its vehicle's bottom).
// Pure arithmetic (no game state): the decision is LiftOnto; crew.cpp ExitGroundTick and sidecar.cpp StepOff find the
// floor and warp.
#pragma once

namespace crew {
namespace exitground {
constexpr float kSunk=0.15f;   // m under the floor that is in it (feet a few cm into a slope stand on it)
constexpr float kLift=0.05f;   // m over the floor they are put
constexpr float kMostLift=4.0f;
constexpr float kNoFloor=-1e9f;

// The height to put a soldier at `y` (their feet) with the floor at `floor` (kNoFloor: none found): true with `*to`
// when they are in it.
constexpr bool LiftOnto(float y,float floor,float* to) noexcept {
    if(floor==kNoFloor || !(y<floor-kSunk) || floor-y>kMostLift)return false;
    *to=floor+kLift;
    return true;
}

// A nearer surface overhead is not evidence of penetration. Query from the feet down before accepting an upward
// correction. This intentionally leaves ambiguous multi-level penetration to the native movement code rather than
// teleporting a legal exit through a ceiling. `ray` is MapFloorRay and `findNear` is MapGroundNear.
template<class Ray,class Near>
bool Correct(const float* at,Ray ray,Near findNear,float* to) noexcept {
    const float below[3]={at[0],at[1]-4000.0f,at[2]};
    float hit[3];
    if(ray(at,below,hit)>=0.0f)return false;
    float floor=kNoFloor;
    return findNear(at[0],at[2],at[1],&floor,false) && LiftOnto(at[1],floor,to);
}
static_assert([] { float t=0.0f; return !LiftOnto(10.0f,10.1f,&t) && !LiftOnto(10.0f,kNoFloor,&t); }(),
              "a few cm into the slope, or no floor: left where they are");
static_assert([] { float t=0.0f; return !LiftOnto(0.0f,10.0f,&t); }(),"a floor 10 m over them is a deck or a roof, not theirs");
static_assert([] { float t=0.0f; return LiftOnto(9.0f,10.0f,&t) && t==10.0f+kLift; }(),"a metre under: put on it");
}  // namespace exitground
}  // namespace crew
