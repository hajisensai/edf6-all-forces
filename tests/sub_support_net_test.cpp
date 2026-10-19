#include "sub_support_state.h"
#include "command_net.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

using crew::SubSupportState;
using crew::CommandNetworkContext;
namespace ss=crew::sub_support;
namespace {
unsigned checks=0;
void Check(bool yes,const char* what) {
    ++checks;if(!yes){std::fprintf(stderr,"FAIL %s (check %u)\n",what,checks);std::exit(1);}
}
SubSupportState State(unsigned id) {
    SubSupportState s{};s.netId[0]=static_cast<unsigned char>(id);s.netId[31]=static_cast<unsigned char>(id+100);
    s.flags=ss::kReady|ss::kSea;s.downMask=5;s.droneCount=3;s.turretMask=3;
    for(unsigned k=0;k<4;++k){s.wear[k]=100.25f+float(k);s.reloadRemainingMs[k]=100+k;}
    s.repairRemainingMs[0]=80000;s.repairRemainingMs[2]=90000;
    s.missileRemainingMs=2400;s.droneRemainingMs=9000;s.turretAim[0][2]=1;s.turretAim[1][0]=1;
    return s;
}
ss::Snapshot Message(unsigned count=3,std::uint64_t sequence=1,std::uint64_t epoch=77) {
    ss::Snapshot m{};m.count=count;m.epoch=epoch;m.sequence=sequence;
    for(unsigned i=0;i<count && i<3;++i)m.rows[i]={10+i,State(i+1)};
    return m;
}
using Bytes=std::vector<unsigned char>;
Bytes BytesOf(const ss::Snapshot& m) {
    unsigned char b[ss::kMaxWireBytes]{};const auto n=ss::Encode(m,b,sizeof(b));
    Check(n!=0,"fixture message encodes");return Bytes(b,b+n);
}
void Put(Bytes& b,std::size_t at,std::uint64_t value,unsigned count=4) {
    for(unsigned i=0;i<count;++i)b[at+i]=static_cast<unsigned char>(value>>(i*8));
}
void Rehash(Bytes& b) {Put(b,28,ss::Checksum(b.data(),b.size()));}
bool Decodes(const Bytes& b) {ss::Snapshot m{};return ss::Decode(b.data(),b.size(),m);}
void Bad(Bytes b,const char* what) {
    Rehash(b);ss::Snapshot output{};output.sequence=999;
    Check(!ss::Decode(b.data(),b.size(),output),what);Check(output.sequence==999,"bad decode does not partially publish");
}
struct Packet { std::uint32_t peer=0;Bytes bytes; };
std::vector<Packet> packets;
unsigned failPeer=0,failRemaining=0,sends=0;
bool Send(std::uint32_t peer,const void* bytes,std::size_t count) noexcept {
    ++sends;
    if(peer==failPeer && failRemaining){--failRemaining;return false;}
    const auto* b=static_cast<const unsigned char*>(bytes);packets.push_back({peer,Bytes(b,b+count)});return true;
}
bool OtherSend(std::uint32_t peer,const void* bytes,std::size_t count) noexcept {return Send(peer,bytes,count);}
struct Native {
    SubSupportState states[3]{State(1),State(2),State(3)};
    unsigned count=3,applies=0,attempts=0,reads=0,enumerations=0;
    bool unavailable=false,failApply=false,mismatchRead=false;
};
unsigned Enumerate(void* user,SubSupportState* out,unsigned capacity) noexcept {
    auto& n=*static_cast<Native*>(user);++n.enumerations;
    for(unsigned i=0;i<n.count && i<capacity && i<3;++i)out[i]=n.states[i];
    return n.count;
}
bool Read(void* user,const unsigned char* id,SubSupportState* out) noexcept {
    auto& n=*static_cast<Native*>(user);++n.reads;
    if(n.unavailable)return false;
    for(unsigned i=0;i<n.count && i<3;++i)if(!std::memcmp(n.states[i].netId,id,32)) {
        *out=n.states[i];if(n.mismatchRead)out->netId[31]^=1;return true;
    }
    return false;
}
bool Apply(void* user,const SubSupportState& state) noexcept {
    auto& n=*static_cast<Native*>(user);++n.attempts;
    if(n.failApply)return false;
    for(unsigned i=0;i<n.count && i<3;++i)if(!std::memcmp(n.states[i].netId,state.netId,32)) {
        n.states[i]=state;++n.applies;return true;
    }
    return false;
}
ss::Hooks Hooks(Native& n) {return {&n,&Enumerate,&Read,&Apply};}
CommandNetworkContext Context(bool host=false,std::uint64_t epoch=77,unsigned peers=2) {
    return {true,host,epoch,host ? 0u : 1u,peers,host ? "host-local" : "guest-local",&Send};
}
bool Receive(ss::Protocol& p,const ss::Snapshot& m,std::uint32_t peer=1,const char* puid="host-auth") {
    const auto b=BytesOf(m);return p.Receive(peer,puid,b.data(),b.size(),123456);
}
void Codec() {
    for(unsigned count:{0u,1u,3u}) {
        const auto in=Message(count);const auto b=BytesOf(in);ss::Snapshot out{};
        Check(b.size()==32+count*124,"explicit pointer/padding-free wire size");
        Check(ss::Decode(b.data(),b.size(),out),"0/1/3 hull codec round trip");
        Check(out.epoch==in.epoch && out.sequence==in.sequence && out.count==count,"header round trip");
        for(unsigned i=0;i<count;++i)
            Check(out.rows[i].revision==in.rows[i].revision && ss::SameState(out.rows[i].state,in.rows[i].state),"every field round trips");
    }
    Check(ss::kMaxWireBytes==404 && ss::kMaxWireBytes<1024,"three hulls below transport limit");
    auto b=BytesOf(Message());
    for(std::size_t n=0;n<b.size();++n) {ss::Snapshot out{};Check(!ss::Decode(b.data(),n,out),"every truncation rejected");}
    auto extra=b;extra.push_back(0);Check(!Decodes(extra),"trailing bytes rejected");
    ss::Snapshot out{};Check(!ss::Decode(nullptr,b.size(),out),"null decode");
    Check(!ss::Owned(nullptr,4) && !ss::Owned(b.data(),3) && ss::Owned(b.data(),4),"owned framing only reads four bytes");
    auto invalid=Message(4);Check(!ss::Encode(invalid,b.data(),b.size()),"overfull native snapshot rejected");
    invalid=Message();invalid.rows[2].state=invalid.rows[0].state;
    Check(!ss::Encode(invalid,b.data(),b.size()),"duplicate native identities rejected");
    b=BytesOf(Message());Check(!ss::Encode(Message(),b.data(),b.size()-1),"insufficient encode capacity");
    auto x=b;x[0]^=1;Bad(x,"unknown magic");x=b;x[4]=2;Bad(x,"unknown version");
    x=b;x[5]=2;Bad(x,"unknown message kind");x=b;Put(x,6,x.size()-1,2);Bad(x,"declared size mismatch");
    for(unsigned k=25;k<28;++k){x=b;x[k]=1;Bad(x,"reserved bytes nonzero");}
    x=b;x[24]=4;Bad(x,"four wire hulls");x=b;x[24]=2;Bad(x,"count/size mismatch");
    x=b;Put(x,8,0,8);Bad(x,"zero epoch");x=b;Put(x,16,0,8);Bad(x,"zero snapshot sequence");
    x=b;x.back()^=1;Check(!Decodes(x),"corruption checksum rejected");
    for(unsigned i=0;i<3;++i) {
        const std::size_t row=32+i*124;
        x=b;Put(x,row,0,8);Bad(x,"zero per-hull revision");
        x=b;std::memset(x.data()+row+8,0,32);Bad(x,"empty native ID");
        for(unsigned k=20;k<24;++k){x=b;x[row+8+k]=1;Bad(x,"noncanonical native ID padding");}
        for(unsigned k=0;k<4;++k) {
            for(std::uint32_t bits:{0x7fc00000u,0x7f800000u,0xff800000u,0xbf800000u,0x7f7fffffu}) {
                x=b;Put(x,row+40+k*4,bits);Bad(x,"nonfinite/negative/oversized wear");
            }
            x=b;Put(x,row+56+k*4,ss::kRepairMs+1);Bad(x,"repair bound");
            x=b;Put(x,row+72+k*4,ss::kReloadMs+1);Bad(x,"reload bound");
        }
        x=b;Put(x,row+88,ss::kMissileMs+1);Bad(x,"missile cooldown bound");
        x=b;Put(x,row+92,ss::kDroneMs+1);Bad(x,"drone cooldown bound");
        x=b;x[row+96]=16;Bad(x,"down mask bound");
        x=b;x[row+97]=8;Bad(x,"state flags bound");
        x=b;x[row+98]=5;Bad(x,"drone count bound");
        x=b;x[row+99]=4;Bad(x,"turret mask bound");
        x=b;x[row+96]=0;Bad(x,"repairing timer cannot describe an up part");
        x=b;x[row+99]=0;Bad(x,"absent turret vector must be zero");
        x=b;std::memset(x.data()+row+100,0,12);Bad(x,"present turret direction is normalized");
        for(unsigned t=0;t<2;++t)for(unsigned k=0;k<3;++k) {
            x=b;Put(x,row+100+t*12+k*4,0x7fc00000u);Bad(x,"all turret components finite");
            x=b;Put(x,row+100+t*12+k*4,0x40000000u);Bad(x,"all turret components bounded");
        }
    }
    x=b;std::memcpy(x.data()+32+2*124+8,x.data()+32+8,32);Bad(x,"wire duplicate IDs");
}
void AuthorityAndReplay() {
    Native n;ss::Protocol p(Hooks(n));p.Update(Context(),1000);auto m=Message();
    Check(!Receive(p,m,0) && !Receive(p,m,2) && !Receive(p,m,3),"only current host peer writes");
    Check(!Receive(p,m,1,nullptr) && !Receive(p,m,1,"") && !Receive(p,m,1,"bad puid"),"authentication PUID required");
    char unterminated[65];std::memset(unterminated,'x',sizeof(unterminated));
    Check(!Receive(p,m,1,unterminated),"unterminated PUID rejected");
    Check(n.applies==0,"unauthenticated messages never invoke apply");
    auto wrong=m;wrong.epoch=78;Check(!Receive(p,wrong),"wrong shared epoch rejected");
    Check(Receive(p,m) && n.applies==3,"valid host snapshot applies all registered hulls");
    Check(!Receive(p,m) && n.applies==3,"exact packet replay rejected");
    m.sequence=2;Check(Receive(p,m) && n.applies==3,"periodic same revisions do not write again");
    m.sequence=3;m.rows[0].revision=9;m.rows[0].state.wear[0]=1;
    m.rows[1].revision=13;m.rows[1].state.wear[0]=2;
    Check(Receive(p,m) && n.applies==4 && n.states[0].wear[0]==100.25f && n.states[1].wear[0]==2,
        "each hull has an independent monotonic revision");
    auto inconsistent=m;inconsistent.sequence=4;inconsistent.rows[1].state.wear[0]=3;
    inconsistent.rows[2].revision=14;inconsistent.rows[2].state.wear[0]=4;
    Check(!Receive(p,inconsistent) && n.applies==4,"equal revision cannot change state or partially write other hulls");
    m.sequence=4;m.rows[1].revision=14;m.rows[1].state.wear[0]=0;m.rows[1].state.downMask=0;
    m.rows[1].state.repairRemainingMs[0]=m.rows[1].state.repairRemainingMs[2]=0;
    Check(Receive(p,m) && n.applies==5 && n.states[1].wear[0]==0,"new revision allows real repair and lower wear");
    m.sequence=5;m.rows[1].revision=15;m.rows[1].state.wear[0]=20;
    Check(!Receive(p,m,1,"other-host") && n.applies==5,"host identity cannot change inside a context");
    Check(Receive(p,m) && n.applies==6,"rejected host identity did not consume sequence");
    Native hostNative;ss::Protocol host(Hooks(hostNative));packets.clear();host.Update(Context(true),0);
    Check(!Receive(host,m) && hostNative.applies==0,"host never applies remote subsystem writes");
    auto damaged=BytesOf(m);damaged[32+2*124+99]=4;Rehash(damaged);
    Check(!p.Receive(1,"host-auth",damaged.data(),damaged.size(),0) && n.applies==6,"bad final row cannot partially apply early rows");
}
void DeferredAndIdentity() {
    Native n;n.unavailable=true;ss::Protocol p(Hooks(n));p.Update(Context(),0);auto m=Message(1);
    Check(Receive(p,m) && !n.applies && !n.attempts,"unknown/not-yet-live hull is not created or applied");
    n.unavailable=false;m.sequence=2;
    Check(Receive(p,m) && n.applies==1,"same revision periodic snapshot succeeds after activation");
    n.failApply=true;m.sequence=3;m.rows[0].revision=11;m.rows[0].state.wear[0]=700;
    Check(Receive(p,m) && n.applies==1,"native apply failure not reported as state application");
    n.failApply=false;m.sequence=4;
    Check(Receive(p,m) && n.applies==2 && n.states[0].wear[0]==700,"native apply failure does not consume hull revision");
    m.sequence=5;m.rows[0].revision=12;m.rows[0].state.netId[31]^=1;
    Check(Receive(p,m) && n.applies==2,"full native ID required, including bytes beyond first twenty");
    m=Message(1,6);m.rows[0].revision=12;m.rows[0].state.wear[0]=900;n.mismatchRead=true;
    Check(Receive(p,m) && n.applies==2,"native read cannot substitute a different hull identity");
    n.mismatchRead=false;m.sequence=7;Check(Receive(p,m) && n.applies==3,"mismatched read did not consume revision");
    auto empty=Message(0,8);Check(Receive(p,empty),"empty authoritative snapshot accepted without object deletion");
    m.sequence=9;m.rows[0].revision=11;m.rows[0].state.wear[0]=5;
    Check(Receive(p,m) && n.applies==3,"snapshot omission does not clear the old hull replay watermark");
    m.sequence=std::numeric_limits<std::uint64_t>::max();m.rows[0].revision=std::numeric_limits<std::uint64_t>::max();
    Check(Receive(p,m) && n.applies==4,"maximum counters admitted without wrapping");
    m.sequence=1;m.rows[0].revision=1;Check(!Receive(p,m) && n.applies==4,"wrapped sequence cannot replay");
}
void ContextLifetime() {
    Native n;ss::Protocol p(Hooks(n));auto c=Context();p.Update(c,0);auto m=Message(1);
    Check(Receive(p,m) && n.applies==1,"initial context active");
    c.ready=false;p.Update(c,10);Check(!p.Ready() && p.Epoch()==0 && !Receive(p,m),"lost context clears epoch and rejects old packets");
    c.ready=true;c.epoch=88;p.Update(c,20);Check(!Receive(p,m),"old world epoch rejected after reconnect");
    m.epoch=88;Check(Receive(p,m,1,"new-host") && n.applies==2,"new context may begin from revision one and a new host identity");
    c.hostPeer=2;c.epoch=89;p.Update(c,30);m.epoch=89;
    Check(!Receive(p,m,1,"new-host") && Receive(p,m,2,"host-three"),"host peer change binds new transport authority");
    c.peerCount=1;p.Update(c,40);Check(!p.Ready(),"host outside roster invalidates context");
    c=Context();c.epoch=0;p.Update(c,50);Check(!p.Ready(),"zero shared epoch invalidates context");
    c=Context();c.send=nullptr;p.Update(c,60);Check(!p.Ready(),"missing transport invalidates context");
    c=Context();c.peerCount=1025;p.Update(c,70);Check(!p.Ready(),"oversized roster invalidates context");
    c=Context();c.localPuid="";p.Update(c,80);Check(!p.Ready(),"missing local identity invalidates context");
    char local[65]="guest-before";c=Context();c.localPuid=local;p.Update(c,90);
    m=Message(1);Check(Receive(p,m),"mutable context identity initially accepted");
    std::memcpy(local,"guest-after",sizeof("guest-after"));p.Update(c,100);
    Check(!p.Ready() && !Receive(p,m,1,"replacement-host"),"in-place identity change retires old epoch instead of reopening replay");
    c.epoch=78;p.Update(c,110);m.epoch=78;
    Check(Receive(p,m,1,"replacement-host"),"rebound context resumes only after new shared epoch");
    p.Reset();Check(!p.Ready() && p.Epoch()==0 && !Receive(p,m),"explicit reset clears complete protocol state");
    Check(n.states[0].wear[0]==m.rows[0].state.wear[0],"reset does not heal or otherwise mutate native deck systems");
}
void AsymmetricReset() {
    Native h,g;ss::Protocol host(Hooks(h)),guest(Hooks(g));auto hc=Context(true),gc=Context();
    packets.clear();host.Update(hc,0);guest.Update(gc,0);
    const auto old=packets.front().bytes;
    Check(guest.Receive(1,"host-auth",old.data(),old.size(),0),"asymmetric fixture establishes old wire watermark");
    hc.send=&OtherSend;packets.clear();host.Update(hc,100);
    Check(!host.Ready() && packets.empty(),"host backend rebind cannot restart sequence under same shared epoch");
    host.Update(hc,200);Check(!host.Ready() && packets.empty(),"retired epoch cannot silently recover on the next tick");
    hc.epoch=78;host.Update(hc,201);Check(host.Ready() && !packets.empty(),"host obtains new shared epoch before restart");
    const auto next=packets.front().bytes;
    Check(!guest.Receive(1,"host-auth",next.data(),next.size(),201),"old guest context cannot apply new epoch before negotiation");
    gc.epoch=78;guest.Update(gc,202);
    Check(guest.Receive(1,"host-auth",next.data(),next.size(),202),"both negotiated endpoints converge fresh counters");
    gc.ready=false;guest.Update(gc,203);gc.ready=true;guest.Update(gc,204);
    Check(!guest.Ready() && !guest.Receive(1,"host-auth",next.data(),next.size(),204),"guest-only ready loss cannot reopen old packet replay");
}
void HostAndTwoGuests() {
    Native hostNative,a,b;ss::Protocol host(Hooks(hostNative)),guestA(Hooks(a)),guestB(Hooks(b));
    guestA.Update(Context(),0);guestB.Update(Context(),0);auto c=Context(true);
    packets.clear();sends=failPeer=failRemaining=0;host.Update(c,1000);
    Check(packets.size()==2 && sends==2,"first update sends complete snapshot to both peers");
    for(const auto& p:packets) {
        auto& guest=p.peer==1 ? guestA : guestB;
        Check(guest.Receive(1,"host-auth",p.bytes.data(),p.bytes.size(),9),"two-endpoint production bytes apply");
    }
    Check(a.applies==3 && b.applies==3,"both guest worlds converge all three hulls");
    const auto first=packets[0].bytes;packets.clear();host.Update(c,1099);
    Check(packets.empty(),"unchanged snapshot waits through 99ms");
    host.Update(c,1100);Check(packets.size()==2,"unchanged snapshot sent at 100ms");
    ss::Snapshot initial{},periodic{};Check(ss::Decode(first.data(),first.size(),initial),"decode initial fixture packet");
    Check(ss::Decode(packets[0].bytes.data(),packets[0].bytes.size(),periodic) && periodic.sequence>initial.sequence &&
        periodic.rows[0].revision==initial.rows[0].revision,"periodic packet advances sequence without fictitious state revision");
    packets.clear();hostNative.states[1].wear[0]=875;host.Update(c,1101);
    Check(packets.size()==2,"native state change sends before 100ms");
    ss::Snapshot changed{};Check(ss::Decode(packets[0].bytes.data(),packets[0].bytes.size(),changed) &&
        changed.rows[0].revision==periodic.rows[0].revision && changed.rows[1].revision>periodic.rows[1].revision,
        "only changed hull receives a new revision");
    for(const auto& p:packets)Check((p.peer==1 ? guestA : guestB).Receive(1,"host-auth",p.bytes.data(),p.bytes.size(),99),"changed host snapshot applies");
    Check(a.states[1].wear[0]==875 && b.states[1].wear[0]==875,"state damage converges");
    packets.clear();failPeer=2;failRemaining=1;hostNative.states[0].wear[0]=777;host.Update(c,1102);
    Check(packets.size()==1 && packets[0].peer==1,"one peer can fail without claiming delivery");
    Check(guestA.Receive(1,"host-auth",packets[0].bytes.data(),packets[0].bytes.size(),100),"successful peer advances");
    packets.clear();host.Update(c,1103);Check(packets.size()==2,"failed peer retries full snapshot immediately");
    for(const auto& p:packets)Check((p.peer==1 ? guestA : guestB).Receive(1,"host-auth",p.bytes.data(),p.bytes.size(),101),"retry converges both peers");
    Check(a.states[0].wear[0]==777 && b.states[0].wear[0]==777,"partial send failure fully converges");
    packets.clear();failRemaining=1;hostNative.states[0].wear[0]=333;host.Update(c,1104);
    hostNative.states[0].wear[0]=222;packets.clear();host.Update(c,1105);
    for(const auto& p:packets)Check((p.peer==1 ? guestA : guestB).Receive(1,"host-auth",p.bytes.data(),p.bytes.size(),102),"changes during failed send send current state");
    Check(b.states[0].wear[0]==222,"failed peer receives newest damage, not saved obsolete state");
    packets.clear();host.Update(c,1);Check(packets.size()==2,"wall-clock rollback cannot suppress updates indefinitely");
    packets.clear();hostNative.count=4;host.Update(c,2000);Check(packets.empty(),"overfull native enumeration never silently truncates");
    hostNative.count=3;hostNative.states[2].wear[0]=std::numeric_limits<float>::infinity();host.Update(c,2001);
    Check(packets.empty(),"nonfinite native state is not broadcast");hostNative.states[2]=State(3);
    packets.clear();c.peerCount=0;host.Update(c,2002);Check(packets.empty(),"host with no peers does not send");
    c.peerCount=2;c.epoch=90;host.Update(c,2003);Check(packets.size()==2,"new support context sends full state immediately");
}
Native wrapperNative;
void ProductionWrapper() {
    crew::ResetSubSupportNetwork();packets.clear();auto context=Context();
    crew::UpdateSubSupportNetwork(context,0);auto bytes=BytesOf(Message(1));const auto before=wrapperNative.applies;
    Check(crew::ReceiveSubSupportNetwork(1,"host-auth",bytes.data(),bytes.size(),0) && wrapperNative.applies==before+1,
        "production wrapper invokes the exact native callback contract");
    bytes[4]=2;Check(crew::ReceiveSubSupportNetwork(1,"host-auth",bytes.data(),bytes.size(),0) && wrapperNative.applies==before+1,
        "owned malformed version consumed without native application");
    bytes[0]^=1;Check(!crew::ReceiveSubSupportNetwork(1,"host-auth",bytes.data(),bytes.size(),0),"other protocol magic left for demux");
    crew::ResetSubSupportNetwork();bytes=BytesOf(Message(1));
    Check(crew::ReceiveSubSupportNetwork(1,"host-auth",bytes.data(),bytes.size(),0) && wrapperNative.applies==before+1,
        "owned packet after reset cannot write old world state");
    crew::UpdateSubSupportNetwork(Context(true,78),1000);Check(packets.size()==2,"production wrapper enumerates native host hulls");
    crew::ResetSubSupportNetwork();
}
} // namespace
namespace crew {
unsigned EnumerateSubSupportStates(SubSupportState* out,unsigned count) noexcept {return Enumerate(&wrapperNative,out,count);}
bool ReadSubSupportState(const unsigned char* id,SubSupportState* out) noexcept {return Read(&wrapperNative,id,out);}
bool ApplySubSupportState(const SubSupportState& state) noexcept {return Apply(&wrapperNative,state);}
}
int main() {
    Codec();AuthorityAndReplay();DeferredAndIdentity();ContextLifetime();AsymmetricReset();HostAndTwoGuests();ProductionWrapper();
    std::printf("sub_support_net: %u checks passed (production codec, two guests, authority and lifecycle fixtures)\n",checks);return 0;
}
