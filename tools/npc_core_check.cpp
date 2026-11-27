// Execute npcai.cpp itself against stand-in memory and recording native entry points. No game is loaded or started.
#include <Windows.h>
namespace markinput {
bool down=false;
SHORT Key(int) noexcept { return down ? static_cast<SHORT>(0x8000) : 0; }
HWND Window() noexcept { return nullptr; }
DWORD Process(HWND,LPDWORD pid) noexcept { *pid=GetCurrentProcessId();return 1; }
}
#define GetWindowThreadProcessId markinput::Process
#include "../src/npcai.cpp"
#undef GetWindowThreadProcessId
#include "../src/worker_pool.cpp"   // the parallel Decide's pool (the fixtures that include this file link it too)
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <random>

namespace crew {
bool InstallRealDriverNative(NpcSeatInputOwnedFn) noexcept { return true; }
bool InstallVehicleTargetSearch() noexcept { return true; }   // vehicle_target.cpp: a vtable hook, nothing to stand in
// npc_backpack.cpp stand-ins: one sub slot (packSlot) when packReady; its uses and reloads recorded, the use going on
// (packBusy) as the case sets.
bool packReady=false,packBusy=false,packCanUse=true;int packSlots=0,packUses=0,packUseSlot=-1,packReloads=0;
NpcPackSlot packSlot{};
bool InstallNpcBackpack() noexcept { return packReady; }
bool NpcBackpackReady() noexcept { return packReady; }
int NpcPackSlots(const unsigned char*,NpcPackSlot* out,int most) noexcept {
    if(!packReady || !packSlots || most<1)return 0;
    out[0]=packSlot;return 1;
}
bool NpcPackIndex(const unsigned char*,int index) noexcept { return packReady && packSlots && index==packSlot.index; }
bool NpcPackBusy(const unsigned char*) noexcept { return packBusy; }
bool NpcPackCanUse(unsigned char*,int) noexcept { return packCanUse; }
bool NpcPackUse(unsigned char*,int slot) noexcept { ++packUses;packUseSlot=slot;return true; }
bool NpcPackReload(unsigned char*) noexcept { ++packReloads;return true; }
bool RealDriverNativeReady() noexcept { return true; }
bool SaveIniFlag(const char*,const char*) noexcept { return true; }   // loadout_store.cpp: the ini, not written here
bool PrepareNpcVehicle(unsigned char*,bool) noexcept { return true; }
int announcedBoards=0;
bool AnnounceNpcBoarding(unsigned char*) noexcept { ++announcedBoards;return true; }
bool AnnounceNpcDismount(unsigned char*) noexcept { return true; }
#ifndef NPC_CORE_EXTERNAL_COMMAND_NETWORK
NpcCommandExecutor configuredExecutor=nullptr;
void ConfigureNpcCommandNetwork(NpcCommandExecutor execute) noexcept {configuredExecutor=execute;}
#endif
#ifndef NPC_CORE_EXTERNAL_COMMAND_IDENTITY
bool ReadNativeObjectId(const unsigned char* object,unsigned char* id) noexcept {
    if(!object || !id)return false;std::memset(id,0,32);
    const auto value=reinterpret_cast<std::uintptr_t>(object);std::memcpy(id,&value,sizeof(value));id[24]=5;return true;
}
#endif
unsigned char* image=nullptr;
PlayerFix player{};
const void* heldSupportActor=nullptr;
bool SupportSoldierHeld(const void* h) noexcept {return h && h==heldSupportActor;}
// transport.cpp stand-ins: npcai.cpp's hooks recorded; TransportOrder takes a point order when transportTakes is set.
bool transportTakes=false;int transportOrders=0,transportCancels=0,transportPairs=0;
const void* transportPaired=nullptr;const void* transportPairTop=nullptr;const void* transportSucceededTo=nullptr;
NpcCommandReason transportWithdraw=NpcCommandReason::noTransport;
bool transportWithdrawable=false;   // the paired vehicle is a support vehicle WITHDRAW sends off
bool TransportPair(const void* top,const void* v) noexcept {++transportPairs;transportPairTop=top;transportPaired=v;return v!=nullptr;}
const void* TransportOf(const void*) noexcept {return transportPaired;}
bool TransportOrder(const void*,const mapcmd::Command&) noexcept {++transportOrders;return transportTakes;}
void TransportCancel(const void*) noexcept {++transportCancels;}
NpcCommandReason TransportWithdraw(const void*) noexcept {return transportWithdraw;}
bool TransportWithdrawable(const void*) noexcept {return transportPaired && transportWithdrawable;}
void TransportSucceed(const void*,const void* lead) noexcept {transportSucceededTo=lead;}
void TransportJumperIntent(unsigned char*) noexcept {}
namespace {
Config config{};
ULONGLONG now=1000,frame=1;
bool sessionOn=false,host=true,door=true;
bool wall=false;
// The plans the stand-in wastedfire's asks made (StubPlanMade, below).
struct StubPlan { const void* self;const void* target;float speed,gravity;ULONGLONG at; };
std::vector<StubPlan> stubPlans;
float arcWallZ=0.0f; // a finite map wall for production Backpack's segment queries
bool flatFloor=false,completeRide=false;
bool mapHeld=true,rayOn=false;
const void* markEnemy=nullptr;
// VisitEnemiesOf: gatherFoes enemies at (10 k, 0, 10 k) first, then with gatherFaults a point it cannot read (the walk
// faulting part way through Gather).
int gatherFoes=0;
bool gatherFaults=false;
int pointOrders=0;
int weakDeletes=0;
void DeleteWeakRecord(void*) { ++weakDeletes; }
const void* gunnerEnemy=nullptr;
online::CopyOwner vehicleCopyOwner=online::kCopyHost;
float doorAt[3]{},doorReach=3.0f;
unsigned char human[0x2200]{},dead[0x2200]{},other[0x2200]{},vehicle[0x3000]{},seats[edf::kSeatStride*2]{};
unsigned char head[0x18]{},node[0x18]{},ctrl[0x10]{};
int follows=0,rides=0,failures=0;
unsigned char* playerObj=nullptr;   // PlayerHuman (nullptr but in the box sweep's cases)
// Record the native per-box calls; the native DLL audit separately checks their actual behavior.
alignas(16) unsigned char boxMgr[0xE60]{},boxHead[0x20]{},boxNode[2][0x20]{},boxUnit[2][0xE0]{};
alignas(16) float boxModel[2][4]{};
int collects=0,notifies=0,heals=0;
const void* collectBy=nullptr;const void* notifyBy=nullptr;
int notifyId=0,notifyKind=-1,applyKind=-1;float healAmount=0.0f;const void* healed=nullptr;
bool notifyBeforeApply=false,untakenAtApply=false;
void __fastcall NotifyRec(void* mgr,std::int32_t id,std::int32_t kind,void* by) {
    if(mgr==boxMgr){++notifies;notifyId=id;notifyKind=kind;notifyBy=by;}
}
void __fastcall ApplyRec(void* mgr,void* by,std::int32_t kind,float) {
    if(mgr==boxMgr){++collects;collectBy=by;applyKind=kind;notifyBeforeApply=notifies==collects;untakenAtApply=!boxUnit[0][kBoxTaken];}
}
void __fastcall HealRec(void* h,float amount) { ++heals;healed=h;healAmount=amount;Put<float>(h,kHumanHp,At<float>(h,kHumanHp)+amount); }
const float* __fastcall NodePosRec(const void* model) { return static_cast<const float*>(model); }
void Boxes(int kind0,int kind1) {
    Put<void*>(image,kDropManager,boxMgr);
    Put<void*>(boxMgr,kBoxList,boxHead);
    Put<void*>(boxHead,0,boxNode[0]);Put<void*>(boxNode[0],0,boxNode[1]);Put<void*>(boxNode[1],0,boxHead);
    const int kinds[2]={kind0,kind1};
    for(int i=0;i<2;++i) {
        std::memset(boxUnit[i],0,sizeof(boxUnit[i]));
        Put<void*>(boxNode[i],kBoxNodeUnit,boxUnit[i]);
        Put<const void*>(boxUnit[i],0,image+kBoxVtable);Put<void*>(boxUnit[i],kBoxModel,boxModel[i]);
        Put<std::int32_t>(boxUnit[i],kBoxKind,kinds[i]);Put<std::int32_t>(boxUnit[i],kBoxId,42+i);
        boxModel[i][0]=50.0f+static_cast<float>(i);boxModel[i][1]=2.0f;boxModel[i][2]=30.0f;boxModel[i][3]=1.0f;
    }
    collects=notifies=heals=0;collectBy=notifyBy=healed=nullptr;notifyId=0;notifyKind=applyKind=-1;boxesOk=true;sweep.on=true;
}
void Expect(bool pass,const char* what) { std::printf("%s: %s\n",pass ? "PASS" : "FAIL",what);if(!pass)++failures; }
bool StaleSweepLeavesLockFree() {
    const void* stale=reinterpret_cast<const void*>(1);
    bool caught=false;
    __try { StartSweep(&stale,1,"test stale selection"); }
    __except(EXCEPTION_EXECUTE_HANDLER) { caught=true; }
    const bool free=TryAcquireSRWLockExclusive(&sweepLock)!=0;
    // On the old path this same thread leaked the lock; release it too so a failed check cannot hang the suite.
    ReleaseSRWLockExclusive(&sweepLock);
    return caught && free;
}
void* followSelf[16]{};void* followTo[16]{};
Soldier* nextFollowWatch=nullptr;bool boardingAtFollow=false;
void __fastcall FollowRec(void* self,void* leader,bool) {
    if(nextFollowWatch){boardingAtFollow=static_cast<bool>(nextFollowWatch->boardV);nextFollowWatch=nullptr;}
    Put<void*>(self,kLeader,leader);if(follows<16){followSelf[follows]=self;followTo[follows]=leader;}++follows;
}
// Whom `self` was last made to follow (nullptr none; `none` when it was not re-parented).
const void* none=reinterpret_cast<const void*>(1);
const void* FollowedBy(const void* self) { const void* to=none;for(int i=0;i<follows && i<16;++i)if(followSelf[i]==self)to=followTo[i];return to; }
int kicks=0;
// The stock get-off (kSeatKick): the rider leaves its seat and stands.
void __fastcall KickRec(void* vehicleObj,void* seat) {
    (void)vehicleObj;++kicks;
    auto* h=At<unsigned char*>(seat,kSeatRider);
    Put<void*>(seat,kSeatRider,nullptr);
    if(h){Put<void*>(h,kHumanRiding,nullptr);Put<void*>(h,kHumanVehicleCtrl,nullptr);Put<void*>(h,kHumanSeat,nullptr);}
}
void __fastcall RideRec(void* actor,SharedRef* ref,int slot) {
    --*reinterpret_cast<int*>(static_cast<unsigned char*>(ref->ctrl)+8);++rides;
    if(completeRide) {
        auto* h=static_cast<unsigned char*>(actor);auto* v=static_cast<unsigned char*>(ref->obj);auto* seat=SeatAt(v,static_cast<unsigned>(slot));
        Put<void*>(h,kHumanRiding,v);Put<void*>(h,kHumanVehicleCtrl,ref->ctrl);Put<void*>(h,kHumanSeat,seat);
        Put<void*>(seat,kSeatRider,h);Put<const void*>(seat,kSeatRiderCtrl,At<const void*>(h,kSelfCtrl));
    }
}
void Jump(unsigned rva,const void* to) { auto p=image+rva;p[0]=0x48;p[1]=0xB8;std::memcpy(p+2,&to,8);p[10]=0xFF;p[11]=0xE0; }
// The lists filled by hand, then indexed as Gather leaves them: enemyIndex as SeeEnemy makes it (one row an object, its
// first), the grids by IndexWorld.
void IndexFixture() {
    world.enemyIndex.Clear();
    for(int i=0;i<world.enemies;++i)
        if(world.enemyIndex.Find(world.enemy[static_cast<std::size_t>(i)].object)<0)world.enemyIndex.Set(world.enemy[static_cast<std::size_t>(i)].object,i);
    IndexWorld(world);
}
// Gather run as ThinkHook runs it: a fault caught by the caller's handler. True when it faulted.
bool GatherCaught(std::int32_t team) {
    __try { Gather(team); return false; } __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
}
void Reset() {
    heldSupportActor=nullptr;playerObj=nullptr;ResetNpcAi();ResetNpcParallel();config=Config{};now=1000;frame=1;sessionOn=false;host=true;door=true;wall=false;flatFloor=completeRide=false;follows=rides=announcedBoards=0;
    arcWallZ=0.0f;stubPlans.clear();
    std::memset(human,0,sizeof(human));std::memset(dead,0,sizeof(dead));std::memset(other,0,sizeof(other));
    std::memset(vehicle,0,sizeof(vehicle));std::memset(seats,0,sizeof(seats));std::memset(ctrl,0,sizeof(ctrl));
    for(auto p : {human,dead,other}) { Put<void*>(p,0,image+kSoldiers[0].vtable);Put<int>(p,kTeam,kTeamFriend); }
    Put<void*>(human,kLeader,dead);dead[kDead]=1;
    Put<void*>(dead,kFollowers,head);Put<void*>(head,0,node);Put<void*>(node,0,head);Put<void*>(node,0x10,human);
    Put<void*>(vehicle,kSeats,seats);Put<std::uint64_t>(vehicle,kSeatCount,2);Put<void*>(vehicle,kSelfCtrl,ctrl);Put<int>(ctrl,8,1);
    Put<unsigned>(human,kHumanMask,1);Put<unsigned>(seats+edf::kSeatStride,kSeatClass,1);Put<unsigned>(seats+edf::kSeatStride,kSeatEnable,1);
    doorAt[0]=doorAt[1]=doorAt[2]=0;doorReach=3.0f;
    gunnerEnemy=nullptr;vehicleCopyOwner=online::kCopyHost;
    gatherFoes=0;gatherFaults=false;
    mapHeld=true;rayOn=false;markEnemy=nullptr;pointOrders=0;markinput::down=false;
    ok=followOk=rideOk=true;world.frame=frame;config.npcSquadSuccession=true;
    transportTakes=false;transportOrders=transportCancels=transportPairs=0;transportPaired=transportPairTop=transportSucceededTo=nullptr;
    transportWithdraw=NpcCommandReason::noTransport;transportWithdrawable=false;
}
}
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept { return now; }
ULONGLONG GameFrame() noexcept { return frame; }
void SeeFrame(const void*) noexcept {}
bool InSession() noexcept { return sessionOn; }
bool OnlineHostOnly() noexcept { return !sessionOn || host; }
// host_settings.h stand-ins: no room here, so this machine's ini (host_settings_logic.h Decide outside a room).
bool VanillaRecruitInEffect() noexcept { return Cfg().vanillaRecruit; }
bool NpcFreeSquadCoverInEffect() noexcept { return Cfg().npcFreeSquadCover; }
// The real rules (online_authority.h) on the stand-in objects' flags: a soldier, no vehicle facts.
bool IsOnlineAuthority(const void* o) noexcept {
    return online::Authority(online::Facts{sessionOn,true,host,At<std::uint16_t>(o,0x128),false,false,o==vehicle ? vehicleCopyOwner : online::kCopyHost,0});
}
bool OnlineMaySeatNpc(const void* v) noexcept {
    return online::MaySeatNpc(online::Facts{sessionOn,true,host,At<std::uint16_t>(v,0x128),true,false,online::kCopyHost,0});
}
unsigned char* PlayerHuman() noexcept { return playerObj; }
bool CameraRay(float* eye,float* dir) noexcept {
    if(!rayOn)return false;
    eye[0]=eye[1]=eye[2]=dir[0]=dir[1]=0;dir[2]=1;return true;
}
bool HumanOnFoot(const unsigned char* h) noexcept { return !At<void*>(h,kHumanVehicleCtrl); }
bool MapHoldsKeys() noexcept { return mapHeld; }
bool KnownVehicle(const void* v) noexcept { return v==vehicle; }
bool vehicleJet=false;   // the stand-in vehicle is a fighter (vehicle.cpp IsJet): no vehicle to BOARD (npcai.cpp Boardable)
bool IsJet(const void* v) noexcept { return vehicleJet && v==vehicle; }
bool KnownVehicleVtable(const void* vt) noexcept { return vt && vt==At<const void*>(vehicle,0); }   // the stand-in's class
bool drivableVehicle=true;   // the stand-in vehicle is a ground one a soldier can drive (npcpost.cpp NpcDrivable)
bool NpcDrivable(const unsigned char* v) noexcept { return drivableVehicle && v==vehicle; }
bool PrepareNpcVehicleAi(unsigned char*) noexcept {return true;}
int tankCommands=0;Command lastTankCommand{};
bool TankCommand(const void*,const Command& command,const ObjRef&,const ObjRef&) noexcept {++tankCommands;lastTankCommand=command;return true;}
const unsigned char* CommanderAvatar(const unsigned char*) noexcept { return nullptr; }
float MapRay(const float* from,const float* to,float* hit) noexcept {
    if(arcWallZ>0.0f && from[2]<arcWallZ && to[2]>=arcWallZ) {
        const float t=(arcWallZ-from[2])/(to[2]-from[2]);
        for(int k=0;k<3;++k)hit[k]=from[k]+(to[k]-from[k])*t;
        return npc::Dist(from,hit);
    }
    if(!wall)return -1.0f;hit[0]=0;hit[1]=1.5f;hit[2]=20;return 20;
}
// The object layer (the routes' vehicle bodies, npcai.cpp HullClear): no bodies here (the body->object check is not
// installed in the fixture's image either, so the routes go without the vehicles' query).
float ObjectRay(const float*,const float*,float*,std::uint32_t*) noexcept { return -1.0f; }
int VehicleClassOf(const void*) noexcept { return -1; }   // HullClear's owner test: never reached without a body
bool SeatPoint(const unsigned char*,unsigned,float* at,float* reach) noexcept {
    if(!door)return false;std::memcpy(at,doorAt,12);*reach=doorReach;return true;
}
bool VisitEnemiesOf(std::int32_t,EnemyVisitor visit,void* ctx) noexcept {
    if(markEnemy){const float at[3]={0,0,20};visit(ctx,markEnemy,at);}
    for(int k=0;k<gatherFoes;++k){const float at[3]={10.0f*static_cast<float>(k),0.0f,10.0f*static_cast<float>(k)};visit(ctx,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x5000+k*0x10)),at);}
    if(gatherFaults)visit(ctx,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x6000)),reinterpret_cast<const float*>(static_cast<std::uintptr_t>(0x10)));
    return true;
}
#ifdef SUPPORT_INFANTRY_NATIVE_TEST
float MapFloorRay(const float* a,const float* b,float* at) noexcept {
    if(a[1]<0 || b[1]>0)return -1;
    at[0]=a[0];at[1]=0;at[2]=a[2];return a[1];
}
#else
float MapFloorRay(const float* a,const float* b,float* at) noexcept {
    if(flatFloor){if(a[1]<0 || b[1]>0)return -1;at[0]=a[0];at[1]=0;at[2]=a[2];return a[1];}
    at[0]=at[1]=0;at[2]=30;return rayOn ? 30.0f : -1.0f;
}
#endif
Sea SeaAt(float,float,float*) noexcept { return Sea::land; }
int MapCommandGuardAt(const float*) noexcept { ++pointOrders;return 1; }
// The custom Q's side (qmark.cpp): the ray the camera's centre here; the point marks and the cues counted.
bool QMarkRay(const unsigned char*,float* eye,float* dir) noexcept { return CameraRay(eye,dir); }
int pointMarks=0,cueOwn=0,cueOff=0,qmarkFrames=0,pointClears=0;bool pointShown=false;
void QMarkSetPoint(const float*) noexcept { ++pointMarks;pointShown=true; }
bool QMarkClearPoint(float* at) noexcept {
    ++pointClears;at[0]=at[1]=0.0f;at[2]=30.0f;
    const bool was=pointShown;pointShown=false;return was;
}
void QMarkPlay(QMarkCue c) noexcept { cueOwn+=c==QMarkCue::own;cueOff+=c==QMarkCue::off; }
void QMarkFrame() noexcept { ++qmarkFrames; }
// EDF6AutoTurret's turret seat (turretaim.cpp AutoTurretSeat): the player at one, its auto-aim, keyboard, lock and the
// lock rule's pick; its lock key Q.
bool turretSeat=false,turretAuto=false,turretKeys=true;const void* turretLockTarget=nullptr;const void* turretPick=nullptr;
bool AutoTurretSeat(edf::aimlink::TurretLockV1* t) noexcept {
    *t=edf::aimlink::TurretLockV1{sizeof(*t),turretAuto ? turretLockTarget : nullptr,0x51,turretAuto,turretKeys,turretPick,{0,0,20}};
    return turretSeat;
}
// The player at an aircraft's stick (playerjet.cpp PlayerJetMarkTarget) and its store's lock; at a Dreadnought's bridge
// holding the mark key (dreadnought.cpp DreadnoughtBridgeClaims).
bool jetStick=false,bridgeHolds=false;const void* jetLocked=nullptr;
bool PlayerJetMarkTarget(const unsigned char*,const void** object,float* at) noexcept {
    *object=jetStick ? jetLocked : nullptr;at[0]=at[1]=0.0f;at[2]=50.0f;
    return jetStick;
}
bool DreadnoughtBridgeClaims(const unsigned char*,int) noexcept { return bridgeHolds; }
// The teammates' marked enemies (qmark.cpp QMarkTeamEnemies: live, found in this world).
const void* teamMarks[4]{};int teamMarkCount=0;
int QMarkTeamEnemies(const void** out,int max) noexcept {
    int n=0;for(;n<teamMarkCount && n<max;++n)out[n]=teamMarks[n];
    return n;
}
// The lock registry's valid lock points whatever their lockable flag (the marked enemy out of sight): `lockAt` for `lockOf`.
const void* lockOf=nullptr;float lockAt[3]{};
bool VisitLockPoints(EnemyVisitor visit,void* ctx) noexcept { if(lockOf)visit(ctx,lockOf,lockAt);return true; }
bool nativePayloadFixture=false;
bool NativePayloadControl(const void*,unsigned) noexcept {return nativePayloadFixture;}
unsigned char* chosenGunnerWeapon=nullptr;
unsigned char* chosenSecondaryWeapon=nullptr;const void* lastIndependentWeapon=nullptr;int independentPulls=0;
unsigned char* NpcSecondarySelect(unsigned char*,unsigned,float,bool) noexcept {return chosenSecondaryWeapon;}
bool FireIndependentSecondary(unsigned char* v,unsigned seat,const unsigned char* expectedWeapon) noexcept {
    if(!chosenSecondaryWeapon || (expectedWeapon && expectedWeapon!=chosenSecondaryWeapon) ||
       At<float>(SeatAt(v,seat),0x2E0)<0.8f)return false;
    lastIndependentWeapon=expectedWeapon;++independentPulls;return true;
}
bool suppressGunnerChoice=false;
PayloadFire chosenGunnerFire=PayloadFire::primary;
bool FriendInLine(const float*,const float*,const void*) noexcept { return false; }   // heli.cpp's (NpcOwnWeaponsFire's check)
// wastedfire.cpp WastedFire: the target every shooter's fire is wasted on (the shut teleportation ship), as the case sets.
const void* wastedTarget=nullptr;
// The plans the game thread's asks made (wastedfire.cpp PlanFor: a pair's, per round, kept 150 ms): what the parallel
// Decide's peek reads for a target HoldFire plans for (heldTarget, or any with a weak spot to aim at).
bool PlanKind(const void* target) noexcept;
void StubPlanMade(const void* self,const void* target,float speed,float gravity) noexcept;
// A crowd's targets every shooter's fire is wasted on (the invincible flag: shunned[k] for the object at
// crowdBase+k*crowdStride), and the targets one shooter's own hits held (fedShooter: fed[k]); wastedAsks counts the asks.
const unsigned char* crowdBase=nullptr;std::size_t crowdStride=0;
std::vector<unsigned char> shunned,fed;
const void* fedShooter=nullptr;
long long wastedAsks=0;
int CrowdIndex(const void* target) noexcept {
    const auto p=static_cast<const unsigned char*>(target);
    if(!crowdBase || p<crowdBase || p>=crowdBase+crowdStride*shunned.size())return -1;
    return static_cast<int>(static_cast<std::size_t>(p-crowdBase)/crowdStride);
}
bool WastedFire(const void* self,const void* target) noexcept {
    ++wastedAsks;
    if(PlanKind(target))StubPlanMade(self,target,0.0f,0.0f);
    if(const int k=CrowdIndex(target);k>=0)
        return shunned[static_cast<std::size_t>(k)] || (self==fedShooter && fed[static_cast<std::size_t>(k)]);
    return target && target==wastedTarget;
}
// wastedfire.cpp WatchTarget: the objects Gather had watched, in order.
std::vector<const void*> watchedTargets;
void WatchTarget(const void* target) noexcept { watchedTargets.push_back(target); }
// wastedfire.cpp WastedOnEveryShooter: the crowd's shunned (the invincible flag), none else.
bool WastedOnEveryShooter(const void* target) noexcept {
    const int k=CrowdIndex(target);
    return k>=0 && shunned[static_cast<std::size_t>(k)];
}
// HoldFire: the same target held, and one due to open (heldTarget) aimed at and held; `aim` the weak spot when set.
const void* heldTarget=nullptr;float weakAim[3]{};bool weakAimSet=false;
float predictedSpeed=0.0f,predictedGravity=0.0f;
bool HoldFire(const void* self,const void* target,const float*,float speed,float gravity,float* aim) noexcept {
    predictedSpeed=speed;predictedGravity=gravity;
    if(PlanKind(target)){StubPlanMade(self,target,0.0f,0.0f);StubPlanMade(self,target,speed,gravity);}
    if(aim && weakAimSet && target)std::memcpy(aim,weakAim,12);
    return target && (target==wastedTarget || target==heldTarget);
}
// wastedfire.cpp's read-only side for the parallel Decide: an ordinary target's verdict (WastedFire's), -1 for one
// HoldFire decides with a plan of its own here (heldTarget, or a weak spot to aim at); the version bumped by the case
// where the game's damage hook would settle a hit (wastedVersion).
std::uint64_t wastedVersion=1;
bool PlanKind(const void* target) noexcept { return target && (target==heldTarget || weakAimSet); }
void StubPlanMade(const void* self,const void* target,float speed,float gravity) noexcept {
    for(StubPlan& q:stubPlans)if(q.self==self && q.target==target && q.speed==speed && q.gravity==gravity){q.at=now;return;}
    stubPlans.push_back(StubPlan{self,target,speed,gravity,now});
}
bool StubPlanned(const void* self,const void* target,float speed,float gravity) noexcept {
    for(const StubPlan& q:stubPlans)
        if(q.self==self && q.target==target && q.speed==speed && q.gravity==gravity)return now-q.at<150;
    return false;
}
FirePeek WastedFirePeekFor(const void* self,const void* target,float speed,float gravity) noexcept {
    FirePeek r{0,0,false,{}};
    if(!target || !self)return r;
    const int fb=target==wastedTarget ? 1 : 0;
    if(!PlanKind(target)){r.wasted=r.hold=fb;return r;}
    r.wasted=StubPlanned(self,target,0.0f,0.0f) ? fb : -1;
    if(!StubPlanned(self,target,speed,gravity)){r.hold=-1;return r;}
    r.hold=target==wastedTarget || target==heldTarget ? 1 : 0;
    if(weakAimSet){r.aimSet=true;std::memcpy(r.aim,weakAim,12);}
    return r;
}
int WastedFirePeek(const void* self,const void* target) noexcept { return WastedFirePeekFor(self,target,0.0f,0.0f).wasted; }
void WastedFirePrime(const void* const*,int) noexcept {}
std::uint64_t WastedFireVersion() noexcept { return wastedVersion; }
MiniDumpWriteDumpFn MiniDumpWriter() noexcept { return nullptr; }   // crash_dump.cpp: no dump from the fixtures
int coaxCalls=0;   // payload.cpp NpcOwnWeaponsFire: called beside every crew aim (no coax or keyed weapons on these fixtures)
bool NpcOwnWeaponsFire(unsigned char*,unsigned,const void*,LineBlocked) noexcept { ++coaxCalls;return false; }
void SeatTurretsTrain(unsigned char*,unsigned,const void*) noexcept {}   // seat_turret.cpp: no gunner seat turrets on these fixtures
unsigned char* NpcPayloadSelect(unsigned char* v,unsigned seat,float,bool,PayloadFire* fire) noexcept {
    if(fire)*fire=chosenGunnerFire;
    if(suppressGunnerChoice)return nullptr;
    if(chosenGunnerWeapon)return chosenGunnerWeapon;
    const auto list=At<unsigned char**>(SeatAt(v,seat),kSeatWeapons);
    return list && At<std::uint64_t>(SeatAt(v,seat),kSeatWeaponCount) ? At<unsigned char*>(list[0],kHolderWeapon) : nullptr;
}
bool VisitEnemies(const unsigned char*,EnemyVisitor visit,void* ctx) noexcept {
    if(gunnerEnemy){const float aim[3]={0,0,20};visit(ctx,gunnerEnemy,aim);}
    return true;
}
// The frame's copy picks what the live walk does (tools/enemy_points_check.cpp): the stub enemy, the same rule.
bool NearestEnemyThisStep(const unsigned char* v,const float* from,float reach,EnemyUsable usable,void* ctx,
                          const void*& best,float& bestDist) noexcept {
    best=nullptr;bestDist=1e30f;
    struct C { const float* from; float reach; EnemyUsable usable; void* ctx; const void** best; float* bestDist; };
    C c{from,reach,usable,ctx,&best,&bestDist};
    return VisitEnemies(v,[](void* x,const void* object,const float* aim) {
        auto& k=*static_cast<C*>(x);
        const float d=npc::Dist(k.from,aim);
        if(d<=k.reach && d<*k.bestDist && k.usable(k.ctx,object)){*k.best=object;*k.bestDist=d;}
    },&c);
}
}
#ifndef SUPPORT_INFANTRY_NATIVE_TEST
static bool ScanPackArc(const crew::npcpack::Weapon& w,const float* from,const float* to,float pitch,
                        const std::vector<npc::Friend>& friends) noexcept {
    using namespace crew::npcpack;
    const float horiz=npc::Horiz(from,to),c=std::cos(pitch);
    if(!(horiz>0.01f) || !(w.speed>0.0f) || !(c>1e-4f))return false;
    const float flight=horiz/(w.speed*c);
    if(!std::isfinite(flight) || !(flight>0.0f) || flight>kFlightMost)return false;
    const float byTime=flight/kArcTimeStep,byLength=(w.speed+w.gravity*flight)*flight/kArcStep;
    const float segments=std::ceil(byTime>byLength ? byTime : byLength);
    if(!std::isfinite(segments) || segments>256.0f)return false;
    const int steps=segments>1.0f ? static_cast<int>(segments) : 1;
    const float vx=(to[0]-from[0])/flight,vz=(to[2]-from[2])/flight,vy=w.speed*std::sin(pitch);
    float previous[3]={from[0],from[1],from[2]};
    for(int i=1;i<=steps;++i) {
        const float t=flight*static_cast<float>(i)/static_cast<float>(steps);
        const float point[3]={from[0]+vx*t,from[1]+vy*t-0.5f*w.gravity*t*t,from[2]+vz*t};
        if(!npc::ShotClear(previous,point,crew::kSpread,0.0f,friends.data(),static_cast<int>(friends.size())))return false;
        std::memcpy(previous,point,sizeof(previous));
    }
    const float radius=Radius(w);
    if(radius>0.0f)for(const auto& f:friends)if(npc::Dist(to,f.pos)<radius*1.25f+f.radius)return false;
    return true;
}

