// A wing's pull-out from a dive, predicted (jet_flight.cpp Guard). Pure arithmetic: no game state, so the offline flight
// test (tools/jet_obstacle_sim.cpp --selftest) checks it against the flight code it stands for.
//
// What it stands for is JetSteer's flight (jet_flight.cpp): lift only along the body's up, at most the kind's maxG over
// its stores' mass (Burden), never more than kNegG pushing, the body rolled onto the lift by BodyAttitude (body506.cpp:
// gain times the sine of the error, at most the kind's roll rate). Until 2026-10-06 Guard took the pull-out as a circle
// of radius v^2 / (maxG g) entered after kReact plus the roll at its full rate, which leaves out three things the flight
// does: (1) the stores: a strike jet's 6 AGM, 6 Mk 82, 38 rockets and 2 AIM-9X weigh 1.21 times the clean jet, 4.1 g
// left of its 5; (2) gravity: lifting at n g out of a dive at angle a the path turns at (n - cos a) g / v, so the bottom
// of the pull-out turns at (n - 1) g, a fifth less than n g for a strike jet; (3) the speed it gains meanwhile (g sin a a
// second), and the slow start of a roll from inverted (the attitude gain acts on the sine of the error: near 180 deg
// it hardly turns). The log of 2026-10-06 22:33: a multirole jet dived inverted at -187 m/s, Guard began its pull at
// about 900 m and it reached the ground (held off it at y=26 at -121 m/s: "held off the ground").
#pragma once
#include <cmath>

namespace crew {
namespace jet {

struct PullOutIn {
    float speed;     // m/s along the path
    float sinDive;   // sine of the path's angle under the horizon (0 level, 1 straight down; <= 0: not diving)
    float maxG;      // g the wing lifts at most (the kind's over its Burden's mass)
    float roll;      // rad: the body's up from the lift's way out of the dive (jet_flight.cpp RollToLift)
    float rollRate;  // rad/s: the kind's roll rate (BodyAttitude's cap)
    float gain;      // 1/s: BodyAttitude's gain (jet_flight.cpp kAttGain)
    float react;     // s: before anything is done
    float thrust;    // m/s^2 its engine adds at most (the kind's over its Burden's mass)
    float top;       // m/s it never flies past (JetSteer's clamp)
    float g;         // m/s^2: the flight's gravity (jet_internal.h kG)
};
struct PullOut {
    float drop;      // m it sinks until its path is level
    float run;       // m it goes over the ground meanwhile
    float seconds;   // s that takes
};

// The roll counts as done (the lift mostly up: cos 20 deg) within kRollDone of the lift's way.
constexpr float kRollDone=0.35f;
// A step of the prediction (s), and the longest it runs (s: a jet that cannot level out in that, straight down at the
// top speed, is past saving anyway; the drop is then the sink so far).
constexpr float kPullStep=1.0f/30.0f,kPullMost=20.0f;

// The drop and run of its pull-out begun now: kept straight for `react`, then rolled onto the lift (straight on
// meanwhile: what it lifts while the body comes round is left out), then lifting at maxG until level.
inline PullOut PredictPullOut(const PullOutIn& in) noexcept {
    PullOut out{0.0f,0.0f,0.0f};
    if(!(in.speed>0.0f) || !(in.sinDive>0.0f))return out;
    float s=in.speed,dive=std::asin(in.sinDive>1.0f ? 1.0f : in.sinDive),roll=in.roll>0.0f ? in.roll : 0.0f,t=0.0f;
    const float top=in.top>s ? in.top : s;
    while(t<kPullMost) {
        const float sd=std::sin(dive),cd=std::cos(dive);
        if(t>=in.react && roll<=kRollDone) {
            // Lifting: the path turns up at (n - cos a) g / v.
            dive-=(in.maxG-cd)*in.g/s*kPullStep;
            if(dive<=0.0f)break;
        } else if(t>=in.react) {
            const float rate=in.gain*std::sin(roll),r=rate<in.rollRate ? rate : in.rollRate;
            // Inverted dead on, the sine is 0 and the gain would never start it: the body is never exactly there.
            roll-=(r>0.05f ? r : 0.05f)*kPullStep;
        }
        out.drop+=s*sd*kPullStep;
        out.run+=s*cd*kPullStep;
        // Gravity along the path and the engine's most; the brake and the turn's bleed left out (both slow it, which
        // only tightens the turn: the prediction errs long).
        s+=(in.g*sd+in.thrust)*kPullStep;
        if(s>top)s=top;
        t+=kPullStep;
    }
    out.seconds=t;
    return out;
}

}  // namespace jet
}  // namespace crew
