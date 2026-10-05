// NPC jets (docs/jet-plan.md). The game has no fighter, so a jet is a Vehicle506_Helicopter body (rigid
// body and .cas collision, HP, weapons on the "body" bone, the stock crash and wreck) spawned from a
// derived SGO (testrange/gen.py: edf6tr_jet_*, tools/make_jets.py: EDF6VC_JET_*), told apart by its speed gain k
// (veh+0x162C), which the derived SGO sets to its body's mark (jet_internal.h kBodies, 7001-7011: no stock heli is
// anywhere near; body506.cpp's range table hands those to jet.cpp). Its NPC pilot does nothing; the plugin flies it
// in two stages a frame:
//  - input (slot 55, from HeliFrame: JetFrame here): the target, the guidance step (a wing: JetSteer, lift along
//    the body's up, at most its kind's maxG, so it banks before it turns; a rotor craft: Hover), the fire bytes
//    slot 57 reads (0x2020 both guns, 0x2021 the missile; vehicle_weapon_setting puts them along the body's nose),
//    and a bomber's bay (BayFrame);
//  - physics (slot 57, after the stock code wrote the heli's velocity and spin, body506.cpp -> JetBodyStep): the
//    body's linear velocity (0x11B18F0) and an angular velocity (0x11B1760) that turns the nose onto that
//    velocity, banked into the turn.
// What a jet does is its kind's table row (jet_internal.h kKinds): how it flies (wing, rotor), what it fights
// with (guns and missiles, shells, drones, a charge), the bones it poses. Roles: strike (ground targets first;
// JetLaunchBomber's bombers first fly the stock bomber's run and drop its bombs), fighter and interceptor
// (flying targets first; the interceptor faster, higher, farther out, its missiles from farther), multirole (the
// nearest target, either), carrier (jet_carrier.cpp: the V508 transport's four nacelles; it circles a station and
// sends its drones), drone (from its carrier), blast and doll (a blast or doll carrier's drones: rotor drones that
// blow up next to the enemy, the doll one carrying a singing, dancing hololive doll), gunship (jet_bay.cpp: it
// shells ground targets and fires its side cannon at them from its orbit). With missiles a jet stands off
// (Missile): it fires them from its role's missileRange and turns away, and closes in with the guns only once
// they are spent. None reloads; out of ammo, out of fuel (Cfg().jetFuelSec times its role's fuel, a launched
// sortie Cfg().jetSortieSec) or below kWithdrawHp of its HP it flies off and is deleted out of the player's sight.
// Two ways in: a mission places one (the test range's CreateFriend: it guards the player), or JetLaunch
// makes one at run time (the airstrike takeovers, airstrike.cpp; jet_spawn.cpp).
// Time is the plugin's game clock (GameMs): wall time that stops while no vehicle updates (pause menu,
// loading), so a pause neither burns fuel nor makes a jet look gone.
// A table entry belongs to one object (Jet::ref): its address and the control block of its weak-this (object
// +0x30), on which the entry holds a weak reference, so the block outlives the object: a jet destroyed is told
// by the block's use count, and no new object takes over the entry of an old one at its address. An entry goes
// only when its jet is gone or dead (JetReap, once a frame), never because it was not flown for a while; and
// all of them at the mission's start (ResetJets), with nothing of the last mission's touched.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "jet_internal.h"
#include <intrin.h>

