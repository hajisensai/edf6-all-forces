// Execute npcai.cpp itself against stand-in memory and recording native entry points. No game is loaded or started.
#include <Windows.h>
namespace markinput {
bool down=false;
SHORT Key(int) noexcept { return down ? static_cast<SHORT>(0x8000) : 0; }
HWND Window() noexcept { return nullptr; }
DWORD Process(HWND,LPDWORD pid) noexcept { *pid=GetCurrentProcessId();return 1; }
}
#define GetAsyncKeyState markinput::Key
#define GetForegroundWindow markinput::Window
#define GetWindowThreadProcessId markinput::Process
#include "../src/npcai.cpp"
#undef GetAsyncKeyState
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace crew {
bool InstallRealDriverNative(NpcSeatInputOwnedFn) noexcept { return true; }
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
namespace {
Config config{};
ULONGLONG now=1000,frame=1;
bool sessionOn=false,host=true,door=true;
bool wall=false;
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
float MapRay(const float*,const float*,float* hit) noexcept {
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
// The lock registry's valid lock points whatever their lockable flag (the marked enemy out of sight): `lockAt` for `lockOf`.
const void* lockOf=nullptr;float lockAt[3]{};
bool VisitLockPoints(EnemyVisitor visit,void* ctx) noexcept { if(lockOf)visit(ctx,lockOf,lockAt);return true; }
unsigned char* chosenGunnerWeapon=nullptr;
bool suppressGunnerChoice=false;
PayloadFire chosenGunnerFire=PayloadFire::primary;
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
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    Jump(kSetFollow,reinterpret_cast<const void*>(&FollowRec));Jump(kRideVehicle,reinterpret_cast<const void*>(&RideRec));
    Jump(kNotifyBox,reinterpret_cast<const void*>(&NotifyRec));Jump(kApplyBox,reinterpret_cast<const void*>(&ApplyRec));Jump(kHealHuman,reinterpret_cast<const void*>(&HealRec));
    Jump(kNodePos,reinterpret_cast<const void*>(&NodePosRec));
    Reset();Put<int>(human,kTeam,1);PreThink(human);
    Expect(follows==0,"enemy soldiers are never reorganized by friendly NPC AI");
    Reset();Put<unsigned>(human,kObjectFlags,kFixPosition);PreThink(human);
    Expect(follows==0,"a fixed-position scripted follower keeps its native leader transition");
    Reset();PreThink(human);Expect(follows==1,"an ordinary friendly remnant still elects its leader");
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
        const Arms both=ArmsOf(human);
        Expect(both.n==3 && both.held[0]==2 && both.held[1]==1,"each WeaponSet's held weapon is found at the native set stride");
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
    // A panel order lives only under the lead it was given in (npc::LeadOf).
    Reset();playerObj=other;Put<unsigned char>(other,kHumanPlayer,1);Put<void*>(other,kHumanPad,other);
    Put<void*>(human,kLeader,other);q=SeeSquad(human,human,0,npc::Control::recruited,now);
    Expect(SquadCommand(human,Command{Order::dismiss,{}}) && q->cmd.order==Order::guard,"a dismissed squad holds where it was let go");
    ++frame;++now;SeeSquad(human,human,0,npc::Control::free,now);
    Expect(q->cmd.order==Order::guard,"the dismissal's hold stays while the squad is its own");
    now+=static_cast<ULONGLONG>(config.npcRecruitCooldownSec*1000.0f)+1;++frame;SeeSquad(human,human,0,npc::Control::free,now);
    Expect(!q->dismissed && q->cmd.order==Order::guard,"the cooldown ending does not move it by itself");
    ++frame;++now;SeeSquad(human,human,0,npc::Control::recruited,now);
    Expect(q->cmd.order==Order::none,"recruited again by the stock walk-up after the cooldown: the hold is dropped and it follows");
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
        const auto assignedAt=child->boardAt;++now;
        result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::board,{}},ObjRef::Of(other),{});
        Expect(result.Accepted() && child->boardAt==assignedAt,"an already valid boarding assignment is not reported as failure or restarted");
        completeRide=true;Board(*child,dead,Pos(dead),now);
        Expect(rides==1 && announcedBoards==1 && At<void*>(dead,kHumanRiding)==vehicle && At<void*>(seats,kSeatRider)==dead,
            "accepted online command reaches real Board/native Ride entry and announces the completed seat transition");
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
        Expect(result.Accepted() && fights(behavior.move),"an attack-move fights the enemy in reach on its way");
        world.enemies=0;
        behavior=Drive(*soldier,human,kSoldiers[0],available,nullptr,eyePoint,Pos(human),commandSquad,now);
        Expect(std::strcmp(behavior.move,"attack-move")==0,"an attack-move with nothing in reach walks on to its point");
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
            result=NpcSquadCommandForRequester(ObjRef::Of(human),Command{Order::move,{40,0,0}},ObjRef::Of(other),{});
            Expect(!result.Accepted(),"a seated squad's soldiers take no map move (its vehicle does)");
        } else Expect(false,"the stand-in ride makes the soldier seated");
        Put<void*>(human,kHumanRiding,nullptr);Put<void*>(human,kHumanVehicleCtrl,nullptr);
        Expect(SquadRecruitable(*commandSquad),"on foot again, a free squad: recruitment offered");
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
    // Fireteams (SplitSquad, MergeSquads): a recruited squad of four (its top and three followers in its +0x550 list)
    // split: the odd places under the first of them, who follows the player; the even under the top; merged back.
    {
        static unsigned char top[0x2200]{},f1[0x2200]{},f2[0x2200]{},f3[0x2200]{},list[0x18]{},nodes[3][0x18]{};
        Reset();
        unsigned char* const team[4]={top,f1,f2,f3};
        for(auto t:team){std::memset(t,0,0x2200);Put<void*>(t,0,image+kSoldiers[0].vtable);Put<int>(t,kTeam,kTeamFriend);}
        Put<void*>(top,kLeader,other);   // `other` stands for the player
        other[edf::kHumanPlayer]=1;Put<void*>(other,edf::kHumanPad,other);
        Put<void*>(top,kFollowers,list);Put<void*>(list,0,nodes[0]);
        for(int i=0;i<3;++i){Put<void*>(nodes[i],0,i<2 ? static_cast<void*>(nodes[i+1]) : static_cast<void*>(list));Put<void*>(nodes[i],0x10,team[i+1]);Put<void*>(team[i+1],kLeader,top);}
        SeeSquad(top,top,0,npc::Control::recruited,now);
        sessionOn=true;
        Expect(SplitSquad(top)==-1 && follows==0,"online: no split (the panel's orders are offline only)");
        sessionOn=false;
        Put<void*>(f3,kRoute,f3);
        Expect(SplitSquad(top)==-1 && follows==0,"a scripted follower blocks the whole split before any reparenting");
        Put<void*>(f3,kRoute,nullptr);
        Soldier* const boarding=Entry(f1,now);boarding->boardV=ObjRef::Of(vehicle);boarding->boardSeat=1;
        nextFollowWatch=boarding;
        Expect(SplitSquad(top)==2,"a squad of four splits two and two");
        Expect(!boarding->boardV && !boardingAtFollow,"splitting cancels both halves' boarding requests before changing native follow lists");
        Expect(FollowedBy(f1)==other && FollowedBy(f3)==f1 && FollowedBy(f2)==top && FollowedBy(top)==none,
               "the new team's leader follows the player, its other soldier it; the one staying follows the top");
        // Merged back: the new team's top under the old one (another squad entry of the panel).
        SeeSquad(f1,f1,0,npc::Control::recruited,now);follows=0;
        Expect(MergeSquads(top,f1) && FollowedBy(f1)==top && follows==1,"joined: the other team's top follows this one's");
        Expect(!FindSquad(f1) && !MergeSquads(f1,top) && follows==1,
               "a merged top is retired immediately: reversing the merge cannot create a follow cycle");
        Expect(!MergeSquads(top,top),"a squad does not join itself");
        Reset();Put<void*>(human,kLeader,nullptr);SeeSquad(human,human,0,npc::Control::free,now);
        Expect(SplitSquad(human)==-1 && follows==0,"one soldier: nothing to split");
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
    VirtualFree(image,0,MEM_RELEASE);return failures ? 1 : 0;
}
#endif
