// NPC jets' fighting (jet.cpp): the target a jet picks, its guns' and missiles' attack runs (Strike, Chase,
// Missile) and the fire gate every weapon of a jet goes through (WeaponsFree).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "jet_internal.h"

namespace crew {
namespace jet {
namespace {
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10;
constexpr std::size_t kWeaponLockon=0x6B0,kWeaponSpeed=0x894,kWeaponAlive=0x898,kWeaponGravity=0x8E0,kWeaponAmmo=0xBE8;
constexpr std::int32_t kHoming=1;
// The homing weapon's lock-on (Weapon_VehicleShoot: its lock tick 0x6963A0 runs for AI riders too; the
// test 0x22DF30 wants the target within LockonRange and LockonAngle of the arms bone); unlocked, its rounds fly
// straight on (MissileBullet01 mode 1 steers only at a target the lock list gave it). The jets' missile
// (pylib/vcobjects.py STORES) sets its own range, cone and lock time; a jet fires it within that
// range (MissileReach) only once the game has a target in its lock list (kWeaponLocked), which it keeps
// HoldTime (600 frames) nose or not. (Until 2026-10-04 the plugin raised the stock 500 m lock at run time to
// each role's missileRange: the weapon's own settings said one thing and the jets did another.)
constexpr std::size_t kWeaponLockRange=0x6D0,kWeaponLocked=0xC68;
constexpr std::size_t kWeaponBlast=0x830+0x80;   // AmmoExplosion (InitParam +0x80; core +0xA20 = 0x9A0 + 0x80)
// The guns are seat weapons 0 and 1 (what 0x2020 fires, testrange/gen.py); after the missile gen.py puts
// the 506's fuel tank (v_fuel01, its "ammo" ~1e6 burnt by the throttle), which is no gun.
constexpr std::uint64_t kGunWeapons=2;
// Strike: approach at alt; from diveStart out with the target within kDiveCone of the nose, dive onto the
// gun's lead point (alt over diveStart: ~15-20 deg), guns from gunOpen in to gunClose; pull out under
// pullAlt over the target or gunClose from it, climb back, fly on extendOut and turn in.
// kDiveCone: the heading off the target it may roll in with (11 deg). At 30 deg (until 2026-10-05) jets of a flight
// arriving together rolled in from 350-550 m with the nose 15-30 deg off and pulled out before it came on: only the
// one lined up from 700 m fired. Not lined up, it turns on toward the target, or flies out and comes round.
constexpr float kDiveCone=0.19f;
constexpr float kGunCone=0.035f,kHitRadius=4.0f;   // rad (2 deg), or what puts kHitRadius on the target
// The guns fire only flying where the nose points (cos 10 deg off): never flank first. And never with the
// player along the rounds' path, or a wingman when its rounds cannot pass through (FriendInLine); other
// friends are hit as the stock game hits them.
constexpr float kGunSlip=0.985f;
// Missiles (Missile, Fire): fired with the nose within kMissileCone of the target (its lock point, not
// the guns' lead) for kLockMs (the weapon's LockonTime, 30 frames, and a margin), from kMissileMin out to
// the role's missileRange; kSalvoMs later (a salvo is 4 rounds 10 frames apart) it cranks kCrankAngle off
// the line for kCrankMs, and closer than kStandoffIn of its range it turns away (kTurnAwayAngle) the same.
// (Until 2026-10-03 the fighter fired only inside 500 m, the nose on the guns' lead point, while flying a
// gun pass at the target: the missiles went at 430 m and it closed in to 120 m all the same.)
constexpr float kMissileCone=0.15f,kMissileMin=150.0f,kStandoffIn=0.45f,kCrankAngle=1.2f,kTurnAwayAngle=2.6f;
// kLockMs: the nose held on the target this long before a missile goes (the user, 2026-10-05: the lock should take
// longer; it was 0.7 s): the player hears the lock warning (playerjet.cpp ThreatTone) that long first.
constexpr ULONGLONG kMissileMs=2500,kLockMs=2500,kSalvoMs=900,kCrankMs=4500,kPullMs=7000,kExtendMs=12000;
// Missiles with no lock: once the game has had nothing in the missile's lock list for kNoLockMs while
// the jet stood off, it goes in with the guns for kGunSpellMs, then tries the missiles again.
constexpr ULONGLONG kNoLockMs=10000,kGunSpellMs=15000;
// Fighter: lead pursuit at the target's speed plus chaseOver; closer than overrun it breaks off
// (extends kRunOutMs) so it does not ram or sit on its tail. With the lead more than kCornerFrom off the nose it
// turns at its corner speed (kCornerShare of the way from minSpeed to attack): the same g turns it faster and
// tighter there (a fighter at 235 m/s pulls 5 g on a 1150 m circle, 12 deg/s; two of them circled each other
// with the lead 52 deg off the nose, 2 gun bursts in 409 looks, 2026-10-05).
constexpr ULONGLONG kRunOutMs=3000;
constexpr float kCornerFrom=0.35f,kCornerShare=0.45f;
// A flyer: its root (object+kPosition: a walker's feet, however tall it is) more than kFlyerClear over the
// ground found by a ray from kFlyerProbe over it. (By its lock point the queen ant, whose lock points
// are high on its body, flew: fighters made gun passes at it, back and forth over it, 2026-10-03.)
constexpr float kFlyerClear=15.0f,kFlyerProbe=30.0f;
constexpr ULONGLONG kFlyerMemoMs=500;
// The current target counts as kKeepScale of its distance less kKeepTarget: another must be much nearer
// to take its place (a fighter switched every 11 s among 32 in one fight, turning hard each time).
constexpr float kKeepScale=0.6f,kKeepTarget=100.0f;

// The target: as the role prefers (Kind::prefer), nearest to the jet among those within `range` of
// `anchor`, the current one counting nearer (kKeepScale, kKeepTarget).
struct Pick { Jet* j; const float* pos; const float* anchor; float range; ULONGLONG ms; const void* best; float score,aim[3]; bool flyer; };
// Whether `object` (lock point `p`) flies: its root more than kFlyerClear over the ground, or no ground
// under it (see kFlyerProbe); one ray per object per kFlyerMemoMs, shared by every jet.
// The memo: kFlyerSets sets of kFlyerWays, an object's set picked by a multiplicative hash of its address (the
// game allocates objects at a fixed stride: their low address bits alone put whole waves of targets in one slot,
// which then evicted each other on every look, re-probed and re-logged each frame); a new object takes its set's
// least recently looked at way.
struct FlyerMemo { const void* object; ULONGLONG at; bool flyer; };
constexpr int kFlyerSets=128,kFlyerWays=4;
FlyerMemo flyerMemo[kFlyerSets][kFlyerWays]{};
FlyerMemo& FlyerSlot(const void* object,bool* first) noexcept {
    const std::uint64_t h=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(object))*0x9E3779B97F4A7C15ull;
    FlyerMemo* const set=flyerMemo[h>>57];   // the top 7 bits: kFlyerSets
    FlyerMemo* oldest=&set[0];
    for(int i=0;i<kFlyerWays;++i) {
        if(set[i].object==object){*first=false;return set[i];}
        if(set[i].at<oldest->at)oldest=&set[i];
    }
    *first=true;
    return *oldest;
}
bool Flies(const void* object,const float* p,ULONGLONG ms) noexcept {
    bool first=false;
    auto& m=FlyerSlot(object,&first);
    if(!first && ms-m.at<kFlyerMemoMs)return m.flyer;
    const auto o=static_cast<const unsigned char*>(object);
    const float* root=Readable(o+kPosition,12) ? reinterpret_cast<const float*>(o+kPosition) : p;
    const float off[3]={root[0]-p[0],root[1]-p[1],root[2]-p[2]};
    if(!std::isfinite(Dot(off,off)) || Dot(off,off)>300.0f*300.0f)root=p;   // not where its lock point is
    const float top[3]={root[0],root[1]+kFlyerProbe,root[2]},bottom[3]={root[0],root[1]-400.0f,root[2]};
    float hit[3];
    const bool ground=MapRay(top,bottom,hit)>=0.0f;
    m={object,ms,!ground || root[1]-hit[1]>kFlyerClear};
    if(first && Cfg().debug)Log("JET target %p: root y=%.0f, lock point y=%.0f, ground %s: %s",object,root[1],p[1],
                              ground ? "under it" : "none seen",m.flyer ? "flies" : "on the ground");
    return m.flyer;
}

// A target it lets be for its edge (airbound.h, jet_flight.cpp SoftEdge): flying back in from past its soft edge,
// anything out past that (the one it chased out there too, the user 2026-10-06: "过了这个小边界会往回走"); else anything out
// past the play edge, where it never goes.
bool PastEdge(const Jet& j,const float* p) noexcept {
    return !airbound::Inside(j.m.edgeBack ? JetSoftBox(j) : airbound::Square(PlayEdge()),p);
}

void VisitTarget(void* ctx,const void* object,const float* p) noexcept {
    auto& k=*static_cast<Pick*>(ctx);
    if(PastEdge(*k.j,p))return;
    const float d[3]={p[0]-k.anchor[0],p[1]-k.anchor[1],p[2]-k.anchor[2]};
    // New targets only within the range of the anchor; the current one is chased wherever it goes.
    if(object!=k.j->t.target && Dot(d,d)>k.range*k.range)return;
    const bool flyer=Flies(object,p,k.ms);
    const float f[3]={p[0]-k.pos[0],p[1]-k.pos[1],p[2]-k.pos[2]};
    float score=Len(f);
    if(object==k.j->t.target)score=score*kKeepScale-kKeepTarget;
    const Prefer prefer=KindOf(*k.j).prefer;
    if(prefer!=Prefer::any && flyer!=(prefer==Prefer::air))score+=2000.0f;   // the other kind: only with none of its own
    if(!k.best || score<k.score){k.best=object;k.score=score;std::memcpy(k.aim,p,12);k.flyer=flyer;}
}

// Whether `at` lies inside the circle the jet turns on toward it (level, at its kind's g, at its speed):
// it cannot bring the nose onto it without flying out first.
bool InsideTurn(const Jet& j,const float* pos,const float* at) noexcept {
    const Kind& k=KindOf(j);
    const float s=Len(j.m.vel),r=s*s/(kG*std::sqrt(k.maxG*k.maxG-1.0f));
    float v[3]={j.m.vel[0],0,j.m.vel[2]};
    if(!Normalize(v))return false;
    const float to[3]={at[0]-pos[0],0,at[2]-pos[2]};
    const float side=to[0]*v[2]-to[2]*v[0]>=0.0f ? 1.0f : -1.0f;   // the target off the (v.z,-v.x) side or not
    const float c[3]={pos[0]+v[2]*side*r-at[0],0,pos[2]-v[0]*side*r-at[2]};
    return Dot(c,c)<r*r;
}

// Where to steer for the guns: the nose onto `lead`, not the flight path. The guns fire along the nose, which rides
// the angle of attack above the path (JetSteer); steering the path at the lead kept the nose that far off it (a
// strike dive held 2.0-2.4 deg off with the gate at 2 deg: half its bursts, 2026-10-05). The path is aimed under
// the line to the lead by the nose's offset from it now.
void GunToward(const Jet& j,const float* pos,const float* nose,const float* lead,float* want) noexcept {
    Toward(pos,lead,want);
    float path[3]={j.m.vel[0],j.m.vel[1],j.m.vel[2]};
    if(!Normalize(path))return;
    float aimed[3];
    for(int i=0;i<3;++i)aimed[i]=want[i]-(nose[i]-path[i]);
    if(Normalize(aimed))std::memcpy(want,aimed,12);
}

// The lead point seen from `pos`: nothing standing between (a building's face, a ridge) more than kSightSlack short
// of it. A dive at one behind a building was a dive into the building.
constexpr float kSightSlack=30.0f;
bool InSight(const float* pos,const float* lead) noexcept {
    float hit[3];
    if(MapRay(pos,lead,hit)<0.0f)return true;
    const float d[3]={hit[0]-lead[0],hit[1]-lead[1],hit[2]-lead[2]};
    return Len(d)<kSightSlack;
}

// Strike attack (see kDiveCone). Returns whether the guns may fire (diving at the lead point).
bool Strike(Jet& j,const float* pos,const float* nose,const float* lead,float height,ULONGLONG ms,float* want,float* speed) noexcept {
    const Kind& k=KindOf(j);
    const float dh=HorizDist(pos,lead),over=pos[1]-lead[1];
    const float to[3]={lead[0]-pos[0],0,lead[2]-pos[2]};
    float vdir[3]={j.m.vel[0],0,j.m.vel[2]};
    if(!Normalize(vdir)){vdir[0]=to[0];vdir[2]=to[2];Normalize(vdir);}
    float toN[3]={to[0],0,to[2]};Normalize(toN);
    const float off=std::acos(Clamp(Dot(vdir,toN),-1.0f,1.0f));
    *speed=k.attack;
    switch(j.mode) {
    case Mode::dive:
        if(over<k.pullAlt || dh<k.gunClose*0.7f || Len(to)<k.gunClose){SetMode(j,Mode::pull,ms);break;}
        GunToward(j,pos,nose,lead,want);
        return true;
    case Mode::pull:
        if(pos[1]>=height-20.0f || ms-j.modeAt>kPullMs) {
            std::memcpy(j.t.out,vdir,12);SetMode(j,Mode::extend,ms);break;
        }
        want[0]=vdir[0];want[1]=0.7f;want[2]=vdir[2];Normalize(want);
        return false;
    case Mode::extend:
        if(dh>k.extendOut || ms-j.modeAt>kExtendMs){SetMode(j,Mode::approach,ms);break;}
        Level(pos,j.t.out,height,want);
        return false;
    default:
        if(j.mode!=Mode::approach)SetMode(j,Mode::approach,ms);
        break;
    }
    // Approach: at the target at height; dive once in the window with the lead in sight, else fly out and come round.
    // Arriving (Entering) it comes straight on at the height it came at and its attack speed, never flying out first.
    // Its arrival ends where it cannot come straight on: the target inside its turn (it would circle it), or too low to
    // dive (the height it came at kept, it never would): the approach's climb and fly-out as ever.
    bool entering=Entering(j,ms);
    if(entering && ((off>kDiveCone && InsideTurn(j,pos,lead)) || over<=k.pullAlt+30.0f)){j.entered=true;entering=false;}
    if(dh<=k.diveStart && dh>k.gunClose*2.0f && off<kDiveCone && over>k.pullAlt+30.0f && InSight(pos,lead)) {
        j.entered=true;SetMode(j,Mode::dive,ms);GunToward(j,pos,nose,lead,want);return true;
    }
    if(!entering && off>kDiveCone && InsideTurn(j,pos,lead)){std::memcpy(j.t.out,vdir,12);SetMode(j,Mode::extend,ms);Level(pos,vdir,height,want);return false;}
    Level(pos,to,entering ? pos[1] : height,want);
    *speed=entering ? k.attack : k.cruise;
    return false;
}

// Air-to-air: lead pursuit; breaks off when it overruns.
bool Chase(Jet& j,const float* pos,const float* nose,const float* lead,ULONGLONG ms,float* want,float* speed) noexcept {
    const Kind& k=KindOf(j);
    const float d[3]={lead[0]-pos[0],lead[1]-pos[1],lead[2]-pos[2]};
    if(j.mode==Mode::runOut) {
        if(ms-j.modeAt<kRunOutMs){std::memcpy(want,j.t.out,12);*speed=k.attack;return false;}
        SetMode(j,Mode::chase,ms);
    }
    if(j.mode!=Mode::chase)SetMode(j,Mode::chase,ms);
    if(Len(d)<k.overrun) {
        float dir[3]={j.m.vel[0],j.m.vel[1]+Len(j.m.vel)*0.3f,j.m.vel[2]};
        if(!Normalize(dir)){dir[0]=0;dir[1]=0.3f;dir[2]=1;Normalize(dir);}
        std::memcpy(j.t.out,dir,12);SetMode(j,Mode::runOut,ms);
        std::memcpy(want,dir,12);*speed=k.attack;return false;
    }
    GunToward(j,pos,nose,lead,want);
    const float dist=Len(d),off=dist>1.0f ? std::acos(Clamp(Dot(d,nose)/dist,-1.0f,1.0f)) : 0.0f;
    const float corner=k.minSpeed+(k.attack-k.minSpeed)*kCornerShare;
    *speed=off>kCornerFrom ? corner : Clamp(Len(j.t.tgtVel)+k.chaseOver,k.minSpeed+20.0f,k.attack);
    return true;
}

// Missiles from standoff (see kMissileCone): at the target until it is within missileRange (level at its
// height over a ground target), then the nose on it, which Fire locks and fires on; once its salvo is
// away (kSalvoMs), or nearer than kStandoffIn of the range, it cranks: level, kCrankAngle (kTurnAwayAngle)
// off the line to the target on the side it flies, for kCrankMs, and comes round again.
void Missile(Jet& j,const float* pos,float height,float range,ULONGLONG ms,float* want,float* speed) noexcept {
    const Kind& k=KindOf(j);
    const float d[3]={j.t.aim[0]-pos[0],j.t.aim[1]-pos[1],j.t.aim[2]-pos[2]};
    const float dist=Len(d),level=j.t.flyer ? j.t.aim[1] : height;
    *speed=k.attack;
    if(j.mode==Mode::crank && ms-j.modeAt<kCrankMs){Level(pos,j.t.out,level,want);return;}
    if(j.mode!=Mode::missile)SetMode(j,Mode::missile,ms);
    const bool fired=j.t.missileAt>=j.modeAt && ms-j.t.missileAt>kSalvoMs,close=dist<range*kStandoffIn;
    if(fired || close) {
        float to[3]={d[0],0.0f,d[2]};
        if(!Normalize(to)){to[0]=0;to[2]=1;}
        const float side=j.m.vel[0]*to[2]-j.m.vel[2]*to[0]>=0.0f ? 1.0f : -1.0f;   // flying off the (to.z,-to.x) side or not
        const float a=close ? kTurnAwayAngle : kCrankAngle,c=std::cos(a),s=std::sin(a)*side;
        j.t.out[0]=to[0]*c+to[2]*s;j.t.out[1]=0.0f;j.t.out[2]=to[2]*c-to[0]*s;
        SetMode(j,Mode::crank,ms);
        Level(pos,j.t.out,level,want);
        return;
    }
    if(dist>range && !j.t.flyer){Level(pos,d,height,want);return;}
    Toward(pos,j.t.aim,want);
}
}  // namespace

Arms ReadArms(unsigned char* v) noexcept {
    Arms a{};
    a.gunSpeed=240.0f;a.gunRange=400.0f;a.pick=-1;
    if(SeatCount(v)==0)return a;
    const auto seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>8 || !Readable(holders,count*8))return a;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kWeaponLocked+8))continue;
        const std::int32_t ammo=At<std::int32_t>(w,kWeaponAmmo);
        if(At<std::int32_t>(w,kWeaponLockon)==kHoming) {
            a.hasMissile=true;a.missiles+=ammo>0 ? ammo : 0;
            const auto locked=At<std::uint64_t>(w,kWeaponLocked);
            a.locked+=locked<64 ? static_cast<std::int32_t>(locked) : 0;
            const float lock=At<float>(w,kWeaponLockRange);
            if(std::isfinite(lock) && lock>a.missileRange)a.missileRange=lock;
            continue;
        }
        const float speed=At<float>(w,kWeaponSpeed)*60.0f,reach=At<float>(w,kWeaponSpeed)*static_cast<float>(At<std::int32_t>(w,kWeaponAlive));
        if(i>=kGunWeapons || !std::isfinite(speed) || speed<=1.0f)continue;
        a.guns+=ammo>0 ? ammo : 0;
        if(!a.hasGun || speed>a.gunSpeed) {
            a.hasGun=true;a.gunSpeed=speed;
            const float g=At<float>(w,kWeaponGravity);
            a.gunGravity=std::isfinite(g) && g>0.0f ? g : 0.0f;
            a.gunRange=std::isfinite(reach) && reach>0.0f ? reach : 400.0f;
        }
    }
    a.storeCount=ReadStores(v,a.stores,kMostStores);
    a.rocket=-1;
    for(int i=0;i<a.storeCount;++i) {
        if(a.stores[i].spec->role==StoreRole::bomb)a.bombs+=a.stores[i].ammo;
        if(a.stores[i].spec->role==StoreRole::rocket && a.stores[i].ammo>0 && a.rocket<0)a.rocket=i;
    }
    return a;
}