namespace crew {
namespace jet {
Jet jets[kMaxJets]{};

namespace {
constexpr std::size_t kInLateral=0x1540,kInThrottle=0x1544,kInForward=0x1548,kInW=0x154C,kInYaw=0x1550;
constexpr float kTakeoffClear=30.0f;   // m over the ground: done taking off
// Withdrawing it climbs toward the ceiling and flies away from the player at full speed; it is deleted
// only out there (never in front of the player): kGone from the player, or, held in by the map's edge,
// kGoneStuck after kStuckMs of withdrawing.
constexpr float kWithdrawHp=0.25f,kGone=1600.0f,kGoneStuck=900.0f,kWithdrawClimb=300.0f;
constexpr ULONGLONG kStuckMs=60000;
// The map's edge (docs/map-edge-re.md): the heli input (slot 55, 0x6543A0) clamps the body into the
// mission's move area shrunk by veh+kAreaInset (0x5A9E50) and teleports it back, every frame, so a jet at
// the edge stopped dead and slid flank first. A jet's inset is set to kNoInset (the box grown 1e6 m: no
// clamp; the stock bombers are never clamped either). Out there the Havok broadphase ends at 3000 m a
// side: walls at the play edge (crew.h PlayEdge, with its buffer) keep the jets in, and one past kWorldGoneIn of it is deleted.
constexpr std::size_t kAreaInset=0xE00;
// kWorldGoneIn: m inside the world's edge (WorldHalf): 2950 stock. It was 300: a jet chasing past the walls
// (then 600 m in) at 200 m/s turns on a radius of some 500 m, and one in its turn was deleted 300 m past the wall
// (2026-10-05 11:43, the user: "do the NPCs vanish at the edge?"). Now the whole of a turn fits between them.
constexpr float kNoInset=-1.0e6f,kWorldGoneIn=50.0f;
// A wingman within kJetSpan of a burst's path is in its way when the rounds cannot pass through (JetInLine).
constexpr float kJetSpan=20.0f;
constexpr ULONGLONG kFullLogMs=5000;   // wall ms between "the table is full" lines
using KickFn=void(*)(void*,void*);
using CtrlFn=void(*)(void*);

// The weak reference an entry holds on its object's control block (MSVC _Ref_count_base: uses +8, weaks +0xC,
// vtable slot 1 deletes the block; booster.cpp holds its boosters' the same way).
void HoldRef(const ObjRef& r) noexcept {
    _InterlockedIncrement(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(const_cast<void*>(r.ctrl))+0xC));
}
void DropRef(const ObjRef& r) noexcept {
    auto ctrl=static_cast<unsigned char*>(const_cast<void*>(r.ctrl));
    if(!ctrl)return;
    if(_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(ctrl+0xC),-1)==1)(*reinterpret_cast<CtrlFn* const*>(ctrl))[1](ctrl);
}

// An entry of this mission let go of: its bay and doll torn down, its drones told their carrier is gone, the
// reference dropped.
void Release(Jet& j) noexcept {
    BayFree(j.bay.ifc);
    DollFree(IndexOf(j));
    for(auto& d:jets)if(d.ref && d.drone.mother==j.ref.ctrl)d.drone.mother=nullptr;
    DropRef(j.ref);
    j=Jet{};
}

// Whether entry `j`'s jet is gone (deleted, destroyed) or shot down: its entry may go.
bool Finished(const Jet& j) noexcept { return !Alive(j.ref) || j.Vehicle()[kDead]; }

void TableFull() noexcept {
    static ULONGLONG loggedAt=0;
    const ULONGLONG now=GetTickCount64();
    if(now-loggedAt>kFullLogMs){loggedAt=now;Log("JET table full: %d jets flying, no new one is taken over or launched",kMaxJets);}
}

// An empty entry, else one whose jet is finished (let go of first); nullptr when every entry flies a live jet.
Jet* FreeSlot() noexcept {
    for(auto& j:jets)if(!j.ref)return &j;
    for(auto& j:jets)if(Finished(j)){Release(j);return &j;}
    TableFull();
    return nullptr;
}

// A jet a mission placed, seen for the first time (its pilot just seated): it guards the player; an enemy's jet
// (HostileJet) joins the enemy team first, so the same flight round the player hunts the player's side: its targets
// are whatever its team is hostile to (VisitEnemies), its friends the enemy's.
Jet* CrewPlaced(unsigned char* v,const float* pos,ULONGLONG ms) noexcept {
    Jet* const j=NewEntry(v,ms);
    if(!j)return nullptr;
    if(HostileJet(v)) {
        SetJetTeam(v,kTeamEnemy);
        Log("JET v=%p is the enemy's: team %d",v,At<std::int32_t>(v,kTeam));
    }
    std::memcpy(j->anchor,pos,12);j->mode=Mode::takeoff;
    JoinFlight(*j,kPlacedFlight);
    j->fuelMs=static_cast<ULONGLONG>(static_cast<float>(Cfg().jetFuelSec)*KindOf(*j).fuel*1000.0f);
    Log("JET v=%p crewed: %s, hp=%.0f, ceiling=%.0f",v,KindOf(*j).name,At<float>(v,kHp),CeilingY());
    Publish(true);
    return j;
}

// Why it leaves (fuel, damage, ammo, its carrier lost, out of drones), or a drone's way back to its carrier.
void Leave(Jet& j,const Kind& kind,const Arms& arms,Jet* mother,float hp,float hpMax,ULONGLONG ms) noexcept {
    const char* why=ms-j.bornAt>j.fuelMs ? "fuel" : hpMax>0.0f && hp<hpMax*kWithdrawHp ? "damaged" :
                    arms.guns<=0 && arms.missiles<=0 && arms.bombs<=0 && (arms.hasGun || arms.hasMissile) ? "out of ammo" : nullptr;
    // A drone goes back to its carrier instead, and after kDroneSortieMs, half its HP gone, the carrier
    // leaving, or kIdleMs with nothing to attack; with the carrier gone it withdraws.
    if(j.drone.carried && !mother && !why)why="carrier lost";
    if(!why && kind.weapon==Weapon::drones) {
        if(OutOfDrones(j))why="out of drones";
    } else if(mother) {
        if(j.mode!=Mode::recover && j.mode!=Mode::withdraw) {
            const ULONGLONG sortie=kind.weapon==Weapon::charge ? kKamikazeSortieMs : kDroneSortieMs;
            const char* back=why ? why : ms-j.bornAt>sortie ? "sortie over" : hpMax>0.0f && hp<hpMax*0.5f ? "damaged" :
                             mother->mode==Mode::withdraw ? "carrier leaving" :
                             ms-(j.t.seenTarget ? j.t.seenTarget : j.bornAt)>kIdleMs ? "nothing to attack" : nullptr;
            if(back){Log("JET v=%p back to carrier %p: %s",j.Vehicle(),mother->Vehicle(),back);SetMode(j,Mode::recover,ms);}
        }
        why=nullptr;
    }
    if(why)Withdraw(j,why,ms);
}

// Withdrawing: climbing toward the ceiling, away from `viewer` (a bomber along its run), at full speed; deleted
// out of sight (see kGone).
void Away(Jet& j,const Kind& kind,const float* pos,const float* nose,const float* viewer,bool walled,ULONGLONG ms,float* want,
          float* speed) noexcept {
    const bool bomber=j.bay.bombSpeed>0.0f;
    float away[3]={pos[0]-viewer[0],0,pos[2]-viewer[2]};
    if(bomber)std::memcpy(away,j.bay.bombDir,12);
    if(!Normalize(away)){away[0]=nose[0];away[2]=nose[2];}
    const float top=CeilingY()-kCeilingGap*2.0f,climb=viewer[1]+kWithdrawClimb;
    Level(pos,away,climb<top ? climb : top,want);
    *speed=bomber && j.bay.bombSpeed>kind.attack ? j.bay.bombSpeed : kind.attack;
    const float d[3]={pos[0]-viewer[0],pos[1]-viewer[1],pos[2]-viewer[2]};
    const float gone=Len(d),turn=Len(j.m.vel)*Len(j.m.vel)/(kind.maxG*kG);
    const bool edge=walled || NearWall(pos,turn*1.5f,ms);
    if(gone>kGone || (gone>kGoneStuck && (edge || ms-j.modeAt>kStuckMs))) {
        if(!j.reap)Log("JET v=%p out of sight (%.0f m from the player): deleting",j.Vehicle(),gone);
        j.reap=true;
    }
}

// What it does with no target of its own to attack, or with a weapon that does not fly at targets: it circles.
void Circle(Jet& j,const float* pos,const float* anchor,float height,ULONGLONG ms,float* want,float* speed) noexcept {
    if(j.mode!=Mode::patrol)SetMode(j,Mode::patrol,ms);
    j.t.lockSeen=0;
    *speed=Patrol(j,pos,anchor,height,want);
}

// The guidance (`want`, `speed`) its mode and its kind's weapon ask for this frame.
void Guide(Jet& j,const Kind& kind,const Arms& arms,Jet* mother,const float* pos,const float* nose,const float* anchor,
           const float* viewer,const float* lead,float height,float clear,bool walled,ULONGLONG ms,float* want,float* speed,
           bool* gunsOk,bool* missileOk) noexcept {
    switch(j.mode) {
    case Mode::takeoff:
        want[0]=nose[0];want[1]=0.6f;want[2]=nose[2];Normalize(want);
        if(clear>kTakeoffClear || clear==kNoGround)SetMode(j,Mode::patrol,ms);
        return;
    case Mode::bomb:
        BombRun(j,pos,ms,want,speed);
        return;
    case Mode::recover:
        if(mother && Recover(j,*mother,pos,want,speed))Dock(j,*mother,ms);
        return;
    case Mode::withdraw:
        Away(j,kind,pos,nose,viewer,walled,ms,want,speed);
        return;
    default:
        break;
    }
    switch(kind.weapon) {
    case Weapon::charge: {   // at its target, else under its carrier (Hover)
        const Mode at=j.t.target ? Mode::approach : Mode::patrol;
        if(j.mode!=at)SetMode(j,at,ms);
        return;
    }
    case Weapon::guns:
        if(j.t.target){Attack(j,arms,pos,nose,lead,height,ms,want,speed,gunsOk,missileOk);return;}
        Circle(j,pos,anchor,height,ms,want,speed);
        return;
    case Weapon::shells:
    case Weapon::drones:
        Circle(j,pos,anchor,height,ms,want,speed);
        return;
    }
}

// A rotor craft's step (FlightModel::rotor): kind.alt over its anchor, facing its target: a carrier about its
// station, a charge at its target; leaving, along `want` (Away's way out, climbing). A carrier's nacelles follow
// its thrust.
void Rotor(Jet& j,const Kind& kind,unsigned char* v,Jet* mother,const float* pos,const float* anchor,const float* want,float height,
           float hp,float hpMax,float dt,ULONGLONG ms) noexcept {
    float goal[3]={anchor[0],height,anchor[2]},face[3];
    float climb=kHoverClimb,speed=kind.cruise;
    bool faced=false;
    if(j.mode==Mode::withdraw)for(int i=0;i<3;++i)goal[i]=pos[i]+want[i]*kHoverLeave;
    else if(kind.weapon==Weapon::charge) {
        // At its target; going back, at its carrier's dock; else under the carrier.
        climb=kind.cruise*0.5f;
        if(j.mode==Mode::recover && mother) {
            const float* mp=reinterpret_cast<const float*>(mother->Vehicle()+kPosition);
            goal[0]=mp[0];goal[1]=mp[1]-kDockBelow;goal[2]=mp[2];
        } else if(j.t.target) {
            std::memcpy(goal,j.t.aim,12);
            // A doll drone comes in kDollRide over the ground there (still within kDollTrigger of a target on it).
            const float under=kind.doll ? GroundClearance(goal) : kNoGround;
            if(under!=kNoGround && under<kDollRide)goal[1]+=kDollRide-under;
        }
        else{goal[0]=anchor[0];goal[1]=anchor[1]-kDockBelow*2.0f;goal[2]=anchor[2];}
    } else {
        // The carrier: about its station (CarrierGoal), kMinAlt*2 over the ground there at least.
        faced=kind.weapon==Weapon::drones;
        if(faced)CarrierGoal(j,kind,pos,anchor,height,hp,hpMax,ms,goal,face,&speed);
        const float top[3]={goal[0],goal[1]+600.0f,goal[2]},bottom[3]={goal[0],goal[1]-1500.0f,goal[2]};
        float hit[3];
        if(MapRay(top,bottom,hit)>=0.0f && goal[1]<hit[1]+kMinAlt*2.0f)goal[1]=hit[1]+kMinAlt*2.0f;
    }
    if(!faced)std::memcpy(face,j.t.target ? j.t.aim : goal,12);
    Hover(j,kind,v,pos,goal,face,speed,climb,dt);
    if(kind.pose==Pose::thrusters)Thrusters(j,kind,v,dt,ms);
}

// The weapon its kind fights with, this frame: the fire bytes (only the guns' weapon sets them), and the
// shells and the cannon (GunshipFire) or the drone launches.
void Arm(Jet& j,const Kind& kind,unsigned char* v,const float* pos,const float* nose,const float* lead,bool gunsOk,bool missileOk,
         const Arms& arms,ULONGLONG ms) noexcept {
    if(kind.weapon==Weapon::guns){Fire(j,v,pos,nose,lead,gunsOk,missileOk,arms,ms);return;}
    v[kFireGun]=0;v[kFireMissile]=0;
    if(kind.weapon==Weapon::shells)GunshipFire(j,v,pos,ms);
    else if(kind.weapon==Weapon::drones)LaunchDrones(j,pos,nose,ms);
}

// Once a game frame (JetReap): every entry whose jet is finished is let go of; with JetPilot turned off, every
// jet still flown is deleted (unflown, it would hover on its dummy pilot where it was).
void Sweep(ULONGLONG ms) noexcept {
    const bool off=!Cfg().jetPilot;
    bool changed=false;
    int left=0;
    for(auto& j:jets) {
        if(!j.ref)continue;
        if(Finished(j)){Release(j);changed=true;continue;}
        if(off && !j.reap){j.reap=true;j.why="JetPilot off";++left;}
    }
    if(left)Log("JET JetPilot off: %d jets deleted",left);
    if(changed)Publish(true);
    BoosterSweep(ms);
}
}  // namespace

