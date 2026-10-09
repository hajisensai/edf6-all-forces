#include "command_protocol.h"
#include "command_identity.h"
#include "support_net.h"
#include "support_soldier.h"
#include "online_authority.h"
#include "memory.h"
#include <cstring>
#include <cwchar>
namespace crew {
namespace {
command_net::Session commands;
CommandNetworkContext context{};
NpcCommandExecutor executor=nullptr;
char localPuid[65]{};
bool Send(void*,std::uint32_t peer,const command_net::Message& message) noexcept {
    unsigned char bytes[command_net::kWireSize]{};
    return context.ready && context.send && command_net::Encode(message,bytes,sizeof(bytes)) && context.send(peer,bytes,sizeof(bytes));
}
void Reject(command_net::Message::Result* out,unsigned count,NpcCommandReason reason) noexcept {
    for(unsigned i=0;i<count;++i)out[i]={static_cast<std::uint32_t>(reason),0};
}
void Execute(void*,const char* sender,const command_net::Request& request,command_net::Message::Result* out) noexcept {
    Reject(out,request.count,NpcCommandReason::failed);
    if(!executor || !Cfg().enabled || !Cfg().customNpcAi){Reject(out,request.count,NpcCommandReason::disabled);return;}
    if(!InSession() || !OnlineHostOnly()){Reject(out,request.count,NpcCommandReason::notAuthority);return;}
    CommandIdentities ids{};
    const unsigned char* focus=request.command.order==mapcmd::Order::focus ? request.focus : nullptr;
    if(!ResolveCommandIdentities(request.requester,request.units,request.count,focus,&ids)) {
        Reject(out,request.count,NpcCommandReason::notFound);return;
    }
    if(!SupportCommandRequesterMatches(ids.requesterPuid,sender)) {
        Reject(out,request.count,NpcCommandReason::invalidRequester);return;
    }
    // Resolve once under the native registry's lock, then release it before any
    // game operation. ForRequester validates ownership/script state again at the
    // point of mutation and uses this exact authenticated player for follow/board.
    for(unsigned i=0;i<request.count;++i) {
        NpcCommandResult result{NpcCommandReason::unsupported,0};
        auto command=request.command;
        bool ground=true;
        if(mapcmd::PointOrder(command.order)) {
            const auto total=request.formationTotal ? request.formationTotal : request.count;
            const auto slot=request.formationTotal ? request.formationSlots[i] : i;
            mapcmd::Formation(static_cast<int>(slot),static_cast<int>(total),request.command.at,mapcmd::kFormationSpacing,command.at);
            float y=0;
            ground=MapGroundNear(command.at[0],command.at[2],command.at[1],&y,true) && std::isfinite(y);
            if(ground)command.at[1]=y;
        }
        if(!IsSoldierClass(ids.units[i].obj))result={NpcCommandReason::unsupported,0};
        else if(!IsOnlineAuthority(ids.units[i].obj))result={NpcCommandReason::notAuthority,0};
        else if(!ground)result={NpcCommandReason::noTarget,0};
        else result=executor(ids.units[i],command,ids.requester,ids.focus);
        if(result.reason>=NpcCommandReason::count || result.affected>256 ||
           ((result.reason==NpcCommandReason::none)!=(result.affected>0)))result={NpcCommandReason::failed,0};
        out[i]={static_cast<std::uint32_t>(result.reason),result.affected};
    }
}
void Backend() noexcept {commands.Configure({nullptr,&Send,executor ? &Execute : nullptr});}
void Note(wchar_t* note,std::size_t count,const wchar_t* text) noexcept {if(note && count)swprintf_s(note,count,L"%ls",text);}
bool Id(const ObjRef& ref,unsigned char* id) noexcept {
    __try {
        const auto* object=static_cast<const unsigned char*>(ref.obj);
        return object && Readable(object,kSelfCtrl+8) && ref.Is(object) && ReadNativeObjectId(object,id);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
void ConfigureNpcCommandNetwork(NpcCommandExecutor execute) noexcept {executor=execute;Backend();}
void ResetCommandNetwork() noexcept {commands.Reset();context={};localPuid[0]=0;Backend();}
void UpdateCommandNetwork(const CommandNetworkContext& next,std::uint64_t now) noexcept {
    context=next;
    if(next.localPuid && std::memchr(next.localPuid,0,sizeof(localPuid)))strcpy_s(localPuid,next.localPuid);
    else localPuid[0]=0;
    context.localPuid=localPuid;
    commands.Update(next.ready && localPuid[0],next.host,next.epoch,next.hostPeer,next.peerCount,now);
}
bool ReceiveCommandNetwork(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t now) noexcept {
    if(!command_net::Owned(bytes,size))return false;
    command_net::Message message;
    if(command_net::Decode(bytes,size,message))commands.Receive(peer,puid,message,now);
    return true; // malformed or newer command versions never leak to the support parser
}
bool MapCommandNetworkReady() noexcept {SupportNetTick();return commands.Ready();}
bool ReadMapCommandNetworkResult(CommandNetworkResult* out) noexcept {
    if(!out)return false;*out=commands.Result();return out->request!=0;
}
std::uint32_t SubmitMapCommand(const ObjRef& requester,const ObjRef* units,unsigned count,
    const mapcmd::Command& command,const ObjRef& focus,wchar_t* note,std::size_t noteSize,
    const std::uint32_t* formationSlots,std::uint32_t formationTotal) noexcept {
    SupportNetTick();
    if(!commands.Ready()) {Note(note,noteSize,L"房主尚未启用联机 NPC 指挥，请确认版本并等待本关同步");return 0;}
    if(!units || !count || count>kCommandNetUnits) {Note(note,noteSize,L"请选择 1～16 个 NPC 小队，超出数量不会截断执行");return 0;}
    command_net::Request request;request.count=count;request.command=command;
    if(formationSlots && formationTotal) {
        request.formationTotal=formationTotal;std::memcpy(request.formationSlots,formationSlots,count*sizeof(std::uint32_t));
    } else if(formationSlots || formationTotal) {Note(note,noteSize,L"守点阵形选择已失效，请重新选择");return 0;}
    if(!Id(requester,request.requester)) {Note(note,noteSize,L"当前玩家缺少有效的本关联机身份");return 0;}
    for(unsigned i=0;i<count;++i)if(!Id(units[i],request.units[i])) {
        Note(note,noteSize,L"所选单位已失效或没有联机身份，请重新选择");return 0;
    }
    if(command.order==mapcmd::Order::focus && !Id(focus,request.focus)) {
        Note(note,noteSize,L"攻击目标已失效或没有联机身份，请重新标记");return 0;
    }
    if(!command_net::ValidRequest(request)) {Note(note,noteSize,L"命令目标或单位选择无效");return 0;}
    const auto id=commands.Submit(request,localPuid,GetTickCount64());
    if(!id){Note(note,noteSize,L"上一条指挥请求仍在等待房主结果");return 0;}
    Note(note,noteSize,commands.Result().state==CommandNetworkState::pending ? L"已请求房主执行，等待真实指挥结果" : L"指挥请求已有结果");
    return id;
}
} // namespace crew