void PickStore(Arms& a,bool flyer,float dist) noexcept {
    if(a.storeCount==0)return;   // no stores known: every homing weapon, the 506's own fire byte
    const StoreRole want=flyer ? StoreRole::air : StoreRole::ground;
    int best=-1;
    for(int i=0;i<a.storeCount;++i) {
        const Store& s=a.stores[i];
        if(s.spec->role!=want || s.ammo<=0 || s.lockRange<=0.0f)continue;
        if(best<0){best=i;continue;}
        const Store& b=a.stores[best];
        const bool reaches=s.lockRange>=dist,bestReaches=b.lockRange>=dist;
        if(reaches!=bestReaches ? reaches : reaches ? s.lockRange<b.lockRange : s.lockRange>b.lockRange)best=i;
    }
    a.pick=best;
    a.missiles=best<0 ? 0 : a.stores[best].ammo;
    a.locked=best<0 ? 0 : a.stores[best].locked;
    a.missileRange=best<0 ? 0.0f : a.stores[best].lockRange;
    a.hasMissile=best>=0;
}

// Where to point the guns to hit `aim` moving at `tv` from `from` (round flight time and drop). Farther
// than the guns reach it leads by their reach's flight time only: at 900 m the rounds' 3.75 s times the
// target's speed (as noisy as its lock point bobs) swung the jet's heading from side to side.
void Lead(const float* from,const float* aim,const float* tv,const Arms& a,float* out) noexcept {
    std::memcpy(out,aim,12);
    for(int pass=0;pass<2;++pass) {
        const float d[3]={out[0]-from[0],out[1]-from[1],out[2]-from[2]};
        const float l=Len(d),t=(l<a.gunRange ? l : a.gunRange)/a.gunSpeed;
        for(int i=0;i<3;++i)out[i]=aim[i]+tv[i]*t;
        out[1]+=0.5f*a.gunGravity*kGravity*t*t;
    }
}