bool Alive(const ObjRef& r) noexcept {
    if(!r.obj || !r.ctrl || !Readable(r.ctrl,0x10) || At<long>(r.ctrl,8)<=0)return false;
    const auto o=static_cast<const unsigned char*>(r.obj);
    return Readable(o,kSeats+8) && At<const void*>(o,kSelfCtrl)==r.ctrl && !(o[kObjFlags]&kObjDeleted);
}

Jet* FindJet(const unsigned char* v) noexcept {
    if(!v)return nullptr;
    const void* const ctrl=At<const void*>(v,kSelfCtrl);
    for(auto& j:jets)if(j.ref.obj==v && j.ref.ctrl==ctrl)return &j;
    return nullptr;
}

bool SlotFree() noexcept {
    for(const auto& j:jets)if(!j.ref || Finished(j))return true;
    TableFull();
    return false;
}

Jet* NewEntry(unsigned char* v,ULONGLONG ms) noexcept {
    Role role=Role::fighter,drones=Role::drone;
    const ObjRef ref=ObjRef::Of(v);
    if(!ref.ctrl || !IsJetVehicle(v,&role,&drones))return nullptr;
    Jet* const j=FreeSlot();
    if(!j)return nullptr;
    *j=Jet{};
    j->ref=ref;HoldRef(ref);
    j->role=role;j->carrier.drones=drones;j->carrier.sorties=kCarrierSorties;j->drone.slot=-1;
    j->bornAt=j->modeAt=j->seen=ms;
    j->lastStep=0;   // the first step is a frame long (GameStep)
    return j;
}

