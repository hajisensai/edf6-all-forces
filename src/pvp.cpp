#include "coop_attr.h"
#include "pvp.h"
#include "pvp_native.h"
#include "pvp_units.h"
#include "room_pvp.h"
#include "debug_spawn_net.h"
#include "map.h"
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
std::atomic<int> queued{0}; // 1 mode off, 2 mode on, 3 pick, 4 preset, 5 the room's match
SRWLOCK viewLock=SRWLOCK_INIT;
PvpStatus view{};
PvpMember members[pvp_protocol::kMaxMembers]{};
unsigned memberCount=0;
struct PendingRequest { int command=0; std::uint64_t epoch=0; std::uint32_t revision=0; char puid[65]{}; pvp_match::Pick pick{}; int preset=0; };
pvp_protocol::State roomMatch{};
PendingRequest request{};
std::atomic<bool> staleRequest{false};
SRWLOCK advertiseLock=SRWLOCK_INIT;
bool resetCleanupPending=false,forgetSupportPending=false;
ULONGLONG lastPvpFrame=~0ull;
// The room's lock this mission world applied (room_pvp.h RoomPvpMatch): its serial and the protocol's shared epoch.
std::uint32_t appliedLock=0;std::uint64_t appliedEpoch=0;
// This member's role in the committed match: the commander's map, the unit's requests (again after it died).
constexpr ULONGLONG kUnitRetryMs=5000,kUnitRespawnMs=8000;
ULONGLONG unitAskedAt=0,unitGoneAt=0;bool unitWasLive=false;
pvp_protocol::Result lastLogged=pvp_protocol::Result::Idle;

bool Send(void*,std::uint32_t peer,const void* data,std::size_t size) noexcept {
    return network.ready && network.send && network.send(peer,data,size);
}
bool Prepare(void*,const pvp_protocol::State& state) noexcept { return PvpNativePrepare(state); }
bool Apply(void*,const pvp_protocol::State& state) noexcept { return PvpNativeApply(state); }
pvp_protocol::Protocol protocol({nullptr,&Send,&Prepare,&Apply});
// The committed, enabled match's entry of this machine's member (false: none).
bool LocalEntry(pvp_protocol::Entry* out) noexcept {
    if(!protocol.Ready() || protocol.Busy() || queued.load())return false;
    const auto& state=protocol.Current();
    if(!state.enabled || !localPuid[0])return false;
    for(unsigned i=0;i<state.count;++i)if(!std::strcmp(state.entries[i].puid,localPuid)){*out=state.entries[i];return true;}
    return false;
}
// The unit role: its unit asked for near the player once the match is committed and whenever the last one died
// (kUnitRespawnMs after), every kUnitRetryMs while the request does not go through.
void UnitRole(const pvp_protocol::Entry& me,ULONGLONG now) noexcept {
    const bool live=PvpUnitLive();
    if(live){unitWasLive=true;return;}
    if(unitWasLive){unitWasLive=false;unitGoneAt=now;}
    if(unitGoneAt && now-unitGoneAt<kUnitRespawnMs)return;
    if(unitAskedAt && now-unitAskedAt<kUnitRetryMs)return;
    const unsigned char* human=PlayerHuman();
    if(!human)return;
    unitAskedAt=now;
    float at[3];
    __try {
        const float* p=reinterpret_cast<const float*>(human+kPosition);
        const float* m=reinterpret_cast<const float*>(human+kMatrix+0x20);   // its forward axis (the matrix's third row)
        for(int i=0;i<3;++i)at[i]=p[i]+m[i]*12.0f;
        at[1]=p[1]+2.0f;
    } __except(EXCEPTION_EXECUTE_HANDLER){return;}
    wchar_t note[160]{};
    RequestPvpUnit(me.unit,at,note,_countof(note));
}

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
    next.preset=protocol.Current().relations.preset;
    std::memcpy(next.localPuid,localPuid,sizeof(next.localPuid));
    const auto& state=protocol.Current();
    PvpMember nextMembers[pvp_protocol::kMaxMembers]{};
    for(unsigned i=0;i<state.count && i<pvp_protocol::kMaxMembers;++i) {
        auto& member=nextMembers[i];
        std::memcpy(member.puid,state.entries[i].puid,sizeof(member.puid));
        member.team=state.entries[i].team;member.local=localPuid[0] && !std::strcmp(member.puid,localPuid);
        member.pick=state.entries[i].Pick();
        if(member.local){next.localTeam=member.team;next.local=state.entries[i];}
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
    static coopattr::Publisher publisher{"EDF6X_PVP"};   // coop_attr.h: published once, again after EDF6Coop changes
    const coopattr::Table* t=coopattr::Api();
    if(!t){publisher.Forget();return;}
    publisher.Publish(t,"3");   // pvp_protocol kVersion
}
void Advertise() noexcept {
    AcquireSRWLockExclusive(&advertiseLock);AdvertiseChecked();ReleaseSRWLockExclusive(&advertiseLock);
}
bool QueueAt(int command,bool hostOnly,std::uint64_t epoch,std::uint32_t revision,const char* puid,
             const pvp_match::Pick& pick={},int preset=0) noexcept {
    if(!puid || !std::memchr(puid,0,65))return false;
    AcquireSRWLockExclusive(&viewLock);
    const bool accepted=view.ready && !view.pending && !queued.load() && (!hostOnly || view.host) &&
        view.epoch==epoch && view.revision==revision && !std::strcmp(view.localPuid,puid);
    if(accepted) {
        request.command=command;request.epoch=epoch;request.revision=revision;request.pick=pick;request.preset=preset;
        std::memcpy(request.puid,puid,std::strlen(puid)+1);queued.store(command);staleRequest.store(false);
    }
    ReleaseSRWLockExclusive(&viewLock);return accepted;
}
}