void PickTarget(Jet& j,unsigned char* v,const float* pos,const float* anchor,float range,float dt,ULONGLONG ms) noexcept {
    Pick pick{&j,pos,anchor,range,ms,nullptr,0.0f,{},false};
    VisitEnemies(v,&VisitTarget,&pick);
    Aim& t=j.t;
    if(!pick.best){t.target=nullptr;return;}
    // The velocity from the target's move over one frame (`dt`, this frame's game step) only: a call after skipped frames
    // (a gun's gate, a phase its caller sits out) divided several frames' move by one (CrewShell's half second: 30 times
    // too fast), a second call in a frame a move of 0. Either keeps the velocity it had; a new target starts it at 0.
    const bool same=pick.best==t.target;
    const ULONGLONG frame=GameFrame();
    const bool step=same && t.trackFrame+1==frame;
    for(int i=0;i<3;++i) {
        const float raw=(pick.aim[i]-t.tgtPrev[i])/dt;
        if(step)t.tgtVel[i]=std::fabs(raw)<80.0f ? t.tgtVel[i]+(raw-t.tgtVel[i])*0.2f : 0.0f;
        else if(!same)t.tgtVel[i]=0.0f;
    }
    if(!same || t.trackFrame!=frame){std::memcpy(t.tgtPrev,pick.aim,12);t.trackFrame=frame;}   // a frame's first sample
    std::memcpy(t.aim,pick.aim,12);
    t.target=pick.best;t.flyer=pick.flyer;t.seenTarget=ms;
}

