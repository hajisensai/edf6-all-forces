#pragma once
// An NPC wing's engine (jet_flight.cpp JetSteer flies it; jetsound.cpp hears it, booster.cpp JetFlames shows it): the
// lever its flight works, with the player's jet's meaning (playerjet.cpp): 0..1 of its full thrust, kCruiseThrottle
// holding its cruise level, past kBurnerAt the afterburner. The lever follows the thrust the flight commands each frame,
// not a speed: a run's fighters pushing for their attack speed peaked at 229 of 235 m/s (gravity and their turns take it
// off), so a burner keyed to a speed near the attack's never lit, and one keyed lower lit for a fast cruise that asks
// for nothing. Here it lights where the thrust is saturated (accelerating, climbing steeply, a sustained hard turn, a
// chase that cannot close) and goes out when the flight holds its speed or slows.
//
// Offline: tools/wing_engine_check.cpp.

namespace crew::jet::wing {

constexpr float kCruiseThrottle=0.55f;   // pjet_handling.h's: the player's lever let go holds its cruise
constexpr float kLeverRate=1.5f;         // the lever's travel a second (playerjet.cpp kAirThrottleRate)
constexpr float kBurnerAt=0.95f;         // the player's: the lever's last notch (throttle > 0.95) lights it

struct SpeedStep {
    float speed;    // m/s along the path after this frame
    float demand;   // 0..1: the share of its full thrust the flight commanded this frame
};

// One frame (dt s) of a wing's speed `s` along its path: it closes on `want` at most `thrust` m/s^2 faster and `brake`
// slower, and gravity along the path (`climb`: the path's sine, + up) and the turn's induced drag `bleed` (m/s^2) come
// on top. Its flight has no parasitic drag (the plugin sets the velocity), so what a real one needs to hold its speed
// is counted as the player's (kCruiseThrottle at `cruise`, as v^2). The demand: that drag share and what the speed
// command took (closing on `want`, so in a steady climb or turn the climb's and the turn's share), never below idle.
inline SpeedStep Speed(float s,float want,float cruise,float thrust,float brake,float climb,float bleed,float g,float dt) noexcept {
    const float up=thrust*dt,down=brake*dt;
    float closing=want-s;
    if(closing>up)closing=up;
    if(closing<-down)closing=-down;
    const float rel=cruise>1.0f ? s/cruise : 1.0f;
    float demand=kCruiseThrottle*rel*rel+(up>0.0f ? closing/up : 0.0f);
    if(demand<0.0f)demand=0.0f;
    if(demand>1.0f)demand=1.0f;
    return SpeedStep{s+closing-(g*climb+bleed)*dt,demand};
}

// The lever a frame on: toward `demand` at kLeverRate.
inline float Lever(float lever,float demand,float dt) noexcept {
    const float step=kLeverRate*dt;
    if(lever<demand)return lever+step<demand ? lever+step : demand;
    return lever-step>demand ? lever-step : demand;
}

inline bool Burner(float lever) noexcept { return lever>kBurnerAt; }

}  // namespace crew::jet::wing
