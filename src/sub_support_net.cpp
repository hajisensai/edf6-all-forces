#include "sub_support_state.h"
#include "command_net.h"
#include <algorithm>
#include <iterator>
#include <limits>

namespace crew::sub_support {
namespace {
void Put(unsigned char* p,std::uint64_t value,unsigned n) noexcept {
    for(unsigned i=0;i<n;++i)p[i]=static_cast<unsigned char>(value>>(i*8));
}
std::uint64_t Get(const unsigned char* p,unsigned n) noexcept {
    std::uint64_t value=0;
    for(unsigned i=0;i<n;++i)value|=std::uint64_t(p[i])<<(i*8);
    return value;
}
void PutFloat(unsigned char* p,float value) noexcept {
    static_assert(sizeof(float)==4,"the explicit wire float is IEEE binary32");
    std::uint32_t bits=0;std::memcpy(&bits,&value,4);Put(p,bits,4);
}
float GetFloat(const unsigned char* p) noexcept {
    const auto bits=static_cast<std::uint32_t>(Get(p,4));float value=0;std::memcpy(&value,&bits,4);return value;
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
bool ValidSnapshot(const Snapshot& s) noexcept {
    if(!s.epoch || !s.sequence || s.count>kMaxHulls)return false;
    for(unsigned i=0;i<s.count;++i) {
        if(!s.rows[i].revision || !ValidState(s.rows[i].state))return false;
        for(unsigned j=0;j<i;++j)if(!std::memcmp(s.rows[i].state.netId,s.rows[j].state.netId,32))return false;
    }
    return true;
}
} // namespace
bool Owned(const void* data,std::size_t size) noexcept {
    return data && size>=4 && Get(static_cast<const unsigned char*>(data),4)==kMagic;
}
std::uint32_t Checksum(const unsigned char* bytes,std::size_t size) noexcept {
    if(!bytes)return 0;
    std::uint32_t hash=2166136261u;
    for(std::size_t i=0;i<size;++i)hash=(hash^((i>=28 && i<32) ? 0u : bytes[i]))*16777619u;
    return hash;
}
std::size_t Encode(const Snapshot& s,void* data,std::size_t capacity) noexcept {
    if(!data || !ValidSnapshot(s))return 0;
    const std::size_t size=kHeaderBytes+s.count*kRowBytes;
    if(capacity<size)return 0;
    auto* out=static_cast<unsigned char*>(data);std::memset(out,0,size);
    Put(out,kMagic,4);out[4]=kVersion;out[5]=1;Put(out+6,size,2);
    Put(out+8,s.epoch,8);Put(out+16,s.sequence,8);out[24]=static_cast<unsigned char>(s.count);
    for(unsigned i=0;i<s.count;++i) {
        auto* row=out+kHeaderBytes+i*kRowBytes;const auto& srow=s.rows[i];const auto& state=srow.state;
        Put(row,srow.revision,8);std::memcpy(row+8,state.netId,32);
        for(unsigned k=0;k<kSystems;++k) {
            PutFloat(row+40+k*4,state.wear[k]);
            Put(row+56+k*4,state.repairRemainingMs[k],4);Put(row+72+k*4,state.reloadRemainingMs[k],4);
        }
        Put(row+88,state.missileRemainingMs,4);Put(row+92,state.droneRemainingMs,4);
        row[96]=state.downMask;row[97]=state.flags;row[98]=state.droneCount;row[99]=state.turretMask;
        for(unsigned t=0;t<kTurrets;++t)for(unsigned k=0;k<3;++k)PutFloat(row+100+t*12+k*4,state.turretAim[t][k]);
    }
    Put(out+28,Checksum(out,size),4);return size;
}
bool Decode(const void* data,std::size_t size,Snapshot& result) noexcept {
    if(!data || size<kHeaderBytes || size>kMaxWireBytes)return false;
    const auto* in=static_cast<const unsigned char*>(data);
    if(!Owned(data,size) || in[4]!=kVersion || in[5]!=1 || Get(in+6,2)!=size ||
       in[24]>kMaxHulls || in[25] || in[26] || in[27] || Get(in+28,4)!=Checksum(in,size) ||
       size!=kHeaderBytes+std::size_t(in[24])*kRowBytes)return false;
    Snapshot next{};next.epoch=Get(in+8,8);next.sequence=Get(in+16,8);next.count=in[24];
    for(unsigned i=0;i<next.count;++i) {
        const auto* row=in+kHeaderBytes+i*kRowBytes;auto& srow=next.rows[i];auto& state=srow.state;
        srow.revision=Get(row,8);std::memcpy(state.netId,row+8,32);
        for(unsigned k=0;k<kSystems;++k) {
            state.wear[k]=GetFloat(row+40+k*4);
            state.repairRemainingMs[k]=static_cast<std::uint32_t>(Get(row+56+k*4,4));
            state.reloadRemainingMs[k]=static_cast<std::uint32_t>(Get(row+72+k*4,4));
        }
        state.missileRemainingMs=static_cast<std::uint32_t>(Get(row+88,4));
        state.droneRemainingMs=static_cast<std::uint32_t>(Get(row+92,4));
        state.downMask=row[96];state.flags=row[97];state.droneCount=row[98];state.turretMask=row[99];
        for(unsigned t=0;t<kTurrets;++t)for(unsigned k=0;k<3;++k)state.turretAim[t][k]=GetFloat(row+100+t*12+k*4);
    }
    if(!ValidSnapshot(next))return false;
    result=next;return true;
}
void Protocol::Reset() noexcept {
    if(epoch_)retiredEpoch_=epoch_; // retain no world IDs, only the retired replay namespace
    ready_=host_=retry_=sent_=false;epoch_=sequence_=revision_=lastSend_=received_=0;
    hostPeer_=peerCount_=0;localPuid_[0]=hostPuid_[0]=0;send_=nullptr;current_={};
    for(auto& applied:applied_)applied={};
}
bool Protocol::SetContext(const CommandNetworkContext& c) noexcept {
    const bool valid=c.ready && c.epoch && c.send && c.peerCount<=1024 && ValidPuid(c.localPuid) &&
        (c.host ? c.hostPeer==0 : c.hostPeer && c.hostPeer<=c.peerCount);
    if(!valid || c.epoch==retiredEpoch_){Reset();return false;}
    const bool changed=!ready_ || host_!=c.host || epoch_!=c.epoch || hostPeer_!=c.hostPeer ||
        peerCount_!=c.peerCount || send_!=c.send || std::strcmp(localPuid_,c.localPuid);
    if(changed) {
        Reset();
        if(c.epoch==retiredEpoch_)return false;
        ready_=true;host_=c.host;epoch_=c.epoch;hostPeer_=c.hostPeer;peerCount_=c.peerCount;send_=c.send;
        std::memcpy(localPuid_,c.localPuid,std::strlen(c.localPuid)+1);
    }
    return true;
}
void Protocol::Update(const CommandNetworkContext& c,std::uint64_t now) noexcept {
    if(SetContext(c) && host_)Publish(now);
}
void Protocol::Publish(std::uint64_t now) noexcept {
    if(!hooks_.enumerate)return;
    SubSupportState states[kMaxHulls]{};
    const unsigned count=hooks_.enumerate(hooks_.user,states,kMaxHulls);
    if(count>kMaxHulls)return; // fail closed: never turn an overfull list into a partial snapshot
    for(unsigned i=0;i<count;++i) {
        if(!ValidState(states[i]))return;
        for(unsigned j=0;j<i;++j)if(!std::memcmp(states[i].netId,states[j].netId,32))return;
    }
    std::sort(states,states+count,[](const SubSupportState& a,const SubSupportState& b) noexcept {
        return std::memcmp(a.netId,b.netId,32)<0;
    });
    Snapshot next{};next.epoch=epoch_;next.count=count;
    bool changed=current_.count!=count;
    std::uint64_t nextRevision=revision_;
    for(unsigned i=0;i<count;++i) {
        const Row* previous=nullptr;
        for(unsigned j=0;j<current_.count;++j)
            if(!std::memcmp(states[i].netId,current_.rows[j].state.netId,32)){previous=&current_.rows[j];break;}
        if(previous && SameState(states[i],previous->state))next.rows[i]=*previous;
        else {
            if(nextRevision==std::numeric_limits<std::uint64_t>::max())return;
            next.rows[i]={++nextRevision,states[i]};changed=true;
        }
    }
    if(!changed && !retry_ && sent_ && now>=lastSend_ && now-lastSend_<100)return;
    if(sequence_==std::numeric_limits<std::uint64_t>::max())return; // no wrap into replay-eligible state
    next.sequence=++sequence_;
    unsigned char bytes[kMaxWireBytes]{};const auto size=Encode(next,bytes,sizeof(bytes));
    if(!size)return;
    // This baseline is the current local state, not a delivery acknowledgement.
    // A single failed peer leaves retry_ set so the whole current snapshot is
    // retried next update; successful peers safely ignore unchanged revisions.
    current_=next;revision_=nextRevision;retry_=false;sent_=true;lastSend_=now;
    for(std::uint32_t peer=1;peer<=peerCount_;++peer)if(!send_(peer,bytes,size))retry_=true;
}
bool Protocol::Receive(std::uint32_t peer,const char* puid,const void* data,std::size_t size,std::uint64_t now) noexcept {
    (void)now; // native callbacks translate remaining times using their own game clock
    if(!ready_ || host_ || peer!=hostPeer_ || !peer || peer>peerCount_ || !ValidPuid(puid) ||
       (hostPuid_[0] && std::strcmp(hostPuid_,puid)) || !hooks_.read || !hooks_.apply)return false;
    Snapshot message{};
    if(!Decode(data,size,message) || message.epoch!=epoch_ || message.sequence<=received_)return false;
    // Check every equal-revision payload before any native write. Revision
    // equality may repeat a state, never silently replace it with another one.
    for(unsigned i=0;i<message.count;++i)for(const auto& applied:applied_)
        if(applied.used && !std::memcmp(applied.row.state.netId,message.rows[i].state.netId,32) &&
           applied.row.revision==message.rows[i].revision && !SameState(applied.row.state,message.rows[i].state))return false;
    if(!hostPuid_[0])std::memcpy(hostPuid_,puid,std::strlen(puid)+1);
    received_=message.sequence;
    for(unsigned i=0;i<message.count;++i) {
        const Row& row=message.rows[i];Applied* saved=nullptr;
        for(auto& applied:applied_)
            if(applied.used && !std::memcmp(applied.row.state.netId,row.state.netId,32)){saved=&applied;break;}
        if(saved && row.revision<=saved->row.revision){saved->seen=message.sequence;continue;}
        SubSupportState live{};
        if(!hooks_.read(hooks_.user,row.state.netId,&live) || !ValidState(live) ||
           std::memcmp(live.netId,row.state.netId,32) || !hooks_.apply(hooks_.user,row.state))continue;
        // Unknown/not-yet-activated or failed native application does not consume
        // its revision: a later periodic snapshot of the same state can succeed.
        if(!saved) {
            for(auto& applied:applied_)if(!applied.used){saved=&applied;break;}
            if(!saved)saved=&*std::min_element(std::begin(applied_),std::end(applied_),
                [](const Applied& a,const Applied& b) noexcept {return a.seen<b.seen;});
        }
        *saved={true,message.sequence,row};
    }
    return true;
}
} // namespace crew::sub_support

namespace crew {
namespace {
unsigned Enumerate(void*,SubSupportState* out,unsigned count) noexcept {return EnumerateSubSupportStates(out,count);}
bool Read(void*,const unsigned char* id,SubSupportState* out) noexcept {return ReadSubSupportState(id,out);}
bool Apply(void*,const SubSupportState& state) noexcept {return ApplySubSupportState(state);}
sub_support::Protocol protocol({nullptr,&Enumerate,&Read,&Apply});
}
void UpdateSubSupportNetwork(const CommandNetworkContext& context,std::uint64_t now) noexcept {protocol.Update(context,now);}
bool ReceiveSubSupportNetwork(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t now) noexcept {
    if(!sub_support::Owned(bytes,size))return false;
    protocol.Receive(peer,puid,bytes,size,now);return true;
}
void ResetSubSupportNetwork() noexcept {protocol.Reset();}
} // namespace crew
