// Player control of registered enemy actors. Pure game-thread protocol; native ownership never changes here.
// PREPARE cannot mutate actors. BEGIN captures/restraints locally. ACTIVATE also needs every native ACK;
// only the final ACTIVE grant admits owner input, after both setup barriers have succeeded everywhere.
// Restore is a barrier too: failed native restoration retains its lease/spec and retries, including after Reset.
// This restores input/AI control state; it cannot undo projectiles or damage already emitted by native gameplay.
#pragma once
#include "enemy_control_action_types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace enemy_control_protocol {
constexpr std::uint32_t kMagic=0x4c544345u,kCapability=4;
constexpr std::uint8_t kVersion=3;
constexpr std::size_t kMaxPeers=32,kMaxLeases=16,kMaxInputBytes=512,kChunkBytes=512,kWirePrefixBytes=192,kWireBytes=kWirePrefixBytes+kMaxInputBytes,kMaxPoseBytes=512,kMaxActions=1024,kActionWindow=128;
using ActionIndex=std::uint16_t;
static_assert(kMaxActions<=static_cast<std::size_t>((std::numeric_limits<ActionIndex>::max)())+1,"all shared action pool indices must fit without truncation");
static_assert(kActionWindow<=kMaxActions,"one native lease window must fit the shared action pool");
constexpr std::uint64_t kTimeoutMs=5000,kHelloMs=1000,kRetryMs=100,kActionRetryMs=500;
constexpr std::size_t kActionPeerFramePackets=32,kActionFramePackets=kActionPeerFramePackets*kMaxPeers,kActionFrameAcks=1;
// DirectNet tracks the full authenticated datagram, not just this plugin's payload. Its worst Data framing is
// 188 bytes (64-byte PUIDs), rounded up here. The required SDK provides a 1 MiB link gate; this
// budget leaves at least 256 KiB for other reliable traffic, and its inbox holds 4096 messages.
constexpr std::size_t kActionSdkFramingBytes=192,kActionPeerByteCredits=768*1024,kActionPeerPacketCredits=768;
constexpr std::uint64_t kActionFrameMs=16,kAppliedAckMs=32;
using NativeId=std::array<std::uint8_t,32>;
using ActionResult=crew::enemy_control::ActionResult;
struct LeaseSpec { NativeId requesterPlayer{},target{};std::uint64_t profileHash=0;char ownerPuid[65]{}; };
struct InputMeta { std::uint64_t sequence=0,seed=0; };
struct Context {
    bool ready=false,host=false;std::uint64_t epoch=0;
    std::uint32_t hostPeer=0,peerCount=0;const char* localPuid=nullptr;
    std::uint32_t localCapability=kCapability; // trusted local SDK capacity proof; advertised independently by every peer
};
enum class Result : std::uint8_t {
    Idle,Pending,Active,Complete,Unavailable,Unsupported,Unauthorized,Conflict,InvalidInput,
    PrepareFailed,BeginFailed,ApplyFailed,RestoreFailed,SendFailed,TimedOut,SessionLost,ProtocolError
};
enum class Phase : std::uint8_t { empty,preparing,beginning,activating,active,draining,restoring,closed };
struct LeaseView {
    std::uint64_t id=0;LeaseSpec spec{};Phase phase=Phase::empty;
    bool started=false,localRestored=true;
    Result result=Result::Idle;
};
// Per-source local outcomes: unrelated leases must never overwrite a split-screen player's pending/refused request.
struct RequestView {
    std::uint64_t request=0;NativeId source{},target{};std::uint64_t profileHash=0,lease=0;
    Result result=Result::Idle;bool cancelPending=false;
};
struct Hooks {
    void* user=nullptr;
    bool (*Send)(void*,std::uint32_t,const void*,std::size_t)=nullptr;
    bool (*Authenticate)(void*,const char*,const NativeId&)=nullptr;
    bool (*Resolve)(void*,LeaseSpec*)=nullptr;
    bool (*Prepare)(void*,std::uint64_t,const LeaseSpec&)=nullptr;
    bool (*Begin)(void*,std::uint64_t,const LeaseSpec&)=nullptr;
    bool (*ValidateInput)(void*,std::uint64_t,const LeaseSpec&,const InputMeta&,const void*,std::size_t)=nullptr;
    bool (*ApplyInput)(void*,std::uint64_t,const LeaseSpec&,const InputMeta&,const void*,std::size_t)=nullptr;
    // Must tolerate a lease whose Prepare succeeded but whose Begin was never called (no-op success).
    bool (*Restore)(void*,std::uint64_t,const LeaseSpec&)=nullptr;
    // Begin only captures/restraints. Grant input/UI authority after every peer acknowledged Begin.
    bool (*Activate)(void*,std::uint64_t,const LeaseSpec&)=nullptr;
    // Host-owned physical corrections are an independent ordered stream. One complete SDK envelope applies atomically.
    bool (*ApplyPose)(void*,std::uint64_t,const LeaseSpec&,std::uint64_t,const void*,std::size_t)=nullptr;
    ActionResult (*ApplyAction)(void*,std::uint64_t,const LeaseSpec&,std::uint64_t,const void*,std::size_t)=nullptr;
    bool (*QuiesceActions)(void*,std::uint64_t,const LeaseSpec&)=nullptr;
    bool (*ActionsEmpty)(void*,std::uint64_t,const LeaseSpec&)=nullptr;
    bool (*AbortActions)(void*,std::uint64_t,const LeaseSpec&)=nullptr;
    // Host normalizes a target which died/became illegal after the owner's sample, before validating/broadcasting.
    bool (*NormalizeInput)(void*,std::uint64_t,const LeaseSpec&,const InputMeta&,void*,std::size_t)=nullptr;
};
inline bool ValidPuid(const char* value) noexcept {
    if(!value || !value[0])return false;
    for(std::size_t i=0;i<65;++i){const auto b=static_cast<unsigned char>(value[i]);if(!b)return true;if(b<33 || b>126)return false;}
    return false;
}
inline bool CanonicalId(const NativeId& id) noexcept {
    bool any=false;for(auto b:id)any=any || b!=0;
    return any && !id[20] && !id[21] && !id[22] && !id[23];
}
inline std::uint64_t Mix64(std::uint64_t value) noexcept {
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ull;
    value=(value^(value>>27))*0x94d049bb133111ebull;
    return value^(value>>31);
}
inline std::uint64_t InputSeed(std::uint64_t epoch,std::uint64_t lease,std::uint64_t sequence) noexcept {
    const auto seed=Mix64(epoch^Mix64(lease)^Mix64(sequence+0x9e3779b97f4a7c15ull));return seed ? seed : 1;
}
inline bool SameSpec(const LeaseSpec& a,const LeaseSpec& b) noexcept {
    return a.requesterPlayer==b.requesterPlayer && a.target==b.target && a.profileHash==b.profileHash && !std::strcmp(a.ownerPuid,b.ownerPuid);
}
enum class Kind : std::uint8_t { Hello=1,Welcome,Request,Reject,Prepare,Prepared,Begin,Started,Activate,InputRequest,Input,Exit,Fault,End,Restored,Ended,Activated,Active,Cancel,CancelAck,Pose,Action,ActionAck,Drain,DrainAck,Abort,ActionAckBatch };
enum AckSignal : std::uint16_t { ackApplied=1,ackPrepared=2,ackStarted=4,ackActivated=8,ackDrained=16,ackRestored=32,ackFault=64 };
constexpr std::size_t kAckEntryBytes=48,kAckBatchEntries=(kMaxInputBytes-8)/kAckEntryBytes;
inline std::uint16_t AckSignalFor(Kind kind) noexcept {
    switch(kind){case Kind::ActionAck:return ackApplied;case Kind::Prepared:return ackPrepared;case Kind::Started:return ackStarted;case Kind::Activated:return ackActivated;case Kind::DrainAck:return ackDrained;case Kind::Restored:return ackRestored;case Kind::Fault:return ackFault;default:return 0;}
}
struct AppliedAck {std::uint64_t lease=0,request=0,sequence=0,endToken=0,results=0;std::uint16_t signals=0,success=0;};
inline Result AckResult(const AppliedAck& ack,unsigned index) noexcept {return static_cast<Result>((ack.results>>(5*index))&31u);}
struct Message {
    Kind kind=Kind::Hello;bool ok=false;Result result=Result::Idle;
    std::uint64_t epoch=0,lease=0,request=0,sequence=0,seed=0;
    std::uint32_t capability=0;LeaseSpec spec{};
    std::uint16_t inputSize=0;std::array<std::uint8_t,kMaxInputBytes> input{};
};
namespace wire {
inline void Put(std::uint8_t*& p,std::uint64_t value,unsigned n) noexcept {for(unsigned i=0;i<n;++i)*p++=static_cast<std::uint8_t>(value>>(8*i));}
inline std::uint64_t Get(const std::uint8_t*& p,unsigned n) noexcept {std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=static_cast<std::uint64_t>(*p++)<<(8*i);return v;}
inline bool CanonicalPuid(const char* p) noexcept {
    bool ended=false;for(unsigned i=0;i<65;++i){const auto b=static_cast<unsigned char>(p[i]);if(ended && b)return false;if(!b)ended=true;else if(b<33 || b>126)return false;}return ended;
}
inline bool Valid(const Message& m) noexcept {
    if(m.kind<Kind::Hello || m.kind>Kind::ActionAckBatch || m.result>Result::ProtocolError || !m.epoch ||
       m.inputSize>kMaxInputBytes || !CanonicalPuid(m.spec.ownerPuid))return false;
    for(std::size_t i=m.inputSize;i<m.input.size();++i)if(m.input[i])return false;
    if(m.kind!=Kind::InputRequest && m.kind!=Kind::Input && m.kind!=Kind::Pose && m.kind!=Kind::Action && m.kind!=Kind::ActionAckBatch && m.inputSize)return false;
    if(m.kind==Kind::Hello || m.kind==Kind::Welcome)return m.request!=0;
    if(m.kind==Kind::Request || m.kind==Kind::Cancel || m.kind==Kind::CancelAck)return m.request && CanonicalId(m.spec.requesterPlayer) && CanonicalId(m.spec.target) && !m.spec.ownerPuid[0];
    if(m.kind==Kind::Reject)return m.request!=0;
    if(m.kind==Kind::ActionAckBatch) {
        if(!m.ok || m.result!=Result::Complete || m.lease || m.request || m.sequence || m.seed || m.capability || m.spec.profileHash || m.inputSize<8+kAckEntryBytes || m.input[0]!=2 || !m.input[1] || m.input[1]>kAckBatchEntries || m.inputSize!=8+kAckEntryBytes*m.input[1] || m.spec.ownerPuid[0])return false;
        for(auto b:m.spec.requesterPlayer)if(b)return false;for(auto b:m.spec.target)if(b)return false;
        for(unsigned i=2;i<8;++i)if(m.input[i])return false;
        const auto* p=m.input.data()+8;std::uint64_t previous=0;
        for(unsigned i=0;i<m.input[1];++i){const auto lease=Get(p,8),request=Get(p,8),sequence=Get(p,8),endToken=Get(p,8),signals=Get(p,2),success=Get(p,2),results=Get(p,8),reserved=Get(p,4);
            if(lease<=previous || !request || !signals || (signals&~127u) || (success&~signals) || reserved || (results>>35) || ((signals&ackApplied)!=0)!=static_cast<bool>(sequence) || (!(signals&ackRestored) && endToken))return false;
            for(unsigned bit=0;bit<7;++bit){const auto result=(results>>(5*bit))&31u;if(result>static_cast<unsigned>(Result::ProtocolError) || (!(signals&(1ull<<bit)) && result))return false;}previous=lease;}
        return true;
    }
    if(!m.lease)return false;
    if(m.kind==Kind::Prepare)return m.request && CanonicalId(m.spec.requesterPlayer) && CanonicalId(m.spec.target) &&
        m.spec.profileHash && ValidPuid(m.spec.ownerPuid);
    if(m.kind==Kind::ActionAck)return m.request && m.sequence && !m.seed && !m.capability && !m.inputSize &&
        CanonicalId(m.spec.requesterPlayer) && CanonicalId(m.spec.target) && m.spec.profileHash && ValidPuid(m.spec.ownerPuid);
    if(m.kind==Kind::Pose || m.kind==Kind::Action) {
        const auto parts=m.capability>>16,index=m.capability&0xFFFFu;
        return m.sequence && m.request && !m.seed && !m.ok && m.result==Result::Idle &&
            CanonicalId(m.spec.requesterPlayer) && CanonicalId(m.spec.target) && m.spec.profileHash &&
            ValidPuid(m.spec.ownerPuid) && m.inputSize && m.inputSize<=kChunkBytes && parts && parts<=kMaxPoseBytes/kChunkBytes &&
            index<parts && (index+1==parts || m.inputSize==kChunkBytes);
    }
    if(m.kind==Kind::InputRequest || m.kind==Kind::Input)return m.sequence && m.inputSize && m.spec.profileHash &&
        (m.kind!=Kind::Input || m.seed);
    return true;
}
}
inline std::size_t AppliedAckCount(const Message& m) noexcept {return m.kind==Kind::ActionAckBatch && wire::Valid(m) ? m.input[1] : 0;}
inline AppliedAck AppliedAckAt(const Message& m,std::size_t index) noexcept {
    if(index>=AppliedAckCount(m))return {};const auto* p=m.input.data()+8+kAckEntryBytes*index;AppliedAck ack;ack.lease=wire::Get(p,8);ack.request=wire::Get(p,8);ack.sequence=wire::Get(p,8);ack.endToken=wire::Get(p,8);ack.signals=static_cast<std::uint16_t>(wire::Get(p,2));ack.success=static_cast<std::uint16_t>(wire::Get(p,2));ack.results=wire::Get(p,8);return ack;
}
inline bool Owned(const void* bytes,std::size_t size) noexcept {
    if(!bytes || size<4)return false;auto p=static_cast<const std::uint8_t*>(bytes);return wire::Get(p,4)==kMagic;
}
inline std::size_t EncodedSize(const Message& m) noexcept {return kWirePrefixBytes+m.inputSize;}
inline bool Encode(const Message& m,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=EncodedSize(m) || !wire::Valid(m))return false;
    auto p=static_cast<std::uint8_t*>(bytes);wire::Put(p,kMagic,4);*p++=kVersion;*p++=static_cast<std::uint8_t>(m.kind);
    *p++=m.ok ? 1 : 0;*p++=static_cast<std::uint8_t>(m.result);
    wire::Put(p,m.epoch,8);wire::Put(p,m.lease,8);wire::Put(p,m.request,8);wire::Put(p,m.sequence,8);wire::Put(p,m.seed,8);
    wire::Put(p,m.capability,4);wire::Put(p,m.spec.profileHash,8);
    for(auto b:m.spec.requesterPlayer)*p++=b;for(auto b:m.spec.target)*p++=b;
    for(char b:m.spec.ownerPuid)*p++=static_cast<std::uint8_t>(b);
    wire::Put(p,m.inputSize,2);for(std::size_t i=0;i<m.inputSize;++i)*p++=m.input[i];*p++=0;
    return p==static_cast<std::uint8_t*>(bytes)+size;
}
inline bool Decode(const void* bytes,std::size_t size,Message& out) noexcept {
    if(!bytes || size<kWirePrefixBytes || size>kWireBytes || !Owned(bytes,size))return false;
    auto p=static_cast<const std::uint8_t*>(bytes)+4;if(*p++!=kVersion)return false;
    Message m;m.kind=static_cast<Kind>(*p++);const auto ok=*p++;if(ok>1)return false;m.ok=ok!=0;m.result=static_cast<Result>(*p++);
    m.epoch=wire::Get(p,8);m.lease=wire::Get(p,8);m.request=wire::Get(p,8);m.sequence=wire::Get(p,8);m.seed=wire::Get(p,8);
    m.capability=static_cast<std::uint32_t>(wire::Get(p,4));m.spec.profileHash=wire::Get(p,8);
    for(auto& b:m.spec.requesterPlayer)b=*p++;for(auto& b:m.spec.target)b=*p++;
    for(auto& b:m.spec.ownerPuid)b=static_cast<char>(*p++);
    m.inputSize=static_cast<std::uint16_t>(wire::Get(p,2));
    if(m.inputSize>kMaxInputBytes || size!=EncodedSize(m))return false;
    for(std::size_t i=0;i<m.inputSize;++i)m.input[i]=*p++;
    if(*p++ || !wire::Valid(m))return false;out=m;return true;
}
class Protocol {
public:
    explicit Protocol(Hooks hooks={}) noexcept:hooks_(hooks) {}
    bool Configure(Hooks hooks) noexcept;
    // Real extension transport has a bounded inbox. Frame pacing retains native batches in the existing pool;
    // exhausted quota is not a Send failure. The caller advances this once per actual shared game frame.
    bool SetReliableOrderedTransport(bool enabled) noexcept {if(HasCurrent())return false;paced_=enabled;return true;}
    // Compatibility spelling for the pure fixtures; enabling asserts the same reliable ordered SDK contract.
    bool SetSendPacing(bool enabled) noexcept {return SetReliableOrderedTransport(enabled);}
    void TransportFrame(std::uint64_t now) noexcept;
    void Tick(const Context&,std::uint64_t now) noexcept;
    // Claim every ECTL packet, even malformed ones, so it cannot be repolled as another protocol.
    bool Receive(std::uint32_t peer,const char* authenticatedPuid,const void*,std::size_t,std::uint64_t now) noexcept;
    bool Request(const NativeId& player,const NativeId& target,std::uint64_t desiredProfile,std::uint64_t now) noexcept;
    bool SubmitInput(std::uint64_t lease,const void*,std::size_t,std::uint64_t now) noexcept;
    // Canonical native physics state only; the authenticated host is its sole writer. A compound pose is applied
    // atomically, never as a truncated transform/cache update. Input sequence/seed remain independent.
    bool SubmitPose(std::uint64_t lease,const void*,std::size_t,std::uint64_t now) noexcept;
    // Host has already accepted/performed the native action. Guests acknowledge the actual transition, never
    // mere receipt or a locally refused cooldown. pending retries without ACK; failure/overflow ends the lease.
    bool SubmitAction(std::uint64_t lease,const void*,std::size_t,std::uint64_t now) noexcept;
    // ActionIdle is the closing barrier: every earlier batch was actually applied by every peer.
    // CanCaptureAction separately admits a bounded pipeline, so multi-gun bursts do not wait one RTT per batch.
    bool ActionIdle(std::uint64_t id) const noexcept {
        if(!live_ || !context_.host || !hooks_.ApplyAction)return false;
        for(const auto& l:leases_)if(l.view.id==id)return (l.view.phase==Phase::active || l.view.phase==Phase::draining) && !l.actionCount && (!blocked_ || l.view.phase==Phase::draining);
        return false;
    }
    bool CanCaptureAction(std::uint64_t id) const noexcept {
        if(!live_ || !context_.host || !hooks_.ApplyAction)return false;
        for(const auto& l:leases_)if(l.view.id==id && (l.view.phase==Phase::active || l.view.phase==Phase::draining) &&
            (!blocked_ || l.view.phase==Phase::draining) && l.actionCount<kActionWindow) {
            for(const auto& action:actionPool_)if(!action.used)return true;return false;
        }
        return false;
    }
    // Identity-bound candidate cancellation, including a Prepare that already assigned a lease. Completion needs
    // the authenticated host's no-lease ACK or the allocated lease's full native restore barrier.
    bool Cancel(const NativeId& source,const NativeId& target,std::uint64_t profile,std::uint64_t requestNonce,std::uint64_t now) noexcept;
    bool Exit(std::uint64_t lease,std::uint64_t now) noexcept;
    void Failed(std::uint64_t lease,std::uint64_t now) noexcept;
    void Reset(std::uint64_t now) noexcept;
    bool Ready() const noexcept;
    Result LastResult() const noexcept {return last_;}
    const RequestView* RequestFor(const NativeId& source) const noexcept {
        for(const auto& request:requests_)if(request.request && request.source==source)return &request;return nullptr;
    }
    const RequestView* RequestAt(std::size_t index) const noexcept {return index<kMaxLeases ? &requests_[index] : nullptr;}
    const LeaseView* LeaseAt(std::size_t index) const noexcept {return index<kMaxLeases ? &leases_[index].view : nullptr;}
    std::uint32_t ActiveCount() const noexcept {std::uint32_t n=0;for(const auto& l:leases_)n+=l.view.phase==Phase::active;return n;}
private:
    struct Peer {char puid[65]{};std::uint64_t requestHigh=0,hello=0,seen=0;bool capable=false;};
    struct Action {std::uint64_t sequence=0,since=0,retry=0;std::size_t size=0;std::array<std::uint8_t,kMaxPoseBytes> bytes{};std::array<bool,kMaxPeers+1> ack{},sent{};std::array<std::uint64_t,kMaxPeers+1> sentAt{};std::array<std::uint32_t,kMaxPeers+1> creditBytes{};std::array<std::uint16_t,kMaxPeers+1> creditPackets{};bool used=false;};
    struct Lease {
        LeaseView view{};std::uint64_t request=0,since=0,retry=0,ownerSequence=0,inputSequence=0,nextOwnerSequence=0;
        std::array<bool,kMaxPeers+1> prepared{},begun{},activated{},restored{},quiesced{};
        std::uint64_t requestedProfile=0;
        bool localOnly=false,revokeRequested=false,abortRequired=false,drainFault=false;Result ending=Result::Idle;
        std::uint64_t poseSequence=0;
        std::uint32_t poseParts=0,poseNext=0;std::size_t poseSize=0;
        std::array<std::uint8_t,kMaxPoseBytes> pose{};
        std::array<ActionIndex,kActionWindow> actions{};std::size_t actionHead=0,actionCount=0;
        std::uint64_t nextAction=0,actionSince=0,actionRetry=0,actionApplied=0,actionAttempt=UINT64_MAX,actionAttemptSequence=0,pendingAppliedAck=0,endToken=0,pendingEndToken=0,pendingAckResults=0;
        std::uint16_t pendingAckSignals=0,pendingAckSuccess=0;
        std::array<std::uint32_t,kMaxPeers+1> retiredCreditBytes{};std::array<std::uint16_t,kMaxPeers+1> retiredCreditPackets{};
        std::array<bool,kMaxPeers+1> actionAck{};
        Action receivingAction{};std::uint32_t actionParts=0,actionNext=0;bool actionPending=false;
    };
    struct Cancellation {std::uint64_t request=0,profile=0;NativeId source{},target{};char owner[65]{};};
    std::array<Cancellation,kMaxLeases> cancelled_{};
    std::array<Action,kMaxActions> actionPool_{}; // shared bounded pool, not a separate large payload pool per native lease
    Hooks hooks_{};Context context_{};char local_[65]{},hostPuid_[65]{};
    bool live_=false,welcomed_=false,blocked_=false,paced_=false;Result last_=Result::Idle;
    std::uint64_t transportFrameAt_=0,appliedAckAt_=0;std::size_t sendCursor_=0,ackCursor_=0;
    std::array<std::size_t,kMaxPeers+1> actionPeerBytes_{},actionPeerPackets_{};
    std::array<Message,kMaxLeases> orphanReplies_{};std::array<bool,kMaxLeases> orphanUsed_{};std::size_t orphanCursor_=0;bool preferOrphan_=false;
    std::uint64_t nextRequest_=0,nextLease_=0,helloSerial_=0,lastHello_=0,lastHost_=0,seenLease_=0;
    std::array<Peer,kMaxPeers+1> peers_{};std::array<Lease,kMaxLeases> leases_{};std::array<RequestView,kMaxLeases> requests_{};std::array<std::uint64_t,kMaxLeases> requestSince_{},cancelRetry_{};
    static bool Current(Phase p) noexcept {return p!=Phase::empty && p!=Phase::closed;}
    bool Unrestored() const noexcept {for(const auto& l:leases_)if(Current(l.view.phase) && !l.view.localRestored)return true;return false;}
    bool HasCurrent() const noexcept {for(const auto& l:leases_)if(Current(l.view.phase))return true;return false;}
    void RecordRequest(const Message&,std::uint64_t now) noexcept;
    void RejectRequest(std::uint64_t,Result) noexcept;
    void UpdateRequest(const Lease&) noexcept;
    bool HooksReady() const noexcept;
    bool Send(std::uint32_t peer,Message message) noexcept;
    bool Broadcast(Message message) noexcept;
    void Welcome() noexcept;
    Lease* Find(std::uint64_t id) noexcept {for(auto& l:leases_)if(l.view.id==id && l.view.phase!=Phase::empty)return &l;return nullptr;}
    Lease* Slot() noexcept {for(auto& l:leases_)if(!Current(l.view.phase) && l.view.localRestored)return &l;return nullptr;}
    bool Auth(std::uint32_t peer,const char* puid) const noexcept;
    void Reject(std::uint32_t peer,std::uint64_t request,Result result) noexcept;
    void HostRequest(std::uint32_t peer,const char* owner,const Message&,std::uint64_t now) noexcept;
    void HostInput(Lease&,const Message&,std::uint64_t now) noexcept;
    bool SendAction(Lease&,std::uint64_t now) noexcept;
    void AcceptAppliedAck(Lease&,std::uint32_t peer,std::uint64_t sequence,std::uint64_t now) noexcept;
    int SendOneAction(Lease&,std::uint64_t now,std::array<std::size_t,kMaxPeers+1>* quota=nullptr) noexcept;
    void ReleaseActionCredit(Action&) noexcept;
    void ReleaseRetiredCredit(Lease&,std::uint32_t peer) noexcept;
    void ReleaseActions(Lease&) noexcept;
    void TryAction(Lease&,std::uint64_t now) noexcept;
    bool ActionReply(Lease&,std::uint64_t sequence,bool ok,std::uint64_t now) noexcept;
    void HostCancel(std::uint32_t,const char*,const Message&,std::uint64_t now) noexcept;
    void CancelReply(std::uint32_t,const Message&,bool,Result) noexcept;
    void End(Lease&,Result,std::uint64_t now) noexcept;
    void FinishEnd(Lease&,Result,std::uint64_t now) noexcept;
    void StartDrain(Lease&,Result,std::uint64_t now) noexcept;
    void TickDrain(Lease&,std::uint64_t now) noexcept;
    void Retire(Lease&,Result,std::uint64_t now) noexcept;
    void DrainReply(Lease&,std::uint64_t now) noexcept;
    void Fault(Lease&,Result,std::uint64_t now) noexcept;
    void Restore(Lease&,std::uint64_t now,bool forceReply=false) noexcept;
    void Advance(Lease&,std::uint64_t now) noexcept;
    void Lose(std::uint64_t now) noexcept;
};

