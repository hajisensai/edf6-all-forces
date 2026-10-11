// The stock route guide (class RouteGuide, the scrolling arrow strip a mission draws from the player to where it
// sends them): the pure part of route_guide.cpp, no game memory (tests/route_guide_test.cpp).
// The game finds the route on the Havok navmesh (its corners hug the walls), then the strip's builder spoils it
// (0x5C7990, docs/route-guide-re.md):
//  - it drops every corner within +0x194 (15 m) of the route's start and every one within +0x198 (7 m) of the last
//    kept, so the strip runs straight from the player to a corner farther on, across the building corner between;
//  - with +0x1A0 false (every stock caller) it draws a Hermite curve through the middles of the legs (0x5CA140), the
//    corners only its tangents: at each corner the curve cuts deep into the wall's side;
//  - it eases each point towards last frame's point of the same index (+0x2D8, 0.04 a frame): once the route gains
//    or loses a corner the points pair with other corners and slide through walls for ~25 frames.
// And the EDF5 scripts' guide (BVM native 1200, 0x21F380) is made in mode 0 with its target object at +0x168, but the
// frame's target update (0x5C9D20) knows modes 1 (an object) and 2 (an area) only: its goal +0x1D0 is never set and
// the strip points at the map's origin.
// Each correction below is a mode the game's own fields offer; the route search is not touched.
#pragma once
#include <cstdint>

namespace routeguide {
// RouteGuide's fields (the InitParam copied to +0x120, docs/route-guide-re.md §1).
constexpr std::uint32_t kMode=0x150,kTarget=0x168,kNearDrop=0x194,kMergeDrop=0x198,kPolyline=0x1A0,kSmoothing=0x2D8;
enum : std::uint32_t { kModeNone=0,kModeObject=1,kModeArea=2 };
// A corner this close to the start or to the last kept one is still dropped: the strip's segments need some length
// (its scroll phase runs along them), and a navmesh corner that close is no turn worth drawing.
constexpr float kKeepCorners=1.0f;

struct Fields {
    std::uint32_t mode;
    bool target;        // +0x168 holds an object
    float nearDrop,mergeDrop;
    bool polyline;
    float smoothing;
};

// The guide's fields corrected: along the navmesh route's own corners, every frame's route as found, and the EDF5
// scripts' guide following the object they named.
inline Fields Corrected(Fields f) noexcept {
    if(f.mode==kModeNone && f.target)f.mode=kModeObject;
    if(!(f.nearDrop<=kKeepCorners))f.nearDrop=kKeepCorners;    // NaN as well
    if(!(f.mergeDrop<=kKeepCorners))f.mergeDrop=kKeepCorners;
    f.polyline=true;
    f.smoothing=1.0f;
    return f;
}
}  // namespace routeguide
