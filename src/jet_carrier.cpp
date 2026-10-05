// The drone carrier and its drones (jet.cpp): the carrier's station, orbit, sidestep and drone launches; a
// drone's way back and docking; the blast and doll drones' charges and the dolls they carry.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "jet_internal.h"

namespace crew {
namespace jet {
namespace {
// The carrier and its drones (LaunchDrones, Recover): one drone every kLaunchGapMs while the carrier has a
// target, from kLaunchBelow under it; back after kDroneSortieMs out (or damaged, out of ammo or fuel, no
// target for kIdleMs, the carrier leaving), docked within kDockDist of the point kDockBelow under it,
// ready again kRearmMs later. A drone shot down is lost: the carrier has a new one kReplaceMs later.
constexpr ULONGLONG kLaunchGapMs=1500,kRearmMs=10000,kReplaceMs=30000;
constexpr float kLaunchBelow=25.0f,kDockDist=25.0f;
// The way back: a drone far off heads for a point kDockBehind behind and kDockUnder under the dock (along
// the carrier's track), drawn in to the dock itself as it nears (kDockLine metres out), so it comes in
// from behind and below instead of through the carrier's body.
constexpr float kDockBehind=80.0f,kDockUnder=25.0f,kDockLine=200.0f;
// A blast or doll drone's charge (see kBlastTrigger): fired kBlastFireMs, the drone deleted kBlastMs on.
constexpr ULONGLONG kBlastMs=300,kBlastFireMs=100;
// The carrier's work (CarrierGoal), instead of hovering still over its anchor:
//  - its station: over its anchor, moved up to kStationShift toward its target, but kept kStandoff from it
//    (out of the fight its drones are sent into);
//  - it circles the station slowly (kCarrierOrbit, kOrbitSpeed, carrots kOrbitLead ahead on the circle; every
//    other carrier of a flight the other way round), facing its target, or along its way with none;
//    for kLaunchHoldMs after a launch it slows to kLaunchSpeed (the drone leaves along its track);
//  - while a drone of its own comes back within kDockHold, it holds still (stops over where it is);
//  - hit (its HP fallen kEvadeHit of its most since the last sidestep), it sidesteps kEvadeShift across the line
//    from its target (or its track), the way it was drifting, for kEvadeMs, then not again for kEvadeGapMs.
// Withdrawing (damaged, fuel, out of drones) stays the withdrawal's.
constexpr float kCarrierOrbit=260.0f,kOrbitLead=0.5f,kOrbitSpeed=15.0f,kLaunchSpeed=6.0f;
constexpr float kStationShift=500.0f,kStandoff=700.0f,kDockHold=300.0f,kEvadeShift=180.0f,kEvadeHit=0.01f;
constexpr ULONGLONG kLaunchHoldMs=1200,kEvadeMs=5000,kEvadeGapMs=6000;

// A doll drone carries a hololive Recruiter's doll (docs/decoy-blast-re.md 2): a Decoy object made with
// the drone the way the Recruiter's core makes it (InitParam@Decoy, team 4, which only the enemy is hostile
// to: they go for it), which sings and dances on its own (its SGO's random_action) and follows the matrix
// the plugin keeps for it (dolls[], set as Decoy+0x10D0 by Decoy_Setup) kDollBelow under the drone,
// upright, turned as the drone. kDollHp HP, its life the drone's fuel. Deleted with the drone (shot down,
// docked, blown, gone), +0x10D0 cleared first (DollFree). The dolls are DLC: a drone whose doll cannot be
// made flies without.
constexpr unsigned kPreload=0x7A3780;
constexpr unsigned kDecoyParamVtable=0x17A4FF0,kDecoyVtable=0x17D3F88,kDecoySetup=0x5ACC30;
constexpr std::size_t kDecoyFollow=0x10D0;
constexpr std::int32_t kTeamDecoy=4;
constexpr float kDollBelow=4.0f,kDollHp=2000.0f;
const unsigned char kDecoySetupSig[]={0x0F,0x28,0xC2,0xF3,0x0F,0x11,0x91,0xF4,0x02,0x00,0x00};
const wchar_t* const kDollSgo[]={L"app:/object/e_throw_decoyscreen_ayame.sgo",L"app:/object/e_throw_decoyscreen_mio.sgo",
                                 L"app:/object/e_throw_decoyscreen_fubuki.sgo"};
bool dollOk=false;

// The dolls, one per jet entry: the Decoy and its control block, and the matrix it follows. The matrices are
// never freed (static), so the pointer a doll keeps never dangles.
struct Doll { unsigned char* obj; const void* ctrl; alignas(16) float m[16]; };
Doll dolls[kMaxJets]{};
// InitParam@Decoy as 0x29E780 builds it (0x38 bytes, padded to 0x40): the vtable, zeros; +0x30 = 0, owned
// here (not a remote copy).
struct alignas(16) InitParamDecoy { const void* vtable; unsigned char rest[0x38]; };
using DecoyCreateFn=unsigned char*(*)(void*,const float*,const wchar_t*,InitParamDecoy*);
using DecoySetupFn=void(__fastcall*)(void*,const float*,float,std::int32_t);

// Doll `i`'s matrix: kDollBelow under drone `v`, upright, its nose's heading.
void DollPose(int i,const unsigned char* v) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    float f[3]={m[8],0.0f,m[10]};
    if(!Normalize(f)){f[0]=0;f[2]=1;}
    const float pose[16]={f[2],0,-f[0],0, 0,1,0,0, f[0],0,f[2],0, p[0],p[1]-kDollBelow,p[2],1};
    std::memcpy(dolls[i].m,pose,sizeof(pose));
}

unsigned char* DollCreate(const wchar_t* sgo,const float* m) noexcept {
    InitParamDecoy ip{image+kDecoyParamVtable,{}};
    __try { return reinterpret_cast<DecoyCreateFn>(image+kCreateObject)(At<void*>(image,kObjectMgr),m,sgo,&ip); }
    __except(FaultLog("JET doll create",GetExceptionInformation())) { return nullptr; }
}

// Its station (see kStationShift) at `height`, logged when its target changes.
void CarrierStation(Jet& c,const float* pos,const float* anchor,float height,float* st) noexcept {
    st[0]=anchor[0];st[1]=height;st[2]=anchor[2];
    const float* aim=c.t.aim;
    if(c.t.target) {
        float away[3]={anchor[0]-aim[0],0.0f,anchor[2]-aim[2]};
        const float apart=Len(away);
        if(!Normalize(away)) {
            away[0]=pos[0]-aim[0];away[1]=0.0f;away[2]=pos[2]-aim[2];
            if(!Normalize(away)){away[0]=1;away[1]=0;away[2]=0;}
        }
        const float keep=apart-kStationShift>kStandoff ? apart-kStationShift : kStandoff;   // from the target
        st[0]=aim[0]+away[0]*keep;st[2]=aim[2]+away[2]*keep;
    }
    if(c.t.target==c.carrier.stationFor)return;
    c.carrier.stationFor=c.t.target;
    if(Cfg().debug)Log("JET v=%p carrier: station (%.0f,%.0f), %.0f m from its anchor%s",c.Vehicle(),st[0],st[2],HorizDist(st,anchor),
                     c.t.target ? ", off its target" : " (no target)");
}

// Whether a drone of the carrier's is coming back within kDockHold of it.
bool DroneDocking(const Jet& c,const float* pos,ULONGLONG ms) noexcept {
    for(const auto& d:jets) {
        if(!Flown(d,ms) || d.drone.mother!=c.ref.ctrl || d.mode!=Mode::recover || d.reap)continue;
        const float to[3]={d.m.prevPos[0]-pos[0],d.m.prevPos[1]-pos[1],d.m.prevPos[2]-pos[2]};
        if(Dot(to,to)<kDockHold*kDockHold)return true;
    }
    return false;
}

// Hit: the sidestep (see kEvadeShift) starts, unless `hold` (a drone docking: a sidestep then dragged out its
// last approach). Returns whether it is sidestepping.
bool CarrierEvade(Jet& c,const float* pos,float height,float hp,float hpMax,bool hold,ULONGLONG ms) noexcept {
    CarrierState& cs=c.carrier;
    // hpSeen: its HP since the last sidestep (or healing): small hits add up.
    const bool hit=hpMax>0.0f && hp<cs.hpSeen-hpMax*kEvadeHit;
    if(hp>cs.hpSeen || hit || ms<cs.evadeAgain)cs.hpSeen=hp;
    if(hit && !hold && ms>=cs.evadeAgain) {
        float line[3]={c.m.vel[0],0.0f,c.m.vel[2]};
        if(c.t.target){line[0]=pos[0]-c.t.aim[0];line[2]=pos[2]-c.t.aim[2];}
        float side[3]={line[2],0.0f,-line[0]};
        if(!Normalize(side)){side[0]=1;side[2]=0;}
        if(side[0]*c.m.vel[0]+side[2]*c.m.vel[2]<0.0f){side[0]=-side[0];side[2]=-side[2];}
        cs.evadeTo[0]=pos[0]+side[0]*kEvadeShift;cs.evadeTo[1]=height;cs.evadeTo[2]=pos[2]+side[2]*kEvadeShift;
        cs.evadeUntil=ms+kEvadeMs;cs.evadeAgain=cs.evadeUntil+kEvadeGapMs;
        if(Cfg().debug)Log("JET v=%p carrier hit (hp %.0f/%.0f): sidesteps %.0f m to (%.0f,%.0f)",c.Vehicle(),hp,hpMax,kEvadeShift,
                         cs.evadeTo[0],cs.evadeTo[2]);
    }
    return ms<cs.evadeUntil;
}

// Whether drone place `i` of carrier `c` has its drone out (flying, not leaving for the dock or deleted).
bool DroneOut(const Jet& c,int i) noexcept {
    for(const auto& d:jets)
        if(d.ref && d.drone.mother==c.ref.ctrl && d.drone.slot==i && !d.reap && Alive(d.ref) && !d.Vehicle()[kDead])return true;
    return false;
}

// A drone place whose drone is gone (shot down) gets a new one kReplaceMs on.
void ReplaceLost(Jet& c,ULONGLONG ms) noexcept {
    CarrierState& cs=c.carrier;
    for(int i=0;i<kCarrierDrones;++i) {
        if(cs.dock[i]!=kDroneOut || DroneOut(c,i))continue;
        cs.dock[i]=ms+kReplaceMs;
        Log("JET v=%p carrier: drone %d lost, a new one in %.0f s",c.Vehicle(),i,static_cast<float>(kReplaceMs)*0.001f);
    }
}

// One drone launched from a ready place (see kCarrierDrones) at `aim` (`target`: what it is sent at, for the log):
// from kLaunchBelow under the carrier along its heading, faster than it, in its flight. False: no place ready, the
// table full (FreeSlot says so in the log, at most every few seconds), the game could not make it.
bool LaunchOne(Jet& c,const float* pos,const float* nose,const float* aim,const void* target,ULONGLONG ms) noexcept {
    CarrierState& cs=c.carrier;
    int i=0;
    while(i<kCarrierDrones && cs.dock[i]>ms)++i;
    if(i==kCarrierDrones || cs.sorties<=0 || !SlotFree())return false;
    cs.launchAt=ms;
    float heading[3]={c.m.vel[0],0.0f,c.m.vel[2]};
    if(!Normalize(heading)){heading[0]=nose[0];heading[1]=0.0f;heading[2]=nose[2];}
    const float from[3]={pos[0],pos[1]-kLaunchBelow,pos[2]};
    // Its own drones (blast, doll: its body's mark says which) when their body is there this mission, else the gun drone.
    const Kind& own=KindOf(cs.drones);
    const Body b=own.weapon==Weapon::charge && Preloaded(own.body) ? own.body : Body::drone;
    const float least=KindOf(Row(b).role).minSpeed,s=Len(c.m.vel)+20.0f;
    Jet* d=Launch(b,from,heading,aim,Cfg().jetFuelSec,s>least ? s : least,c.Vehicle());
    if(!d)return false;
    d->drone.mother=c.ref.ctrl;d->drone.carried=true;d->drone.slot=i;
    JoinFlight(*d,c.flight);
    cs.dock[i]=kDroneOut;--cs.sorties;
    if(KindOf(*d).doll)DollMake(IndexOf(*d),d->Vehicle(),Cfg().jetFuelSec);
    Log("JET v=%p carrier %p launched %s %d at %p",d->Vehicle(),c.Vehicle(),KindOf(*d).name,i,target);
    Publish(true);
    return true;
}
}  // namespace

Jet* MotherOf(const Jet& d) noexcept {
    if(!d.drone.mother)return nullptr;
    for(auto& c:jets)
        if(c.ref && c.ref.ctrl==d.drone.mother && !c.reap && Alive(c.ref) && !c.Vehicle()[kDead])return &c;
    return nullptr;
}

// A drone's way back (Mode::recover): at the point kDockBelow under its carrier where the carrier will be
// half a second on, at the carrier's speed plus what the distance adds; true once within kDockDist.
bool Recover(const Jet& d,const Jet& mother,const float* pos,float* want,float* speed) noexcept {
    const float* mp=reinterpret_cast<const float*>(mother.Vehicle()+kPosition);
    const float* mv=mother.m.vel;
    const float dock[3]={mp[0]+mv[0]*0.5f,mp[1]-kDockBelow+mv[1]*0.5f,mp[2]+mv[2]*0.5f};
    const float to[3]={dock[0]-pos[0],dock[1]-pos[1],dock[2]-pos[2]};
    const float dist=Len(to),own=Len(mv);
    if(dist<kDockDist)return true;
    float track[3]={mv[0],0.0f,mv[2]};
    if(!Normalize(track)){track[0]=0;track[2]=0;}
    const float out=Clamp(dist/kDockLine,0.0f,1.0f);
    const float in[3]={dock[0]-track[0]*kDockBehind*out,dock[1]-kDockUnder*out,dock[2]-track[2]*kDockBehind*out};
    Toward(pos,in,want);
    *speed=Clamp(own+dist*0.25f,own+10.0f,KindOf(d).attack);
    return false;
}

// Docked: the drone is deleted (JetReap) and its place on the carrier ready kRearmMs on.
void Dock(Jet& d,Jet& mother,ULONGLONG ms) noexcept {
    if(d.drone.slot>=0 && d.drone.slot<kCarrierDrones)mother.carrier.dock[d.drone.slot]=ms+kRearmMs;
    Log("JET v=%p docked on carrier %p (place %d), ready again in %.0f s",d.Vehicle(),mother.Vehicle(),d.drone.slot,
        static_cast<float>(kRearmMs)*0.001f);
    d.reap=true;d.drone.mother=nullptr;d.drone.carried=false;d.why="docked";
}

// Where the carrier flies this frame (`goal`, at `height`), what it faces and how fast (see kCarrierOrbit).
void CarrierGoal(Jet& c,const Kind& k,const float* pos,const float* anchor,float height,float hp,float hpMax,ULONGLONG ms,
                 float* goal,float* face,float* speed) noexcept {
    float st[3];
    CarrierStation(c,pos,anchor,height,st);
    const bool coming=DroneDocking(c,pos,ms);
    const bool evading=CarrierEvade(c,pos,height,hp,hpMax,coming,ms);
    const bool docking=!evading && coming;
    if(docking!=c.carrier.docking) {
        c.carrier.docking=docking;
        if(Cfg().debug)Log("JET v=%p carrier: %s",c.Vehicle(),docking ? "a drone is coming in: holds still" : "back on its orbit");
    }
    if(evading) {
        std::memcpy(goal,c.carrier.evadeTo,12);*speed=k.cruise;
    } else if(docking) {
        goal[0]=pos[0];goal[1]=height;goal[2]=pos[2];*speed=0.0f;
    } else {
        const float rel[3]={pos[0]-st[0],0.0f,pos[2]-st[2]};
        const float dist=Len(rel),way=c.wing%2 ? 1.0f : -1.0f;
        const float a=(dist>1.0f ? std::atan2(rel[2],rel[0]) : 0.0f)+kOrbitLead*way;
        goal[0]=st[0]+std::cos(a)*kCarrierOrbit;goal[1]=height;goal[2]=st[2]+std::sin(a)*kCarrierOrbit;
        *speed=dist>kCarrierOrbit*1.5f ? k.cruise : ms-c.carrier.launchAt<kLaunchHoldMs ? kLaunchSpeed : kOrbitSpeed;
    }
    std::memcpy(face,c.t.target ? c.t.aim : goal,12);
}

// A carrier's launch (see kCarrierDrones): a ready drone every kLaunchGapMs while it has a target, from
// kLaunchBelow under it along its heading, faster than it, in its flight. With the table full it waits for an
// entry (FreeSlot says so in the log, at most every few seconds), trying nothing meanwhile.
void LaunchDrones(Jet& c,const float* pos,const float* nose,ULONGLONG ms) noexcept {
    ReplaceLost(c,ms);
    if(!c.t.target || c.mode==Mode::withdraw || c.carrier.sorties<=0 || ms-c.carrier.launchAt<kLaunchGapMs)return;
    LaunchOne(c,pos,nose,c.t.aim,c.t.target,ms);
}

// The player flying carrier `v` sends a drone at `at` (playerjet_board.inc), at most one every kLaunchGapMs: its drones
// then work round that point (jet.cpp JetFrame: CarrierState::order) until they come back or the player leaves.
bool PlayerLaunchDrone(unsigned char* v,const float* at,ULONGLONG ms) noexcept {
    Jet* const c=FindJet(v);
    if(!c || KindOf(*c).weapon!=Weapon::drones)return false;
    ReplaceLost(*c,ms);
    if(ms-c->carrier.launchAt<kLaunchGapMs)return false;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    std::memcpy(c->carrier.order,at,12);c->carrier.ordered=true;
    for(auto& d:jets)if(d.ref && d.drone.mother==c->ref.ctrl)d.t.target=nullptr;   // the drones out pick again round the new point
    return LaunchOne(*c,pos,m+8,at,nullptr,ms);
}

// Its drones out called back to it (Mode::recover: Recover, Dock); how many.
int RecallDrones(unsigned char* v,ULONGLONG ms) noexcept {
    Jet* const c=FindJet(v);
    if(!c)return 0;
    c->carrier.ordered=false;
    int n=0;
    for(auto& d:jets) {
        if(!d.ref || d.drone.mother!=c->ref.ctrl || d.reap || d.mode==Mode::recover || d.mode==Mode::withdraw || d.drone.blastAt)continue;
        SetMode(d,Mode::recover,ms);++n;
    }
    if(n)Log("JET v=%p carrier: %d drones called back by the player",v,n);
    return n;
}

int DronesLeft(const unsigned char* v) noexcept {
    const Jet* const c=FindJet(v);
    return c && KindOf(*c).weapon==Weapon::drones ? c->carrier.sorties : 0;
}

bool OutOfDrones(const Jet& c) noexcept {
    if(c.carrier.sorties>0)return false;
    for(const auto at:c.carrier.dock)if(at==kDroneOut)return false;
    return true;
}

void Detonate(Jet& j,Jet* mother,float dist,ULONGLONG ms) noexcept {
    j.drone.blastAt=ms;
    if(mother && j.drone.slot>=0 && j.drone.slot<kCarrierDrones)mother->carrier.dock[j.drone.slot]=ms+kRearmMs;
    j.drone.mother=nullptr;
    Log("JET v=%p %s detonates %.1f m from %p",j.Vehicle(),KindOf(j).name,dist,j.t.target);
}

// A blast or doll drone's charge (see kBlastTrigger): fired, the drone held still, deleted kBlastMs on.
void Blast(Jet& j,unsigned char* v,ULONGLONG ms) noexcept {
    std::memset(j.m.vel,0,sizeof(j.m.vel));std::memset(j.m.omega,0,sizeof(j.m.omega));j.m.ready=true;
    v[kFireGun]=0;v[kFireMissile]=ms-j.drone.blastAt<kBlastFireMs;
    if(ms-j.drone.blastAt<kBlastMs || j.reap)return;
    j.reap=true;j.why="blown";
    DollFree(IndexOf(j));
}

// The doll of doll drone `i` (`v`), the three in turn, living `lifeSec`; none when it cannot be made.
void DollMake(int i,const unsigned char* v,DWORD lifeSec) noexcept {
    DollFree(i);
    if(!dollOk || !At<void*>(image,kObjectMgr))return;
    static unsigned next=0;
    const wchar_t* const sgo=kDollSgo[next++%(sizeof(kDollSgo)/sizeof(kDollSgo[0]))];
    DollPose(i,v);
    unsigned char* const d=DollCreate(sgo,dolls[i].m);
    if(!d){Log("JET doll %ls: not made (DLC not installed?)",sgo);return;}
    __try {
        if(At<const void*>(d,0)!=image+kDecoyVtable) {
            Log("JET doll %ls: %p is no Decoy: deleted",sgo,d);
            reinterpret_cast<DeleteFn>(image+kDelete)(d);
            return;
        }
        reinterpret_cast<SetTeamFn>(image+kSetTeam)(d,kTeamDecoy,true);
        const DWORD frames=lifeSec<0x1000000 ? lifeSec*60 : 0x3C000000;
        reinterpret_cast<DecoySetupFn>(image+kDecoySetup)(d,dolls[i].m,kDollHp,static_cast<std::int32_t>(frames));
        dolls[i].obj=d;dolls[i].ctrl=At<const void*>(d,kSelfCtrl);
        Log("JET doll %ls (%p) on drone %p",sgo,d,v);
    } __except(FaultLog("JET doll setup",GetExceptionInformation())){}
}

// Doll `i` deleted (if it is still the one made), after it stops following the plugin's matrix.
void DollFree(int i) noexcept {
    if(i<0 || i>=kMaxJets)return;
    Doll& d=dolls[i];
    unsigned char* const o=d.obj;
    d.obj=nullptr;
    if(!o)return;
    __try {
        if(Readable(o,kDecoyFollow+8) && At<const void*>(o,0)==image+kDecoyVtable && At<const void*>(o,kSelfCtrl)==d.ctrl &&
           !(o[kObjFlags]&kObjDeleted)) {
            Put<const void*>(o,kDecoyFollow,nullptr);
            reinterpret_cast<DeleteFn>(image+kDelete)(o);
        }
    } __except(FaultLog("JET doll delete",GetExceptionInformation())){}
}

void DollFrame(int i,const unsigned char* v) noexcept {
    if(i>=0 && i<kMaxJets && dolls[i].obj)DollPose(i,v);
}

void ResetDolls() noexcept {
    for(auto& d:dolls){d.obj=nullptr;d.ctrl=nullptr;}
}

bool PreloadDolls(void* mgr,bool dollBody) noexcept {
    if(!dollOk || !dollBody)return false;
    for(const auto sgo:kDollSgo)reinterpret_cast<void(*)(void*,const wchar_t*,std::int32_t,std::int32_t)>(image+kPreload)(mgr,sgo,2,-1);
    return true;
}

bool InstallDolls() noexcept {
    dollOk=Matches(kDecoySetup,kDecoySetupSig,sizeof(kDecoySetupSig)) && Readable(image+kDecoyParamVtable,8) && Readable(image+kDecoyVtable,8);
    return dollOk;
}
}  // namespace jet
}  // namespace crew
