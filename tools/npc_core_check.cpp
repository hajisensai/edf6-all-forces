// Execute npcai.cpp itself against stand-in memory and recording native entry points. No game is loaded or started.
#include "../src/npcai.cpp"
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace crew {
unsigned char* image=nullptr;
PlayerFix player{};
namespace {
Config config{};
ULONGLONG now=1000,frame=1;
bool online=false,host=true,door=true;
bool wall=false;
float doorAt[3]{},doorReach=3.0f;
unsigned char human[0x2200]{},dead[0x2200]{},other[0x2200]{},vehicle[0x3000]{},seats[edf::kSeatStride*2]{};
unsigned char head[0x18]{},node[0x18]{},ctrl[0x10]{};
int follows=0,rides=0,failures=0;
void Expect(bool pass,const char* what) { std::printf("%s: %s\n",pass ? "PASS" : "FAIL",what);if(!pass)++failures; }
void __fastcall FollowRec(void* self,void* leader,bool) { Put<void*>(self,kLeader,leader);++follows; }
void __fastcall RideRec(void*,SharedRef* ref,int) { --*reinterpret_cast<int*>(static_cast<unsigned char*>(ref->ctrl)+8);++rides; }
void Jump(unsigned rva,const void* to) { auto p=image+rva;p[0]=0x48;p[1]=0xB8;std::memcpy(p+2,&to,8);p[10]=0xFF;p[11]=0xE0; }
void Reset() {
    ResetNpcAi();config=Config{};now=1000;frame=1;online=false;host=true;door=true;wall=false;follows=rides=0;
    std::memset(human,0,sizeof(human));std::memset(dead,0,sizeof(dead));std::memset(other,0,sizeof(other));
    std::memset(vehicle,0,sizeof(vehicle));std::memset(seats,0,sizeof(seats));std::memset(ctrl,0,sizeof(ctrl));
    for(auto p : {human,dead,other}) { Put<void*>(p,0,image+kSoldiers[0].vtable);Put<int>(p,kTeam,kTeamFriend); }
    Put<void*>(human,kLeader,dead);dead[kDead]=1;
    Put<void*>(dead,kFollowers,head);Put<void*>(head,0,node);Put<void*>(node,0,head);Put<void*>(node,0x10,human);
    Put<void*>(vehicle,kSeats,seats);Put<std::uint64_t>(vehicle,kSeatCount,2);Put<void*>(vehicle,kSelfCtrl,ctrl);Put<int>(ctrl,8,1);
    Put<unsigned>(human,kHumanMask,1);Put<unsigned>(seats+edf::kSeatStride,kSeatClass,1);Put<unsigned>(seats+edf::kSeatStride,kSeatEnable,1);
    doorAt[0]=doorAt[1]=doorAt[2]=0;doorReach=3.0f;
    ok=followOk=rideOk=true;world.frame=frame;config.npcSquadSuccession=true;
}
}
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept { return now; }
ULONGLONG GameFrame() noexcept { return frame; }
void SeeFrame(const void*) noexcept {}
bool InSession() noexcept { return online; }
bool IsRoomHost() noexcept { return host; }
unsigned char* PlayerHuman() noexcept { return nullptr; }
bool CameraRay(float*,float*) noexcept { return false; }
bool HumanOnFoot(const unsigned char* h) noexcept { return !At<void*>(h,kHumanVehicleCtrl); }
bool MapHoldsKeys() noexcept { return true; }
bool KnownVehicle(const void* v) noexcept { return v==vehicle; }
float MapRay(const float*,const float*,float* hit) noexcept {
    if(!wall)return -1.0f;hit[0]=0;hit[1]=1.5f;hit[2]=20;return 20;
}
bool SeatPoint(const unsigned char*,unsigned,float* at,float* reach) noexcept {
    if(!door)return false;std::memcpy(at,doorAt,12);*reach=doorReach;return true;
}
bool VisitEnemiesOf(std::int32_t,EnemyVisitor,void*) noexcept { return true; }
bool VisitEnemies(const unsigned char*,EnemyVisitor,void*) noexcept { return true; }
}
int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    Jump(kSetFollow,reinterpret_cast<const void*>(&FollowRec));Jump(kRideVehicle,reinterpret_cast<const void*>(&RideRec));
    Reset();Put<int>(human,kTeam,1);PreThink(human);
    Expect(follows==0,"enemy soldiers are never reorganized by friendly NPC AI");
    Reset();Put<unsigned>(human,kObjectFlags,kFixPosition);PreThink(human);
    Expect(follows==0,"a fixed-position scripted follower keeps its native leader transition");
    Reset();PreThink(human);Expect(follows==1,"an ordinary friendly remnant still elects its leader");
    Reset();world.friends=1;world.frObject[0]=other;Put<std::uint64_t>(other,kFollowerCount,1);Put<unsigned>(other,kObjectFlags,kFixPosition);
    unsigned char* leaders[4];float places[4][3];int sizes[4];
    Expect(OtherSquads(dead,false,leaders,places,sizes,4)==0,"fixed script squads cannot absorb a remnant");
    Reset();online=true;config.scriptNpcSettleSec=0;Put<void*>(human,kLeader,nullptr);Put<void*>(human,kRoute,other);
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
    online=true;Expect(!SquadCommand(human,Command{Order::guard,{10,0,0}}) && q->cmd.order==Order::none,
                       "direct squad commands are rejected online");
    online=false;config.enabled=false;
    Expect(!SquadCommand(human,Command{Order::guard,{10,0,0}}),"direct squad commands respect the total switch");
    Reset();Put<std::uint8_t>(human,kNet,1);PreThink(human);
    Expect(follows==0,"remote-owned soldier succession remains native");
    Reset();online=true;host=false;PreThink(human);
    Expect(follows==0,"online guest cannot reorganize squads");
    Reset();Put<void*>(human,kLeader,nullptr);SeeSquad(human,human,0,npc::Control::free,now);
    Soldier* walking=Entry(human,now);walking->boardV=ObjRef::Of(vehicle);
    Expect(SquadCommand(human,Command{Order::dismount,{}}) && !walking->boardV,
           "dismount cancels an in-flight boarding order before the soldier has reached its seat");
    walking->boardV=ObjRef::Of(vehicle);
    Expect(SquadCommand(human,Command{Order::guard,{10,0,0}}) && !walking->boardV,
           "a new guard order replaces the earlier walk-to-seat order");
    // A panel order lives only under the lead it was given in (npc::LeadOf).
    Reset();Put<void*>(human,kLeader,nullptr);q=SeeSquad(human,human,0,npc::Control::recruited,now);
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
    Reset();Put<void*>(human,kLeader,nullptr);q=SeeSquad(human,human,0,npc::Control::recruited,now);
    Expect(SquadCommand(human,Command{Order::guard,{10,0,0}}),"a recruited squad takes a guard order");
    ++frame;++now;SeeSquad(human,human,0,npc::Control::recruited,now);
    Expect(q->cmd.order==Order::guard,"a recruited squad told to guard keeps guarding while the player still leads it");
    ++frame;++now;SeeSquad(human,human,0,npc::Control::script,now);
    Expect(q->cmd.order==Order::none,"a script taking the squad drops the player's order");
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
    VirtualFree(image,0,MEM_RELEASE);return failures ? 1 : 0;
}