void Attack(Jet& j,const Arms& arms,const float* pos,const float* nose,const float* lead,float height,ULONGLONG ms,float* want,
            float* speed,bool* gunsOk,bool* missileOk) noexcept {
    const Kind& kind=KindOf(j);
    Aim& t=j.t;
    const float reach=MissileReach(kind,arms);
    if(arms.missiles>0 && reach>0.0f && ms>=t.gunsUntil) {
        if(!t.lockSeen || arms.locked>0)t.lockSeen=ms;
        if(ms-t.lockSeen>kNoLockMs) {
            Log("JET v=%p no missile lock in %.0f s: guns for %.0f s",j.Vehicle(),static_cast<float>(kNoLockMs)*0.001f,
                static_cast<float>(kGunSpellMs)*0.001f);
            t.gunsUntil=ms+kGunSpellMs;t.lockSeen=0;
            if(j.mode==Mode::missile || j.mode==Mode::crank)SetMode(j,Mode::patrol,ms);
        }
        Missile(j,pos,height,reach,ms,want,speed);
        *missileOk=j.mode==Mode::missile;
    } else if(t.flyer)*gunsOk=Chase(j,pos,nose,lead,ms,want,speed);
    else *gunsOk=Strike(j,pos,nose,lead,height,ms,want,speed);
}