inline void Protocol::RecordRequest(const Message& m,std::uint64_t now) noexcept {
    RequestView* slot=nullptr;
    for(auto& r:requests_)if(r.request && r.source==m.spec.requesterPlayer){slot=&r;break;}
    if(!slot)for(auto& r:requests_)if(!r.request){slot=&r;break;}
    if(!slot){slot=&requests_[0];for(auto& r:requests_)if(r.request<slot->request)slot=&r;}
    *slot={};slot->request=m.request;slot->source=m.spec.requesterPlayer;slot->target=m.spec.target;
    slot->profileHash=m.spec.profileHash;slot->result=Result::Pending;requestSince_[static_cast<std::size_t>(slot-requests_.data())]=now;
}
inline void Protocol::RejectRequest(std::uint64_t id,Result result) noexcept {
    for(auto& r:requests_)if(r.request==id && !r.lease && r.result==Result::Pending){r.result=result;return;}
}
inline void Protocol::UpdateRequest(const Lease& l) noexcept {
    if(std::strcmp(l.view.spec.ownerPuid,local_))return;
    for(auto& r:requests_)if(r.request==l.request && r.source==l.view.spec.requesterPlayer && r.target==l.view.spec.target) {
        r.lease=l.view.id;r.profileHash=l.view.spec.profileHash;
        if(r.cancelPending && l.view.phase!=Phase::closed)r.result=l.view.result==Result::RestoreFailed ? Result::RestoreFailed : Result::Pending;
        else {r.result=l.view.result;if(l.view.phase==Phase::closed)r.cancelPending=false;}return;
    }
}
inline bool Protocol::HooksReady() const noexcept {
    return context_.localCapability==kCapability && hooks_.Send && hooks_.Authenticate && hooks_.Resolve && hooks_.Prepare && hooks_.Begin &&
        hooks_.ValidateInput && hooks_.ApplyInput && hooks_.Restore && hooks_.Activate;
}
inline bool Protocol::Configure(Hooks hooks) noexcept {
    if(HasCurrent() || Unrestored())return false;hooks_=hooks;return true;
}
inline bool Protocol::Ready() const noexcept {
    if(!live_ || blocked_ || !HooksReady())return false;
    for(const auto& l:leases_)if(l.view.phase==Phase::restoring)return false;
    if(!context_.host)return welcomed_;
    for(std::uint32_t p=1;p<=context_.peerCount;++p)if(!peers_[p].capable)return false;
    return true;
}
inline bool Protocol::Send(std::uint32_t peer,Message m) noexcept {
    const auto signal=AckSignalFor(m.kind);
    if(paced_ && !context_.host && peer==context_.hostPeer && signal) {
        auto* l=Find(m.lease);if(!l){
            for(std::size_t i=0;i<kMaxLeases;++i)if(orphanUsed_[i] && orphanReplies_[i].lease==m.lease && orphanReplies_[i].kind==m.kind){orphanReplies_[i]=m;return true;}
            for(std::size_t i=0;i<kMaxLeases;++i)if(!orphanUsed_[i]){orphanUsed_[i]=true;orphanReplies_[i]=m;return true;}return false;
        }l->pendingAckSignals|=signal;
        if(m.ok)l->pendingAckSuccess|=signal;else l->pendingAckSuccess=static_cast<std::uint16_t>(l->pendingAckSuccess&~signal);
        unsigned bit=0;while((1u<<bit)!=signal)++bit;l->pendingAckResults=(l->pendingAckResults&~(31ull<<(5*bit)))|(static_cast<std::uint64_t>(m.result)<<(5*bit));
        if(signal==ackApplied && m.sequence>l->pendingAppliedAck)l->pendingAppliedAck=m.sequence;
        if(signal==ackRestored)l->pendingEndToken=m.sequence;return true;
    }
    m.epoch=context_.epoch;std::uint8_t bytes[kWireBytes]{};const auto size=EncodedSize(m);
    return live_ && hooks_.Send && Encode(m,bytes,size) && hooks_.Send(hooks_.user,peer,bytes,size);
}
inline bool Protocol::Broadcast(Message m) noexcept {
    bool sent=true;for(std::uint32_t p=1;p<=context_.peerCount;++p)if(!Send(p,m))sent=false;return sent;
}
inline bool Protocol::Auth(std::uint32_t peer,const char* puid) const noexcept {
    if(!ValidPuid(puid) || !peer || peer>context_.peerCount)return false;
    const char* expected=context_.host ? peers_[peer].puid : hostPuid_;
    return expected[0] && !std::strcmp(expected,puid);
}
inline void Protocol::Welcome() noexcept {
    if(!context_.host)return;
    bool capable=HooksReady() && !blocked_;
    for(std::uint32_t p=1;p<=context_.peerCount;++p)capable=capable && peers_[p].capable;
    for(std::uint32_t p=1;p<=context_.peerCount;++p)if(peers_[p].puid[0]) {
        Message m;m.kind=Kind::Welcome;m.request=peers_[p].hello;m.ok=capable;m.capability=HooksReady() ? kCapability : 0;Send(p,m);
    }
}
inline void Protocol::Reject(std::uint32_t peer,std::uint64_t request,Result result) noexcept {
    last_=result;if(!peer){RejectRequest(request,result);return;}Message m;m.kind=Kind::Reject;m.request=request;m.result=result;Send(peer,m);
}
inline void Protocol::HostRequest(std::uint32_t peer,const char* owner,const Message& m,std::uint64_t now) noexcept {
    auto& p=peers_[peer];if(m.request<=p.requestHigh)return;p.requestHigh=m.request;
    if(!Ready()){Reject(peer,m.request,Result::Unavailable);return;}
    for(const auto& cancel:cancelled_)if(cancel.request && !std::strcmp(cancel.owner,owner) && cancel.source==m.spec.requesterPlayer && m.request<=cancel.request) {
        CancelReply(peer,m,true,Result::Complete);return;
    }
    if(!hooks_.Authenticate(hooks_.user,owner,m.spec.requesterPlayer)){Reject(peer,m.request,Result::Unauthorized);return;}
    for(const auto& l:leases_)if(Current(l.view.phase) && (l.view.spec.target==m.spec.target ||
        l.view.spec.requesterPlayer==m.spec.requesterPlayer)) {
        Reject(peer,m.request,Result::Conflict);return;
    }
    LeaseSpec spec=m.spec;std::memcpy(spec.ownerPuid,owner,std::strlen(owner)+1);const LeaseSpec before=spec;
    if(!hooks_.Resolve(hooks_.user,&spec) || !spec.profileHash || spec.requesterPlayer!=before.requesterPlayer ||
       spec.target!=before.target || std::strcmp(spec.ownerPuid,before.ownerPuid) ||
       (before.profileHash && before.profileHash!=spec.profileHash)) {Reject(peer,m.request,Result::Unsupported);return;}
    for(const auto& current:leases_)if(current.view.phase==Phase::preparing || current.view.phase==Phase::beginning || current.view.phase==Phase::activating){Reject(peer,m.request,Result::Unavailable);return;}
    auto* l=Slot();if(!l || nextLease_==UINT64_MAX){Reject(peer,m.request,Result::Unsupported);return;}
    *l=Lease{};l->view.id=++nextLease_;l->view.spec=spec;l->view.phase=Phase::preparing;l->view.localRestored=false;
    l->view.result=Result::Pending;l->request=m.request;l->requestedProfile=m.spec.profileHash;l->since=now;last_=Result::Pending;UpdateRequest(*l);
    l->prepared[0]=hooks_.Prepare(hooks_.user,l->view.id,spec);
    if(!l->prepared[0]){End(*l,Result::PrepareFailed,now);return;}
    Message prepare;prepare.kind=Kind::Prepare;prepare.lease=l->view.id;prepare.request=m.request;prepare.spec=spec;
    if(!Broadcast(prepare)){blocked_=true;End(*l,Result::SendFailed,now);return;}
    Advance(*l,now);
}
inline void Protocol::Restore(Lease& l,std::uint64_t now,bool forceReply) noexcept {
    const bool wasRestored=l.view.localRestored;
    if(!l.view.localRestored) {
        if(l.abortRequired && (!hooks_.AbortActions || hooks_.AbortActions(hooks_.user,l.view.id,l.view.spec)))l.abortRequired=false;
        l.view.localRestored=!l.abortRequired && hooks_.Restore && hooks_.Restore(hooks_.user,l.view.id,l.view.spec);
        if(!l.view.localRestored){l.view.result=Result::RestoreFailed;last_=Result::RestoreFailed;}
        else l.view.result=l.ending==Result::Complete ? Result::Pending : l.ending;
    }
    if(l.localOnly) {
        if(l.view.localRestored){l.view.phase=Phase::closed;l.view.result=Result::SessionLost;last_=Result::SessionLost;}
        UpdateRequest(l);return;
    }
    const bool notify=forceReply || (!paced_ && now-l.retry>=kRetryMs) || (!wasRestored && l.view.localRestored);
    if(context_.host)l.restored[0]=l.view.localRestored;
    else if(live_ && notify) {
        Message ack;ack.kind=Kind::Restored;ack.lease=l.view.id;ack.sequence=l.endToken;ack.ok=l.view.localRestored;
        ack.result=l.view.localRestored ? l.view.result : Result::RestoreFailed;Send(context_.hostPeer,ack);
        if(l.revokeRequested){Message fault;fault.kind=Kind::Fault;fault.lease=l.view.id;fault.result=l.view.result;fault.request=l.request;fault.spec=l.view.spec;Send(context_.hostPeer,fault);}
    }
    if(notify)l.retry=now;UpdateRequest(l);
}
inline void Protocol::DrainReply(Lease& l,std::uint64_t now) noexcept {
    Message m;m.kind=Kind::DrainAck;m.lease=l.view.id;m.request=l.request;m.spec=l.view.spec;m.ok=l.quiesced[0];
    Send(context_.hostPeer,m);l.retry=now;
}
inline void Protocol::StartDrain(Lease& l,Result result,std::uint64_t now) noexcept {
    if(l.view.phase!=Phase::draining) {
        l.view.phase=Phase::draining;l.ending=result;l.view.result=result==Result::Complete ? Result::Pending : result;
        l.since=now;l.retry=now;l.quiesced.fill(false);
    } else if(result!=Result::Complete) {l.ending=result;l.view.result=result;}
    l.drainFault=l.drainFault || result!=Result::Complete;
    if(!l.quiesced[0])l.quiesced[0]=!hooks_.QuiesceActions || hooks_.QuiesceActions(hooks_.user,l.view.id,l.view.spec);
    UpdateRequest(l);
    if(context_.host) {
        Message m;m.kind=Kind::Drain;m.lease=l.view.id;m.request=l.request;m.spec=l.view.spec;m.result=l.ending;
        Broadcast(m);
    } else {DrainReply(l,now);Message m;m.kind=Kind::Fault;m.lease=l.view.id;m.request=l.request;m.spec=l.view.spec;m.result=result;Send(context_.hostPeer,m);}
}
inline void Protocol::Retire(Lease& l,Result result,std::uint64_t now) noexcept {
    // Only the authenticated host's local deadline/body failure or a local context loss may retire undelivered work.
    if(context_.host){Message m;m.kind=Kind::Abort;m.lease=l.view.id;m.request=l.request;m.spec=l.view.spec;m.result=result;Broadcast(m);}
    l.abortRequired=true;FinishEnd(l,result,now);
}
inline void Protocol::TickDrain(Lease& l,std::uint64_t now) noexcept {
    const bool wasQuiesced=l.quiesced[0];
    if(!l.quiesced[0])l.quiesced[0]=!hooks_.QuiesceActions || hooks_.QuiesceActions(hooks_.user,l.view.id,l.view.spec);
    if(!context_.host) {
        if(l.actionPending)TryAction(l,now);
        if((!wasQuiesced && l.quiesced[0]) || (!paced_ && now-l.retry>=kRetryMs))DrainReply(l,now);return;
    }
    if(now-l.since>kTimeoutMs || (l.actionCount && now-l.actionSince>kTimeoutMs)){Retire(l,l.drainFault ? l.ending : Result::TimedOut,now);return;}
    if(l.actionCount && !paced_)SendAction(l,now);
    if(!paced_ && now-l.retry>=kRetryMs){Message m;m.kind=Kind::Drain;m.lease=l.view.id;m.request=l.request;m.spec=l.view.spec;m.result=l.ending;Broadcast(m);l.retry=now;}
    bool all=true;for(std::uint32_t p=0;p<=context_.peerCount;++p)all=all && l.quiesced[p];
    if(all && !l.actionCount && (!hooks_.ActionsEmpty || hooks_.ActionsEmpty(hooks_.user,l.view.id,l.view.spec)))FinishEnd(l,l.ending,now);
}
inline void Protocol::End(Lease& l,Result result,std::uint64_t now) noexcept {
    if((l.view.phase==Phase::active || l.view.phase==Phase::draining) && hooks_.QuiesceActions && hooks_.ActionsEmpty) {StartDrain(l,result,now);return;}
    FinishEnd(l,result,now);
}
inline void Protocol::FinishEnd(Lease& l,Result result,std::uint64_t now) noexcept {
    if(l.view.phase==Phase::closed || l.view.phase==Phase::empty)return;
    if(l.view.phase==Phase::restoring && result!=Result::Complete){l.ending=result;if(l.view.localRestored)l.view.result=result;}
    if(l.view.phase!=Phase::restoring){if(context_.host)l.endToken=1;ReleaseActions(l);l.view.phase=Phase::restoring;l.view.result=result==Result::Complete ? Result::Pending : result;l.ending=result;l.since=now;l.restored.fill(false);last_=l.view.result;}
    Message m;m.kind=Kind::End;m.lease=l.view.id;m.sequence=l.endToken;m.result=l.ending;Broadcast(m);Restore(l,now);Advance(l,now);
}
inline void Protocol::Fault(Lease& l,Result result,std::uint64_t now) noexcept {
    if(context_.host){End(l,result,now);return;}
    if((l.view.phase==Phase::active || l.view.phase==Phase::draining) && hooks_.QuiesceActions && hooks_.ActionsEmpty){StartDrain(l,result,now);return;}
    if(l.view.phase==Phase::closed || l.view.phase==Phase::empty)return;
    ReleaseActions(l);l.view.phase=Phase::restoring;l.view.result=result;l.ending=result;l.since=now;l.revokeRequested=true;Restore(l,now);
}
inline void Protocol::Advance(Lease& l,std::uint64_t now) noexcept {
    if(!context_.host || !live_)return;
    if(l.view.phase==Phase::preparing) {
        for(std::uint32_t p=0;p<=context_.peerCount;++p)if(!l.prepared[p])return;
        l.view.phase=Phase::beginning;l.since=now;l.view.started=true;
        l.begun[0]=hooks_.Begin(hooks_.user,l.view.id,l.view.spec);
        if(!l.begun[0]){End(l,Result::BeginFailed,now);return;}
        Message m;m.kind=Kind::Begin;m.lease=l.view.id;
        if(!Broadcast(m)){blocked_=true;End(l,Result::SendFailed,now);return;}
    }
    if(l.view.phase==Phase::beginning) {
        for(std::uint32_t p=0;p<=context_.peerCount;++p)if(!l.begun[p])return;
        l.view.phase=Phase::activating;l.since=now;
        l.activated[0]=hooks_.Activate(hooks_.user,l.view.id,l.view.spec);
        if(!l.activated[0]){End(l,Result::BeginFailed,now);return;}
        Message m;m.kind=Kind::Activate;m.lease=l.view.id;
        if(!Broadcast(m)){blocked_=true;End(l,Result::SendFailed,now);return;}
    }
    if(l.view.phase==Phase::activating) {
        for(std::uint32_t p=0;p<=context_.peerCount;++p)if(!l.activated[p])return;
        l.view.phase=Phase::active;l.view.result=Result::Active;last_=Result::Active;UpdateRequest(l);
        Message m;m.kind=Kind::Active;m.lease=l.view.id;
        if(!Broadcast(m)){blocked_=true;End(l,Result::SendFailed,now);return;}
    }
    if(l.view.phase==Phase::restoring) {
        for(std::uint32_t p=0;p<=context_.peerCount;++p)if(!l.restored[p])return;
        Message m;m.kind=Kind::Ended;m.lease=l.view.id;m.result=l.ending;
        if(!Broadcast(m)){blocked_=true;last_=Result::SendFailed;return;}
        l.view.phase=Phase::closed;l.view.result=l.ending;last_=l.ending;UpdateRequest(l);
    }
}
inline void Protocol::HostInput(Lease& l,const Message& m,std::uint64_t now) noexcept {
    if(l.view.phase!=Phase::active || m.sequence<=l.ownerSequence)return;
    if(blocked_){End(l,Result::Unavailable,now);return;}
    if(m.spec.profileHash!=l.view.spec.profileHash || l.inputSequence==UINT64_MAX ||
       !hooks_.Authenticate(hooks_.user,l.view.spec.ownerPuid,l.view.spec.requesterPlayer)){End(l,Result::InvalidInput,now);return;}
    const InputMeta meta{l.inputSequence+1,InputSeed(context_.epoch,l.view.id,l.inputSequence+1)};
    Message accepted=m;
    if((hooks_.NormalizeInput && !hooks_.NormalizeInput(hooks_.user,l.view.id,l.view.spec,meta,accepted.input.data(),accepted.inputSize)) ||
       !hooks_.ValidateInput(hooks_.user,l.view.id,l.view.spec,meta,accepted.input.data(),accepted.inputSize)){End(l,Result::InvalidInput,now);return;}
    l.ownerSequence=m.sequence;l.inputSequence=meta.sequence;
    accepted.kind=Kind::Input;accepted.sequence=meta.sequence;accepted.request=m.sequence;accepted.seed=meta.seed;
    if(!Broadcast(accepted)){blocked_=true;End(l,Result::SendFailed,now);return;}
    if(!hooks_.ApplyInput(hooks_.user,l.view.id,l.view.spec,meta,accepted.input.data(),accepted.inputSize))End(l,Result::ApplyFailed,now);
}
inline bool Protocol::Request(const NativeId& player,const NativeId& target,std::uint64_t profile,std::uint64_t now) noexcept {
    if(!CanonicalId(player) || !CanonicalId(target) || nextRequest_==UINT64_MAX){last_=Result::Unauthorized;return false;}
    Message m;m.kind=Kind::Request;m.request=++nextRequest_;m.spec.requesterPlayer=player;m.spec.target=target;m.spec.profileHash=profile;
    RecordRequest(m,now);
    if(!Ready()){last_=Result::Unavailable;RejectRequest(m.request,last_);return false;}
    last_=Result::Pending;
    if(context_.host){HostRequest(0,local_,m,now);return last_==Result::Pending || last_==Result::Active;}
    if(!Send(context_.hostPeer,m)){last_=Result::SendFailed;RejectRequest(m.request,last_);return false;}return true;
}
inline void Protocol::CancelReply(std::uint32_t peer,const Message& request,bool ok,Result result) noexcept {
    if(!peer) {
        for(auto& r:requests_)if(r.request==request.request && r.source==request.spec.requesterPlayer && r.target==request.spec.target && r.profileHash==request.spec.profileHash && r.cancelPending) {
            r.result=result;r.cancelPending=false;last_=result;return;
        }
        return;
    }
    Message reply=request;reply.kind=Kind::CancelAck;reply.ok=ok;reply.result=result;Send(peer,reply);
}
inline void Protocol::HostCancel(std::uint32_t peer,const char* owner,const Message& m,std::uint64_t now) noexcept {
    if(!hooks_.Authenticate(hooks_.user,owner,m.spec.requesterPlayer)){CancelReply(peer,m,false,Result::Unauthorized);return;}
    for(auto& l:leases_)if(l.view.phase!=Phase::empty && l.request==m.request &&
        !std::strcmp(l.view.spec.ownerPuid,owner) && l.view.spec.requesterPlayer==m.spec.requesterPlayer) {
        if(l.view.spec.target!=m.spec.target || (m.spec.profileHash!=l.view.spec.profileHash && m.spec.profileHash!=l.requestedProfile)) {
            CancelReply(peer,m,false,Result::Unauthorized);return;
        }
        if(Current(l.view.phase))End(l,Result::Complete,now);
        else if(l.view.localRestored)CancelReply(peer,m,true,l.view.result);
        return;
    }
    Cancellation* mark=nullptr;
    for(auto& c:cancelled_)if(c.request && c.source==m.spec.requesterPlayer && !std::strcmp(c.owner,owner)){mark=&c;break;}
    if(mark && mark->request==m.request && (mark->target!=m.spec.target || mark->profile!=m.spec.profileHash)) {
        CancelReply(peer,m,false,Result::Unauthorized);return;
    }
    if(!mark)for(auto& c:cancelled_)if(!c.request){mark=&c;break;}
    if(!mark){CancelReply(peer,m,false,Result::Unsupported);return;}
    if(m.request>=mark->request) {
        *mark={};mark->request=m.request;mark->source=m.spec.requesterPlayer;mark->target=m.spec.target;mark->profile=m.spec.profileHash;
        std::memcpy(mark->owner,owner,std::strlen(owner)+1);
    }
    CancelReply(peer,m,true,Result::Complete);
}
inline bool Protocol::Cancel(const NativeId& source,const NativeId& target,std::uint64_t profile,std::uint64_t nonce,std::uint64_t now) noexcept {
    RequestView* request=nullptr;
    for(auto& r:requests_)if(r.request && r.source==source){request=&r;break;}
    if(!live_ || !request || request->request!=nonce || request->target!=target || request->profileHash!=profile ||
       !hooks_.Authenticate(hooks_.user,local_,source))return false;
    if(request->result==Result::Complete && !request->cancelPending)return true;
    if(!request->cancelPending && request->result!=Result::Pending && request->result!=Result::Active && request->result!=Result::RestoreFailed)return false;
    const auto index=static_cast<std::size_t>(request-requests_.data());
    if(!request->cancelPending){request->cancelPending=true;request->result=Result::Pending;requestSince_[index]=now;}
    Message m;m.kind=Kind::Cancel;m.request=nonce;m.spec.requesterPlayer=source;m.spec.target=target;m.spec.profileHash=profile;
    last_=request->result;cancelRetry_[index]=now;
    if(context_.host){HostCancel(0,local_,m,now);return request->cancelPending || request->result==Result::Complete;}
    if(!Send(context_.hostPeer,m)){request->result=Result::SendFailed;last_=Result::SendFailed;return false;}return true;
}
inline bool Protocol::SubmitInput(std::uint64_t id,const void* bytes,std::size_t size,std::uint64_t now) noexcept {
    auto* l=Find(id);
    if(!live_ || blocked_ || !l || l->view.phase!=Phase::active || std::strcmp(l->view.spec.ownerPuid,local_))return false;
    const auto* localRequest=RequestFor(l->view.spec.requesterPlayer);
    if(localRequest && localRequest->request==l->request && localRequest->cancelPending)return false;
    if(!bytes || !size || size>kMaxInputBytes || l->nextOwnerSequence==UINT64_MAX){Fault(*l,Result::InvalidInput,now);return false;}
    Message m;m.kind=Kind::InputRequest;m.lease=id;m.sequence=++l->nextOwnerSequence;m.spec.profileHash=l->view.spec.profileHash;
    m.inputSize=static_cast<std::uint16_t>(size);std::memcpy(m.input.data(),bytes,size);
    if(context_.host){HostInput(*l,m,now);return l->view.phase==Phase::active;}
    if(!Send(context_.hostPeer,m)){Fault(*l,Result::SendFailed,now);return false;}return true;
}
inline bool Protocol::SubmitPose(std::uint64_t id,const void* bytes,std::size_t size,std::uint64_t now) noexcept {
    auto* l=Find(id);
    if(!live_ || blocked_ || !context_.host || !l || l->view.phase!=Phase::active || !hooks_.ApplyPose)return false;
    if(!bytes || !size || size>kMaxPoseBytes || l->poseSequence==UINT64_MAX){End(*l,Result::InvalidInput,now);return false;}
    const auto count=static_cast<std::uint32_t>((size+kChunkBytes-1)/kChunkBytes);
    const auto sequence=++l->poseSequence;
    for(std::uint32_t index=0;index<count;++index) {
        Message m;m.kind=Kind::Pose;m.lease=id;m.request=l->request;m.sequence=sequence;m.spec=l->view.spec;
        m.capability=(count<<16)|index;
        const auto offset=static_cast<std::size_t>(index)*kChunkBytes;
        const auto remaining=size-offset;m.inputSize=static_cast<std::uint16_t>(remaining<kChunkBytes ? remaining : kChunkBytes);
        std::memcpy(m.input.data(),static_cast<const std::uint8_t*>(bytes)+offset,m.inputSize);
        if(!Broadcast(m)){blocked_=true;End(*l,Result::SendFailed,now);return false;}
    }
    return true;
}
inline void Protocol::ReleaseActionCredit(Action& action) noexcept {
    for(std::size_t peer=1;peer<=kMaxPeers;++peer){const auto credit=action.creditBytes[peer];const auto packets=action.creditPackets[peer];actionPeerBytes_[peer]=actionPeerBytes_[peer]>=credit ? actionPeerBytes_[peer]-credit : 0;actionPeerPackets_[peer]=actionPeerPackets_[peer]>=packets ? actionPeerPackets_[peer]-packets : 0;action.creditBytes[peer]=0;action.creditPackets[peer]=0;}
}
inline void Protocol::ReleaseRetiredCredit(Lease& l,std::uint32_t peer) noexcept {
    const auto bytes=l.retiredCreditBytes[peer];const auto packets=l.retiredCreditPackets[peer];actionPeerBytes_[peer]=actionPeerBytes_[peer]>=bytes ? actionPeerBytes_[peer]-bytes : 0;actionPeerPackets_[peer]=actionPeerPackets_[peer]>=packets ? actionPeerPackets_[peer]-packets : 0;l.retiredCreditBytes[peer]=0;l.retiredCreditPackets[peer]=0;
}
inline void Protocol::ReleaseActions(Lease& l) noexcept {
    for(std::size_t i=0;i<l.actionCount;++i){auto& action=actionPool_[l.actions[(l.actionHead+i)%kActionWindow]];
        if(paced_ && live_ && context_.host){for(std::size_t peer=1;peer<=kMaxPeers;++peer){l.retiredCreditBytes[peer]+=action.creditBytes[peer];l.retiredCreditPackets[peer]=static_cast<std::uint16_t>(l.retiredCreditPackets[peer]+action.creditPackets[peer]);action.creditBytes[peer]=0;action.creditPackets[peer]=0;}}
        else ReleaseActionCredit(action);action={};}
    l.actionCount=l.actionHead=0;
}
inline int Protocol::SendOneAction(Lease& l,std::uint64_t now,std::array<std::size_t,kMaxPeers+1>* quota) noexcept {
    if(!l.actionCount || (l.view.phase!=Phase::active && l.view.phase!=Phase::draining))return 0;
    for(std::size_t queued=0;queued<l.actionCount;++queued) {
        auto& action=actionPool_[l.actions[(l.actionHead+queued)%kActionWindow]];
        for(std::uint32_t peer=1;peer<=context_.peerCount;++peer) {
            // The paced backend is the real SDK ReliableOrdered route. Its receiver delivers a link sequence
            // once; retransmission belongs to that SDK. Re-sending an application Action would create a new
            // SDK sequence which an earlier Applied ACK cannot prove consumed.
            if(action.ack[peer] || (action.sent[peer] && (paced_ || now-action.sentAt[peer]<kActionRetryMs)) || (quota && !(*quota)[peer]))continue;
            const auto credit=kWirePrefixBytes+action.size+kActionSdkFramingBytes;
            if(paced_ && (actionPeerBytes_[peer]+credit>=kActionPeerByteCredits || actionPeerPackets_[peer]>=kActionPeerPacketCredits))continue;
            Message m;m.kind=Kind::Action;m.lease=l.view.id;m.request=l.request;m.sequence=action.sequence;m.spec=l.view.spec;
            m.capability=1u<<16;m.inputSize=static_cast<std::uint16_t>(action.size);std::memcpy(m.input.data(),action.bytes.data(),action.size);
            if(!Send(peer,m)){blocked_=true;End(l,Result::SendFailed,now);return -1;}
            action.sent[peer]=true;action.sentAt[peer]=now;l.actionRetry=now;
            if(paced_){actionPeerBytes_[peer]+=credit;action.creditBytes[peer]+=static_cast<std::uint32_t>(credit);++actionPeerPackets_[peer];++action.creditPackets[peer];}
            if(quota)--(*quota)[peer];return 1;
        }
    }
    return 0;
}
inline bool Protocol::SendAction(Lease& l,std::uint64_t now) noexcept {
    if(paced_)return true; // queued in the native pool until a real shared frame grants sender quota
    int sent=0;while((sent=SendOneAction(l,now))>0){}return sent>=0;
}
inline void Protocol::TransportFrame(std::uint64_t now) noexcept {
    if(!paced_ || !live_ || (transportFrameAt_ && now-transportFrameAt_<kActionFrameMs))return;
    transportFrameAt_=now;
    if(context_.host) {
        std::array<std::size_t,kMaxPeers+1> quota{};quota.fill(kActionPeerFramePackets);
        for(std::size_t packet=0;packet<kActionFramePackets;++packet) {
            bool progress=false;
            for(std::size_t offset=0;offset<kMaxLeases;++offset) {
                const auto index=(sendCursor_+offset)%kMaxLeases;auto& l=leases_[index];const auto sent=SendOneAction(l,now,&quota);
                if(sent<0)return;
                if(sent>0){sendCursor_=(index+1)%kMaxLeases;progress=true;break;}
            }
            if(!progress)break;
        }
    } else if(!appliedAckAt_ || now-appliedAckAt_>=kAppliedAckMs) {
        std::array<Lease*,kMaxLeases> pending{};std::size_t count=0;
        for(std::size_t offset=0;offset<kMaxLeases && count<kAckBatchEntries;++offset){auto& l=leases_[(ackCursor_+offset)%kMaxLeases];if(l.pendingAckSignals)pending[count++]=&l;}
        std::size_t orphan=kMaxLeases;for(std::size_t i=0;i<kMaxLeases;++i){const auto index=(orphanCursor_+i)%kMaxLeases;if(orphanUsed_[index]){orphan=index;break;}}
        if(orphan<kMaxLeases && (!count || preferOrphan_)){
            auto m=orphanReplies_[orphan];m.epoch=context_.epoch;std::uint8_t replyBytes[kWireBytes]{};const auto replySize=EncodedSize(m);
            if(!hooks_.Send || !Encode(m,replyBytes,replySize) || !hooks_.Send(hooks_.user,context_.hostPeer,replyBytes,replySize)){Lose(now);return;}
            orphanUsed_[orphan]=false;orphanCursor_=(orphan+1)%kMaxLeases;appliedAckAt_=now;preferOrphan_=false;return;
        }
        if(!count)return;preferOrphan_=true;
        for(std::size_t i=1;i<count;++i){auto* value=pending[i];std::size_t j=i;while(j && pending[j-1]->view.id>value->view.id){pending[j]=pending[j-1];--j;}pending[j]=value;}
        Message m;m.kind=Kind::ActionAckBatch;m.ok=true;m.result=Result::Complete;m.input[0]=2;m.input[1]=static_cast<std::uint8_t>(count);m.inputSize=static_cast<std::uint16_t>(8+kAckEntryBytes*count);auto* p=m.input.data()+8;
        for(std::size_t i=0;i<count;++i){wire::Put(p,pending[i]->view.id,8);wire::Put(p,pending[i]->request,8);wire::Put(p,pending[i]->pendingAppliedAck,8);wire::Put(p,pending[i]->pendingEndToken,8);wire::Put(p,pending[i]->pendingAckSignals,2);wire::Put(p,pending[i]->pendingAckSuccess,2);wire::Put(p,pending[i]->pendingAckResults,8);wire::Put(p,0,4);}
        if(!Send(context_.hostPeer,m)){for(std::size_t i=0;i<count;++i)Fault(*pending[i],Result::SendFailed,now);return;}
        for(std::size_t i=0;i<count;++i){pending[i]->pendingAppliedAck=0;pending[i]->pendingEndToken=0;pending[i]->pendingAckSignals=pending[i]->pendingAckSuccess=0;pending[i]->pendingAckResults=0;}ackCursor_=(ackCursor_+count)%kMaxLeases;appliedAckAt_=now;
    }
}
inline void Protocol::AcceptAppliedAck(Lease& l,std::uint32_t peer,std::uint64_t sequence,std::uint64_t) noexcept {
    Action* acknowledged=nullptr;for(std::size_t i=0;i<l.actionCount;++i){auto& item=actionPool_[l.actions[(l.actionHead+i)%kActionWindow]];if(item.sequence==sequence){acknowledged=&item;break;}}
    if(!acknowledged || !acknowledged->sent[peer])return;
    for(std::size_t i=0;i<l.actionCount;++i){const auto& item=actionPool_[l.actions[(l.actionHead+i)%kActionWindow]];if(item.sequence<=sequence && !item.sent[peer])return;}
    for(std::size_t i=0;i<l.actionCount;++i){auto& item=actionPool_[l.actions[(l.actionHead+i)%kActionWindow]];if(item.sequence<=sequence){item.ack[peer]=true;const auto credit=item.creditBytes[peer];const auto packets=item.creditPackets[peer];actionPeerBytes_[peer]=actionPeerBytes_[peer]>=credit ? actionPeerBytes_[peer]-credit : 0;actionPeerPackets_[peer]=actionPeerPackets_[peer]>=packets ? actionPeerPackets_[peer]-packets : 0;item.creditBytes[peer]=0;item.creditPackets[peer]=0;}}
    while(l.actionCount){auto& head=actionPool_[l.actions[l.actionHead]];bool all=true;for(std::uint32_t p=0;p<=context_.peerCount;++p)all=all && head.ack[p];if(!all)break;head={};l.actionHead=(l.actionHead+1)%kActionWindow;--l.actionCount;}
    if(l.actionCount)l.actionSince=actionPool_[l.actions[l.actionHead]].since;
}
inline bool Protocol::SubmitAction(std::uint64_t id,const void* bytes,std::size_t size,std::uint64_t now) noexcept {
    auto* l=Find(id);
    if(!live_ || !context_.host || !l || (l->view.phase!=Phase::active && l->view.phase!=Phase::draining) || (blocked_ && l->view.phase!=Phase::draining) || !hooks_.ApplyAction)return false;
    if(!bytes || !size || size>kMaxPoseBytes || l->nextAction==UINT64_MAX || l->actionCount>=kActionWindow) {
        End(*l,Result::ApplyFailed,now);return false;
    }
    if(!context_.peerCount){++l->nextAction;return true;}
    std::size_t free=0;while(free<kMaxActions && actionPool_[free].used)++free;
    if(free==kMaxActions){End(*l,Result::ApplyFailed,now);return false;}
    auto& action=actionPool_[free];action={};action.used=true;action.sequence=++l->nextAction;action.since=now;action.ack[0]=true;
    l->actions[(l->actionHead+l->actionCount)%kActionWindow]=static_cast<ActionIndex>(free);
    action.size=size;std::memcpy(action.bytes.data(),bytes,size);++l->actionCount;
    if(l->actionCount==1)l->actionSince=now;
    return SendAction(*l,now);
}
inline bool Protocol::ActionReply(Lease& l,std::uint64_t sequence,bool ok,std::uint64_t now) noexcept {
    Message m;m.kind=Kind::ActionAck;m.lease=l.view.id;m.request=l.request;m.sequence=sequence;m.spec=l.view.spec;m.ok=ok;
    m.result=ok ? Result::Complete : Result::ApplyFailed;
    if(Send(context_.hostPeer,m))return true;Fault(l,Result::SendFailed,now);return false;
}
inline void Protocol::TryAction(Lease& l,std::uint64_t now) noexcept {
    if((l.view.phase!=Phase::active && l.view.phase!=Phase::draining) || !l.actionCount)return;
    while(l.actionCount) {
        auto& action=actionPool_[l.actions[l.actionHead]];
        if(l.actionAttempt==now && l.actionAttemptSequence==action.sequence)return;
        l.actionAttempt=now;l.actionAttemptSequence=action.sequence;
        const auto result=hooks_.ApplyAction ? hooks_.ApplyAction(hooks_.user,l.view.id,l.view.spec,action.sequence,action.bytes.data(),action.size) : ActionResult::failed;
        if(result==ActionResult::pending){l.actionPending=true;return;}
        if(result!=ActionResult::applied){ActionReply(l,action.sequence,false,now);Fault(l,Result::ApplyFailed,now);return;}
        const auto sequence=action.sequence;l.actionApplied=sequence;action={};l.actionHead=(l.actionHead+1)%kActionWindow;--l.actionCount;
        l.actionPending=l.actionCount!=0;if(l.actionCount)l.actionSince=actionPool_[l.actions[l.actionHead]].since;
        if(!ActionReply(l,sequence,true,now))return;
        if(l.view.phase!=Phase::active && l.view.phase!=Phase::draining)return;
    }
}
inline bool Protocol::Exit(std::uint64_t id,std::uint64_t now) noexcept {
    auto* l=Find(id);if(!live_ || !l || !Current(l->view.phase) || (!context_.host && std::strcmp(l->view.spec.ownerPuid,local_)))return false;
    if(context_.host)End(*l,Result::Complete,now);
    else {Message m;m.kind=Kind::Exit;m.lease=id;if(!Send(context_.hostPeer,m)){Fault(*l,Result::SendFailed,now);return false;}last_=Result::Pending;}
    return true;
}
inline void Protocol::Failed(std::uint64_t id,std::uint64_t now) noexcept {if(auto* l=Find(id))Fault(*l,Result::ApplyFailed,now);}
inline void Protocol::Lose(std::uint64_t now) noexcept {
    if(live_ && context_.host)for(auto& l:leases_)if(Current(l.view.phase)) {
        l.endToken=1;Message m;m.kind=Kind::End;m.lease=l.view.id;m.request=l.request;m.sequence=l.endToken;m.result=Result::SessionLost;Broadcast(m);
    }
    live_=false;welcomed_=false;blocked_=true;
    for(auto& request:requests_)if(request.request && !request.lease && request.result==Result::Pending)request.result=Result::SessionLost;
    for(auto& l:leases_)if(Current(l.view.phase)) {
        ReleaseActions(l);l.abortRequired=true;l.view.phase=Phase::restoring;l.view.result=Result::SessionLost;l.ending=Result::SessionLost;l.localOnly=true;l.since=now;Restore(l,now);
    }
    actionPeerBytes_={};actionPeerPackets_={};orphanUsed_={};
    for(auto& l:leases_){l.retiredCreditBytes={};l.retiredCreditPackets={};l.pendingAppliedAck=l.pendingEndToken=l.pendingAckResults=0;l.pendingAckSignals=l.pendingAckSuccess=0;}
    last_=Unrestored() ? Result::RestoreFailed : Result::SessionLost;
}
inline void Protocol::Reset(std::uint64_t now) noexcept {Lose(now);}
inline void Protocol::Tick(const Context& next,std::uint64_t now) noexcept {
    const bool valid=next.ready && next.epoch && ValidPuid(next.localPuid) && next.peerCount<=kMaxPeers &&
        (next.host ? next.hostPeer==0 : next.hostPeer>0 && next.hostPeer<=next.peerCount);
    if(!valid) {
        if(live_)Lose(now);
        for(auto& l:leases_)if(l.view.phase==Phase::restoring && (!l.view.localRestored || now-l.retry>=kRetryMs))Restore(l,now);
        if(next.peerCount>kMaxPeers)last_=Result::Unsupported;
        else if(!HasCurrent() && last_!=Result::SessionLost)last_=Result::Unavailable;
        return;
    }
    const bool same=context_.epoch==next.epoch && context_.host==next.host && context_.hostPeer==next.hostPeer &&
        context_.peerCount==next.peerCount && context_.localCapability==next.localCapability && local_[0] && !std::strcmp(local_,next.localPuid);
    if(live_ && !same)Lose(now);
    if(!live_) {
        for(auto& l:leases_)if(l.view.phase==Phase::restoring)Restore(l,now);
        if(Unrestored())return;
        if(helloSerial_==UINT64_MAX){last_=Result::Unavailable;return;}
        if(!same){peers_={};seenLease_=0;leases_={};requests_={};requestSince_={};cancelRetry_={};cancelled_={};}
        for(auto& p:peers_){p.capable=false;p.seen=0;}
        context_=next;std::memset(local_,0,sizeof(local_));std::memcpy(local_,next.localPuid,std::strlen(next.localPuid)+1);
        context_.localPuid=local_;std::memset(hostPuid_,0,sizeof(hostPuid_));
        live_=true;blocked_=false;welcomed_=false;++helloSerial_;lastHello_=now>=kHelloMs ? now-kHelloMs : 0;lastHost_=0;
        std::memcpy(peers_[0].puid,local_,sizeof(local_));peers_[0].capable=true;
        last_=Result::Unavailable;
    }
    if(now-lastHello_>=kHelloMs) {
        if(context_.host)Welcome();
        else {Message hello;hello.kind=Kind::Hello;hello.request=helloSerial_;hello.capability=HooksReady() ? kCapability : 0;Send(context_.hostPeer,hello);}
        lastHello_=now;
    }
    if(context_.host) {
        for(std::uint32_t p=1;p<=context_.peerCount;++p)if(peers_[p].capable && now-peers_[p].seen>kTimeoutMs) {
            peers_[p].capable=false;blocked_=true;
            for(auto& l:leases_)if(Current(l.view.phase))End(l,Result::SessionLost,now);
            Welcome();last_=Result::SessionLost;
        }
    } else if(welcomed_ && lastHost_ && now-lastHost_>kTimeoutMs) {Lose(now);return;}
    for(std::size_t i=0;i<requests_.size();++i) {
        auto& request=requests_[i];
        if(request.request && !request.lease && request.result==Result::Pending && now-requestSince_[i]>kTimeoutMs) {
            request.result=Result::TimedOut;if(request.request==nextRequest_)last_=Result::TimedOut;
        }
        if(request.cancelPending && now-cancelRetry_[i]>=kRetryMs)Cancel(request.source,request.target,request.profileHash,request.request,now);
    }
    if(blocked_)for(auto& l:leases_)if(Current(l.view.phase) && l.view.phase!=Phase::restoring && l.view.phase!=Phase::draining)Fault(l,Result::SessionLost,now);
    for(auto& l:leases_) {
        if((l.view.phase==Phase::preparing || l.view.phase==Phase::beginning || l.view.phase==Phase::activating) && now-l.since>kTimeoutMs) {
            blocked_=true;Fault(l,Result::TimedOut,now);
        }
        if(l.view.phase==Phase::draining){TickDrain(l,now);continue;}
        if(l.view.phase==Phase::restoring) {
            if(now-l.since>kTimeoutMs){blocked_=true;l.ending=Result::TimedOut;if(l.view.localRestored)l.view.result=Result::TimedOut;UpdateRequest(l);}
            if(now-l.retry>=kRetryMs){if(context_.host && !l.localOnly){if(!paced_)End(l,l.ending,now);else {Restore(l,now);Advance(l,now);}}else Restore(l,now);}
            else if(!l.view.localRestored)Restore(l,now); // native cleanup retries next tick; only wire retransmits are throttled
        } else if(l.view.phase==Phase::active) {
            if(context_.host && l.actionCount) {
                if(now-l.actionSince>kTimeoutMs)End(l,Result::TimedOut,now);
                else if(!paced_)SendAction(l,now);
            } else if(!context_.host && l.actionPending) {
                if(now-l.actionSince>kTimeoutMs)Fault(l,Result::TimedOut,now);else TryAction(l,now);
            }
        } else if(context_.host)Advance(l,now);
    }
}
inline bool Protocol::Receive(std::uint32_t peer,const char* authenticatedPuid,const void* bytes,std::size_t size,std::uint64_t now) noexcept {
    if(!Owned(bytes,size))return false;
    Message m;if(!Decode(bytes,size,m) || !live_ || m.epoch!=context_.epoch || !peer || peer>context_.peerCount ||
       !ValidPuid(authenticatedPuid))return true;
    if(context_.host && m.kind==Kind::Hello) {
        for(std::uint32_t p=0;p<=context_.peerCount;++p)if(p!=peer && peers_[p].puid[0] && !std::strcmp(peers_[p].puid,authenticatedPuid))return true;
        auto& p=peers_[peer];
        if(p.puid[0] && std::strcmp(p.puid,authenticatedPuid)) {
            blocked_=true;p.capable=false;for(auto& l:leases_)if(Current(l.view.phase))End(l,Result::SessionLost,now);Welcome();return true;
        }
        if(m.request<p.hello)return true;
        if(p.hello && m.request>p.hello && HasCurrent()) {
            blocked_=true;for(auto& l:leases_)if(Current(l.view.phase))End(l,Result::SessionLost,now);
        }
        std::memcpy(p.puid,authenticatedPuid,std::strlen(authenticatedPuid)+1);p.hello=m.request;p.seen=now;
        p.capable=m.capability==kCapability;
        if(!p.capable && HasCurrent()){blocked_=true;for(auto& l:leases_)if(Current(l.view.phase))End(l,Result::Unsupported,now);}
        Welcome();return true;
    }
    if(!context_.host && peer==context_.hostPeer && m.kind==Kind::Welcome) {
        if(m.request!=helloSerial_)return true;
        if(hostPuid_[0] && std::strcmp(hostPuid_,authenticatedPuid))return true;
        std::memcpy(hostPuid_,authenticatedPuid,std::strlen(authenticatedPuid)+1);lastHost_=now;welcomed_=m.ok && m.capability==kCapability;
        if(!m.ok && HasCurrent())for(auto& l:leases_)if(Current(l.view.phase))Fault(l,Result::Unavailable,now);
        return true;
    }
    if((!context_.host && peer!=context_.hostPeer) || !Auth(peer,authenticatedPuid))return true;
    if(context_.host) {
        if(m.kind==Kind::Request){HostRequest(peer,authenticatedPuid,m,now);return true;}
        if(m.kind==Kind::Cancel){HostCancel(peer,authenticatedPuid,m,now);return true;}
        if(m.kind==Kind::ActionAckBatch) {
            const auto count=AppliedAckCount(m);
            std::array<bool,kAckBatchEntries> valid{};
            for(std::size_t i=0;i<count;++i){const auto ack=AppliedAckAt(m,i);const auto* current=Find(ack.lease);valid[i]=current && current->request==ack.request;}
            constexpr Kind kinds[]={Kind::ActionAck,Kind::Prepared,Kind::Started,Kind::Activated,Kind::DrainAck,Kind::Restored,Kind::Fault};
            for(std::size_t i=0;i<count;++i){if(!valid[i])continue;const auto ack=AppliedAckAt(m,i);auto* current=Find(ack.lease);if(!current || current->request!=ack.request)continue;
                constexpr unsigned order[]={6,0,1,2,3,4,5};
                for(unsigned index=0;index<7;++index){const unsigned bit=order[index];if(!(ack.signals&(1ull<<bit)))continue;Message status;status.kind=kinds[bit];status.epoch=context_.epoch;status.lease=ack.lease;status.request=ack.request;status.spec=current->view.spec;status.sequence=bit==0 ? ack.sequence : bit==5 ? ack.endToken : 0;status.ok=(ack.success&(1u<<bit))!=0;status.result=AckResult(ack,bit);std::uint8_t ackBytes[kWireBytes]{};const auto ackSize=EncodedSize(status);if(Encode(status,ackBytes,ackSize))Receive(peer,authenticatedPuid,ackBytes,ackSize,now);}
            }
            return true;
        }
        auto* l=Find(m.lease);if(!l || !Current(l->view.phase))return true;
        if(m.kind==Kind::InputRequest) {
            if(std::strcmp(l->view.spec.ownerPuid,authenticatedPuid)){last_=Result::Unauthorized;return true;}
            HostInput(*l,m,now);
        } else if(m.kind==Kind::ActionAck && (l->view.phase==Phase::active || l->view.phase==Phase::draining) && l->actionCount) {
            if(m.request!=l->request || !SameSpec(m.spec,l->view.spec))return true;
            if(!m.ok){if(m.sequence && m.sequence<=l->nextAction)End(*l,Result::ApplyFailed,now);return true;}
            AcceptAppliedAck(*l,peer,m.sequence,now);
        } else if(m.kind==Kind::Exit) {
            if(!std::strcmp(l->view.spec.ownerPuid,authenticatedPuid))End(*l,Result::Complete,now);else last_=Result::Unauthorized;
        } else if(m.kind==Kind::Prepared && l->view.phase==Phase::preparing) {
            if(!m.ok)End(*l,m.result==Result::Complete ? Result::Complete : Result::PrepareFailed,now);else {l->prepared[peer]=true;Advance(*l,now);}
        } else if(m.kind==Kind::Started && l->view.phase==Phase::beginning) {
            if(!m.ok)End(*l,m.result==Result::Complete ? Result::Complete : Result::BeginFailed,now);else {l->begun[peer]=true;Advance(*l,now);}
        } else if(m.kind==Kind::Activated && l->view.phase==Phase::activating) {
            if(!m.ok)End(*l,m.result==Result::Complete ? Result::Complete : Result::BeginFailed,now);
            else {l->activated[peer]=true;Advance(*l,now);}
        } else if(m.kind==Kind::DrainAck && l->view.phase==Phase::draining) {
            if(m.request==l->request && SameSpec(m.spec,l->view.spec)){l->quiesced[peer]=m.ok;TickDrain(*l,now);}
        } else if(m.kind==Kind::Fault) {
            if(m.request!=l->request || !SameSpec(m.spec,l->view.spec))return true;
            const auto reason=m.result==Result::Complete || m.result==Result::Active || m.result==Result::Idle ? Result::ApplyFailed : m.result;
            End(*l,reason,now);
        } else if(m.kind==Kind::Restored && l->view.phase==Phase::restoring) {
            if(paced_ && (!l->endToken || m.sequence!=l->endToken))return true;
            // End follows all older host packets on the one reliable ordered SDK route. Even a failed native
            // restore status with this token proves the peer polled that barrier and every retired Action before it.
            ReleaseRetiredCredit(*l,peer);l->restored[peer]=m.ok;
            if(!m.ok){l->view.result=Result::RestoreFailed;last_=Result::RestoreFailed;}UpdateRequest(*l);Advance(*l,now);
        }
        return true;
    }
    if(m.kind==Kind::CancelAck) {
        for(auto& r:requests_)if(r.cancelPending && r.request==m.request && r.source==m.spec.requesterPlayer && r.target==m.spec.target && r.profileHash==m.spec.profileHash) {
            if(!r.lease || !Find(r.lease) || Find(r.lease)->view.phase==Phase::closed) {
                r.result=m.result;r.cancelPending=false;last_=m.result;
            }
            break;
        }
        return true;
    }
    if(m.kind==Kind::Reject){RejectRequest(m.request,m.result);if(m.request==nextRequest_)last_=m.result;return true;}
    auto* l=Find(m.lease);
    if(m.kind==Kind::Prepare) {
        Message ack;ack.kind=Kind::Prepared;ack.lease=m.lease;
        if(l) {
            ack.ok=SameSpec(l->view.spec,m.spec) && Current(l->view.phase) && l->prepared[0];
            if(!SameSpec(l->view.spec,m.spec) && Current(l->view.phase))Fault(*l,Result::ProtocolError,now);
            Send(context_.hostPeer,ack);return true;
        }
        if(!Ready() || m.lease<=seenLease_){Send(context_.hostPeer,ack);return true;}
        if(!std::strcmp(m.spec.ownerPuid,local_)) {
            const auto* localRequest=RequestFor(m.spec.requesterPlayer);
            if(!localRequest || localRequest->request!=m.request || localRequest->target!=m.spec.target ||
               localRequest->result!=Result::Pending || localRequest->cancelPending || localRequest->lease ||
               (localRequest->profileHash && localRequest->profileHash!=m.spec.profileHash)) {
                if(localRequest && localRequest->request==m.request && (localRequest->cancelPending || localRequest->result==Result::Complete))ack.result=Result::Complete;
                Send(context_.hostPeer,ack);return true;
            }
        }
        seenLease_=m.lease;
        for(const auto& prior:leases_)if(Current(prior.view.phase) && (prior.view.spec.target==m.spec.target ||
            prior.view.spec.requesterPlayer==m.spec.requesterPlayer)) {
            Send(context_.hostPeer,ack);return true;
        }
        l=Slot();if(!l){Send(context_.hostPeer,ack);return true;}
        *l=Lease{};l->view.id=m.lease;l->view.spec=m.spec;l->request=m.request;l->view.phase=Phase::preparing;
        l->view.localRestored=false;l->view.result=Result::Pending;l->since=now;UpdateRequest(*l);
        ack.ok=hooks_.Authenticate(hooks_.user,m.spec.ownerPuid,m.spec.requesterPlayer) && hooks_.Prepare(hooks_.user,m.lease,m.spec);
        l->prepared[0]=ack.ok;Send(context_.hostPeer,ack);return true;
    }
    if(m.kind==Kind::End && !l) {
        // A send/host-Prepare failure may cancel before this peer received its lease. It created nothing.
        Message ack;ack.kind=Kind::Restored;ack.lease=m.lease;ack.sequence=m.sequence;ack.ok=true;Send(context_.hostPeer,ack);return true;
    }
    if(!l)return true;
    if(m.kind==Kind::Begin && l->view.phase==Phase::preparing && l->prepared[0]) {
        Message ack;ack.kind=Kind::Started;ack.lease=m.lease;
        const auto* request=RequestFor(l->view.spec.requesterPlayer);
        if(!std::strcmp(l->view.spec.ownerPuid,local_) && request && request->request==l->request && request->cancelPending) {
            ack.result=Result::Complete;Send(context_.hostPeer,ack);return true;
        }
        l->view.phase=Phase::beginning;l->view.started=true;l->since=now;
        ack.ok=hooks_.Begin(hooks_.user,m.lease,l->view.spec);
        l->begun[0]=ack.ok;Send(context_.hostPeer,ack);
        if(!ack.ok)Fault(*l,Result::BeginFailed,now);
    } else if(m.kind==Kind::Activate && (l->view.phase==Phase::beginning || l->view.phase==Phase::activating) && l->begun[0]) {
        Message ack;ack.kind=Kind::Activated;ack.lease=m.lease;
        const auto* request=RequestFor(l->view.spec.requesterPlayer);
        if(!std::strcmp(l->view.spec.ownerPuid,local_) && request && request->request==l->request && request->cancelPending) {
            ack.result=Result::Complete;Send(context_.hostPeer,ack);return true;
        }
        if(!l->activated[0])l->activated[0]=hooks_.Activate(hooks_.user,l->view.id,l->view.spec);
        ack.ok=l->activated[0];l->view.phase=Phase::activating;l->since=now;Send(context_.hostPeer,ack);
        if(!ack.ok)Fault(*l,Result::BeginFailed,now);
    } else if(m.kind==Kind::Active && l->view.phase==Phase::activating && l->activated[0]) {
        const auto* request=RequestFor(l->view.spec.requesterPlayer);
        if(!std::strcmp(l->view.spec.ownerPuid,local_) && request && request->request==l->request && request->cancelPending)return true;
        l->view.phase=Phase::active;l->view.result=Result::Active;last_=Result::Active;UpdateRequest(*l);
    } else if(m.kind==Kind::Input && l->view.phase==Phase::active) {
        if(m.sequence<=l->inputSequence)return true;
        const InputMeta meta{m.sequence,m.seed};
        if(m.sequence!=l->inputSequence+1 || m.seed!=InputSeed(context_.epoch,m.lease,m.sequence) ||
           m.spec.profileHash!=l->view.spec.profileHash ||
           !hooks_.Authenticate(hooks_.user,l->view.spec.ownerPuid,l->view.spec.requesterPlayer) ||
           !hooks_.ValidateInput(hooks_.user,m.lease,l->view.spec,meta,m.input.data(),m.inputSize)) {Fault(*l,Result::InvalidInput,now);return true;}
        l->inputSequence=m.sequence;
        if(!hooks_.ApplyInput(hooks_.user,m.lease,l->view.spec,meta,m.input.data(),m.inputSize))Fault(*l,Result::ApplyFailed,now);
    } else if(m.kind==Kind::Drain && Current(l->view.phase)) {
        if(m.request!=l->request || !SameSpec(m.spec,l->view.spec))return true;
        if(l->view.phase!=Phase::draining){l->view.phase=Phase::draining;l->since=now;l->quiesced[0]=false;}
        l->ending=m.result;l->view.result=m.result==Result::Complete ? Result::Pending : m.result;
        if(!l->quiesced[0])l->quiesced[0]=!hooks_.QuiesceActions || hooks_.QuiesceActions(hooks_.user,l->view.id,l->view.spec);
        UpdateRequest(*l);DrainReply(*l,now);
    } else if(m.kind==Kind::Abort && Current(l->view.phase)) {
        if(m.request!=l->request || !SameSpec(m.spec,l->view.spec) || m.result==Result::Complete)return true;
        ReleaseActions(*l);l->abortRequired=true;l->ending=m.result;l->view.result=m.result;l->view.phase=Phase::restoring;l->since=now;Restore(*l,now,true);
    } else if(m.kind==Kind::Action && (l->view.phase==Phase::active || l->view.phase==Phase::draining)) {
        if(m.request!=l->request || !SameSpec(m.spec,l->view.spec) || !hooks_.ApplyAction ||
           (l->view.phase!=Phase::draining && !hooks_.Authenticate(hooks_.user,l->view.spec.ownerPuid,l->view.spec.requesterPlayer))){Fault(*l,Result::InvalidInput,now);return true;}
        const auto parts=m.capability>>16,index=m.capability&0xFFFFu;
        if(m.sequence<=l->actionApplied){if(index+1==parts)ActionReply(*l,m.sequence,true,now);return true;}
        if(m.sequence<=l->nextAction) {
            for(std::size_t i=0;i<l->actionCount;++i){const auto& item=actionPool_[l->actions[(l->actionHead+i)%kActionWindow]];if(item.sequence==m.sequence){const auto off=static_cast<std::size_t>(index)*kChunkBytes;if(off+m.inputSize>item.size || std::memcmp(item.bytes.data()+off,m.input.data(),m.inputSize))Fault(*l,Result::InvalidInput,now);break;}}
            TryAction(*l,now);return true;
        }
        if(m.sequence!=l->nextAction+1){Fault(*l,Result::InvalidInput,now);return true;}
        if(!l->actionNext){l->receivingAction={};l->receivingAction.sequence=m.sequence;l->actionParts=parts;}
        auto& action=l->receivingAction;const auto offset=static_cast<std::size_t>(index)*kChunkBytes;
        if(parts!=l->actionParts || action.sequence!=m.sequence || index>l->actionNext || offset+m.inputSize>kMaxPoseBytes) {
            Fault(*l,Result::InvalidInput,now);return true;
        }
        if(index<l->actionNext) {
            if(offset+m.inputSize>action.size || std::memcmp(action.bytes.data()+offset,m.input.data(),m.inputSize))Fault(*l,Result::InvalidInput,now);
            return true;
        }
        std::memcpy(action.bytes.data()+offset,m.input.data(),m.inputSize);action.size=offset+m.inputSize;++l->actionNext;
        if(l->actionNext==parts) {
            if(l->actionCount>=kActionWindow){Fault(*l,Result::ApplyFailed,now);return true;}
            std::size_t free=0;while(free<kMaxActions && actionPool_[free].used)++free;if(free==kMaxActions){Fault(*l,Result::ApplyFailed,now);return true;}
            auto& queued=actionPool_[free];queued=action;queued.used=true;queued.since=now;
            l->actions[(l->actionHead+l->actionCount)%kActionWindow]=static_cast<ActionIndex>(free);++l->actionCount;l->nextAction=m.sequence;
            l->actionNext=l->actionParts=0;l->actionPending=true;if(l->actionCount==1)l->actionSince=now;
            TryAction(*l,now);
        }
    } else if(m.kind==Kind::Pose && l->view.phase==Phase::active) {
        if(m.sequence<=l->poseSequence)return true;
        const auto parts=m.capability>>16,index=m.capability&0xFFFFu;
        if(m.sequence!=l->poseSequence+1 || !SameSpec(m.spec,l->view.spec) || m.request!=l->request ||
           !hooks_.ApplyPose || !hooks_.Authenticate(hooks_.user,l->view.spec.ownerPuid,l->view.spec.requesterPlayer)) {
            Fault(*l,Result::InvalidInput,now);return true;
        }
        if(!l->poseNext){l->poseParts=parts;l->poseSize=0;}
        if(parts!=l->poseParts || index!=l->poseNext || l->poseSize+m.inputSize>kMaxPoseBytes) {
            Fault(*l,Result::InvalidInput,now);return true;
        }
        std::memcpy(l->pose.data()+l->poseSize,m.input.data(),m.inputSize);l->poseSize+=m.inputSize;++l->poseNext;
        if(l->poseNext==parts) {
            l->poseSequence=m.sequence;l->poseNext=l->poseParts=0;
            if(!hooks_.ApplyPose(hooks_.user,m.lease,l->view.spec,m.sequence,l->pose.data(),l->poseSize))Fault(*l,Result::ApplyFailed,now);
            l->poseSize=0;
        }
    } else if(m.kind==Kind::End) {
        if(paced_ && !m.sequence)return true;l->endToken=m.sequence;
        if(l->view.phase!=Phase::closed) {
            ReleaseActions(*l);if(l->view.phase!=Phase::restoring){l->view.phase=Phase::restoring;l->since=now;}
            l->ending=m.result;l->view.result=m.result==Result::Complete ? Result::Pending : m.result;l->revokeRequested=false;last_=l->view.result;Restore(*l,now,true);
        } else {Message ack;ack.kind=Kind::Restored;ack.lease=m.lease;ack.sequence=m.sequence;ack.ok=l->view.localRestored;Send(context_.hostPeer,ack);}
    } else if(m.kind==Kind::Ended && l->view.phase==Phase::restoring && l->view.localRestored) {
        l->view.phase=Phase::closed;l->view.result=m.result;last_=m.result;UpdateRequest(*l);
    }
    return true;
}
} // namespace enemy_control_protocol
