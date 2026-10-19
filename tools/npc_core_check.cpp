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
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace crew {
bool InstallRealDriverNative(NpcSeatInputOwnedFn) noexcept { return true; }
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
bool TransportPair(const void* top,const void* v) noexcept {++transportPairs;transportPairTop=top;transportPaired=v;return v!=nullptr;}
const void* TransportOf(const void*) noexcept {return transportPaired;}
bool TransportOrder(const void*,const mapcmd::Command&) noexcept {++transportOrders;return transportTakes;}
void TransportCancel(const void*) noexcept {++transportCancels;}
NpcCommandReason TransportWithdraw(const void*) noexcept {return transportWithdraw;}
void TransportSucceed(const void*,const void* lead) noexcept {transportSucceededTo=lead;}
namespace {
Config config{};
ULONGLONG now=1000,frame=1;
bool sessionOn=false,host=true,door=true;
bool wall=false;
float arcWallZ=0.0f; // a finite map wall for production Backpack's segment queries
bool flatFloor=false,completeRide=false;
bool mapHeld=true,rayOn=false;
const void* markEnemy=nullptr;
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
void Reset() {
    heldSupportActor=nullptr;playerObj=nullptr;ResetNpcAi();config=Config{};now=1000;frame=1;sessionOn=false;host=true;door=true;wall=false;flatFloor=completeRide=false;follows=rides=announcedBoards=0;
    arcWallZ=0.0f;
    std::memset(human,0,sizeof(human));std::memset(dead,0,sizeof(dead));std::memset(other,0,sizeof(other));
    std::memset(vehicle,0,sizeof(vehicle));std::memset(seats,0,sizeof(seats));std::memset(ctrl,0,sizeof(ctrl));
    for(auto p : {human,dead,other}) { Put<void*>(p,0,image+kSoldiers[0].vtable);Put<int>(p,kTeam,kTeamFriend); }
    Put<void*>(human,kLeader,dead);dead[kDead]=1;
    Put<void*>(dead,kFollowers,head);Put<void*>(head,0,node);Put<void*>(node,0,head);Put<void*>(node,0x10,human);
    Put<void*>(vehicle,kSeats,seats);Put<std::uint64_t>(vehicle,kSeatCount,2);Put<void*>(vehicle,kSelfCtrl,ctrl);Put<int>(ctrl,8,1);
    Put<unsigned>(human,kHumanMask,1);Put<unsigned>(seats+edf::kSeatStride,kSeatClass,1);Put<unsigned>(seats+edf::kSeatStride,kSeatEnable,1);
    doorAt[0]=doorAt[1]=doorAt[2]=0;doorReach=3.0f;
    gunnerEnemy=nullptr;vehicleCopyOwner=online::kCopyHost;
    mapHeld=true;rayOn=false;markEnemy=nullptr;pointOrders=0;markinput::down=false;
    ok=followOk=rideOk=true;world.frame=frame;config.npcSquadSuccession=true;
    transportTakes=false;transportOrders=transportCancels=transportPairs=0;transportPaired=transportPairTop=transportSucceededTo=nullptr;
    transportWithdraw=NpcCommandReason::noTransport;
}
}
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept { return now; }
ULONGLONG GameFrame() noexcept { return frame; }
void SeeFrame(const void*) noexcept {}
bool InSession() noexcept { return sessionOn; }
bool OnlineHostOnly() noexcept { return !sessionOn || host; }
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
bool drivableVehicle=true;   // the stand-in vehicle is a ground one a soldier can drive (npcpost.cpp NpcDrivable)
bool NpcDrivable(const unsigned char* v) noexcept { return drivableVehicle && v==vehicle; }
bool PrepareNpcVehicleAi(unsigned char*) noexcept {return true;}
int tankCommands=0;Command lastTankCommand{};
bool TankCommand(const void*,const Command& command,const ObjRef&) noexcept {++tankCommands;lastTankCommand=command;return true;}
float MapRay(const float* from,const float* to,float* hit) noexcept {
    if(arcWallZ>0.0f && from[2]<arcWallZ && to[2]>=arcWallZ) {
        const float t=(arcWallZ-from[2])/(to[2]-from[2]);
        for(int k=0;k<3;++k)hit[k]=from[k]+(to[k]-from[k])*t;
        return npc::Dist(from,hit);
    }
    if(!wall)return -1.0f;hit[0]=0;hit[1]=1.5f;hit[2]=20;return 20;
}
bool SeatPoint(const unsigned char*,unsigned,float* at,float* reach) noexcept {
    if(!door)return false;std::memcpy(at,doorAt,12);*reach=doorReach;return true;
}
bool VisitEnemiesOf(std::int32_t,EnemyVisitor visit,void* ctx) noexcept {
    if(markEnemy){const float at[3]={0,0,20};visit(ctx,markEnemy,at);}return true;
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
int pointMarks=0,cueOwn=0,cueOff=0,qmarkFrames=0;
void QMarkSetPoint(const float*) noexcept { ++pointMarks; }
void QMarkPlay(QMarkCue c) noexcept { cueOwn+=c==QMarkCue::own;cueOff+=c==QMarkCue::off; }
void QMarkFrame() noexcept { ++qmarkFrames; }
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
bool suppressGunnerChoice=false;
PayloadFire chosenGunnerFire=PayloadFire::primary;
bool FriendInLine(const float*,const float*,const void*) noexcept { return false; }   // heli.cpp's (NpcCoaxFire's check)
// wastedfire.cpp WastedFire: the target every shooter's fire is wasted on (the shut teleportation ship), as the case sets.
const void* wastedTarget=nullptr;
bool WastedFire(const void*,const void* target) noexcept { return target && target==wastedTarget; }
// HoldFire: the same target held, and one due to open (heldTarget) aimed at and held; `aim` the weak spot when set.
const void* heldTarget=nullptr;float weakAim[3]{};bool weakAimSet=false;
float predictedSpeed=0.0f,predictedGravity=0.0f;
bool HoldFire(const void*,const void* target,const float*,float speed,float gravity,float* aim) noexcept {
    predictedSpeed=speed;predictedGravity=gravity;
    if(aim && weakAimSet && target)std::memcpy(aim,weakAim,12);
    return target && (target==wastedTarget || target==heldTarget);
}
int coaxCalls=0;   // payload.cpp NpcCoaxFire: called beside every crew aim (no coax on these fixtures)
bool NpcCoaxFire(unsigned char*,unsigned,const void*,LineBlocked) noexcept { ++coaxCalls;return false; }
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
}
#ifndef SUPPORT_INFANTRY_NATIVE_TEST
int main() {
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
    Reset();world.friends=1;world.frObject[0]=other;Put<std::uint64_t>(other,kFollowerCount,1);Put<unsigned>(other,kObjectFlags,kFixPosition);
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
    Reset();world.friends=1;world.frObject[0]=other;world.fr[0]=npc::Friend{{8,1.5f,20},0.8f};
    human[kTrigger]=human[kTrigger+1]=1;Enemy target{dead,{0,1.5f,20},1};Arms arms{};arms.n=2;arms.held[0]=0;arms.held[1]=1;
    arms.arm[0]=npc::Arm{100,0,1,false,true,false};arms.arm[1]=npc::Arm{100,10,1,false,true,false};
    Put<void*>(human,kStockTarget,dead);Put<void*>(human,kStockTargetCtrl,ctrl);world.enemies=1;world.enemy[0]=target;
    const float eye[3]={0,1.5f,0};Scripted(s,human,arms,eye,nearby);
    Expect(human[kTrigger+1]==0,"scripted Fencer secondary blast is checked even when primary is nonexplosive");
    Expect(human[kTrigger]==1,"the nonexplosive primary hand keeps firing while only the explosive second hand is vetoed");
    Reset();world.friends=1;world.frObject[0]=other;world.fr[0]=npc::Friend{{8,1.5f,20},0.8f};
    arms=Arms{};arms.n=2;arms.held[0]=0;arms.arm[0]=npc::Arm{100,0,1,false,true,false};arms.arm[1]=npc::Arm{100,10,1,false,true,false};
    human[kTrigger]=1;Veto(human,&target,eye,arms);
    Expect(human[kTrigger]==1,"a held rifle is not vetoed for the rocket launcher only carried in the weapon list");
    arms.held[0]=-1;human[kTrigger]=1;Veto(human,&target,eye,arms);
    Expect(human[kTrigger]==0,"a hand whose weapon is unknown is tested with the largest carried blast");
    Reset();wall=true;world.friends=1;world.frObject[0]=other;world.fr[0]=npc::Friend{{8,1.5f,20},0.8f};
    arms=Arms{};arms.n=1;arms.held[0]=0;arms.arm[0]=npc::Arm{100,10,1,false,true,false};
    human[kTrigger]=1;Veto(human,nullptr,eye,arms);
    Expect(human[kTrigger]==0,"unknown target still checks the blast at the actual map impact");
    Reset();world.friends=1;world.frObject[0]=other;world.fr[0]=npc::Friend{{0,1.5f,90},0.8f};
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
        const Arms both=ArmsOf(human);
        Expect(both.n==3 && both.held[0]==2 && both.held[1]==1,"each WeaponSet's held weapon is found at the native set stride");
        Expect(both.speed[2]==120.0f && std::fabs(both.gravity[2]-7.35f)<0.001f,
               "the held weapon's native speed and gravity are converted to metres and seconds");
        Put<std::uint64_t>(human,kSetCount,1);
        Expect(ArmsOf(human).held[1]==-1,"a one-handed soldier has no second hand's weapon");
    }
    Reset();s=Soldier{};s.wantArm=-1;arms=Arms{};arms.n=4;arms.held[0]=0;
    arms.arm[0]=npc::Arm{5,0,1,false,true,false};arms.arm[1]=npc::Arm{100,0,10,false,true,false};
    arms.arm[3]=npc::Arm{100,0,100,false,true,false};ChooseArm(s,human,arms,&target,eye,nearby);
    Expect(human[kPickWeapon+1]==1,"an inaccessible fourth weapon cannot suppress selecting a usable second one");
    Reset();Put<void*>(human,kLeader,nullptr);config.scriptNpcSettleSec=0;Put<void*>(human,kRoute,other);
    SeeSquad(human,human,0,npc::Control::script,now);++frame;++now;Put<void*>(human,kRoute,nullptr);
    SeeSquad(human,human,0,npc::Control::free,now);
    Expect(human[kAutoFollow]==1,"offline script release still enables recruitment");
    Reset();world.friends=1;world.frObject[0]=other;Put<std::uint64_t>(other,kFollowerCount,1);
    squads[0].top=ObjRef::Of(dead);squads[0].dismissed=true;squads[0].autoFollow=1;dismissedCount=1;
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
        Put<float>(vehicle,kPosition,1000);world.friends=1;world.frObject[0]=vehicle;
        SeeSquad(human,human,0,npc::Control::free,now);sessionOn=true;
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
        world.friends=0;world.nobodys=0;
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
        world.friends=0;world.nobodys=0;SeeSquad(human,human,0,npc::Control::free,now);
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
        world.enemies=1;world.enemy[0]=Enemy{enemy,{0,0,20},1};++frame;SeeSquad(human,human,0,npc::Control::free,now);
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
        config.npcEvade=true;world.enemies=1;world.enemy[0]=Enemy{enemy,{0,0,4},1};
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
        world.enemy[0]=Enemy{enemy,{0,0,30},1};
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::attackMove,{40,0,0}},ObjRef::Of(other),{});
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(fights(behavior.move) && soldier->target.Is(enemy),"an attack-move stands and fights an enemy in reach, not only a close one");
        // A target it never gets a shot at (this stand-in soldier never fires: its look is not on it) holds it only for the
        // grace: then it walks on, still on its target, instead of standing there for ever.
        now+=mapcmd::kAttackMoveGraceMs;++frame;world.frame=frame;SeeSquad(human,human,0,npc::Control::free,now);
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(std::strcmp(behavior.move,"attack-move")==0 && soldier->target.Is(enemy),
               "an attack-move with no shot at its target for the grace walks on (not stuck at its combat spot)");
        world.enemies=0;frame+=2;world.frame=frame;
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
            transportTakes=false;transportWithdraw=NpcCommandReason::none;
            result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::withdraw,{}},ObjRef::Of(other),{});
            Expect(result.Accepted(),"WITHDRAW answered by its transport");
            transportWithdraw=NpcCommandReason::noTransport;
            result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::withdraw,{}},ObjRef::Of(other),{});
            Expect(result.reason==NpcCommandReason::noTransport,"...and its refusal passed on");
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
        MoveTo(human,Pos(human),Pos(human),1e9f);
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
        world.enemies=1;world.enemy[0]=Enemy{dead,{300.0f,1.5f,60.0f},1};
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
        Expect(PickUp(fetch,human,away) && collects==0,"a soldier far from its box runs to it, nothing taken yet");
        const float downstairs[3]={50.0f,-8.0f,30.0f};
        Expect(PickUp(fetch,human,downstairs) && collects==0 && !boxUnit[0][kBoxTaken],
               "a soldier directly below a box cannot collect it through another floor");
        sweep.on=false;
        Expect(!PickUp(fetch,human,there) && collects==0,"recalling a sweep invalidates its already assigned pickup in the same frame");
        sweep.on=true;
        Expect(PickUp(fetch,human,there) && collects==1 && collectBy==me && notifies==1 && notifyBy==me,
               "at its box: native Notify and Apply once, credited to the local player");
        Expect(notifyId==42 && notifyKind==npc::pickup::kWeapon && applyKind==notifyKind && notifyBeforeApply && untakenAtApply,
               "native Collect ordering and exact box identity: Notify, Apply, then mark taken");
        Expect(boxUnit[0][kBoxTaken]==1 && boxUnit[1][kBoxTaken]==0 && !fetch.pickUnit,"only that box taken, the one a metre off left");
        Expect(!PickUp(fetch,human,there),"no box any more: the soldier's other moves");
        fetch.pickUnit=boxUnit[0];
        Expect(!PickUp(fetch,human,there) && collects==1,"a box someone else took: nothing called");
        Boxes(npc::pickup::kWeapon,npc::pickup::kHealSmall);
        std::memcpy(boxModel[1],boxModel[0],16);fetch.pickUnit=boxUnit[0];sessionOn=true;
        Expect(PickUp(fetch,human,there) && collects==1 && notifies==1 && boxUnit[0][kBoxTaken] && !boxUnit[1][kBoxTaken] && heals==0,
               "online overlapping boxes: only the selected weapon is taken, the health box is never consumed");
        sessionOn=false;
        // Health boxes: the hurt soldier itself, offline, allowed.
        Boxes(npc::pickup::kHealBig,npc::pickup::kWeapon);
        config.npcPickupHealth=true;Put<float>(human,kHumanHpMax,1000.0f);Put<float>(human,kHumanHp,400.0f);
        fetch=Soldier{};fetch.pickUnit=boxUnit[0];std::memcpy(fetch.pickPos,boxModel[0],12);fetch.pickKind=npc::pickup::kHealBig;
        Expect(PickUp(fetch,human,there) && heals==1 && healed==human && std::fabs(healAmount-300.0f)<0.01f && collects==0 &&
               boxUnit[0][kBoxTaken]==1 && boxUnit[1][kBoxTaken]==0,
               "a hurt soldier at a big health box: healed 30% of its full health itself, not the player's Collect");
        Boxes(npc::pickup::kHealSmall,npc::pickup::kWeapon);
        Put<float>(human,kHumanHp,1000.0f);fetch.pickUnit=boxUnit[0];fetch.pickKind=npc::pickup::kHealSmall;
        Expect(PickUp(fetch,human,there) && heals==0 && boxUnit[0][kBoxTaken]==0,"healed to full meanwhile: the box left for the player");
        Put<float>(human,kHumanHp,400.0f);fetch.pickUnit=boxUnit[0];sessionOn=true;
        Expect(PickUp(fetch,human,there) && heals==0 && boxUnit[0][kBoxTaken]==0,"online: no health box (the plugin's heal is not synced)");
        sessionOn=false;config.npcPickupHealth=false;fetch.pickUnit=boxUnit[0];
        Expect(PickUp(fetch,human,there) && heals==0 && boxUnit[0][kBoxTaken]==0,"health boxes switched off: left alone");
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
        mapHeld=true;markinput::down=true;NpcMarkFrame(human,false);
        Expect(!NpcMarked(),"TV or closing-map input hold blocks on-foot marking");
        markinput::down=false;NpcMarkFrame(human,false);mapHeld=false;
        cueOwn=cueOff=pointMarks=qmarkFrames=0;
        markinput::down=true;NpcMarkFrame(human,false);
        Expect(NpcMarked(),"a fresh player-frame press marks without any NPC Think");
        Expect(cueOwn==1 && cueOff==0 && qmarkFrames==1,"marking asks for the own cue; the Q mark's frame runs from the player's");
        markinput::down=false;NpcMarkFrame(human,false);
        markinput::down=true;NpcMarkFrame(human,false);
        Expect(!NpcMarked() && cueOff==1,"the same enemy again: let go, the off cue");
        markinput::down=false;NpcMarkFrame(human,false);
        Put<void*>(human,kHumanVehicleCtrl,foeCtrl);   // riding (the stock spot it replaces was riding too)
        markinput::down=true;NpcMarkFrame(human,false);
        Expect(NpcMarked() && cueOwn==2,"riding: Q marks as on foot");
        Put<void*>(human,kHumanVehicleCtrl,nullptr);
        markinput::down=false;NpcMarkFrame(human,false);markEnemy=nullptr;
        markinput::down=true;NpcMarkFrame(human,false);
        Expect(pointOrders==1 && NpcMarked(),"a ground miss orders selected units without clearing the existing mark");
        Expect(pointMarks==1 && cueOwn==3,"a ground miss marks the point (shared, shown) with the own cue");
        markinput::down=false;NpcMarkFrame(human,false);config.enabled=false;
        markinput::down=true;NpcMarkFrame(human,false);
        Expect(pointOrders==1 && !NpcMarked(),"global disable prevents ground orders and hides the mark");
        ResetNpcAi();Expect(At<long>(foeCtrl,0xC)==1,"player-frame mark releases its identity on reset");
    }
    // The teammates' marks are the NPCs' as this machine's are (the user, 2026-10-10: "npc不是统一的吗，都去打"): each NPC
    // takes the marked enemy nearest it, whoever marked it (NearestMark, the soldiers' and the gunners' pick).
    {
        Reset();config.enabled=true;config.customNpcAi=true;
        unsigned char foeCtrl[0x10]{};Put<void*>(other,kSelfCtrl,foeCtrl);Put<long>(foeCtrl,8,1);Put<long>(foeCtrl,0xC,1);
        static unsigned char teamFoe[0x400]{},plainFoe[0x400]{};
        world.enemies=3;
        world.enemy[0]=Enemy{plainFoe,{0,0,5},1};
        world.enemy[1]=Enemy{other,{0,0,100},1};
        world.enemy[2]=Enemy{teamFoe,{0,0,20},1};
        const float from[3]={0,0,0},distant[3]={0,0,150};
        Expect(!NearestMark(from),"nothing marked: no marked enemy, the nearest plain one not taken for one");
        teamMarks[0]=teamFoe;teamMarkCount=1;
        Expect(NearestMark(from)==&world.enemy[2],"a teammate's mark alone: the NPCs go for it as for their own");
        const float seen[3]={0,0,100};
        Expect(NpcMarkEnemy(other,seen,true),"this machine marks another enemy too");
        Expect(NearestMark(from)==&world.enemy[2] && NearestMark(distant)==&world.enemy[1],
               "two marks: each NPC takes the one nearest it, whoever marked it");
        world.enemy[2].aim[2]=100.0f;
        Expect(NearestMark(from)==&world.enemy[1],"as near as each other: this machine's first");
        teamMarks[0]=plainFoe;world.enemy[2].aim[2]=20.0f;
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
        Arms packArms=ArmsOf(human);
        Expect(packArms.n==2 && packArms.arm[0].ready && !packArms.arm[1].ready && packArms.arm[1].reach==0.0f && LongestReach(packArms)==150.0f &&
               LargestBlast(packArms)==0.0f,"the backpack's grenade is out of the arms it picks, fires and fights at (production ArmsOf)");
        packArms.held[0]=0;
        auto* soldier=Entry(human,now);soldier->control=npc::Control::free;
        static unsigned char foe[3][0x300]{},foeCtrl[3][0x10]{};
        for(int i=0;i<3;++i){Put<void*>(foe[i],kSelfCtrl,foeCtrl[i]);Put<long>(foeCtrl[i],8,1);Put<long>(foeCtrl[i],12,1);}
        world.enemies=3;world.enemy[0]=Enemy{foe[0],{-2,0,24},1};world.enemy[1]=Enemy{foe[1],{0,0,25},1};world.enemy[2]=Enemy{foe[2],{2,0,26},1};
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
        packCanUse=false;world.enemy[0].aim[0]=0.0f;
        Put<float>(human,kViewPitch,std::atan2(eyePoint[1],24.0f));Put<float>(human,kViewYaw,0.0f);
        drive();
        Expect(packUses==0 && At<unsigned char>(human,kTrigger)==1 && std::fabs(At<float>(human,kLookPitch))<0.2f,
               "native backpack gate closed: the main rifle keeps aiming and firing (production Drive)");
        const float beforePitch=At<float>(human,kLookPitch),beforeYaw=At<float>(human,kLookYaw);
        human[kTrigger]=0;
        drive();
        Expect(packUses==0 && At<unsigned char>(human,kTrigger)==1 && At<float>(human,kLookPitch)==beforePitch && At<float>(human,kLookYaw)==beforeYaw,
               "a closed backpack gate does not starve the main rifle on subsequent frames");
        world.enemy[0].aim[0]=-2.0f;packCanUse=true;
        Put<float>(human,kViewPitch,0.0f);Put<float>(human,kViewYaw,0.0f);
        arcWallZ=12.0f;Put<float>(human,kLookPitch,123.0f);human[kTrigger]=1;
        Backpack(*soldier,human,&world.enemy[0],eyePoint,kMaskLook,now);
        Expect(!packUses && human[kTrigger]==1 && At<float>(human,kLookPitch)==123.0f,
               "a wall beyond six metres forbids the grenade before taking the main weapon's aim / trigger (production Backpack)");
        arcWallZ=0.0f;
        const float mid=flight*0.5f;
        world.friends=1;world.frObject[0]=other;
        world.fr[0]=npc::Friend{{-1.0f,eyePoint[1]+36.0f*std::sin(pitch)*mid-0.5f*14.7f*mid*mid,12.0f},0.8f};
        Backpack(*soldier,human,&world.enemy[0],eyePoint,kMaskLook,now);
        Expect(!packUses && human[kTrigger]==1 && At<float>(human,kLookPitch)==123.0f,
               "a friend midway along the actual lifted arc forbids the grenade before taking aim (production Backpack)");
        world.friends=0;
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
        now+=npcpack::kThrowGapMs;frame+=npcpack::kHoldFrames;world.enemies=1;onIt();drive();
        Expect(packUses==2,"one small enemy: no grenade for it");
        world.enemy[0].points=3;onIt();drive();
        Expect(packUses==3,"one large one: the grenade");
        now+=npcpack::kThrowGapMs;frame+=npcpack::kHoldFrames;world.enemies=3;world.enemy[0].points=1;
        world.friends=1;world.frObject[0]=other;world.fr[0]=npc::Friend{{-1,0,21},0.8f};onIt();drive();
        Expect(packUses==3,"a friend where it lands: held");
        world.friends=0;config.npcBackpack=false;onIt();drive();
        Expect(packUses==3,"NpcBackpack=0: carried, never used");
        config.npcBackpack=true;Put<std::int32_t>(grenade,kWeaponAmmo,0);onIt();drive();drive();
        Expect(packUses==3 && packReloads==1,"empty: no throw, its reload asked for once");
        Put<std::int32_t>(grenade,kWeaponAmmo,3);onIt();drive();
        Expect(packUses==4,"loaded again: thrown");
        packReady=false;packSlots=0;Put<unsigned char**>(human,kWeapons,nullptr);Put<std::uint64_t>(human,kWeaponCount,0);
        world.enemies=0;ResetNpcAi();
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
    VirtualFree(image,0,MEM_RELEASE);return failures ? 1 : 0;
}
#endif
