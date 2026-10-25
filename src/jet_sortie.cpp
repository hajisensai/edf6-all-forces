// The jets' sorties (docs/plan-2026-10-25-strikes.md §1.2; the user's strike list, 2026-10-25: 「打一轮就走」): a sortie
// call's jets (tools/calls.py `sortie`, its SupportAircraft::sortie: ActivateSupportAircraft, jet_spawn.cpp) come in from
// off the map through the support deployment as every call's, make one pass of their kind at the mark and withdraw
// (Withdraw: jet.cpp Away climbs off away from the player, the dispatcher deletes it out of sight with its crew). Their
// weapons are their own stores (the native rounds every machine sees), fired through jet_combat.cpp Fire as ever; what a
// sortie changes is the way in, what is struck and when it is over. Geometry: src/sortie_plan.h.
//   gunRun       the strike run (Strike: a shallow dive, rockets then guns) once: pulled out of its dive, it withdraws
//   missilePass  level kMissilePassAlt over the mark along its line at its attack speed, the nose put on each target
//                ahead in turn for its air-to-ground missiles (each at another target: SortieStruck), on past the mark
//   carpet       level kCarpetAlt over the mark along its line, its bombs let go to land within kCarpetHalf of it either
//                side, a gap apart over the run (SortieFire); a formation abreast lays parallel bands
//   popUp        in low (kPopAlt) at the mark, two bombs onto it, then a steep climb away
//   missileRain  from high (kRainAlt) and far, diving at the mark: its long-range missiles (air-to-air stores, fired at the
//                ground: SortieStoreFlyer) at the enemies round it, one a gap, each at another; at kRainTo it withdraws
//   gunshipLap   the gunship's own circle round the mark and its shells, one lap counted (sortie::Lap), then away
// Every pass that finds nothing to strike gives up after kSearchMs, or once past the mark on its line.
#include "jet_internal.h"
#include "sortie_plan.h"

