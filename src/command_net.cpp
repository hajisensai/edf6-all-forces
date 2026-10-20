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
        if(!IsSoldierClass(ids.units[i].obj) && !NpcDrivable(static_cast<const unsigned char*>(ids.units[i].obj)))result={NpcCommandReason::unsupported,0};
        else if(!IsOnlineAuthority(ids.units[i].obj))result={NpcCommandReason::notAuthority,0};
        else if(!ground)result={NpcCommandReason::noTarget,0};
        // The map's own entry for a unit this machine is the authority for (mapcmd.cpp): the queue kept here, the
        // order given by the executor, exactly as the host's own map gives its own units theirs.
        else result=MapCommandAuthority(ids.units[i],command,ids.requester,ids.focus,(request.flags&command_net::kQueued)!=0,executor);
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
std::uint32_t MapCommandNetworkVersion() noexcept {return commands.HostVersion();}
void SyncMapCommandWays(const MapCommandWays* list,unsigned count) noexcept {
    if(!context.ready || !context.host)return;
    unsigned char ids[command_net::Session::kWaysUnits][32]{};unsigned published=0;
    for(unsigned i=0;i<count && published<command_net::Session::kWaysUnits;++i) {
        if(!Id(ObjRef{list[i].obj,list[i].ctrl},ids[published]))continue;   // no native identity (not registered): nothing a client can match
        commands.PublishWays(ids[published],list[i].at,list[i].n<command_net::kWaysMost ? list[i].n : command_net::kWaysMost);
        ++published;
    }
    unsigned char out[command_net::Session::kWaysUnits][32];
    const unsigned n=commands.PublishedWays(out,command_net::Session::kWaysUnits);
    for(unsigned i=0;i<n;++i) {
        bool now=false;
        for(unsigned k=0;k<published && !now;++k)now=!std::memcmp(out[i],ids[k],32);
        if(!now)commands.PublishWays(out[i],nullptr,0);   // its queue gone (or its unit): cleared on every map
    }
}
unsigned ReadMapCommandWays(const ObjRef& unit,float (*at)[3],unsigned most) noexcept {
    if(!context.ready || context.host || !at)return 0;
    unsigned char id[32];command_net::Ways ways;
    if(!Id(unit,id) || !commands.WaysOf(id,&ways))return 0;
    const unsigned n=ways.n<most ? ways.n : most;
    for(unsigned i=0;i<n;++i)for(unsigned c=0;c<3;++c)at[i][c]=ways.at[i][c];
    return n;
}
bool ReadMapCommandNetworkResult(CommandNetworkResult* out) noexcept {
    if(!out)return false;*out=commands.Result();return out->request!=0;
}
std::uint32_t SubmitMapCommand(const ObjRef& requester,const ObjRef* units,unsigned count,
    const mapcmd::Command& command,const ObjRef& focus,wchar_t* note,std::size_t noteSize,
    const std::uint32_t* formationSlots,std::uint32_t formationTotal,bool queued) noexcept {
    SupportNetTick();
    if(!commands.Ready()) {Note(note,noteSize,L"房主尚未启用联机 NPC 指挥，请确认版本并等待本关同步");return 0;}
    if(!units || !count || count>kCommandNetUnits) {Note(note,noteSize,L"请选择 1～16 个 NPC 小队，超出数量不会截断执行");return 0;}
    command_net::Request request;request.count=count;request.command=command;request.flags=queued ? command_net::kQueued : 0u;
    // A host of version 6 reads neither the queue's flag nor the formation order: refused here, with why, never sent as
    // an immediate order or a guard instead (command_protocol.h Version 7).
    if(command_net::RequestVersion(request)>commands.HostVersion()) {
        Note(note,noteSize,command.order==mapcmd::Order::formation ? L"房主的插件版本较旧，不支持联机阵形指令，请房主更新后再试" :
                                                                     L"房主的插件版本较旧，不支持 Shift 排队路点（未发送，单位保持原命令）");
        return 0;
    }
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