static void CheckPackArcWork() {
    using namespace crew;
    const float from[3]={0,0,0},to[3]={0,0,100};
    const npcpack::Weapon pack{PackUse::thrown,150.0f,5.0f,0.0f,55.0f,npcpack::kGravity,0.0f,true};
    float pitch=0,flight=0;
    Expect(npcpack::LobPitch(100.0f,0.0f,pack.speed,pack.gravity,&pitch,&flight),"dense arc fixture has a trajectory");
    constexpr int friendCount=1200;
    std::vector<npc::Friend> crowd;
    crowd.reserve(friendCount);
    for(int i=0;i<friendCount;++i)crowd.push_back({{0.0f,55.0f,70.0f+static_cast<float>(i%61)*0.5f},0.9f});
    npc::grid::Grid grid;
    grid.Build(friendCount,[&](int i) { return crowd[static_cast<std::size_t>(i)].pos; },4.0f);
    const auto noSelf=[](int) noexcept { return false; };
    int comparisons=0,rays=0;
    const bool clear=npcpack::ArcClear(pack,from,to,pitch,kSpread,[&](const npc::Shot* chords,int count) noexcept {
        return npc::grid::ArcChordsClear(grid,chords,count,0.9f,[&](int i) noexcept -> const npc::Friend& {
            comparisons+=count;  // every candidate here clears every chord (the crowd is 55 m above the ground)
            return crowd[static_cast<std::size_t>(i)];
        },noSelf);
    },[&](const float* landing,float radius) noexcept {
        return npc::grid::BlastClear(grid,npc::Shot(landing,landing,0.0f,radius),0.9f,[&](int i) noexcept -> const npc::Friend& {
            return crowd[static_cast<std::size_t>(i)];
        },noSelf);
    },[&](const float*,const float*) noexcept { ++rays;return false; });
    // Every point projects into the old whole-shot corridor. Previously each chord tested all 1,200 points.
    const int before=rays*friendCount;
    Expect(clear && ScanPackArc(pack,from,to,pitch,crowd) && comparisons>0 && comparisons<before/2,
           "dense arc: identical safety verdict with less than half the chord-friend tests");
    std::printf("backpack arc: %d chords tested against %d friends, %d -> %d chord-friend tests\n",
                rays,friendCount,before,comparisons);
}

