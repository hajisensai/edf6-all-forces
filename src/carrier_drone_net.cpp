#include "carrier_drone_net.h"
#include "command_net.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace crew::carrier_drone_net {
namespace {
void Put(unsigned char* p,std::uint64_t value,unsigned n) noexcept {
    for(unsigned i=0;i<n;++i)p[i]=static_cast<unsigned char>(value>>(i*8));
}
std::uint64_t Get(const unsigned char* p,unsigned n) noexcept {
    std::uint64_t value=0;for(unsigned i=0;i<n;++i)value|=std::uint64_t(p[i])<<(i*8);return value;
}
bool ValidPuid(const char* text) noexcept {
    if(!text || !text[0])return false;
    for(unsigned i=0;i<65;++i) {
        const auto c=static_cast<unsigned char>(text[i]);
        if(!c)return true;
        if(c<33 || c>126)return false;
    }
    return false;
}
} // namespace
bool ValidId(const unsigned char* id) noexcept {
    if(!id)return false;
    for(unsigned i=20;i<24;++i)if(id[i])return false;
    for(unsigned i=0;i<32;++i)if(id[i])return true;
    return false;
}
bool ValidRequest(const Request& r) noexcept {
    if(!r.epoch || !r.nonce || !ValidId(r.parentId) || r.kind>kMaxKind || r.slot>kMaxSlot)return false;
    for(float value:r.anchor)if(!std::isfinite(value) || std::fabs(value)>kMaxAnchor)return false;
    if(r.kind==kRecall) {
        if(r.slot)return false;
        for(float value:r.anchor)if(value!=0)return false;
    }
    return true;
}
bool Owned(const void* data,std::size_t size) noexcept {
    return data && size>=4 && Get(static_cast<const unsigned char*>(data),4)==kMagic;
}
std::uint32_t Checksum(const unsigned char* bytes,std::size_t size) noexcept {
    if(!bytes)return 0;
    std::uint32_t hash=2166136261u;
    for(std::size_t i=0;i<size;++i)hash=(hash^((i>=24 && i<28) ? 0u : bytes[i]))*16777619u;
    return hash;
}
bool Encode(const Request& r,void* data,std::size_t size) noexcept {
    if(!data || size!=kWireBytes || !ValidRequest(r))return false;
    auto* out=static_cast<unsigned char*>(data);std::memset(out,0,size);
    Put(out,kMagic,4);out[4]=kVersion;out[5]=1;Put(out+6,kWireBytes,2);
    Put(out+8,r.epoch,8);Put(out+16,r.nonce,8);std::memcpy(out+32,r.parentId,32);
    out[64]=r.kind;out[65]=r.slot;
    static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"wire anchor is IEEE binary32");
    for(unsigned i=0;i<3;++i){std::uint32_t bits=0;std::memcpy(&bits,&r.anchor[i],4);Put(out+68+i*4,bits,4);}
    Put(out+24,Checksum(out,size),4);return true;
}
bool Decode(const void* data,std::size_t size,Request& result) noexcept {
    if(!data || size!=kWireBytes)return false;
    const auto* in=static_cast<const unsigned char*>(data);
    if(!Owned(data,size) || in[4]!=kVersion || in[5]!=1 || Get(in+6,2)!=kWireBytes ||
       Get(in+24,4)!=Checksum(in,size) || Get(in+28,4) || in[66] || in[67])return false;
    Request next{};next.epoch=Get(in+8,8);next.nonce=Get(in+16,8);std::memcpy(next.parentId,in+32,32);
    next.kind=in[64];next.slot=in[65];
    for(unsigned i=0;i<3;++i){const auto bits=static_cast<std::uint32_t>(Get(in+68+i*4,4));std::memcpy(&next.anchor[i],&bits,4);}
    if(!ValidRequest(next))return false;
    result=next;return true;
}
void Protocol::Reset() noexcept {
    if(epoch_)retiredEpoch_=epoch_;
    ready_=host_=false;epoch_=nextNonce_=0;hostPeer_=peerCount_=0;localPuid_[0]=0;send_=nullptr;
    for(auto& peer:peers_)peer={};
}
void Protocol::Update(const CommandNetworkContext& c) noexcept {
    const bool valid=c.ready && c.epoch && c.send && c.peerCount<=kMaxPeers && ValidPuid(c.localPuid) &&
        (c.host ? c.hostPeer==0 : c.hostPeer && c.hostPeer<=c.peerCount);
    if(!valid || c.epoch==retiredEpoch_){Reset();return;}
    const bool changed=!ready_ || host_!=c.host || epoch_!=c.epoch || hostPeer_!=c.hostPeer ||
        peerCount_!=c.peerCount || send_!=c.send || std::strcmp(localPuid_,c.localPuid);
    if(changed) {
        Reset();if(c.epoch==retiredEpoch_)return;
        ready_=true;host_=c.host;epoch_=c.epoch;hostPeer_=c.hostPeer;peerCount_=c.peerCount;send_=c.send;
        std::memcpy(localPuid_,c.localPuid,std::strlen(c.localPuid)+1);
    }
}
bool Protocol::Submit(const unsigned char* parentId,unsigned kind,unsigned slot,const float* anchor) noexcept {
    if(!ready_ || host_ || !parentId || !anchor || kind>kMaxKind || slot>kMaxSlot ||
       nextNonce_==std::numeric_limits<std::uint64_t>::max())return false;
    Request request{};request.epoch=epoch_;request.nonce=nextNonce_+1;request.kind=static_cast<std::uint8_t>(kind);
    request.slot=static_cast<std::uint8_t>(slot);std::memcpy(request.parentId,parentId,32);std::memcpy(request.anchor,anchor,12);
    unsigned char bytes[kWireBytes]{};
    if(!Encode(request,bytes,sizeof(bytes)))return false;
    // A failed queue attempt may have an ambiguous transport outcome. Never
    // reuse its nonce, retry automatically or spawn a client-only substitute.
    nextNonce_=request.nonce;return send_(hostPeer_,bytes,sizeof(bytes));
}
bool Protocol::Receive(std::uint32_t peer,const char* puid,const void* data,std::size_t size) noexcept {
    if(!ready_ || !host_ || !peer || peer>peerCount_ || !ValidPuid(puid))return false;
    Request request{};
    if(!Decode(data,size,request) || request.epoch!=epoch_)return false;
    auto& sender=peers_[peer];
    if((sender.puid[0] && std::strcmp(sender.puid,puid)) || request.nonce<=sender.nonce)return false;
    if(!sender.puid[0])std::memcpy(sender.puid,puid,std::strlen(puid)+1);
    // Consume before entering the dispatcher, including rejection/transitions
    // and reentrant calls. Only the next explicit input may submit a new nonce.
    sender.nonce=request.nonce;
    if(hooks_.handle)hooks_.handle(hooks_.user,peer,puid,request.parentId,request.kind,request.slot,request.anchor);
    return true;
}
} // namespace crew::carrier_drone_net

namespace crew {
namespace {
bool Handle(void*,std::uint32_t peer,const char* puid,const unsigned char* parent,unsigned kind,unsigned slot,const float* anchor) noexcept {
    return HandleCarrierDroneNetworkRequest(peer,puid,parent,kind,slot,anchor);
}
carrier_drone_net::Protocol protocol({nullptr,&Handle});
}
void UpdateCarrierDroneNetwork(const CommandNetworkContext& context,std::uint64_t now) noexcept {(void)now;protocol.Update(context);}
bool ReceiveCarrierDroneNetwork(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t now) noexcept {
    (void)now;if(!carrier_drone_net::Owned(bytes,size))return false;
    protocol.Receive(peer,puid,bytes,size);return true;
}
void ResetCarrierDroneNetwork() noexcept {protocol.Reset();}
bool SubmitCarrierDroneNetworkRequest(const unsigned char* parent,unsigned kind,unsigned slot,const float* anchor) noexcept {
    return protocol.Submit(parent,kind,slot,anchor);
}
} // namespace crew
