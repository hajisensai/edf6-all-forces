// The EMC's charged beam without the game (src/emc.cpp drives it, tools/emc_check.cpp checks it; no EDF.dll in here):
// the trigger's charge / beam / rearm cycle, the damage budget one beam carries, and the scan along the beam's line
// that finds the buildings it passes through and the ground it ends on (docs/emc-re.md).
#pragma once
#include <cmath>

namespace crew {
namespace emc {
constexpr float kFrameSec=1.0f/60.0f;
// m: the beam's reach, the stock round's (AmmoSpeed 8 m a frame x AmmoAlive 75: pylib/vcobjects.py EMC_BEAM_RANGE, which
// tools/selftest.py holds equal and the installer checks against the stock SGO).
constexpr float kBeamRange=600.0f;
// Let go before it is full: the charge drains this many times as fast as it filled (a quick tap does not keep it).
constexpr float kDrainShare=2.0f;
// s after a beam before the trigger charges again: the stock weapon's FireInterval (120 frames) between its bursts.
constexpr float kRearmSec=2.0f;

// idle: nothing (a charge left over drains); charging: the trigger held; firing: the beam is out (beamLeft s more);
// rearm: after it, the trigger dead for kRearmSec. A beam needs the trigger let go (any time after it fired) and pressed
// again: holding it through a beam and the rearm does not start the next charge (`released`).
enum class Phase : unsigned char { idle, charging, firing, rearm };
struct Tuning { float chargeSec,beamSec; };
struct State { Phase phase=Phase::idle; float charge=0.0f,beamLeft=0.0f,rearm=0.0f; bool released=true; };
// What a step did: a charge began (start), was let go of (cancel), filled and fired (fire), the beam ended (end).
enum class Event : unsigned char { none, start, cancel, fire, end };

// One step of `dt` s: `held` the trigger, `loaded` rounds left in the weapon.
inline Event Step(State& s,const Tuning& t,bool held,bool loaded,float dt) noexcept {
    if(!held)s.released=true;
    if(s.phase==Phase::firing) {
        s.beamLeft-=dt;
        if(s.beamLeft>0.5f*dt)return Event::none;   // within half a frame: over (float steps), as the charge
        s.beamLeft=0.0f;s.phase=Phase::rearm;s.rearm=kRearmSec;
        return Event::end;
    }
    if(s.phase==Phase::rearm) {
        s.rearm-=dt;
        if(s.rearm>0.5f*dt)return Event::none;
        s.rearm=0.0f;s.phase=Phase::idle;
        return Event::none;   // its last dead frame: the trigger charges from the next
    }
    if(held && s.released && loaded) {
        const bool starting=s.phase!=Phase::charging;
        s.phase=Phase::charging;
        // Full within half a frame: the fire lands on the frame the charge time ends, not one after (float steps).
        s.charge+=dt/t.chargeSec;
        if(s.charge>=1.0f-0.5f*dt/t.chargeSec) {
            s.charge=0.0f;s.phase=Phase::firing;s.beamLeft=t.beamSec;s.released=false;
            return Event::fire;
        }
        return starting ? Event::start : Event::none;
    }
    s.charge-=kDrainShare*dt/t.chargeSec;
    if(s.charge<0.0f)s.charge=0.0f;
    if(s.phase!=Phase::charging)return Event::none;
    s.phase=Phase::idle;
    return Event::cancel;
}

// What one beam carries: the rounds of the stock burst it spends (`used`: FireBurstCount, or what is left), their
// damage together (`line`: AmmoDamage x the weapon's damage factor x used) dealt to every enemy the beam passes
// through over its `rounds` (a round a frame, perRound each: every round passes through every enemy on the line, so
// each takes `line`), and the blast at its end (`blast`, line x EmcBlastShare).
struct Budget { int used,rounds; float line,perRound,blast; };
inline Budget Plan(float perHit,int burst,int ammo,float beamSec,float blastShare) noexcept {
    Budget b{};
    b.used=ammo<burst ? ammo : burst;
    if(b.used<0)b.used=0;
    b.rounds=static_cast<int>(std::lround(beamSec/kFrameSec));
    if(b.rounds<1)b.rounds=1;
    b.line=perHit*static_cast<float>(b.used);
    b.perRound=b.line/static_cast<float>(b.rounds);
    b.blast=b.line*blastShare;
    return b;
}

// The scan along the beam's line (from `from` along the unit `dir`, at most `range` m). Two rays a step: `map` the map's
// (layer 22: terrain and buildings, metres to the nearest hit or < 0), `buildings` the buildings' alone (layer 27: the
// map objects' layers, no terrain). A building first: it is on the line (its distance kept, kBreakSpacing apart), and
// the scan goes on from kSkip inside it (a ray started inside a shape does not meet that shape). Anything else first
// is the ground: the beam ends there. Nothing: it ends at `range`. Out of steps inside buildings: it ends where it got.
constexpr int kScanSteps=12;
constexpr int kMaxBreaks=8;
constexpr float kSkip=0.5f;          // m into a building the next step starts
constexpr float kSameHit=0.25f;      // m: the map ray's hit this near the buildings' is that building
constexpr float kBreakSpacing=9.0f;  // m between two buildings each given their own charge (the charge's 12 m blast covers nearer)
struct Scan { float end; bool ground; int breaks; float at[kMaxBreaks]; };
template<class MapRay,class BuildingRay>
Scan ScanLine(const float* from,const float* dir,float range,MapRay map,BuildingRay buildings) noexcept {
    Scan s{};s.end=range;
    float along=0.0f;
    for(int k=0;k<kScanSteps;++k) {
        const float a[3]={from[0]+dir[0]*along,from[1]+dir[1]*along,from[2]+dir[2]*along};
        const float b[3]={from[0]+dir[0]*range,from[1]+dir[1]*range,from[2]+dir[2]*range};
        const float m=map(a,b),bld=buildings(a,b);
        if(bld>=0.0f && (m<0.0f || bld<=m+kSameHit)) {
            const float at=along+bld;
            if(s.breaks<kMaxBreaks && (s.breaks==0 || at-s.at[s.breaks-1]>=kBreakSpacing))s.at[s.breaks++]=at;
            along=at+kSkip;
            if(along>=range)return s;
            continue;
        }
        if(m>=0.0f){s.end=along+m;s.ground=true;}
        return s;
    }
    s.end=along;
    return s;
}
}  // namespace emc
}  // namespace crew
