#include "support_protocol.h"
#include <cmath>
#include <cstring>

namespace crew::support_net {
namespace {
bool Point(const float* p) noexcept {
    for(int i=0;i<3;++i)if(!std::isfinite(p[i]) || std::fabs(p[i])>1.0e6f)return false;
    return true;
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
    if(m.kind<Kind::hello || m.kind>Kind::activated || m.transaction>kMaxTransactions || m.catalog>=1024 ||
       m.count>kMaxUnits || m.index>=kMaxUnits || m.ok>1 || !Point(m.target))return false;
    if(m.kind==Kind::hello)return m.challenge!=0 && m.request!=0;
    if(!m.epoch)return false;
    if(m.kind==Kind::welcome)return m.challenge!=0 && m.request!=0;
    if(m.kind==Kind::request)return m.request!=0;
    if(!m.transaction)return false;
    if(m.kind==Kind::begin)return m.count!=0;
    return m.kind!=Kind::unit || ValidUnit(m.unit,true);
}
}
bool ValidPlan(const Plan& p,bool ids) noexcept {
    if(!p.count || p.count>kMaxUnits || p.catalogId>=1024 || !Point(p.target))return false;
    for(std::uint32_t i=0;i<p.count;++i) {
        if(!ValidUnit(p.units[i],ids) || p.units[i].role>p.count || p.units[i].role==i+1)return false;
        if(p.units[i].resourceId==kExistingVehicle && p.catalogId!=kMissionCrewCatalog)return false;
        if(p.units[i].role && p.units[p.units[i].role-1].role)return false;
        for(std::uint32_t j=0;ids && j<i;++j)
            if(!std::memcmp(p.units[i].netId,p.units[j].netId,32))return false;
    }
    return true;
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
    nextTransaction_=0;requests_.fill(0);lastRequestAt_.fill(0);nextRequest_=0;
}
void Session::Stop() noexcept { ClearTransactions();running_=false;suspended_=false;epoch_=0;challenge_=0;challenges_.fill(0);peerMissions_.fill(0); }
bool Session::HasSpawned() const noexcept {
    for(const auto& t:transactions_)if(t.spawned && t.phase!=Phase::cancelled)return true;
    return false;
}
void Session::Suspend() noexcept {
    // A transport generation is not an actor lifetime. Preserve constructed
    // copies, including those awaiting finalize: another peer may already have
    // activated them. Only the explicit mission reset may clear that ledger.
    running_=false;suspended_=true;
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
    return backend_.hooks.plan && backend_.hooks.validate && backend_.hooks.spawn && backend_.hooks.destroy && backend_.hooks.deriveId;
}
bool Session::Send(std::uint32_t peer,Message m) noexcept {
    m.epoch=epoch_;return backend_.send && backend_.send(backend_.context,peer,m);
}
bool Session::Broadcast(Message m) noexcept {
    bool ok=true;for(std::uint32_t i=1;i<=peers_;++i)if(!Send(i,m))ok=false;return ok;
}
void Session::Welcome(std::uint32_t peer) noexcept {
    Message m;m.kind=Kind::welcome;m.challenge=challenges_[peer];m.request=epochSerial_;Send(peer,m);
}
void Session::Cancel(std::uint32_t id,bool broadcast) noexcept {
    if(!id || id>kMaxTransactions)return;
    auto& t=transactions_[id-1];
    if(t.phase!=Phase::cancelled) {
        if(t.token && backend_.hooks.destroy)backend_.hooks.destroy(t.token);
        t.result.fill(0);t.result[0]=1;t.since=0;t.cancelConfirmed=false;
    }
    t.phase=Phase::cancelled;
    if(broadcast){Message m;m.kind=Kind::cancel;m.transaction=id;Broadcast(m);}
}
void Session::Failed(std::uint64_t token) noexcept {
    if(!running_ || !token)return;
    std::uint32_t id=0;
    for(std::uint32_t i=0;i<kMaxTransactions;++i)if(transactions_[i].token==token){id=i+1;break;}
    if(!id)return;
    if(host_)Cancel(id,true);
    else {Cancel(id,false);Message m;m.kind=Kind::result;m.transaction=id;Send(hostPeer_,m);}
}
bool Session::Submit(std::uint32_t catalog,const float* target,std::uint64_t now) noexcept {
    if(!target || !Ready() || catalog>=kMissionCrewCatalog || !Point(target) || nextRequest_==UINT32_MAX)return false;
    Message m;m.kind=Kind::request;m.epoch=epoch_;m.request=++nextRequest_;m.catalog=catalog;
    std::memcpy(m.target,target,sizeof(m.target));
    if(host_){const auto before=nextTransaction_;HostRequest(0,m,now);return nextTransaction_!=before;}
    return Send(hostPeer_,m);
}
void Session::HostRequest(std::uint32_t peer,const Message& m,std::uint64_t now) noexcept {
    if(!Ready() || !m.request || m.request<=requests_[peer] || m.catalog>=kMissionCrewCatalog)return;
    requests_[peer]=m.request; // consume even rejected requests; they cannot be replayed later
    if(lastRequestAt_[peer] && now-lastRequestAt_[peer]<2000)return;
    lastRequestAt_[peer]=now;
    if(nextTransaction_>=kMaxTransactions || nextToken_==UINT64_MAX)return;
    for(const auto& t:transactions_)if(t.phase==Phase::planning || t.phase==Phase::prepared || t.phase==Phase::spawning)return;
    auto& t=transactions_[nextTransaction_++];t.token=++nextToken_;t.phase=Phase::planning;t.requester=peer;t.request=m.request;t.since=now;
    t.plan.catalogId=m.catalog;std::memcpy(t.plan.target,m.target,sizeof(m.target));
}
std::uint64_t Session::SubmitPrepared(const Plan& plan,std::uint64_t now) noexcept {
    if(!host_ || !Ready() || plan.catalogId!=kMissionCrewCatalog || !ValidPlan(plan,false) ||
       nextTransaction_>=kMaxTransactions || nextToken_==UINT64_MAX)return 0;
    unsigned existing=0;
    for(std::uint32_t i=0;i<plan.count;++i)if(plan.units[i].resourceId==kExistingVehicle) {
        ++existing;if(!ValidUnit(plan.units[i],true))return 0;
        for(const auto& prior:transactions_)if(prior.phase!=Phase::empty && prior.phase!=Phase::cancelled)
            for(std::uint32_t j=0;j<prior.plan.count;++j)if(prior.plan.units[j].resourceId==kExistingVehicle &&
                !std::memcmp(prior.plan.units[j].netId,plan.units[i].netId,32))return 0;
    }
    if(existing!=1)return 0;
    for(const auto& t:transactions_)if(t.phase==Phase::planning || t.phase==Phase::prepared || t.phase==Phase::spawning)return 0;
    auto& t=transactions_[nextTransaction_++];t.token=++nextToken_;t.phase=Phase::planning;t.external=true;t.plan=plan;t.since=now;
    return t.token;
}
void Session::Advance(std::uint32_t id,std::uint64_t now) noexcept {
    auto& t=transactions_[id-1];
    if(t.phase==Phase::planning) {
        const auto catalog=t.plan.catalogId;float target[3];std::memcpy(target,t.plan.target,sizeof(target));
        const auto result=t.external ? PlanResult::ready : backend_.hooks.plan(catalog,target,&t.plan);
        if(result==PlanResult::pending)return;
        if(result==PlanResult::refused || t.plan.catalogId!=catalog || !ValidPlan(t.plan,false)){Cancel(id,true);return;}
        for(std::uint32_t i=0;i<t.plan.count;++i) {
            if(t.plan.units[i].resourceId==kExistingVehicle)continue;
            if(ordinal_>=0x80000000u || !backend_.hooks.deriveId(ordinal_++,t.plan.units[i].netId)){Cancel(id,true);return;}
        }
        if(!ValidPlan(t.plan) || !backend_.hooks.validate(t.plan)){Cancel(id,true);return;}
        t.phase=Phase::prepared;t.since=now;t.ready[0]=1;
        Message m;m.kind=Kind::begin;m.transaction=id;m.catalog=t.plan.catalogId;m.count=t.plan.count;
        std::memcpy(m.target,t.plan.target,sizeof(m.target));
        if(!Broadcast(m)){Cancel(id,true);return;}
        for(std::uint32_t i=0;i<t.plan.count;++i) {
            m=Message{};m.kind=Kind::unit;m.transaction=id;m.index=i;m.unit=t.plan.units[i];
            if(!Broadcast(m)){Cancel(id,true);return;}
        }
        m=Message{};m.kind=Kind::prepare;m.transaction=id;
        if(!Broadcast(m)){Cancel(id,true);return;}
    }
    if(t.phase==Phase::prepared) {
        for(std::uint32_t i=0;i<=peers_;++i)if(!t.ready[i])return;
        t.phase=Phase::spawning;t.since=now;
        Message m;m.kind=Kind::commit;m.transaction=id;
        if(!Broadcast(m) || !backend_.hooks.spawn(t.token,t.plan,false)){Cancel(id,true);return;}
        t.spawned=true;t.result[0]=1;
    }
    if(t.phase==Phase::spawning) {
        for(std::uint32_t i=0;i<=peers_;++i)if(!t.result[i])return;
        t.phase=Phase::active;t.activated[0]=1;t.since=now;
        Message m;m.kind=Kind::activate;m.transaction=id;Broadcast(m);
    }
}
void Session::Receive(std::uint32_t peer,const Message& m,std::uint64_t now) noexcept {
    if(!running_ || !peer || peer>peers_ || !ValidMessage(m))return;
    if(host_ && m.kind==Kind::hello) {
        if(m.request<peerMissions_[peer] || (m.request==peerMissions_[peer] && challenges_[peer]!=m.challenge))return;
        peerMissions_[peer]=m.request;
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
        if(epoch_!=m.epoch){if(HasSpawned()){Suspend();return;}ClearTransactions();epoch_=m.epoch;}
        seenEpochSerial_=m.request;
        return;
    }
    if(!epoch_ || m.epoch!=epoch_)return;
    if(host_) {
        if(!challenges_[peer])return;
        if(m.kind==Kind::request){HostRequest(peer,m,now);return;}
        if(!m.transaction)return;
        auto& t=transactions_[m.transaction-1];
        if(m.kind==Kind::activated && t.phase==Phase::active){t.activated[peer]=1;return;}
        if(m.kind==Kind::result && !m.ok && t.phase==Phase::cancelled) {t.result[peer]=1;return;}
        if(m.kind==Kind::ready && t.phase==Phase::prepared) {
            if(!m.ok){Cancel(m.transaction,true);return;}t.ready[peer]=1;
        } else if(m.kind==Kind::result && (t.phase==Phase::prepared || t.phase==Phase::spawning || t.phase==Phase::active)) {
            if(!m.ok){Cancel(m.transaction,true);return;}t.result[peer]=1;
        }
        return;
    }
    if(peer!=hostPeer_ || !m.transaction)return; // authenticated host, never payload sender
    auto& t=transactions_[m.transaction-1];
    if(m.kind==Kind::cancel) {
        Cancel(m.transaction,false);t.cancelConfirmed=true;
        Message reply;reply.kind=Kind::result;reply.transaction=m.transaction;Send(hostPeer_,reply);return;
    }
    if(m.kind==Kind::begin && t.phase==Phase::empty) {
        if(nextToken_==UINT64_MAX){Suspend();return;}
        t.token=++nextToken_;
        t.phase=Phase::assembling;t.since=now;t.plan.catalogId=m.catalog;t.plan.count=m.count;
        std::memcpy(t.plan.target,m.target,sizeof(m.target));
    } else if(m.kind==Kind::unit && t.phase==Phase::assembling && m.index<t.plan.count) {
        const auto bit=1u<<m.index;
        if((t.received&bit) && std::memcmp(&t.plan.units[m.index],&m.unit,sizeof(Unit))) {Failed(t.token);return;}
        t.plan.units[m.index]=m.unit;t.received|=bit;
    } else if(m.kind==Kind::prepare && t.phase==Phase::assembling) {
        Message reply;reply.kind=Kind::ready;reply.transaction=m.transaction;
        reply.ok=t.received==((1u<<t.plan.count)-1) && ValidPlan(t.plan) && backend_.hooks.validate && backend_.hooks.validate(t.plan);
        t.phase=reply.ok ? Phase::prepared : Phase::cancelled;t.since=now;Send(hostPeer_,reply);
    } else if(m.kind==Kind::commit && (t.phase==Phase::prepared || t.phase==Phase::spawning || t.phase==Phase::active)) {
        Message reply;reply.kind=Kind::result;reply.transaction=m.transaction;
        const bool alreadySpawned=t.spawned;
        reply.ok=alreadySpawned || (backend_.hooks.spawn && backend_.hooks.spawn(t.token,t.plan,true));
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
    // Keep the challenge alive after establishment too: a welcome whose local
    // enqueue failed during another member's mission reset must be recoverable.
    if(!host_ && now-lastHello_>=1000) {
        Message m;m.kind=Kind::hello;m.challenge=challenge_;m.request=missionSerial_;Send(hostPeer_,m);lastHello_=now;
    }
    for(std::uint32_t id=1;id<=kMaxTransactions;++id) {
        auto& t=transactions_[id-1];
        if(t.phase==Phase::active && host_ && now-t.since>=1000) {
            Message m;m.kind=Kind::activate;m.transaction=id;
            for(std::uint32_t peer=1;peer<=peers_;++peer)if(!t.activated[peer])Send(peer,m);
            t.since=now;
        }
        if(t.phase==Phase::cancelled) {
            if(now-t.since>=1000) {
                bool confirmed=t.cancelConfirmed;
                if(host_) {
                    confirmed=true;for(std::uint32_t peer=1;peer<=peers_;++peer)confirmed=confirmed && t.result[peer]!=0;
                }
                if(!confirmed) {
                    Message m;m.kind=host_ ? Kind::cancel : Kind::result;m.transaction=id;
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
                Message m;m.kind=Kind::result;m.transaction=id;m.ok=1;Send(hostPeer_,m);t.since=now;continue;
            }
            Failed(t.token);continue;
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
