// The production player-jet catch, touchdown and ground controls against stand-in game state. No game is loaded.
#include "../src/playerjet.cpp"
#include <cstdio>
#include <cstdlib>
#include "../src/config.h"
#include "../src/plugin_log.h"
#include "../src/bay_rounds.h"
#include "../src/boarding.h"
#include "../src/plugin_body.h"
#include "../src/booster.h"
#include "../src/dreadnought.h"
#include "../src/hud.h"
#include "../src/launchers.h"
#include "../src/map.h"
#include "../src/missile.h"
#include "../src/shield.h"
#include "../src/sightzoom_api.h"
#include "../src/vhud.h"

namespace crew {
bool payloadScenario=false,payloadMap=false;
Store payloadStores[kMostStores]{};int payloadCount=0,payloadChoice=-1,payloadTriggers=0,payloadForgets=0;
unsigned char* payloadHuman=nullptr;
int AircraftPayloadChoice(unsigned char*,const Store*,int,int) noexcept { return payloadScenario ? payloadChoice : -1; }
void ForgetAircraftPayload(const void*) noexcept { if(payloadScenario)++payloadForgets; }
bool AiGunner(const unsigned char*,const unsigned char*) noexcept { return false; }
bool NpcDriver(const unsigned char* v) noexcept { return v && SeatCount(v)>0 && SeatRider(SeatAt(const_cast<unsigned char*>(v),0))==Rider::other; }
bool JetFliesItself(const void*) noexcept { return false; }   // the recovered aircraft are crewed ones, no launched drone
bool DreadnoughtTakesBack(const unsigned char*) noexcept { return false; }   // no dreadnought in these scenes

bool ReadRound(const unsigned char*,RoundModel*) noexcept {return false;}
bool RoundLands(const unsigned char*,const RoundModel&,const float*,const float*,float,float*,float*) noexcept {return false;}
float SightZoomNow(const void*) noexcept {return 1.0f;}
void SightZoomFrame(unsigned char*,unsigned,bool) noexcept {}
unsigned char* image=nullptr;
bool recoveryAuthority=true,rescueChoiceScenario=false;int crewRequests=0,npcResumes=0;
bool OnlineRunsHere(const void*) noexcept {return recoveryAuthority;}
// Offline (online_authority.h): an NPC rider may be seated, through the vehicle's own RideAi.
bool SeatNpcRider(unsigned char*,bool) noexcept {++crewRequests;return false;}
Config recoveryConfig{};
PlayerFix player{};
ULONGLONG recoveryTime=10000;
bool testGearDown=true,testDoor=true;
float doorOffset[3]={8.65f,-1.38f,1.8f};
int boardPresses=0,boardRequests=0;
unsigned char recoveryBone[0x200]{};
bool ramScenario=false,ramDeathWorks=true;
int ramEnemyCount=0,ramBlasts=0,ramDeaths=0,ramVisits=0;
float ramDamage=0.0f;
alignas(16) unsigned char ramEnemies[20][0x200]{};
const JetMass ramMass{7201.0f,16000.0f,1000.0f};
const Config& Cfg() noexcept { return recoveryConfig; }
ULONGLONG GameMs() noexcept { return recoveryTime; }
ULONGLONG GameFrame() noexcept { return recoveryTime/16; }
float GameStep(ULONGLONG) noexcept { return 1.0f/60.0f; }
void Log(const char*,...) noexcept {}
bool MapHoldsKeys() noexcept { return payloadScenario && payloadMap; }
const void* BoardingOnly() noexcept {return nullptr;}
bool GearDown(const void*) noexcept { return testGearDown; }
float GearDragShare(const void*) noexcept { return 0.0f; }
GearState GearStep(unsigned char*,bool,float,bool) noexcept { return GearState{}; }
void JetFlames(const unsigned char*,float,bool,ULONGLONG) noexcept {}
void BodyAttitude(const unsigned char*,const float*,const float*,float,float,float* omega) noexcept { omega[0]=omega[1]=omega[2]=0.0f; }
float testGround=-1.0e9f;   // the ground's height in the catch scenes that set one (else 500 m under everything)
float GroundClearance(const float* p) noexcept { return testGround>-1.0e8f ? p[1]-testGround : 500.0f; }
Sea SeaAt(float,float,float*) noexcept { return Sea::land; }
PlayArea MapPlayArea() noexcept { return PlayArea{{-2000,-2000},{2000,2000},false,0.0f,false}; }
float MapRay(const float*,const float*,float*) noexcept { return -1.0f; }
unsigned char* BoneRecord506(const unsigned char*,const wchar_t*) noexcept { return recoveryBone; }
bool SeatPoint(const unsigned char* v,unsigned,float* at,float* reach) noexcept {
    if(!testDoor)return false;
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    for(int i=0;i<3;++i)at[i]=p[i]+doorOffset[i];
    *reach=3.0f;
    return true;
}
void PressBoardButton(unsigned char*) noexcept { ++boardPresses; }
// The catch's capture (boarding.cpp): counted, never under way across frames here.
bool boardRefuses=false,boardReady=true;
bool BoardingRequest(unsigned char*) noexcept { ++boardRequests;return !boardRefuses; }
bool BoardingReady() noexcept { return boardReady; }
bool BoardingUnderway() noexcept { return false; }
// Unrelated stores, weapons, spawning and NPC paths must not be reached by these recovery scenarios.
void MissingRecoveryDependency() noexcept { void(*volatile stop)()=std::abort;stop(); }
int ReadStores(unsigned char*,Store* out,int most) noexcept {
    if(!payloadScenario){MissingRecoveryDependency();return 0;}
    const int n=payloadCount<most ? payloadCount : most;for(int i=0;i<n;++i)out[i]=payloadStores[i];return n;
}
void TriggerStore(const Store&) noexcept { if(payloadScenario){++payloadTriggers;return;}MissingRecoveryDependency(); }
Burden BurdenOf(float,const Store*,int) noexcept { if(!payloadScenario)MissingRecoveryDependency();return Burden{1.0f,0.0f}; }
const JetMass* JetMassOf(float) noexcept { if(!ramScenario)MissingRecoveryDependency();return &ramMass; }
bool IsStoreWeapon(const unsigned char*) noexcept { MissingRecoveryDependency();return false; }
bool LevelVehicle(unsigned char*) noexcept { MissingRecoveryDependency();return false; }
void LogImpact(const char*,const void*,const float*,const float*) noexcept { if(!ramScenario)MissingRecoveryDependency(); }
bool LastViewProj(float*) noexcept { MissingRecoveryDependency();return false; }
bool CameraRay(float*,float*) noexcept { MissingRecoveryDependency();return false; }
void SetObjectTeam(unsigned char*,std::int32_t) noexcept { MissingRecoveryDependency(); }
float ShieldBlock(const unsigned char*,float*) noexcept { MissingRecoveryDependency();return 0.0f; }
bool JetMotionProps(void*) noexcept { MissingRecoveryDependency();return false; }
JetBody BomberBody(const unsigned char*) noexcept { MissingRecoveryDependency();return JetBody{}; }
PluginBody BodyOf(const void*) noexcept {if(rescueChoiceScenario)return PluginBody::jet;MissingRecoveryDependency();return PluginBody{};}
float BodyMark(const void*) noexcept {if(rescueChoiceScenario)return pjet::MarkOf(jet::Body::fighter);if(!ramScenario)MissingRecoveryDependency();return ramMass.mark;}
bool Body506Ok() noexcept { MissingRecoveryDependency();return false; }
bool Body506ReadsSolver() noexcept { MissingRecoveryDependency();return false; }
bool ImpactDamage(const unsigned char*,const float*,float damage,float) noexcept {
    if(!ramScenario)MissingRecoveryDependency();
    ++ramBlasts;ramDamage=damage;return true;
}
bool VisitEnemies(const unsigned char*,EnemyVisitor visit,void* ctx) noexcept {
    if(!ramScenario)MissingRecoveryDependency();
    ++ramVisits;
    for(int i=0;i<ramEnemyCount;++i)visit(ctx,ramEnemies[i],reinterpret_cast<const float*>(ramEnemies[i]+kPosition));
    return true;
}
bool RoundImpact(const float*,const float*,const float*,int,float*,float*) noexcept { MissingRecoveryDependency();return false; }
bool MissileHoming(const float*,float) noexcept { if(!payloadScenario)MissingRecoveryDependency();return false; }
int MissilesHomingAt(const float*,float,float (*)[3],int) noexcept { MissingRecoveryDependency();return 0; }
void FlareDrop(const void*,const float*,const float*,const float*,bool) noexcept { MissingRecoveryDependency(); }
void FlaresStep() noexcept { MissingRecoveryDependency(); }
int FlaresOf(const void*,float (*)[3],float (*)[3],int) noexcept { if(!payloadScenario)MissingRecoveryDependency();return 0; }
void FlareFlames(const unsigned char*,const float (*)[3],const float (*)[3],int,ULONGLONG) noexcept { if(!payloadScenario)MissingRecoveryDependency(); }
unsigned char* PlayerHuman() noexcept { if(payloadScenario)return payloadHuman;MissingRecoveryDependency();return nullptr; }
bool FuelGauge(const void*,FuelReading*) noexcept { MissingRecoveryDependency();return false; }
bool IsFuelTank(const unsigned char*) noexcept { MissingRecoveryDependency();return false; }
bool FixBodyPart506(unsigned char*,const char*) noexcept { MissingRecoveryDependency();return false; }
bool BodyPartOk() noexcept { MissingRecoveryDependency();return false; }
void HingePose(const float*,float,float*) noexcept { MissingRecoveryDependency(); }
bool Body506MessageOk() noexcept { MissingRecoveryDependency();return false; }
bool Die506(unsigned char* v) noexcept {
    if(!ramScenario)MissingRecoveryDependency();
    ++ramDeaths;if(ramDeathWorks){v[kDead]=1;Put<float>(v,kHp,0.0f);}return ramDeathWorks;
}
bool Die506Ok() noexcept { MissingRecoveryDependency();return false; }
int WeaponLock(const unsigned char*,float*,float*) noexcept { if(!payloadScenario)MissingRecoveryDependency();return 0; }
void ClearWeaponLock(unsigned char*) noexcept { if(!payloadScenario)MissingRecoveryDependency(); }
const void* WeaponLockObject(const unsigned char*) noexcept { MissingRecoveryDependency();return nullptr; }
const void* WeaponLockingObject(const unsigned char*) noexcept { MissingRecoveryDependency();return nullptr; }
bool MapPadState(std::uint16_t*,std::uint8_t*,std::uint8_t*) noexcept { MissingRecoveryDependency();return false; }
void NextLockTarget(unsigned char*) noexcept { MissingRecoveryDependency(); }
GearState PlayerGear(unsigned char*,bool,bool,bool,float,float,float,float) noexcept { MissingRecoveryDependency();return GearState{}; }
float ClosureIn(const float*,const float*,float,float,float,float,bool*) noexcept { MissingRecoveryDependency();return -1.0f; }
Gpws GpwsOf(float,bool) noexcept { MissingRecoveryDependency();return Gpws::none; }
namespace audio { void LockTone(int,float,const void*,const void*) noexcept { if(!payloadScenario)MissingRecoveryDependency(); } }
namespace jet {
Jet jets[kMaxJets]{};
bool SpawnReady() noexcept { MissingRecoveryDependency();return false; }
bool ModFileThere(const wchar_t*) noexcept { MissingRecoveryDependency();return false; }
bool LockingOn(const void*) noexcept { if(!payloadScenario)MissingRecoveryDependency();return false; }
int BreakLocks(const void*,float) noexcept { MissingRecoveryDependency();return 0; }
int LockersOf(const void*,float (*)[3],int) noexcept { MissingRecoveryDependency();return 0; }
bool Alive(const ObjRef& r) noexcept {return r && r.Is(r.obj) && !At<unsigned char>(r.obj,kDead);}
bool seedScenario=false;Jet* seedJet=nullptr;   // the NPC flight's entry a takeover finds (or none)
Jet* FindJet(const unsigned char*) noexcept { if(!seedScenario)MissingRecoveryDependency();return seedJet; }
void HoldOffGround(Jet&,const float*,float,float,ULONGLONG,float) noexcept { MissingRecoveryDependency(); }
void Hover(Jet&,const Kind&,const unsigned char*,const float*,const float*,const float*,float,float,float,float,bool) noexcept { MissingRecoveryDependency(); }
void Thrusters(Jet&,const Kind&,unsigned char*,float,ULONGLONG,float) noexcept { MissingRecoveryDependency(); }
bool PlayerLaunchDrone(unsigned char*,const float*,ULONGLONG) noexcept { MissingRecoveryDependency();return false; }
int RecallDrones(unsigned char*,ULONGLONG) noexcept { MissingRecoveryDependency();return 0; }
int DronesLeft(const unsigned char*) noexcept { MissingRecoveryDependency();return 0; }
int BayLeft(const unsigned char*) noexcept { MissingRecoveryDependency();return 0; }
bool PlayerOpenBay(unsigned char*,const float*,const float*) noexcept { MissingRecoveryDependency();return false; }
void PlayerBayFrame(unsigned char*,const float*) noexcept { MissingRecoveryDependency(); }
bool PlayerShell(unsigned char*,const float*,ULONGLONG) noexcept { MissingRecoveryDependency();return false; }
bool ShellsReady() noexcept { MissingRecoveryDependency();return false; }
bool CrewShell(unsigned char*,float,ULONGLONG) noexcept { MissingRecoveryDependency();return false; }
float ShellWait(const unsigned char*,ULONGLONG) noexcept { MissingRecoveryDependency();return 0.0f; }
bool PlayerSideGun(unsigned char*,SideGun,const float*,ULONGLONG) noexcept { MissingRecoveryDependency();return false; }
bool SideGunReady(SideGun) noexcept { MissingRecoveryDependency();return false; }
float SideGunWait(const unsigned char*,SideGun,ULONGLONG) noexcept { MissingRecoveryDependency();return 0.0f; }
float SideGunReach(SideGun) noexcept { MissingRecoveryDependency();return 0.0f; }
float ShellReach() noexcept { MissingRecoveryDependency();return 0.0f; }
void ResumeNpc(unsigned char*,const float*) noexcept {++npcResumes;}
Jet* Adopt(unsigned char*) noexcept { MissingRecoveryDependency();return nullptr; }
}
}