void JoinFlight(Jet& j,unsigned flight) noexcept {
    j.flight=flight;j.wing=0;
    for(const auto& o:jets)if(o.ref && &o!=&j && o.flight==flight)++j.wing;
}

bool IsJetVehicle(const unsigned char* v,Role* role,Role* drones) noexcept {
    if(crew::BodyOf(v)!=PluginBody::jet)return false;
    const float k=BodyMark(v);
    for(const auto& b:kBodies) {
        if(b.mark<=0.0f || k!=b.mark)continue;
        if(role)*role=b.role;
        if(drones)*drones=b.drones;
        return true;
    }
    return false;
}
bool HostileJet(const unsigned char* v) noexcept {
    if(crew::BodyOf(v)!=PluginBody::jet)return false;
    const float k=BodyMark(v);
    for(const auto& b:kBodies)if(b.mark>0.0f && k==b.mark)return b.hostile;
    return false;
}
}  // namespace jet

using namespace jet;

int FaultLog(const char* where,const EXCEPTION_POINTERS* e) noexcept {
    // Any thread (the bullets' pass-through may not be the game thread's): the throttle table under a lock.
    static SRWLOCK lock=SRWLOCK_INIT;
    static struct { const char* where; ULONGLONG at; } seen[32]{};
    const ULONGLONG now=GetTickCount64();
    AcquireSRWLockExclusive(&lock);
    decltype(&seen[0]) s=nullptr;
    for(auto& x:seen)if(x.where==where || !x.where){s=&x;break;}   // its slot, else the first free one
    if(!s)s=&seen[0];   // more sites than slots: the first one is shared
    if(s->where!=where){s->where=where;s->at=0;}
    const bool quiet=s->at && now-s->at<5000;
    if(!quiet)s->at=now;
    ReleaseSRWLockExclusive(&lock);
    if(quiet)return EXCEPTION_EXECUTE_HANDLER;
    const auto r=e->ExceptionRecord;
    const auto at=static_cast<const unsigned char*>(r->ExceptionAddress);
    Log("FAULT in %s: %08lX at %p (EDF+%llX)",where,r->ExceptionCode,at,static_cast<unsigned long long>(at-image));
    return EXCEPTION_EXECUTE_HANDLER;
}