bool WeaponsFree(const Jet& j) noexcept {
    return Cfg().jetPilot && j.t.target && j.mode!=Mode::withdraw && j.mode!=Mode::recover && j.mode!=Mode::takeoff;
}

// Bombs (stores.h StoreRole::bomb) at a target on the ground: let go once where one would hit (its fall from here
// at the jet's velocity, the target's height, the target moving on as it falls) is within kBombHit of its blast
// radius of the target, and the jet is kBombSafe over it (its own blast); one every kBombMs.
// Rockets (StoreRole::rocket) in a strafing run: a ripple (the store's burst) every kRocketMs while the guns' lead is
// within kRocketCone of the nose, from kRocketOpen in to kRocketClose times the guns' closest; then the guns go on.
constexpr float kRocketOpen=1400.0f,kRocketClose=2.5f,kRocketCone=0.026f;   // m, x gunClose, rad (1.5 deg)
constexpr ULONGLONG kRocketMs=600;
constexpr float kBombHit=1.0f,kBombSafe=100.0f;
constexpr ULONGLONG kBombMs=400;
void Bomb(Jet& j,unsigned char* v,const float* pos,const Arms& a,ULONGLONG ms) noexcept {
    const Aim& t=j.t;
    if(!t.target || t.flyer || a.bombs<=0 || ms-t.bombAt<kBombMs)return;
    const float over=pos[1]-t.aim[1];
    if(over<kBombSafe)return;
    const float vy=j.m.vel[1],fall=(vy+std::sqrt(vy*vy+2.0f*kG*over))/kG;   // s to the target's height
    for(int i=0;i<a.storeCount;++i) {
        const Store& s=a.stores[i];
        if(s.spec->role!=StoreRole::bomb || s.ammo<=0)continue;
        const float dx=pos[0]+j.m.vel[0]*fall-(t.aim[0]+t.tgtVel[0]*fall),dz=pos[2]+j.m.vel[2]*fall-(t.aim[2]+t.tgtVel[2]*fall);
        const float miss=std::sqrt(dx*dx+dz*dz),hit=At<float>(s.weapon,kWeaponBlast)*kBombHit;
        if(!(miss<hit))return;
        TriggerStore(s);
        j.t.bombAt=ms;
        if(Cfg().debug)Log("JET v=%p bomb: %s, %.0f m over the target, falls %.1f s, %.0f m off (%d left)",v,s.spec->name,over,fall,miss,
                         s.ammo-1);
        return;
    }
}