int main() {
    CheckPackArcWork();
    edf::SetInputSource(&markinput::Key,&markinput::Window);   // the plugin's key source (edf/input_source.h)
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    Jump(kSetFollow,reinterpret_cast<const void*>(&FollowRec));Jump(kRideVehicle,reinterpret_cast<const void*>(&RideRec));
    Jump(kSeatKick,reinterpret_cast<const void*>(&KickRec));
    Jump(kNotifyBox,reinterpret_cast<const void*>(&NotifyRec));Jump(kApplyBox,reinterpret_cast<const void*>(&ApplyRec));Jump(kHealHuman,reinterpret_cast<const void*>(&HealRec));
    Jump(kNodePos,reinterpret_cast<const void*>(&NodePosRec));
    Reset();Put<int>(human,kTeam,1);PreThink(human);
    Expect(follows==0,"enemy soldiers are never reorganized by friendly NPC AI");
    Reset();Put<unsigned>(human,kObjectFlags,kFixPosition);PreThink(human);
    Expect(follows==0,"a fixed-position scripted follower keeps its native leader transition");
    Reset();PreThink(human);Expect(follows==1,"an ordinary friendly remnant still elects its leader");
    Expect(transportSucceededTo==human,"its transport goes with the squad to the new leader (transport.cpp TransportSucceed)");
    {
        // The map's selection and RTS groups hold a squad by its top's identity: the hand-over names the new top.
        ObjRef next{};
        Expect(NpcSquadSuccessor(ObjRef::Of(dead),&next) && next.obj==human,"the dead top's squad goes on as its new leader's");
        Expect(!NpcSquadSuccessor(ObjRef::Of(other),&next),"a top that handed over to none has no successor");
    }
    Reset();world.friends=1;IndexFixture();world.frObject[0]=other;Put<std::uint64_t>(other,kFollowerCount,1);Put<unsigned>(other,kObjectFlags,kFixPosition);
    unsigned char* leaders[4];float places[4][3];int sizes[4];
    Expect(OtherSquads(dead,false,leaders,places,sizes,4)==0,"fixed script squads cannot absorb a remnant");
    Reset();sessionOn=true;config.scriptNpcSettleSec=0;Put<void*>(human,kLeader,nullptr);Put<void*>(human,kRoute,other);
    SeeSquad(human,human,0,npc::Control::script,now);++frame;++now;Put<void*>(human,kRoute,nullptr);
    SeeSquad(human,human,0,npc::Control::free,now);
    Expect(human[kAutoFollow]==0,"online script release does not write the unreplicated recruit flag");
    Reset();Soldier s{};s.boardV=ObjRef::Of(vehicle);s.boardSeat=1;s.boardAt=now;
    const float above[3]={0,20,0};Board(s,human,above,now);
    Expect(rides==0,"a soldier above a seat cannot board through another floor");
    Reset();s=Soldier{};s.boardV=ObjRef::Of(vehicle);s.boardSeat=1;s.boardAt=now;door=false;
    const float nearby[3]={0,0,0};Board(s,human,nearby,now);
    Expect(rides==0,"an unreadable seat door cannot fall back to boarding at the vehicle origin");
    Reset();s=Soldier{};s.boardV=ObjRef::Of(vehicle);s.boardSeat=1;s.boardAt=now;Board(s,human,nearby,now);
    Expect(rides==1 && At<int>(ctrl,8)==1,"a reachable door boards and balances the native by-value shared reference");
    Reset();world.friends=1;world.frObject[0]=other;world.fr[0]=npc::Friend{{8,1.5f,20},0.8f};IndexFixture();
    human[kTrigger]=human[kTrigger+1]=1;Enemy target{dead,{0,1.5f,20},1};Arms arms{};arms.n=2;arms.held[0]=0;arms.held[1]=1;
    arms.arm[0]=npc::Arm{100,0,1,false,true,false};arms.arm[1]=npc::Arm{100,10,1,false,true,false};
    Put<void*>(human,kStockTarget,dead);Put<void*>(human,kStockTargetCtrl,ctrl);world.enemies=1;world.enemy[0]=target;IndexFixture();
    const float eye[3]={0,1.5f,0};Scripted(s,human,arms,eye,nearby);
    Expect(human[kTrigger+1]==0,"scripted Fencer secondary blast is checked even when primary is nonexplosive");
    Expect(human[kTrigger]==1,"the nonexplosive primary hand keeps firing while only the explosive second hand is vetoed");
    Reset();world.friends=1;world.frObject[0]=other;world.fr[0]=npc::Friend{{8,1.5f,20},0.8f};IndexFixture();
    arms=Arms{};arms.n=2;arms.held[0]=0;arms.arm[0]=npc::Arm{100,0,1,false,true,false};arms.arm[1]=npc::Arm{100,10,1,false,true,false};
    human[kTrigger]=1;Veto(human,&target,eye,arms);
    Expect(human[kTrigger]==1,"a held rifle is not vetoed for the rocket launcher only carried in the weapon list");
    arms.held[0]=-1;human[kTrigger]=1;Veto(human,&target,eye,arms);
    Expect(human[kTrigger]==0,"a hand whose weapon is unknown is tested with the largest carried blast");
    Reset();wall=true;world.friends=1;world.frObject[0]=other;world.fr[0]=npc::Friend{{8,1.5f,20},0.8f};IndexFixture();
    arms=Arms{};arms.n=1;arms.held[0]=0;arms.arm[0]=npc::Arm{100,10,1,false,true,false};
    human[kTrigger]=1;Veto(human,nullptr,eye,arms);
    Expect(human[kTrigger]==0,"unknown target still checks the blast at the actual map impact");
    Reset();world.friends=1;world.frObject[0]=other;world.fr[0]=npc::Friend{{0,1.5f,90},0.8f};IndexFixture();
    arms=Arms{};arms.n=1;arms.held[0]=0;arms.arm[0]=npc::Arm{100,0,1,false,true,false};
    human[kTrigger]=1;Veto(human,nullptr,eye,arms);
    Expect(human[kTrigger]==0,"unknown target fire lane extends beyond 60 metres to the weapon reach");
    {   // the native WeaponSet array: kSetStride apart, each set's weapon two pointers down from +kSetWeapon
        static unsigned char sets[kSetStride*2]{},w0[0xC00]{},w1[0xC00]{},w2[0xC00]{};
        static unsigned char* list[3]{w0,w1,w2};static unsigned char* slot0[1]{w2};static unsigned char* slot1[1]{w1};
        Reset();Put<void*>(human,kWeapons,list);Put<std::uint64_t>(human,kWeaponCount,3);
        Put<void*>(human,kSets,sets);Put<std::uint64_t>(human,kSetCount,2);
        Put<void*>(sets,kSetWeapon,slot0);Put<void*>(sets+kSetStride,kSetWeapon,slot1);
        Put<float>(w2,0x894,2.0f);Put<float>(w2,0x8E0,0.5f);
        ArmsSeen seen{};const Arms both=ArmsOf(human,seen);
        Expect(both.n==3 && both.held[0]==2 && both.held[1]==1,"each WeaponSet's held weapon is found at the native set stride");
        Expect(both.speed[2]==120.0f && std::fabs(both.gravity[2]-7.35f)<0.001f,
               "the held weapon's native speed and gravity are converted to metres and seconds");
        Put<std::uint64_t>(human,kSetCount,1);
        seen={};Expect(ArmsOf(human,seen).held[1]==-1,"a one-handed soldier has no second hand's weapon");
    }
    // ArmsOf with its pages asked in two batches (edf::PrefetchReadableSpans) = the ArmsOf before them (one call for
    // the list, one for the weapons' batch, one each for a set and an entry), over lists with unreadable, null and
    // shared weapons, no / one / two sets, a set's entry null or unreadable; and two working-set calls in a fresh tick.
    {
        constexpr std::size_t kPage=0x1000;
        constexpr int kPages=48;
        auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,kPages*kPage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        Expect(block!=nullptr,"ArmsOf fixture pages");
        if(block) {
            DWORD old=0;
            VirtualProtect(block+40*kPage,kPage,PAGE_NOACCESS,&old);   // an unreadable weapon / entry / set
            unsigned char* const noAccess=block+40*kPage;
            // weapons on pages 0..19 (two pages each: kWeaponAmmo+4 is 0xBEC bytes, put across a page edge)
            unsigned char* weapons[10];
            for(int i=0;i<10;++i) {
                weapons[i]=block+static_cast<std::size_t>(2*i)*kPage+0x800;
                Put<float>(weapons[i],0x894,1.0f+static_cast<float>(i));Put<float>(weapons[i],0x8E0,0.1f*static_cast<float>(i));
                Put<float>(weapons[i],kArmReach,50.0f*static_cast<float>(i+1));Put<float>(weapons[i],kArmDamage,i==3 ? -1.0f : 7.0f);
                Put<float>(weapons[i],kArmBlast,i%3==0 ? 4.0f : 0.0f);Put<std::int32_t>(weapons[i],kWeaponLockon,i==2 ? kHoming : 0);
                Put<std::int32_t>(weapons[i],kWeaponAmmo,i==4 ? 0 : 30);
            }
            auto** list=reinterpret_cast<unsigned char**>(block+24*kPage);
            unsigned char* const sets=block+26*kPage;   // two sets kSetStride apart
            auto** entries=reinterpret_cast<unsigned char**>(block+30*kPage);
            const auto before=[&](const unsigned char* h) {   // ArmsOf as it was (b1d80bec), SetWeapon its own
                Arms a{};
                const auto l=At<unsigned char* const*>(h,kWeapons);
                const auto count=At<std::uint64_t>(h,kWeaponCount);
                if(!count || count>16 || !Readable(l,count*8))return a;
                const unsigned char* held[kHands];
                for(int k=0;k<kHands;++k)held[k]=SetWeapon(h,k);
                a.n=count<kMaxArms ? static_cast<int>(count) : kMaxArms;
                for(int i=0;i<a.n;++i) {
                    const unsigned char* w=l[i];
                    npc::Arm& m=a.arm[i];m=npc::Arm{};
                    if(!Readable(w,kWeaponAmmo+4) || NpcPackIndex(h,i))continue;
                    const float speed=At<float>(w,0x894)*60.0f,factor=At<float>(w,0x8E0);
                    a.speed[i]=std::isfinite(speed) && speed>0.0f ? speed : 0.0f;
                    a.gravity[i]=std::isfinite(factor) && factor>0.0f ? factor*14.7f : 0.0f;
                    for(int k=0;k<kHands;++k)if(w==held[k])a.held[k]=i;
                    const float reach=At<float>(w,kArmReach),damage=At<float>(w,kArmDamage),blast=At<float>(w,kArmBlast);
                    m.reach=std::isfinite(reach) && reach>0.0f ? reach : 0.0f;
                    m.blast=std::isfinite(blast) && blast>0.0f ? blast : 0.0f;
                    m.dps=std::isfinite(damage) && damage>0.0f ? damage : 1.0f;
                    m.homing=At<std::int32_t>(w,kWeaponLockon)==kHoming;
                    m.antiAir=m.homing;
                    m.ready=At<std::int32_t>(w,kWeaponAmmo)>0;
                }
                return a;
            };
            const auto same=[](const Arms& x,const Arms& y) {
                if(x.n!=y.n || x.held[0]!=y.held[0] || x.held[1]!=y.held[1])return false;
                for(int i=0;i<x.n;++i) {
                    const npc::Arm &p=x.arm[i],&q=y.arm[i];
                    if(p.reach!=q.reach || p.blast!=q.blast || p.dps!=q.dps || p.homing!=q.homing || p.ready!=q.ready ||
                       p.antiAir!=q.antiAir || x.speed[i]!=y.speed[i] || x.gravity[i]!=y.gravity[i])return false;
                }
                return true;
            };
            std::mt19937 rng(1108);
            int compared=0,differ=0,armed=0;
            for(int round=0;round<4000;++round) {
                Reset();
                const int count=static_cast<int>(rng()%7);   // 0 (no list) .. 6
                for(int i=0;i<count;++i) {
                    const unsigned r=rng()%12;
                    list[i]=r<10 ? weapons[r] : r==10 ? nullptr : noAccess;
                }
                const bool listDead=rng()%15==0;
                Put<const void*>(human,kWeapons,listDead ? static_cast<const void*>(noAccess) : list);
                Put<std::uint64_t>(human,kWeaponCount,rng()%40==0 ? 17u : static_cast<std::uint64_t>(count));
                const unsigned setCount=rng()%3;
                Put<const void*>(human,kSets,rng()%20==0 ? static_cast<const void*>(noAccess) : sets);
                Put<std::uint64_t>(human,kSetCount,setCount);
                for(int k=0;k<kHands;++k) {
                    const unsigned r=rng()%8;
                    entries[k]=count && r<5 ? list[rng()%static_cast<unsigned>(count)] : r==5 ? weapons[rng()%10] : nullptr;
                    Put<const void*>(sets+k*kSetStride,kSetWeapon,r==7 ? static_cast<const void*>(noAccess) : r==6 ? nullptr : &entries[k]);
                }
                packReady=packSlots=rng()%5==0;packSlot.index=static_cast<int>(rng()%4);
                ArmsSeen fresh{};const Arms got=ArmsOf(human,fresh),was=before(human);
                ++compared;differ+=!same(got,was);armed+=got.n>0 && got.held[0]>=0;
            }
            packReady=packSlots=false;
            char what[160];
            std::snprintf(what,sizeof(what),"ArmsOf with its pages batched = ArmsOf before (%d soldiers, %d differ)",compared,differ);
            Expect(differ==0,what);
            Expect(armed>500,"the soldiers compared held weapons found through their sets");
            // Two calls a soldier: its list and sets, then its weapons and the sets' entries (each a page of its own here).
            for(int i=0;i<4;++i)list[i]=weapons[i];
            entries[0]=weapons[1];entries[1]=weapons[3];
            Put<const void*>(human,kWeapons,list);Put<std::uint64_t>(human,kWeaponCount,4);
            Put<const void*>(human,kSets,sets);Put<std::uint64_t>(human,kSetCount,2);
            Put<const void*>(sets,kSetWeapon,&entries[0]);Put<const void*>(sets+kSetStride,kSetWeapon,&entries[1]);
            bool counted=false;
            for(int attempt=0;attempt<200 && !counted;++attempt) {
                for(const ULONGLONG was=GetTickCount64();GetTickCount64()==was;)Sleep(1);   // nothing kept from before
                const ULONGLONG tick=GetTickCount64();
                edf::TakeReadableStats();
                ArmsSeen seen{};
                const Arms a=ArmsOf(human,seen);
                const edf::ReadableStats st=edf::TakeReadableStats();
                if(GetTickCount64()!=tick)continue;
                counted=true;
                Expect(a.n==4 && a.held[0]==1 && a.held[1]==3,"the fixture soldier's weapons and hands");
                Expect(st.wsCalls==2,"ArmsOf asks the working set twice: the list and sets, then the weapons and entries");
                edf::TakeReadableStats();
                const Arms b=before(human);
                const edf::ReadableStats again=edf::TakeReadableStats();
                Expect(same(a,b) && (again.wsCalls==0 || GetTickCount64()!=tick),"the answers kept for the tick serve the old order too");
                // cpu-2: the same list, sets and entries next time (a live soldier's own): nothing asked again, the same
                // arms; a hand switched within its list read live; a weapon swapped in the list: asked again
                for(const ULONGLONG was=GetTickCount64();GetTickCount64()==was;)Sleep(1);   // the page cache can't answer
                edf::TakeReadableStats();
                const Arms memo=ArmsOf(human,seen);
                const edf::ReadableStats st2=edf::TakeReadableStats();
                Expect(same(a,memo) && st2.wsCalls==0 && st2.asks==0,"ArmsOf asks nothing for a soldier whose arms did not change");
                entries[1]=weapons[2];
                for(const ULONGLONG was=GetTickCount64();GetTickCount64()==was;)Sleep(1);
                edf::TakeReadableStats();
                const Arms swapped=ArmsOf(human,seen);
                const edf::ReadableStats st3=edf::TakeReadableStats();
                Expect(swapped.held[1]==2 && st3.wsCalls==0,"a hand switched to another weapon of its list: the new one, read live, nothing asked");
                list[0]=weapons[5];
                edf::TakeReadableStats();
                const Arms relisted=ArmsOf(human,seen);
                const edf::ReadableStats st4=edf::TakeReadableStats();
                Expect(st4.asks>0 && (!seen.known || seen.weapon[0]==weapons[5]) && relisted.n==4,
                       "a weapon swapped in the list: asked again and fingerprinted anew");
                entries[1]=weapons[3];list[0]=weapons[0];
            }
            Expect(counted,"a tick long enough to count ArmsOf's calls in");
            VirtualFree(block,0,MEM_RELEASE);
        }
    }
    Reset();s=Soldier{};s.wantArm=-1;arms=Arms{};arms.n=4;arms.held[0]=0;
    arms.arm[0]=npc::Arm{5,0,1,false,true,false};arms.arm[1]=npc::Arm{100,0,10,false,true,false};
    arms.arm[3]=npc::Arm{100,0,100,false,true,false};ChooseArm(s,human,arms,&target,eye,nearby);
    Expect(human[kPickWeapon+1]==1,"an inaccessible fourth weapon cannot suppress selecting a usable second one");
    Reset();Put<void*>(human,kLeader,nullptr);config.scriptNpcSettleSec=0;Put<void*>(human,kRoute,other);
    SeeSquad(human,human,0,npc::Control::script,now);++frame;++now;Put<void*>(human,kRoute,nullptr);
    SeeSquad(human,human,0,npc::Control::free,now);
    Expect(human[kAutoFollow]==1,"offline script release still enables recruitment");
    Reset();world.friends=1;IndexFixture();world.frObject[0]=other;Put<std::uint64_t>(other,kFollowerCount,1);
    squads.emplace_back();squadIndex.Set(dead,0);squads[0].top=ObjRef::Of(dead);squads[0].dismissed=true;squads[0].autoFollow=1;dismissedCount=1;
    cooldowns.Start(SquadKey(dead),now,10000);PreThink(human);
    Expect(At<void*>(human,kLeader)==nullptr && squads[0].top.Is(human) && squads[0].dismissed &&
           cooldowns.Left(SquadKey(human),now)==10000 && human[kAutoFollow]==0,
           "a dismissed remnant inherits its cooldown instead of bypassing it by merging");
    now+=10000;++frame;SeeSquad(human,human,0,npc::Control::free,now);
    Expect(human[kAutoFollow]==1 && dismissedCount==0,"the successor restores recruitment when the inherited cooldown expires");
    Reset();Put<void*>(human,kLeader,nullptr);Squad* q=SeeSquad(human,human,0,npc::Control::free,now);
    sessionOn=true;Expect(!SquadCommand(human,Command{Order::guard,{10,0,0}}) && q->cmd.order==Order::none,
                       "online squad commands require a real requesting player");
    sessionOn=false;config.enabled=false;
    Expect(!SquadCommand(human,Command{Order::guard,{10,0,0}}),"direct squad commands respect the total switch");
    // A soldier another machine runs exists only online (its NetworkObject flags +0x128 bit 0): the host does not reorganize it.
    Reset();sessionOn=true;Put<std::uint8_t>(human,0x128,1);PreThink(human);
    Expect(follows==0,"remote-owned soldier succession remains native");
    Reset();sessionOn=true;host=false;PreThink(human);
    Expect(follows==0,"online guest cannot reorganize squads");
    Reset();Put<void*>(human,kLeader,nullptr);SeeSquad(human,human,0,npc::Control::free,now);
    Soldier* walking=Entry(human,now);walking->boardV=ObjRef::Of(vehicle);
    Expect(SquadCommand(human,Command{Order::dismount,{}}) && !walking->boardV,
           "dismount cancels an in-flight boarding order before the soldier has reached its seat");
    walking->boardV=ObjRef::Of(vehicle);
    Expect(SquadCommand(human,Command{Order::guard,{10,0,0}}) && !walking->boardV,
           "a new guard order replaces the earlier walk-to-seat order");
    {   // DISMOUNT and ALL OUT by the seat's role (the user, 2026-10-09: "下车是除了驾驶员，炮手等操作席都下车。全体下车是全
        // 部人"): the production DismountSquad through the stock get-off.
        auto seatIn=[&](unsigned k,std::uint64_t weapons) {
            auto* seat=SeatAt(vehicle,k);
            Put<void*>(human,kHumanRiding,vehicle);Put<void*>(human,kHumanVehicleCtrl,ctrl);Put<long>(ctrl,8,1);Put<void*>(human,kHumanSeat,seat);
            Put<void*>(seat,kSeatRider,human);Put<const void*>(seat,kSeatRiderCtrl,At<const void*>(human,kSelfCtrl));
            Put<std::uint64_t>(seat,kSeatWeaponCount,weapons);
        };
        Reset();Put<void*>(human,kLeader,nullptr);SeeSquad(human,human,0,npc::Control::free,now);
        const int k0=kicks;
        seatIn(1,0);
        Expect(SquadCommand(human,Command{Order::dismount,{}}) && kicks==k0+1 && HumanOnFoot(human),"DISMOUNT takes a passenger off");
        seatIn(1,1);
        Expect(!SquadCommand(human,Command{Order::dismount,{}}) && kicks==k0+1 && !HumanOnFoot(human) &&
               LastNpcCommandResult().reason==NpcCommandReason::noPassengers,"DISMOUNT leaves a gunner aboard (and says why)");
        Expect(SquadCommand(human,Command{Order::dismountAll,{}}) && kicks==k0+2 && HumanOnFoot(human),"ALL OUT takes the gunner off");
        Put<std::uint64_t>(SeatAt(vehicle,1),kSeatWeaponCount,0);
        seatIn(0,0);
        Expect(!SquadCommand(human,Command{Order::dismount,{}}) && kicks==k0+2 && !HumanOnFoot(human),"DISMOUNT leaves the driver aboard");
        Expect(SquadCommand(human,Command{Order::dismountAll,{}}) && kicks==k0+3 && HumanOnFoot(human),"ALL OUT takes the driver off");
    }
    // A recruited squad BOARD paired with a vehicle no order sends off (a mission's truck, a player's helicopter:
    // TransportPair takes any it got into): its WITHDRAW (the card's B, its only way out of command) dismisses it, as
    // with no transport at all -- not refused as "no transport".
    Reset();playerObj=other;Put<unsigned char>(other,kHumanPlayer,1);Put<void*>(other,kHumanPad,other);
    Put<void*>(human,kLeader,other);q=SeeSquad(human,human,0,npc::Control::recruited,now);
    transportPaired=other;transportWithdrawable=false;transportWithdraw=NpcCommandReason::noTransport;
    Expect(SquadCommand(human,Command{Order::withdraw,{}}) && q->dismissed && q->cmd.order==Order::guard,
           "a recruited squad paired with a non-support vehicle: WITHDRAW dismisses it");
    transportPaired=nullptr;
    // A panel order lives only under the lead it was given in (npc::LeadOf).
    Reset();playerObj=other;Put<unsigned char>(other,kHumanPlayer,1);Put<void*>(other,kHumanPad,other);
    Put<void*>(human,kLeader,other);q=SeeSquad(human,human,0,npc::Control::recruited,now);
    Expect(SquadCommand(human,Command{Order::dismiss,{}}) && q->cmd.order==Order::guard,"a dismissed squad holds where it was let go");
    ++frame;++now;SeeSquad(human,human,0,npc::Control::free,now);
    Expect(q->cmd.order==Order::guard,"the dismissal's hold stays while the squad is its own");
    now+=static_cast<ULONGLONG>(config.npcRecruitCooldownSec*1000.0f)+1;++frame;SeeSquad(human,human,0,npc::Control::free,now);
    Expect(!q->dismissed && q->cmd.order==Order::guard,"the cooldown ending does not move it by itself");
    ++frame;++now;SeeSquad(human,human,0,npc::Control::recruited,now);
    Expect(q->cmd.order==Order::none,"recruited again after the cooldown: the hold is dropped and it follows");
    Reset();Put<void*>(human,kLeader,nullptr);q=SeeSquad(human,human,0,npc::Control::free,now);
    Expect(SquadCommand(human,Command{Order::guard,{10,0,0}}),"a free squad takes a guard order");
    ++frame;++now;SeeSquad(human,human,0,npc::Control::squad,now);
    Expect(q->cmd.order==Order::guard,"a squad gaining or losing members keeps its order (still its own lead)");
    ++frame;++now;SeeSquad(human,human,0,npc::Control::recruited,now);
    Expect(q->cmd.order==Order::none,"a guarding squad the player walks up to and recruits follows them, as the panel says");
    Reset();playerObj=other;Put<unsigned char>(other,kHumanPlayer,1);Put<void*>(other,kHumanPad,other);
    Put<void*>(human,kLeader,other);q=SeeSquad(human,human,0,npc::Control::recruited,now);
    Expect(SquadCommand(human,Command{Order::guard,{10,0,0}}),"a recruited squad takes a guard order");
    ++frame;++now;SeeSquad(human,human,0,npc::Control::recruited,now);
    Expect(q->cmd.order==Order::guard,"a recruited squad told to guard keeps guarding while the player still leads it");
    ++frame;++now;SeeSquad(human,human,0,npc::Control::script,now);
    Expect(q->cmd.order==Order::none,"a script taking the squad drops the player's order");
    // Replicas are visible for RPC selection without being inserted into this peer's AI tables.
    {
        Reset();sessionOn=true;host=false;Put<void*>(human,kLeader,nullptr);Put<void*>(human,kSelfCtrl,ctrl);
        Put<unsigned char>(human,edf::kRiderNet+edf::kNetFlags,1);Put<float>(human,kMoveX,0.37f);human[kTrigger]=1;
        const int previousFollows=follows;CommandSquadVisit(nullptr,human);remoteSquadFrame=frame;
        CommandUnit units[2]{};SquadRow rows[2]{};
        Expect(SquadCommandUnits(units,2)==1 && units[0].v==human && SquadRows(rows,2)==1 && rows[0].identity.ctrl==ctrl,
            "read-only remote snapshot publishes actual native squad identity without local Think");
        Expect(!FindSquad(human) && follows==previousFollows && At<float>(human,kMoveX)==0.37f && human[kTrigger]==1,
            "remote listing never creates local AI state or changes move/fire/follow intents");
        ResetNpcAi();Expect(remoteSquadCount==0,"mission reset drops the replica selection snapshot");
    }
    // Commands use physical ownership and one consistent visible/dispatch freshness window.
    {
        Reset();Put<void*>(human,kLeader,nullptr);playerObj=other;
        Put<unsigned char>(other,kHumanPlayer,1);Put<void*>(other,kHumanPad,other);Put<int>(other,kTeam,0);
        auto* commandSquad=SeeSquad(human,human,0,npc::Control::free,now);
        now+=300;
        auto result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::guard,{40,0,0}},ObjRef::Of(other),{});
        Expect(result.Accepted() && commandSquad->cmd.order==Order::guard,"a still-visible 300ms squad accepts a command instead of the old 100ms rejection");
        now+=201;
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::engage,{}},ObjRef::Of(other),{});
        Expect(result.reason==NpcCommandReason::stale && commandSquad->cmd.order==Order::guard,"expired selection refuses without changing the last real order");
        ++frame;SeeSquad(human,human,0,npc::Control::free,now);sessionOn=true;
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::engage,{}},ObjRef::Of(other),{});
        Expect(result.Accepted() && commandSquad->cmd.order==Order::engage,"online AI authority executes engage for the real requesting player");
        Put<unsigned char>(human,edf::kRiderNet+edf::kNetFlags,1);
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::guard,{0,0,0}},ObjRef::Of(other),{});
        Expect(result.reason==NpcCommandReason::notAuthority && commandSquad->cmd.order==Order::engage,"a replica cannot pretend to execute a command");
        Put<unsigned char>(human,edf::kRiderNet+edf::kNetFlags,0);
        Put<void*>(human,kLeader,other);dead[kDead]=0;Put<unsigned char>(dead,kHumanPlayer,1);Put<void*>(dead,kHumanPad,dead);Put<int>(dead,kTeam,0);
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::follow,{}},ObjRef::Of(dead),{});
        Expect(result.reason==NpcCommandReason::notOwner && At<void*>(human,kLeader)==other,"another player cannot steal a recruited squad");
        Put<void*>(human,kLeader,nullptr);int before=follows;
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::recruit,{}},ObjRef::Of(dead),{});
        Expect(result.Accepted() && follows==before+1 && At<void*>(human,kLeader)==dead,"host recruit follows the explicit requester, never host PlayerHuman");
        Put<void*>(human,kRoute,other);
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::engage,{}},ObjRef::Of(dead),{});
        Expect(result.reason==NpcCommandReason::scripted,"live script control blocks commandeering even when the row was previously free");
    }
    // The stock walk-up recruit off (VanillaRecruit=0, the user 2026-10-11: "删了原版的率领npc机制，和指挥跟随重复了"):
    // its one gate (+0x548 == 0 ? +0x540 : 0 at 0x596BD8) reads 0 inside the stock Think, the flag put back after it.
    {
        Reset();Put<void*>(human,kLeader,nullptr);human[kAutoFollow]=1;
        static std::uint8_t seenAuto=0xFF;seenAuto=0xFF;
        struct StockThink { static void __fastcall Run(void* h,const float*) { seenAuto=static_cast<unsigned char*>(h)[kAutoFollow]; } };
        const ThinkFn keep=nextThink[0];nextThink[0]=&StockThink::Run;
        const float dt=1.0f/60.0f;
        ThinkHook<0>(human,&dt);
        Expect(seenAuto==0 && human[kAutoFollow]==1,"VanillaRecruit=0: the stock Think sees no walk-up flag, the script's flag is put back");
        config.vanillaRecruit=true;seenAuto=0xFF;ThinkHook<0>(human,&dt);
        Expect(seenAuto==1 && human[kAutoFollow]==1,"VanillaRecruit=1: the stock walk-up reads the flag as before");
        config.vanillaRecruit=false;config.customNpcAi=false;seenAuto=0xFF;ThinkHook<0>(human,&dt);
        Expect(seenAuto==1,"with the custom AI off the stock walk-up stays (the map cannot recruit then)");
        config.customNpcAi=true;followOk=false;seenAuto=0xFF;ThinkHook<0>(human,&dt);
        Expect(seenAuto==1,"SetFollow not as read: the stock walk-up stays (the map cannot recruit then)");
        followOk=true;Put<void*>(human,kLeader,other);seenAuto=0xFF;ThinkHook<0>(human,&dt);
        Expect(seenAuto==1 && human[kAutoFollow]==1,"a soldier with a leader is left alone (the gate reads 0 for it anyway)");
        nextThink[0]=keep;
        // A script's squad the stock would have let in: the map's RECRUIT / FOLLOW is that walk-up now; nothing else.
        Reset();Put<void*>(human,kLeader,nullptr);playerObj=other;
        Put<unsigned char>(other,kHumanPlayer,1);Put<void*>(other,kHumanPad,other);Put<int>(other,kTeam,0);
        Put<unsigned>(human,kObjectFlags,kFixPosition);human[kAutoFollow]=1;
        SeeSquad(human,human,0,npc::Control::hold,now);
        Expect(JoinOnly(npc::Control::hold,human),"a held script squad with its walk-up flag is joinable from the map");
        auto result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::engage,{}},ObjRef::Of(other),{});
        Expect(result.reason==NpcCommandReason::scripted,"the joinable script squad still takes no other order");
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::recruit,{}},ObjRef::Of(other),{});
        Expect(result.Accepted() && At<void*>(human,kLeader)==other,"RECRUIT joins it to the requester as the stock walk-up did");
        Put<void*>(human,kLeader,nullptr);human[kAutoFollow]=0;
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::follow,{}},ObjRef::Of(other),{});
        Expect(result.reason==NpcCommandReason::scripted && !At<void*>(human,kLeader),"a script squad the stock would not let in stays locked");
        human[kAutoFollow]=1;config.vanillaRecruit=true;
        Expect(!JoinOnly(npc::Control::hold,human),"VanillaRecruit=1: a script squad is joined by walking up, as before");
    }
    // Leader class is not the whole squad: a Wing Diver can lead a Ranger who may occupy the driver seat.
    {
        Reset();Put<void*>(human,kLeader,nullptr);playerObj=other;
        Put<unsigned char>(other,kHumanPlayer,1);Put<void*>(other,kHumanPad,other);Put<int>(other,kTeam,0);
        dead[kDead]=0;unsigned char childCtrl[0x10]{};Put<long>(childCtrl,8,1);Put<void*>(dead,kSelfCtrl,childCtrl);
        Put<unsigned>(human,kHumanMask,2);Put<unsigned>(dead,kHumanMask,1);Put<void*>(dead,kLeader,human);
        Put<void*>(human,kFollowers,head);Put<void*>(head,0,node);Put<void*>(node,0,head);Put<void*>(node,0x10,dead);
        Put<unsigned>(seats,kSeatClass,1);Put<unsigned>(seats,kSeatEnable,1);Put<int>(vehicle,kTeam,0);
        Put<float>(vehicle,kPosition,1000);world.friends=1;IndexFixture();world.frObject[0]=vehicle;
        SeeSquad(human,human,0,npc::Control::free,now);sessionOn=true;
        // The card's seats and the authority's BOARD read one test (npcai.cpp OpenSeats; review 2026-11-27: BoardRoomIn and
        // BoardDistance differed): a fighter is no vehicle to board, with boarding off no squad has a seat anywhere.
        Expect(NpcBoardingOn() && NpcBoardable(vehicle) && NpcBoardRoom(human,vehicle)>0 &&
               NpcBoardRoom(human,nullptr)==NpcBoardRoom(human,vehicle),
            "the card counts the seats the member fits (named, and as BOARD picks: the same vehicle)");
        vehicleJet=true;
        Expect(!NpcBoardable(vehicle) && NpcBoardRoom(human,vehicle)==0 && NpcBoardRoom(human,nullptr)==0,
            "a jet: no carrier, no seat counted");
        Expect(!NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::board,{}},ObjRef::Of(other),ObjRef::Of(vehicle)).Accepted() &&
               !NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::board,{}},ObjRef::Of(other),{}).Accepted() && transportPairs==0,
            "a jet: BOARD named into it or to the nearest is refused, as the card greys it (no pairing)");
        vehicleJet=false;config.npcBoarding=false;
        Expect(!NpcBoardingOn() && NpcBoardRoom(human,vehicle)==0 && NpcBoardable(vehicle),
            "NpcBoarding=0: no seat for any squad (the card says boarding is off), the vehicle still a carrier");
        config.npcBoarding=true;
        auto result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::board,{}},ObjRef::Of(other),{});
        auto* child=Entry(dead,now);
        Expect(result.Accepted() && result.affected==1 && child->boardV.Is(vehicle) && child->boardSeat==0,
            "mixed squad boards a compatible member via nearby entrance even when leader class cannot ride and body centre is far");
        Expect(transportPairs==1 && transportPairTop==human && transportPaired==vehicle,
            "a squad that boards a vehicle is paired with it (its transport from now on: transport.cpp)");
        const auto assignedAt=child->boardAt;++now;
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::board,{}},ObjRef::Of(other),{});
        Expect(result.Accepted() && child->boardAt==assignedAt,"an already valid boarding assignment is not reported as failure or restarted");
        completeRide=true;Board(*child,dead,Pos(dead),now);
        Expect(rides==1 && announcedBoards==1 && At<void*>(dead,kHumanRiding)==vehicle && At<void*>(seats,kSeatRider)==dead,
            "accepted online command reaches real Board/native Ride entry and announces the completed seat transition");
    }
    // 2026-10-10 "步兵不乘啊": an empty stock vehicle is nobody's (team 5), which the friends' walk never visits. Team 5's own
    // walk (NobodyVisit) lists its drivable ground vehicles and BOARD takes them.
    {
        Reset();Put<void*>(human,kLeader,nullptr);playerObj=other;drivableVehicle=true;
        Put<unsigned char>(other,kHumanPlayer,1);Put<void*>(other,kHumanPad,other);Put<int>(other,kTeam,0);
        Put<int>(human,kTeam,0);Put<int>(vehicle,kTeam,kTeamVehicle);
        Put<unsigned>(seats,kSeatClass,1);Put<unsigned>(seats,kSeatEnable,1);
        world.friends=0;IndexFixture();world.nobodys=0;
        FriendWalk walk{kNobodyVtable,&world};
        NobodyVisit(&walk,vehicle);NobodyVisit(&walk,other);
        Expect(world.nobodys==1 && world.nobody[0]==vehicle,"team 5's walk keeps the empty ground vehicle, not other objects");
        drivableVehicle=false;NobodyVisit(&walk,vehicle);drivableVehicle=true;
        Expect(world.nobodys==1,"team 5's walk leaves aircraft (not NPC-drivable: a parked jet, a rescue helicopter waiting)");
        SeeSquad(human,human,0,npc::Control::free,now);
        auto result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::board,{}},ObjRef::Of(other),{});
        auto* boarding=Entry(human,now);
        Expect(result.Accepted() && boarding && boarding->boardV.Is(vehicle),
            "BOARD takes an empty (team 5) vehicle the friends' walk never lists");
        Reset();Put<void*>(human,kLeader,nullptr);playerObj=other;
        Put<unsigned char>(other,kHumanPlayer,1);Put<void*>(other,kHumanPad,other);Put<int>(other,kTeam,0);
        Put<int>(human,kTeam,0);Put<int>(vehicle,kTeam,kTeamVehicle);
        Put<unsigned>(seats,kSeatClass,1);Put<unsigned>(seats,kSeatEnable,1);
        world.friends=0;IndexFixture();world.nobodys=0;SeeSquad(human,human,0,npc::Control::free,now);
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::board,{}},ObjRef::Of(other),{});
        Expect(result.reason==NpcCommandReason::noVehicle,"without team 5's walk the same empty vehicle was never found (the bug)");
    }
    // A guest pilots its own vehicle while the host owns the NPCs boarding the gunner seats.
    {
        Reset();sessionOn=true;host=true;playerObj=other;
        Put<unsigned char>(other,kHumanPlayer,1);Put<void*>(other,kHumanPad,other);Put<int>(other,kTeam,0);
        dead[kDead]=0;Put<unsigned char>(dead,kHumanPlayer,1);Put<void*>(dead,kHumanPad,dead);Put<int>(dead,kTeam,0);
        Put<unsigned char>(dead,edf::kRiderNet+edf::kNetFlags,1);unsigned char guestCtrl[0x10]{},npcCtrl[0x10]{};
        Put<long>(guestCtrl,8,1);Put<long>(npcCtrl,8,1);Put<void*>(dead,kSelfCtrl,guestCtrl);Put<void*>(human,kSelfCtrl,npcCtrl);
        Put<unsigned char>(vehicle,edf::kRiderNet+edf::kNetFlags,1);Put<int>(vehicle,kTeam,0);
        Put<void*>(dead,kHumanRiding,vehicle);Put<void*>(dead,kHumanVehicleCtrl,ctrl);Put<void*>(dead,kHumanSeat,seats);
        Put<void*>(seats,kSeatRider,dead);Put<void*>(seats,kSeatRiderCtrl,guestCtrl);
        Put<unsigned>(seats,kSeatClass,1);Put<unsigned>(seats,kSeatEnable,1);
        Put<void*>(human,kLeader,dead);SeeSquad(human,human,0,npc::Control::recruited,now);
        Expect(!IsOnlineAuthority(vehicle) && IsOnlineAuthority(human) && OnlineMaySeatNpc(vehicle),
            "real authority policy allows host NPC boarding despite guest vehicle authority");
        auto result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::board,{}},ObjRef::Of(dead),{});
        auto* boarding=Entry(human,now);
        Expect(result.Accepted() && boarding->boardV.Is(vehicle) && boarding->boardSeat==1 && world.friends==0,
            "host uses requester's ridden vehicle and its free passenger slot, not host PlayerHuman or its vehicle list");
        completeRide=true;Board(*boarding,human,Pos(human),now);
        Expect(rides==1 && announcedBoards==1 && At<void*>(seats,kSeatRider)==dead &&
            At<void*>(seats+edf::kSeatStride,kSeatRider)==human,
            "native boarding and announcement put host NPC in guest vehicle without kicking its driver");
    }
    // Actual guard and attack behavior, not merely an accepted boolean or a recorded map event.
    {
        Reset();Put<void*>(human,kLeader,nullptr);playerObj=other;
        Put<unsigned char>(other,kHumanPlayer,1);Put<void*>(other,kHumanPad,other);Put<int>(other,kTeam,0);
        auto* commandSquad=SeeSquad(human,human,0,npc::Control::free,now);flatFloor=true;config.npcEvade=false;
        Put<unsigned>(human,kControlMask,kMaskMove);Arms available;available.n=1;available.arm[0]=npc::Arm{100,0,10,false,true,false};
        auto* soldier=Entry(human,now);soldier->control=npc::Control::free;
        auto result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::guard,{40,0,0}},ObjRef::Of(other),{});
        const float eyePoint[3]={0,1.5f,0};Plan behavior{};
        for(int tick=0;tick<100 && At<float>(human,kMoveX)==0;++tick) {
            now+=16;++frame;world.frame=frame;behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        }
        Expect(result.Accepted() && std::strcmp(behavior.move,"at its post")==0 && At<float>(human,kMoveX)>0,
            "guard command runs production MoveTo/navigation and writes actual movement intent");
        unsigned char enemy[0x300]{},enemyCtrl[0x10]{};Put<void*>(enemy,kSelfCtrl,enemyCtrl);Put<long>(enemyCtrl,8,1);Put<long>(enemyCtrl,12,1);
        world.enemies=1;world.enemy[0]=Enemy{enemy,{0,0,20},1};IndexFixture();++frame;SeeSquad(human,human,0,npc::Control::free,now);
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::engage,{}},ObjRef::Of(other),{});
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(result.Accepted() && soldier->target.Is(enemy) && std::strcmp(behavior.move,"combat spot")==0,
            "engage command enters actual production target acquisition and combat movement");
        // Fire wasted on it (the user 2026-10-12: 「ai还会无脑攻击金色飞船浪费子弹」; wasted_fire.h): no target, its trigger off.
        // The stock AI (with no target of the plugin's the soldier is the stock's) has it as its own target and fires.
        wastedTarget=enemy;Put<unsigned char>(human,kTrigger,1);
        Put<void*>(human,kStockTarget,enemy);Put<void*>(human,kStockTargetCtrl,enemyCtrl);
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(!soldier->target && !behavior.fire && At<unsigned char>(human,kTrigger)==0,
               "an enemy its fire is wasted on is no target, and the stock AI's shot at it is taken off (production Drive)");
        wastedTarget=nullptr;Put<void*>(human,kStockTarget,nullptr);Put<void*>(human,kStockTargetCtrl,nullptr);
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(soldier->target.Is(enemy),"no longer wasted: its target again");
        // Its window due to open (hit_predict.h wait): kept as the target, aimed at its weak spot, the trigger held; then open:
        // the shot, still at the weak spot.
        // The plugin's own aim needs its held weapon known (held[0] -1 leaves the look to the stock AI).
        heldTarget=enemy;available.held[0]=0;
        available.speed[0]=120.0f;available.gravity[0]=7.35f;
        Put<unsigned>(human,kControlMask,kMaskMove|kMaskLook|kMaskTrigger);
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(predictedSpeed==120.0f && predictedGravity==7.35f,"production Drive predicts using the chosen weapon's ballistics");
        const float atLock=At<float>(human,kLookPitch);   // the look's pitch step toward its lock point (0,0,20)
        weakAimSet=true;weakAim[0]=0;weakAim[1]=12;weakAim[2]=20;
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(soldier->target.Is(enemy) && !behavior.fire && At<unsigned char>(human,kTrigger)==0 &&
               At<float>(human,kLookPitch)<atLock,
               "a target due to open: kept, aimed up at its weak spot, the trigger held (production Drive)");
        // Its look already on the weak spot (every other test of the shot passes): held all the same while due to open;
        // released (open), the shot goes.
        const float up[3]={weakAim[0]-eyePoint[0],weakAim[1]-eyePoint[1],weakAim[2]-eyePoint[2]};
        Put<float>(human,kViewPitch,-std::atan2(up[1],std::sqrt(up[0]*up[0]+up[2]*up[2])));Put<float>(human,kViewYaw,std::atan2(up[0],up[2]));
        available.arm[0]=npc::Arm{100,0,10,false,true,false};
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(!behavior.fire && At<unsigned char>(human,kTrigger)==0,"its look on the weak spot, due to open: still no shot");
        heldTarget=nullptr;
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(behavior.fire && At<unsigned char>(human,kTrigger)==1,"open: the shot at the weak spot goes");
        weakAimSet=false;available.held[0]=-1;Put<unsigned char>(human,kTrigger,0);
        Put<float>(human,kViewPitch,0.0f);Put<float>(human,kViewYaw,0.0f);
        Put<unsigned>(human,kControlMask,kMaskMove);
        markEnemy=enemy;
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::focus,{}},ObjRef::Of(other),ObjRef::Of(enemy));
        Expect(result.Accepted() && commandSquad->commandFocus.Is(enemy) && !NpcMarked(),
            "requester's explicit attack target belongs to that squad and does not replace host's global marker");
        Expect(At<long>(enemyCtrl,12)==2,"squad focus pins the borrowed RPC target identity between frames");
        NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::engage,{}},ObjRef::Of(other),{});
        Expect(At<long>(enemyCtrl,12)==1 && !commandSquad->commandFocus,"replacing focus releases exactly its weak identity reference");
        // The player's move over the soldier's own fight (the user, 2026-10-09: "如果npc在打怪，就没办法移动了"): an enemy
        // close by takes a guard's soldier into its dodge / combat spot, never a move order's; an attack-move fights
        // what is in reach and walks on with nothing; the top at its place turns either into a guard of the point.
        config.npcEvade=true;world.enemies=1;world.enemy[0]=Enemy{enemy,{0,0,4},1};IndexFixture();
        auto fights=[](const char* m){return std::strcmp(m,"combat spot")==0 || std::strcmp(m,"back off")==0 ||
                                             std::strcmp(m,"side-step")==0 || std::strcmp(m,"roll")==0;};
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::guard,{40,0,0}},ObjRef::Of(other),{});
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(result.Accepted() && fights(behavior.move),"under a guard order an enemy close by takes the soldier's moves");
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::move,{40,0,0}},ObjRef::Of(other),{});
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(result.Accepted() && std::strcmp(behavior.move,"move order")==0 && soldier->target.Is(enemy),
               "a move order walks on through the fight, still turned on its target");
        Expect(commandSquad->cmd.order==Order::move,"on the way: still a move");
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::attackMove,{40,0,0}},ObjRef::Of(other),{});
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(result.Accepted() && fights(behavior.move),"an attack-move fights the enemy pressing on it");
        // The user, 2026-10-09: "一个移动攻击是全消灭再走。一个移动攻击是先走次要消灭": an attack-move stops for any enemy
        // in its reach and fights it (the move order is the one that walks on firing).
        world.enemy[0]=Enemy{enemy,{0,0,30},1};IndexFixture();
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::attackMove,{40,0,0}},ObjRef::Of(other),{});
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(fights(behavior.move) && soldier->target.Is(enemy),"an attack-move stands and fights an enemy in reach, not only a close one");
        // A target it never gets a shot at (this stand-in soldier never fires: its look is not on it) holds it only for the
        // grace: then it walks on, still on its target, instead of standing there for ever.
        now+=mapcmd::kAttackMoveGraceMs;++frame;world.frame=frame;SeeSquad(human,human,0,npc::Control::free,now);
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(std::strcmp(behavior.move,"attack-move")==0 && soldier->target.Is(enemy),
               "an attack-move with no shot at its target for the grace walks on (not stuck at its combat spot)");
        world.enemies=0;IndexFixture();frame+=2;world.frame=frame;
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::attackMove,{40,0,0}},ObjRef::Of(other),{});
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(std::strcmp(behavior.move,"attack-move")==0,"an attack-move with the ground round it clear walks on to its point");
        const float there[3]={38.0f,0.0f,1.0f};
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,there,commandSquad,now);
        Expect(commandSquad->cmd.order==Order::guard && commandSquad->cmd.at[0]==40.0f,"at its point the attack-move is a guard of it");
        NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::move,{40,0,0}},ObjRef::Of(other),{});
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,there,commandSquad,now);
        Expect(commandSquad->cmd.order==Order::guard,"at its point the move is a guard of it");
        config.npcEvade=false;
        // A squad seated in a vehicle: no recruitment out of it, no map point order for its soldiers.
        Put<void*>(human,kHumanRiding,vehicle);Put<void*>(human,kHumanVehicleCtrl,ctrl);Put<long>(ctrl,8,1);
        const bool seated=!HumanOnFoot(human);
        if(seated) {
            result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::recruit,{}},ObjRef::Of(other),{});
            Expect(result.reason==NpcCommandReason::riding,"a seated squad is not recruited out of its vehicle");
            Expect(!SquadRecruitable(*commandSquad) && std::strcmp(StatusOf(*commandSquad,now,nullptr,0),"RIDING")==0,
                   "a seated squad: no recruitment offered, its status RIDING");
            const int refusedCancels=transportCancels;
            result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::move,{40,0,0}},ObjRef::Of(other),{});
            Expect(!result.Accepted(),"a seated squad's soldiers take no map move (its vehicle does)");
            Expect(transportCancels==refusedCancels,"a refused order leaves its trip as it was (no TransportCancel)");
            // Its transport (transport.cpp; the user, 2026-10-09: "卡车之类的运输载具改成断剑那种操作方式"): a point order the
            // transport takes is accepted and the squad's own order left for the trip to give back once off; WITHDRAW is the
            // transport's (its refusal passed on); any other order leaves the trip.
            transportTakes=true;const Order before=commandSquad->cmd.order;const int asked=transportOrders;
            result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::attackMove,{900,0,0}},ObjRef::Of(other),{});
            Expect(result.Accepted() && transportOrders==asked+1 && commandSquad->cmd.order==before,
                   "a seated paired squad's far order goes by its transport, its own order untouched");
            // WITHDRAW (B, BACK TO BASE) goes to the squad's transport when it has one (with none it is a dismissal).
            const void* pairedWas=transportPaired;transportPaired=vehicle;
            transportTakes=false;transportWithdraw=NpcCommandReason::none;transportWithdrawable=true;
            result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::withdraw,{}},ObjRef::Of(other),{});
            Expect(result.Accepted(),"WITHDRAW answered by its transport");
            transportWithdraw=NpcCommandReason::noTransport;
            result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::withdraw,{}},ObjRef::Of(other),{});
            Expect(result.reason==NpcCommandReason::noTransport,"...and its refusal passed on");
            // Paired with a vehicle no order sends off, not recruited: nothing of its to withdraw (and not dismissed).
            transportWithdrawable=false;transportWithdraw=NpcCommandReason::none;
            result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::withdraw,{}},ObjRef::Of(other),{});
            Expect(result.reason==NpcCommandReason::noTransport && !commandSquad->dismissed,
                   "a free squad paired with a non-support vehicle: WITHDRAW refused as no transport, not sent to it");
            transportWithdraw=NpcCommandReason::noTransport;
            transportPaired=pairedWas;
            const int cancels=transportCancels;
            result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::none,{}},ObjRef::Of(other),{});
            Expect(result.Accepted() && transportCancels==cancels+1,"another order, taken, leaves the trip (TransportCancel)");
        } else Expect(false,"the stand-in ride makes the soldier seated");
        Put<void*>(human,kHumanRiding,nullptr);Put<void*>(human,kHumanVehicleCtrl,nullptr);
        Expect(SquadRecruitable(*commandSquad),"on foot again, a free squad: recruitment offered");
    }
    // A Wing Diver hovering 25 m up held where she is (an attack-move's stand): no search from the floor under her to a
    // goal in the air (it never ends: every node spent, failed, again a second later); the goal is the floor under it.
    Reset();flatFloor=true;
    {
        Put<float>(human,kPosition,0.0f);Put<float>(human,kPosition+4,25.0f);Put<float>(human,kPosition+8,0.0f);
        Put<float>(human,kMoveX,0.5f);
        MoveTo(human,Pos(human),Pos(human),1e9f,now);
        const Soldier* const hovering=Entry(human,now);
        Expect(hovering && !hovering->navigation.initialized && At<float>(human,kMoveX)==0.0f,
               "a soldier held in the air stands: no route search from the floor to a point in the air");
        Put<float>(human,kPosition+4,0.0f);
    }
    // The player a soldier fights for is the one who recruited its squad, whichever machine's (this harness's
    // PlayerHuman is nullptr: `other` stands for another machine's player).
    Reset();Put<float>(other,kPosition,300.0f);Put<float>(other,kPosition+8,40.0f);
    world.player=true;world.playerAt[0]=world.playerAt[1]=world.playerAt[2]=0.0f;
    {
        const Served remote=ServedBy(npc::Control::recruited,other);
        Expect(remote.at==Pos(other) && !remote.look,"a squad another machine's player recruited fights for that player, not this one");
        const Served local=ServedBy(npc::Control::free,nullptr);
        Expect(local.at==world.playerAt,"an unrecruited soldier still takes this machine's player as its reference");
        world.player=false;
        Expect(!ServedBy(npc::Control::free,nullptr).at,"no player known here: no one to fall back behind");
        Put<float>(human,kHumanHpMax,100.0f);Put<float>(human,kHumanHp,10.0f);
        world.enemies=1;world.enemy[0]=Enemy{dead,{300.0f,1.5f,60.0f},1};IndexFixture();
        Soldier hurt{};const float at[3]={290.0f,0.0f,40.0f};
        Expect(FallBack(hurt,human,at,remote,now),"a hurt soldier of a remote player's squad falls back");
        Expect(std::fabs(hurt.fallTo[0]-300.0f)<0.01f && std::fabs(hurt.fallTo[2]-(40.0f-kBehindPlayer))<0.01f,
               "behind that player, on their side away from the threat (their camera is not this machine's)");
    }
    // HOLD (the map's H, RTS hold position): a guard of the spot the squad's top stands on, whatever point came with it.
    {
        Reset();Put<void*>(human,kLeader,nullptr);Squad* const held=SeeSquad(human,human,0,npc::Control::free,now);
        const float spot[3]={12.0f,3.0f,-7.0f};std::memcpy(human+kPosition,spot,12);
        Expect(SquadCommand(human,Command{Order::hold,{900.0f,0.0f,900.0f}}) && held->cmd.order==Order::guard &&
               held->cmd.at[0]==spot[0] && held->cmd.at[2]==spot[2],"HOLD: a guard where its top stands");
    }
    // Only NPCs this machine can drive may reserve a box; the map must not redirect another player's recruits.
    {
        Reset();playerObj=dead;dead[kDead]=0;dead[edf::kHumanPlayer]=1;Put<void*>(dead,edf::kHumanPad,dead);
        Put<void*>(dead,kFollowers,nullptr);Put<void*>(human,kLeader,dead);
        Put<void*>(human,kFollowers,head);Put<void*>(head,0,node);Put<void*>(node,0,head);Put<void*>(node,0x10,other);
        Put<void*>(other,kLeader,human);Put<std::uint16_t>(other,0x128,1);sessionOn=true;
        SeeSquad(human,human,0,npc::Control::recruited,now);
        const void* roster[8]{};
        Expect(SweepRoster(now,roster,8)==1 && roster[0]==human,"remote-owned followers cannot reserve boxes they will never walk to");
        sweep.tops=1;sweep.top[0]=ObjRef::Of(human);
        playerObj=nullptr;
        Expect(SweepRoster(now,roster,8)==0,"a selected squad recruited by another player is not redirected to this player's sweep online");
        playerObj=dead;Put<void*>(human,kRoute,human);
        Expect(SweepRoster(now,roster,8)==0,"a newly scripted selected squad leaves the sweep before the cached squad control updates");
        playerObj=nullptr;
    }
    // The box sweep's taking (PickUp): weapon and armour the stock way for the player, health for the hurt soldier.
    {
        static unsigned char me[0x2200]{};
        Reset();playerObj=me;Put<void*>(me,0x340,me);   // the player's pad object: Collect's gate
        Boxes(npc::pickup::kWeapon,npc::pickup::kArmour);
        Expect(StaleSweepLeavesLockFree(),"a stale selected object cannot leave the sweep HUD lock held after SEH");
        Soldier fetch{};fetch.pickUnit=boxUnit[0];std::memcpy(fetch.pickPos,boxModel[0],12);fetch.pickKind=npc::pickup::kWeapon;
        const float away[3]={20.0f,2.0f,30.0f},there[3]={50.4f,2.0f,30.0f};
        Expect(PickUp(fetch,human,away,now) && collects==0,"a soldier far from its box runs to it, nothing taken yet");
        const float downstairs[3]={50.0f,-8.0f,30.0f};
        Expect(PickUp(fetch,human,downstairs,now) && collects==0 && !boxUnit[0][kBoxTaken],
               "a soldier directly below a box cannot collect it through another floor");
        sweep.on=false;
        Expect(!PickUp(fetch,human,there,now) && collects==0,"recalling a sweep invalidates its already assigned pickup in the same frame");
        sweep.on=true;
        Expect(PickUp(fetch,human,there,now) && collects==1 && collectBy==me && notifies==1 && notifyBy==me,
               "at its box: native Notify and Apply once, credited to the local player");
        Expect(notifyId==42 && notifyKind==npc::pickup::kWeapon && applyKind==notifyKind && notifyBeforeApply && untakenAtApply,
               "native Collect ordering and exact box identity: Notify, Apply, then mark taken");
        Expect(boxUnit[0][kBoxTaken]==1 && boxUnit[1][kBoxTaken]==0 && !fetch.pickUnit,"only that box taken, the one a metre off left");
        Expect(!PickUp(fetch,human,there,now),"no box any more: the soldier's other moves");
        fetch.pickUnit=boxUnit[0];
        Expect(!PickUp(fetch,human,there,now) && collects==1,"a box someone else took: nothing called");
        Boxes(npc::pickup::kWeapon,npc::pickup::kHealSmall);
        std::memcpy(boxModel[1],boxModel[0],16);fetch.pickUnit=boxUnit[0];sessionOn=true;
        Expect(PickUp(fetch,human,there,now) && collects==1 && notifies==1 && boxUnit[0][kBoxTaken] && !boxUnit[1][kBoxTaken] && heals==0,
               "online overlapping boxes: only the selected weapon is taken, the health box is never consumed");
        sessionOn=false;
        // Health boxes: the hurt soldier itself, offline, allowed.
        Boxes(npc::pickup::kHealBig,npc::pickup::kWeapon);
        config.npcPickupHealth=true;Put<float>(human,kHumanHpMax,1000.0f);Put<float>(human,kHumanHp,400.0f);
        fetch=Soldier{};fetch.pickUnit=boxUnit[0];std::memcpy(fetch.pickPos,boxModel[0],12);fetch.pickKind=npc::pickup::kHealBig;
        Expect(PickUp(fetch,human,there,now) && heals==1 && healed==human && std::fabs(healAmount-300.0f)<0.01f && collects==0 &&
               boxUnit[0][kBoxTaken]==1 && boxUnit[1][kBoxTaken]==0,
               "a hurt soldier at a big health box: healed 30% of its full health itself, not the player's Collect");
        Boxes(npc::pickup::kHealSmall,npc::pickup::kWeapon);
        Put<float>(human,kHumanHp,1000.0f);fetch.pickUnit=boxUnit[0];fetch.pickKind=npc::pickup::kHealSmall;
        Expect(PickUp(fetch,human,there,now) && heals==0 && boxUnit[0][kBoxTaken]==0,"healed to full meanwhile: the box left for the player");
        Put<float>(human,kHumanHp,400.0f);fetch.pickUnit=boxUnit[0];sessionOn=true;
        Expect(PickUp(fetch,human,there,now) && heals==0 && boxUnit[0][kBoxTaken]==0,"online: no health box (the plugin's heal is not synced)");
        sessionOn=false;config.npcPickupHealth=false;fetch.pickUnit=boxUnit[0];
        Expect(PickUp(fetch,human,there,now) && heals==0 && boxUnit[0][kBoxTaken]==0,"health boxes switched off: left alone");
        playerObj=nullptr;
    }
    // The mark (§6.3) is kept until its enemy dies or is gone (the user, 2026-10-07: "标记还很快消失", "标记效果应该先打死
    // 才换吧"), not only while its lock point is lockable this frame.
    {
        Reset();
        unsigned char foeCtrl[0x10]{};
        Put<void*>(other,kSelfCtrl,foeCtrl);Put<long>(foeCtrl,8,1);Put<long>(foeCtrl,0xC,1);Put<int>(other,kTeam,1);
        const float seen[3]={10.0f,1.0f,10.0f};
        Expect(NpcMarkEnemy(other,seen,true) && NpcMarked(),"the map marks an enemy");
        Expect(At<long>(foeCtrl,0xC)==2,"a long-lived mark pins its identity token against control-block reuse");
        lockOf=other;lockAt[0]=12.0f;lockAt[1]=1.0f;lockAt[2]=14.0f;   // its lock point in the registry
        KeepMark();
        Expect(NpcMarked() && mark.at[0]==12.0f && mark.at[2]==14.0f,"a lockable marked enemy: kept, followed");
        lockOf=other;lockAt[0]=30.0f;lockAt[1]=2.0f;lockAt[2]=-5.0f;
        KeepMark();
        Expect(NpcMarked(),"the marked enemy moved (not lockable now, still in the registry): the mark kept");
        Expect(mark.at[0]==30.0f && mark.at[2]==-5.0f,"...where its lock point still is");
        lockOf=nullptr;
        KeepMark();KeepMark();
        Expect(NpcMarked() && mark.at[0]==30.0f,"no lock point at all: kept where it was last seen");
        other[kDead]=1;
        Expect(!NpcMarked(),"death is rejected even before the player's next mark frame");
        KeepMark();
        Expect(!NpcMarked(),"the marked enemy dead: the mark let go");
        Expect(At<long>(foeCtrl,0xC)==1,"death releases the mark's weak reference");
        other[kDead]=0;
        Expect(NpcMarkEnemy(other,seen,true),"marked again");
        unsigned char otherCtrl[0x10]{};
        Put<void*>(other,kSelfCtrl,otherCtrl);
        KeepMark();
        Expect(!NpcMarked(),"a new object at the marked one's address: the mark let go");
        Put<void*>(other,kSelfCtrl,foeCtrl);
        Expect(NpcMarkEnemy(other,seen,true),"marked again");
        other[npcmark::kFlags]|=npcmark::kDeleted;KeepMark();
        Expect(!NpcMarked(),"the marked enemy removed without dying (a despawn): the mark let go");
        other[npcmark::kFlags]=0;
        Expect(NpcMarkEnemy(other,seen,true),"marked again");
        Put<long>(foeCtrl,8,0);KeepMark();
        Expect(!NpcMarked(),"the marked enemy's last strong reference gone: the mark let go");
        Put<long>(foeCtrl,8,1);
        Expect(NpcMarkEnemy(other,seen,true) && !NpcMarkEnemy(other,seen,true) && !NpcMarked(),"the same enemy again: let go");
        Expect(NpcMarkEnemy(other,seen,true) && NpcMarkEnemy(other,seen,false) && NpcMarked(),"the focus order's mark never lets go");
        // The Q mark replaces the stock spot (2026-10-10): it marks and shows without the custom AI; the NPCs' focus on it
        // (NpcMarked: the map's focus order, the soldiers' priority) is the AI's alone.
        config.customNpcAi=false;KeepMark();
        float shown[3];
        Expect(!NpcMarked() && NpcMarkReadout(shown),"disabled AI: the mark still shown, the NPCs not set on it");
        Expect(!NpcMarkEnemy(other,seen,true) && NpcMarkEnemy(other,seen,true) && !NpcMarked() && NpcMarkReadout(shown),
               "disabled AI: the map's Q lets go and marks again");
        config.npcMarkKey=0;KeepMark();
        Expect(!NpcMarkEnemy(other,seen,true) && !NpcMarkReadout(shown),"NpcMarkKey=0: no Q mark at all");
        config.npcMarkKey=0x51;
        config.customNpcAi=true;KeepMark();
        Expect(NpcMarked() && NpcMarkReadout(shown),"reenabling AI restores a still-live saved mark");
        ResetNpcAi();
        Expect(!NpcMarkReadout(shown),"mission reset immediately drops the old published mark");
        Expect(At<long>(foeCtrl,0xC)==1,"mission reset releases the saved mark's control block");
        void* controlVtable[2]={nullptr,reinterpret_cast<void*>(&DeleteWeakRecord)};
        Put<void*>(foeCtrl,0,controlVtable);
        Expect(NpcMarkEnemy(other,seen,false),"mark before the engine releases its final weak reference");
        Put<long>(foeCtrl,8,0);InterlockedDecrement(reinterpret_cast<volatile LONG*>(foeCtrl+0xC));
        Expect(At<long>(foeCtrl,0xC)==1 && !weakDeletes,"destroyed enemy's identity remains allocated while marked");
        KeepMark();
        Expect(At<long>(foeCtrl,0xC)==0 && weakDeletes==1 && !NpcMarked(),"last owned weak calls the native control deleter exactly once");
        Put<void*>(other,kSelfCtrl,nullptr);
    }
    // T/Y, like Q, must not fire a second time when a held map key returns to on-foot control.
    {
        Reset();playerObj=human;mapHeld=true;markinput::down=true;
        march.shape=npc::formation::Shape::wedge;march.shapeSet=true;march.held=false;
        boxesOk=false;sweep.logged=false;
        FormationTick();SweepFrame();
        mapHeld=false;FormationTick();SweepFrame();
        Expect(march.shape==npc::formation::Shape::wedge && !sweep.logged,
               "held T/Y from the map cannot change formation or start a second sweep on close");
        markinput::down=false;FormationTick();SweepFrame();
        markinput::down=true;FormationTick();SweepFrame();
        Expect(march.shape==npc::formation::Shape::vee && sweep.logged,"releasing then pressing T/Y starts a fresh on-foot action");
        markinput::down=false;playerObj=nullptr;boxesOk=true;
    }
    // Player-frame edges work without a soldier Think, but never leak out of the map/TV or a held close key.
    {
        Reset();mapHeld=false;rayOn=true;
        unsigned char foeCtrl[0x10]{};Put<void*>(other,kSelfCtrl,foeCtrl);Put<long>(foeCtrl,8,1);Put<long>(foeCtrl,0xC,1);
        markEnemy=other;markinput::down=true;
        NpcMarkFrame(human,true);
        Expect(!NpcMarked(),"a map press does not also mark from the player's camera");
        NpcMarkFrame(human,false);
        Expect(!NpcMarked(),"closing the map with Q held does not create a second press");
        markinput::down=false;NpcMarkFrame(human,false);
        Expect(!NpcMarked(),"...nor its release a tap");
        mapHeld=true;markinput::down=true;NpcMarkFrame(human,false);
        Expect(!NpcMarked(),"TV or closing-map input hold blocks on-foot marking");
        markinput::down=false;NpcMarkFrame(human,false);mapHeld=false;
        Expect(!NpcMarked(),"...released under it: no tap");
        cueOwn=cueOff=pointMarks=qmarkFrames=0;
        // A tap acts on its release (the key held is the clear's: qmark_ray.h StepMarkKey).
        markinput::down=true;NpcMarkFrame(human,false);
        Expect(!NpcMarked() && cueOwn==0,"the press itself: nothing yet (a hold would clear)");
        now+=100;markinput::down=false;NpcMarkFrame(human,false);
        Expect(NpcMarked(),"a fresh player-frame tap marks on its release without any NPC Think");
        Expect(cueOwn==1 && cueOff==0 && qmarkFrames==2,"marking asks for the own cue; the Q mark's frame runs from the player's");
        markinput::down=true;NpcMarkFrame(human,false);markinput::down=false;NpcMarkFrame(human,false);
        Expect(!NpcMarked() && cueOff==1,"the same enemy again: let go, the off cue");
        Put<void*>(human,kHumanVehicleCtrl,foeCtrl);   // riding (the stock spot it replaces was riding too)
        markinput::down=true;NpcMarkFrame(human,false);markinput::down=false;NpcMarkFrame(human,false);
        Expect(NpcMarked() && cueOwn==2,"riding: Q marks as on foot");
        Put<void*>(human,kHumanVehicleCtrl,nullptr);
        markEnemy=nullptr;
        markinput::down=true;NpcMarkFrame(human,false);markinput::down=false;NpcMarkFrame(human,false);
        Expect(pointOrders==1 && NpcMarked(),"a ground miss orders selected units without clearing the existing mark");
        Expect(pointMarks==1 && cueOwn==3,"a ground miss marks the point (shared, shown) with the own cue");
        // Held kMarkClearMs (the user, 2026-10-31: "长按q应该能取消标记"): the enemy mark and the point cleared while
        // still held, the off cue (the teammates' let-go), the ring where it was; the release nothing; the orders stay.
        markEnemy=other;
        markinput::down=true;NpcMarkFrame(human,false);
        now+=qmark::kMarkClearMs-1;NpcMarkFrame(human,false);
        Expect(NpcMarked() && cueOff==1,"held just short of the clear: the mark stays");
        now+=1;NpcMarkFrame(human,false);
        Expect(!NpcMarked() && !pointShown && cueOff==2 && cueOwn==3,"held kMarkClearMs: the enemy mark and the point cleared, one off cue");
        {
            NpcPing p{};
            Expect(NpcPingReadout(&p) && p.given==kPingCleared,"...the cleared ring for the HUD");
        }
        now+=500;NpcMarkFrame(human,false);markinput::down=false;NpcMarkFrame(human,false);
        Expect(!NpcMarked() && cueOwn==3 && cueOff==2 && pointMarks==1 && pointOrders==1,
               "the release after the clear: no tap (nothing marked, no point, no order)");
        // Nothing marked: held does nothing (no cue).
        markinput::down=true;NpcMarkFrame(human,false);now+=qmark::kMarkClearMs;NpcMarkFrame(human,false);
        markinput::down=false;NpcMarkFrame(human,false);
        Expect(!NpcMarked() && cueOff==2 && cueOwn==3,"held with nothing marked: nothing, no cue");
        // Let go on the way to the clear: a tap (on the same enemy as ever).
        markinput::down=true;NpcMarkFrame(human,false);now+=qmark::kMarkClearMs-50;NpcMarkFrame(human,false);
        markinput::down=false;NpcMarkFrame(human,false);
        Expect(NpcMarked() && cueOwn==4,"released before the clear: a tap marks");
        // The map opened mid-press: neither a clear nor a tap.
        markinput::down=true;NpcMarkFrame(human,false);NpcMarkFrame(human,true);now+=qmark::kMarkClearMs;
        NpcMarkFrame(human,false);markinput::down=false;NpcMarkFrame(human,false);
        Expect(NpcMarked() && cueOff==2,"the map opened mid-press: dropped (the mark kept, no let-go)");
        config.enabled=false;
        markinput::down=true;NpcMarkFrame(human,false);markinput::down=false;NpcMarkFrame(human,false);
        Expect(pointOrders==1 && !NpcMarked(),"global disable prevents ground orders and hides the mark");
        ResetNpcAi();Expect(At<long>(foeCtrl,0xC)==1,"player-frame mark releases its identity on reset");
    }
    // In a turret seat of EDF6AutoTurret's the mark picks by the lock's rule (2026-10-25: "载具上按q……提示对准敌人才能。但是我
    // 明明对准了"), auto-aim on or off: never the mark's own pick (markEnemy, 8 deg, no line of sight) nor its near-miss word.
    {
        Reset();mapHeld=false;rayOn=true;config.npcMarkKey=0x51;
        unsigned char foeCtrl[0x10]{};Put<void*>(other,kSelfCtrl,foeCtrl);Put<long>(foeCtrl,8,1);Put<long>(foeCtrl,0xC,1);
        Put<void*>(human,kHumanVehicleCtrl,foeCtrl);
        auto press=[&]{markinput::down=true;NpcMarkFrame(human,false);markinput::down=false;NpcMarkFrame(human,false);};
        // Auto-aim off: the lock rule sees nobody (the enemy under the old 8 deg pick is behind a wall): the ground point.
        markEnemy=other;turretSeat=true;turretAuto=false;turretPick=nullptr;cueOwn=cueOff=pointMarks=pointOrders=0;
        press();
        Expect(!NpcMarked() && pointMarks==1 && pointOrders==1,"turret seat, auto-aim off, none in the lock's sight: the ground point and squads, not the old pick");
        // ...its pick marked, the same again let go (as on foot).
        turretPick=other;press();
        Expect(NpcMarked() && cueOwn==2,"auto-aim off: Q marks the lock rule's pick");
        press();
        Expect(!NpcMarked() && cueOff==1,"the same pick again: let go");
        // Auto-aim on, keyboard: the press is the lock's; the mark follows the lock, once.
        turretAuto=true;turretLockTarget=nullptr;cueOwn=cueOff=0;
        press();
        Expect(!NpcMarked() && cueOwn==0,"auto-aim on: the Q press is the lock's, no pick of the mark's");
        turretLockTarget=other;NpcMarkFrame(human,false);
        Expect(NpcMarked() && cueOwn==1,"the lock took an enemy: the mark follows it, the own cue once");
        NpcMarkFrame(human,false);
        Expect(NpcMarked() && cueOwn==1,"the same lock held: nothing more");
        turretLockTarget=nullptr;NpcMarkFrame(human,false);
        Expect(NpcMarked(),"the lock let go: the mark stays (until that enemy dies)");
        // On a pad the lock is X: the keyboard's Q stays the mark's, by the same pick.
        turretKeys=false;press();
        Expect(!NpcMarked() && cueOff==1,"a pad's player: the keyboard's Q is the mark's (the pick again: let go)");
        turretKeys=true;turretAuto=false;turretSeat=false;turretPick=nullptr;
        Put<void*>(human,kHumanVehicleCtrl,nullptr);markEnemy=nullptr;
        ResetNpcAi();
    }
    // The bridge's strafe Q and an aircraft's target Q (plan-2026-10-25-keybinds.md §3.3 B / C).
    {
        Reset();mapHeld=false;rayOn=true;config.enabled=true;config.npcMarkKey=0x51;config.playerJetTargetKey=0x51;
        unsigned char foeCtrl[0x10]{};Put<void*>(other,kSelfCtrl,foeCtrl);Put<long>(foeCtrl,8,1);Put<long>(foeCtrl,0xC,1);
        auto press=[&]{markinput::down=true;NpcMarkFrame(human,false);markinput::down=false;NpcMarkFrame(human,false);};
        markEnemy=other;bridgeHolds=true;cueOwn=cueOff=pointMarks=pointOrders=0;
        press();
        Expect(!NpcMarked() && pointMarks==0 && pointOrders==0,"the Dreadnought's bridge holds Q (strafe): no mark, no point, no order");
        bridgeHolds=false;jetStick=true;jetLocked=nullptr;markEnemy=nullptr;
        press();
        Expect(!NpcMarked() && pointMarks==0 && pointOrders==0,"at an aircraft's stick with no lock: no ground point, no order");
        jetLocked=other;NpcMarkFrame(human,false);
        Expect(NpcMarked() && cueOwn==1,"...the lock the target key's cycle brings: marked, once");
        press();
        for(int f=0;f<3;++f)NpcMarkFrame(human,false);
        Expect(NpcMarked() && cueOff==0,"...pressed again on the same lock: never let go");
        jetStick=false;jetLocked=nullptr;
        ResetNpcAi();
    }
    // The teammates' marks are the NPCs' as this machine's are (the user, 2026-10-10: "npc不是统一的吗，都去打"): each NPC
    // takes the marked enemy nearest it, whoever marked it (NearestMark, the soldiers' and the gunners' pick).
    {
        Reset();config.enabled=true;config.customNpcAi=true;
        unsigned char foeCtrl[0x10]{};Put<void*>(other,kSelfCtrl,foeCtrl);Put<long>(foeCtrl,8,1);Put<long>(foeCtrl,0xC,1);
        static unsigned char teamFoe[0x400]{},plainFoe[0x400]{};
        world.enemies=3;IndexFixture();
        world.enemy[0]=Enemy{plainFoe,{0,0,5},1};IndexFixture();
        world.enemy[1]=Enemy{other,{0,0,100},1};IndexFixture();
        world.enemy[2]=Enemy{teamFoe,{0,0,20},1};IndexFixture();
        const float from[3]={0,0,0},distant[3]={0,0,150};
        Expect(!NearestMark(from),"nothing marked: no marked enemy, the nearest plain one not taken for one");
        teamMarks[0]=teamFoe;teamMarkCount=1;
        Expect(NearestMark(from)==&world.enemy[2],"a teammate's mark alone: the NPCs go for it as for their own");
        const float seen[3]={0,0,100};
        Expect(NpcMarkEnemy(other,seen,true),"this machine marks another enemy too");
        Expect(NearestMark(from)==&world.enemy[2] && NearestMark(distant)==&world.enemy[1],
               "two marks: each NPC takes the one nearest it, whoever marked it");
        world.enemy[2].aim[2]=100.0f;IndexFixture();
        Expect(NearestMark(from)==&world.enemy[1],"as near as each other: this machine's first");
        teamMarks[0]=plainFoe;world.enemy[2].aim[2]=20.0f;IndexFixture();
        Expect(NearestMark(from)==&world.enemy[0],"the teammate's mark moved to another enemy: that one");
        teamMarks[0]=dead;
        Expect(NearestMark(from)==&world.enemy[1],"a teammate's enemy not in this frame's list: not taken");
        teamMarkCount=0;ResetNpcAi();
        Put<void*>(other,kSelfCtrl,nullptr);
    }
    // The backpack (docs/npc-backpack-re.md; the user, 2026-10-18: 「让npc可以使用背包位武器如突击的手雷，飞翼的短剑等」):
    // production ArmsOf leaves the sub slot's weapon out of the arms it picks, fires and fights at; production Drive throws
    // it at a group in its reach once its look is on the arc, through NpcPackUse, then holds the look, the main trigger and
    // the picks while the throw goes on; it rests between throws; never at a lone small one, never with a friend where it
    // lands, never with NpcBackpack=0; empty, its reload is asked for once.
    {
        Reset();Put<void*>(human,kLeader,nullptr);playerObj=other;config.enabled=true;config.customNpcAi=true;
        Put<unsigned char>(other,kHumanPlayer,1);Put<void*>(other,kHumanPad,other);Put<int>(other,kTeam,0);
        auto* squad=SeeSquad(human,human,0,npc::Control::free,now);config.npcEvade=false;config.npcBackpack=true;
        Put<unsigned>(human,kControlMask,kMaskMove|kMaskLook|kMaskTrigger);
        static unsigned char grenade[0xC00]{},rifle[0xC00]{};
        Put<float>(grenade,kArmReach,360.0f);Put<float>(grenade,kArmBlast,5.0f);Put<float>(grenade,kWeaponSpeed,0.6f);
        Put<float>(grenade,kWeaponGravity,1.0f);Put<std::int32_t>(grenade,kWeaponAmmo,3);
        Put<float>(rifle,kArmReach,150.0f);Put<float>(rifle,kArmDamage,10.0f);Put<std::int32_t>(rifle,kWeaponAmmo,30);
        unsigned char* list[2]={rifle,grenade};
        Put<unsigned char**>(human,kWeapons,list);Put<std::uint64_t>(human,kWeaponCount,2);
        packReady=true;packSlots=1;packSlot=NpcPackSlot{grenade,PackOf(1),0,1};packUses=packReloads=0;packBusy=false;packCanUse=true;
        ArmsSeen packSeen{};Arms packArms=ArmsOf(human,packSeen);
        Expect(packArms.n==2 && packArms.arm[0].ready && !packArms.arm[1].ready && packArms.arm[1].reach==0.0f && LongestReach(packArms)==150.0f &&
               LargestBlast(packArms)==0.0f,"the backpack's grenade is out of the arms it picks, fires and fights at (production ArmsOf)");
        packArms.held[0]=0;
        auto* soldier=Entry(human,now);soldier->control=npc::Control::free;
        static unsigned char foe[3][0x300]{},foeCtrl[3][0x10]{};
        for(int i=0;i<3;++i){Put<void*>(foe[i],kSelfCtrl,foeCtrl[i]);Put<long>(foeCtrl[i],8,1);Put<long>(foeCtrl[i],12,1);}
        world.enemies=3;world.enemy[0]=Enemy{foe[0],{-2,0,24},1};world.enemy[1]=Enemy{foe[1],{0,0,25},1};world.enemy[2]=Enemy{foe[2],{2,0,26},1};IndexFixture();
        const float eyePoint[3]={0,1.5f,0};
        // The look the throw takes at the nearest (-2, 0, 24): the low arc at 36 m/s under 14.7 m/s^2, less the MG11's lift.
        const float to[3]={-2,0,24};float pitch=0.0f,flight=0.0f,dir[3];
        npcpack::LobPitch(npc::Horiz(eyePoint,to),to[1]-eyePoint[1],36.0f,14.7f,&pitch,&flight);
        npcpack::ThrowLook(eyePoint,to,pitch,0.125f,dir);
        const auto onIt=[&]{
            Put<float>(human,kViewPitch,-std::atan2(dir[1],std::sqrt(dir[0]*dir[0]+dir[2]*dir[2])));Put<float>(human,kViewYaw,std::atan2(dir[0],dir[2]));
        };
        const auto drive=[&]{now+=16;++frame;world.frame=frame;return Drive(*soldier,human,kSoldiers[0],packArms,nullptr,eyePoint,Pos(human),squad,now);};
        // A ready round is not sufficient: the native state / sub-slot mask may forbid its use. Such a backpack must
        // not continually take the main weapon's look and trigger, even though it is a good target for the grenade.
        packCanUse=false;world.enemy[0].aim[0]=0.0f;IndexFixture();
        Put<float>(human,kViewPitch,std::atan2(eyePoint[1],24.0f));Put<float>(human,kViewYaw,0.0f);
        drive();
        Expect(packUses==0 && At<unsigned char>(human,kTrigger)==1 && std::fabs(At<float>(human,kLookPitch))<0.2f,
               "native backpack gate closed: the main rifle keeps aiming and firing (production Drive)");
        const float beforePitch=At<float>(human,kLookPitch),beforeYaw=At<float>(human,kLookYaw);
        human[kTrigger]=0;
        drive();
        Expect(packUses==0 && At<unsigned char>(human,kTrigger)==1 && At<float>(human,kLookPitch)==beforePitch && At<float>(human,kLookYaw)==beforeYaw,
               "a closed backpack gate does not starve the main rifle on subsequent frames");
        world.enemy[0].aim[0]=-2.0f;IndexFixture();packCanUse=true;
        Put<float>(human,kViewPitch,0.0f);Put<float>(human,kViewYaw,0.0f);
        arcWallZ=12.0f;Put<float>(human,kLookPitch,123.0f);human[kTrigger]=1;
        Backpack(*soldier,human,&world.enemy[0],eyePoint,kMaskLook,now);
        Expect(!packUses && human[kTrigger]==1 && At<float>(human,kLookPitch)==123.0f,
               "a wall beyond six metres forbids the grenade before taking the main weapon's aim / trigger (production Backpack)");
        arcWallZ=0.0f;
        const float mid=flight*0.5f;
        world.friends=1;IndexFixture();world.frObject[0]=other;
        world.fr[0]=npc::Friend{{-1.0f,eyePoint[1]+36.0f*std::sin(pitch)*mid-0.5f*14.7f*mid*mid,12.0f},0.8f};IndexFixture();
        Backpack(*soldier,human,&world.enemy[0],eyePoint,kMaskLook,now);
        Expect(!packUses && human[kTrigger]==1 && At<float>(human,kLookPitch)==123.0f,
               "a friend midway along the actual lifted arc forbids the grenade before taking aim (production Backpack)");
        world.friends=0;IndexFixture();
        drive();
        float expect[2]{};npc::AimDelta(0.0f,0.0f,dir,1.0f,kLookMost,expect);
        Expect(packUses==0 && At<unsigned char>(human,kTrigger)==0 && std::fabs(At<float>(human,kLookPitch)-expect[0])<1e-5f,
               "a group at 24 m: the look turned onto the throw's arc first (its lift taken off), the main trigger off meanwhile");
        onIt();drive();
        Expect(packUses==1 && packUseSlot==0 && soldier->packHoldUntil>frame,"its look on the arc: the throw (NpcPackUse, slot 0)");
        packBusy=true;Put<unsigned char>(human,kTrigger,1);Put<unsigned char>(human,kPickWeapon+1,1);drive();
        Expect(packUses==1 && At<unsigned char>(human,kTrigger)==0 && At<unsigned char>(human,kPickWeapon+1)==0,
               "the throw going on: no other, the main trigger and the picks off");
        frame=soldier->packHoldUntil+1;Put<float>(human,kLookPitch,999.0f);drive();
        Expect(packUses==1 && std::fabs(At<float>(human,kLookPitch)-expect[0])<1e-5f,
               "an animation still going after 45 frames keeps the actual throw direction");
        packBusy=false;frame=soldier->packHoldUntil;onIt();drive();
        Expect(packUses==1,"done, but within kThrowGapMs of the last: it rests");
        now+=npcpack::kThrowGapMs;onIt();drive();
        Expect(packUses==2,"rested: the next throw");
        now+=npcpack::kThrowGapMs;frame+=npcpack::kHoldFrames;world.enemies=1;IndexFixture();onIt();drive();
        Expect(packUses==2,"one small enemy: no grenade for it");
        world.enemy[0].points=3;onIt();drive();
        Expect(packUses==3,"one large one: the grenade");
        now+=npcpack::kThrowGapMs;frame+=npcpack::kHoldFrames;world.enemies=3;IndexFixture();world.enemy[0].points=1;
        world.friends=1;world.frObject[0]=other;world.fr[0]=npc::Friend{{-1,0,21},0.8f};IndexFixture();onIt();drive();
        Expect(packUses==3,"a friend where it lands: held");
        world.friends=0;IndexFixture();config.npcBackpack=false;onIt();drive();
        Expect(packUses==3,"NpcBackpack=0: carried, never used");
        config.npcBackpack=true;Put<std::int32_t>(grenade,kWeaponAmmo,0);onIt();drive();drive();
        Expect(packUses==3 && packReloads==1,"empty: no throw, its reload asked for once");
        Put<std::int32_t>(grenade,kWeaponAmmo,3);onIt();drive();
        Expect(packUses==4,"loaded again: thrown");
        packReady=false;packSlots=0;Put<unsigned char**>(human,kWeapons,nullptr);Put<std::uint64_t>(human,kWeaponCount,0);
        world.enemies=0;IndexFixture();ResetNpcAi();
    }
    // The stock's join mark and free-squad cover (the user 2026-10-19: "删除npc顶上的未招募红标" / "删除玩家招募npc以后npc
    // 的减伤消失问题", then "暂时取消这个掩护设定吧"): the mark hidden while the map recruits; the cover nobody's by
    // default (NpcFreeSquadCover=0), read from the squad's top NPC, not the player, with NpcFreeSquadCover=1.
    {
        Reset();dead[kDead]=0;
        Put<unsigned char>(other,kHumanPlayer,1);Put<void*>(other,kHumanPad,other);Put<int>(other,kTeam,0);
        Put<void*>(human,kLeader,dead);Put<void*>(dead,kLeader,other);
        Expect(!config.npcFreeSquadCover,"NpcFreeSquadCover defaults to 0 (no squad has the cover)");
        const unsigned char* const uncovered=CoverRoot(human);
        Expect(uncovered==kNoCover && !uncovered[kAutoFollow] && !uncovered[0x1A],"NpcFreeSquadCover=0, recruited: the read sees no +0x540, no AI-list bit");
        Put<void*>(dead,kLeader,nullptr);dead[kAutoFollow]=1;
        Expect(CoverRoot(human)==kNoCover && CoverRoot(dead)==kNoCover,"NpcFreeSquadCover=0, a free +0x540 squad: no cover either");
        dead[kAutoFollow]=0;Put<void*>(dead,kLeader,other);
        config.npcFreeSquadCover=true;
        Expect(CoverRoot(human)==dead,"a recruited squad's member: its cover read from the top NPC, not from the player it follows");
        Expect(CoverRoot(dead)==dead,"a recruited squad's top: its own +0x540, as before it joined");
        Put<void*>(dead,kLeader,nullptr);
        Expect(CoverRoot(human)==dead,"a free squad: the end of the chain, exactly the stock walk");
        Put<void*>(human,kLeader,nullptr);
        Expect(CoverRoot(human)==human,"no leader: itself");
        Put<void*>(human,kLeader,dead);Put<void*>(dead,kLeader,other);config.customNpcAi=false;
        Expect(CoverRoot(human)==other,"the AI off: the stock walk up to the player (the cover ends on joining, stock)");
        config.npcFreeSquadCover=false;
        Expect(CoverRoot(human)==other,"the AI off, NpcFreeSquadCover=0: still the stock walk");
        config.npcFreeSquadCover=true;
        config.customNpcAi=true;Put<void*>(dead,kLeader,human);
        const unsigned char* const looped=CoverRoot(human);
        Expect(looped==human || looped==dead,"a leader loop ends (the walk is bounded)");
        Put<void*>(dead,kLeader,other);
        static int marks=0;marks=0;
        struct MarkRec { static void __fastcall Run(void*,const unsigned char*) { ++marks; } };
        nextJoinMark=&MarkRec::Run;
        JoinMarkHook(nullptr,human);
        Expect(marks==0,"VanillaRecruit=0: no join mark over a free soldier (the map's RECRUIT is the way in)");
        config.vanillaRecruit=true;JoinMarkHook(nullptr,human);
        Expect(marks==1,"VanillaRecruit=1: the stock walk-up's mark stays");
        config.vanillaRecruit=false;config.customNpcAi=false;JoinMarkHook(nullptr,human);
        Expect(marks==2,"the AI off: the stock walk-up and its mark stay");
        config.customNpcAi=true;
        // Installed on the image stand-in: the vtable slot swapped, the walk replaced by a call that the stock read after it
        // still follows, then executed with rbx = the soldier and rsp as in 0x59ABB0's body.
        std::memcpy(image+kJoinMarkVisit,kJoinMarkSig,sizeof(kJoinMarkSig));
        Put<void*>(image,kJoinMarkSlot,image+kJoinMarkVisit);
        std::memcpy(image+kCoverWalk,kCoverWalkSig,sizeof(kCoverWalkSig));
        std::memcpy(image+kCoverWalk+sizeof(kCoverWalkSig),kCoverReadSig,sizeof(kCoverReadSig));
        nextJoinMark=nullptr;coverOk=false;
        InstallFreeSquadCover();
        Expect(At<void*>(image,kJoinMarkSlot)==reinterpret_cast<void*>(&JoinMarkHook) &&
               reinterpret_cast<void*>(nextJoinMark)==image+kJoinMarkVisit,"the join mark's slot chains onto the stock visit");
        const unsigned char* const at=image+kCoverWalk;
        std::int32_t rel=0;std::memcpy(&rel,at+4,4);
        const unsigned char* const thunk=at+8+rel;
        const void* jumpTo=nullptr;std::memcpy(&jumpTo,thunk+2,8);
        Expect(coverOk && at[0]==0x48 && at[1]==0x8B && at[2]==0xCB && at[3]==0xE8 && at[8]==0x48 && at[9]==0x8B && at[10]==0xC8,
               "the walk becomes mov rcx,rbx; call; mov rcx,rax");
        Expect(thunk[0]==0x48 && thunk[1]==0xB8 && jumpTo==reinterpret_cast<const void*>(&CoverRoot) && thunk[10]==0xFF && thunk[11]==0xE0,
               "the call reaches CoverRoot");
        Expect(Matches(kCoverWalk+sizeof(kCoverWalkSig),kCoverReadSig,sizeof(kCoverReadSig)),"the stock read of +0x540 after it is untouched");
        // push rbx; mov rbx,rcx; sub rsp,20h; jmp walk -- and after the walk: mov rax,rcx; add rsp,20h; pop rbx; ret
        constexpr unsigned kEntry=0x1000;
        unsigned char* const entry=image+kEntry;
        const unsigned char prologue[]={0x53,0x48,0x8B,0xD9,0x48,0x83,0xEC,0x20,0xE9};
        std::memcpy(entry,prologue,sizeof(prologue));
        const auto jump=static_cast<std::int32_t>((image+kCoverWalk)-(entry+sizeof(prologue)+4));
        std::memcpy(entry+sizeof(prologue),&jump,4);
        const unsigned char epilogue[]={0x48,0x8B,0xC1,0x48,0x83,0xC4,0x20,0x5B,0xC3};
        std::memcpy(image+kCoverWalk+sizeof(kCoverWalkSig),epilogue,sizeof(epilogue));
        FlushInstructionCache(GetCurrentProcess(),image,0x2200000);
        using WalkRun=const unsigned char*(*)(const unsigned char*);
        const auto run=reinterpret_cast<WalkRun>(entry);
        Expect(run(human)==dead,"executed: the patched walk hands the stock read the top NPC of a recruited squad");
        config.npcFreeSquadCover=false;
        Expect(run(human)==kNoCover,"executed, NpcFreeSquadCover=0: the stock read gets the no-cover stand-in");
        config.npcFreeSquadCover=true;
        config.customNpcAi=false;
        Expect(run(human)==other,"executed, the AI off: the stock walk's player");
        config.customNpcAi=true;nextJoinMark=nullptr;
    }
    {   // Battle-size tables (perfrun stress:30 had 662 soldiers and 1508 enemies; the old tables held 256 / 1024 / 512 / 64):
        // every soldier, enemy, friend, squad and leader's hand-over is kept, and a known one is found again as itself.
        Reset();
        constexpr int kSoldierCount=1500,kEnemyCount=4000,kSquadCount=600,kHandoverCount=100;
        constexpr std::size_t kSmall=0x40;
        std::vector<unsigned char> crowd(static_cast<std::size_t>(kEnemyCount)*kSmall);
        const auto at=[&](int i){ return crowd.data()+static_cast<std::size_t>(i)*kSmall; };
        std::vector<Soldier*> slot(kSoldierCount);
        bool distinct=true,again=true;
        for(int i=0;i<kSoldierCount;++i){slot[static_cast<std::size_t>(i)]=Entry(at(i),now);distinct=distinct && slot[static_cast<std::size_t>(i)];}
        for(int i=0;i<kSoldierCount;++i){Soldier* e=Entry(at(i),now);again=again && e==slot[static_cast<std::size_t>(i)] && e->ref.Is(at(i));e->seen=now;}
        Expect(distinct && again && soldiers.size()==kSoldierCount,"1500 soldiers: each has its own entry and finds it again");
        slot[1]->arm=7;
        now+=kStaleMs+1;slot[1]->seen=now;
        Soldier* const reused=Entry(at(kSoldierCount),now);
        Expect(reused==slot[0] && soldiers.size()==kSoldierCount && FindSoldier(at(0))==nullptr && Entry(at(1),now)==slot[1] && slot[1]->arm==7,
               "a gone soldier's entry is the first taken again; a live one keeps its own");

        world.enemies=0;IndexFixture();world.enemyIndex.Clear();
        const float aim[3]={1,2,3};
        for(int k=0;k<2;++k)for(int i=0;i<kEnemyCount;++i)SeeEnemy(&world,at(i),aim);   // two lock points each
        bool rows=world.enemies==kEnemyCount;
        for(int i=0;rows && i<kEnemyCount;++i)rows=world.enemy[static_cast<std::size_t>(i)].object==at(i) && world.enemy[static_cast<std::size_t>(i)].points==2;
        Expect(rows,"4000 enemies: every one in the frame's list, in walk order, its lock points counted on its own row");

        std::vector<std::vector<unsigned char>> people(kSquadCount,std::vector<unsigned char>(0x2200));
        world.friends=0;IndexFixture();
        FriendWalk walk{kFriendVtable,&world};
        for(auto& p:people) {
            Put<void*>(p.data(),0,image+kSoldiers[0].vtable);Put<int>(p.data(),kTeam,kTeamFriend);Put<float>(p.data(),kPosition,1.0f);
            FriendVisit(&walk,p.data());
        }
        IndexFixture();
        const float shotFrom[3]={0.0f,1.0f,-5.0f},shotTo[3]={2.0f,1.0f,5.0f};   // across the spot they all stand on
        Expect(world.friends==kSquadCount && world.frObject[static_cast<std::size_t>(kSquadCount-1)]==people.back().data() &&
               !ShotClearOf(people[5].data(),nullptr,shotFrom,shotTo,0.0f),
               "600 friends (the old table held 512): all of them in the shot's test");
        bool squadsOk=true;
        for(auto& p:people)squadsOk=squadsOk && SeeSquad(p.data(),p.data(),0,npc::Control::free,now)!=nullptr;
        for(auto& p:people){const Squad* f=FindSquad(p.data());squadsOk=squadsOk && f && f->top.Is(p.data());}
        Expect(squadsOk && squads.size()==kSquadCount,"600 squads (the old table held 64): each counted in its own entry and found by its top");
        Retop(*FindSquad(people[0].data()),ObjRef::Of(people[1].data()+0x100));
        Expect(FindSquad(people[0].data())==nullptr && FindSquad(people[1].data()+0x100)==&squads[0],"a hand-over moves the squad's index to its new top");

        for(int i=0;i<kHandoverCount;++i)AddHandover(Handover{ObjRef::Of(at(i)),ObjRef::Of(at(kHandoverCount+i)),now});
        ObjRef next{};
        Expect(NpcSquadSuccessor(ObjRef::Of(at(0)),&next) && next.obj==at(kHandoverCount) &&
               NpcSquadSuccessor(ObjRef::Of(at(kHandoverCount-1)),&next) && next.obj==at(2*kHandoverCount-1),
               "100 hand-overs in a moment: the first one still followed (the old ring kept 32)");
        now+=kHandoverMs+1;AddHandover(Handover{ObjRef::Of(at(3000)),ObjRef::Of(at(3001)),now});
        Expect(!NpcSquadSuccessor(ObjRef::Of(at(0)),&next) && handovers.size()==1,"hand-overs past kHandoverMs are dropped");
    }
    // FriendVisit's radius off the vtable it read (IsSoldierClassLive, KnownVehicleVtable) against the old test
    // (IsSoldierClassLive || IsAnyPlayer, then KnownVehicle, both asking Readable): the same for an NPC soldier of each
    // class, this machine's player and another machine's (a soldier class, as the game makes every player), a soldier
    // flagged a player with no pad and no network bit, the vehicle, and an object of no class we know.
    {
        Reset();
        Put<const void*>(vehicle,0,image+0x17DADB0);   // a vtable of its own (505_Tank's), as every stock vehicle has
        auto oldRadius=[](const unsigned char* o) {
            return IsSoldierClassLive(o) || IsAnyPlayer(o) ? kSoldierRadius : KnownVehicle(o) ? kVehicleRadius : kOtherRadius;
        };
        std::vector<std::vector<unsigned char>> objects;
        auto add=[&](const void* vt,bool flag,bool pad,bool remote) {
            objects.emplace_back(0x2200);
            unsigned char* o=objects.back().data();
            Put<const void*>(o,0,vt);Put<int>(o,kTeam,kTeamFriend);Put<float>(o,kPosition,2.0f);
            o[edf::kHumanPlayer]=flag ? 1 : 0;Put<const void*>(o,edf::kHumanPad,pad ? o : nullptr);
            if(remote)o[edf::kRiderNet+edf::kNetFlags]|=1;
        };
        for(const auto& c:kSoldiers) {
            const void* vt=image+c.vtable;
            add(vt,false,false,false);add(vt,true,true,false);add(vt,true,false,true);add(vt,true,false,false);add(vt,false,true,true);
        }
        add(image+0x17D7320,false,false,false);   // DummyVehicleRider: no soldier class, not a vehicle
        add(image+0x1234560,false,false,false);
        world.friends=0;IndexFixture();
        FriendWalk walk{kFriendVtable,&world};
        bool same=true;int compared=0;
        for(auto& o:objects) {
            const int before=world.friends;
            FriendVisit(&walk,o.data());
            same=same && world.friends==before+1 && world.fr[static_cast<std::size_t>(before)].radius==oldRadius(o.data());++compared;
        }
        const int before=world.friends;
        FriendVisit(&walk,vehicle);
        same=same && world.friends==before+1 && world.fr[static_cast<std::size_t>(before)].radius==kVehicleRadius &&
             oldRadius(vehicle)==kVehicleRadius;++compared;
        Expect(same && compared==static_cast<int>(objects.size())+1,"FriendVisit's radius off the vtable: what the Readable-asking test gave, every kind");
        Reset();IndexFixture();
    }
    // npcai.cpp's own grid questions (the wrappers as Think asks them: PickTarget with its held row and WastedFire,
    // EnemyRow, FriendNear and shot safety with frObject != self, ThreatsNear's first 64 and
    // weights, PackGroupExists, NearestEnemy) against independent scans, over
    // random battles: clusters, repeats (enemies and friends on one spot; a friend object listed twice), far points.
    {
        Reset();
        std::mt19937 rng(20261108u);
        const auto U=[&rng](float a,float b){ return std::uniform_real_distribution<float>(a,b)(rng); };
        const auto I=[&rng](int n){ return static_cast<int>(rng()%static_cast<unsigned>(n)); };
        constexpr int kObjects=2000,kStride=0x40;
        std::vector<unsigned char> objects(static_cast<std::size_t>(kObjects)*kStride,0);
        const auto obj=[&objects](int k){ return objects.data()+static_cast<std::size_t>(k)*kStride; };
        for(int k=0;k<kObjects;++k)Put<const void*>(obj(k),kSelfCtrl,obj(k)+8);   // each its own control block
        int bad[8]={},asked=0,arcs=0;
        for(int battle=0;battle<12;++battle) {
            const float spread=battle%3==0 ? 60.0f : battle%3==1 ? 400.0f : 1500.0f;
            const int enemies=battle==11 ? 0 : 50+I(1100),friends=battle==10 ? 0 : 20+I(650);
            float cx=U(-spread,spread),cz=U(-spread,spread);
            const auto spot=[&](float* p){
                const int kind=I(10);
                if(kind<5){if(I(15)==0){cx=U(-spread,spread);cz=U(-spread,spread);}p[0]=cx+U(-12,12);p[2]=cz+U(-12,12);}
                else if(kind<9){p[0]=U(-spread,spread);p[2]=U(-spread,spread);}
                else{p[0]=U(-9e4f,9e4f);p[2]=U(-9e4f,9e4f);}
                p[1]=U(-3,40);
            };
            world.enemies=enemies;world.friends=friends;
            Room(world.enemy,enemies);Room(world.fr,friends);Room(world.frObject,friends);
            for(int i=0;i<enemies;++i) {
                Enemy& e=world.enemy[static_cast<std::size_t>(i)];
                // One row an object (SeeEnemy: another lock point of it counts in its row), on a spot of its own or
                // on another's (ties).
                e.object=obj(i);
                if(i>0 && I(6)==0)std::memcpy(e.aim,world.enemy[static_cast<std::size_t>(I(i))].aim,12);else spot(e.aim);
                e.points=1+I(4);
            }
            for(int i=0;i<friends;++i) {
                npc::Friend& f=world.fr[static_cast<std::size_t>(i)];
                if(i>0 && I(8)==0)f=world.fr[static_cast<std::size_t>(I(i))];else spot(f.pos);
                f.radius=I(3)==0 ? kVehicleRadius : I(2) ? kSoldierRadius : kOtherRadius;
                world.frObject[static_cast<std::size_t>(i)]=obj(I(kObjects));
            }
            IndexFixture();
            wastedTarget=enemies ? world.enemy[static_cast<std::size_t>(I(enemies))].object : nullptr;
            for(int ask=0;ask<700;++ask,++asked) {
                float from[3],at[3];
                if(enemies && I(2)){const float* a=world.enemy[static_cast<std::size_t>(I(enemies))].aim;from[0]=a[0]+U(-20,20);from[1]=a[1]+1.5f;from[2]=a[2]+U(-20,20);}
                else spot(from);
                if(friends && I(2)){const float* a=world.fr[static_cast<std::size_t>(I(friends))].pos;at[0]=a[0]+U(-3,3);at[1]=a[1];at[2]=a[2]+U(-3,3);}
                else spot(at);
                const void* self=I(4) && friends ? world.frObject[static_cast<std::size_t>(I(friends))] : obj(I(kObjects));
                // PickTarget: the held target an object's ref (none, or one of them), WastedFire passing one over.
                Soldier sol{};
                sol.target=I(3)==0 ? ObjRef{} : ObjRef::Of(obj(I(kObjects)));
                const float scope=I(6)==0 ? 1e9f : U(5,700),reach=I(3)==0 ? 0.0f : U(10,900);
                const int want=npc::PickTarget(world.enemies,[](int k) noexcept { return world.enemy[k].aim; },
                    [&sol](int k) noexcept { return sol.target.Is(world.enemy[k].object); },
                    [self](int k) noexcept { return !WastedFire(self,world.enemy[k].object); },from,at,scope,reach);
                const Pick pick=PickTarget(sol,self,from,at,scope,reach);
                if(pick.e!=(want<0 ? nullptr : &world.enemy[static_cast<std::size_t>(want)]))++bad[0];
                // EnemyRow: the first row whose object the ref is.
                int row=-1;
                for(int i=0;i<world.enemies;++i)if(sol.target.Is(world.enemy[static_cast<std::size_t>(i)].object)){row=i;break;}
                if(EnemyRow(sol.target)!=row)++bad[1];
                // FriendNear.
                const float r=I(4)==0 ? U(20,300) : U(0.5f,10.0f);
                bool nearWant=false;
                for(int i=0;i<world.friends;++i)if(world.frObject[static_cast<std::size_t>(i)]!=self && npc::Dist(world.fr[static_cast<std::size_t>(i)].pos,at)<r){nearWant=true;break;}
                if(FriendNear(self,nullptr,at,r)!=nearWant)++bad[2];
                // The production grid should give the same verdict as visiting everyone except the shooter.
                float to[3];
                if(enemies && I(3)){const float* a=world.enemy[static_cast<std::size_t>(I(enemies))].aim;to[0]=a[0];to[1]=a[1];to[2]=a[2];}
                else{const float len=I(5)==0 ? U(300,3000) : U(0,200),ang=U(0,6.3f);to[0]=from[0]+std::sin(ang)*len;to[1]=from[1]+U(-15,15);to[2]=from[2]+std::cos(ang)*len;}
                const float blast=I(2) ? 0.0f : U(1,25);
                std::vector<npc::Friend> all;
                for(int i=0;i<world.friends;++i)if(world.frObject[static_cast<std::size_t>(i)]!=self)all.push_back(world.fr[static_cast<std::size_t>(i)]);
                const bool clearAll=npc::ShotClear(from,to,kSpread,blast,all.data(),static_cast<int>(all.size()));
                if(clearAll!=ShotClearOf(self,nullptr,from,to,blast))++bad[3];
                // Check the production trajectory and self-filter against the unbatched reference.
                {
                    const float speed=U(15,60),gravity=I(4)==0 ? 0.0f : npcpack::kGravity*U(0.5f,1.5f),radius=I(3)==0 ? 0.0f : U(1,12);
                    const npcpack::Weapon pack{PackUse::thrown,150.0f,radius,0.0f,speed,gravity,0.0f,true};
                    float pitch=0.0f,flight=0.0f;
                    if(npcpack::LobPitch(npc::Horiz(from,to),to[1]-from[1],speed,gravity,&pitch,&flight)) {
                        const auto open=[](const float*,const float*) noexcept { return false; };
                        if(ScanPackArc(pack,from,to,pitch,all)!=PackArcClearOf(self,pack,from,to,pitch,open))++bad[4];
                        ++arcs;
                    }
                }
                // ThreatsNear: the first 64 within reach in the list's order, weighed as Evade weighs them.
                const float tr=I(3)==0 ? U(30,200) : U(2,40);
                npc::Threat want64[kMaxThreats],got64[kMaxThreats];int wn=0;
                for(int i=0;i<world.enemies && wn<kMaxThreats;++i) {
                    const Enemy& e=world.enemy[static_cast<std::size_t>(i)];
                    if(npc::Horiz(from,e.aim)<tr)want64[wn++]=npc::Threat{{e.aim[0],e.aim[1],e.aim[2]},e.points>=kLargePoints ? 3.0f : 1.0f};
                }
                const int gn=ThreatsNear(from,tr,got64);
                if(gn!=wn || std::memcmp(want64,got64,sizeof(npc::Threat)*static_cast<std::size_t>(wn))!=0)++bad[5];
                // PackGroupExists round a target, and the nearest over the ground (FallBack).
                if(enemies) {
                    const Enemy& t=world.enemy[static_cast<std::size_t>(I(enemies))];
                    const float gr=U(0,40);
                    int count=0;
                    for(int i=0;i<world.enemies;++i)if(npc::Dist(world.enemy[static_cast<std::size_t>(i)].aim,t.aim)<=gr)++count;
                    if(PackGroupExists(t,gr)!=(count>=npcpack::kGroupLeast))++bad[6];
                }
                const Enemy* nearest=nullptr;float nd=1e30f;
                for(int i=0;i<world.enemies;++i){const float d=npc::Horiz(from,world.enemy[static_cast<std::size_t>(i)].aim);if(d<nd){nd=d;nearest=&world.enemy[static_cast<std::size_t>(i)];}}
                if(NearestEnemy(from)!=nearest)++bad[7];
            }
        }
        wastedTarget=nullptr;
        static const char* const kWhat[]={"PickTarget","EnemyRow","FriendNear","ShotClearOf","backpack arc",
                                          "ThreatsNear","PackGroupExists","NearestEnemy"};
        for(int k=0;k<8;++k) {
            char what[200];
            std::snprintf(what,sizeof(what),"npcai's %s through the grid = the scan it replaced (%d asks over 12 battles, %d differ)",kWhat[k],asked,bad[k]);
            Expect(bad[k]==0,what);
        }
        Expect(arcs>1000,"the thrown rounds compared had an arc to fly");
        // IsAnyPlayerSelf (the Think's own object, no Readable) = IsAnyPlayer over every combination of its fields.
        {
            int differ=0;
            alignas(16) static unsigned char body[0x400];
            for(int k=0;k<64;++k) {
                std::memset(body,0,sizeof(body));
                body[edf::kHumanPlayer]=static_cast<unsigned char>(k&1 ? (k&8 ? 0x80 : 1) : 0);
                Put<const void*>(body,edf::kHumanPad,k&2 ? body : nullptr);
                body[edf::kRiderNet+edf::kNetFlags]=static_cast<unsigned char>((k&4 ? 1 : 0)|(k&16 ? 0xFE : 0)|(k&32 ? 2 : 0));
                differ+=edf::IsAnyPlayer(body)!=edf::IsAnyPlayerSelf(body);
            }
            Expect(differ==0,"IsAnyPlayerSelf reads the fields IsAnyPlayer reads, the same answer");
        }
    }
    // A crowd of targets no fire hurts (bench:4000, 2026-11-28: every enemy invincible) with a few that can be hurt among
    // them. Gather leaves the crowd out of the picks (IndexPicks: WastedOnEveryShooter), so each pick asks of the few;
    // without it (every row a pick's, as a shooter's own held fire leaves them) a pick stops after kPickRejects noes.
    // Each pick: the nearest it can hurt (the scan's) unless its noes ran out first, never one its fire is wasted on.
    {
        Reset();
        std::mt19937 rng(20261128u);
        const auto U=[&rng](float a,float b){ return std::uniform_real_distribution<float>(a,b)(rng); };
        constexpr int kCrowd=4000,kShooters=120;
        constexpr std::size_t kStride=0x40;
        std::vector<unsigned char> objects(static_cast<std::size_t>(kCrowd)*kStride,0);
        // mode 0: Gather's picks (the crowd left out); 1: every row a pick's (pickAll); 2: one shooter's own held fire
        // on the crowd (fed: not left out by Gather, its noes bounded).
        for(const int mode:{0,1,2})
        for(const int kHurt:{0,10}) {
            crowdBase=objects.data();crowdStride=kStride;
            shunned.assign(kCrowd,mode==2 ? 0 : 1);fed.assign(kCrowd,mode==2 ? 1 : 0);fedShooter=mode==2 ? other : nullptr;
            for(int k=0;k<kHurt;++k){const std::size_t r=rng()%kCrowd;shunned[r]=0;fed[r]=0;}
            world.enemies=kCrowd;Room(world.enemy,kCrowd);
            for(int i=0;i<kCrowd;++i) {
                Enemy& e=world.enemy[static_cast<std::size_t>(i)];
                e.object=objects.data()+static_cast<std::size_t>(i)*kStride;
                e.aim[0]=U(-250,250);e.aim[1]=U(0,5);e.aim[2]=U(-250,250);e.points=1;
            }
            IndexFixture();
            if(mode!=1)IndexPicks(world,[](int k) noexcept { return WastedOnEveryShooter(world.enemy[static_cast<std::size_t>(k)].object); });
            Expect(world.pickAll==(mode==1 || mode==2),"the picks leave out the rows every shooter's fire is wasted on alone");
            Expect(mode!=0 || world.picks==kHurt,"Gather's picks: the hurtable rows alone");
            int differ=0,early=0,wasted=0;long long asks=0,most=0;
            LARGE_INTEGER f0,t0,t1;QueryPerformanceFrequency(&f0);
            double ms=0.0;
            for(int k=0;k<kShooters;++k) {
                const float from[3]={U(-200,200),1.5f,U(-200,200)};
                const void* self=other;
                Soldier sol{};
                const float scope=400.0f,reach=300.0f;
                const int want=npc::PickTarget(world.enemies,[](int r) noexcept { return world.enemy[r].aim; },
                    [](int) noexcept { return false; },
                    [self](int r) noexcept { return !WastedFire(self,world.enemy[r].object); },from,from,scope,reach);
                const long long before=wastedAsks;
                QueryPerformanceCounter(&t0);
                const Pick pick=PickTarget(sol,self,from,from,scope,reach);
                QueryPerformanceCounter(&t1);
                ms+=static_cast<double>(t1.QuadPart-t0.QuadPart)*1000.0/static_cast<double>(f0.QuadPart);
                const long long n=wastedAsks-before;
                asks+=n;most=(std::max)(most,n);
                const Enemy* const wantE=want<0 ? nullptr : &world.enemy[static_cast<std::size_t>(want)];
                if(pick.e!=wantE) {
                    // Only a pick whose noes ran out may differ (none taken, or one it can hurt: never a wasted one).
                    if(n>=kPickRejects)++early;else ++differ;
                }
                if(pick.e && WastedFire(self,pick.e->object))++wasted;
            }
            std::printf("crowd of %d (%d hurtable, %s): %d picks, %.1f WastedFire asks a pick (most %lld), %d stopped at the bound, %.4f ms a pick\n",
                        kCrowd,kHurt,mode==0 ? "crowd left out" : mode==1 ? "every row" : "own held fire",kShooters,
                        static_cast<double>(asks)/kShooters,most,early,ms/kShooters);
            Expect(differ==0,"a crowd no fire hurts: each pick the nearest target it can hurt (the scan's), or none at the bound");
            Expect(wasted==0,"a crowd no fire hurts: no pick one the soldier's fire is wasted on");
            Expect(most<=kPickRejects+1,"a crowd no fire hurts: a pick asks at most kPickRejects noes and its one yes");
            Expect(mode!=0 || most<=kHurt,"Gather's picks: a pick asks of the hurtable rows alone");
        }
        crowdBase=nullptr;shunned.clear();fed.clear();fedShooter=nullptr;
        IndexFixture();
    }
    // A fault part way through Gather's walks: the grids are made from the lists as the walk left them (Gather's
    // __finally), not left over the last frame's rows; the row whose point could not be read is not made.
    {
        Reset();
        world.enemies=50;world.friends=5;
        for(int i=0;i<50;++i)world.enemy[static_cast<std::size_t>(i)]=Enemy{reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x9000+i*0x10)),{500.0f+static_cast<float>(i),0.0f,-300.0f},1};
        for(int i=0;i<5;++i){world.fr[static_cast<std::size_t>(i)]=npc::Friend{{static_cast<float>(i),1.0f,0.0f},0.8f};world.frObject[static_cast<std::size_t>(i)]=other;}
        IndexFixture();
        ++frame;gatherFoes=3;gatherFaults=true;
        const bool faulted=GatherCaught(0);
        const float at[3]={10.0f,0.0f,10.0f},old[3]={500.0f,0.0f,-300.0f};
        npc::Threat threats[kMaxThreats];
        const int close=ThreatsNear(at,1.0f,threats),stale=ThreatsNear(old,5.0f,threats);
        const int nearest=npc::grid::NearestHoriz(world.enemyGrid,world.enemies,[](int k) noexcept { return world.enemy[static_cast<std::size_t>(k)].aim; },old);
        Expect(faulted && world.frame==frame && world.enemies==3 && world.enemyGrid.Points()==3 && world.friends==0 &&
               world.friendGrid.Points()==0 && world.frRadius==0.0f && close==1 && stale==0 && nearest==2 &&
               world.enemyIndex.Find(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x6000)))<0 &&
               world.enemyIndex.Find(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x5010)))==1,
               "a fault part way through Gather: the grids are this frame's lists as walked, no half-made row");
        gatherFaults=false;++frame;watchedTargets.clear();
        Expect(!GatherCaught(0) && world.enemies==3 && world.enemyGrid.Points()==3,"the next frame's Gather walks them all again");
        // Every enemy's window watched by the frame's walk (WatchTarget), once an object, not only the would-be best
        // PickTarget asks WastedFire of: the observation does not depend on who asks.
        const std::vector<const void*> all={reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x5000)),
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x5010)),reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x5020))};
        Expect(watchedTargets==all,"Gather watches every enemy of the frame, once an object");
    }
    // The parallel Decide (NpcParallelThink, docs/npc-ai-design.md「并行决策与串行提交」): its answers are kept by what
    // they are about (the same object and weapon, from within mt::kNear of where the Decide asked), so the behaviour is
    // the same as with every ask made at Drive (off) up to a step's lateness, not bit for bit. The frames go through what
    // turns kept answers stale, unused or late: fire and holds, a hand switched (its arms changed), the walk to a combat
    // spot, a target HoldFire plans for (its verdict a plan's: peeked from the pair's last plan), the wastedfire book
    // written part way through the pass (a settled hit), a soldier dead and one turned to another faction part way
    // (Think's early returns: no Drive), targets killed or turned between frames, the targets moved after the fork (a
    // little: kept; one farther: asked again) and the map ray's wall put up after the fork. Checked: the fork on one
    // thread, on the pool and under Debug=1's shadow asks gives the same intents and the same behaviour bit for bit (the
    // shadow only counts); on and off give the same counts of shots, switches, plugin moves and target picks within a
    // tolerance; no shot at a target gone from the frame's list; a wall put up after the fork is shot through for one
    // step at most more than off. The hit rate and why each ask was made again are printed.
    {
        constexpr int kCrowd=24,kFoes=6,kFrames=8;
        alignas(16) static unsigned char crowd[kCrowd][0x2200]{},crowdCtrl[kCrowd][0x10]{};
        alignas(16) static unsigned char foes[kFoes][0x300]{},foeCtrl[kFoes][0x10]{};
        struct Seen {
            const void* target;int arm,wantArm;bool los;
            const char* move;bool fire;int planArm;
        };
        struct Run {
            std::vector<unsigned char> blocks;std::vector<Seen> seen;std::vector<unsigned char> intents;mt::Stats st{};
            int atGone=0;int walled[kCrowd]{};
        };
        const auto armsOf=[](int k,int frameNo) {
            Arms a{};a.n=2;
            a.arm[0]=npc::Arm{120,0,10,false,true,false};      // a rifle
            a.arm[1]=npc::Arm{90,6,30,false,true,false};       // a rocket launcher
            const bool switched=k==5 && frameNo>=2;            // its hand switched from frame 2
            a.held[0]=(k%3==0)!=switched ? 1 : 0;a.held[1]=k%4==0 ? 1-a.held[0] : -1;
            return a;
        };
        const auto run=[&](int mode)->Run {
            Run r;
            Reset();config.npcParallelThink=mode!=0;config.debug=mode==3;flatFloor=true;wall=false;
            wastedTarget=nullptr;heldTarget=foes[3];weakAimSet=false;wastedVersion=1;
            workers::SetLimit(mode==1 ? 1 : 0);
            std::memset(crowd,0,sizeof(crowd));std::memset(crowdCtrl,0,sizeof(crowdCtrl));
            std::memset(foes,0,sizeof(foes));std::memset(foeCtrl,0,sizeof(foeCtrl));
            for(int k=0;k<kCrowd;++k) {
                unsigned char* h=crowd[k];
                Put<void*>(h,0,image+kSoldiers[0].vtable);Put<int>(h,kTeam,kTeamFriend);Put<void*>(h,kLeader,nullptr);
                Put<unsigned>(h,kHumanMask,1);Put<unsigned>(h,kControlMask,kMaskMove|kMaskLook|kMaskTrigger);
                Put<void*>(h,kSelfCtrl,crowdCtrl[k]);Put<long>(crowdCtrl[k],8,1);Put<long>(crowdCtrl[k],12,1);
                const float at[3]={static_cast<float>(k%6)*6.0f-15.0f,0.0f,static_cast<float>(k/6)*7.0f-140.0f+static_cast<float>(k%5)*40.0f};
                std::memcpy(h+kPosition,at,12);
            }
            for(int i=0;i<kFoes;++i){Put<void*>(foes[i],kSelfCtrl,foeCtrl[i]);Put<long>(foeCtrl[i],8,1);Put<long>(foeCtrl[i],12,1);}
            for(int f=1;f<=kFrames;++f) {
                now+=16;++frame;world.frame=frame;wall=f%2==0 && f!=6;
                // the frame's list: foe 4 killed from frame 5 on, foe 5 turned to another faction from frame 6 on
                std::vector<const void*> gone;
                world.enemies=0;
                for(int i=0;i<kFoes;++i) {
                    if((i==4 && f>=5) || (i==5 && f>=6)){gone.push_back(foes[i]);continue;}
                    world.enemy[static_cast<std::size_t>(world.enemies++)]=Enemy{foes[i],{static_cast<float>(i)*9.0f-20.0f+static_cast<float>(f),
                        i%3==0 ? 1.0f : 30.0f,40.0f+static_cast<float>(i)*25.0f-static_cast<float>(f)*2.0f},i==2 ? 400 : 1};
                }
                // friends: the crowd (each its own object, passed over by its own clears) and two in the lines of fire
                world.friends=kCrowd+2;
                for(int k=0;k<kCrowd;++k) {
                    const float* p=Pos(crowd[k]);
                    world.fr[static_cast<std::size_t>(k)]=npc::Friend{{p[0],p[1]+1.0f,p[2]},0.8f};
                    world.frObject[static_cast<std::size_t>(k)]=crowd[k];
                }
                world.fr[kCrowd]=npc::Friend{{-10.0f,1.5f,20.0f},0.8f};world.frObject[kCrowd]=other;
                world.fr[kCrowd+1]=npc::Friend{{8.0f,20.0f,50.0f},0.8f};world.frObject[kCrowd+1]=dead;
                IndexFixture();
                mt::Fork(GameFrame(),now);
                if(mode!=0) {
                    const auto* b=reinterpret_cast<const unsigned char*>(mt::intents.data());
                    r.intents.insert(r.intents.end(),b,b+mt::intents.size()*sizeof(mt::Intent));
                }
                // after the fork: every target a little on (within mt::kNear), the first 1 m on in frame 6; frame 6's
                // wall put up
                for(int i=0;i<world.enemies;++i)world.enemy[static_cast<std::size_t>(i)].aim[0]+=i==0 && f==6 ? 1.0f : 0.2f;
                if(f==6)wall=true;
                for(int k=0;k<kCrowd;++k) {
                    unsigned char* h=crowd[k];
                    if(f==3 && k==kCrowd/2){wastedTarget=foes[1];++wastedVersion;}   // a hit settled part way through the pass
                    if(f>=4 && k==7)h[kDead]=1;                     // dead part way (frame 4 on): Think returns before Drive
                    if(f>=4 && k==9)Put<int>(h,kTeam,1);            // another faction: likewise
                    if(h[kDead] || At<int>(h,kTeam)!=kTeamFriend){r.seen.push_back(Seen{});continue;}
                    Soldier* s=Entry(h,now);
                    s->seen=now;
                    if(!s->controlSet){s->control=npc::Control::free;s->controlSet=true;std::memcpy(s->home,Pos(h),12);s->homeSet=true;}
                    const Arms a=armsOf(k,f);
                    const float* pos=Pos(h);
                    const float eye[3]={pos[0],pos[1]+kEye,pos[2]};
                    mt::Begin(h);
                    const Plan p=Drive(*s,h,kSoldiers[0],a,nullptr,eye,pos,nullptr,now);
                    mt::End();
                    mt::applying=nullptr;
                    if(p.fire && std::find(gone.begin(),gone.end(),s->target.obj)!=gone.end())++r.atGone;
                    if(p.fire && wall)++r.walled[k];
                    r.seen.push_back(Seen{s->target.obj,s->arm,s->wantArm,s->los,p.move,p.fire,p.arm});
                }
                r.blocks.insert(r.blocks.end(),&crowd[0][0],&crowd[0][0]+sizeof(crowd));
            }
            r.st=mt::stats;
            return r;
        };
        const auto same=[](const Run& x,const Run& y) {
            if(x.blocks!=y.blocks || x.seen.size()!=y.seen.size())return false;
            for(std::size_t i=0;i<x.seen.size();++i) {
                const Seen& a=x.seen[i];const Seen& b=y.seen[i];
                if(a.target!=b.target || a.arm!=b.arm || a.wantArm!=b.wantArm || a.los!=b.los || a.fire!=b.fire ||
                   a.planArm!=b.planArm || (a.move!=b.move && (!a.move || !b.move || std::strcmp(a.move,b.move)!=0)))return false;
            }
            return true;
        };
        struct Counts { int fired=0,switched=0,walked=0,picks=0,sticks=0,turns=0; };
        const auto counts=[&](const Run& x) {
            Counts c;
            for(std::size_t i=0;i<x.seen.size();++i) {
                const Seen& v=x.seen[i];
                c.fired+=v.fire;c.switched+=v.arm!=v.wantArm || v.planArm==1;c.walked+=v.move && std::strcmp(v.move,"stock")!=0;
                if(i>=kCrowd && v.target!=x.seen[i-kCrowd].target)++c.turns;   // a target picked anew (frame to frame)
            }
            for(std::size_t at=0;at<x.blocks.size();at+=0x2200) {   // every soldier's block after every frame
                const unsigned char* b=x.blocks.data()+at;
                c.picks+=b[kPickWeapon]|b[kPickWeapon+1]|b[kPickWeapon+2];
                c.sticks+=At<float>(b,kMoveX)!=0.0f || At<float>(b,kMoveZ)!=0.0f;
            }
            return c;
        };
        const Run off=run(0),one=run(1),pool=run(2),shadow=run(3);
        const Counts co=counts(off),cn=counts(one);
        char what[400];
        std::snprintf(what,sizeof(what),"the frames off: %d shots, %d with the second hand or switching (%d weapon picks "
                      "written), %d plans of the plugin's, %d moving, %d targets picked anew",co.fired,co.switched,co.picks,
                      co.walked,co.sticks,co.turns);
        Expect(co.fired>0 && co.switched>0 && co.picks>0 && co.walked>0 && co.sticks>0 && co.turns>0,what);
        Expect(off.st.applied==0 && off.st.hits==0 && off.intents.empty(),"NpcParallelThink=0: no fork, every ask at Drive");
        std::snprintf(what,sizeof(what),"the fork on 1 thread, on %d and with the shadow asks: the same intents bit for bit "
                      "(%zu bytes) and the same blocks, entries and plans",workers::Threads(),pool.intents.size());
        Expect(!one.intents.empty() && one.intents==pool.intents && pool.intents==shadow.intents && same(one,pool) &&
               same(one,shadow) && one.st.faults==0 && pool.st.faults==0,what);
        const auto within=[](int a,int b) { const int d=a>b ? a-b : b-a;return d<=(std::max)(3,b/10); };
        std::snprintf(what,sizeof(what),"on vs off (tolerance max(3, 10%%)): shots %d/%d, switches %d/%d, weapon picks %d/%d, "
                      "plugin plans %d/%d, moving %d/%d, targets picked anew %d/%d",cn.fired,co.fired,cn.switched,co.switched,
                      cn.picks,co.picks,cn.walked,co.walked,cn.sticks,co.sticks,cn.turns,co.turns);
        Expect(within(cn.fired,co.fired) && within(cn.switched,co.switched) && within(cn.picks,co.picks) && within(cn.walked,co.walked) &&
               within(cn.sticks,co.sticks) && within(cn.turns,co.turns),what);
        Expect(off.atGone==0 && one.atGone==0 && pool.atGone==0,"no shot at a target killed or turned (gone from the frame's list)");
        bool walledOk=true;int walledOn=0,walledOff=0;
        for(int k=0;k<kCrowd;++k){walledOn+=one.walled[k];walledOff+=off.walled[k];walledOk=walledOk && one.walled[k]<=off.walled[k]+1;}
        std::snprintf(what,sizeof(what),"a wall put up after the fork: shot through one step at most more than off, a soldier "
                      "(shots with the wall up: on %d, off %d)",walledOn,walledOff);
        Expect(walledOk,what);
        const mt::Stats& st=one.st;
        const unsigned asks=st.hits+st.misses;
        std::snprintf(what,sizeof(what),"kept answers %u of %u (%.0f%%; pool %u), stale %u, no intent %u; asked again: pick %u, "
                      "hold %u, close %u, ray %u, clear %u",st.hits,asks,asks ? 100.0*st.hits/asks : 0.0,pool.st.hits,st.stale,
                      st.noIntent,st.asked[mt::kAskPick],st.asked[mt::kAskHold],st.asked[mt::kAskClose],st.asked[mt::kAskRay],
                      st.asked[mt::kAskClear]);
        Expect(st.hits>0 && st.hits==pool.st.hits && st.misses==pool.st.misses && st.misses>0 && st.stale>0 &&
               st.hits>=3*st.misses,what);
        std::printf("  why asked again: no answer %u, other object %u, weapon %u, held %u, drift %u, plan %u, line %u, stale %u;"
                    " kept unused: pick %u, hold %u, close %u, ray %u, clear %u\n",st.cause[mt::kNoAnswer],st.cause[mt::kOther],
                    st.cause[mt::kWeapon],st.cause[mt::kHeld],st.cause[mt::kDrift],st.cause[mt::kPlan],st.cause[mt::kLine],
                    st.stale,st.unused[mt::kAskPick],st.unused[mt::kAskHold],st.unused[mt::kAskClose],st.unused[mt::kAskRay],
                    st.unused[mt::kAskClear]);
        Expect(st.cause[mt::kDrift]>0 && st.stale>0,
               "the causes the frames make: drift (the target moved 1 m after the fork), stale (a hit settled in the pass)");
        std::snprintf(what,sizeof(what),"Debug=1's shadow asks: %u of %u kept answers differ from the game thread's now "
                      "(counted, not an error)",shadow.st.differs,shadow.st.shadowed);
        Expect(shadow.st.shadowed>0,what);
        // A kept answer made wrong (the pick turned to no target): the shadow ask counts the difference.
        {
            Reset();flatFloor=true;workers::SetLimit(0);heldTarget=nullptr;
            unsigned char* h=crowd[0];
            std::memset(crowd,0,sizeof(crowd));std::memset(crowdCtrl,0,sizeof(crowdCtrl));
            Put<void*>(h,0,image+kSoldiers[0].vtable);Put<int>(h,kTeam,kTeamFriend);Put<void*>(h,kLeader,nullptr);
            Put<unsigned>(h,kControlMask,kMaskMove|kMaskLook|kMaskTrigger);
            Put<void*>(h,kSelfCtrl,crowdCtrl[0]);Put<long>(crowdCtrl[0],8,1);Put<long>(crowdCtrl[0],12,1);
            Put<void*>(foes[0],kSelfCtrl,foeCtrl[0]);Put<long>(foeCtrl[0],8,1);Put<long>(foeCtrl[0],12,1);
            world.enemies=1;world.enemy[0]=Enemy{foes[0],{0,1,30},1};world.friends=0;IndexFixture();
            const Arms a=armsOf(1,1);
            const float eye0[3]={0,kEye,0};
            Soldier* s0=Entry(h,now);s0->control=npc::Control::free;s0->controlSet=s0->homeSet=true;
            const auto frameOf=[&](bool corrupt) {
                now+=16;++frame;world.frame=frame;s0->seen=now;
                mt::Fork(GameFrame(),now);
                if(corrupt && !mt::intents.empty()){mt::intents[0].pick.row=-1;config.debug=1;mt::shadowTick=31;}
                mt::Begin(h);
                Drive(*s0,h,kSoldiers[0],a,nullptr,eye0,Pos(h),nullptr,now);
                mt::End();
                mt::applying=nullptr;config.debug=0;
            };
            frameOf(false);
            const unsigned before=mt::stats.differs;
            frameOf(true);
            Expect(mt::intents.size()==1 && mt::stats.differs==before+1,"a wrong kept pick: the shadow ask counts it (NPCMT SHADOW)");
            // A target HoldFire plans for: the first frame's pick asked again (no plan made yet: kPlan), the next one's
            // kept (the pair's plans the game thread's asks made a step before, peeked).
            heldTarget=foes[0];stubPlans.clear();
            const unsigned plan0=mt::stats.cause[mt::kPlan];
            frameOf(false);
            const unsigned plan1=mt::stats.cause[mt::kPlan],asked1=mt::stats.asked[mt::kAskPick];
            frameOf(false);
            char planWhat[200];
            std::snprintf(planWhat,sizeof(planWhat),"a target with a plan: asked again until its plan is made (plan %u), then "
                          "kept (pick asked %u more)",plan1-plan0,mt::stats.asked[mt::kAskPick]-asked1);
            Expect(plan1>plan0 && mt::stats.cause[mt::kPlan]==plan1 && mt::stats.asked[mt::kAskPick]==asked1,planWhat);
        }
        workers::SetLimit(0);heldTarget=nullptr;
    }
    // stepobj (common/edf/step_objects.h): with the fixture's objects in a stand-in System's list for this frame, a
    // soldier's PreThink and Think ask edf::Readable for none of them (stress:100: ~63,000 asks a step before).
    {
        struct Node { void* next; void* prev; void* object; void* ctrl; };
        alignas(16) static unsigned char sys[0x1000]{};
        static Node nodes[4]{},listHead{},addedHead{};
        static unsigned char blocks[4][0x10]{};
        InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(sys+stepobj::kLock));
        void* objects[4]={human,dead,other,vehicle};
        listHead.next=listHead.prev=&listHead;addedHead.next=addedHead.prev=&addedHead;
        for(int i=0;i<4;++i) {
            Put<int>(blocks[i],8,1);
            nodes[i]={&listHead,listHead.prev,objects[i],blocks[i]};
            static_cast<Node*>(listHead.prev)->next=&nodes[i];listHead.prev=&nodes[i];
        }
        Put<void*>(sys,stepobj::kObjects,&listHead);Put<std::uint64_t>(sys,stepobj::kObjects+8,4);
        Put<void*>(sys,stepobj::kAdded,&addedHead);Put<std::uint64_t>(sys,stepobj::kAdded+8,0);
        Put<void*>(sys,stepobj::kScene,sys);
        static void* sysPtr=sys;
        struct Stock { static void __fastcall Run(void*,const float*) {} };
        const ThinkFn keep=nextThink[0];nextThink[0]=&Stock::Run;
        const float dt=1.0f/60.0f;
        const auto asks=[&]() {
            Reset();Put<void*>(human,kLeader,other);++frame;world.frame=frame;
            edf::TakeReadableStats();
            PreThink(human);ThinkHook<0>(human,&dt);
            return edf::TakeReadableStats().asks;
        };
        asks();
        const unsigned long long off=asks();
        stepobj::Install(const_cast<const void* const*>(&sysPtr));stepobj::Released(true);
        const unsigned long long on=asks();
        const stepobj::Stats st=stepobj::TakeStats();
        stepobj::Install(nullptr);
        nextThink[0]=keep;
        char what[200];
        std::snprintf(what,sizeof(what),"a soldier's PreThink + Think: %llu Readable asks without stepobj, %llu with (%llu found in the copy)",
                      off,on,st.yes);
        Expect(off>0 && on*4<=off && on<=4,what);
        Expect(st.copies==1,"one copy of the System's list for the frame");
    }
    VirtualFree(image,0,MEM_RELEASE);return failures ? 1 : 0;
}
#endif
