// The jets' sorties (docs/plan-2026-10-25-strikes.md §1.2; the user's strike list, 2026-10-25: 「打一轮就走」): a sortie
// call's jets (tools/calls.py `sortie`, its SupportAircraft::sortie: ActivateSupportAircraft, jet_spawn.cpp) come in from
// off the map through the support deployment as every call's, make one pass of their kind at the mark and withdraw
// (Away follows the sortie heading to the mission boundary, where the dispatcher deletes it with its crew). Their
// weapons are their own stores (the native rounds every machine sees), fired through jet_combat.cpp Fire as ever; what a
// sortie changes is the way in, what is struck and when it is over. Geometry: src/sortie_plan.h.
//   gunRun       the strike run (Strike: a shallow dive, rockets then guns) once: pulled out of its dive, it withdraws
//   missilePass  level kMissilePassAlt over the mark along its line at its attack speed, the nose put on each target
//                ahead in turn for its air-to-ground missiles (each at another target: SortieStruck), on past the mark
//   carpet       level kCarpetAlt over the mark along its line, its bombs let go to land within kCarpetHalf of it either
//                side, a gap apart over the run (SortieFire); a formation abreast lays parallel bands
//   popUp        in low (kPopAlt) at the mark, two bombs onto it, then a steep climb away
//   missileRain  approaches the mark at kRainAlt, holds its fire until over the target area with its nose settled
//                (sortie::RainHoldStep), then releases its magazine on its first native lock.
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

void SortieBegin(Jet& j,SortiePass pass,const float* mark,const float* pos,const float* heading,ULONGLONG ms,
                 const sortie::RunSpec& run) noexcept {
    Sortie& s=j.sortie;
    s=Sortie{};
    s.pass=pass;s.begun=ms;s.phaseAt=ms;
    const float explicitHeading[3]={run.x,0.0f,run.z};
    s.line=sortie::LineThrough(mark,pos,run.half>0.0f ? explicitHeading : heading);
    s.runHalf=run.half;
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
    if(j.sortie.done)return stock;
    if(p==SortiePass::missileRain)return sortie::kRainBurstMs;
    return p==SortiePass::missilePass ? sortie::kMissileGapMs : stock;
}

