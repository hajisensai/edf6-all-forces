// The Nix's torso hold, the pure part (src/nix.cpp; checked offline by tools/nix_twist_check.cpp, which includes it
// alone: no game, no Windows). Angles in radians, yaw positive from +z toward +x (the convention 0x4CD10 turns the
// vehicle's matrix by and 0x5FACD0 turns the seat's aim by: both give the forward row (sin a, 0, cos a)), so the
// torso's world yaw is the legs' heading plus the aim's yaw axis angle (docs/nix-re.md §3).
#pragma once
#include <cmath>

namespace nixtwist {
constexpr float kTwoPi=6.28318530717958647692f;
// The stops when the aim's yaw axis has none (a full circle, which the stock step wraps instead of clamping):
// +-120 deg, so the torso still cannot turn its back on the legs.
constexpr float kOpenTwist=2.09439510239319549231f;

// `a` into [-pi, pi].
inline float Wrap(float a) noexcept { return std::remainder(a,kTwoPi); }

struct Stops { float lo,hi; };

// The yaw axis' stops: its own {min, max} when they span less than a full turn (the stock step clamps the angle
// there, 0x5FBD29..0x5FBD52), else +-kOpenTwist (a span of 2 pi or more, or within a float's epsilon of it, the
// stock wraps: 0x5FBD17..0x5FBD27, 0x5FBD59).
inline Stops TwistStops(float min,float max) noexcept {
    const float span=max-min;
    if(!(span<kTwoPi) || std::fabs(span-kTwoPi)<=1.1920929e-7f)return Stops{-kOpenTwist,kOpenTwist};
    return Stops{min,max};
}

// One frame of the hold: the legs turned by `turn` (their heading's change since the last frame, wrapped) under a
// torso twisted `twist` off them. The twist that keeps the torso's world yaw, held at the stops (past a stop the
// torso goes round with the legs; the legs are never pushed).
inline float Hold(float twist,float turn,Stops s) noexcept {
    const float t=twist-turn;
    return t<s.lo ? s.lo : t>s.hi ? s.hi : t;
}

// The torso's aim in the vehicle's frame from the two axis angles (yaw `twist`, pitch `pitch`, negative up: the
// aim's matrix 0x5FACD0 turns its forward row to (0, -sin p, cos p) by the pitch, then by the yaw).
inline void AimLocal(float twist,float pitch,float* out) noexcept {
    const float cp=std::cos(pitch);
    out[0]=std::sin(twist)*cp;out[1]=-std::sin(pitch);out[2]=std::cos(twist)*cp;
}
}  // namespace nixtwist
