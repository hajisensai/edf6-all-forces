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
constexpr float kBombReach=4000.0f;   // m a bomb's path is followed for its landing (the carpet's: ~1.6 km from 260 m up)
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
// The strafe's nose on `aim`: the guns and rockets fire along the nose, which rides the angle of attack above the path,
// so the path is aimed under the line to it by the nose's offset (as jet_combat.cpp GunToward); no steeper than
// kNoseDown, and up off it below `floorY`.
void StrafeNose(const Jet& j,const float* pos,const float* nose,const float* aim,float floorY,float* want) noexcept {
    Toward(pos,aim,want);
    float path[3]={j.m.vel[0],j.m.vel[1],j.m.vel[2]};
    if(Normalize(path)) {
        float aimed[3];
        for(int i=0;i<3;++i)aimed[i]=want[i]-(nose[i]-path[i]);
        if(Normalize(aimed))std::memcpy(want,aimed,12);
    }
    if(want[1]<kNoseDown)want[1]=kNoseDown;
    if(pos[1]<floorY && want[1]<0.15f)want[1]=0.15f;
    Normalize(want);
}
// The marked strafe (a gun run along its line, sortie_plan.h; the user, 2026-11-26: 「需要绕一或n圈才能进入航线，应该直接
// 进入航线才对」「只有机枪」「扫射时间是不是不对」). In along the line from its entry (support_entry.h AirRoute: on the line
// produced beyond its start, heading along it) at the height (StrafeHeight) its guns' lead lasts the line from. Its nose
// goes onto the point its lead on kStrafeSettleS before that point reaches the line's start (the jet on the line by then:
// kStrafeCross, kStrafeAngle), so it is pitched down and steady as the window opens; from there the point walks down the
// line at its own speed (2 half / speed s: StrafeSeconds), guns, rockets and missiles (Fire: by its stores) on it, to the
// line's end. Off the line it turns onto it while it can (CaptureDistance at its hardest turn); a real miss (it cannot be
// on it in time, or heads away) comes round once on a teardrop to the line produced, never a lap; a second miss, or too
// long, ends the pass.
//   phase 0  in toward the window     1  the strafe     2  the teardrop out     3  turning in onto the line
void MarkedRun(Jet& j,const Kind& kind,const Arms& arms,const float* pos,const float* nose,float along,ULONGLONG ms,
               float* want,float* speed,bool* gunsOk,bool* missileOk) noexcept {
    Sortie& s=j.sortie;
    const sortie::Line& l=s.line;
    const float mark=l.at[1],half=s.runHalf;
    const float settle=kind.attack*sortie::kStrafeSettleS,run=half+settle*0.5f;   // the line and its settling, halved
    const float least=kind.gunClose*2.6f,reach=GunReach(kind,arms);
    const float preferred=std::fmax(least,reach*0.9f);
    const float inHeight=mark+sortie::StrafeHeight(run,preferred);
    // Its turns: onto the line at its hardest (maxG, a short S), round the teardrop at the g it holds (TurnRadiusOf).
    const float mass=j.burden.mass>1.0f ? j.burden.mass : 1.0f,hard=kind.maxG/mass;
    const float sharp=airbound::TurnRadius(kind.attack,std::sqrt(hard*hard>1.0f ? hard*hard-1.0f : 0.25f));
    const float wide=TurnRadiusOf(kind,j.burden.mass,kind.attack);
    const float capture=std::fmax(sharp,900.0f);   // m ahead on the line it steers for, off it
    const float relative[3]={pos[0]-l.at[0],0.0f,pos[2]-l.at[2]};
    const float cross=sortie::Dot2(relative,l.side);
    const float angle=std::atan2(sortie::Dot2(nose,l.side),sortie::Dot2(nose,l.dir));
    *speed=kind.attack;
    if(s.phase!=1 && ms-s.phaseAt>2*sortie::kSearchMs) {
        Done(j,"sortie: its strafe's line never reached",ms);
        Level(pos,nose,inHeight,want);
        return;
    }
    const auto missed=[&](const char* why,float need,float room) noexcept {
        if(s.reentered) {
            Log("JET v=%p sortie: strafe line missed again (%s: %.0f m off, %.0f deg, %.0f m to turn in, %.0f m left)",j.Vehicle(),
                why,cross,angle*180.0f/kPi,need,room);
            Done(j,"sortie: its strafe's line missed twice",ms);
            Level(pos,nose,inHeight,want);
            return true;
        }
        s.phase=2;s.phaseAt=ms;s.reenterSide=cross<0.0f ? -1.0f : 1.0f;
        Log("JET v=%p sortie: strafe line missed (%s: %.0f m off, %.0f deg, %.0f m to turn in, %.0f m left): coming round once",
            j.Vehicle(),why,cross,angle*180.0f/kPi,need,room);
        return false;
    };
    if(s.phase==0) {
        const float over=std::fmax(pos[1]-mark,sortie::kStrafeFloor*1.1f);
        const float lead=sortie::StrafeLead(run,over,least,sortie::kStrafeLeadMost);
        const float room=-half-settle-lead-along;   // m on before its nose goes down onto the point
        // What it needs to be on it (within kStrafeCross of it, along it): nothing once there.
        const bool onLine=std::fabs(cross)<=sortie::kStrafeCross && std::fabs(angle)<=sortie::kStrafeAngle;
        const float beside=std::fabs(cross)>sortie::kStrafeCross ? cross-(cross>0.0f ? sortie::kStrafeCross : -sortie::kStrafeCross) : 0.0f;
        const float need=onLine ? 0.0f : sortie::CaptureDistance(beside,angle,sharp);
        if(room<=0.0f && onLine) {
            s.phase=1;s.phaseAt=ms;s.strafeLead=lead;
            Log("JET v=%p sortie: strafe settling, lead %.0f m from %.0f m up, %.0f m off at %.0f deg, %.1f s down a %.0f m line",
                j.Vehicle(),lead,pos[1]-mark,cross,angle*180.0f/kPi,sortie::StrafeSeconds(half,kind.attack),2.0f*half);
        } else if(room<= -settle) {
            if(missed("off the line at its start",need,room))return;
        } else if(need>room+settle || Dot(nose,l.dir)<=0.0f) {
            if(missed(need>=1e8f ? "heading away" : "no room to turn onto it",need,room))return;
        } else {
            Run(j,pos,inHeight,capture,want);
            return;
        }
    }
    if(s.phase==1) {
        if(along>half || (sortie::StrafeOver(along,s.strafeLead,half) && along>half-kind.gunClose*1.5f)) {
            Done(j,"sortie: marked strafe completed",ms);
            Run(j,pos,pos[1],600.0f,want);
            return;
        }
        // The point its lead on, held at the line's end once there (a long line's last guns closing on it); short of the
        // line's start (settling) the nose on it, no fire.
        const float point=std::fmin(along+s.strafeLead,half);
        for(int i=0;i<3;++i)s.shotPoint[i]=l.at[i]+l.dir[i]*point;
        StrafeNose(j,pos,nose,s.shotPoint,mark+sortie::kStrafeFloor*0.6f,want);
        s.strafing=true;*gunsOk=sortie::StrafeOpen(along,s.strafeLead,half);
        *missileOk=j.t.target && !j.t.flyer && arms.missiles>0 && MissileReach(kind,arms)>0.0f;
        return;
    }
    if(s.phase==2) {
        // Out beside the line, back past its start, far enough out to turn in and settle onto it (a teardrop).
        const float back=settle+preferred+2.5f*wide,aside=2.0f*wide*s.reenterSide;
        const float out[3]={l.at[0]-l.dir[0]*(half+back)+l.side[0]*aside,inHeight,l.at[2]-l.dir[2]*(half+back)+l.side[2]*aside};
        const float onto[3]={out[0]-pos[0],0.0f,out[2]-pos[2]};
        if(sortie::Dot2(onto,onto)<wide*wide || along<-half-back){s.phase=3;s.phaseAt=ms;}
        else {
            Level(pos,onto,inHeight,want);
            return;
        }
    }
    // Phase 3: round onto the line; along it again, in as at first (one more miss ends the pass).
    if(Dot(nose,l.dir)>0.7f){s.phase=0;s.reentered=true;}
    Run(j,pos,inHeight,capture,want);
}
}  // namespace

