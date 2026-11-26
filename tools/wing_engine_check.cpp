// An NPC wing's engine offline (src/wing_engine.h; jet_flight.cpp JetSteer flies it, jetsound.cpp and JetFlames read
// its lever): over the typical stretches of a fighter's flight the lever and the afterburner follow the thrust the
// flight commands, not its speed. Level at cruise: the player's cruise lever, unlit. Accelerating for the attack, a
// steep climb, a sustained hard turn, an attack run pushing for its speed in a turn (the live run's 229 of 235 m/s): lit.
// Holding the attack speed level, a fast cruise wanting nothing more, a dive braking: out. A one-frame gap does not
// light it.
//
//   cmake --build build --target wing_engine_check && build\wing_engine_check.exe
#include "../src/wing_engine.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace crew::jet::wing;
int failures=0;
void Check(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}

// The fighter's kind (jet_internal.h kKinds): cruise 210, attack 235 m/s, thrust 15, brake 20 m/s^2.
constexpr float kCruise=210.0f,kAttack=235.0f,kThrust=15.0f,kBrake=20.0f,kG=9.8f,kDt=1.0f/60.0f;
constexpr float kTurnBleed=3.0f;   // jet_flight.cpp: m/s^2 lost per g pulled over 1

struct Flight {
    float s,lever;
    bool everLit=false;
    float litFor=0.0f;   // s lit, the last stretch
};
// `secs` of flight wanting `want`, its path's sine `climb`, pulling `g` g; the lever and the burner at the end.
void Fly(Flight& f,float want,float climb,float pull,float secs) {
    const float bleed=pull>1.0f ? (pull-1.0f)*kTurnBleed : 0.0f;
    f.litFor=0.0f;
    for(int i=0;i<static_cast<int>(secs/kDt+0.5f);++i) {
        const SpeedStep st=Speed(f.s,want,kCruise,kThrust,kBrake,climb,bleed,kG,kDt);
        f.s=st.speed;
        f.lever=Lever(f.lever,st.demand,kDt);
        if(Burner(f.lever)){f.everLit=true;f.litFor+=kDt;}
    }
}
}  // namespace

int main() {
    {
        Flight f{kCruise,0.0f};
        Fly(f,kCruise,0.0f,1.0f,10.0f);
        std::printf("      level cruise: %.1f m/s, lever %.3f\n",f.s,f.lever);
        Check(std::fabs(f.lever-kCruiseThrottle)<0.01f,"level at cruise: the player's cruise lever (0.55)");
        Check(!f.everLit,"level at cruise: never lit");
    }
    {
        Flight f{150.0f,kCruiseThrottle};
        Fly(f,kAttack,0.0f,1.0f,3.0f);
        std::printf("      accelerating 150 -> %.0f: %.1f m/s, lever %.3f, lit %.2f s\n",kAttack,f.s,f.lever,f.litFor);
        Check(Burner(f.lever) && f.litFor>2.0f,"accelerating for the attack: lit within a second, and held");
        Fly(f,kAttack,0.0f,1.0f,8.0f);
        std::printf("      holding the attack level: %.1f m/s, lever %.3f\n",f.s,f.lever);
        Check(std::fabs(f.s-kAttack)<0.5f,"the attack speed reached");
        Check(!Burner(f.lever) && f.lever<0.8f,"holding the attack speed level: out (only the drag's share)");
    }
    {
        Flight f{kCruise,kCruiseThrottle};
        Fly(f,kCruise,0.5f,1.0f,5.0f);   // 30 degrees: 4.9 of 15 m/s^2
        std::printf("      30 deg climb: %.1f m/s, lever %.3f\n",f.s,f.lever);
        Check(!f.everLit && f.lever>0.8f,"a 30 degree climb at cruise: the lever up, not lit");
        Fly(f,kCruise,0.87f,1.0f,5.0f);   // 60 degrees: 8.5 of 15
        std::printf("      60 deg climb: %.1f m/s, lever %.3f\n",f.s,f.lever);
        Check(Burner(f.lever),"a 60 degree climb: lit");
        Fly(f,kCruise,0.0f,1.0f,10.0f);
        Check(!Burner(f.lever) && std::fabs(f.s-kCruise)<0.5f,"levelled off at cruise again: out");
    }
    {
        Flight f{kCruise,kCruiseThrottle};
        Fly(f,kCruise,0.0f,5.0f,4.0f);   // a 5 g sustained turn: 12 m/s^2 of bleed
        std::printf("      5 g turn: %.1f m/s, lever %.3f\n",f.s,f.lever);
        Check(Burner(f.lever),"a sustained 5 g turn: lit");
        Flight e{kCruise,kCruiseThrottle};
        Fly(e,kCruise,0.0f,2.0f,4.0f);   // 2 g: 3 of 15
        Check(!e.everLit,"a 2 g turn: not lit");
    }
    {
        // The live run's: pushing for 235 at 229 in a 4 g turn, climbing 10 degrees (the thrust all taken).
        Flight f{229.0f,0.6f};
        Fly(f,kAttack,0.17f,4.0f,4.0f);
        std::printf("      attack run at 229 for 235, 4 g: %.1f m/s, lever %.3f, lit %.2f s\n",f.s,f.lever,f.litFor);
        Check(Burner(f.lever) && f.litFor>3.0f,"an attack run pushing for its speed in a turn: lit (at 229 of 235 m/s)");
    }
    {
        // A fast cruise (the speed of the old threshold, 222.5) wanting no more, level: no thrust asked past the drag.
        Flight f{222.5f,kCruiseThrottle};
        Fly(f,222.5f,0.0f,1.0f,5.0f);
        Check(!f.everLit,"a fast level cruise wanting nothing more: not lit (a speed does not light it)");
    }
    {
        Flight f{kAttack,1.0f};
        Fly(f,kCruise,-0.5f,1.0f,0.8f);   // diving, braking for cruise
        std::printf("      diving, braking to cruise: %.1f m/s, lever %.3f\n",f.s,f.lever);
        Check(f.s>kCruise+1.0f && f.lever<0.01f,"a dive braking: out, at idle");
        Fly(f,kCruise,-0.5f,1.0f,3.0f);
        std::printf("      diving at cruise: %.1f m/s, lever %.3f\n",f.s,f.lever);
        Check(f.litFor==0.0f && f.lever<kCruiseThrottle-0.2f,"diving at cruise: below the level cruise's lever (gravity helps)");
    }
    {
        // A wanted speed a frame's thrust off: two frames saturated do not light it.
        Flight f{kCruise,kCruiseThrottle};
        Fly(f,kCruise+0.5f,0.0f,1.0f,2.0f);
        Check(!f.everLit,"a 0.5 m/s gap: not lit");
    }
    Check(Lever(0.0f,1.0f,1.0f/kLeverRate)==1.0f && Lever(1.0f,0.0f,0.5f)==0.25f,"the lever's travel: kLeverRate a second");
    std::printf("%s\n",failures ? "wing_engine_check: FAILED" : "wing_engine_check: all ok");
    return failures ? 1 : 0;
}
