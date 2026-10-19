#include "pvp.h"
#include "pvp_native.h"
#include "coop_member_attribute_api.h"
#include "crew.h"
#include "hudtext.h"
#include <atomic>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
CommandNetworkContext network{};
char localPuid[65]{};
std::atomic<int> queued{0}; // 1 mode off, 2 mode on, 3 team 1, 4 team 2
SRWLOCK viewLock=SRWLOCK_INIT;
PvpStatus view{};
PvpMember members[pvp_protocol::kMaxMembers]{};
unsigned memberCount=0;
struct PendingRequest { int command=0; std::uint64_t epoch=0; std::uint32_t revision=0; char puid[65]{}; };
PendingRequest request{};
std::atomic<bool> staleRequest{false};
SRWLOCK advertiseLock=SRWLOCK_INIT;
bool resetCleanupPending=false,forgetSupportPending=false;
ULONGLONG lastPvpFrame=~0ull;
pvp_protocol::Result lastLogged=pvp_protocol::Result::Idle;

bool Send(void*,std::uint32_t peer,const void* data,std::size_t size) noexcept {
    return network.ready && network.send && network.send(peer,data,size);
}
bool Prepare(void*,const pvp_protocol::State& state) noexcept { return PvpNativePrepare(state); }
bool Apply(void*,const pvp_protocol::State& state) noexcept { return PvpNativeApply(state); }
pvp_protocol::Protocol protocol({nullptr,&Send,&Prepare,&Apply});