// The NPC jets' flares (the user, 2026-10-05: the NPC jets drop them too): every kNpcFlareLookMs a jet with a missile
// homing on it (missile.cpp MissileHoming) drops a pair with kNpcFlareReact, at most one every kNpcFlareGapMs, while it
// has pairs (Jet::flares); its burning flares are drawn (booster.cpp FlareFlames).
constexpr ULONGLONG kNpcFlareLookMs=500,kNpcFlareGapMs=1500;
constexpr float kNpcFlareReact=0.6f,kNpcFlareRadius=20.0f,kNpcFlareBack=8.0f,kNpcFlareSide=12.0f,kNpcFlareKeep=0.6f;
void NpcFlares(Jet& j,unsigned char* v,const float* pos,const float* nose,ULONGLONG ms) noexcept {
    static unsigned seed=0x85EBCA6Bu;
    if(j.flares>0 && ms-j.flareLook>=kNpcFlareLookMs && ms-j.flareAt>=kNpcFlareGapMs) {
        j.flareLook=ms;
        seed=seed*1664525u+1013904223u;
        if(MissileHoming(pos,kNpcFlareRadius) && static_cast<float>(seed>>8)/16777216.0f<kNpcFlareReact) {
            j.flares--;j.flareAt=ms;
            const float* m=reinterpret_cast<const float*>(v+kMatrix);
            float right[3]={m[0],m[1],m[2]};
            Normalize(right);
            for(int side=-1;side<=1;side+=2) {
                float at[3],vel[3];
                for(int i=0;i<3;++i){at[i]=pos[i]-nose[i]*kNpcFlareBack;vel[i]=j.m.vel[i]*kNpcFlareKeep+right[i]*kNpcFlareSide*static_cast<float>(side);}
                FlareDrop(v,at,vel,nose,side<0);
            }
            if(Cfg().debug)Log("JET v=%p flares at a missile (%d pairs left)",v,j.flares);
        }
    }
    float at[8][3],vel[8][3];
    const int n=FlaresOf(v,at,vel,8);
    if(n || j.flares<4)FlareFlames(v,at,vel,n,ms);
}

namespace {
// The player at the gunship's gun (playerjet_crew.inc): held for them as a jet the player flies is (jet::ResumeNpc,
// playerjet_board.inc Held): its fuel clock stands (the frame's time added to its birth, as ResumeNpc adds a held
// stretch), it does not withdraw (Leave not asked; a withdrawal begun before they boarded called off) and is not
// deleted (JetReap); its NPC pilot flies on.
void GunnerHold(Jet& j,ULONGLONG ms) noexcept {
    if(j.seen && ms>j.seen)j.bornAt+=ms-j.seen;
    if(j.mode!=Mode::withdraw)return;
    j.reap=false;j.why=nullptr;
    SetMode(j,Mode::patrol,ms);
    Log("JET v=%p withdrawal called off: the player is at its gun",j.Vehicle());
}

// The gunner's pylon turn: round the point they last shelled, else the call's own point (a launched one's strike point),
// else where they boarded (a placed or following one works round the player, and the player is aboard).
const float* GunnerAnchor(const Jet& j,const GunnerOrder& o) noexcept {
    return o.centred ? o.at : j.launched ? j.anchor : o.home;
}
}  // namespace

