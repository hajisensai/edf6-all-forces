#include "carrier_drone_net.h"
#include "command_net.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

namespace cd=crew::carrier_drone_net;
using crew::CommandNetworkContext;
namespace {
unsigned checks=0;
void Check(bool yes,const char* label) {
    ++checks;if(!yes){std::fprintf(stderr,"FAIL %s (check %u)\n",label,checks);std::exit(1);}
}
cd::Request Request(std::uint64_t nonce=1,std::uint64_t epoch=77) {
    cd::Request r{};r.epoch=epoch;r.nonce=nonce;r.parentId[0]=13;r.parentId[31]=89;
    r.kind=2;r.slot=5;r.anchor[0]=15.25f;r.anchor[1]=-800;r.anchor[2]=1.0e6f;return r;
}
using Bytes=std::vector<unsigned char>;
Bytes Encode(const cd::Request& r) {
    Bytes b(cd::kWireBytes);Check(cd::Encode(r,b.data(),b.size()),"fixture encodes valid request");return b;
}
void Put(Bytes& b,std::size_t at,std::uint64_t value,unsigned size=4) {
    for(unsigned i=0;i<size;++i)b[at+i]=static_cast<unsigned char>(value>>(i*8));
}
void Rehash(Bytes& b) {Put(b,24,cd::Checksum(b.data(),b.size()));}
void Bad(Bytes b,const char* label) {
    Rehash(b);cd::Request out{};out.nonce=12345;
    Check(!cd::Decode(b.data(),b.size(),out),label);Check(out.nonce==12345,"malformed decode leaves output untouched");
}
struct Packet {unsigned peer=0;Bytes data;};
std::vector<Packet> packets,attempts;
unsigned failures=0;
bool Send(std::uint32_t peer,const void* data,std::size_t size) noexcept {
    const auto* p=static_cast<const unsigned char*>(data);Packet packet{peer,Bytes(p,p+size)};attempts.push_back(packet);
    if(failures){--failures;return false;}packets.push_back(packet);return true;
}
bool OtherSend(std::uint32_t peer,const void* data,std::size_t size) noexcept {return Send(peer,data,size);}
CommandNetworkContext Context(bool host=false,std::uint64_t epoch=77,unsigned peers=2,unsigned hostPeer=2) {
    return {true,host,epoch,host ? 0u : hostPeer,peers,host ? "host-local" : "guest-local",&Send};
}
struct Backend {
    unsigned calls=0,peer=0,kind=0,slot=0;
    char puid[65]{};unsigned char parent[32]{};float anchor[3]{};
    bool result=true,recursiveResult=true;
    cd::Protocol* recursive=nullptr;const Bytes* replay=nullptr;
};
bool Handle(void* user,std::uint32_t peer,const char* puid,const unsigned char* parent,unsigned kind,unsigned slot,const float* anchor) noexcept {
    auto& b=*static_cast<Backend*>(user);++b.calls;b.peer=peer;b.kind=kind;b.slot=slot;
    std::memcpy(b.puid,puid,std::strlen(puid)+1);std::memcpy(b.parent,parent,32);std::memcpy(b.anchor,anchor,12);
    if(b.recursive && b.replay)b.recursiveResult=b.recursive->Receive(peer,puid,b.replay->data(),b.replay->size());
    return b.result;
}
bool Receive(cd::Protocol& host,const cd::Request& r,unsigned peer=1,const char* puid="guest-auth-a") {
    const auto bytes=Encode(r);return host.Receive(peer,puid,bytes.data(),bytes.size());
}
void Codec() {
    for(unsigned kind=0;kind<3;++kind)for(unsigned slot=0;slot<6;++slot) {
        auto input=Request();input.kind=static_cast<std::uint8_t>(kind);input.slot=static_cast<std::uint8_t>(slot);
        const auto b=Encode(input);cd::Request out{};
        Check(b.size()==80 && cd::kWireBytes<1024,"fixed 80-byte request excludes arbitrary player/team/matrix payload");
        Check(cd::Decode(b.data(),b.size(),out),"all 18 kind/slot combinations decode");
        Check(out.epoch==input.epoch && out.nonce==input.nonce && out.kind==kind && out.slot==slot &&
            !std::memcmp(out.parentId,input.parentId,32) && !std::memcmp(out.anchor,input.anchor,12),"all explicit fields round trip");
    }
    const auto good=Encode(Request());cd::Request out{};
    Check(!cd::Decode(nullptr,80,out) && !cd::Owned(nullptr,80),"null data rejected");
    for(std::size_t n=0;n<80;++n)Check(!cd::Decode(good.data(),n,out),"every truncation rejected");
    auto x=good;x.push_back(0);Check(!cd::Decode(x.data(),x.size(),out),"tail data rejected");
    Check(!cd::Owned(good.data(),3) && cd::Owned(good.data(),4),"demux ownership requires only four valid magic bytes");
    x=good;Check(!cd::Encode(Request(),x.data(),79) && !cd::Encode(Request(),x.data(),81),"encoder requires exact explicit size");
    x=good;x[0]^=1;Bad(x,"wrong magic");x=good;x[4]=2;Bad(x,"wrong version");
    x=good;x[5]=2;Bad(x,"wrong wire operation");x=good;Put(x,6,79,2);Bad(x,"declared size mismatch");
    for(unsigned at:{28u,29u,30u,31u,66u,67u}){x=good;x[at]=1;Bad(x,"reserved bits rejected");}
    x=good;Put(x,8,0,8);Bad(x,"zero shared epoch");x=good;Put(x,16,0,8);Bad(x,"zero nonce");
    x=good;std::memset(x.data()+32,0,32);Bad(x,"zero parent identity");
    for(unsigned i=20;i<24;++i){x=good;x[32+i]=1;Bad(x,"canonical native identity padding required");}
    x=good;x[64]=4;Bad(x,"invalid drone kind");x=good;x[65]=6;Bad(x,"invalid requested slot");
    for(unsigned i=0;i<3;++i)for(std::uint32_t bits:{0x7fc00000u,0x7f800000u,0xff800000u,0x7f7fffffu,0xff7fffffu}) {
        x=good;Put(x,68+i*4,bits);Bad(x,"nonfinite or unbounded anchor component");
    }
    x=good;x.back()^=1;Check(!cd::Decode(x.data(),x.size(),out),"checksum rejects corruption");
    auto boundary=Request();for(float& f:boundary.anchor)f=-cd::kMaxAnchor;
    Check(cd::ValidRequest(boundary),"finite negative anchor boundary valid");
    boundary.anchor[0]=std::numeric_limits<float>::quiet_NaN();Check(!cd::ValidRequest(boundary),"native request validates finite anchor too");
}
void GuestQueue() {
    Backend backend;cd::Protocol guest({&backend,&Handle});const auto r=Request();
    Check(!guest.Submit(r.parentId,r.kind,r.slot,r.anchor),"offline guest has no local fallback");
    guest.Update(Context());packets.clear();attempts.clear();failures=0;
    Check(guest.Submit(r.parentId,0,0,r.anchor) && packets.size()==1 && packets[0].peer==2,"guest sends only to authenticated host peer");
    cd::Request first{};Check(cd::Decode(packets[0].data.data(),packets[0].data.size(),first) && first.nonce==1 && first.epoch==77,
        "guest wire uses shared epoch and first monotonic nonce");
    Check(!guest.Submit(nullptr,0,0,r.anchor) && !guest.Submit(r.parentId,0,0,nullptr),"null submit arguments rejected");
    Check(!guest.Submit(r.parentId,4,0,r.anchor) && !guest.Submit(r.parentId,0,6,r.anchor) &&
        !guest.Submit(r.parentId,256,256,r.anchor),"wide input cannot truncate into allowed byte kind or slot");
    auto invalid=r;invalid.anchor[1]=std::numeric_limits<float>::infinity();
    Check(!guest.Submit(invalid.parentId,0,0,invalid.anchor),"guest does not queue nonfinite anchors");
    invalid=r;invalid.parentId[20]=1;Check(!guest.Submit(invalid.parentId,0,0,invalid.anchor),"guest requires canonical full parent identity");
    Check(packets.size()==1 && !backend.calls,"invalid input never dispatches or sends");
    failures=1;Check(!guest.Submit(r.parentId,1,1,r.anchor),"submit false means reliable queue rejected request");
    Check(attempts.size()==2 && packets.size()==1,"failed queue does not invent a local drone");
    for(unsigned i=0;i<10;++i)guest.Update(Context());
    Check(attempts.size()==2 && !backend.calls,"ticks never retry or create a drone automatically");
    Check(guest.Submit(r.parentId,2,5,r.anchor),"next real input can submit after queue failure");
    cd::Request next{};Check(cd::Decode(packets.back().data.data(),80,next) && next.nonce==3,"failed enqueue nonce is not reused");
    Check(!Receive(guest,r) && !backend.calls,"a guest never handles network spawn requests");
    Backend hostBackend;cd::Protocol host({&hostBackend,&Handle});host.Update(Context(true));
    const auto sends=attempts.size();Check(!host.Submit(r.parentId,0,0,r.anchor) && attempts.size()==sends && !hostBackend.calls,
        "host Submit cannot bypass dispatcher with a second local path");
}
void AuthAndFreshness() {
    Backend backend;cd::Protocol host({&backend,&Handle});host.Update(Context(true));auto r=Request();
    Check(!Receive(host,r,0) && !Receive(host,r,3),"host accepts only nonzero authenticated current peer indices");
    Check(!Receive(host,r,1,nullptr) && !Receive(host,r,1,"") && !Receive(host,r,1,"bad puid"),"missing or malformed auth identity rejected");
    char text[65];std::memset(text,'x',sizeof(text));Check(!Receive(host,r,1,text),"unterminated auth identity rejected");
    auto stale=r;stale.epoch=76;stale.nonce=5000;Check(!Receive(host,stale),"wrong epoch never consumes a nonce");
    Check(Receive(host,r) && backend.calls==1,"first valid request dispatches");
    Check(backend.peer==1 && !std::strcmp(backend.puid,"guest-auth-a") && !std::memcmp(backend.parent,r.parentId,32) &&
        backend.kind==r.kind && backend.slot==r.slot && !std::memcmp(backend.anchor,r.anchor,12),"dispatcher receives only exact validated request and transport identity");
    Check(!Receive(host,r) && backend.calls==1,"duplicate request never dispatches twice");
    r.nonce=3;Check(Receive(host,r) && backend.calls==2,"newer nonce dispatches");
    r.nonce=2;Check(!Receive(host,r) && backend.calls==2,"late request rejected");
    r.nonce=4;Check(!Receive(host,r,1,"replacement-identity") && backend.calls==2,"same-epoch peer identity replacement rejected");
    Check(Receive(host,r) && backend.calls==3,"identity rejection does not poison legitimate sender nonce");
    auto other=Request();Check(Receive(host,other,2,"guest-auth-b") && backend.calls==4,"each authenticated peer has independent nonce namespace");
    backend.result=false;r.nonce=5;
    Check(Receive(host,r) && backend.calls==5,"authorization or transition denial is still consumed");
    backend.result=true;Check(!Receive(host,r) && backend.calls==5,"denied request cannot later replay when permissions change");
    r.nonce=6;r.parentId[31]^=1;Check(Receive(host,r) && backend.calls==6 && backend.parent[31]==r.parentId[31],
        "dispatcher gets all 32 identity bytes instead of prefix or hash substitution");
    r.nonce=std::numeric_limits<std::uint64_t>::max();Check(Receive(host,r) && backend.calls==7,"maximum nonce valid without wrap");
    r.nonce=1;Check(!Receive(host,r) && backend.calls==7,"wrapped nonce cannot reopen prior requests");
    cd::Protocol bare;bare.Update(Context(true));Check(Receive(bare,Request()),"missing optional backend remains fail-closed with request consumed");
}
void Reentry() {
    Backend b;cd::Protocol p({&b,&Handle});p.Update(Context(true));const auto r=Request();const auto bytes=Encode(r);
    b.recursive=&p;b.replay=&bytes;b.result=false;
    Check(p.Receive(1,"guest-auth-a",bytes.data(),bytes.size()) && b.calls==1 && !b.recursiveResult,
        "nonce is consumed before handler can recursively dispatch the same request");
}
void Recall() {
    Backend b;cd::Protocol host({&b,&Handle}),guest;host.Update(Context(true));guest.Update(Context());
    auto recall=Request();recall.kind=cd::kRecall;recall.slot=0;for(float& value:recall.anchor)value=0;
    const auto wire=Encode(recall);cd::Request decoded{};
    Check(cd::Decode(wire.data(),wire.size(),decoded) && decoded.kind==cd::kRecall && decoded.slot==0,
        "recall has explicit public operation and round trips through request codec");
    for(unsigned slot=1;slot<=5;++slot) {auto malformed=wire;malformed[65]=static_cast<unsigned char>(slot);Bad(malformed,"recall rejects any nonzero slot");}
    for(unsigned i=0;i<3;++i) {auto malformed=wire;Put(malformed,68+i*4,0x3f800000u);Bad(malformed,"recall rejects any nonzero anchor component");}
    packets.clear();attempts.clear();failures=0;
    Check(guest.Submit(recall.parentId,cd::kRecall,0,recall.anchor) && packets.size()==1,"guest recall queues one host request");
    Check(host.Receive(1,"recall-driver",packets[0].data.data(),80) && b.calls==1 && b.kind==cd::kRecall && b.slot==0 &&
        b.anchor[0]==0 && b.anchor[1]==0 && b.anchor[2]==0,"host dispatches authenticated recall exactly once");
    Check(!host.Receive(1,"recall-driver",packets[0].data.data(),80) && b.calls==1,"replayed recall never dispatches twice");
    const auto sends=attempts.size();guest.Update(Context());
    Check(attempts.size()==sends,"recall has no automatic resend or local mode fallback");
    Check(!guest.Submit(recall.parentId,cd::kRecall,1,recall.anchor) && !guest.Submit(recall.parentId,cd::kRecall,0,Request().anchor),
        "guest rejects malformed recall before queueing");
    b.result=false;recall.nonce=2;
    Check(Receive(host,recall,1,"recall-driver") && b.calls==2,"denied recall still consumes its request nonce");
    b.result=true;Check(!Receive(host,recall,1,"recall-driver") && b.calls==2,"denied recall cannot replay after authorization changes");
}
void ContextLifetime() {
    Backend b;cd::Protocol p({&b,&Handle});auto c=Context(true);p.Update(c);auto r=Request();Check(Receive(p,r),"context starts");
    c.ready=false;p.Update(c);Check(!p.Ready() && p.Epoch()==0 && !Receive(p,r),"lost context clears peer identities and epoch");
    c.ready=true;p.Update(c);Check(!p.Ready() && !Receive(p,r),"same shared epoch cannot reopen replay after local loss");
    c.epoch=78;p.Update(c);r.epoch=78;
    Check(Receive(p,r,1,"new-auth") && b.calls==2,"new shared epoch permits new peer binding and low nonce");
    c.send=&OtherSend;p.Update(c);Check(!p.Ready(),"host-only backend rebinding retires current shared epoch");
    c.epoch=79;p.Update(c);r.epoch=79;Check(Receive(p,r,1,"third-auth"),"backend rebound under new epoch");
    c.peerCount=1;p.Update(c);Check(!p.Ready(),"roster mutation requires new epoch");
    c=Context(true,80);char local[65]="original-local";c.localPuid=local;p.Update(c);
    std::memcpy(local,"changed-local",sizeof("changed-local"));p.Update(c);
    Check(!p.Ready(),"in-place identity replacement detected via deep copy");
    c=Context(true,81);p.Update(c);c.host=false;c.hostPeer=1;p.Update(c);
    Check(!p.Ready(),"authority role change retires current epoch");
    c=Context(false,82);p.Update(c);c.hostPeer=1;p.Update(c);Check(!p.Ready(),"host peer change retires current epoch");
    for(unsigned mode=0;mode<5;++mode) {
        c=Context(false,83+mode);
        if(mode==0)c.epoch=0;else if(mode==1)c.send=nullptr;else if(mode==2)c.localPuid="";
        else if(mode==3)c.peerCount=cd::kMaxPeers+1;else c.hostPeer=3;
        p.Update(c);Check(!p.Ready(),"invalid context fails closed");
    }
    c=Context(true,90,cd::kMaxPeers);p.Update(c);r.epoch=90;
    Check(Receive(p,r,cd::kMaxPeers,"last-peer"),"largest allowed peer index remains bounded and usable");
    p.Reset();Check(!p.Ready() && !Receive(p,r),"explicit reset clears all peer watermarks and bindings");
}
void MultiEndpoint() {
    Backend b;cd::Protocol host({&b,&Handle}),a,d;auto hc=Context(true),ac=Context(),dc=Context();
    ac.localPuid="client-a";dc.localPuid="client-b";host.Update(hc);a.Update(ac);d.Update(dc);const auto r=Request();
    packets.clear();attempts.clear();failures=0;
    Check(a.Submit(r.parentId,0,0,r.anchor) && d.Submit(r.parentId,2,5,r.anchor),"two guests queue independently");
    Check(packets.size()==2,"two explicit inputs produce exactly two requests");
    Check(host.Receive(1,"auth-a",packets[0].data.data(),80) && host.Receive(2,"auth-b",packets[1].data.data(),80) && b.calls==2,
        "production codec dispatches both nonce-one requests without cross-peer collision");
    Check(!host.Receive(1,"auth-b",packets[1].data.data(),80) && b.calls==2,"peer spoof cannot cross existing PUID binding");
    const auto old=packets[0].data;ac.ready=false;a.Update(ac);ac.ready=true;a.Update(ac);
    Check(!a.Submit(r.parentId,0,0,r.anchor),"client-only context reset cannot emit nonce-one into old host epoch");
    Check(!host.Receive(1,"auth-a",old.data(),80) && b.calls==2,"old captured request remains rejected at unchanged host");
    hc.epoch=ac.epoch=dc.epoch=91;host.Update(hc);a.Update(ac);d.Update(dc);packets.clear();
    Check(a.Submit(r.parentId,1,3,r.anchor) && host.Receive(1,"new-auth-a",packets[0].data.data(),80) && b.calls==3,
        "newly negotiated shared epoch restores guest-to-host path");
}
Backend wrapper;
void ProductionWrapper() {
    crew::ResetCarrierDroneNetwork();packets.clear();attempts.clear();failures=0;const auto r=Request();
    crew::UpdateCarrierDroneNetwork(Context(false,500),0);
    Check(crew::SubmitCarrierDroneNetworkRequest(r.parentId,1,4,r.anchor) && packets.size()==1,"production Submit uses guest reliable queue");
    crew::ResetCarrierDroneNetwork();crew::UpdateCarrierDroneNetwork(Context(true,501),0);
    wrapper.result=false;auto request=Request(1,501);auto bytes=Encode(request);
    Check(crew::ReceiveCarrierDroneNetwork(1,"real-auth",bytes.data(),80,0) && wrapper.calls==1 &&
        !std::strcmp(wrapper.puid,"real-auth"),"production wrapper calls dispatcher with authenticated poll identity");
    Check(crew::ReceiveCarrierDroneNetwork(1,"real-auth",bytes.data(),80,0) && wrapper.calls==1,"owned duplicate consumed without dispatcher retry");
    bytes[4]=2;Check(crew::ReceiveCarrierDroneNetwork(1,"real-auth",bytes.data(),80,0) && wrapper.calls==1,"owned wrong version consumed safely");
    bytes[0]^=1;Check(!crew::ReceiveCarrierDroneNetwork(1,"real-auth",bytes.data(),80,0),"foreign protocol left to single-poll demux");
    crew::ResetCarrierDroneNetwork();bytes=Encode(request);
    Check(crew::ReceiveCarrierDroneNetwork(1,"real-auth",bytes.data(),80,0) && wrapper.calls==1,"reset wrapper cannot dispatch old world requests");
}
} // namespace
namespace crew {
bool HandleCarrierDroneNetworkRequest(std::uint32_t peer,const char* puid,const unsigned char* parent,unsigned kind,unsigned slot,const float* anchor) noexcept {
    return Handle(&wrapper,peer,puid,parent,kind,slot,anchor);
}
}
int main() {
    Codec();GuestQueue();AuthAndFreshness();Reentry();Recall();ContextLifetime();MultiEndpoint();ProductionWrapper();
    std::printf("carrier_drone_net: %u checks passed (80-byte codec, authenticated guests, dispatcher and context lifecycle)\n",checks);return 0;
}