const wchar_t* ResultNote(pvp_protocol::Result result) noexcept {
    using R=pvp_protocol::Result;using hudtext::Tr;using hudtext::Tx;
    switch(result) {
    case R::Pending:return Tr(Tx::pvpPending);
    case R::Complete:return Tr(Tx::pvpComplete);
    case R::NotReady:return Tr(Tx::pvpNotReady);
    case R::Busy:return Tr(Tx::pvpBusy);
    case R::InvalidArgument:return Tr(Tx::pvpInvalid);
    case R::UnsupportedRoster:return Tr(Tx::pvpRoster);
    case R::ProtocolError:return Tr(Tx::pvpChanged);
    case R::PrepareFailed:return Tr(Tx::pvpPrepareFailed);
    case R::ApplyFailed:return Tr(Tx::pvpApplyFailed);
    case R::SendFailed:return Tr(Tx::pvpSendFailed);
    case R::TimedOut:return Tr(Tx::pvpTimedOut);
    case R::Aborted:return Tr(Tx::pvpAborted);
    case R::SessionLost:return Tr(Tx::pvpSessionLost);
    case R::Faulted:return Tr(Tx::pvpFaulted);
    case R::Idle:return Tr(Tx::pvpReady);
    }
    return L"";
}
void PublishView() noexcept {
    PvpStatus next{};
    next.ready=Cfg().enabled && protocol.Ready();next.host=network.host;
    next.enabled=protocol.Current().enabled;next.pending=protocol.Busy() || queued.load()!=0;
    next.revision=protocol.Revision();next.result=protocol.LastResult();next.epoch=protocol.WireEpoch();
    std::memcpy(next.localPuid,localPuid,sizeof(next.localPuid));
    const auto& state=protocol.Current();
    PvpMember nextMembers[pvp_protocol::kMaxMembers]{};
    for(unsigned i=0;i<state.count && i<pvp_protocol::kMaxMembers;++i) {
        auto& member=nextMembers[i];
        std::memcpy(member.puid,state.entries[i].puid,sizeof(member.puid));
        member.team=state.entries[i].team;member.local=localPuid[0] && !std::strcmp(member.puid,localPuid);
        if(member.local)next.localTeam=member.team;
    }
    swprintf_s(next.note,L"%ls",ResultNote(next.result));
    if(protocol.Restoring())swprintf_s(next.note,L"%ls",hudtext::Tr(hudtext::Tx::pvpApplyFailed));
    if(staleRequest.load()) { next.result=pvp_protocol::Result::ProtocolError;
        swprintf_s(next.note,L"%ls",hudtext::Tr(hudtext::Tx::pvpChanged)); }
    AcquireSRWLockExclusive(&viewLock);
    view=next;memberCount=state.count;
    std::memcpy(members,nextMembers,sizeof(members));
    ReleaseSRWLockExclusive(&viewLock);
    if(next.result!=lastLogged) {
        if(next.result!=pvp_protocol::Result::Idle && next.result!=pvp_protocol::Result::NotReady)
            Log("PVP result=%u enabled=%d ready=%d pending=%d revision=%u team=%d",
                static_cast<unsigned>(next.result),next.enabled,next.ready,next.pending,next.revision,next.localTeam);
        lastLogged=next.result;
    }
}
void AdvertiseChecked() noexcept {
    // Capability/preselection only. Lobby attributes can arrive seconds apart;
    // they never change gameplay teams. All runtime changes use Prepare/Apply ACKs.
    static HMODULE module=nullptr;
    static const EDF6CoopMemberAttributeApi* api=nullptr;
    static bool published=false;
    const HMODULE current=GetModuleHandleW(L"EDF6Coop.dll");
    if(current!=module){module=current;api=nullptr;published=false;}
    if(!current)return;
    if(!api) {
        const auto get=reinterpret_cast<EDF6CoopGetMemberAttributeApiFn>(GetProcAddress(current,"EDF6CoopGetMemberAttributeApi"));
        api=get ? get(EDF6COOP_MEMBER_ATTRIBUTE_VERSION) : nullptr;
        if(!api || !EDF6COOP_MA_HAS(api,publish) || !api->publish){api=nullptr;return;}
    }
    if(!published)published=api->publish("EDF6X_PVP","1")!=0;
}
void Advertise() noexcept {
    AcquireSRWLockExclusive(&advertiseLock);AdvertiseChecked();ReleaseSRWLockExclusive(&advertiseLock);
}
bool QueueAt(int command,bool hostOnly,std::uint64_t epoch,std::uint32_t revision,const char* puid) noexcept {
    if(!puid || !std::memchr(puid,0,65))return false;
    AcquireSRWLockExclusive(&viewLock);
    const bool accepted=view.ready && !view.pending && !queued.load() && (!hostOnly || view.host) &&
        view.epoch==epoch && view.revision==revision && !std::strcmp(view.localPuid,puid);
    if(accepted) {
        request.command=command;request.epoch=epoch;request.revision=revision;
        std::memcpy(request.puid,puid,std::strlen(puid)+1);queued.store(command);staleRequest.store(false);
    }
    ReleaseSRWLockExclusive(&viewLock);return accepted;
}
}

