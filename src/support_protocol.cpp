#include "support_protocol.h"
#include "support_loadout.h"
#include "support_submarine.h"
#include <cmath>
#include <cstring>

namespace crew::support_net {
namespace {
bool Point(const float* p) noexcept {
    for(int i=0;i<3;++i)if(!std::isfinite(p[i]) || std::fabs(p[i])>1.0e6f)return false;
    return true;
}
bool ZeroId(const unsigned char* id) noexcept {
    for(unsigned i=0;i<32;++i)if(id[i])return false;
    return true;
}
bool CanonicalRequester(const unsigned char* id) noexcept {
    if(ZeroId(id))return false;
    for(unsigned i=20;i<24;++i)if(id[i])return false;
    return true;
}
bool RequesterMetadata(const unsigned char* id,std::uint32_t team) noexcept {
    return team<=2 && (ZeroId(id) ? team==0 : CanonicalRequester(id));
}
bool BeginUnit(const Unit& unit) noexcept {
    // BEGIN has no unit transform/variant. Require byte-zero floats, including rejecting negative zero.
    const float zero[16]{};
    if(unit.variant || std::memcmp(unit.matrix,zero,sizeof(zero)))return false;
    if(!unit.resourceId)return !unit.role && ZeroId(unit.netId);
    return unit.resourceId==kBeginRequesterMetadata && unit.role<=2 && CanonicalRequester(unit.netId);
}
bool HasRequester(const Plan& plan) noexcept { return !ZeroId(plan.requester); }
constexpr unsigned char kExplicitCancelAck=2;
std::uint32_t TransactionSlot(std::uint32_t id) noexcept { return (id-1)%kMaxTransactions; }
bool DynamicDroneResource(std::uint32_t id) noexcept {return id==kSupportSubDroneResource || IsCarrierDroneResource(id);}
std::uint32_t RequiredDetails(const Plan& plan,std::uint32_t wireId) noexcept {
    std::uint32_t bits=HasRequester(plan) ? kDetailPvpSupport : 0u;
    if(wireId>kMaxTransactions)bits|=kDetailTransactionGenerations;
    for(std::uint32_t i=0;i<plan.count && i<kMaxUnits;++i)
        if(DynamicDroneResource(plan.units[i].resourceId))bits|=kDetailDynamicDrones|kDetailTransactionGenerations;
    return bits;
}
bool ValidUnit(const Unit& u,bool ids) noexcept {
    if(!u.resourceId || u.role>kMaxUnits)return false;
    for(float x:u.matrix)if(!std::isfinite(x) || std::fabs(x)>1.0e6f)return false;
    if(u.matrix[3]!=0 || u.matrix[7]!=0 || u.matrix[11]!=0 || u.matrix[15]!=1)return false;
    for(int row=0;row<3;++row)for(int other=row;other<3;++other) {
        float dot=0;for(int c=0;c<3;++c)dot+=u.matrix[row*4+c]*u.matrix[other*4+c];
        if(std::fabs(dot-(row==other ? 1.0f : 0.0f))>0.05f)return false;
    }
    if(ids) {
        std::uint32_t type=0,ordinal=0;
        std::memcpy(&type,u.netId+12,4);std::memcpy(&ordinal,u.netId+4,4);
        if(u.resourceId!=kExistingVehicle && (type!=5 || ordinal<0x40000000u || ordinal>=0x80000000u))return false;
        if(u.resourceId==kExistingVehicle) {
            bool any=false;for(unsigned char b:u.netId)any=any || b!=0;
            if(!any || u.role)return false;
        }
        for(unsigned i=20;i<24;++i)if(u.netId[i])return false; // native NetId padding is not semantic identity
    }
    return true;
}
void Store32(unsigned char*& p,std::uint32_t v) noexcept {
    for(unsigned i=0;i<4;++i)*p++=static_cast<unsigned char>(v>>(i*8));
}
std::uint32_t Read32(const unsigned char*& p) noexcept {
    std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=static_cast<std::uint32_t>(*p++)<<(i*8);return v;
}
void Float(unsigned char*& p,float f) noexcept { std::uint32_t v;std::memcpy(&v,&f,4);Store32(p,v); }
float Float(const unsigned char*& p) noexcept { const auto v=Read32(p);float f;std::memcpy(&f,&v,4);return f; }
bool ValidMessage(const Message& m) noexcept {
    if(m.kind<Kind::hello || m.kind>Kind::requestStatus || m.catalog>=1024 ||
       m.count>kMaxUnits || m.index>=kMaxUnits || m.ok>1 || !Point(m.target))return false;
    if(m.kind==Kind::hello)return m.challenge!=0 && m.request!=0;
    if(!m.epoch)return false;
    if(m.kind==Kind::welcome)return m.challenge!=0 && m.request!=0;
    if(m.kind==Kind::request)return m.request!=0;
    if(m.kind==Kind::requestStatus)return m.request!=0 && m.index<=static_cast<std::uint32_t>(RequestStatus::interrupted);
    if(!m.transaction)return false;
    if(m.kind==Kind::begin)return m.count!=0 && BeginUnit(m.unit);
    if(m.kind==Kind::result && m.index>static_cast<std::uint32_t>(RequestStatus::interrupted))
        return m.index==kCancelAckIndex && !m.ok;
    return m.kind!=Kind::unit || ValidUnit(m.unit,true);
}
}
bool ValidPlan(const Plan& p,bool ids) noexcept {
    if(!p.count || p.count>kMaxUnits || p.catalogId>=1024 || !Point(p.target) || !RequesterMetadata(p.requester,p.team))return false;
    for(std::uint32_t i=0;i<p.count;++i) {
        if(!ValidUnit(p.units[i],ids) || p.units[i].role>p.count || p.units[i].role==i+1)return false;
        if(p.units[i].resourceId==kExistingVehicle && p.catalogId!=kMissionCrewCatalog)return false;
        if(p.units[i].role && p.units[p.units[i].role-1].role)return false;
        for(std::uint32_t j=0;ids && j<i;++j)
            if(!std::memcmp(p.units[i].netId,p.units[j].netId,32))return false;
    }
    return true;
}
bool ValidPreparedDronePlan(const Plan& plan) noexcept {
    return ValidPlan(plan,false) && plan.catalogId==kPreparedDroneCatalog && plan.count==1 &&
        CanonicalRequester(plan.requester) && !plan.units[0].role && plan.units[0].variant &&
        DynamicDroneResource(plan.units[0].resourceId) && ZeroId(plan.units[0].netId);
}
bool Encode(const Message& m,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kWireSize || !ValidMessage(m))return false;
    auto p=static_cast<unsigned char*>(bytes);
    Store32(p,kMagic);Store32(p,kVersion);Store32(p,static_cast<std::uint32_t>(m.kind));Store32(p,0);
    Store32(p,static_cast<std::uint32_t>(m.epoch));Store32(p,static_cast<std::uint32_t>(m.epoch>>32));
    Store32(p,static_cast<std::uint32_t>(m.challenge));Store32(p,static_cast<std::uint32_t>(m.challenge>>32));
    Store32(p,m.transaction);Store32(p,m.request);Store32(p,m.catalog);Store32(p,m.count);Store32(p,m.index);Store32(p,m.ok);
    for(float f:m.target)Float(p,f);
    Store32(p,m.unit.resourceId);Store32(p,m.unit.role);
    for(float f:m.unit.matrix)Float(p,f);
    std::memcpy(p,m.unit.netId,32);p+=32;Store32(p,0);
    return true;
}
bool Decode(const void* bytes,std::size_t size,Message& out) noexcept {
    if(!bytes || size!=kWireSize)return false;
    auto p=static_cast<const unsigned char*>(bytes);
    if(Read32(p)!=kMagic || Read32(p)!=kVersion)return false;
    Message m;m.kind=static_cast<Kind>(Read32(p));if(Read32(p))return false;
    m.epoch=Read32(p);m.epoch|=static_cast<std::uint64_t>(Read32(p))<<32;
    m.challenge=Read32(p);m.challenge|=static_cast<std::uint64_t>(Read32(p))<<32;
    m.transaction=Read32(p);m.request=Read32(p);m.catalog=Read32(p);m.count=Read32(p);m.index=Read32(p);m.ok=Read32(p);
    for(float& f:m.target)f=Float(p);
    m.unit.resourceId=Read32(p);m.unit.role=Read32(p);
    for(float& f:m.unit.matrix)f=Float(p);
    std::memcpy(m.unit.netId,p,32);p+=32;if(Read32(p))return false;
    if(!ValidMessage(m))return false;
    out=m;return true;
}
void Session::ClearTransactions() noexcept {
    for(std::uint32_t i=0;i<kMaxTransactions;++i) {
        if(transactions_[i].token && backend_.hooks.destroy)backend_.hooks.destroy(transactions_[i].token);
        transactions_[i]=Transaction{};
    }
    nextTransaction_=0;requests_.fill(0);lastRequestAt_.fill(0);nextRequest_=0;rescueAt_.fill(0);
    replies_.fill(Replies{});localReplies_.fill(Reply{});
}
void Session::Stop() noexcept {
    ClearTransactions();running_=false;suspended_=false;epoch_=0;challenge_=0;challenges_.fill(0);peerMissions_.fill(0);peerCaps_.fill(0);
    peerExt_.fill(0);peerDetails_.fill(0);for(auto& b:peerBloom_)b.fill(0);
    hostCaps_=0;hostDetails_=0;hostCapsKnown_=false;hostRoomBehind_=false;
}
std::uint32_t Session::PeersBehind(std::uint32_t* missing) const noexcept {
    std::uint32_t behind=0,lack=0;
    for(std::uint32_t i=1;host_ && i<=peers_;++i) {
        if(!challenges_[i])continue;   // not heard yet
        const std::uint32_t gap=kFeatures&~Features(peerCaps_[i],peerExt_[i]);
        if(gap){++behind;lack|=gap;}
    }
    if(missing)*missing=lack;
    return behind;
}
std::uint32_t Session::PeersAhead() const noexcept {
    std::uint32_t ahead=0;
    // A newer guest: a capability or an extension bit this build does not know (peerExt_ keeps the hello's whole word).
    for(std::uint32_t i=1;host_ && i<=peers_;++i)if(challenges_[i] && ((peerCaps_[i]&~kCapabilities) || (peerExt_[i]&~kExtensions)))++ahead;
    return ahead;
}
bool Session::RequesterOf(std::uint64_t token,std::uint32_t* peer) const noexcept {
    if(!token || !peer)return false;
    for(const auto& t:transactions_)if(t.token==token) {
        if(t.external || !t.request)return false;
        *peer=t.requester;return true;
    }
    return false;
}
bool Session::PeersHave(std::uint32_t caps) const noexcept {
    if(!host_)return true;
    for(std::uint32_t i=1;i<=peers_;++i)if((peerCaps_[i]&caps)!=caps)return false;
    return true;
}
bool Session::PeersHaveDetails(std::uint32_t bits) const noexcept {
    if(!running_ || !epoch_ || (bits&~kDetails))return false;
    if(!host_)return hostCapsKnown_ && (hostDetails_&bits)==bits;
    for(std::uint32_t i=1;i<=peers_;++i)
        if(!challenges_[i] || (peerDetails_[i]&bits)!=bits)return false;
    return true;
}
bool Session::PeersHaveVariant(std::uint32_t ext,std::uint64_t hash) const noexcept {
    if(!host_)return true;
    for(std::uint32_t i=1;i<=peers_;++i) {
        if((peerExt_[i]&ext)!=ext)return false;
        if(hash && !BloomHas(peerBloom_[i].data(),hash))return false;
    }
    return true;
}
bool Session::HasSpawned() const noexcept {
    for(const auto& t:transactions_)if(t.spawned && t.phase!=Phase::cancelled)return true;
    return false;
}
void Session::Suspend() noexcept {
    // A transport generation is not an actor lifetime. Preserve constructed
    // copies, including those awaiting finalize: another peer may already have
    // activated them. Only the explicit mission reset may clear that ledger.
    running_=false;suspended_=true;
    for(const auto reply:localReplies_)if(reply.request && reply.status==RequestStatus::accepted)Notice(reply.request,RequestStatus::interrupted);
    for(std::uint32_t i=0;i<kMaxTransactions;++i) {
        auto& t=transactions_[i];
        if(!t.spawned && t.phase!=Phase::empty && t.phase!=Phase::cancelled)Cancel(i+1,false);
    }
}
void Session::Start(bool host,std::uint32_t peers,std::uint32_t hostPeer,std::uint64_t now) noexcept {
    if(running_ || suspended_){Suspend();return;}
    Stop();if(peers>kMaxPeers || (!host && (!hostPeer || hostPeer>peers)) || !backend_.nonce ||
        epochSerial_==UINT32_MAX || missionSerial_==UINT32_MAX)return;
    host_=host;peers_=peers;hostPeer_=hostPeer;challenge_=backend_.nonce(backend_.context);
    epoch_=host ? backend_.nonce(backend_.context) : 0;
    if(host)++epochSerial_;seenEpochSerial_=0;
    ++missionSerial_;
    running_=challenge_ && (!host || epoch_);lastHello_=now>1000 ? now-1000 : 0;
}
bool Session::Ready() const noexcept {
    if(!running_ || !epoch_)return false;
    if(host_)for(std::uint32_t i=1;i<=peers_;++i)if(!challenges_[i])return false;
    return (backend_.hooks.plan || backend_.hooks.planForRequester) && backend_.hooks.validate && backend_.hooks.spawn && backend_.hooks.destroy && backend_.hooks.deriveId;
}
bool Session::Send(std::uint32_t peer,Message m) noexcept {
    m.epoch=epoch_;return backend_.send && backend_.send(backend_.context,peer,m);
}
bool Session::Broadcast(Message m) noexcept {
    bool ok=true;for(std::uint32_t i=1;i<=peers_;++i)if(!Send(i,m))ok=false;return ok;
}
void Session::Welcome(std::uint32_t peer) noexcept {
    Message m;m.kind=Kind::welcome;m.challenge=challenges_[peer];m.request=epochSerial_;
    // This build's features (support_protocol.h kFeatures): the low ten bits where an older guest reads them, the whole
    // extension word beside them (WelcomeFeatures).
    m.catalog=kFeatures&kWelcomeCatalogMask;m.ok=PeersBehind(nullptr) ? 1u : 0u;
    m.unit.resourceId=kExtensions;
    m.unit.role=kDetails;
    Send(peer,m);
}
void Session::Cancel(std::uint32_t id,bool broadcast,RequestStatus reason) noexcept {
    if(!id || id>kMaxTransactions)return;
    auto& t=transactions_[id-1];
    if(t.phase!=Phase::cancelled) {
        if(t.token && backend_.hooks.destroy)backend_.hooks.destroy(t.token);
        t.result.fill(0);t.result[0]=kExplicitCancelAck;t.since=0;t.cancelConfirmed=false;t.failure=reason;
    }
    t.phase=Phase::cancelled;
    if(host_ && t.request)Publish(t.requester,t.request,reason);
    if(broadcast){Message m;m.kind=Kind::cancel;m.transaction=t.id;Broadcast(m);}
}
void Session::Failed(std::uint64_t token,RequestStatus reason) noexcept {
    if(!running_ || !token)return;
    std::uint32_t id=0;
    for(std::uint32_t i=0;i<kMaxTransactions;++i)if(transactions_[i].token==token){id=i+1;break;}
    if(!id)return;
    if(host_)Cancel(id,true,reason);
    else {Cancel(id,false,reason);Message m;m.kind=Kind::result;m.transaction=transactions_[id-1].id;m.index=static_cast<std::uint32_t>(reason);Send(hostPeer_,m);}
}
// The kept status of `request` among `replies` (or the slot it may take: the oldest kept, if older than it), applying
// the status rules: a terminal one never reopens on a late reply, an active one never goes back to accepted. nullptr:
// nothing to record (the same status again, a late one, or older than every kept request).
Session::Reply* Session::Record(Replies& replies,std::uint32_t request,RequestStatus status) noexcept {
    Reply* oldest=nullptr;
    for(auto& reply:replies) {
        if(reply.request==request) {
            if(reply.status==status || reply.status>=RequestStatus::refused)return nullptr;
            if(reply.status==RequestStatus::active && status==RequestStatus::accepted)return nullptr;
            reply.status=status;return &reply;
        }
        if(!oldest || reply.request<oldest->request)oldest=&reply;
    }
    if(!oldest || oldest->request>request)return nullptr;
    *oldest={request,status,false};return oldest;
}
void Session::Notice(std::uint32_t request,RequestStatus status) noexcept {
    // Any of this machine's last kRecentRequests requests: the map's call and the rescue are told apart by their ids.
    if(!request || request>nextRequest_ || nextRequest_-request>=kRecentRequests)return;
    if(!Record(localReplies_,request,status))return;
    if(backend_.hooks.notice)backend_.hooks.notice(request,status);
}
void Session::ReplyTo(std::uint32_t peer) noexcept {
    for(auto& reply:replies_[peer]) {
        if(!reply.request || !reply.dirty)continue;
        Message m;m.kind=Kind::requestStatus;m.request=reply.request;m.index=static_cast<std::uint32_t>(reply.status);
        reply.dirty=!Send(peer,m);
    }
}
void Session::Publish(std::uint32_t peer,std::uint32_t request,RequestStatus status) noexcept {
    if(!request)return;
    if(!peer){Notice(request,status);return;}
    // Each of the peer's recent requests keeps its own outcome: a newer request (a rescue after a map call) does not
    // swallow its predecessor's, and the guest tells them apart by id.
    Reply* reply=Record(replies_[peer],request,status);
    if(!reply)return;
    reply->dirty=true;ReplyTo(peer);
}
bool Session::Submit(std::uint32_t catalog,const float* target,std::uint64_t now,std::uint64_t loadout) noexcept {
    if(!target || !Ready() || catalog>=kMissionCrewCatalog || !Point(target) || nextRequest_==UINT32_MAX)return false;
    Message m;m.kind=Kind::request;m.epoch=epoch_;m.request=++nextRequest_;m.catalog=catalog;m.challenge=loadout;
    Notice(m.request,RequestStatus::accepted);
    std::memcpy(m.target,target,sizeof(m.target));
    if(host_){const auto before=nextTransaction_;HostRequest(0,m,now);return nextTransaction_!=before;}
    const bool sent=Send(hostPeer_,m);if(!sent)Notice(m.request,RequestStatus::interrupted);return sent;
}
bool Session::InFlight(const Transaction& t) const noexcept {
    return t.phase==Phase::planning || t.phase==Phase::prepared || t.phase==Phase::spawning;
}
// Callback tokens, wire transaction identities, and storage positions have separate lifetimes.
// Never wrap a wire identity or release its slot before every cancellation ACK: a delayed control
// packet must not affect the replacement. Scan by increasing identity so a long-lived root cannot
// strand free slots behind it. Mixed old rooms keep their original bounded tombstones.
Session::Transaction* Session::AllocateTransaction() noexcept {
    if(nextToken_==UINT64_MAX)return nullptr;
    const bool recycle=PeersHaveDetails(kDetailTransactionGenerations);
    auto id=nextTransaction_;
    for(std::uint32_t attempt=0;attempt<kMaxTransactions && id!=UINT32_MAX;++attempt) {
        ++id;if(!recycle && id>kMaxTransactions)return nullptr;
        auto& t=transactions_[TransactionSlot(id)];
        if(t.phase!=Phase::empty) {
            if(!recycle || t.phase!=Phase::cancelled)continue;
            bool confirmed=true;
            for(std::uint32_t peer=1;peer<=peers_;++peer)confirmed=confirmed && t.result[peer]==kExplicitCancelAck;
            if(!confirmed)continue;
        }
        t=Transaction{};t.id=id;t.token=++nextToken_;nextTransaction_=id;return &t;
    }
    return nullptr;
}
// Two channels (2026-10-10): the map's (and the mission crew's), one transaction in flight in the whole room and each
// requester's requests 2 s apart; and, when every peer has kExtRescueChannel, the sea rescue's: one in flight per
// requester beside anything else, each requester's rescues rescueCooldown_ apart from its last one made. Both draw on
// the same bounded live slots and native ordinals; a failed one of either rolls back alone (Cancel of its own id).
// Explicit prepared drone children have their own parent/slot channel and never
// occupy either request channel while awaiting the same all-peer ACK barriers.
void Session::HostRequest(std::uint32_t peer,const Message& m,std::uint64_t now) noexcept {
    if(!m.request || m.catalog>=kMissionCrewCatalog)return;
    if(m.request<=requests_[peer]) {
        if(peer)for(auto& reply:replies_[peer])if(reply.request==m.request){reply.dirty=true;ReplyTo(peer);}
        return;
    }
    requests_[peer]=m.request; // consume even rejected requests; they cannot be replayed later
    const bool isRescue=backend_.hooks.ownChannel && backend_.hooks.ownChannel(m.catalog);
    const bool channel=isRescue && PeersHaveExtension(kExtRescueChannel);
    if(!Ready()) {Publish(peer,m.request,RequestStatus::refused);return;}
    if(isRescue && rescueAt_[peer] && now-rescueAt_[peer]<rescueCooldown_) {Publish(peer,m.request,RequestStatus::refused);return;}
    if(!channel) {
        if(lastRequestAt_[peer] && now-lastRequestAt_[peer]<2000) {Publish(peer,m.request,RequestStatus::refused);return;}
        lastRequestAt_[peer]=now;
    }
    for(const auto& t:transactions_) {
        if(!InFlight(t))continue;
        // The rescue channel: this requester's own rescue in flight blocks it, nothing else. The map's: anything of its own.
        if(channel ? t.rescue && t.requester==peer : !t.rescue && !t.dynamicDrone) {Publish(peer,m.request,RequestStatus::refused);return;}
    }
    auto* allocated=AllocateTransaction();if(!allocated){Publish(peer,m.request,RequestStatus::refused);return;}
    auto& t=*allocated;t.phase=Phase::planning;t.requester=peer;t.request=m.request;t.since=now;
    t.rescue=channel;
    t.plan.catalogId=m.catalog;std::memcpy(t.plan.target,m.target,sizeof(m.target));t.loadout=m.challenge;
    Publish(peer,m.request,RequestStatus::accepted);
}
std::uint64_t Session::SubmitPrepared(const Plan& plan,std::uint64_t now,PreparedKind kind) noexcept {
    if(!host_ || !Ready() || !ValidPlan(plan,false) ||
       nextTransaction_==UINT32_MAX || nextToken_==UINT64_MAX)return 0;
    if(kind!=PreparedKind::missionCrew && kind!=PreparedKind::dynamicDrone)return 0;
    if(kind==PreparedKind::dynamicDrone) {
        if(!ValidPreparedDronePlan(plan) || !PeersHaveDetails(kDetailPvpSupport|kDetailDynamicDrones|kDetailTransactionGenerations) ||
           !backend_.hooks.validate(plan))return 0;
        for(const auto& prior:transactions_) {
            if(!InFlight(prior) || !prior.dynamicDrone || prior.plan.units[0].variant!=plan.units[0].variant)continue;
            const auto a=prior.plan.units[0].resourceId,b=plan.units[0].resourceId;
            if(a==kSupportSubDroneResource || b==kSupportSubDroneResource || CarrierDroneSlot(a)==CarrierDroneSlot(b))return 0;
        }
    } else {
        if(plan.catalogId!=kMissionCrewCatalog)return 0;
        unsigned existing=0;
        for(std::uint32_t i=0;i<plan.count;++i)if(plan.units[i].resourceId==kExistingVehicle) {
            ++existing;if(!ValidUnit(plan.units[i],true))return 0;
            for(const auto& prior:transactions_)if(prior.phase!=Phase::empty && prior.phase!=Phase::cancelled)
                for(std::uint32_t j=0;j<prior.plan.count;++j)if(prior.plan.units[j].resourceId==kExistingVehicle &&
                    !std::memcmp(prior.plan.units[j].netId,plan.units[i].netId,32))return 0;
        }
        if(existing!=1)return 0;
        for(const auto& t:transactions_)if(InFlight(t) && !t.rescue && !t.dynamicDrone)return 0;
    }
    auto* allocated=AllocateTransaction();if(!allocated)return 0;
    auto& t=*allocated;t.phase=Phase::planning;t.external=true;t.plan=plan;t.since=now;
    t.dynamicDrone=kind==PreparedKind::dynamicDrone;
    return t.token;
}
void Session::Advance(std::uint32_t id,std::uint64_t now) noexcept {
    auto& t=transactions_[id-1];
    if(t.phase==Phase::planning) {
        const auto catalog=t.plan.catalogId;float target[3];std::memcpy(target,t.plan.target,sizeof(target));
        const auto result=t.external ? PlanResult::ready : backend_.hooks.planForRequester ?
            backend_.hooks.planForRequester(t.requester,catalog,target,t.loadout,&t.plan) :
            backend_.hooks.plan(catalog,target,t.loadout,&t.plan);
        if(result==PlanResult::pending)return;
        if(result==PlanResult::refused || t.plan.catalogId!=catalog || !ValidPlan(t.plan,false) ||
           (t.dynamicDrone && !ValidPreparedDronePlan(t.plan)) ||
           (RequiredDetails(t.plan,t.id) && !PeersHaveDetails(RequiredDetails(t.plan,t.id)))){Cancel(id,true,RequestStatus::refused);return;}
        for(std::uint32_t i=0;i<t.plan.count;++i) {
            if(t.plan.units[i].resourceId==kExistingVehicle)continue;
            if(ordinal_>=0x80000000u || !backend_.hooks.deriveId(ordinal_++,t.plan.units[i].netId)){Cancel(id,true);return;}
        }
        if(!ValidPlan(t.plan) || !backend_.hooks.validate(t.plan)){Cancel(id,true,RequestStatus::refused);return;}
        t.phase=Phase::prepared;t.since=now;t.ready[0]=1;
        Message m;m.kind=Kind::begin;m.transaction=t.id;m.catalog=t.plan.catalogId;m.count=t.plan.count;
        std::memcpy(m.target,t.plan.target,sizeof(m.target));
        if(HasRequester(t.plan)) {
            m.unit.resourceId=kBeginRequesterMetadata;m.unit.role=t.plan.team;
            std::memcpy(m.unit.netId,t.plan.requester,sizeof(m.unit.netId));
        }
        if(!Broadcast(m)){Cancel(id,true);return;}
        for(std::uint32_t i=0;i<t.plan.count;++i) {
            m=Message{};m.kind=Kind::unit;m.transaction=t.id;m.index=i;m.unit=t.plan.units[i];m.challenge=t.plan.units[i].variant;
            if(!Broadcast(m)){Cancel(id,true);return;}
        }
        m=Message{};m.kind=Kind::prepare;m.transaction=t.id;
        if(!Broadcast(m)){Cancel(id,true);return;}
    }
    if(t.phase==Phase::prepared) {
        if(RequiredDetails(t.plan,t.id) && !PeersHaveDetails(RequiredDetails(t.plan,t.id))){Cancel(id,true,RequestStatus::refused);return;}
        for(std::uint32_t i=0;i<=peers_;++i)if(!t.ready[i])return;
        t.phase=Phase::spawning;t.since=now;
        Message m;m.kind=Kind::commit;m.transaction=t.id;
        if(!Broadcast(m) || !backend_.hooks.spawn(t.token,t.plan,false)){Cancel(id,true);return;}
        t.spawned=true;t.result[0]=1;
    }
    if(t.phase==Phase::spawning) {
        if(RequiredDetails(t.plan,t.id) && !PeersHaveDetails(RequiredDetails(t.plan,t.id))){Cancel(id,true,RequestStatus::refused);return;}
        for(std::uint32_t i=0;i<=peers_;++i)if(!t.result[i])return;
        t.phase=Phase::active;t.activated[0]=1;t.since=now;
        // A rescue made on every machine: its requester's cooldown starts (whichever channel carried it); a cancelled one
        // starts none.
        if(!t.external && t.request && backend_.hooks.ownChannel && backend_.hooks.ownChannel(t.plan.catalogId))rescueAt_[t.requester]=now;
        Message m;m.kind=Kind::activate;m.transaction=t.id;Broadcast(m);
        if(t.request)Publish(t.requester,t.request,RequestStatus::active);
    }
}
void Session::Receive(std::uint32_t peer,const Message& m,std::uint64_t now) noexcept {
    if(!running_ || !peer || peer>peers_ || !ValidMessage(m))return;
    if(host_ && m.kind==Kind::hello) {
        if(m.request<peerMissions_[peer] || (m.request==peerMissions_[peer] && challenges_[peer]!=m.challenge))return;
        peerMissions_[peer]=m.request;peerCaps_[peer]=m.index;
        // The whole extension word (PeersAhead reads the bits this build does not know) and the variant files' filter.
        peerExt_[peer]=m.unit.resourceId;peerDetails_[peer]=m.unit.role;std::memcpy(peerBloom_[peer].data(),m.unit.netId,32);
        if(challenges_[peer] && challenges_[peer]!=m.challenge) {
            if(HasSpawned()){Suspend();return;}
            if(epochSerial_==UINT32_MAX){Stop();return;}
            ClearTransactions();epoch_=backend_.nonce(backend_.context);++epochSerial_;
            if(!epoch_){Stop();return;}
            challenges_[peer]=m.challenge;
            for(std::uint32_t i=1;i<=peers_;++i)if(challenges_[i])Welcome(i);
        } else {challenges_[peer]=m.challenge;Welcome(peer);}
        return;
    }
    if(!host_ && peer==hostPeer_ && m.kind==Kind::welcome && m.challenge==challenge_ && m.request>=seenEpochSerial_) {
        if(m.request==seenEpochSerial_ && epoch_!=m.epoch)return;
        hostCaps_=WelcomeFeatures(m.catalog,m.unit.resourceId);hostDetails_=m.unit.role;hostRoomBehind_=m.ok!=0;hostCapsKnown_=true;   // the host's kFeatures (0: an older host)
        if(epoch_!=m.epoch){if(HasSpawned()){Suspend();return;}ClearTransactions();epoch_=m.epoch;}
        seenEpochSerial_=m.request;
        return;
    }
    if(!epoch_ || m.epoch!=epoch_)return;
    if(!host_ && peer==hostPeer_ && m.kind==Kind::requestStatus) {
        Notice(m.request,static_cast<RequestStatus>(m.index));return;
    }
    if(host_) {
        if(!challenges_[peer])return;
        if(m.kind==Kind::request){HostRequest(peer,m,now);return;}
        if(!m.transaction)return;
        auto& t=transactions_[TransactionSlot(m.transaction)];
        if(t.id!=m.transaction)return;
        if(m.kind==Kind::activated && t.phase==Phase::active){t.activated[peer]=1;return;}
        if(m.kind==Kind::result && !m.ok && m.index==kCancelAckIndex) {
            if(t.phase==Phase::cancelled)t.result[peer]=kExplicitCancelAck;
            return;
        }
        if(m.kind==Kind::result && !m.ok && t.phase==Phase::cancelled) {
            // Old decoders have no explicit cancellation ACK. Their unmarked response can stop
            // legacy retries, but can never grant permission to recycle storage, even if details
            // are later refreshed. A generation-capable guest's local failure is not an ACK.
            if(!(peerDetails_[peer]&kDetailTransactionGenerations) && t.result[peer]!=kExplicitCancelAck)t.result[peer]=1;
            return;
        }
        if(m.kind==Kind::ready && t.phase==Phase::prepared) {
            if(!m.ok){Cancel(TransactionSlot(m.transaction)+1,true,RequestStatus::refused);return;}t.ready[peer]=1;
        } else if(m.kind==Kind::result && (t.phase==Phase::prepared || t.phase==Phase::spawning || t.phase==Phase::active)) {
            if(!m.ok){Cancel(TransactionSlot(m.transaction)+1,true,m.index==static_cast<std::uint32_t>(RequestStatus::timeout) ?
                RequestStatus::timeout : RequestStatus::cancelled);return;}t.result[peer]=1;
        }
        return;
    }
    if(peer!=hostPeer_ || !m.transaction)return; // authenticated host, never payload sender
    auto& t=transactions_[TransactionSlot(m.transaction)];
    if(m.transaction<t.id)return; // retired generation: no old BEGIN, ACK, or cancellation may reopen it
    if(m.transaction!=t.id) {
        // Only an authenticated host can establish a newer identity, and only after this machine has
        // processed the previous cancellation. A cancel before BEGIN also retires a failed host plan.
        if(m.kind!=Kind::begin && m.kind!=Kind::cancel)return;
        if(m.transaction>kMaxTransactions && !PeersHaveDetails(kDetailTransactionGenerations))return;
        if(t.phase!=Phase::empty && (t.phase!=Phase::cancelled || !t.cancelConfirmed))return;
        t=Transaction{};t.id=m.transaction;
    }
    if(m.kind==Kind::cancel) {
        Cancel(TransactionSlot(m.transaction)+1,false);t.cancelConfirmed=true;
        Message reply;reply.kind=Kind::result;reply.transaction=m.transaction;reply.index=kCancelAckIndex;Send(hostPeer_,reply);return;
    }
    if(m.kind==Kind::begin && t.phase==Phase::empty) {
        if(m.unit.resourceId && !PeersHaveDetails(kDetailPvpSupport)) {
            t.phase=Phase::cancelled;t.since=now;t.failure=RequestStatus::refused;
            Message reply;reply.kind=Kind::ready;reply.transaction=m.transaction;Send(hostPeer_,reply);return;
        }
        if(nextToken_==UINT64_MAX){Suspend();return;}
        t.token=++nextToken_;
        t.phase=Phase::assembling;t.since=now;t.plan.catalogId=m.catalog;t.plan.count=m.count;
        std::memcpy(t.plan.target,m.target,sizeof(m.target));
        if(m.unit.resourceId==kBeginRequesterMetadata) {
            std::memcpy(t.plan.requester,m.unit.netId,sizeof(t.plan.requester));t.plan.team=m.unit.role;
        }
    } else if(m.kind==Kind::unit && t.phase==Phase::assembling && m.index<t.plan.count) {
        const auto bit=1u<<m.index;
        Unit unit=m.unit;unit.variant=m.challenge;
        if((t.received&bit) && std::memcmp(&t.plan.units[m.index],&unit,sizeof(Unit))) {Failed(t.token);return;}
        t.plan.units[m.index]=unit;t.received|=bit;
    } else if(m.kind==Kind::prepare && t.phase==Phase::assembling) {
        // What it needs being loaded here (Hooks::prepare): READY once it is (Tick), within the transaction's deadline.
        if(t.received==((1u<<t.plan.count)-1) && ValidPlan(t.plan) && backend_.hooks.prepare) {
            const PlanResult loaded=backend_.hooks.prepare(t.plan);
            if(loaded==PlanResult::pending){if(!t.readyPending){t.readyPending=true;t.since=now;}return;}
            if(loaded==PlanResult::refused) {
                t.readyPending=false;t.phase=Phase::cancelled;t.since=now;
                Message reply;reply.kind=Kind::ready;reply.transaction=m.transaction;Send(hostPeer_,reply);return;
            }
        }
        t.readyPending=false;
        Message reply;reply.kind=Kind::ready;reply.transaction=m.transaction;
        reply.ok=t.received==((1u<<t.plan.count)-1) && ValidPlan(t.plan) &&
            (!RequiredDetails(t.plan,t.id) || PeersHaveDetails(RequiredDetails(t.plan,t.id))) && backend_.hooks.validate && backend_.hooks.validate(t.plan);
        t.phase=reply.ok ? Phase::prepared : Phase::cancelled;t.since=now;Send(hostPeer_,reply);
    } else if(m.kind==Kind::commit && (t.phase==Phase::prepared || t.phase==Phase::spawning || t.phase==Phase::active)) {
        Message reply;reply.kind=Kind::result;reply.transaction=m.transaction;
        const bool alreadySpawned=t.spawned;
        reply.ok=alreadySpawned || ((!RequiredDetails(t.plan,t.id) || PeersHaveDetails(RequiredDetails(t.plan,t.id))) &&
            backend_.hooks.spawn && backend_.hooks.spawn(t.token,t.plan,true));
        t.spawned=reply.ok!=0;
        if(!alreadySpawned)t.since=now;
        if(t.phase!=Phase::active)t.phase=reply.ok ? Phase::spawning : Phase::cancelled;
        if(!reply.ok && backend_.hooks.destroy)backend_.hooks.destroy(t.token);
        Send(hostPeer_,reply);
    } else if(m.kind==Kind::activate && t.spawned && (t.phase==Phase::spawning || t.phase==Phase::active)) {
        t.phase=Phase::active;
        Message reply;reply.kind=Kind::activated;reply.transaction=m.transaction;Send(hostPeer_,reply);
    }
}
void Session::Tick(std::uint64_t now) noexcept {
    if(!running_)return;
    if(host_)for(std::uint32_t peer=1;peer<=peers_;++peer)ReplyTo(peer);   // the dirty ones
    // Keep the challenge alive after establishment too: a welcome whose local
    // enqueue failed during another member's mission reset must be recoverable.
    if(!host_ && now-lastHello_>=1000) {
        Message m;m.kind=Kind::hello;m.challenge=challenge_;m.request=missionSerial_;m.index=kCapabilities;
        if(backend_.hooks.variants)backend_.hooks.variants(&m.unit.resourceId,m.unit.netId);
        m.unit.resourceId|=kExtBuilt;   // the catalog's and the protocol's, announced whatever the variants hook said
        m.unit.role=kDetails;
        Send(hostPeer_,m);lastHello_=now;
    }
    for(std::uint32_t id=1;id<=kMaxTransactions;++id) {
        auto& t=transactions_[id-1];
        if(!host_ && t.phase==Phase::assembling && t.readyPending) {   // PREPARE's READY, once loaded here
            const PlanResult loaded=backend_.hooks.prepare ? backend_.hooks.prepare(t.plan) : PlanResult::ready;
            if(loaded!=PlanResult::pending) {
                t.readyPending=false;
                Message reply;reply.kind=Kind::ready;reply.transaction=t.id;
                reply.ok=loaded==PlanResult::ready && ValidPlan(t.plan) &&
                    (!RequiredDetails(t.plan,t.id) || PeersHaveDetails(RequiredDetails(t.plan,t.id))) &&
                    backend_.hooks.validate && backend_.hooks.validate(t.plan);
                t.phase=reply.ok ? Phase::prepared : Phase::cancelled;t.since=now;Send(hostPeer_,reply);
                continue;
            }
        }
        if(t.phase==Phase::active && host_ && now-t.since>=1000) {
            Message m;m.kind=Kind::activate;m.transaction=t.id;
            for(std::uint32_t peer=1;peer<=peers_;++peer)if(!t.activated[peer])Send(peer,m);
            t.since=now;
        }
        if(t.phase==Phase::cancelled) {
            if(now-t.since>=1000) {
                bool confirmed=t.cancelConfirmed;
                if(host_) {
                    confirmed=true;for(std::uint32_t peer=1;peer<=peers_;++peer)
                        confirmed=confirmed && (t.result[peer]==kExplicitCancelAck ||
                            (!(peerDetails_[peer]&kDetailTransactionGenerations) && t.result[peer]!=0));
                }
                if(!confirmed) {
                    Message m;m.kind=host_ ? Kind::cancel : Kind::result;m.transaction=t.id;
                    m.index=static_cast<std::uint32_t>(t.failure);
                    if(host_)Broadcast(m);else Send(hostPeer_,m);
                }
                t.since=now;
            }
            continue;
        }
        const std::uint64_t timeout=t.phase==Phase::planning ? 120000 : 20000;
        if(t.phase!=Phase::empty && t.phase!=Phase::active && t.phase!=Phase::cancelled && now-t.since>timeout) {
            if(!host_ && t.spawned) {
                // Only the host may cancel an all-peer transaction. A lost
                // finalize enqueue is retried; never delete a registered copy
                // merely because another transport channel is late.
                Message m;m.kind=Kind::result;m.transaction=t.id;m.ok=1;Send(hostPeer_,m);t.since=now;continue;
            }
            Failed(t.token,RequestStatus::timeout);continue;
        }
        if(host_ && Ready())Advance(id,now);
    }
}
std::uint32_t Session::ActiveCount() const noexcept {
    std::uint32_t result=0;for(const auto& t:transactions_)if(t.phase==Phase::active)++result;return result;
}
bool Session::IsActive(std::uint64_t token) const noexcept {
    if(!token)return false;
    for(const auto& t:transactions_)if(t.token==token)return t.phase==Phase::active;
    return false;
}
} // namespace crew::support_net