namespace {
using namespace crew;
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
alignas(16) unsigned char recoveryVehicle[0x3000]{},recoveryCtrl[0x20]{},recoveryHuman[0x1600]{},recoverySeat[edf::kSeatStride]{},recoveryPilot[0x500]{};
void ResetJet(PJet& j) {
    j=PJet{};j.vehicle=recoveryVehicle;j.ref=ObjRef::Of(recoveryVehicle);j.kind=&kKinds[0];j.phase=Phase::air;
    j.driven=true;j.fresh=true;j.vel[2]=55.0f;j.vel[1]=-3.0f;
    j.throttle=0.55f;j.stall=true;j.stallShare=1.2f;j.hasAim=j.hasUp=j.mouseFlies=true;
}

// The takeoff (the user, 2026-10-11: 銆岄鏈鸿捣椋炵殑鏃跺€欎細鎶栨姈鐨勩€?: Fly's order of the production steps (Lever, ReconcileGround,
// Air or Ground) over a stand-in body on flat ground at y = 0 (its bottom at its position: rest 0), the body step as
// PlayerJetBodyStep sends it (in the air the flight's whole velocity; on the ground GroundContact over a solver that
// keeps the normal motion, gravity kGravity, the ground holding it up). The native contact byte (veh+0x1580 bit 1) as
// the contact callback leaves it: set by a step that began with the wheels within kSkin of the ground (Havok keeps the
// manifold of a contact still that near, and the step that lifts it off starts on it), read by the next frame's Fly.
struct TakeoffRun { int takeoffs,relandings,climbFlips; float clearAt1s,lowestClimbAfter; bool airAtEnd; };
TakeoffRun Takeoff(PJet& j,float boost) {
    constexpr float dt=1.0f/60.0f,kGravity=14.7f,kSkin=0.05f;
    ResetJet(j);
    j.phase=Phase::rolling;j.vel[1]=0.0f;j.vel[2]=j.kind->rotate-3.0f;j.throttle=1.0f;j.throttleIn=boost;
    j.hasUp=j.hasAim=j.mouseFlies=false;j.stall=false;
    float* body=reinterpret_cast<float*>(recoveryVehicle+kPosition);body[0]=body[1]=body[2]=0.0f;
    float* mat=reinterpret_cast<float*>(recoveryVehicle+kMatrix);
    std::memset(mat,0,64);mat[0]=mat[5]=mat[10]=mat[15]=1.0f;
    float solver[3]={0.0f,0.0f,j.vel[2]};
    recoveryVehicle[0x1580]=2;
    Stick s{};s.pitch=1.0f;s.throttle=boost;   // a pad: the stick held back from the roll on, the boost as given
    TakeoffRun r{0,0,0,0.0f,1e9f,false};
    Phase was=j.phase;float lastClimb=0.0f;bool flown=false;int frame=0;
    for(;frame<240;++frame) {
        Put<float>(recoveryBone,kBoneWorld506+0x34,body[1]);   // RestOver: 0
        const float clear=body[1];
        Lever(j,recoveryVehicle,s,dt);
        ReconcileGround(j,recoveryVehicle,clear,false,false,recoveryTime);
        // Counted where each happens: a frame that lands it (ReconcileGround) and lifts it off again (Ground) is both.
        if(was==Phase::air && j.phase!=Phase::air)++r.relandings;
        was=j.phase;
        if(j.phase==Phase::air)Air(j,recoveryVehicle,s,body,clear,false,dt,recoveryTime);
        else Ground(j,recoveryVehicle,s,clear,dt);
        if(was!=Phase::air && j.phase==Phase::air)++r.takeoffs;
        if(was==Phase::air && j.phase!=Phase::air)++r.relandings;
        was=j.phase;
        float lin[3],ang[3]={0.0f,0.0f,0.0f};
        if(j.phase==Phase::air)std::memcpy(lin,j.vel,12);
        else{std::memcpy(lin,solver,12);handling::GroundContact(mat+4,j.vel,j.omega,lin,ang);}
        const bool touching=body[1]<=kSkin;
        for(int i=0;i<3;++i)body[i]+=lin[i]*dt;
        lin[1]-=kGravity*dt;
        if(body[1]<0.0f){body[1]=0.0f;if(lin[1]<0.0f)lin[1]=0.0f;}
        if(body[1]<=0.0f && lin[1]<0.0f)lin[1]=0.0f;
        std::memcpy(solver,lin,12);
        recoveryVehicle[0x1580]=touching ? 2 : 0;
        if(j.phase==Phase::air) {
            if(flown && (lastClimb>0.0f)!=(j.vel[1]>0.0f))++r.climbFlips;
            if(flown && j.vel[1]<r.lowestClimbAfter)r.lowestClimbAfter=j.vel[1];
            lastClimb=j.vel[1];flown=true;
        }
        if(frame==60+30)r.clearAt1s=body[1];   // ~1 s after the roll reaches its rotate speed
        recoveryTime+=16;
    }
    r.airAtEnd=j.phase==Phase::air;
    return r;
}
}

