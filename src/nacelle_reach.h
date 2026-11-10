// The carrier's nacelles over the ground (jet_flight.cpp Thrusters; the user, 2026-10-09: 「空母的碰撞体积和实际外观不
// 符。我都撞地上了，还会把空母往天上弹」).
// Its collision (pylib/aircraft_collision.py, EDF6VC_CARRIER_AIRFRAME.SHKT) is the grounded model in its bind pose: its
// bottom is the model origin's plane, what FloorClear measures from. Thrusters tilts the four nacelles about their X
// from 0 to -kThrustBack (1.92 rad) along the thrust Hover asks for, and a hover asks it straight up: the front pair
// then hung 5.2 m under that plane, through the ground the craft stood or flew low over, where its collision had
// nothing; the hull's contact held it up over what the eye saw it sink into.
// So near the ground a nacelle tilts no further than keeps its lowest point over the ground under the craft: kFront /
// kBack are the nacelle's lowest model y (m over the origin) every kStep from 0 to -kBack, measured off the model by
// pylib/aircraft_collision.py nacelle_lowest (tools/aircraft_collision_check.py --game holds these to it).
// The support's Helicarrier (pylib/helicarrier_model.py, jet_internal.h Body::supCarrier ..) tilts its four lift fans under
// the same bone names: kHcFront / kHcBack, its fans' lowest model y the same way (tools/aircraft_collision_check.py holds them
// to the model pack's model when it is there). Its fans stand in rings high on its sponsons: 5 m over its bottom at the
// least, so they never limit the tilt; jet.cpp NewEntry marks the carrier (Jet::liftFans, by its mark) and Thrusters picks the table by it.
// Pure arithmetic: tools/jet_obstacle_sim.cpp --ground-settle-suite checks MostTilt.
#pragma once
#include <cmath>

namespace crew {
namespace nacelle {
constexpr float kStep=5.0f*3.14159265f/180.0f;   // rad between entries
constexpr int kCount=23;                          // 0 .. -110 deg
constexpr float kMostBack=1.92f;                  // rad: the furthest Thrusters tilts them (jet_flight.cpp kThrustBack)
constexpr float kFront[kCount]={1.17f,1.04f,0.97f,0.96f,1.01f,1.12f,0.46f,-0.50f,-1.39f,-2.19f,-2.91f,-3.54f,-4.08f,-4.51f,-4.84f,
                                -5.06f,-5.18f,-5.19f,-5.09f,-5.19f,-5.17f,-5.05f,-4.83f};
constexpr float kBack[kCount]={7.14f,7.24f,7.06f,6.69f,6.10f,5.56f,5.05f,4.59f,4.17f,3.80f,3.49f,3.23f,3.02f,2.88f,2.79f,2.77f,2.81f,
                               2.84f,2.93f,2.85f,2.81f,2.78f,2.80f};
constexpr float kHcFront[kCount]={5.02f,5.06f,5.11f,5.18f,5.29f,5.42f,5.58f,5.76f,5.97f,6.20f,6.45f,6.72f,6.98f,7.04f,7.10f,7.19f,
                                  7.28f,7.39f,7.51f,7.50f,7.48f,7.48f,7.48f};
constexpr float kHcBack[kCount]={4.96f,5.02f,5.10f,5.22f,5.36f,5.53f,5.73f,5.95f,6.20f,6.45f,6.71f,6.99f,7.28f,7.59f,7.65f,7.67f,
                                 7.70f,7.74f,7.77f,7.70f,7.64f,7.60f,7.55f};
// A carrier's two tables: its front pair's (boosterF_*) and its back pair's (boosterB_*).
struct Reach { const float* front; const float* back; };
constexpr Reach kV508{kFront,kBack};
constexpr Reach kHelicarrier{kHcFront,kHcBack};

// The nacelle's lowest model y at tilt `at` (rad, 0 .. -kStep (kCount - 1)), between the entries.
inline float Lowest(const float* table,float at) noexcept {
    float f=-at/kStep;
    if(!(f>0.0f))return table[0];
    if(f>=kCount-1)return table[kCount-1];
    const int i=static_cast<int>(f);
    f-=static_cast<float>(i);
    return table[i]+(table[i+1]-table[i])*f;
}

// The furthest tilt (rad, <= 0, no further than `most` < 0) from level whose lowest point stays at or over `floor` (model
// y: minus the bottom's clearance) all the way from 0: the nacelle swings no lower on its way there either.
inline float MostTilt(const float* table,float floor,float most) noexcept {
    if(!std::isfinite(floor))return most;
    constexpr int kFine=8;   // samples between entries
    float ok=0.0f;
    for(int s=1;;++s) {
        const float at=-kStep*static_cast<float>(s)/kFine;
        if(at<most){return Lowest(table,most)>=floor ? most : ok;}
        if(Lowest(table,at)<floor)return ok;
        ok=at;
    }
}
}  // namespace nacelle
}  // namespace crew