bool InstallPvp() noexcept { return InstallPvpNative(); }
bool SetPvpEnabledAt(bool enabled,std::uint64_t epoch,std::uint32_t revision,const char* puid) noexcept {
    return QueueAt(enabled ? 2 : 1,true,epoch,revision,puid);
}
bool SetPvpLocalTeamAt(int team,std::uint64_t epoch,std::uint32_t revision,const char* puid) noexcept {
    return (team==1 || team==2) && QueueAt(team==1 ? 3 : 4,false,epoch,revision,puid);
}
bool SetPvpEnabled(bool enabled) noexcept {
    PvpStatus current{};ReadPvpStatus(&current);
    return SetPvpEnabledAt(enabled,current.epoch,current.revision,current.localPuid);
}
bool SetPvpLocalTeam(int team) noexcept {
    PvpStatus current{};ReadPvpStatus(&current);
    return SetPvpLocalTeamAt(team,current.epoch,current.revision,current.localPuid);
}
bool ReadPvpStatus(PvpStatus* out) noexcept {
    if(!out)return false;
    AcquireSRWLockShared(&viewLock);*out=view;ReleaseSRWLockShared(&viewLock);return true;
}
unsigned ReadPvpMembers(PvpMember* out,unsigned capacity) noexcept {
    if(!out || !capacity)return 0;
    AcquireSRWLockShared(&viewLock);
    const unsigned count=memberCount<capacity ? memberCount : capacity;
    std::memcpy(out,members,count*sizeof(PvpMember));
    ReleaseSRWLockShared(&viewLock);return count;
}
void UpdatePvpNetwork(const CommandNetworkContext& context,std::uint64_t now) noexcept {
    network=context;
    localPuid[0]=0;
    if(context.localPuid && std::memchr(context.localPuid,0,sizeof(localPuid)))
        std::memcpy(localPuid,context.localPuid,std::strlen(context.localPuid)+1);
    network.localPuid=localPuid;
    protocol.Update({Cfg().enabled && context.ready,context.host,context.epoch,context.hostPeer,context.peerCount,localPuid},now);
    PublishView();
}
bool ReceivePvpNetwork(std::uint32_t peer,const char* authenticatedPuid,const void* bytes,std::size_t size,
                       std::uint64_t now) noexcept {
    if(!bytes || size<4)return false;
    const auto* header=static_cast<const unsigned char*>(bytes);
    if(header[0]!='A' || header[1]!='F' || header[2]!='P' || header[3]!='V')return false;
    // Claim our magic even when validation rejects the packet: never feed a
    // malformed PVP message into another extension's decoder.
    protocol.Receive(peer,authenticatedPuid,bytes,size,now);
    PublishView();return true;
}
void PvpFrame() noexcept {
    SeeFrame(nullptr);
    const auto frame=GameFrame();
    if(frame && lastPvpFrame==frame)return;
    lastPvpFrame=frame;
    Advertise();
    const auto now=GetTickCount64();
    protocol.Update({Cfg().enabled && network.ready,network.host,network.epoch,network.hostPeer,network.peerCount,localPuid},now);
    if(resetCleanupPending && !protocol.Restoring()) {
        ResetPvpNative();resetCleanupPending=false;
        if(forgetSupportPending){PvpNativeClearSupportOwners();forgetSupportPending=false;}
    }
    AcquireSRWLockExclusive(&viewLock);
    const PendingRequest pending=request;const int command=queued.exchange(0);request={};
    ReleaseSRWLockExclusive(&viewLock);
    if(command) {
        const bool current=pending.epoch==protocol.WireEpoch() && pending.revision==protocol.Revision() &&
            !std::strcmp(pending.puid,localPuid);
        if(!current)staleRequest.store(true);
        else if(command==1 || command==2)protocol.RequestMode(command==2,now);
        else if(command==3 || command==4)protocol.RequestTeam(static_cast<std::uint8_t>(command-2),now);
    }
    if(!PvpNativeTick()) {
        Log("PVP native current-world/vehicle invariant lost; restoring original teams and notifying host");
        protocol.Fail(now);
    }
    PublishView();
}
void PvpRoomTick() noexcept { Advertise(); }
void ResetPvpNetwork() noexcept {
    protocol.Reset();network={};localPuid[0]=0;lastPvpFrame=~0ull;
    AcquireSRWLockExclusive(&viewLock);queued.store(0);request={};ReleaseSRWLockExclusive(&viewLock);
    staleRequest.store(false);resetCleanupPending=true;
    if(!protocol.Restoring()){ResetPvpNative();resetCleanupPending=false;}
    PublishView();
}
int PvpSupportTeam(const char* puid) noexcept {
    if(queued.load()!=0 || protocol.Busy() || protocol.Restoring())return -1;
    const auto& state=protocol.Current();
    if(!state.enabled)return 0;
    if(!protocol.Ready() || !puid)return -1;
    for(unsigned i=0;i<state.count;++i)if(!std::strcmp(state.entries[i].puid,puid))return state.entries[i].team;
    return -1;
}
void PvpForgetSupportObject(const ObjRef& object) noexcept { PvpNativeForgetSupport(object); }
void ResetPvp() noexcept {
    ResetPvpNetwork();forgetSupportPending=true;
    if(!protocol.Restoring()){PvpNativeClearSupportOwners();forgetSupportPending=false;}
}
}
