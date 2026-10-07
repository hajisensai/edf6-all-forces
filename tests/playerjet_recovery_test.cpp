// The production player-jet catch, touchdown and ground controls against stand-in game state. No game is loaded.
#include "../src/playerjet.cpp"
#include <cstdio>
#include <cstdlib>

namespace crew {
bool ReadRound(const unsigned char*,RoundModel*) noexcept {return false;}
bool RoundLands(const unsigned char*,const RoundModel&,const float*,const float*,float,float*,float*) noexcept {return false;}
float SightZoomNow(const void*) noexcept {return 1.0f;}
void SightZoomFrame(unsigned char*,unsigned,bool) noexcept {}
unsigned char* image=nullptr;
// Offline (online_authority.h): an NPC rider may be seated, through the vehicle's own RideAi.
bool SeatNpcRider(unsigned char* v,bool spawned) noexcept {
    reinterpret_cast<void(__fastcall* const*)(void*,bool)>(At<void* const*>(v,0))[kSlotRideAi](v,spawned);
    return true;
}
Config recoveryConfig{};
PlayerFix player{};
ULONGLONG recoveryTime=10000;
bool testGearDown=true,testDoor=true;
float doorOffset[3]={8.65f,-1.38f,1.8f};
int boardPresses=0;
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
bool MapHoldsKeys() noexcept { return false; }
bool GearDown(const void*) noexcept { return testGearDown; }
float GearDragShare(const void*) noexcept { return 0.0f; }
GearState GearStep(unsigned char*,bool,float,bool) noexcept { return GearState{}; }
void JetFlames(const unsigned char*,float,bool,ULONGLONG) noexcept {}
void BodyAttitude(const unsigned char*,const float*,const float*,float,float,float* omega) noexcept { omega[0]=omega[1]=omega[2]=0.0f; }
float GroundClearance(const float*) noexcept { return 500.0f; }
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
// Unrelated stores, weapons, spawning and NPC paths must not be reached by these recovery scenarios.
void MissingRecoveryDependency() noexcept { void(*volatile stop)()=std::abort;stop(); }
int ReadStores(unsigned char*,Store*,int) noexcept { MissingRecoveryDependency();return 0; }
void TriggerStore(const Store&) noexcept { MissingRecoveryDependency(); }
Burden BurdenOf(float,const Store*,int) noexcept { MissingRecoveryDependency();return Burden{1.0f,0.0f}; }
const JetMass* JetMassOf(float) noexcept { if(!ramScenario)MissingRecoveryDependency();return &ramMass; }
bool IsStoreWeapon(const unsigned char*) noexcept { MissingRecoveryDependency();return false; }
void LevelVehicle(unsigned char*) noexcept { MissingRecoveryDependency(); }
void LogImpact(const char*,const void*,const float*,const float*) noexcept { if(!ramScenario)MissingRecoveryDependency(); }
bool LastViewProj(float*) noexcept { MissingRecoveryDependency();return false; }
bool CameraRay(float*,float*) noexcept { MissingRecoveryDependency();return false; }
void SetObjectTeam(unsigned char*,std::int32_t) noexcept { MissingRecoveryDependency(); }
float ShieldBlock(const unsigned char*,float*) noexcept { MissingRecoveryDependency();return 0.0f; }
bool JetMotionProps(void*) noexcept { MissingRecoveryDependency();return false; }
JetBody BomberBody(const unsigned char*) noexcept { MissingRecoveryDependency();return JetBody{}; }
PluginBody BodyOf(const void*) noexcept { MissingRecoveryDependency();return PluginBody{}; }
float BodyMark(const void*) noexcept { if(!ramScenario)MissingRecoveryDependency();return ramMass.mark; }
bool Body506Ok() noexcept { MissingRecoveryDependency();return false; }
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
bool MissileHoming(const float*,float) noexcept { MissingRecoveryDependency();return false; }
int MissilesHomingAt(const float*,float,float (*)[3],int) noexcept { MissingRecoveryDependency();return 0; }
void FlareDrop(const void*,const float*,const float*,const float*,bool) noexcept { MissingRecoveryDependency(); }
void FlaresStep() noexcept { MissingRecoveryDependency(); }
int FlaresOf(const void*,float (*)[3],float (*)[3],int) noexcept { MissingRecoveryDependency();return 0; }
void FlareFlames(const unsigned char*,const float (*)[3],const float (*)[3],int,ULONGLONG) noexcept { MissingRecoveryDependency(); }
unsigned char* PlayerHuman() noexcept { MissingRecoveryDependency();return nullptr; }
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
int WeaponLock(const unsigned char*,float*,float*) noexcept { MissingRecoveryDependency();return 0; }
void ClearWeaponLock(unsigned char*) noexcept { MissingRecoveryDependency(); }
void NextLockTarget(unsigned char*) noexcept { MissingRecoveryDependency(); }
GearState PlayerGear(unsigned char*,bool,bool,bool,float,float,float,float) noexcept { MissingRecoveryDependency();return GearState{}; }
float ClosureIn(const float*,const float*,float,float,float,float,bool*) noexcept { MissingRecoveryDependency();return -1.0f; }
Gpws GpwsOf(float,bool) noexcept { MissingRecoveryDependency();return Gpws::none; }
namespace audio { void LockTone(int,float) noexcept { MissingRecoveryDependency(); } }
namespace jet {
Jet jets[kMaxJets]{};
bool SpawnReady() noexcept { MissingRecoveryDependency();return false; }
bool ModFileThere(const wchar_t*) noexcept { MissingRecoveryDependency();return false; }
bool LockingOn(const void*) noexcept { MissingRecoveryDependency();return false; }
int BreakLocks(const void*,float) noexcept { MissingRecoveryDependency();return 0; }
int LockersOf(const void*,float (*)[3],int) noexcept { MissingRecoveryDependency();return 0; }
bool Alive(const ObjRef&) noexcept { MissingRecoveryDependency();return false; }
Jet* FindJet(const unsigned char*) noexcept { MissingRecoveryDependency();return nullptr; }
void HoldOffGround(Jet&,const float*,float,float,ULONGLONG,float) noexcept { MissingRecoveryDependency(); }
void Hover(Jet&,const Kind&,const unsigned char*,const float*,const float*,const float*,float,float,float,float,bool) noexcept { MissingRecoveryDependency(); }
void Thrusters(Jet&,const Kind&,unsigned char*,float,ULONGLONG) noexcept { MissingRecoveryDependency(); }
void DollFrame(int,const unsigned char*,float) noexcept { MissingRecoveryDependency(); }
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
void ResumeNpc(unsigned char*,const float*) noexcept { MissingRecoveryDependency(); }
Jet* Adopt(unsigned char*) noexcept { MissingRecoveryDependency();return nullptr; }
}
}