bool SortieGuide(Jet& j,const Kind& kind,const Arms& arms,const float* pos,const float* nose,const float* lead,float height,
                 ULONGLONG ms,float* want,float* speed,bool* gunsOk,bool* missileOk) noexcept {
    Sortie& s=j.sortie;
    if(s.pass==SortiePass::none || s.done)return false;
    const float along=sortie::Along(s.line,pos);
    const float mark=s.line.at[1];
    const bool ground=j.t.target && !j.t.flyer;
    s.strafing=false;
    switch(s.pass) {
    case SortiePass::gunRun:
        if(s.runHalf>0.0f) {
            const float attackHeight=mark+160.0f;
            *speed=kind.attack;
            Run(j,pos,attackHeight,550.0f,want);
            const float relative[3]={pos[0]-s.line.at[0],0.0f,pos[2]-s.line.at[2]};
            const float cross=std::fabs(sortie::Dot2(relative,s.line.side));
            if(s.phase==0 && along< -s.runHalf && cross<=65.0f && Dot(nose,s.line.dir)>0.7f)s.phase=1;
            if(s.phase==0 && ms-s.begun<=2*sortie::kSearchMs) {
                const float staging[3]={s.line.at[0]-s.line.dir[0]*(s.runHalf+650.0f),attackHeight,
                                        s.line.at[2]-s.line.dir[2]*(s.runHalf+650.0f)};
                const float onto[3]={staging[0]-pos[0],0.0f,staging[2]-pos[2]};
                Level(pos,onto,attackHeight,want);
                return true;
            }
            if(along>s.runHalf || ms-s.begun>2*sortie::kSearchMs) {
                Done(j,"sortie: marked strafe completed",ms);
                return true;
            }
            const float reach=GunReach(kind,arms);
            if(cross<=65.0f && along>=-s.runHalf-reach*0.6f) {
                const float point=Clamp(along+reach*0.65f,-s.runHalf,s.runHalf);
                const float space=point-along;
                if(space>kind.gunClose) {
                    for(int i=0;i<3;++i)s.shotPoint[i]=s.line.at[i]+s.line.dir[i]*point;
                    NoseOn(pos,s.shotPoint,mark+70.0f,want);
                    s.strafing=true;*gunsOk=true;
                }
                *missileOk=j.t.target && arms.missiles>0 && MissileReach(kind,arms)>0.0f;
            }
            return true;
        }
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
        if((s.fired>0 && arms.missiles<=0) ||
           along>sortie::kPassBeyond || (along<0.0f && ms-s.begun>2*sortie::kSearchMs)) {
            Done(j,s.fired ? "sortie: its pass made" : "sortie: nothing to strike",ms);
            Run(j,pos,mark+sortie::kMissilePassAlt,600.0f,want);
            return true;
        }
        const float toward=ground ? sortie::Along(s.line,j.t.aim)-along : 0.0f;
        const float to[3]={j.t.aim[0]-pos[0],j.t.aim[1]-pos[1],j.t.aim[2]-pos[2]};
        *gunsOk=j.t.target && Dot(to,nose)>0.0f && Len(to)<GunReach(kind,arms);
        if(ground && toward>200.0f && arms.missiles>0) {
            NoseOn(pos,j.t.aim,mark+120.0f,want);
            *missileOk=MissileReach(kind,arms)>0.0f;
        } else Run(j,pos,mark+sortie::kMissilePassAlt,600.0f,want);
        return true;
    }
    case SortiePass::carpet:
        *speed=kind.attack;
        if(along>(s.runHalf>0.0f ? s.runHalf : sortie::kCarpetHalf)+sortie::kPassBeyond ||
           ms-s.begun>2*sortie::kSearchMs)Done(j,s.fired ? "sortie: its bombs laid" : "sortie: no bombs to lay",ms);
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
        *speed=sortie::kRainSpeed;
        const float off[3]={pos[0]-s.line.at[0],0.0f,pos[2]-s.line.at[2]};
        const float dist=Len(off);
        const bool outsideWindow=dist<sortie::kRainTo || along>sortie::kRainTo || ms-s.begun>2*sortie::kSearchMs;
        if((s.fired>0 && arms.missiles<=0) || (outsideWindow && !sortie::SalvoDraining(ms,j.t.missileAt))) {
            Log("JET v=%p missile rain: leaving with %d missiles remaining",j.Vehicle(),arms.missiles);
            Done(j,arms.missiles<=0 ? "sortie: salvo complete" : "sortie: firing window ended",ms);
            std::memcpy(want,nose,12);
            return true;
        }
        if(j.t.target && dist<sortie::kRainFrom && !outsideWindow && arms.missiles>0) {
            NoseOn(pos,j.t.aim,mark+110.0f,want);
            // Held until it is over the target area with its nose settled (sortie::RainHoldStep).
            float n[3]={nose[0],nose[1],nose[2]};
            const float cosOff=Normalize(n) ? Dot(n,want) : -1.0f;
            const float noseOff=std::acos(cosOff>1.0f ? 1.0f : cosOff<-1.0f ? -1.0f : cosOff);
            const int was=s.phase;
            const bool open=sortie::RainHoldStep(s.phase,s.phaseAt,dist,noseOff,ms);
            if(open && was!=2)Log("JET v=%p missile rain: open fire %.0f m from the mark, nose %.2f rad off, %llu ms after it came",
                                  j.Vehicle(),dist,noseOff,ms-s.begun);
            *missileOk=open && arms.missileRange>0.0f;
        } else {
            if(s.phase==1)s.phase=0;   // off its aim: the hold starts again (an open pass stays open)
            Run(j,pos,mark+sortie::kRainAlt,600.0f,want);
        }
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
    const bool carpet=s.pass==SortiePass::carpet || (s.pass==SortiePass::gunRun && s.runHalf>0.0f);
    if(s.done || (!carpet && s.pass!=SortiePass::popUp) || (s.pass==SortiePass::popUp && s.phase!=0))return;
    if(s.bombWeapon) {
        for(int i=0;i<arms.storeCount;++i) {
            const Store& st=arms.stores[i];
            if(st.weapon!=s.bombWeapon || st.ammo>=s.bombAmmo)continue;
            s.fired+=s.bombAmmo-st.ammo;
            s.bombAt=ms;
            Log("JET v=%p sortie: %d bombs confirmed released",v,s.fired);
            break;
        }
        s.bombWeapon=nullptr;
    }
    if(!carpet && s.fired>=sortie::kPopBombs){s.phase=1;s.phaseAt=ms;return;}
    const float mark=s.line.at[1];
    if(pos[1]-mark<sortie::kPopAlt*0.6f)return;   // its own bombs' blast
    float at[3];sortie::Impact(pos,j.m.vel,mark,kG,at);
    const float half=s.runHalf>0.0f ? s.runHalf : sortie::kCarpetHalf;
    if(carpet ? std::fabs(sortie::Along(s.line,at))>half : HorizDist(at,s.line.at)>sortie::kPopDrop)return;
    if(s.runHalf>0.0f) {
        const float offset[3]={at[0]-s.line.at[0],0.0f,at[2]-s.line.at[2]};
        if(std::fabs(sortie::Dot2(offset,s.line.side))>65.0f)return;
    }
    int bombs=0;
    for(int i=0;i<arms.storeCount;++i)if(arms.stores[i].spec->role==StoreRole::bomb)bombs+=arms.stores[i].ammo>0 ? arms.stores[i].ammo : 0;
    const ULONGLONG gap=carpet ? sortie::CarpetGapMs(bombs+s.fired,Len(j.m.vel),half) : 150;
    if(s.fired && ms-s.bombAt<gap)return;
    for(int i=0;i<arms.storeCount;++i) {
        const Store& st=arms.stores[i];
        if(st.spec->role!=StoreRole::bomb || st.ammo<=0)continue;
        TriggerStore(st);
        s.bombWeapon=st.weapon;s.bombAmmo=st.ammo;
        return;
    }
    if(!carpet && s.fired){s.phase=1;s.phaseAt=ms;}   // out of bombs before its second
}
}  // namespace jet
}  // namespace crew