namespace crew {
namespace jet {
namespace {
void Done(Jet& j,const char* why,ULONGLONG ms) noexcept {
    j.sortie.done=true;
    Withdraw(j,why,ms);
}
// Level at `height` toward the point `lead` m on along the pass from where it is.
void Run(const Jet& j,const float* pos,float height,float lead,float* want) noexcept {
    float p[3];sortie::Ahead(j.sortie.line,pos,lead,p);
    const float to[3]={p[0]-pos[0],0.0f,p[2]-pos[2]};
    Level(pos,to,height,want);
}
// The nose on `aim` (no steeper down than kNoseDown), and up off it below `floorY`.
constexpr float kNoseDown=-0.5f;
void NoseOn(const float* pos,const float* aim,float floorY,float* want) noexcept {
    float d[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
    if(!Normalize(d)){d[0]=0.0f;d[1]=0.0f;d[2]=1.0f;}
    if(d[1]<kNoseDown)d[1]=kNoseDown;
    if(pos[1]<floorY && d[1]<0.15f)d[1]=0.15f;
    std::memcpy(want,d,12);
    Normalize(want);
}
// Its last missile (Fire's missileAt) taken as a shot at its target: that one struck (another next), one more fired.
void CountMissile(Jet& j) noexcept {
    Sortie& s=j.sortie;
    if(j.t.missileAt==s.missileSeen)return;
    s.missileSeen=j.t.missileAt;
    ++s.fired;
    if(j.t.target && s.struckCount<static_cast<int>(sizeof(s.struck)/sizeof(s.struck[0])))s.struck[s.struckCount++]=j.t.target;
    j.t.lockAt=0;
    Log("JET v=%p sortie: missile %d away",j.Vehicle(),s.fired);
}
const char* const kPassNames[]={"none","gun run","missile pass","carpet","hit and run","missile rain","gunship lap",
                                "heli strafe","heli hunter","heli circle"};
}  // namespace

void SortieBegin(Jet& j,SortiePass pass,const float* mark,const float* pos,const float* heading,ULONGLONG ms) noexcept {
    Sortie& s=j.sortie;
    s=Sortie{};
    s.pass=pass;s.begun=ms;s.phaseAt=ms;
    s.line=sortie::LineThrough(mark,pos,heading);
    s.missileSeen=j.t.missileAt;
    Log("JET v=%p sortie: %s at (%.0f,%.0f,%.0f)",j.Vehicle(),kPassNames[static_cast<int>(pass)],mark[0],mark[1],mark[2]);
}

bool SortieStruck(const Jet& j,const void* object) noexcept {
    for(int i=0;i<j.sortie.struckCount;++i)if(j.sortie.struck[i]==object)return true;
    return false;
}

bool SortieStoreFlyer(const Jet& j,bool flyer) noexcept {
    return flyer || (j.sortie.pass==SortiePass::missileRain && !j.sortie.done);
}

ULONGLONG SortieLockMs(const Jet& j,ULONGLONG stock) noexcept {
    const SortiePass p=j.sortie.pass;
    return !j.sortie.done && (p==SortiePass::missilePass || p==SortiePass::missileRain) ? sortie::kLockMs : stock;
}
ULONGLONG SortieMissileGapMs(const Jet& j,ULONGLONG stock) noexcept {
    const SortiePass p=j.sortie.pass;
    return !j.sortie.done && (p==SortiePass::missilePass || p==SortiePass::missileRain) ? sortie::kMissileGapMs : stock;
}

bool SortieGuide(Jet& j,const Kind& kind,const Arms& arms,const float* pos,const float* nose,const float* lead,float height,
                 ULONGLONG ms,float* want,float* speed,bool* gunsOk,bool* missileOk) noexcept {
    Sortie& s=j.sortie;
    if(s.pass==SortiePass::none || s.done)return false;
    const float along=sortie::Along(s.line,pos);
    const float mark=s.line.at[1];
    const bool ground=j.t.target && !j.t.flyer;
    switch(s.pass) {
    case SortiePass::gunRun:
        if(!ground) {
            if(along>sortie::kPassBeyond || ms-s.begun>sortie::kSearchMs)Done(j,"sortie: nothing to strike",ms);
            Run(j,pos,height,600.0f,want);*speed=kind.attack;
            return true;
        }
        Attack(j,arms,pos,nose,lead,height,ms,want,speed,gunsOk,missileOk);
        if(j.mode==Mode::dive)s.phase=1;
        else if(s.phase==1)Done(j,"sortie: its pass made",ms);   // pulled out of its one dive
        return true;
    case SortiePass::missilePass: {
        CountMissile(j);
        *speed=kind.attack;
        if(along>sortie::kPassBeyond || (along<0.0f && ms-s.begun>2*sortie::kSearchMs)) {
            Done(j,s.fired ? "sortie: its pass made" : "sortie: nothing to strike",ms);
            Run(j,pos,mark+sortie::kMissilePassAlt,600.0f,want);
            return true;
        }
        const float toward=ground ? sortie::Along(s.line,j.t.aim)-along : 0.0f;
        if(ground && s.fired<sortie::kPassMissiles && toward>200.0f && arms.missiles>0) {
            NoseOn(pos,j.t.aim,mark+120.0f,want);
            *missileOk=MissileReach(kind,arms)>0.0f;
        } else Run(j,pos,mark+sortie::kMissilePassAlt,600.0f,want);
        return true;
    }
    case SortiePass::carpet:
        *speed=kind.attack;
        if(along>sortie::kCarpetHalf+sortie::kPassBeyond)Done(j,s.fired ? "sortie: its bombs laid" : "sortie: no bombs to lay",ms);
        Run(j,pos,mark+sortie::kCarpetAlt,600.0f,want);
        return true;
    case SortiePass::popUp:
        *speed=kind.attack;
        if(s.phase==0) {
            if(along>300.0f){s.phase=1;s.phaseAt=ms;}   // past the mark with nothing let go: away all the same
            Run(j,pos,mark+sortie::kPopAlt,400.0f,want);
            return true;
        }
        want[0]=s.line.dir[0];want[1]=sortie::kPopClimb;want[2]=s.line.dir[2];Normalize(want);
        if(ms-s.phaseAt>sortie::kPopClimbMs)Done(j,s.fired ? "sortie: hit and away" : "sortie: no bombs to drop",ms);
        return true;
    case SortiePass::missileRain: {
        CountMissile(j);
        *speed=kind.attack;
        const float off[3]={pos[0]-s.line.at[0],0.0f,pos[2]-s.line.at[2]};
        const float dist=Len(off);
        if((dist<sortie::kRainTo && ms-s.begun>5000) || ms-s.begun>2*sortie::kSearchMs || (s.fired>0 && arms.missiles<=0)) {
            Done(j,s.fired ? "sortie: its missiles away" : "sortie: nothing to strike",ms);
            std::memcpy(want,nose,12);
            return true;
        }
        if(ground && dist<sortie::kRainFrom && arms.missiles>0) {
            NoseOn(pos,j.t.aim,mark+300.0f,want);
            *missileOk=MissileReach(kind,arms)>0.0f;
        } else Run(j,pos,mark+sortie::kRainAlt,600.0f,want);
        return true;
    }
    case SortiePass::gunshipLap: {
        // Its own circle and shells (Circle, GunshipFire); counted only once on its circle round the mark.
        const float off[3]={pos[0]-s.line.at[0],0.0f,pos[2]-s.line.at[2]};
        if(Len(off)<1500.0f)sortie::LapStep(s.lap,s.line.at,pos);
        const bool flown=sortie::LapDone(s.lap);
        if(!flown && ms-s.begun<=3*sortie::kSearchMs)return false;
        // Away from this frame: the kind's own circle (Circle) would set its mode back to patrol (jet.cpp Guide).
        Done(j,flown ? "sortie: its lap flown" : "sortie: its lap took too long",ms);
        std::memcpy(want,nose,12);*speed=kind.attack;
        return true;
    }
    default:
        return false;
    }
}

void SortieFire(Jet& j,const unsigned char* v,const float* pos,const Arms& arms,ULONGLONG ms) noexcept {
    Sortie& s=j.sortie;
    if(s.done || (s.pass!=SortiePass::carpet && s.pass!=SortiePass::popUp) || (s.pass==SortiePass::popUp && s.phase!=0))return;
    const float mark=s.line.at[1];
    if(pos[1]-mark<sortie::kPopAlt*0.6f)return;   // its own bombs' blast
    float at[3];sortie::Impact(pos,j.m.vel,mark,kG,at);
    const bool carpet=s.pass==SortiePass::carpet;
    if(carpet ? !sortie::InCarpet(s.line,at) : HorizDist(at,s.line.at)>sortie::kPopDrop)return;
    int bombs=0;
    for(int i=0;i<arms.storeCount;++i)if(arms.stores[i].spec->role==StoreRole::bomb)bombs+=arms.stores[i].ammo>0 ? arms.stores[i].ammo : 0;
    const ULONGLONG gap=carpet ? sortie::CarpetGapMs(bombs+s.fired,Len(j.m.vel)) : 150;
    if(s.fired && ms-s.bombAt<gap)return;
    for(int i=0;i<arms.storeCount;++i) {
        const Store& st=arms.stores[i];
        if(st.spec->role!=StoreRole::bomb || st.ammo<=0)continue;
        TriggerStore(st);
        s.bombAt=ms;++s.fired;
        Log("JET v=%p sortie: bomb %d away, %.0f m over the mark, landing %.0f m along",v,s.fired,pos[1]-mark,sortie::Along(s.line,at));
        if(!carpet && s.fired>=sortie::kPopBombs){s.phase=1;s.phaseAt=ms;}
        return;
    }
    if(!carpet && s.fired){s.phase=1;s.phaseAt=ms;}   // out of bombs before its second
}
}  // namespace jet
}  // namespace crew
