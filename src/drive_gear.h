// (Since 2026-10-31 the gears are two keys of their own, W / S override them: Input below.)
// War Thunder's cruise gears for the stock ground vehicles (the user, 2026-10-25:「飞机和车类改成ws调挡位和战雷一样」
// 「车辆定速挡位吧」; ini VehicleGears): on the keyboard the cruise-up key shifts up a gear and the cruise-down key down one (held, one more a kRepeatSec),
// R N 1 2 3 4, and the vehicle keeps going in the gear it was left in. Each forward gear below the top holds a speed, its
// share of this vehicle's own governed top (the user, 2026-10-26: by each vehicle's real top, not its class's): the
// native drive's top gear speed, read off the vehicle (drivegear.cpp; docs/vehicle-top-speed-re.md); the top gear is the
// throttle held open, R held in reverse, N let go. The stock drive is given only what a key gives it (the seat's left
// stick, -1 forward / +1 back / 0), the speed held by opening it below the gear's speed and letting it go above it
// (kBand under it: no chatter), so nothing rests on how the stock responds to a part-way stick (no record of one:
// docs/feedback-2026-10-25-ws-gears.md).
// Pure (tests/drive_gear_test.cpp); drivegear.cpp reads the game.
#pragma once
#include <cmath>
#include <cstddef>

namespace drivegear {

constexpr int kReverse=-1,kNeutral=0,kTopGear=4;
constexpr float kShare[kTopGear]={0.0f,0.25f,0.5f,0.75f};   // gears 1..3: their speed's share of the top (the top gear: open)
constexpr float kBand=0.06f;          // of the top: the speed the drive opens again under a gear's
constexpr float kRepeatSec=0.4f;      // a key held shifts again this often
constexpr float kForward=-1.0f,kBack=1.0f;   // the seat's left stick Y (seat+0x2C4): forward is negative

struct State {
    int gear=kNeutral;
    int held=0;            // the key held last frame: +1 W, -1 S, 0 none
    float heldSec=0.0f;    // how long since it shifted on it
    bool open=false;       // the drive open (under the gear's speed, not yet up to it)
    bool blocked=false;    // W / S cancelled the cruise: the gear keys do nothing until they are let go (a press resumes it)
};

inline void Step(State& s,int key) noexcept {
    const int g=s.gear+key;
    s.gear=g<kReverse ? kReverse : g>kTopGear ? kTopGear : g;
}

// This frame's key: +1 W, -1 S, 0 neither (both: neither). A press shifts once; held, once more every kRepeatSec.
inline void Shift(State& s,int key,float dt) noexcept {
    if(key!=s.held) {
        s.held=key;s.heldSec=0.0f;
        if(key)Step(s,key);
        return;
    }
    if(!key)return;
    s.heldSec+=dt;
    if(s.heldSec>=kRepeatSec){s.heldSec-=kRepeatSec;Step(s,key);}
}

// One frame of the gears' keys (the user, 2026-10-31: 「载具的档位制有些难用…建议改成战雷高度相似 因为战雷档位是单独的按键，
// 同时优先级低于正常WASD 会被直接覆盖」). The gears are two keys of their own (`cruiseKey`: +1 the cruise-up key, -1 the
// cruise-down key, 0 neither or both); W / S (`manual`: the seat's stick Y past the press threshold) always win: one press
// cancels the cruise (back to N) and the keyboard's own stick goes through as the stock game reads it, no gear written.
// Let go, it stays in N and writes nothing; the gear keys cruise again once pressed anew (a gear key held through the
// W / S press does not count), from N up to 1st as before. True: the gear's stick is to be written this frame.
inline bool Input(State& s,int cruiseKey,bool manual,float dt) noexcept {
    if(manual) {
        s.gear=kNeutral;s.open=false;s.held=cruiseKey;s.heldSec=0.0f;s.blocked=cruiseKey!=0;
        return false;
    }
    if(s.blocked) {   // a gear key down since the cancel / the start / the keys' return: ignored until let go
        if(cruiseKey!=0)return s.gear!=kNeutral;
        s.blocked=false;s.held=0;s.heldSec=0.0f;
    }
    Shift(s,cruiseKey,dt);
    return s.gear!=kNeutral;
}

// A drive that starts (boarding, another vehicle, a stale one begun again): in N, and a gear key already down then (the
// sprint's Shift as the player gets in) is no press: it has to be let go and pressed anew.
inline State Fresh() noexcept {
    State s;
    s.blocked=true;
    return s;
}
// The keys are not the player's for now (the game paused, the map open, another window in front): a gear key still down
// when they come back is no press either.
inline void Hold(State& s) noexcept {
    s.blocked=true;s.held=0;s.heldSec=0.0f;
}

// The native forward gear table (CarBase +0x1760, docs/vehicle-top-speed-re.md): `count` rows, the last one's speed
// `lastSpeed` (m/s, after the setup scaled it) is the drive's top. 0: no usable table (no gears for this vehicle). The
// stock tables have 1 (car_base_engine_simple_gear) to 6 rows (car_base_engine_gear); kMaxGearRows only bounds the read.
constexpr std::size_t kMaxGearRows=32;
inline float TopOfGears(std::size_t count,float lastSpeed) noexcept {
    if(count==0 || count>kMaxGearRows || !std::isfinite(lastSpeed) || lastSpeed<=0.0f)return 0.0f;
    return lastSpeed;
}

// The seat's left stick Y for the gear at `forward` m/s along the nose (negative backing), `top` m/s this vehicle's top.
inline float Drive(State& s,float forward,float top) noexcept {
    if(s.gear==kNeutral){s.open=false;return 0.0f;}
    if(s.gear==kReverse){s.open=false;return kBack;}
    if(s.gear>=kTopGear){s.open=true;return kForward;}
    const float want=kShare[s.gear]*top;
    if(forward<want-kBand*top)s.open=true;
    else if(forward>=want)s.open=false;
    return s.open ? kForward : 0.0f;
}

// The gear's name on the HUD.
inline const wchar_t* Name(int gear) noexcept {
    switch(gear) {
    case kReverse: return L"R";
    case kNeutral: return L"N";
    case 1: return L"1";
    case 2: return L"2";
    case 3: return L"3";
    default: return L"4";
    }
}

}  // namespace drivegear
