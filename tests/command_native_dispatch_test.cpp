// Real command bytes -> production RPC receiver -> native ID resolver -> actual
// npcai.cpp ForRequester -> recording native follow/ride entry points. No game.
#define main ExistingNpcCoreChecks
#include "../tools/npc_core_check.cpp"
#undef main
#include "../src/command_protocol.h"
#include <vector>
namespace rpc_fixture {
using namespace crew;
unsigned char ownerPlayer[0x2200]{},enemy[0x2200]{},users[2][0x50]{},userControls[2][16]{};
unsigned char controls[5][16]{},netControls[5][16]{},entries[5][0x28]{},ids[5][32]{};
unsigned char manager[0x50]{},teams[7*0x38]{},status[0x15000]{};
const char authClient[65]="client-one",authHost[65]="host";
unsigned char* actors[5]{};
std::vector<command_net::Message> replies;
unsigned executed=0;
std::uint64_t wall=10000;
constexpr unsigned kAllTeams=0x5E0C80,kTeamsGlobal=0x20B2978,kStatusGlobal=0x20B2890;
const unsigned char enumSignature[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83};
void Check(bool passed,const char* why){Expect(passed,why);if(!passed)std::exit(1);}
void __fastcall Walk(void*,void* visitor) {
    const auto table=*static_cast<void***>(visitor);
    for(auto* actor:actors)reinterpret_cast<void(__fastcall*)(void*,const unsigned char*)>(table[1])(visitor,actor);
}
void SetActor(unsigned index,unsigned char* actor,bool playerActor,int playerIndex) {
    actors[index]=actor;Put<void*>(actor,0,image+kSoldiers[0].vtable);actor[kDead]=0;
    Put<int>(actor,kTeam,playerActor ? 0 : kTeamFriend);Put<void*>(actor,kSelf,actor);
    Put<void*>(actor,kSelfCtrl,controls[index]);Put<LONG>(controls[index],8,1);Put<LONG>(controls[index],12,1);
    Put<unsigned>(actor,0x128,index==1 ? 1u : 2u);Put<void*>(actor,0x130,netControls[index]);
    Put<LONG>(netControls[index],0,1);Put<void*>(netControls[index],8,entries[index]);
    ids[index][0]=static_cast<unsigned char>(index+1);ids[index][12]=5;ids[index][24]=0x55;
    std::memcpy(entries[index]+8,ids[index],32);std::memset(entries[index]+28,0xCC,4);
    if(playerActor) {
        actor[kHumanPlayer]=1;Put<void*>(actor,kHumanPad,index==1 ? nullptr : actor);
        auto* user=users[playerIndex];Put<void*>(actor,0x1ED0,user);Put<void*>(actor,0x1ED8,userControls[playerIndex]);
        Put<LONG>(userControls[playerIndex],8,1);Put<void*>(user,0x18,user);Put<int>(user,0x48,playerIndex);
    }
}
bool Send(std::uint32_t peer,const void* bytes,std::size_t size) noexcept {
    command_net::Message message;
    if(peer!=1 || !command_net::Decode(bytes,size,message))return false;
    replies.push_back(message);return true;
}
NpcCommandResult Actual(const ObjRef& unit,const mapcmd::Command& command,const ObjRef& requester,const ObjRef& target) noexcept {
    ++executed;return NpcSquadCommandForRequester(unit,command,requester,target);
}
void Setup() {
    Reset();ResetCommandNetwork();replies.clear();executed=0;wall+=1000;
    std::memset(ownerPlayer,0,sizeof(ownerPlayer));std::memset(enemy,0,sizeof(enemy));
    std::memset(users,0,sizeof(users));std::memset(userControls,0,sizeof(userControls));
    std::memset(controls,0,sizeof(controls));std::memset(netControls,0,sizeof(netControls));
    std::memset(entries,0,sizeof(entries));std::memset(ids,0,sizeof(ids));
    SetActor(0,human,false,0);SetActor(1,other,true,1);SetActor(2,ownerPlayer,true,0);SetActor(3,enemy,false,0);SetActor(4,dead,false,0);
    Put<void*>(human,kLeader,nullptr);Put<void*>(dead,kLeader,nullptr);Put<void*>(dead,kFollowers,nullptr);
    Put<int>(enemy,kTeam,1);Put<unsigned>(human,kHumanMask,1);Put<unsigned>(dead,kHumanMask,1);
    playerObj=ownerPlayer;sessionOn=true;host=true;flatFloor=true;config.npcEvade=false;
    SeeSquad(human,human,0,npc::Control::free,now);SeeSquad(dead,dead,0,npc::Control::free,now);
    Put<void*>(image,kTeamsGlobal,manager);Put<void*>(manager,0x38,teams);
    Put<void*>(image,kStatusGlobal,status);Put<unsigned>(status,0x14FF8,2);
    ConfigureNpcCommandNetwork(&Actual);
    UpdateCommandNetwork({true,true,99,0,1,authHost,&Send},wall);
    command_net::Message hello;hello.epoch=99;unsigned char bytes[command_net::kWireSize];
    Check(command_net::Encode(hello,bytes,sizeof(bytes)) && ReceiveCommandNetwork(1,authClient,bytes,sizeof(bytes),wall),"negotiate production command capability");
}
command_net::Message Request(mapcmd::Order order,unsigned sequence=1) {
    command_net::Message message;message.kind=command_net::Kind::request;message.epoch=99;message.sequence=sequence;
    message.request.count=1;message.request.command={order,{40,0,0}};
    std::memcpy(message.request.requester,ids[1],32);std::memcpy(message.request.units[0],ids[0],32);
    if(order==Order::focus)std::memcpy(message.request.focus,ids[3],32);
    return message;
}
command_net::Message Dispatch(const command_net::Message& request) {
    unsigned char bytes[command_net::kWireSize]{};
    Check(command_net::Encode(request,bytes,sizeof(bytes)) && ReceiveCommandNetwork(1,authClient,bytes,sizeof(bytes),wall),"receive actual canonical request bytes");
    Check(!replies.empty() && replies.back().kind==command_net::Kind::result,"native result returned through production reply serializer");
    return replies.back();
}
}
namespace crew {
void SupportNetTick() noexcept {}
bool SupportCommandRequesterMatches(void* puid,const char* sender) noexcept {
    return puid==rpc_fixture::users[1] && !std::strcmp(sender,rpc_fixture::authClient);
}
}
int main() {
    using namespace crew;using namespace rpc_fixture;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!image)return 2;
    Jump(kSetFollow,reinterpret_cast<const void*>(&FollowRec));Jump(kRideVehicle,reinterpret_cast<const void*>(&RideRec));
    std::memcpy(image+kAllTeams,enumSignature,sizeof(enumSignature));
    unsigned char thunk[]={0xC4,0,0x41,0x5E,0x5F,0x5E,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto walk=&Walk;std::memcpy(thunk+8,&walk,8);std::memcpy(image+kAllTeams+16,thunk,sizeof(thunk));
    Setup();auto recruit=Request(Order::recruit);auto reply=Dispatch(recruit);
    Check(reply.results[0].reason==0 && reply.results[0].affected==1 && follows==1 && At<void*>(human,kLeader)==other && PlayerHuman()!=other,
        "remote RPC recruits to actual authenticated requester, never host PlayerHuman");
    Dispatch(recruit);Check(executed==1 && follows==1,"retry never repeats a native follow side effect");
    Setup();Put<void*>(human,kLeader,ownerPlayer);SeeSquad(human,human,0,npc::Control::recruited,now);
    reply=Dispatch(Request(Order::follow));Check(reply.results[0].reason==static_cast<unsigned>(NpcCommandReason::notOwner) && follows==0,
        "actual NPC executor rejects stealing the host player's recruited squad");
    Setup();auto forged=Request(Order::recruit);std::memcpy(forged.request.requester,ids[2],32);reply=Dispatch(forged);
    Check(reply.results[0].reason==static_cast<unsigned>(NpcCommandReason::invalidRequester) && !executed && !follows,
        "native player ID cannot impersonate another authenticated PUID");
    Setup();reply=Dispatch(Request(Order::guard));auto* q=FindSquad(human);auto* soldier=Entry(human,now);
    soldier->control=npc::Control::free;Put<unsigned>(human,kControlMask,kMaskMove);
    Arms arms;arms.n=1;arms.arm[0]=npc::Arm{100,0,10,false,true,false};const float eye[3]={0,1.5f,0};
    for(int tick=0;tick<100 && At<float>(human,kMoveX)==0;++tick) {now+=16;++frame;world.frame=frame;Drive(*soldier,human,kSoldiers[0],arms,nullptr,eye,Pos(human),q,now);}
    Check(reply.results[0].reason==0 && q->cmd.order==Order::guard && At<float>(human,kMoveX)>0,
        "serialized guard reaches real NPC navigation and movement intent");
    Setup();markEnemy=enemy;reply=Dispatch(Request(Order::focus));
    Check(reply.results[0].reason==0 && FindSquad(human)->commandFocus.Is(enemy) && !mark.obj,
        "focus RPC targets its explicit enemy without overwriting host global mark");
    Setup();Put<void*>(human,kLeader,other);SeeSquad(human,human,0,npc::Control::recruited,now);
    Put<int>(vehicle,kTeam,0);Put<unsigned>(vehicle,0x128,1);Put<unsigned>(seats,kSeatClass,1);Put<unsigned>(seats,kSeatEnable,1);
    Put<void*>(other,kHumanRiding,vehicle);Put<void*>(other,kHumanVehicleCtrl,ctrl);Put<void*>(other,kHumanSeat,seats);
    Put<void*>(seats,kSeatRider,other);Put<const void*>(seats,kSeatRiderCtrl,At<const void*>(other,kSelfCtrl));
    world.friends=0;reply=Dispatch(Request(Order::board));soldier=Entry(human,now);
    Check(reply.results[0].reason==0 && soldier->boardV.Is(vehicle) && soldier->boardSeat==1 && !rides,
        "RPC assigns host NPC to guest requester's vehicle rather than host's empty preference");
    completeRide=true;Board(*soldier,human,Pos(human),now);
    Check(rides==1 && announcedBoards==1 && At<void*>(seats,kSeatRider)==other && At<void*>(seats+edf::kSeatStride,kSeatRider)==human,
        "accepted remote board order walks through real Board/native Ride and seat announcement");
    VirtualFree(image,0,MEM_RELEASE);return failures ? 1 : 0;
}