void JetFrame(unsigned char* v) noexcept {
    if(!HooksOk())return;
    // Called down for the player (its NPC pilot still aboard): playerjet.cpp flies it, its fuel clock standing (ResumeNpc).
    if(PlayerJetHolds(v))return;
    const ULONGLONG ms=GameMs();
    Publish(false);
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    Jet* j=FindJet(v);
    if(!j)j=CrewPlaced(v,pos,ms);
    if(!j)return;   // kMaxJets flying: this one is not taken over (FreeSlot logged it)
    // The player at the gunship's gun, its NPC pilot flying on (playerjet_crew.inc): held for them as the pilot seat
    // is (GunnerHold), its pylon turn round the point they shell (GunnerAnchor), its own gun theirs.
    GunnerOrder crewOrder{};
    const bool gunner=PlayerGunnerOrder(v,&crewOrder);
    if(gunner)GunnerHold(*j,ms);
    j->seen=ms;
    FarRender(*j,v);
    Put<float>(v,kAreaInset,kNoInset);
    const float gone=WorldHalf()-kWorldGoneIn;
    if(std::fabs(pos[0])>gone || std::fabs(pos[2])>gone) {
        if(!j->reap)Log("JET v=%p at the world's edge (%.0f,%.0f): deleting",v,pos[0],pos[2]);
        j->reap=true;
    }
    const float dt=GameStep(j->lastStep ? ms-j->lastStep : 0);   // a slow frame moves the world no more than 1/60 s
    j->lastStep=ms;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    float nose[3]={m[8],m[9],m[10]};
    if(!Normalize(nose)){nose[0]=0;nose[1]=0;nose[2]=1;}
    // The stock input stays out of it: rotor spinning, no stick.
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kInThrottle,1.0f);Put<float>(v,kInW,1.0f);
    if(j->drone.blastAt){Blast(*j,v,ms);return;}

    const Kind& kind=KindOf(*j);
    Arms arms=ReadArms(v);
    j->burden=BurdenOf(BodyMark(v),arms.stores,arms.storeCount);
    const bool follow=player.at && ms-player.at<10000;
    // A drone works round its carrier, a launched jet round its strike point, a placed one guards the
    // player. Each withdraws away from the player (`viewer`), so it is deleted out of their sight.
    Jet* const mother=MotherOf(*j);
    if(j->escort && follow && !gunner)std::memcpy(j->anchor,player.pos,12);   // the player aboard: player.pos is its own
    // The player's carrier sends its drones to a point (PlayerLaunchDrone): they work round that.
    const bool ordered=mother && mother->carrier.ordered;
    const float* anchor=gunner ? GunnerAnchor(*j,crewOrder) : ordered ? mother->carrier.order :
                        mother ? reinterpret_cast<const float*>(mother->Vehicle()+kPosition) : follow && !j->launched ? player.pos : j->anchor;
    const float* viewer=follow ? player.pos : anchor;
    const float hp=At<float>(v,kHp),hpMax=At<float>(v,kHpMax);
    if(!gunner)Leave(*j,kind,arms,mother,hp,hpMax,ms);   // held for the player at the gun: no withdrawal
    const bool walled=Sense(*j,pos,ms);

    // The target and its motion.
    if(j->mode!=Mode::withdraw && j->mode!=Mode::takeoff && j->mode!=Mode::recover)PickTarget(*j,v,pos,anchor,ordered ? kOrderRange : TargetRange(kind),dt,ms);
    else j->t.target=nullptr;
    if(j->t.target){const float to[3]={j->t.aim[0]-pos[0],j->t.aim[1]-pos[1],j->t.aim[2]-pos[2]};PickStore(arms,j->t.flyer,Len(to));}
    if(kind.weapon==Weapon::charge && j->t.target && j->mode!=Mode::withdraw && j->mode!=Mode::recover) {
        const float to[3]={j->t.aim[0]-pos[0],j->t.aim[1]-pos[1],j->t.aim[2]-pos[2]};
        const float d=Len(to);
        if(d<kind.trigger || (walled && d<kind.trigger*kTriggerHeld)){Detonate(*j,mother,d,ms);Blast(*j,v,ms);return;}
    }
    float lead[3];
    if(j->t.target)Lead(pos,j->t.aim,j->t.tgtVel,arms,lead);
    else std::memcpy(lead,pos,12);

    // Guidance, then the flight its kind flies.
    const float clear=GroundClearance(pos);
    const float base=j->t.target && !j->t.flyer ? j->t.aim[1] : anchor[1];
    const float height=base+kind.alt;
    float want[3]={nose[0],0,nose[2]},speed=kind.cruise;
    bool gunsOk=false,missileOk=false;
    Guide(*j,kind,arms,mother,pos,nose,anchor,viewer,lead,height,clear,walled,ms,want,&speed,&gunsOk,&missileOk);
    if(kind.flight==FlightModel::rotor)Rotor(*j,kind,v,mother,pos,anchor,want,height,hp,hpMax,dt,ms);
    else Wing(*j,kind,v,pos,nose,want,speed,dt,ms);
    HoldOffGround(*j,pos,clear,dt,ms);
    j->m.ready=true;
    BayFrame(*j,pos);
    if(gunner){v[kFireGun]=0;v[kFireMissile]=0;}   // the gun is the player's (playerjet_crew.inc GunnerFire)
    else Arm(*j,kind,v,pos,nose,lead,gunsOk,missileOk,arms,ms);
    DollFrame(IndexOf(*j),v,clear);
    NpcFlares(*j,v,pos,nose,ms);
    if(Cfg().debug && ms-j->loggedAt>1000){j->loggedAt=ms;JetLog(*j,v,pos,arms,speed,clear,ms);}
}

