// Engine activity is separate from airspeed: a parked empty aircraft is off,
// an occupied one idles, and a hovering aircraft still needs powered lift.
#pragma once
#include <cmath>

namespace crew::jetsound {
struct State {
    bool occupied=false,commanded=false,parked=true,rotor=false;
    float throttle=0.0f,speed=0.0f;
};
struct Engine { float gain=0.0f,power=0.0f; };
inline float Unit(float x) noexcept { return std::isfinite(x) && x>0.0f ? (x<1.0f ? x : 1.0f) : 0.0f; }
inline Engine For(const State& s) noexcept {
    if(!s.occupied && (!s.commanded || s.parked))return {};
    if(s.parked)return {1.0f,0.0f};   // idling: the engine's idle layers (sound_engine.h)
    const float speed=Unit(s.speed/150.0f),throttle=Unit(s.throttle);
    const float power=throttle>speed ? throttle : speed;
    return {1.0f,s.rotor && power<0.35f ? 0.35f : power};
}
// False: this aircraft has no current player-flight record. No ownership is acquired.
bool PlayerState(const unsigned char* vehicle,State* out) noexcept;
// An aircraft's engine lever (0..1 of full thrust: the player's throttle, an NPC wing's wing_engine.h lever, a rotor's
// power) and how it is heard.
struct Lever { float lever=0.0f; bool rotor=false,parked=false; };
// Where this machine flies it now: its player's (PlayerState) or its live NPC flight's (jet.cpp); false: neither.
bool LocalLever(const unsigned char* vehicle,Lever* out) noexcept;
// Sent by the machine that runs it (jet_lever_net.cpp, a registered jet this one does not run), heard lately and not
// off; false otherwise.
bool HeardLever(const unsigned char* vehicle,Lever* out) noexcept;
}  // namespace crew::jetsound
