// The submarine carrier's interface to its neighbours (carrierlaser.cpp, body506.cpp): its geometry and the
// carriers the plugin drives, so no one else reads a carrier's matrix or keeps a list of carriers of its own.
// Game thread only.
#pragma once
#include "objref.h"
#include "npcai.h"
#include "support_call.h"
#include "transport.h"

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
// Nearest actual deck point of this exact carrier (for callers filtering by native enemy relations).
bool SubDeckFor(const ObjRef& sub,const float* from,float* out) noexcept;
// Exact-plan support transaction. Planning alone chooses the water entry; Prepare creates an inert EMPTY hull.
// Only an authority's real seated driver or a registered remote copy may activate after all-peer acknowledgement.
bool PlanSupportSubmarine(const float* target,const float* from,float* matrix,float* afloatY) noexcept;
bool ValidateSupportSubmarine(const float* matrix,float afloatY) noexcept;
unsigned char* PrepareSupportSubmarine(const float* matrix,float afloatY) noexcept;
bool ActivateSupportSubmarine(const ObjRef& ref) noexcept;
bool DeleteSupportSubmarine(const ObjRef& ref) noexcept;
// Dispatcher binds only its strictly validated canonical requester before activation.
bool BindSupportSubmarineOwner(const ObjRef& ref,const unsigned char owner[32]) noexcept;
std::uint64_t SubmarineSupportKey(const ObjRef& ref) noexcept;
bool ResolveSupportSubmarine(std::uint64_t parentHash,ObjRef* out) noexcept;
bool AddSupportSubmarineDrone(const ObjRef& parent,const ObjRef& child) noexcept;
// Implemented by the support dispatcher: one host-authorized child transaction, all-peer spawn/ACK/activation.
bool SubmitSubmarineDrone(const ObjRef& parent,const float* from,const float* nose,const float* anchor,
                          const unsigned char owner[32]) noexcept;
// The follower gauge's redirect (the carriers' gauges and panels, and hud.cpp's vehicle HUD drawn from it): only the
// draw's own code is checked, nothing of the carrier's profile. At load, once.
bool InstallGauge() noexcept;
}  // namespace crew

namespace crew {
void ResetSubs() noexcept;        // subcarrier.cpp
bool SubBodyStep(unsigned char* v,float* lin,float* ang) noexcept;        // subcarrier.cpp

// subcarrier.cpp: the submarine carrier (潜水母艦, docs/subcarrier-re.md), a 506 body from EDF6VC_SUB_CARRIER.SGO
// (tools/make_sub.py) driven by the plugin: it sits surfaced, follows the player at a ship's pace, turns its bow
// on the nearest enemy, fires its turret guns and homing missiles, reloads aboard; its HP shows as a follower gauge.
bool IsSub(const void* vehicle) noexcept;         // a 506 body with the carrier's mark
void SubFrame(unsigned char* vehicle) noexcept;   // from every vehicle's input hook (crew.cpp SubStep), carriers only
bool InstallSub() noexcept;                       // after InstallJets (it chains onto the 506 physics slot)
void PreloadSub() noexcept;                       // from the mission's player preload
// A carrier made at run time on the ground at `pos` (metres; it is raised to sit on the highest ground under
// its hull), its bow along `heading` (horizontal part used), friend, NPC pilot: the vehicle, or nullptr (its
// files not installed or not preloaded this mission, three already out, the game failed to build it).
unsigned char* SubLaunch(const float* pos,const float* heading) noexcept;
// The live carrier nearest to `from`: a point on its flat bow deck (the hull box top, 193 m over its origin)
// nearest to `from`, into `deck`; false with no carrier out.
bool SubDeck(const float* from,float* deck) noexcept;
// Horizontal metres from `p` to the nearest live carrier's hull footprint (0 over it), -1 with none.
float SubHullGap(const float* p) noexcept;
}  // namespace crew
