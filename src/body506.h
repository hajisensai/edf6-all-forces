// The 506 body's shared tools (body506.cpp): what the owners of the plugin's 506 bodies (subcarrier.cpp,
// playerjet.cpp; jet.cpp has its own copies until it switches, docs/review-fix-plan.md) do the same way.
// Game thread only, under the caller's __try.
#pragma once
#include "crew.h"
#include <cstdint>

namespace crew {
// Metres the ground (terrain, buildings: map rays) is under `p`, negative under the ground (the surface over
// it), kNoGround with none seen (off the map's edge). Water is not seen: the map ray goes through it to the
// seabed (docs/water-re.md, layer 22).
constexpr float kNoGround=-1e9f;
float GroundClearance(const float* p) noexcept;
// The ceiling the stock input holds every body under (*(*(image+0x20B2998)+0x3C)), 1e9 with none.
float CeilingY() noexcept;
// Seconds of game the frame `since` ms of GameMs long moved the world: the game steps a frame at a time, so a
// slow frame (under 60 a second) moves a body no more than 1/60 s of its velocity (jet.cpp Sense). 1/60 for
// the first frame (since 0).
float GameStep(ULONGLONG since) noexcept;
// The heli's "body" part (veh+0x1530, -1 in a model without that bone): the crash step reads it unchecked, so
// it is looked up by the model's fuselage bone (bomber501 / bomber401 / body). `tag` names the owner in the log.
// False when it is still missing (going down the vehicle would crash the game) or the lookup is off.
bool FixBodyPart506(unsigned char* v,const char* tag) noexcept;
bool BodyPartOk() noexcept;
// The angular velocity (into `omega`) that turns v's rows (right, up, forward at veh+0x60) onto `nose` and `up`:
// sin(angle) * axis from the three rows, times `gain` (1/s), at most `maxRate` rad/s (jet.cpp Attitude).
void BodyAttitude(const unsigned char* v,const float* nose,const float* up,float gain,float maxRate,float* omega) noexcept;
// The record of bone `name` in model instance `inst` (veh+0xEE0), or nullptr (jet.cpp BoneRecord).
unsigned char* BoneRecord506(const unsigned char* inst,const wchar_t* name) noexcept;
constexpr std::size_t kModelInst506=0xEE0,kInstBones506=0x10,kBoneLocal506=0x70,kBoneWorld506=0xB0;
// `bind` (a bone's local matrix, row vectors) turned `angle` rad about its local X: Rx(angle) x bind.
void HingePose(const float* bind,float angle,float* out) noexcept;

// The 506's messages (slot 9, hooked once here like slot 57): each owner sees the messages to its bodies first.
// An owner returns true to take a message whole (the stock handler never sees it, the hook answers true);
// else it may set `restore`: a float in the message data put back to `was` after the stock handler ran.
struct MessageRestore { float* at; float was; };
bool SubMessage(unsigned char* v,std::uint32_t msg,void* data,MessageRestore* restore) noexcept;        // subcarrier.cpp
bool PlayerJetMessage(unsigned char* v,std::uint32_t msg,void* data,MessageRestore* restore) noexcept;  // playerjet.cpp
bool SwarmMessage(unsigned char* v,std::uint32_t msg,void* data,MessageRestore* restore) noexcept;      // jet_swarm.cpp
constexpr std::uint32_t kMsgDamage=0x10000000,kMsgWater=0x10000025,kMsgDie=0x1000000F;
bool Body506MessageOk() noexcept;   // the slot 9 hook is in
// The vehicle's death as the game delivers it: message 0x1000000F through its own message slot, which the 506
// passes to the vehicle handler 0x62ECB0, whose death step 0x6329B0 kicks every seat, sets HP 0 and the dead
// byte and starts the wreck (docs/player-jet-re.md §4). False when that path did not check out at install:
// the caller must then not leave the vehicle half dead.
bool Die506(unsigned char* v) noexcept;
bool Die506Ok() noexcept;
}  // namespace crew