void SortieBegin(Jet& j,SortiePass pass,const float* mark,const float* pos,const float* heading,ULONGLONG ms,
                 const sortie::RunSpec& run) noexcept {
    Sortie& s=j.sortie;
    s=Sortie{};
    s.pass=pass;s.begun=ms;s.phaseAt=ms;
    const float explicitHeading[3]={run.x,0.0f,run.z};
    s.line=sortie::LineThrough(mark,pos,sortie::HasHeading(run) ? explicitHeading : heading);
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

// A marked strafe (a gun run along a line) passes its targets in a few seconds: its missiles go on a sortie's dwell and
// gap, not the dogfight's 2.5 s each (which let at most one go a pass).
bool MarkedStrafe(const Sortie& s) noexcept { return s.pass==SortiePass::gunRun && s.runHalf>0.0f; }
ULONGLONG SortieLockMs(const Jet& j,ULONGLONG stock) noexcept {
    const SortiePass p=j.sortie.pass;
    return !j.sortie.done && (p==SortiePass::missilePass || p==SortiePass::missileRain || MarkedStrafe(j.sortie)) ?
           sortie::kLockMs : stock;
}
ULONGLONG SortieMissileGapMs(const Jet& j,ULONGLONG stock) noexcept {
    const SortiePass p=j.sortie.pass;
    if(j.sortie.done)return stock;
    if(p==SortiePass::missileRain)return sortie::kRainBurstMs;
    return p==SortiePass::missilePass || MarkedStrafe(j.sortie) ? sortie::kMissileGapMs : stock;
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
            MarkedRun(j,kind,arms,pos,nose,along,ms,want,speed,gunsOk,missileOk);
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
    // The bomb it would let go now and where that lands as the game flies it (StoreLands: the world's gravity, 14.7 m/s^2,
    // x the bomb's factor, its eject and the share of the jet's velocity it takes; the ground or a building under it). It
    // reckoned 9.8 m/s^2 onto the mark's height before (the user, 2026-11-27: 「炸弹也很难投中」): every bomb fell ~22%
    // sooner than it planned, ~300 m short from the carpet's height at its speed, wide of a 150 m window.
    const Store* bomb=nullptr;
    int bombs=0;
    for(int i=0;i<arms.storeCount;++i) {
        const Store& st=arms.stores[i];
        if(st.spec->role!=StoreRole::bomb || st.ammo<=0)continue;
        bombs+=st.ammo;
        if(!bomb)bomb=&st;
    }
    if(!bomb){if(!carpet && s.fired){s.phase=1;s.phaseAt=ms;}return;}   // out of bombs before its second
    float at[3];
    if(!StoreLands(*bomb,kBombReach,at))return;   // none lands within reach yet: held
    const float half=s.runHalf>0.0f ? s.runHalf : sortie::kCarpetHalf;
    if(carpet ? std::fabs(sortie::Along(s.line,at))>half : HorizDist(at,s.line.at)>sortie::kPopDrop)return;
    if(s.runHalf>0.0f) {
        const float offset[3]={at[0]-s.line.at[0],0.0f,at[2]-s.line.at[2]};
        if(std::fabs(sortie::Dot2(offset,s.line.side))>65.0f)return;
    }
    const ULONGLONG gap=carpet ? sortie::CarpetGapMs(bombs+s.fired,Len(j.m.vel),half) : 150;
    if(s.fired && ms-s.bombAt<gap)return;
    TriggerStore(*bomb);
    s.bombWeapon=bomb->weapon;s.bombAmmo=bomb->ammo;
}
}  // namespace jet
}  // namespace crew