bool InstallPvp() noexcept { return InstallPvpNative(); }
bool SetPvpEnabledAt(bool enabled,std::uint64_t epoch,std::uint32_t revision,const char* puid) noexcept {
    return QueueAt(enabled ? 2 : 1,true,epoch,revision,puid);
}
bool SetPvpLocalPickAt(const pvp_match::Pick& pick,std::uint64_t epoch,std::uint32_t revision,const char* puid) noexcept {
    return pvp_match::PickValid(pick) && QueueAt(3,false,epoch,revision,puid,pick);
}
bool SetPvpPresetAt(int preset,std::uint64_t epoch,std::uint32_t revision,const char* puid) noexcept {
    return preset>=0 && preset<pvp_match::kPresetCount && preset!=static_cast<int>(pvp_match::Preset::custom) &&
        QueueAt(4,true,epoch,revision,puid,{},preset);
}
bool QueuePvpMatch(const pvp_protocol::State& match) noexcept {
    if(!match.relations.Valid())return false;
    AcquireSRWLockExclusive(&viewLock);
    const bool accepted=view.ready && view.host && !view.pending && !queued.load();
    if(accepted){roomMatch=match;request={};request.command=5;request.epoch=view.epoch;request.revision=view.revision;
        std::memcpy(request.puid,view.localPuid,sizeof(request.puid));queued.store(5);staleRequest.store(false);}
    ReleaseSRWLockExclusive(&viewLock);return accepted;
}
bool SetPvpEnabled(bool enabled) noexcept {
    PvpStatus current{};ReadPvpStatus(&current);
    return SetPvpEnabledAt(enabled,current.epoch,current.revision,current.localPuid);
}
bool SetPvpLocalPick(const pvp_match::Pick& pick) noexcept {
    PvpStatus current{};ReadPvpStatus(&current);
    return SetPvpLocalPickAt(pick,current.epoch,current.revision,current.localPuid);
}
pvp_match::Pick PvpPickForFaction(int faction,const pvp_protocol::Entry& current) noexcept {
    if(!pvp_match::FactionValid(faction))return current.Pick();
    pvp_match::Pick pick{static_cast<std::uint8_t>(faction),current.role,current.unit};
    if(!pvp_match::RoleAllowed(faction,pick.role))pick.role=static_cast<std::uint8_t>(pvp_match::DefaultRole(faction));
    const auto side=pvp_match::kFactions[faction].side;
    if(pick.role!=static_cast<std::uint8_t>(pvp_match::Role::unit))pick.unit=pvp_match::kNoUnit;
    else if(pick.unit==pvp_match::kNoUnit || !pvp_units::Playable(pick.unit,side)) {
        const int row=pvp_units::DefaultRow(side);
        pick.unit=row>=0 ? static_cast<std::uint16_t>(row) : pvp_match::kNoUnit;
    }
    return pick;
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
    const pvp_protocol::State match=roomMatch;
    ReleaseSRWLockExclusive(&viewLock);
    if(command) {
        const bool current=pending.epoch==protocol.WireEpoch() && pending.revision==protocol.Revision() &&
            !std::strcmp(pending.puid,localPuid);
        if(!current)staleRequest.store(true);
        else if(command==1 || command==2)protocol.RequestMode(command==2,now);
        else if(command==3)protocol.RequestPick(pending.pick,now);
        else if(command==4){pvp_match::Relations r;r.FromPreset(static_cast<pvp_match::Preset>(pending.preset));protocol.RequestRelations(r,now);}
        else if(command==5)protocol.RequestMatch(match,now);
    }
    // The host applies the room's lock once per mission world (a retry is a new world) as one transaction: every member
    // gets the host's table through Prepare / Apply / Final, never its own reading of the room's attributes.
    if(network.host && protocol.Ready() && !protocol.Busy() && !queued.load()) {
        pvp_protocol::State lock{};std::uint32_t serial=0;
        if(RoomPvpMatch(&lock,&serial) && (serial!=appliedLock || protocol.WireEpoch()!=appliedEpoch)) {
            appliedLock=serial;appliedEpoch=protocol.WireEpoch();
            const bool ok=protocol.RequestMatch(lock,now);
            Log("PVP room lock %u: the mission's match %s",serial,ok ? "requested from every member" : "refused (see PVP result)");
        }
    }
    {
        pvp_protocol::Entry me{};
        const bool on=LocalEntry(&me);
        MapSetCommander(on && me.role==static_cast<std::uint8_t>(pvp_match::Role::commander));
        if(on && me.role==static_cast<std::uint8_t>(pvp_match::Role::unit) && me.unit!=pvp_match::kNoUnit)UnitRole(me,now);
        else{unitAskedAt=unitGoneAt=0;unitWasLive=false;}
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
bool PvpMemberPick(const char* puid,pvp_match::Pick* out) noexcept {
    if(!puid || !out || queued.load()!=0 || protocol.Busy() || protocol.Restoring() || !protocol.Ready())return false;
    const auto& state=protocol.Current();
    if(!state.enabled)return false;
    for(unsigned i=0;i<state.count;++i)if(!std::strcmp(state.entries[i].puid,puid)){*out=state.entries[i].Pick();return true;}
    return false;
}
void PvpForgetSupportObject(const ObjRef& object) noexcept { PvpNativeForgetSupport(object); }
void ResetPvp() noexcept {
    MapSetCommander(false);unitAskedAt=unitGoneAt=0;unitWasLive=false;
    ResetPvpNetwork();forgetSupportPending=true;
    if(!protocol.Restoring()){PvpNativeClearSupportOwners();forgetSupportPending=false;}
}
}