namespace {
using namespace crew;
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
alignas(16) unsigned char recoveryVehicle[0x3000]{},recoveryCtrl[0x20]{},recoveryHuman[0x1600]{};
void ResetJet(PJet& j) {
    j=PJet{};j.vehicle=recoveryVehicle;j.ref=ObjRef::Of(recoveryVehicle);j.kind=&kKinds[0];j.phase=Phase::air;
    j.driven=true;j.fresh=true;j.vel[2]=55.0f;j.vel[1]=-3.0f;
    j.throttle=0.55f;j.stall=true;j.stallShare=1.2f;j.hasAim=j.hasUp=j.mouseFlies=true;
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
    float bodyPos[3]={0.0f,108.5f,0.0f};Put<float>(recoveryBone,kBoneWorld506+0x34,100.0f);
    Check(std::fabs(FloorClear(j,recoveryVehicle,bodyPos,8.5f))<0.001f,"a large wing touching its bottom is at zero clearance");
    Check(FloorClear(j,recoveryVehicle,bodyPos,kNoGround)==kNoGround,"missing ground remains missing");

    // Descending parachute and an offset native door: repeated production Catch -> AutoFly physics steps must put
    // the door in reach, not leave it six metres ahead. Begin with the body centre near the human but the door far.
    ResetJet(j);j.autopilot=true;j.driven=false;j.vel[0]=j.vel[2]=0.0f;j.vel[1]=-6.0f;
    float* body=reinterpret_cast<float*>(recoveryVehicle+kPosition);body[0]=0;body[1]=300;body[2]=0;
    float* human=reinterpret_cast<float*>(recoveryHuman+kPosition);human[0]=0;human[1]=300;human[2]=0;
    float* humanVelocity=reinterpret_cast<float*>(recoveryHuman+kHumanVel);humanVelocity[1]=-6.0f;
    bail=Bailout{};bail.state=Eject::chute;bail.caught=ObjRef::Of(recoveryVehicle);bail.caughtAt=recoveryTime;
    catchFlight=CatchFlight{recoveryVehicle,{0,300,0},150.0f,{0,-6,0},{0,0,1}};
    for(int frame=0;frame<600;++frame) {
        Put<float>(recoveryBone,kBoneWorld506+0x34,body[1]-1.38f);
        Catch(recoveryHuman,recoveryTime);
        AutoFly(j,recoveryVehicle,body,1.0f/60.0f,recoveryTime);
        for(int i=0;i<3;++i){body[i]+=j.vel[i]/60.0f;human[i]+=humanVelocity[i]/60.0f;}
        recoveryTime+=16;
    }
    float door[3],reach=0.0f;SeatPoint(recoveryVehicle,0,door,&reach);
    Check(vec::Dist(door,human)<0.1f && boardPresses>0,"descending native door converges inside actual boarding reach");
    Check(std::fabs(j.vel[1]+6.0f)<0.01f,"close catch follows parachute speed without Air's 25 m/s stall floor");
    for(int i=0;i<3;++i){catchFlight.target[i]=body[i];catchFlight.drift[i]=humanVelocity[i];j.vel[i]=30.0f;}
    AutoFly(j,recoveryVehicle,body,1.0f/60.0f,recoveryTime);
    Check(vec::Dist(j.vel,humanVelocity)<0.001f,"zero catch error still replaces stale velocity with parachute drift");
    testDoor=false;const int presses=boardPresses;Catch(recoveryHuman,recoveryTime);
    Check(!catchFlight.hasDoor && boardPresses==presses,"an unreadable door never falls back to boarding by body centre");
    Check(vec::Dist(catchFlight.target,human)>kCatchBelow-0.001f && vec::Dist(catchFlight.target,human)<kCatchBelow+0.001f &&
          catchFlight.target[1]<human[1],"with no door the jet is still sent for the player, under them");
    // No door known (the first frames of a jet just made: hasDoor starts false) is a flight like any other: Air steers,
    // and the velocity is never last frame's left as it was (2026-10-07 review: up to 45 s with no terrain or stall step).
    ResetJet(j);j.autopilot=true;j.driven=false;j.vel[0]=j.vel[1]=0.0f;j.vel[2]=150.0f;
    body[0]=0;body[1]=300;body[2]=0;
    catchFlight=CatchFlight{recoveryVehicle,{-1500,300,0},150.0f,{0,0,0},{0,0,1}};
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
    float target[3],drift[3];pjet::CatchDoor(p,velocity,at,offsetDoor,velocity,spin,1.0f/60.0f,target,drift);
    Check(std::fabs(drift[2]-10.0f)<0.001f,"door rotation is cancelled in formation translation feed-forward");
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
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("playerjet_recovery: %d production-path checks passed\n",checks);
    return 0;
}