int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(image!=nullptr,"stand-in image allocated");
    Put<const void*>(recoveryVehicle,kSelfCtrl,recoveryCtrl);Put<int>(recoveryCtrl,8,1);
    float* mat=reinterpret_cast<float*>(recoveryVehicle+kMatrix);mat[0]=mat[5]=mat[10]=mat[15]=1.0f;
    PJet& j=jets[0];ResetJet(j);
    Touch(j,recoveryVehicle,55.0f,false,recoveryTime);
    Check(j.phase==Phase::rolling && j.throttle==0.0f && !j.stall && !j.hasAim && !j.hasUp,"touchdown hands cruise throttle and air state to ground control");
    Stick stick{};stick.keys=true;stick.pitch=1.0f;
    j.vel[2]=j.kind->rotate+10.0f;
    Ground(j,recoveryVehicle,stick,1.0f,1.0f/60.0f);
    Check(j.phase==Phase::rolling && j.vel[1]<=0.0f,"holding the landing flare at idle cannot bounce back into takeoff");
    Lever(j,recoveryVehicle,Stick{},1.0f/60.0f);
    Check(j.throttle==0.0f,"released ground throttle stays idle instead of returning to cruise");
    for(int frame=0;frame<1800;++frame)Ground(j,recoveryVehicle,stick,1.0f,1.0f/60.0f);
    Check(j.phase==Phase::parked && vec::Len(j.vel)<0.01f,"idle rollout reaches parked despite a held landing flare");
    j.vel[2]=j.kind->rotate+10.0f;
    j.throttle=1.0f;Ground(j,recoveryVehicle,stick,1.0f,1.0f/60.0f);
    Check(j.phase==Phase::air && j.vel[1]>0.0f,"an explicit powered takeoff still works");
    ResetJet(j);j.phase=Phase::rolling;j.throttle=0.4f;j.vel[2]=j.kind->rotate+10.0f;
    Ground(j,recoveryVehicle,stick,1.0f,1.0f/60.0f);
    Check(j.phase==Phase::air,"manual rotation at partial power still works");
    ResetJet(j);j.phase=Phase::rolling;j.throttle=0.4f;j.vel[2]=j.kind->rotate+kAutoRotate+10.0f;
    Ground(j,recoveryVehicle,Stick{},1.0f,1.0f/60.0f);
    Check(j.phase==Phase::rolling,"automatic rotation still requires its original power threshold");
    ResetJet(j);j.throttle=1.0f;j.throttleIn=1.0f;Touch(j,recoveryVehicle,55.0f,false,recoveryTime);
    Check(j.throttle==1.0f,"explicit boost at touchdown preserves a touch-and-go request");
    ResetJet(j);j.vel[1]=0.4f;recoveryVehicle[0x1580]=2;
    ReconcileGround(j,recoveryVehicle,1.0f,false,false,recoveryTime);
    Check(j.phase==Phase::rolling,"native ground support lands a rebounding airframe even with upward velocity");
    ResetJet(j);ReconcileGround(j,recoveryVehicle,30.0f,false,false,recoveryTime);
    Check(j.phase==Phase::air,"contact on a high object is not confused with the map floor");
    ResetJet(j);recoveryVehicle[0x1580]=0;ReconcileGround(j,recoveryVehicle,1.0f,false,false,recoveryTime);
    Check(j.phase==Phase::air,"clearance alone does not invent native ground support");
    recoveryVehicle[0x1580]=2;ReconcileGround(j,recoveryVehicle,1.0f,true,false,recoveryTime);
    Check(j.phase==Phase::air,"water contact is not accepted as a safe landing");
    ReconcileGround(j,recoveryVehicle,1.0f,false,true,recoveryTime);
    Check(j.phase==Phase::air,"a native wet frame is not accepted as a safe landing");
    ResetJet(j);j.throttleIn=1.0f;Lever(j,recoveryVehicle,Stick{},1.0f/60.0f);
    ReconcileGround(j,recoveryVehicle,1.0f,false,false,recoveryTime);
    Check(j.throttle==0.0f,"releasing boost on the contact frame clears cruise throttle");
    // The keys' lever (War Thunder's, playerjet.cpp ReadStick): W / S move it and it stays, through a touchdown too.
    ResetJet(j);j.keys=true;
    {Stick keys{};keys.keys=true;keys.lever=true;keys.throttle=1.0f;Lever(j,recoveryVehicle,keys,0.1f);
     Check(std::fabs(j.throttle-0.7f)<1e-4f,"W held moves the keys' lever up at its air rate");
     keys.throttle=0.0f;for(int i=0;i<60;++i)Lever(j,recoveryVehicle,keys,1.0f/60.0f);
     Check(std::fabs(j.throttle-0.7f)<1e-4f,"let go, the keys' lever stays where W left it (no return to cruise)");}
    ReconcileGround(j,recoveryVehicle,1.0f,false,false,recoveryTime);
    Check(j.phase==Phase::rolling && std::fabs(j.throttle-0.7f)<1e-4f,"a touchdown keeps the keys' lever where the player set it");
    float bodyPos[3]={0.0f,108.5f,0.0f};Put<float>(recoveryBone,kBoneWorld506+0x34,100.0f);
    Check(std::fabs(FloorClear(j,recoveryVehicle,bodyPos,8.5f))<0.001f,"a large wing touching its bottom is at zero clearance");
    Check(FloorClear(j,recoveryVehicle,bodyPos,kNoGround)==kNoGround,"missing ground remains missing");

    Put<void*>(recoveryVehicle,kSeats,recoverySeat);Put<std::uint64_t>(recoveryVehicle,kSeatCount,1);
    Put<void*>(recoverySeat,kSeatRider,recoveryPilot);Put<void*>(recoverySeat,kSeatRiderCtrl,recoveryCtrl);
    rescueChoiceScenario=true;j=PJet{};jet::jets[0].ref=ObjRef::Of(recoveryVehicle);
    Put<int>(recoveryVehicle,kTeam,jet::kTeamFriend);
    const float origin[3]={0,0,0};
    Check(CatchChoice(origin)==&jet::jets[0] && HailChoice(origin,nullptr,nullptr)==&jet::jets[0],
          "existing real-piloted aircraft is eligible for catch and hail");
    // The catch's capture is the boarding gun's way in: with its hook not in no jet is sent (it used to fly in and wait
    // out kCatchMostMs with nothing to take the player aboard); with it, the same parachute gets one.
    {
        const float zero[3]={0,0,0};
        std::memcpy(recoveryHuman+kPosition,zero,12);
        bail=Bailout{};bail.state=Eject::chute;bail.catchWith=pjet::kCatchPlayerFighter;bail.at=recoveryTime-kCatchAfterMs-1;
        boardReady=false;Catch(recoveryHuman,recoveryTime);boardReady=true;
        Check(!bail.caught && !catchFlight.v && bail.catchWith==pjet::kCatchNone,"no boarding hook: no catch jet sent");
        bail=Bailout{};bail.state=Eject::chute;bail.catchWith=pjet::kCatchPlayerFighter;bail.at=recoveryTime-kCatchAfterMs-1;
        Catch(recoveryHuman,recoveryTime);
        Check(bail.caught && catchFlight.v==recoveryVehicle,"with the boarding hook: the catch jet is sent");
        bail=Bailout{};catchFlight=CatchFlight{};
    }
    recoveryAuthority=false;
    Check(!CatchChoice(origin) && !HailChoice(origin,nullptr,nullptr),"another machine's real pilot is not locally commandeered");
    recoveryAuthority=true;Put<void*>(recoverySeat,kSeatRiderCtrl,nullptr);
    Check(!CatchChoice(origin) && !HailChoice(origin,nullptr,nullptr),"uncrewed aircraft never offered for catch or hail");
    ResetJet(j);j.board=&pjet::kBoardable[0];npcResumes=crewRequests=0;
    HandBack(j,recoveryVehicle,"test empty handback");
    Check(npcResumes==0 && crewRequests==1,"handback requests real crew but does not resume empty aircraft");
    gunner.pilotTried=false;EnsurePilot(recoveryVehicle);
    Check(npcResumes==0,"gunner waiting for a real pilot does not resume flight");
    Put<void*>(recoverySeat,kSeatRiderCtrl,recoveryCtrl);EnsurePilot(recoveryVehicle);
    Check(npcResumes==1,"a real pilot who actually boarded completes delayed handback");
    jet::jets[0]=jet::Jet{};rescueChoiceScenario=false;
    // Descending parachute and an offset native door (test hub #42: no velocity out of nowhere): the catch jet is a wing
    // all the way in. Repeated production Catch -> AutoFly physics steps fly a pass at the door's point, never slower
    // than the wing flies and never a velocity change past what its lift and engine give, and the player is captured as
    // the door goes by. (Until 2026-11-21 its last 250 m were a formation that wrote the velocity whole: the parachute's
    // 6 m/s down plus a closing term, the jet sliding under the player with its nose held still.)
    ResetJet(j);j.autopilot=true;j.driven=false;j.vel[0]=90.0f;j.vel[1]=0.0f;j.vel[2]=0.0f;j.stall=false;
    float* body=reinterpret_cast<float*>(recoveryVehicle+kPosition);body[0]=-900;body[1]=300;body[2]=40;
    float* human=reinterpret_cast<float*>(recoveryHuman+kPosition);human[0]=0;human[1]=300;human[2]=0;
    float* humanVelocity=reinterpret_cast<float*>(recoveryHuman+kHumanVel);humanVelocity[1]=-6.0f;
    bail=Bailout{};bail.state=Eject::chute;bail.caught=ObjRef::Of(recoveryVehicle);bail.caughtAt=recoveryTime;
    catchFlight=CatchFlight{recoveryVehicle,{0,300,0}};
    boardRequests=0;
    float slowest=1e9f,hardest=0.0f,capturedGap=-1.0f;
    for(int frame=0;frame<60*30 && !boardRequests;++frame) {
        Put<float>(recoveryBone,kBoneWorld506+0x34,body[1]-1.38f);
        Catch(recoveryHuman,recoveryTime);
        if(boardRequests){float at[3],r=0.0f;SeatPoint(recoveryVehicle,0,at,&r);capturedGap=vec::Dist(at,human);break;}
        const float was[3]={j.vel[0],j.vel[1],j.vel[2]};
        AutoFly(j,recoveryVehicle,body,1.0f/60.0f,recoveryTime);
        slowest=std::fmin(slowest,vec::Len(j.vel));
        const float dv[3]={j.vel[0]-was[0],j.vel[1]-was[1],j.vel[2]-was[2]};
        hardest=std::fmax(hardest,vec::Len(dv)*60.0f);
        for(int i=0;i<3;++i){body[i]+=j.vel[i]/60.0f;human[i]+=humanVelocity[i]/60.0f;}
        recoveryTime+=16;
    }
    Check(boardRequests>0 && capturedGap>=0.0f && capturedGap<pjet::kCaptureRange,
          "the catch jet's pass takes the parachuting player aboard within its capture");
    Check(slowest>=kStallFloor,"the catch jet never flies slower than a wing flies (no parachute-speed formation)");
    Check(hardest<=(j.kind->maxG+2.0f)*kG,"no velocity out of nowhere: each frame's change within its lift and engine");
    Check(j.phase==Phase::air && j.autopilot,"it flies on through the capture as it flew");
    // The catches that are hard for a wing (review 2026-11-21): the production Catch -> AutoFly steps, the parachute as
    // EjectTick holds it (6 m/s down, its carried speed bled at kChuteBleed), until the capture, the parachute down, or
    // kCatchMostMs. Each must capture within kCatchMostMs, never under the stall floor, never a velocity change past its
    // lift and engine, never under kCatchFloor over the ground (less a frame's sink), and not go round more than
    // `laps` times.
    struct CatchRun { bool captured=false,landed=false,gaveUp=false; float seconds=0.0f,gap=-1.0f,slowest=1e9f,hardest=0.0f;
                      float lowest=1e9f,turned=0.0f; };
    const auto runCatch=[&](const float* bodyAt,const float* bodyVel,const float* humanAt,const float* humanVel0,
                            const char* name) {
        CatchRun r;
        ResetJet(j);j.autopilot=true;j.driven=false;j.stall=false;
        for(int i=0;i<3;++i){body[i]=bodyAt[i];j.vel[i]=bodyVel[i];human[i]=humanAt[i];humanVelocity[i]=humanVel0[i];}
        bail=Bailout{};bail.state=Eject::chute;bail.caught=ObjRef::Of(recoveryVehicle);bail.caughtAt=recoveryTime;
        catchFlight=CatchFlight{recoveryVehicle,{human[0],human[1]-kCatchBelow,human[2]}};
        boardRequests=0;
        float heading=std::atan2(j.vel[2],j.vel[0]);
        const ULONGLONG start=recoveryTime;
        for(int frame=0;frame<60*50;++frame) {
            Put<float>(recoveryBone,kBoneWorld506+0x34,body[1]-1.38f);
            Catch(recoveryHuman,recoveryTime);
            r.seconds=static_cast<float>(recoveryTime-start)*0.001f;
            if(boardRequests){float at[3],reach=0.0f;SeatPoint(recoveryVehicle,0,at,&reach);r.gap=vec::Dist(at,human);r.captured=true;break;}
            if(!bail.caught){r.gaveUp=true;break;}
            const float was[3]={j.vel[0],j.vel[1],j.vel[2]};
            AutoFly(j,recoveryVehicle,body,1.0f/60.0f,recoveryTime);
            r.slowest=std::fmin(r.slowest,vec::Len(j.vel));
            const float dv[3]={j.vel[0]-was[0],j.vel[1]-was[1],j.vel[2]-was[2]};
            r.hardest=std::fmax(r.hardest,vec::Len(dv)*60.0f);
            const float now=std::atan2(j.vel[2],j.vel[0]);
            float turn=now-heading;
            while(turn>kWatchPi)turn-=2.0f*kWatchPi;
            while(turn< -kWatchPi)turn+=2.0f*kWatchPi;
            r.turned+=std::fabs(turn);heading=now;
            for(int i=0;i<3;++i)body[i]+=j.vel[i]/60.0f;
            if(testGround>-1.0e8f)r.lowest=std::fmin(r.lowest,body[1]-1.38f-testGround);
            humanVelocity[0]*=1.0f-kChuteBleed/60.0f;humanVelocity[2]*=1.0f-kChuteBleed/60.0f;
            for(int i=0;i<3;++i)human[i]+=humanVelocity[i]/60.0f;
            recoveryTime+=16;
            if(testGround>-1.0e8f && human[1]-testGround<kChuteLand){r.landed=true;break;}   // EjectTick: landed
        }
        std::printf("catch %-38s captured %d (gap %.1f m) in %.1f s, landed %d, gave up %d, slowest %.0f, hardest %.0f m/s^2, "
                    "lowest %.0f m, turned %.0f deg\n",name,r.captured,r.gap,r.seconds,r.landed,r.gaveUp,r.slowest,r.hardest,
                    r.lowest,r.turned*57.2958f);
        return r;
    };
    const auto wingOk=[&](const CatchRun& r) {
        return r.slowest>=kStallFloor && r.hardest<=(j.kind->maxG+2.0f)*kG;
    };
    {   // the jet the player left: flying on away from them at 150 m/s, 500 m past them, they drift after it
        const float at[3]={600,400,0},v[3]={150,0,0},h[3]={100,420,0},hv[3]={40,-6,0};
        const CatchRun r=runCatch(at,v,h,hv,"self, 500 m past and flying away");
        Check(r.captured && r.seconds*1000.0f<kCatchMostMs && r.gap<pjet::kCaptureRange,"the jet left turns round and captures its pilot");
        Check(wingOk(r),"turning round for its pilot it flies as a wing");
        Check(r.turned<2.0f*kWatchPi*1.5f,"its pilot captured within a lap and a half");
    }
    {   // passing abeam, 250 m off, its turn wider than that: it cannot turn in on the first pass
        const float at[3]={-300,300,-250},v[3]={150,0,0},h[3]={0,300,0},hv[3]={0,-6,0};
        const CatchRun r=runCatch(at,v,h,hv,"abeam 250 m, turn wider than that");
        Check(r.captured && r.seconds*1000.0f<kCatchMostMs && r.gap<pjet::kCaptureRange,"abeam and too close to turn in: it comes round and captures");
        Check(wingOk(r),"coming round it flies as a wing");
        Check(r.turned<2.0f*kWatchPi*1.5f,"abeam: captured within a lap and a half");
    }
    {   // straight overhead the wrong way, 150 m up and 100 m past
        const float at[3]={100,450,0},v[3]={160,0,0},h[3]={0,300,0},hv[3]={-20,-6,0};
        const CatchRun r=runCatch(at,v,h,hv,"overhead, past and flying away");
        Check(r.captured && r.seconds*1000.0f<kCatchMostMs,"overhead and past: it comes round and captures");
        Check(wingOk(r),"overhead: it flies as a wing");
    }
    testGround=0.0f;
    {   // a low parachute (just over kCatchClear at the send) and the jet near: caught before it is down, never under
        // kCatchFloor
        const float at[3]={-500,60,0},v[3]={120,0,0},h[3]={0,kCatchClear+20.0f,0},hv[3]={0,-6,0};
        const CatchRun r=runCatch(at,v,h,hv,"low parachute, jet 500 m out");
        Check(wingOk(r) && r.lowest>=kCatchFloor-2.0f,"low: the jet keeps over kCatchFloor");
        Check(r.captured,"a low parachute with the jet near: captured before it is down");
    }
    {   // at kCatchFloor already (the jet comes no lower): the jet passes at its floor, the door within
        // kCaptureRange of them (down to ~10 m over the ground at best)
        const float at[3]={-250,40,0},v[3]={120,0,0},h[3]={0,kCatchFloor,0},hv[3]={0,-6,0};
        const CatchRun r=runCatch(at,v,h,hv,"parachute at kCatchFloor, jet 250 m out");
        Check(wingOk(r) && r.lowest>=kCatchFloor-2.0f,"under the floor: the jet keeps over kCatchFloor");
        Check(r.captured && r.gap<pjet::kCaptureRange,"under the floor: captured from the jet's floor");
    }
    {   // diving on a low parachute from high: its dive eased onto kCatchFloor, not levelled only once under it
        const float at[3]={-400,200,0},v[3]={110,-50,0},h[3]={0,kCatchFloor+2.0f,0},hv[3]={0,-6,0};
        const CatchRun r=runCatch(at,v,h,hv,"diving from 200 m on a low parachute");
        Check(wingOk(r) && r.lowest>=kCatchFloor,"diving on a low parachute: the jet eases onto kCatchFloor, never under it");
    }
    {   // a low parachute and the jet far: it cannot be there before the parachute is down. The catch ends with the
        // parachute, the jet never under kCatchFloor chasing it.
        const float at[3]={-1500,200,0},v[3]={120,0,0},h[3]={0,kCatchClear+5.0f,0},hv[3]={0,-6,0};
        const CatchRun r=runCatch(at,v,h,hv,"low parachute, jet 1.5 km out");
        Check(wingOk(r) && r.lowest>=kCatchFloor-2.0f,"low and far: the jet keeps over kCatchFloor");
        Check(!r.captured && (r.landed || r.gaveUp),"low and far: no capture, the catch over by the parachute's landing");
    }
    testGround=-1.0e9f;
    // A hailed rotor craft's nose (HailFace, test hub #42): the way it flies while faster than kHailFaceSpeed, so a spot
    // round the player's side does not take it past them sideways at speed; slow, on the player.
    {
        const float at[3]={100,30,0},playerAt[3]={0,0,0},past[3]={0,0,-25},slow[3]={0,0,-2};
        float face[3];
        HailFace(at,past,playerAt,face);
        Check(std::fabs(face[0]-at[0])<1e-4f && face[2]<at[2],"a rotor craft passing the player at 25 m/s faces the way it flies");
        HailFace(at,slow,playerAt,face);
        Check(vec::Dist(face,playerAt)<1e-4f,"a rotor craft slower than kHailFaceSpeed faces the player");
    }
    // A capture refused (boarding.cpp: not a board-able friend, the player in a vehicle, no seats; or no hook) ends the
    // catch at once, logged, the parachute going on: it used to press on silently until kCatchMostMs.
    {
        const float at[3]={-900,300,40},v[3]={90,0,0},h[3]={0,300,0},hv[3]={0,-6,0};
        boardRefuses=true;
        const CatchRun r=runCatch(at,v,h,hv,"capture refused");
        boardRefuses=false;
        Check(r.captured && !bail.caught && !catchFlight.v && bail.state==Eject::chute,
              "a refused capture ends the catch, the parachute goes on");
    }
    // Its first frame on the autopilot goes on from the velocity its NPC flight had (jet.cpp's entry), not its nose times
    // a catch speed: an aircraft flying +x with its body's rows still +z was turned 90 degrees and sped up in one frame.
    {
        ResetJet(j);j.autopilot=false;j.vel[0]=j.vel[1]=j.vel[2]=0.0f;
        body[0]=-900;body[1]=300;body[2]=0;
        jet::Jet flying{};flying.m.vel[0]=120.0f;flying.m.vel[1]=-2.0f;flying.m.vel[2]=0.0f;
        jet::seedScenario=true;jet::seedJet=&flying;
        catchFlight=CatchFlight{recoveryVehicle,{0,300,0}};
        AutoFly(j,recoveryVehicle,body,1.0f/60.0f,recoveryTime);
        jet::seedScenario=false;jet::seedJet=nullptr;
        Check(j.autopilot && vec::Dist(j.vel,flying.m.vel)<(j.kind->maxG+2.0f)*kG/60.0f,
              "the autopilot's first frame flies on from the NPC flight's velocity");
    }
    // The player takes the catch jet over at the velocity it flies (Board), not a heading times a speed.
    {
        ResetJet(j);j.autopilot=true;j.driven=false;j.fresh=true;
        const float flown[3]={70.0f,-1.0f,40.0f};
        std::memcpy(j.sent,flown,12);std::memcpy(j.measured,flown,12);
        jet::seedScenario=true;jet::seedJet=nullptr;
        const float at[3]={0.0f,400.0f,0.0f};
        Board(j,recoveryVehicle,at,400.0f);
        jet::seedScenario=false;
        Check(vec::Dist(j.vel,flown)<0.001f && j.phase==Phase::air && !j.autopilot,"taken over at the velocity it flew");
    }
    // A target on its very position (nothing to aim at): still a frame of flight, never the stale velocity left as it was.
    ResetJet(j);j.autopilot=true;j.driven=false;j.vel[0]=j.vel[1]=0.0f;j.vel[2]=150.0f;
    for(int i=0;i<3;++i)catchFlight.target[i]=body[i];
    {
        const float stale[3]={j.vel[0],j.vel[1],j.vel[2]};
        AutoFly(j,recoveryVehicle,body,1.0f/60.0f,recoveryTime);
        Check(j.active && vec::Dist(j.vel,stale)>0.001f,"zero catch error still runs the wing's flight");
    }
    const float pilotVelocity[3]={j.vel[0],j.vel[1],j.vel[2]};
    Put<void*>(recoverySeat,kSeatRiderCtrl,nullptr);
    AutoFly(j,recoveryVehicle,body,1.0f/60.0f,recoveryTime);
    Check(!j.autopilot && !j.active && vec::Dist(j.vel,pilotVelocity)==0,
          "empty pilot seat cannot continue catch guidance or invent acceleration");
    Catch(recoveryHuman,recoveryTime);
    Check(!bail.caught && !catchFlight.v && bail.state==Eject::chute,
          "pilot loss cancels only the catch and keeps the parachute active");
    bail.catchWith=pjet::kCatchPlayerFighter;bail.at=recoveryTime-kCatchAfterMs-1;
    Catch(recoveryHuman,recoveryTime);
    Check(!bail.caught && bail.state==Eject::chute,
          "no crewed rescue aircraft means no aircraft is spawned in the air");
    Put<void*>(recoverySeat,kSeatRiderCtrl,recoveryCtrl);
    bail.caught=ObjRef::Of(recoveryVehicle);bail.caughtAt=recoveryTime;
    catchFlight=CatchFlight{recoveryVehicle,{0,300,0}};
    for(int i=0;i<3;++i)body[i]=human[i];   // the body on the player: only an unreadable door keeps it from boarding
    testDoor=false;const int presses=boardPresses,requests=boardRequests;Catch(recoveryHuman,recoveryTime);
    Check(!catchFlight.hasDoor && boardPresses==presses && boardRequests==requests,
          "an unreadable door never falls back to boarding by body centre");
    Check(vec::Dist(catchFlight.target,human)>kCatchBelow-0.001f && vec::Dist(catchFlight.target,human)<kCatchBelow+0.001f &&
          catchFlight.target[1]<human[1],"with no door the jet is still sent for the player, under them");
    // No door known (the first frames of a jet just made: hasDoor starts false) is a flight like any other: Air steers,
    // and the velocity is never last frame's left as it was (2026-10-07 review: up to 45 s with no terrain or stall step).
    ResetJet(j);j.autopilot=true;j.driven=false;j.vel[0]=j.vel[1]=0.0f;j.vel[2]=150.0f;
    body[0]=0;body[1]=300;body[2]=0;
    catchFlight=CatchFlight{recoveryVehicle,{-1500,300,0}};
    const float stale[3]={j.vel[0],j.vel[1],j.vel[2]};
    AutoFly(j,recoveryVehicle,body,1.0f/60.0f,recoveryTime);
    Check(!catchFlight.hasDoor && vec::Dist(j.vel,stale)>0.001f,"no door: the first frame already runs the flight");
    for(int frame=0;frame<600;++frame) {
        AutoFly(j,recoveryVehicle,body,1.0f/60.0f,recoveryTime);
        for(int i=0;i<3;++i)body[i]+=j.vel[i]/60.0f;
        recoveryTime+=16;
    }
    Check(j.vel[0]<0.0f && j.phase==Phase::air && vec::Len(j.vel)>j.kind->rotate,"no door: it turns for the target at flying speed");
    testDoor=true;

    const float at[3]={0,0,0},offsetDoor[3]={20,-2,0},p[3]={20,-2,0},velocity[3]={0,-6,0},spin[3]={0,0.5f,0};
    float target[3];pjet::CatchDoor(p,velocity,at,offsetDoor,velocity,spin,1.0f/60.0f,target);
    // The door's own rotation (0.5 rad/s round y, 20 m out: 10 m/s along z) is relative motion led one step: 1/6 m.
    Check(std::fabs(target[0])<0.001f && std::fabs(target[1])<0.001f && std::fabs(target[2]-10.0f/60.0f)<0.001f,
          "the body's target leads the door's rotation by one physics step only");
    // Real EnemyRam and Blocked, with only enemy enumeration, native charge creation and native death replaced.
    ramScenario=true;recoveryConfig.playerJetRamDamage=1.0f;
    const float ramPos[3]={0,0,0};
    auto resetRam=[&]() {
        ResetJet(j);j.active=true;j.measured[2]=200.0f;
        ramBlasts=ramDeaths=ramVisits=0;ramEnemyCount=1;ramDeathWorks=true;recoveryVehicle[kDead]=0;
        Put<float>(recoveryVehicle,kHpMax,1000.0f);Put<float>(recoveryVehicle,kHp,1000.0f);
        for(int i=0;i<20;++i) {
            Put<const void*>(ramEnemies[i],kSelfCtrl,ramEnemies[i]);
            Put<float>(ramEnemies[i],kPosition,0.0f);
            Put<float>(ramEnemies[i],kPosition+4,j.kind->ram*kRamBoxHeight);
            Put<float>(ramEnemies[i],kPosition+8,5.0f);
        }
    };
    resetRam();EnemyRam(j,recoveryVehicle,ramPos,10000);
    Check(ramBlasts==1 && std::fabs(ramDamage-ram::Damage(16000.0f,200.0f,1.0f,1.0f))<0.01f,"contact uses production kinetic-energy ram damage");
    Check(std::fabs(At<float>(recoveryVehicle,kHp)-900.0f)<0.01f,"enemy ram takes speed-scaled self damage once");
    EnemyRam(j,recoveryVehicle,ramPos,10500);
    Check(ramBlasts==1 && At<float>(recoveryVehicle,kHp)==900.0f,"same enemy remains throttled for a full second");
    EnemyRam(j,recoveryVehicle,ramPos,11000);
    Check(ramBlasts==2 && At<float>(recoveryVehicle,kHp)==800.0f,"enemy becomes eligible at cooldown boundary");
    resetRam();Put<float>(ramEnemies[0],kPosition+8,30.0f);EnemyRam(j,recoveryVehicle,ramPos,10000);
    Check(ramBlasts==0 && At<float>(recoveryVehicle,kHp)==1000.0f,"nearby non-contact enemy cannot trigger a blast");
    Put<float>(ramEnemies[0],kPosition+8,-5.0f);EnemyRam(j,recoveryVehicle,ramPos,10000);
    Check(ramBlasts==0,"receding enemy cannot trigger a blast");
    resetRam();j.measured[2]=19.0f;EnemyRam(j,recoveryVehicle,ramPos,10000);
    Check(ramVisits==0 && ramBlasts==0,"slow movement does not enumerate enemies or ram");
    resetRam();ramEnemyCount=20;EnemyRam(j,recoveryVehicle,ramPos,10000);
    Check(ramBlasts==8 && At<float>(recoveryVehicle,kHp)==900.0f,"swarm is batched and takes only one self hit");
    EnemyRam(j,recoveryVehicle,ramPos,10016);
    Check(ramBlasts==16 && At<float>(recoveryVehicle,kHp)==900.0f,"cooling first batch cannot starve later enemies or multiply self damage");
    EnemyRam(j,recoveryVehicle,ramPos,10032);
    Check(ramBlasts==16,"full cooldown table never evicts live hits");
    resetRam();EnemyRam(j,recoveryVehicle,ramPos,10000);
    Put<const void*>(ramEnemies[0],kSelfCtrl,recoveryCtrl);EnemyRam(j,recoveryVehicle,ramPos,10016);
    Check(ramBlasts==2,"reused address with a new object identity is a fresh contact");
    resetRam();j.measured[2]=1000.0f;EnemyRam(j,recoveryVehicle,ramPos,10000);
    Check(At<float>(recoveryVehicle,kHp)==750.0f,"extreme closing speed caps self damage at a quarter of max HP");
    resetRam();Put<float>(recoveryVehicle,kHp,50.0f);EnemyRam(j,recoveryVehicle,ramPos,10000);
    Check(ramDeaths==1 && recoveryVehicle[kDead] && !j.active,"lethal contact follows native Kill path");
    resetRam();ramDeathWorks=false;Put<float>(recoveryVehicle,kHp,50.0f);EnemyRam(j,recoveryVehicle,ramPos,10000);
    Check(ramDeaths==1 && !recoveryVehicle[kDead] && At<float>(recoveryVehicle,kHp)==1.0f,"unavailable native death preserves existing one-HP fallback");
    resetRam();EnemyRam(j,recoveryVehicle,ramPos,10000);j.sent[2]=200.0f;j.measured[2]=0.0f;j.blockedSince=10000;
    Blocked(j,recoveryVehicle,ramPos,10200);
    Check(ramBlasts==1,"blocked recovery does not duplicate a recent enemy blast");
    ramScenario=false;recoveryVehicle[kDead]=0;Put<float>(recoveryVehicle,kHp,1000.0f);
    // The takeoff: its own wheels' contact in the step that lifts it off is not a landing. Before, the next frame's
    // ReconcileGround took it for one: the boost let go, the lever closed and it braked on the runway after a hop; the
    // boost held, it lifted off again every frame from the ground step (its pitch rate and angle of attack zeroed each
    // time) while the solver's ground contact and the flight's whole velocity took turns.
    char takeoffLine[160]{};int takeoffNote=0;
    for(const float boost:{0.0f,1.0f}) {
        const TakeoffRun t=Takeoff(j,boost);
        Check(t.takeoffs==1 && t.relandings==0,"takeoff: one lift-off, never handed back to the ground by its own wheels");
        Check(t.climbFlips==0 && t.lowestClimbAfter>0.0f,"takeoff: the climb never reverses after lift-off");
        Check(t.airAtEnd && t.clearAt1s>kTouch,"takeoff: past the touch height within a second and a half, still flying");
        takeoffNote+=std::snprintf(takeoffLine+takeoffNote,sizeof(takeoffLine)-takeoffNote," [boost %.0f: %d lift-off, %.1f m up]",
                                   boost,t.takeoffs,t.clearAt1s);
    }
    // A real touchdown inside the window after a lift-off (sinking back onto the ground) still lands.
    ResetJet(j);j.vel[1]=-2.0f;j.liftOffAt=recoveryTime;j.liftedOff=true;recoveryVehicle[0x1580]=2;
    ReconcileGround(j,recoveryVehicle,0.5f,false,false,recoveryTime+100);
    Check(j.phase==Phase::rolling,"takeoff: sinking back onto its contacts within the lift-off window still lands");
    // Taken over in the air (the user, 2026-10-12: "上npc飞机的时候，速度会清零"): an NPC's aircraft has no record until
    // the player sits at its stick, so Board runs on a record that has measured nothing (fresh false). Its velocity is the
    // NPC flight's (jet.cpp's entry), not zero; with no entry, or one not finite, the record keeps what it had.
    jet::seedScenario=true;
    jet::Jet flying{};flying.m.vel[0]=0.0f;flying.m.vel[1]=-2.0f;flying.m.vel[2]=120.0f;
    const float seedPos[3]={0.0f,400.0f,0.0f};
    j=PJet{};j.vehicle=recoveryVehicle;j.ref=ObjRef::Of(recoveryVehicle);j.kind=&kKinds[0];
    jet::seedJet=&flying;
    Board(j,recoveryVehicle,seedPos,400.0f);
    Check(j.phase==Phase::air && j.vel[2]==120.0f && j.vel[1]==-2.0f && j.measured[2]==120.0f,
          "taken over in the air: it flies on at the NPC flight's velocity");
    j=PJet{};j.vehicle=recoveryVehicle;j.ref=ObjRef::Of(recoveryVehicle);j.kind=&kKinds[0];
    j.fresh=true;j.measured[2]=80.0f;
    Board(j,recoveryVehicle,seedPos,400.0f);
    Check(j.vel[2]==80.0f,"a record with a frame of its own keeps its measured velocity");
    j=PJet{};j.vehicle=recoveryVehicle;j.ref=ObjRef::Of(recoveryVehicle);j.kind=&kKinds[0];
    jet::seedJet=nullptr;
    Board(j,recoveryVehicle,seedPos,400.0f);
    Check(j.vel[0]==0.0f && j.vel[1]==0.0f && j.vel[2]==0.0f,"no flight entry: nothing made up");
    flying.m.vel[2]=std::nanf("");jet::seedJet=&flying;
    j=PJet{};j.vehicle=recoveryVehicle;j.ref=ObjRef::Of(recoveryVehicle);j.kind=&kKinds[0];
    Board(j,recoveryVehicle,seedPos,400.0f);
    Check(std::isfinite(j.vel[2]) && j.vel[2]==0.0f,"a flight velocity not finite is not taken");
    jet::seedScenario=false;jet::seedJet=nullptr;
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("playerjet_recovery: %d production-path checks passed; takeoff%s\n",checks,takeoffLine);
    return 0;
}