// The fire bytes: guns while the nose is on the lead point within reach; the missile (`missileOk`: on its
// standoff run) once the nose has been on the target itself kLockMs, inside MissileReach.
void Fire(Jet& j,unsigned char* v,const float* pos,const float* nose,const float* lead,bool gunsOk,bool missileOk,const Arms& a,
          ULONGLONG ms) noexcept {
    bool gun=false,missile=false;
    Aim& t=j.t;
    if(!missileOk)t.lockAt=0;
    if(WeaponsFree(j)) {
        const float d[3]={lead[0]-pos[0],lead[1]-pos[1],lead[2]-pos[2]};
        const float dist=Len(d);
        const float miss=dist>1.0f ? std::acos(Clamp(Dot(d,nose)/dist,-1.0f,1.0f)) : 0.0f;
        const float wide=dist>1.0f ? std::atan(kHitRadius/dist) : 1.0f;
        const Kind& k=KindOf(j);
        const float reach=a.gunRange<k.gunOpen ? a.gunRange : k.gunOpen;
        const float path[3]={pos[0]+nose[0]*a.gunRange,pos[1]+nose[1]*a.gunRange,pos[2]+nose[2]*a.gunRange};
        float flight[3]={j.m.vel[0],j.m.vel[1],j.m.vel[2]};
        // The nose rides aoa above the path (JetSteer): the slip allowed is past that.
        const bool straight=Normalize(flight) && Dot(flight,nose)>std::cos(std::acos(kGunSlip)+std::fabs(j.m.aoa));
        const bool friendly=FriendInLine(pos,path,v);
        gun=gunsOk && straight && a.guns>0 && dist<reach && dist>k.gunClose*0.8f && miss<(wide>kGunCone ? wide : kGunCone) && !friendly;
        if(gunsOk && straight && !t.flyer && a.rocket>=0 && dist<kRocketOpen && dist>k.gunClose*kRocketClose && miss<kRocketCone &&
           !friendly && ms-t.rocketAt>kRocketMs) {
            TriggerStore(a.stores[a.rocket]);
            t.rocketAt=ms;
            if(Cfg().debug)Log("JET v=%p rockets: %s, %.0f m, %.1f deg off (%d left)",v,a.stores[a.rocket].spec->name,dist,
                             miss*180.0f/kPi,a.stores[a.rocket].ammo);
        }
        if(Cfg().debug && dist<reach && ms-t.gateAt>250) {
            t.gateAt=ms;
            Log("JET v=%p gun gate: %s mode=%s dist=%.0f miss=%.1f cone=%.1f deg slip=%.3f friend=%d aimed=%d",v,gun ? "FIRE" : "hold",
                kModeNames[static_cast<int>(j.mode)],dist,miss*180.0f/kPi,(wide>kGunCone ? wide : kGunCone)*180.0f/kPi,
                Dot(flight,nose),friendly,gunsOk);
        }
        const float to[3]={t.aim[0]-pos[0],t.aim[1]-pos[1],t.aim[2]-pos[2]};
        const float tdist=Len(to),off=tdist>1.0f ? std::acos(Clamp(Dot(to,nose)/tdist,-1.0f,1.0f)) : 0.0f;
        if(!missileOk || off>kMissileCone)t.lockAt=0;
        else if(!t.lockAt)t.lockAt=ms;
        missile=missileOk && a.missiles>0 && a.locked>0 && t.lockAt && ms-t.lockAt>=kLockMs && tdist>kMissileMin && tdist<MissileReach(k,a) &&
                ms-t.missileAt>kMissileMs && !FriendInLine(pos,t.aim,v);
        if(missile) {
            t.missileAt=ms;
            if(Cfg().debug)Log("JET v=%p missiles: %s, %.0f m, %.1f deg off the nose, held %.1f s, %d locked",v,
                             a.pick>=0 ? a.stores[a.pick].spec->name : "stock",tdist,off*180.0f/kPi,
                             static_cast<float>(ms-t.lockAt)*0.001f,a.locked);
            if(a.pick>=0){TriggerStore(a.stores[a.pick]);missile=false;}   // its own trigger, not holder 2's byte
        }
        Bomb(j,v,pos,a,ms);
    }
    v[kFireGun]=gun;v[kFireMissile]=missile;
}

