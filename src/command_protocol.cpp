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
        !std::memcmp(a.requester,b.requester,32) && !std::memcmp(a.focus,b.focus,32) && !std::memcmp(a.units,b.units,sizeof(a.units));
}
bool Valid(const Message& m) noexcept {
    if(!m.epoch || m.kind<Kind::hello || m.kind>Kind::result || m.rpc>=Rpc::count)return false;
    if(m.kind==Kind::hello || m.kind==Kind::capability)return m.sequence==0 && m.request.count==0 &&
        (m.rpc==Rpc::none || (m.kind==Kind::capability && m.rpc==Rpc::unsupported));
    if(!m.sequence || !m.request.count || m.request.count>kCommandNetUnits)return false;
    if(m.kind==Kind::request)return m.rpc==Rpc::none && ValidRequest(m.request);
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
bool ValidRequest(const Request& r) noexcept {
    if(!r.count || r.count>kCommandNetUnits || r.command.order>mapcmd::Order::recruit || !CanonicalId(r.requester))return false;
    for(float f:r.command.at)if(!std::isfinite(f) || std::fabs(f)>1.0e6f)return false;
    if(r.command.order==mapcmd::Order::focus) {if(!CanonicalId(r.focus) || !std::memcmp(r.focus,r.requester,32))return false;}
    else if(!Empty(r.focus))return false;
    for(std::uint32_t i=0;i<r.count;++i) {
        if(!CanonicalId(r.units[i]) || !std::memcmp(r.units[i],r.requester,32) ||
           (!Empty(r.focus) && !std::memcmp(r.units[i],r.focus,32)))return false;
        for(std::uint32_t j=0;j<i;++j)if(!std::memcmp(r.units[i],r.units[j],32))return false;
    }
    for(std::uint32_t i=r.count;i<kCommandNetUnits;++i)if(!Empty(r.units[i]))return false;
    return true;
}
bool Encode(const Message& m,void* bytes,std::size_t size) noexcept {
    if(!bytes || size!=kWireSize || !Valid(m))return false;
    auto p=static_cast<unsigned char*>(bytes);U32(p,kMagic);U32(p,kVersion);U32(p,static_cast<std::uint32_t>(m.kind));U32(p,0);
    U32(p,static_cast<std::uint32_t>(m.epoch));U32(p,static_cast<std::uint32_t>(m.epoch>>32));
    U32(p,m.sequence);U32(p,m.request.count);U32(p,static_cast<std::uint32_t>(m.request.command.order));U32(p,static_cast<std::uint32_t>(m.rpc));
    for(float f:m.request.command.at){std::uint32_t bits;std::memcpy(&bits,&f,4);U32(p,bits);}U32(p,0);
    std::memcpy(p,m.request.requester,32);p+=32;std::memcpy(p,m.request.focus,32);p+=32;
    std::memcpy(p,m.request.units,sizeof(m.request.units));p+=sizeof(m.request.units);
    for(const auto& r:m.results){U32(p,r.reason);U32(p,r.affected);}return true;
}
bool Decode(const void* bytes,std::size_t size,Message& out) noexcept {
    if(!bytes || size!=kWireSize)return false;
    auto p=static_cast<const unsigned char*>(bytes);if(U32(p)!=kMagic || U32(p)!=kVersion)return false;
    Message m;m.kind=static_cast<Kind>(U32(p));if(U32(p))return false;
    m.epoch=U32(p);m.epoch|=static_cast<std::uint64_t>(U32(p))<<32;m.sequence=U32(p);m.request.count=U32(p);
    const auto order=U32(p);if(order>static_cast<std::uint32_t>(mapcmd::Order::recruit))return false;
    m.request.command.order=static_cast<mapcmd::Order>(order);m.rpc=static_cast<Rpc>(U32(p));
    for(float& f:m.request.command.at){const auto bits=U32(p);std::memcpy(&f,&bits,4);}if(U32(p))return false;
    std::memcpy(m.request.requester,p,32);p+=32;std::memcpy(m.request.focus,p,32);p+=32;
    std::memcpy(m.request.units,p,sizeof(m.request.units));p+=sizeof(m.request.units);
    for(auto& r:m.results){r.reason=U32(p);r.affected=U32(p);}if(!Valid(m))return false;out=m;return true;
}
bool Owned(const void* bytes,std::size_t size) noexcept {
    if(!bytes || size<4)return false;auto p=static_cast<const unsigned char*>(bytes);return U32(p)==kMagic;
}
void Session::Reset() noexcept {ready_=false;hostCapable_=false;epoch_=0;nextSequence_=0;lastHello_=0;result_={};peer_.fill(Peer{});}
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
        Message hello;hello.epoch=epoch_;Send(hostPeer_,hello);lastHello_=now;
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
        } else {auto denied=request;denied.kind=Kind::result;denied.rpc=Rpc::stale;if(peer)Send(peer,denied);else ResultOf(denied);}
        return;
    }
    Message reply;reply.kind=Kind::result;reply.epoch=epoch_;reply.sequence=request.sequence;reply.request.count=request.request.count;
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
            peer_[peer].capable=true;Message reply;reply.kind=Kind::capability;reply.epoch=epoch_;
            reply.rpc=backend_.execute ? Rpc::none : Rpc::unsupported;Send(peer,reply);
        } else if(message.kind==Kind::request)Execute(peer,puid,message,now);
    } else if(peer==hostPeer_) {
        if(message.kind==Kind::capability)hostCapable_=message.rpc==Rpc::none;
        else if(message.kind==Kind::result && result_.state==CommandNetworkState::pending &&
                message.sequence==pending_.sequence && message.request.count==pending_.request.count)ResultOf(message);
    }
}
std::uint32_t Session::Submit(const Request& request,const char* puid,std::uint64_t now) noexcept {
    if(!Ready() || !puid || !puid[0] || !ValidRequest(request) || nextSequence_==UINT32_MAX || result_.state==CommandNetworkState::pending)return 0;
    pending_={};pending_.kind=Kind::request;pending_.epoch=epoch_;pending_.sequence=++nextSequence_;pending_.request=request;
    result_={};result_.request=pending_.sequence;result_.count=request.count;result_.state=CommandNetworkState::pending;
    pendingAt_=lastSend_=now;
    if(host_) {peer_[0].capable=true;Execute(0,puid,pending_,now);}
    else if(!Send(hostPeer_,pending_)){auto reply=pending_;reply.rpc=Rpc::interrupted;ResultOf(reply);}
    return pending_.sequence;
}
}
