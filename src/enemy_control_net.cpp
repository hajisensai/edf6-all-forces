// Authenticated bridge from one Coop poll to the enemy-control core. Native ownership stays with the host.
#include "enemy_control_net.h"
#include "enemy_control_capacity.h"
#include "enemy_control_net_input.h"
#include "enemy_control_core_pose.h"
#include "enemy_control_core_action.h"
#include "enemy_control.h"
#include "command_identity.h"
#include "coop_extension_api.h"
#include "support_net.h"
#include "support_soldier.h"
#include "online_authority.h"
#include "memory.h"
#include <atomic>
#include <cstring>

namespace crew {
namespace {
namespace ep=enemy_control_protocol;
namespace en=enemy_control_net;
CommandNetworkContext transport{};
char localPuid[65]{};
bool configured=false;
static_assert(ep::kWireBytes<=EDF6COOP_EXTENSION_MAX_PAYLOAD,"enemy control envelope must fit the actual Coop reliable extension ABI");
static_assert(en::kInputBytes<=ep::kMaxInputBytes,"semantic input must fit the negotiated protocol payload");
static_assert(ep::kMaxActions>=ep::kActionWindow*enemy_control::kMaxControllers,"every admitted native controller must retain a full reliable action window");
constexpr std::uint64_t kSampleMs=33; // steady motion/held state; changes of skill state are immediate
struct Sample {
    std::uint64_t lease=0,lastSent=0;
    enemy_control::Input previous{},pending{};
    bool sent=false,waiting=false;
};
Sample samples[enemy_control::kMaxControllers]{};
constexpr std::uint64_t kPoseTimeoutMs=500;
struct PoseSample {std::uint64_t lease=0,created=0,sent=0,received=0;};
PoseSample poseSamples[enemy_control::kMaxControllers]{};
std::uint64_t networkNow=0;
const ep::LeaseView* Lease(std::uint64_t id) noexcept;
void PrunePoses() noexcept {
    for(auto& sample:poseSamples)if(sample.lease) {
        const auto* lease=Lease(sample.lease);if(!lease || lease->phase!=ep::Phase::active)sample={};
    }
}
PoseSample* PoseFor(std::uint64_t lease,std::uint64_t now) noexcept {
    // Restore ACKs and a new activation may arrive in one poll. Reclaim retired slots before admission, including
    // receive callbacks that run before the next frame, so a full old ledger cannot spuriously revoke the new body.
    PrunePoses();
    for(auto& sample:poseSamples)if(sample.lease==lease)return &sample;
    for(auto& sample:poseSamples)if(!sample.lease){sample={lease,now,0,0};return &sample;}
    return nullptr;
}
struct UiRequest {
    ep::NativeId source{},target{};
    std::uint64_t profile=0,revision=0,request=0;
    std::uintptr_t ownerKey=0;
};
UiRequest uiRequests[ep::kMaxLeases]{};
SRWLOCK statusLock=SRWLOCK_INIT;
EnemyControlNetworkStatus roomStatus{},statuses[ep::kMaxLeases]{};
unsigned statusCount=0;
// Requester's refusal log lines: at most 16 a mission, counted again from ResetEnemyControlNetwork (a mission's start),
// not 16 for the whole process.
std::atomic<int> requesterLogs{0};
ep::Result lastLogged=ep::Result::Idle;

bool Match(const ep::NativeId& id,const unsigned char* bytes) noexcept {return bytes && !std::memcmp(id.data(),bytes,id.size());}
const ep::LeaseView* Lease(std::uint64_t id) noexcept;
void Publish() noexcept;
bool Send(void*,std::uint32_t peer,const void* bytes,std::size_t size) noexcept {
    return transport.ready && transport.send && peer && peer<=transport.peerCount && transport.send(peer,bytes,size);
}
ep::SendResult TrySendAction(void*,std::uint32_t peer,const void* bytes,std::size_t size) noexcept {
    if(!transport.ready || !transport.trySend || !peer || peer>transport.peerCount)return ep::SendResult::unavailable;
    const auto result=transport.trySend(peer,bytes,size,static_cast<std::uint32_t>(ep::kActionPeerByteCredits),static_cast<std::uint32_t>(ep::kActionPeerPacketCredits));
    if(result==EDF6COOP_EXTENSION_SEND_ACCEPTED)return ep::SendResult::accepted;
    return result==EDF6COOP_EXTENSION_SEND_WOULD_BLOCK ? ep::SendResult::wouldBlock : ep::SendResult::unavailable;
}
bool Requester(const char* authenticated,const ep::NativeId& id) noexcept {
    // The host's reason for an Unauthorized reply (the guest sees only that; 2026-10-30 report #27). A PUID / participant
    // mismatch is logged by the sender check itself (SupportCommandRequesterMatches).
    std::atomic<int>& said=requesterLogs;
    if(!ep::ValidPuid(authenticated) || !ep::CanonicalId(id)) {
        if(said.fetch_add(1)<16)Log("ENEMYCTRL NET request refused: malformed sender or player ID");
        return false;
    }
    CommandIdentities identities{};
    if(!ResolveCommandIdentities(id.data(),nullptr,0,nullptr,&identities) || !identities.requester) {
        if(said.fetch_add(1)<16)Log("ENEMYCTRL NET request refused: the sender's player %02X%02X%02X%02X is not one live player here (not found, "
            "not a soldier, or its ID on two objects)",id[0],id[1],id[2],id[3]);
        return false;
    }
    return SenderIsPlayer(identities.requesterPuid,authenticated);
}
bool Authenticate(void*,const char* puid,const ep::NativeId& source) noexcept {
    __try {return InSession() && Requester(puid,source);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct Actors { ObjRef source{},target{}; };
bool ResolveActors(const ep::LeaseSpec& spec,Actors* out) noexcept {
    if(!out)return false;*out={};
    if(!InSession() || !ep::CanonicalId(spec.requesterPlayer) || !ep::CanonicalId(spec.target) ||
       spec.requesterPlayer==spec.target || !ep::ValidPuid(spec.ownerPuid))return false;
    CommandIdentities identities{};
    if(!ResolveCommandIdentities(spec.requesterPlayer.data(),nullptr,0,spec.target.data(),&identities) ||
       !identities.requester || !identities.focus ||
       !SenderIsPlayer(identities.requesterPuid,spec.ownerPuid))return false;
    // Native IDs must still be registered. The host proves this target really is its authority; guests only accept
    // that same host-issued lease and must see a remote copy, never turn their object into a local owner.
    const auto* target=static_cast<const unsigned char*>(identities.focus.obj);
    unsigned char native[32]{};
    if(!ReadNativeObjectId(target,native) || std::memcmp(native,spec.target.data(),32) ||
       IsOnlineAuthority(target)!=transport.host)return false;
    *out={identities.requester,identities.focus};return true;
}
bool Resolve(void*,ep::LeaseSpec* spec) noexcept {
    if(!spec || !transport.host)return false;
    __try {
        // Describe is a local UI operation (IsPlayer), so it cannot validate a guest's source actor on the host.
        // The UI already supplied a nonzero catalogue hash; Prepare recomputes/compares it against this peer's actual
        // native profile using IsAnyPlayer, without granting a lease or moving any identity.
        Actors actors{};enemy_control::Reason reason=enemy_control::Reason::none;
        return spec->profileHash && ResolveActors(*spec,&actors) &&
            EnemyControlPrepare(actors.source,actors.target,spec->profileHash,&reason);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Prepare(void*,std::uint64_t,const ep::LeaseSpec& spec) noexcept {
    __try {
        Actors actors{};enemy_control::Reason reason=enemy_control::Reason::none;
        return ResolveActors(spec,&actors) && EnemyControlPrepare(actors.source,actors.target,spec.profileHash,&reason);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Begin(void*,std::uint64_t token,const ep::LeaseSpec& spec) noexcept {
    __try {
        Actors actors{};
        const bool local=localPuid[0] && !std::strcmp(localPuid,spec.ownerPuid);
        return ResolveActors(spec,&actors) && EnemyControlBegin(actors.source,actors.target,spec.profileHash,token,local,spec.ownerPuid);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Activate(void*,std::uint64_t token,const ep::LeaseSpec& spec) noexcept {
    __try {Actors actors{};return ResolveActors(spec,&actors) && EnemyControlActivate(token);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ValidatedInput(std::uint64_t token,const ep::LeaseSpec& spec,const ep::InputMeta& meta,
                    const void* bytes,std::size_t size,enemy_control::Input* out) noexcept {
    Actors actors{};
    if(!out || !meta.sequence || !meta.seed || !ResolveActors(spec,&actors) || !en::DecodeInput(bytes,size,out))return false;
    out->sequence=meta.sequence;out->seed=meta.seed;
    return enemy_control::ValidInput(*out) && EnemyControlValidateInput(token,*out);
}
bool ValidateInput(void*,std::uint64_t token,const ep::LeaseSpec& spec,const ep::InputMeta& meta,
                   const void* bytes,std::size_t size) noexcept {
    __try {enemy_control::Input input;return ValidatedInput(token,spec,meta,bytes,size,&input);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ApplyInput(void*,std::uint64_t token,const ep::LeaseSpec& spec,const ep::InputMeta& meta,
                const void* bytes,std::size_t size) noexcept {
    __try {
        enemy_control::Input input;
        return ValidatedInput(token,spec,meta,bytes,size,&input) && EnemyControlApply(token,input);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Restore(void*,std::uint64_t token,const ep::LeaseSpec&) noexcept {
    // Source/target may already be dead or removed. The core owns retained weak references and the original baseline;
    // never require a fresh requester lookup before releasing its old input/AI/camera state.
    __try {return EnemyControlRestore(token);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ApplyPose(void*,std::uint64_t token,const ep::LeaseSpec& spec,std::uint64_t,const void* bytes,std::size_t size) noexcept {
    __try {
        Actors actors{};
        if(transport.host || !ResolveActors(spec,&actors) || !EnemyControlNeedsNetworkPose(token) ||
           !EnemyControlApplyNetworkPose(token,bytes,size))return false;
        auto* sample=PoseFor(token,networkNow);if(!sample)return false;
        sample->received=networkNow;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
ep::ActionResult ApplyAction(void*,std::uint64_t token,const ep::LeaseSpec& spec,std::uint64_t sequence,const void* bytes,std::size_t size) noexcept {
    __try {
        Actors actors{};
        const auto* current=Lease(token);const bool closing=current && current->phase==ep::Phase::draining;
        // Closing outcomes use the core's retained native identities: source/target may have left the active registry
        // after host acceptance. The authenticated full lease spec remains fixed; no fresh actor is adopted here.
        if(transport.host || (!closing && !ResolveActors(spec,&actors)) || !EnemyControlNeedsHostActions(token))return ep::ActionResult::failed;
        return EnemyControlApplyNetworkAction(token,sequence,bytes,size);
    } __except(EXCEPTION_EXECUTE_HANDLER){return ep::ActionResult::failed;}
}
bool NormalizeInput(void*,std::uint64_t token,const ep::LeaseSpec& spec,const ep::InputMeta& meta,void* bytes,std::size_t size) noexcept {
    __try {
        Actors actors{};enemy_control::Input input{};
        if(!transport.host || !ResolveActors(spec,&actors) || !en::DecodeInput(bytes,size,&input))return false;
        input.sequence=meta.sequence;input.seed=meta.seed;
        if(!EnemyControlNormalizeNetworkInput(token,&input))std::memset(input.fullFocusId,0,32);
        return en::EncodeInput(input,bytes,size);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool QuiesceActions(void*,std::uint64_t token,const ep::LeaseSpec&) noexcept {
    __try{return EnemyControlQuiesceNetworkActions(token);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ActionsEmpty(void*,std::uint64_t token,const ep::LeaseSpec&) noexcept {
    __try{return EnemyControlNetworkActionsEmpty(token);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool AbortActions(void*,std::uint64_t token,const ep::LeaseSpec&) noexcept {
    __try{return EnemyControlAbortNetworkActions(token);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
ep::Protocol protocol({nullptr,&Send,&Authenticate,&Resolve,&Prepare,&Begin,&ValidateInput,&ApplyInput,&Restore,&Activate,&ApplyPose,&ApplyAction,&QuiesceActions,&ActionsEmpty,&AbortActions,&NormalizeInput,&TrySendAction});
const ep::LeaseView* Lease(std::uint64_t id) noexcept {
    if(!id)return nullptr;
    for(std::size_t i=0;i<ep::kMaxLeases;++i)if(const auto* lease=protocol.LeaseAt(i);lease && lease->id==id)return lease;
    return nullptr;
}
void Fill(EnemyControlNetworkStatus& status,const ep::LeaseView& lease) noexcept {
    status.phase=lease.phase;status.result=lease.result;status.lease=lease.id;status.profileHash=lease.spec.profileHash;
    std::memcpy(status.source,lease.spec.requesterPlayer.data(),32);std::memcpy(status.target,lease.spec.target.data(),32);
}
void Publish() noexcept {
    EnemyControlNetworkStatus room;room.online=InSession();room.ready=transport.ready && protocol.Ready();room.result=protocol.LastResult();
    EnemyControlNetworkStatus next[ep::kMaxLeases]{};unsigned count=0;
    for(std::size_t i=0;i<ep::kMaxLeases;++i) {
        const auto* request=protocol.RequestAt(i);if(!request || !request->request)continue;
        auto& status=next[count++];status=room;status.request=request->request;status.lease=request->lease;
        status.profileHash=request->profileHash;status.result=request->result;
        std::memcpy(status.source,request->source.data(),32);std::memcpy(status.target,request->target.data(),32);
        status.phase=request->cancelPending ? ep::Phase::restoring :
            request->result==ep::Result::Pending ? ep::Phase::preparing : ep::Phase::closed;
        if(const auto* lease=Lease(request->lease))Fill(status,*lease);
        if(request->cancelPending)status.phase=ep::Phase::restoring;
    }
    AcquireSRWLockExclusive(&statusLock);roomStatus=room;statusCount=count;std::memcpy(statuses,next,sizeof(statuses));
    ReleaseSRWLockExclusive(&statusLock);
    if(room.result!=lastLogged) {
        Log("ENEMYCTRL NET result=%u ready=%d host=%d active=%u",static_cast<unsigned>(room.result),room.ready,transport.host,protocol.ActiveCount());
        lastLogged=room.result;
    }
}
void Tick(std::uint64_t now) noexcept {
    networkNow=now;protocol.Tick({transport.ready && Cfg().enabled,transport.host,transport.epoch,transport.hostPeer,transport.peerCount,localPuid,EnemyControlLocalCapacityReady() ? ep::kCapability : 0},now);
}
bool SameSnapshot(const ep::LeaseView& lease,const enemy_control::Snapshot& snapshot) noexcept {
    if(lease.id!=snapshot.lease || lease.spec.profileHash!=snapshot.profileHash ||
       !Match(lease.spec.requesterPlayer,snapshot.owner) || !Match(lease.spec.target,snapshot.target) ||
       !localPuid[0] || std::strcmp(lease.spec.ownerPuid,localPuid))return false;
    Actors actors{};enemy_control::Snapshot current{};
    // Epochs scope wire IDs; native revisions scope rendered callbacks. A new world/host can reuse every wire field
    // including lease one, but its current native controller must still have this exact fresh UI revision.
    return ResolveActors(lease.spec,&actors) && EnemyControlReadSnapshot(actors.source,&current) &&
        current.lease==snapshot.lease && current.revision==snapshot.revision &&
        current.ownerHumanKey==snapshot.ownerHumanKey && current.profileHash==snapshot.profileHash;
}
bool Request(const enemy_control::Snapshot& snapshot) noexcept {
    Tick(GetTickCount64());ep::NativeId source{},target{};std::memcpy(source.data(),snapshot.owner,32);std::memcpy(target.data(),snapshot.target,32);
    const auto* before=protocol.RequestFor(source);const std::uint64_t previous=before ? before->request : 0;
    const bool accepted=protocol.Request(source,target,snapshot.profileHash,GetTickCount64());
    if(const auto* request=protocol.RequestFor(source);request && request->request!=previous) {
        UiRequest* binding=nullptr;
        for(auto& current:uiRequests)if(current.request && current.source==source){binding=&current;break;}
        if(!binding)for(auto& current:uiRequests)if(!current.request){binding=&current;break;}
        if(!binding) {binding=&uiRequests[0];for(auto& current:uiRequests)if(current.request<binding->request)binding=&current;}
        *binding={source,target,snapshot.profileHash,snapshot.revision,request->request,snapshot.ownerHumanKey};
    }
    Publish();return accepted;
}
bool SendSample(Sample& sample,const enemy_control::Input& input,std::uint64_t now) noexcept {
    unsigned char bytes[en::kInputBytes]{};
    if(!en::EncodeInput(input,bytes,sizeof(bytes)) || !protocol.SubmitInput(sample.lease,bytes,sizeof(bytes),now))return false;
    sample.previous=input;sample.lastSent=now;sample.sent=true;sample.waiting=false;return true;
}
bool Input(const enemy_control::Snapshot& snapshot,const enemy_control::Input& input) noexcept {
    const auto now=GetTickCount64();Tick(now);
    const auto* lease=Lease(snapshot.lease);
    if(!lease || lease->phase!=ep::Phase::active || !SameSnapshot(*lease,snapshot) || !en::ValidSemanticInput(input))return false;
    Sample* sample=nullptr;
    for(auto& current:samples)if(current.lease==snapshot.lease){sample=&current;break;}
    if(!sample)for(auto& current:samples)if(!current.lease){current={};current.lease=snapshot.lease;sample=&current;break;}
    if(!sample)return false;
    const bool urgent=!sample->sent || !en::SameChannels(sample->previous,input) || en::HasPress(input);
    if(urgent || now-sample->lastSent>=kSampleMs) {
        const bool sent=SendSample(*sample,input,now);Publish();return sent;
    }
    sample->pending=input;sample->waiting=true;return true; // retained latest axes/held sample, never an unacknowledged skill edge
}
bool Stop(const enemy_control::Snapshot& snapshot) noexcept {
    const auto now=GetTickCount64();Tick(now);
    if(const auto* lease=Lease(snapshot.lease);lease && SameSnapshot(*lease,snapshot)) {
        const bool accepted=protocol.Exit(snapshot.lease,now);Publish();return accepted;
    }
    // PREPARE can already expose a host lease while the core has not begun a local native session. Cancel that exact
    // pending request too, keyed by the rendered candidate revision and its request nonce rather than by lease alone.
    ep::NativeId source{},target{};std::memcpy(source.data(),snapshot.owner,32);std::memcpy(target.data(),snapshot.target,32);
    const auto* request=protocol.RequestFor(source);
    if(!request || request->target!=target || request->profileHash!=snapshot.profileHash ||
       (snapshot.request && snapshot.request!=request->request))return false;
    for(const auto& binding:uiRequests)if(binding.request==request->request && binding.source==source && binding.target==target &&
        binding.profile==snapshot.profileHash && binding.revision==snapshot.revision && binding.ownerKey==snapshot.ownerHumanKey) {
        const bool accepted=protocol.Cancel(source,target,snapshot.profileHash,binding.request,now);Publish();return accepted;
    }
    return false;
}
bool Online() noexcept {return InSession();} // no local-only fallback when the room transport/capability gate is unready
}

bool InstallEnemyControlNetwork() noexcept {
    if(configured)return true;
    if(!protocol.SetReliableOrderedTransport(true))return false;
    // Plugin loading precedes the native session manager. Register callbacks only;
    // the first game-thread transport update publishes the initial live status.
    ConfigureEnemyControlNetwork({&Request,&Input,&Stop,&Online});configured=true;return true;
}
void UpdateEnemyControlNetwork(const CommandNetworkContext& context,std::uint64_t now) noexcept {
    const bool same=context.epoch==transport.epoch && context.host==transport.host && context.hostPeer==transport.hostPeer &&
        context.peerCount==transport.peerCount && ep::ValidPuid(context.localPuid) && !std::strcmp(localPuid,context.localPuid);
    // Lease numbers are scoped to an epoch/host. A new host can legitimately start at one; deferred local input from
    // the previous world must not match that new lease merely because its number/source/target were reused.
    if(!context.ready || !same) {
        for(auto& sample:samples)sample={};
        for(auto& sample:poseSamples)sample={};
        for(auto& request:uiRequests)request={};
    }
    transport=context;std::memset(localPuid,0,sizeof(localPuid));
    if(ep::ValidPuid(context.localPuid))std::memcpy(localPuid,context.localPuid,std::strlen(context.localPuid)+1);
    transport.localPuid=localPuid;Tick(now);Publish();
}
bool ReceiveEnemyControlNetwork(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t now) noexcept {
    networkNow=now;const bool owned=protocol.Receive(peer,puid,bytes,size,now);if(owned)Publish();return owned;
}
void EnemyControlNetworkFrame() noexcept {
    const auto now=GetTickCount64();Tick(now);protocol.TransportFrame(now);PrunePoses();
    for(std::size_t i=0;i<ep::kMaxLeases;++i) {
        const auto* lease=protocol.LeaseAt(i);if(!lease || !lease->id || (lease->phase!=ep::Phase::active && lease->phase!=ep::Phase::draining))continue;
        Actors actors{};
        const bool closing=lease->phase==ep::Phase::draining;
        if(!closing && (!ResolveActors(lease->spec,&actors) || !EnemyControlLeaseLive(lease->id))){protocol.Failed(lease->id,now);continue;}
        if(transport.host && EnemyControlNeedsHostActions(lease->id)) {
            // Fill a bounded reliable window with complete native batches; later applied ACKs cannot overtake the
            // guest's serial application order. A large 33-gun burst is not forced into one batch per network RTT.
            for(std::size_t n=0;n<ep::kActionWindow && protocol.CanCaptureAction(lease->id);++n) {
                unsigned char bytes[ep::kMaxPoseBytes]{};std::size_t used=0;
                if(!EnemyControlCaptureNetworkAction(lease->id,bytes,sizeof(bytes),&used)){protocol.Failed(lease->id,now);break;}
                if(!used)break;
                if(!protocol.SubmitAction(lease->id,bytes,used,now)){protocol.Failed(lease->id,now);break;}
            }
            if(lease->phase!=ep::Phase::active && lease->phase!=ep::Phase::draining)continue;
        }
        if(lease->phase==ep::Phase::active && EnemyControlNeedsNetworkPose(lease->id)) {
            auto* sample=PoseFor(lease->id,now);if(!sample){protocol.Failed(lease->id,now);continue;}
            if(transport.host) {
                if(!sample->sent || now-sample->sent>=kSampleMs) {
                    unsigned char bytes[ep::kMaxPoseBytes]{};std::size_t used=0;
                    if(!EnemyControlCaptureNetworkPose(lease->id,bytes,sizeof(bytes),&used) || !used ||
                       !protocol.SubmitPose(lease->id,bytes,used,now)){protocol.Failed(lease->id,now);continue;}
                    sample->sent=now;
                }
            } else if(now-(sample->received ? sample->received : sample->created)>kPoseTimeoutMs) {
                protocol.Failed(lease->id,now);continue;
            }
        }
    }
    for(auto& sample:samples)if(sample.lease) {
        const auto* lease=Lease(sample.lease);
        if(!lease || lease->phase!=ep::Phase::active){sample={};continue;}
        if(sample.waiting && now-sample.lastSent>=kSampleMs && !SendSample(sample,sample.pending,now))protocol.Failed(sample.lease,now);
    }
    Publish();
}
void ResetEnemyControlNetwork() noexcept {
    protocol.Reset(GetTickCount64());transport={};std::memset(localPuid,0,sizeof(localPuid));requesterLogs.store(0);
    for(auto& sample:samples)sample={};for(auto& sample:poseSamples)sample={};for(auto& request:uiRequests)request={};Publish();
}
bool ReadEnemyControlNetworkStatus(const unsigned char source[32],EnemyControlNetworkStatus* out) noexcept {
    if(!out)return false;
    AcquireSRWLockShared(&statusLock);*out=roomStatus;
    if(source)for(unsigned i=0;i<statusCount;++i)if(!std::memcmp(source,statuses[i].source,32)){*out=statuses[i];break;}
    ReleaseSRWLockShared(&statusLock);return true;
}
} // namespace crew