void JetLog(const Jet& j,const unsigned char* v,const float* pos,const Arms& a,float speed,float clear,ULONGLONG ms) noexcept {
    const float hp=At<float>(v,kHp),hpMax=At<float>(v,kHpMax);
    const float* aim=j.t.aim;
    const float d=j.t.target ? std::sqrt((aim[0]-pos[0])*(aim[0]-pos[0])+(aim[1]-pos[1])*(aim[1]-pos[1])+(aim[2]-pos[2])*(aim[2]-pos[2])) : 0.0f;
    Log("JET v=%p %s %s y=%.0f clear=%.0f ceil=%.0f spd=%.0f/%.0f real=%.0f vy=%.1f bank=%.0f target=%p%s dist=%.0f guns=%d msl=%d hp=%.0f/%.0f fuel=%.0fs fire=%d/%d at=(%.0f,%.0f) soft=%.0f%s",
        v,KindOf(j).name,kModeNames[static_cast<int>(j.mode)],pos[1],clear,CeilingY(),Len(j.m.vel),speed,j.m.real,j.m.vel[1],
        std::acos(Clamp(At<float>(v,kMatrix+0x14)/std::sqrt(1.0f-At<float>(v,kMatrix+0x24)*At<float>(v,kMatrix+0x24)+1e-6f),-1.0f,1.0f))*180.0f/kPi,
        j.t.target,j.t.flyer ? "(air)" : "",d,a.guns,a.missiles,hp,hpMax,
        static_cast<float>(j.fuelMs)*0.001f-static_cast<float>(ms-j.bornAt)*0.001f,v[kFireGun],v[kFireMissile],pos[0],pos[2],
        airbound::Depth(JetSoftBox(j),pos),j.m.edgeBack ? " back" : "");
    // Each weapon's barrel against the nose: the guns must point where the nose does.
    if(SeatCount(v)==0)return;
    const auto seat=SeatAt(const_cast<unsigned char*>(v),0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>8 || !Readable(holders,count*8))return;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        float at[3],dir[3];
        if(!w)continue;
        if(!GunBarrel(v,w,at,dir)){Log("JET v=%p gun %llu: no barrel frame",v,static_cast<unsigned long long>(i));continue;}
        Log("JET v=%p gun %llu: dir=(%.2f,%.2f,%.2f) nose=(%.2f,%.2f,%.2f) dot=%.2f at=(%.1f,%.1f,%.1f) from the body",v,
            static_cast<unsigned long long>(i),dir[0],dir[1],dir[2],m[8],m[9],m[10],dir[0]*m[8]+dir[1]*m[9]+dir[2]*m[10],
            at[0]-pos[0],at[1]-pos[1],at[2]-pos[2]);
    }
}

// A new mission: the flyer memo's objects were the last mission's.
void ResetTargets() noexcept {
    for(auto& set:flyerMemo)for(auto& m:set)m=FlyerMemo{};
}
}  // namespace jet
}  // namespace crew
