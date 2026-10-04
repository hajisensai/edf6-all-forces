// The submarine carrier's interface to its neighbours (carrierlaser.cpp, body506.cpp): its geometry and the
// carriers the plugin drives, so no one else reads a carrier's matrix or keeps a list of carriers of its own.
// Game thread only.
#pragma once
#include "crew.h"

namespace crew {
// The hull (the SGO's rigid box, testrange/gen.py 'edf6tr_sub_carrier_mission' rigid: centre (0, 178.08, -7.58),
// half sizes (121, 15, 832), body frame): its top, the main deck the model has at y≈193 (docs/subcarrier-re.md
// §1.1), is kSubDeckTop over the origin.
constexpr float kSubBoxCentreY=178.08f,kSubBoxHalfY=15.0f,kSubBoxCentreZ=-7.58f,kSubBoxHalfX=121.0f,kSubBoxHalfZ=832.0f;
constexpr float kSubDeckTop=kSubBoxCentreY+kSubBoxHalfY;

// A carrier the plugin drives: the object, where it is, its velocity (m/s, what the plugin sets) and its side.
struct SubView { ObjRef ref; float pos[3],vel[3]; std::int32_t team; };
// The live carriers (at most `max`), into `out`: their number.
int SubCarriers(SubView* out,int max) noexcept;
// The point `over` metres over carrier `sub`'s deck, `along` metres along its nose (centred across it); false when
// it is no live carrier.
bool SubSpot(const ObjRef& sub,float along,float over,float* out) noexcept;
// Straight down (world -y) from `p` to carrier `sub`'s deck plane, into `out` (at least 5 m, at most 1 km down).
bool SubDeckUnder(const ObjRef& sub,const float* p,float* out) noexcept;
// The follower gauge's redirect (the carriers' gauges and panels, and hud.cpp's vehicle HUD drawn from it): only the
// draw's own code is checked, nothing of the carrier's profile. At load, once.
bool InstallGauge() noexcept;
}  // namespace crew