// A reaped jet's NPC pilot is put off and the jet deleted only once the game has taken it as empty: its team then
// 5 (nobody's vehicle, the stock emptied-vehicle step: 0x118A4B0 state, then SetTeam(5, registered), 0x5E6548 /
// 0x5E7AE3), or kReapSettleFrames after the kick. The delete (0x118A1B0) takes the object out of its team's set
// (vtable +0x40, 0x54A290: team -1) while it lives on until the manager frees it; a kick in the same frame had the
// emptied step run after it and register the deleted jet in team 5's set again, where its address stayed once it
// was freed: the next walk of team 5 (the on-foot board prompt) read freed memory (crash 2026-10-05 00:19, a jet
// deleted at the world's edge).
constexpr ULONGLONG kReapSettleFrames=30;

namespace {
// Who is aboard: the player in any seat, else an NPC in any, else nobody.
Rider Aboard(unsigned char* v) noexcept {
    Rider r=Rider::none;
    for(unsigned i=0;i<SeatCount(v);++i) {
        const Rider s=SeatRider(SeatAt(v,i));
        if(s==Rider::player)return s;
        if(s==Rider::dummy)r=s;
    }
    return r;
}
}  // namespace

void JetReap(const void* self) noexcept {
    const ULONGLONG ms=GameMs();
    static ULONGLONG frame=~0ull;
    const ULONGLONG f=GameFrame();
    if(f!=frame){frame=f;Publish(false);Sweep(ms);}
    bool changed=false;
    for(auto& j:jets) {
        if(!j.ref || !j.reap || j.ref.obj==self)continue;
        // Only the same object, still there: one destroyed meanwhile is the game's (its entry just goes).
        unsigned char* const v=j.Vehicle();
        if(Alive(j.ref) && !v[kDead] && crew::BodyOf(v)==PluginBody::jet) {
            // Its riders off first (the gunship's gunner too), the delete only once the jet has taken itself as empty
            // (see kReapSettleFrames); never with the player in any of its seats.
            const Rider aboard=Aboard(v);
            if(aboard!=Rider::none) {
                if(aboard==Rider::dummy && !j.emptyFrame) {
                    for(unsigned i=0;i<SeatCount(v);++i)
                        if(SeatRider(SeatAt(v,i))==Rider::dummy)reinterpret_cast<KickFn>(image+kSeatKick)(v,SeatAt(v,i));
                    j.emptyFrame=f;
                }
                continue;   // a player aboard (or the NPC not off yet): no delete under them
            }
            if(j.emptyFrame && At<std::int32_t>(v,kTeam)!=kTeamVehicle && f-j.emptyFrame<kReapSettleFrames)continue;
            reinterpret_cast<DeleteFn>(image+kDelete)(v);
            Log("JET v=%p gone (deleted%s%s)",v,j.why ? ": " : "",j.why ? j.why : "");
        }
        Release(j);
        changed=true;
    }
    if(changed)Publish(true);
}

// The jet handed back to its NPC pilot (playerjet_board.inc: the player left it in the air, the catch or the hail ran
// out), flying at `vel`. Its flight starts afresh from there; its fuel clock stood while the plugin did not fly it (the
// time since it last did is added to its birth: fuel, a sortie's time and a drone's sortie count on), so it goes back
// to its call with what it had, under its own rules (Leave: low on fuel or HP, out of ammo, it withdraws); its drones'
// orders are dropped (they work round the carrier again); a bomber whose run is not over flies it again. Mode: as it
// was when withdrawing, bombing or going back to its carrier, else patrol (takeoff off the ground).
void jet::ResumeNpc(unsigned char* v,const float* vel) noexcept {
    const ULONGLONG ms=GameMs();
    Jet* const j=FindJet(v);
    if(!j)return;   // its first frame with its pilot makes the entry (CrewPlaced)
    if(ms>j->seen)j->bornAt+=ms-j->seen;
    j->seen=ms;j->lastStep=0;
    std::memcpy(j->m.vel,vel,12);
    std::memset(j->m.omega,0,12);std::memset(j->m.acc,0,12);
    j->m.ready=false;j->m.prevAt=0;j->m.blockedFor=0;
    j->t.target=nullptr;j->t.lockAt=0;j->t.lockSeen=0;
    j->carrier.ordered=false;
    if(j->mode!=Mode::withdraw && j->mode!=Mode::bomb && j->mode!=Mode::recover) {
        const float clear=GroundClearance(reinterpret_cast<const float*>(v+kPosition));
        SetMode(*j,clear!=kNoGround && clear<kTakeoffClear ? Mode::takeoff : Mode::patrol,ms);
    }
    Log("JET v=%p back to its NPC pilot: %s, %.0f m/s, %.0f s of its fuel flown",v,kModeNames[static_cast<int>(j->mode)],Len(vel),
        static_cast<float>(ms-j->bornAt)*0.001f);
    Publish(true);
}

