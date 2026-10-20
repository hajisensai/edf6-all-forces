#include "command_protocol.h"
#include <cmath>
#include <cstring>
namespace crew::command_net {
namespace {
bool Empty(const unsigned char* id) noexcept {for(unsigned i=0;i<32;++i)if(id[i])return false;return true;}
bool Puid(const char* text) noexcept {
    if(!text || !text[0])return false;
    for(unsigned i=1;i<65;++i)if(!text[i])return true;
    return false;
}
bool Same(const Request& a,const Request& b) noexcept {
    return a.count==b.count && a.command.order==b.command.order && !std::memcmp(a.command.at,b.command.at,12) &&
        a.command.param==b.command.param && a.flags==b.flags &&
        !std::memcmp(a.requester,b.requester,32) && !std::memcmp(a.focus,b.focus,32) && !std::memcmp(a.units,b.units,sizeof(a.units)) &&
        a.formationTotal==b.formationTotal && !std::memcmp(a.formationSlots,b.formationSlots,sizeof(a.formationSlots));
}
// The last order a version carries (6: HOLD; 7: the formation order).
mapcmd::Order LastOrder(std::uint32_t version) noexcept { return version>=7 ? mapcmd::kLastOrder : mapcmd::Order::hold; }
bool Valid(const Message& m) noexcept {
    if(!Compatible(m.version) || !m.epoch || m.kind<Kind::hello || m.kind>Kind::ways || m.rpc>=Rpc::count)return false;
    if(m.kind==Kind::ways) {   // version 7: one unit's queue, its points finite and bounded
        if(m.version<7 || m.rpc!=Rpc::none || !m.sequence || m.request.count!=1 || !CanonicalId(m.request.units[0]) ||
           m.request.flags || m.request.command.param || m.ways.n>kWaysMost)return false;
        for(unsigned i=1;i<kCommandNetUnits;++i)if(!Empty(m.request.units[i]))return false;
        for(unsigned i=0;i<kWaysMost;++i)for(float f:m.ways.at[i])
            if(i<m.ways.n ? (!std::isfinite(f) || std::fabs(f)>1.0e6f) : f!=0.0f)return false;
        return true;
    }
    // A hello / capability: its formationTotal the sender's highest version (0 from a version-6 build), nothing else.
    if(m.kind==Kind::hello || m.kind==Kind::capability)return m.sequence==0 && m.request.count==0 && !m.request.flags &&
        !m.request.command.param && m.request.formationTotal<=0xFFFFu &&
        (m.rpc==Rpc::none || (m.kind==Kind::capability && m.rpc==Rpc::unsupported));
    if(!m.sequence || !m.request.count || m.request.count>kCommandNetUnits)return false;
    if(m.kind==Kind::request)return m.rpc==Rpc::none && ValidRequest(m.request,m.version);
    if(m.request.flags || m.request.command.param)return false;   // a result echoes no order's flag or parameter
    for(std::uint32_t i=0;i<m.request.count;++i) {
        const auto& r=m.results[i];
        if(r.reason>=static_cast<std::uint32_t>(NpcCommandReason::count) || r.affected>256)return false;
        if(m.rpc==Rpc::none && ((r.reason==0)!=(r.affected>0)))return false;
    }
    return true;
}
void U32(unsigned char*& p,std::uint32_t value) noexcept {for(unsigned i=0;i<4;++i)*p++=static_cast<unsigned char>(value>>(i*8));}
std::uint32_t U32(const unsigned char*& p) noexcept {std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(*p++)<<(i*8);return v;}
}
bool CanonicalId(const unsigned char* id,bool allowEmpty) noexcept {
    if(!id)return false;for(unsigned i=20;i<24;++i)if(id[i])return false;return allowEmpty || !Empty(id);
}
std::uint32_t RequestVersion(const Request& r) noexcept {
    return r.flags || r.command.param || r.command.order>mapcmd::Order::hold ? 7u : kMinVersion;
}
bool ValidRequest(const Request& r,std::uint32_t version) noexcept {
    if(!Compatible(version) || !r.count || r.count>kCommandNetUnits || r.command.order>LastOrder(version) ||
       !CanonicalId(r.requester))return false;
    if(RequestVersion(r)>version)return false;   // a flag or a parameter a version-6 peer would not read
    if(r.flags&~kQueued)return false;
    if((r.flags&kQueued) && !mapcmd::PointOrder(r.command.order))return false;   // only a point goes after the others
    if(r.command.order==mapcmd::Order::formation ? !mapcmd::FormationParamValid(r.command.param) : r.command.param!=0)return false;
    for(float f:r.command.at)if(!std::isfinite(f) || std::fabs(f)>1.0e6f)return false;
    if(r.command.order==mapcmd::Order::focus) {if(!CanonicalId(r.focus) || !std::memcmp(r.focus,r.requester,32))return false;}
    else if(!Empty(r.focus))return false;
    for(std::uint32_t i=0;i<r.count;++i) {
        if(!CanonicalId(r.units[i]) || !std::memcmp(r.units[i],r.requester,32) ||
           (!Empty(r.focus) && !std::memcmp(r.units[i],r.focus,32)))return false;
        for(std::uint32_t j=0;j<i;++j)if(!std::memcmp(r.units[i],r.units[j],32))return false;
    }
    for(std::uint32_t i=r.count;i<kCommandNetUnits;++i)if(!Empty(r.units[i]))return false;
    if(r.formationTotal) {
        if(!mapcmd::PointOrder(r.command.order) || r.formationTotal<r.count || r.formationTotal>mapcmd::kMaxFormationUnits)return false;
        for(std::uint32_t i=0;i<r.count;++i) {
            if(r.formationSlots[i]>=r.formationTotal)return false;
            for(std::uint32_t j=0;j<i;++j)if(r.formationSlots[i]==r.formationSlots[j])return false;
        }
    }
    for(std::uint32_t i=r.formationTotal ? r.count : 0;i<kCommandNetUnits;++i)if(r.formationSlots[i])return false;
    return true;
}
bool Encode(const Message& m,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kWireSize || !Valid(m))return false;
    // Version 6's two reserved words: the flags (after `kind`) and the order's parameter (after the point), zero there.
    auto p=static_cast<unsigned char*>(bytes);U32(p,kMagic);U32(p,m.version);U32(p,static_cast<std::uint32_t>(m.kind));U32(p,m.request.flags);
    U32(p,static_cast<std::uint32_t>(m.epoch));U32(p,static_cast<std::uint32_t>(m.epoch>>32));
    U32(p,m.sequence);U32(p,m.request.count);U32(p,static_cast<std::uint32_t>(m.request.command.order));U32(p,static_cast<std::uint32_t>(m.rpc));
    for(float f:m.request.command.at){std::uint32_t bits;std::memcpy(&bits,&f,4);U32(p,bits);}U32(p,m.request.command.param);
    std::memcpy(p,m.request.requester,32);p+=32;std::memcpy(p,m.request.focus,32);p+=32;
    std::memcpy(p,m.request.units,sizeof(m.request.units));p+=sizeof(m.request.units);
    U32(p,m.kind==Kind::ways ? m.ways.n : m.request.formationTotal);for(auto slot:m.request.formationSlots)U32(p,slot);
    if(m.kind==Kind::ways) {   // the points where a result's words are (24 of its 32), the rest zero
        unsigned words=0;
        for(const auto& at:m.ways.at)for(float f:at){std::uint32_t bits;std::memcpy(&bits,&f,4);U32(p,bits);++words;}
        for(;words<2*kCommandNetUnits;++words)U32(p,0);
        return true;
    }
    for(const auto& r:m.results){U32(p,r.reason);U32(p,r.affected);}return true;
}
bool Decode(const void* bytes,std::size_t size,Message& out) noexcept {
    if(!bytes || size!=kWireSize)return false;
    auto p=static_cast<const unsigned char*>(bytes);if(U32(p)!=kMagic)return false;
    Message m;m.version=U32(p);if(!Compatible(m.version))return false;
    m.kind=static_cast<Kind>(U32(p));m.request.flags=U32(p);
    m.epoch=U32(p);m.epoch|=static_cast<std::uint64_t>(U32(p))<<32;m.sequence=U32(p);m.request.count=U32(p);
    const auto order=U32(p);if(order>static_cast<std::uint32_t>(LastOrder(m.version)))return false;
    m.request.command.order=static_cast<mapcmd::Order>(order);m.rpc=static_cast<Rpc>(U32(p));
    for(float& f:m.request.command.at){const auto bits=U32(p);std::memcpy(&f,&bits,4);}m.request.command.param=U32(p);
    std::memcpy(m.request.requester,p,32);p+=32;std::memcpy(m.request.focus,p,32);p+=32;
    std::memcpy(m.request.units,p,sizeof(m.request.units));p+=sizeof(m.request.units);
    m.request.formationTotal=U32(p);for(auto& slot:m.request.formationSlots)slot=U32(p);
    if(m.kind==Kind::ways) {
        m.ways.n=m.request.formationTotal;m.request.formationTotal=0;
        unsigned words=0;
        for(auto& at:m.ways.at)for(float& f:at){const auto bits=U32(p);std::memcpy(&f,&bits,4);++words;}
        for(;words<2*kCommandNetUnits;++words)if(U32(p))return false;
        if(!Valid(m))return false;out=m;return true;
    }
    for(auto& r:m.results){r.reason=U32(p);r.affected=U32(p);}if(!Valid(m))return false;out=m;return true;
}
bool Owned(const void* bytes,std::size_t size) noexcept {
    if(!bytes || size<4)return false;auto p=static_cast<const unsigned char*>(bytes);return U32(p)==kMagic;
}
void Session::Reset() noexcept {
    ready_=false;hostCapable_=false;epoch_=0;nextSequence_=0;lastHello_=0;hostVersion_=0;result_={};peer_.fill(Peer{});
    ways_.fill(WaysEntry{});waysRevision_=0;
}
Session::WaysEntry* Session::WaysSlot(const unsigned char* unit,bool make) noexcept {
    WaysEntry* free=nullptr;
    for(auto& e:ways_) {
        if(e.used && !std::memcmp(e.unit,unit,32))return &e;
        if(!e.used && !free)free=&e;
    }
    if(!make || !free)return nullptr;
    *free=WaysEntry{};free->used=true;std::memcpy(free->unit,unit,32);
    return free;
}
void Session::SendWays(std::uint32_t peer,const WaysEntry& e) noexcept {
    if(!peer || peer>peers_ || !peer_[peer].capable || peer_[peer].version<7)return;   // a version-6 peer has no such message
    Message m;m.version=peer_[peer].version;m.kind=Kind::ways;m.epoch=epoch_;m.sequence=e.revision;
    m.request.count=1;std::memcpy(m.request.units[0],e.unit,32);m.ways=e.ways;
    Send(peer,m);
}
void Session::PublishWays(const unsigned char* unit,const float (*at)[3],unsigned n) noexcept {
    if(!ready_ || !host_ || !unit || !CanonicalId(unit) || n>kWaysMost || waysRevision_==UINT32_MAX)return;
    Ways next{};next.n=n;
    for(unsigned i=0;i<n;++i)for(unsigned c=0;c<3;++c) {
        if(!std::isfinite(at[i][c]) || std::fabs(at[i][c])>1.0e6f)return;
        next.at[i][c]=at[i][c];
    }
    WaysEntry* e=WaysSlot(unit,n>0);
    if(!e)return;   // none out for it and none now: nothing to say
    if(e->ways.n==next.n && !std::memcmp(e->ways.at,next.at,sizeof(next.at)))return;
    e->ways=next;e->revision=++waysRevision_;
    for(std::uint32_t peer=1;peer<=peers_;++peer)SendWays(peer,*e);
    if(!n)e->used=false;   // its clearing sent: forgotten
}
unsigned Session::PublishedWays(unsigned char (*out)[32],unsigned most) const noexcept {
    unsigned n=0;
    for(const auto& e:ways_)if(e.used && n<most)std::memcpy(out[n++],e.unit,32);
    return n;
}
bool Session::WaysOf(const unsigned char* unit,Ways* out) const noexcept {
    if(!unit || !out || host_)return false;
    for(const auto& e:ways_)if(e.used && !std::memcmp(e.unit,unit,32) && e.ways.n){*out=e.ways;return true;}
    return false;
}
bool Session::Ready() const noexcept {return ready_ && epoch_ && (host_ ? backend_.execute!=nullptr : hostCapable_);}
bool Session::Send(std::uint32_t to,const Message& message) noexcept {return backend_.send && backend_.send(backend_.context,to,message);}
void Session::ResultOf(const Message& m) noexcept {
    result_={};result_.request=m.sequence;result_.count=m.request.count;
    switch(m.rpc) {
    case Rpc::none:result_.state=CommandNetworkState::completed;break;
    case Rpc::timeout:result_.state=CommandNetworkState::timedOut;break;
    case Rpc::interrupted:result_.state=CommandNetworkState::interrupted;break;
    case Rpc::invalid:result_.state=CommandNetworkState::invalid;break;
    case Rpc::stale:result_.state=CommandNetworkState::stale;break;
    case Rpc::rateLimited:result_.state=CommandNetworkState::rateLimited;break;
    default:result_.state=CommandNetworkState::unavailable;break;
    }
    for(std::uint32_t i=0;i<m.request.count;++i)result_.units[i]={static_cast<NpcCommandReason>(m.results[i].reason),m.results[i].affected};
}
void Session::Update(bool ready,bool host,std::uint64_t epoch,std::uint32_t hostPeer,std::uint32_t peers,std::uint64_t now) noexcept {
    if(!ready || !epoch || peers>kMaxPeers || (!host && (!hostPeer || hostPeer>peers))) {
        ready_=false;hostCapable_=false;
        if(result_.state==CommandNetworkState::pending){auto reply=pending_;reply.rpc=Rpc::interrupted;ResultOf(reply);}return;
    }
    if(epoch_!=epoch) {Reset();epoch_=epoch;lastHello_=now>1000 ? now-1000 : 0;}
    ready_=true;host_=host;hostPeer_=hostPeer;peers_=peers;
    if(!host_ && !hostCapable_ && now-lastHello_>=1000) {
        // At the lowest version, the highest this build speaks in it (a version-6 host reads it and answers in 6).
        Message hello;hello.version=kMinVersion;hello.epoch=epoch_;hello.request.formationTotal=maxVersion_;
        Send(hostPeer_,hello);lastHello_=now;
    }
    if(result_.state==CommandNetworkState::pending) {
        if(now-pendingAt_>=10000){auto reply=pending_;reply.rpc=Rpc::timeout;ResultOf(reply);}
        else if(!host_ && now-lastSend_>=1000){Send(hostPeer_,pending_);lastSend_=now;}
    }
}
void Session::Execute(std::uint32_t peer,const char* puid,const Message& request,std::uint64_t now) noexcept {
    auto& cache=peer_[peer];
    if(cache.have && request.sequence<=cache.request.sequence) {
        if(request.sequence==cache.request.sequence && Same(request.request,cache.request.request)) {
            if(peer)Send(peer,cache.reply);else ResultOf(cache.reply);
        } else {
            Message denied;denied.version=request.version;denied.kind=Kind::result;denied.epoch=request.epoch;denied.sequence=request.sequence;
            denied.request.count=request.request.count;denied.rpc=Rpc::stale;
            if(peer)Send(peer,denied);else ResultOf(denied);
        }
        return;
    }
    Message reply;reply.version=request.version;reply.kind=Kind::result;reply.epoch=epoch_;reply.sequence=request.sequence;
    reply.request.count=request.request.count;
    if(!backend_.execute || !cache.capable)reply.rpc=Rpc::unsupported;
    else if(cache.lastAt && now-cache.lastAt<200)reply.rpc=Rpc::rateLimited;
    else {backend_.execute(backend_.context,puid,request.request,reply.results);cache.lastAt=now;}
    cache.have=true;cache.request=request;cache.reply=reply;
    if(peer)Send(peer,reply);else ResultOf(reply);
}
void Session::Receive(std::uint32_t peer,const char* puid,const Message& message,std::uint64_t now) noexcept {
    if(!ready_ || !peer || peer>peers_ || !Puid(puid) || !Valid(message) || message.epoch!=epoch_)return;
    auto& identity=peer_[peer];
    if(identity.puid[0] && std::strcmp(identity.puid,puid))return;
    if(!identity.puid[0])std::memcpy(identity.puid,puid,std::strlen(puid)+1);
    if(host_) {
        if(message.kind==Kind::hello) {
            // The lower of the peer's highest (a version-6 build says none: its hello's own version) and this build's.
            const std::uint32_t said=message.request.formationTotal ? message.request.formationTotal : message.version;
            const std::uint32_t theirs=said<kMinVersion ? kMinVersion : said;
            peer_[peer].capable=true;peer_[peer].version=theirs<maxVersion_ ? theirs : maxVersion_;
            Message reply;reply.version=peer_[peer].version;reply.kind=Kind::capability;reply.epoch=epoch_;
            reply.request.formationTotal=maxVersion_;
            reply.rpc=backend_.execute ? Rpc::none : Rpc::unsupported;Send(peer,reply);
            // A new member (or one back): every queue out now, so its map draws them from the start.
            for(const auto& e:ways_)if(e.used)SendWays(peer,e);
        } else if(message.kind==Kind::request) {
            // Above what this peer agreed (a reset, a replayed packet of another session): not executed.
            if(message.version>peer_[peer].version)return;
            Execute(peer,puid,message,now);
        }
    } else if(peer==hostPeer_) {
        if(message.kind==Kind::capability) {
            hostCapable_=message.rpc==Rpc::none;
            hostVersion_=message.version<maxVersion_ ? message.version : maxVersion_;
        }
        else if(message.kind==Kind::ways) {
            WaysEntry* e=WaysSlot(message.request.units[0],message.ways.n>0);
            if(e && message.sequence>e->revision) {   // newest wins (a reordered older one ignored)
                e->revision=message.sequence;e->ways=message.ways;
                if(!message.ways.n)e->used=false;
            }
        }
        else if(message.kind==Kind::result && result_.state==CommandNetworkState::pending &&
                message.sequence==pending_.sequence && message.request.count==pending_.request.count)ResultOf(message);
    }
}
std::uint32_t Session::Submit(const Request& request,const char* puid,std::uint64_t now) noexcept {
    const std::uint32_t version=host_ ? maxVersion_ : hostVersion_;
    if(!Ready() || !puid || !puid[0] || !ValidRequest(request,version) || nextSequence_==UINT32_MAX ||
       result_.state==CommandNetworkState::pending)return 0;
    pending_={};pending_.version=version;pending_.kind=Kind::request;pending_.epoch=epoch_;pending_.sequence=++nextSequence_;
    pending_.request=request;
    result_={};result_.request=pending_.sequence;result_.count=request.count;result_.state=CommandNetworkState::pending;
    pendingAt_=lastSend_=now;
    if(host_) {peer_[0].capable=true;Execute(0,puid,pending_,now);}
    else if(!Send(hostPeer_,pending_)){auto reply=pending_;reply.rpc=Rpc::interrupted;ResultOf(reply);}
    return pending_.sequence;
}
}