// One of ours the player boarded that has no entry: a mission placed it empty (testrange/gen.py plan.vehicles), so no
// NPC pilot ever sat in it and JetFrame never made one (CrewPlaced runs on its pilot's first frame). What the player's
// flight reads off the entry was missing: a rotor craft's hover (playerjet_board.inc HoverStep: no entry, it never
// lifted), a carrier's drones (DronesLeft 0), and a jet left in the air came back to an NPC in takeoff mode at 200 m/s.
// Made now as a placed jet's (its fuel clock stands while the player holds it: ResumeNpc). nullptr: not one of our jets,
// JetPilot off (Sweep would delete it at once), kMaxJets.
jet::Jet* jet::Adopt(unsigned char* v) noexcept {
    if(Jet* j=FindJet(v))return j;
    if(!HooksOk() || !Cfg().jetPilot)return nullptr;
    Jet* const j=CrewPlaced(v,reinterpret_cast<const float*>(v+kPosition),GameMs());
    if(j)Log("JET v=%p: its entry made for the player who boarded it empty",v);
    return j;
}

// A new mission (mission.cpp MissionStart): every entry, doll and learned wall of the last one is forgotten.
// Its objects are not touched (their bays' destructors, their Delete): they went with that mission. The weak
// reference each entry holds is dropped: it is what kept the control block alive (HoldRef), so the block is
// still there to drop it from, and keeping it would leak the block every mission.
void ResetJets() noexcept {
    for(auto& j:jets){DropRef(j.ref);j=Jet{};}
    ResetDolls();
    ResetWalls();
    ResetTargets();
    ResetFlights();
    ResetShells();
    Publish(true);
}

// Whether a jet has `target` in its missile lock now: its nose came onto it within the missile cone (Aim::lockAt, set
// by jet_combat.cpp Fire). Only enemies make the player's jet their target: the player's lock warning (playerjet.cpp).
bool jet::LockingOn(const void* target) noexcept {
    if(!target)return false;
    for(const auto& j:jets)if(j.ref && j.t.target==target && j.t.lockAt && Alive(j.ref))return true;
    return false;
}

int jet::LockersOf(const void* target,float (*at)[3],int most) noexcept {
    int n=0;
    if(!target)return 0;
    for(const auto& j:jets)
        if(n<most && j.ref && j.t.target==target && j.t.lockAt && Alive(j.ref))std::memcpy(at[n++],j.Vehicle()+kPosition,12);
    return n;
}

int jet::BreakLocks(const void* target,float chance) noexcept {
    static unsigned seed=0x9E3779B9u;
    int broke=0;
    for(auto& j:jets) {
        if(!j.ref || j.t.target!=target || !j.t.lockAt || !Alive(j.ref))continue;
        seed=seed*1664525u+1013904223u;
        if(static_cast<float>(seed>>8)/16777216.0f<chance){j.t.lockAt=0;++broke;}   // the lock starts over
    }
    return broke;
}

bool IsJet(const void* vehicle) noexcept {
    return IsJetVehicle(static_cast<const unsigned char*>(vehicle),nullptr,nullptr);
}

bool JetFlying(const void* vehicle,const void* ctrl) noexcept {
    if(!vehicle)return false;
    for(const auto& j:jets)
        if(j.ref.obj==vehicle && j.ref.ctrl==ctrl && !j.reap && j.mode!=Mode::withdraw && Alive(j.ref) && !j.Vehicle()[kDead])return true;
    return false;
}

bool JetHolds(const void* hold) noexcept {
    if(!hold)return false;
    for(const auto& j:jets)if(j.ref && j.bay.hold==hold && j.bay.ifc)return true;
    return false;
}

// Only without the pass-through (jet_hooks.cpp): a wingman of `self`'s flight by the segment.
bool JetInLine(const float* from,const float* to,const void* self) noexcept {
    if(PassThrough())return false;
    const ULONGLONG ms=GameMs();
    const Jet* me=nullptr;
    for(const auto& o:jets)if(o.ref.obj==self)me=&o;
    if(!me)return false;
    for(const auto& o:jets)
        if(&o!=me && o.flight==me->flight && o.m.prevAt && Flown(o,ms) && NearLine(from,to,o.m.prevPos,kJetSpan))return true;
    return false;
}

bool JetHud(const void* vehicle,JetHudInfo* out) noexcept {
    const ULONGLONG ms=GameMs();
    const Jet* const j=FindJet(static_cast<const unsigned char*>(vehicle));
    if(!j)return false;
    const ULONGLONG flown=ms-j->bornAt;
    out->role=KindOf(*j).name;
    out->fuelSec=j->drone.carried ? -1.0f : j->fuelMs>flown ? static_cast<float>(j->fuelMs-flown)*0.001f : 0.0f;
    out->drones=KindOf(*j).weapon==Weapon::drones ? j->carrier.sorties : -1;
    out->leaving=j->mode==Mode::withdraw;
    return true;
}
}  // namespace crew
