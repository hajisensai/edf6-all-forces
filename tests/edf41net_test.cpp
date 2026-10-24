// The EDF4.1 classes' network wire (src/edf41net.h, the pure part): the header's checks, the per-object sender watermark, the
// float guard, the child identities' ranges. No game needed.
#include "../src/edf41net.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",what); std::exit(1); }
}
using namespace crew::edf41net;

Header Good(std::uint8_t kind,std::uint32_t sequence) {
    Header h;
    h.kind=kind;
    h.sender=0x1234567890ABCDEFull;
    h.sequence=sequence;
    return h;
}
}  // namespace

int main() {
    // ---- encode / decode ----
    unsigned char out[kMostMessage+8]{};
    std::size_t n=0;
    const unsigned char payload[56]={1,2,3,4,5};
    Check(Encode(Good(1,7),payload,sizeof(payload),out,sizeof(out),&n) && n==sizeof(Header)+sizeof(payload),"a message is its header and payload");
    Header h;
    const unsigned char* p=nullptr;
    Check(Decode(out,n,&h,&p) && h.kind==1 && h.sequence==7 && h.size==sizeof(payload) && p==out+sizeof(Header) && p[4]==5,"decoded as sent");
    Check(h.magic==kMagic && h.version==kVersion && h.reserved==0,"magic, version, reserved are the layer's");
    Check(!Decode(out,n-1,&h,&p) && !Decode(out,n+1,&h,&p),"the size must be the header's");
    {
        unsigned char bad[sizeof(out)];
        std::memcpy(bad,out,n);
        bad[0]^=1;
        Check(!Decode(bad,n,&h,&p),"another magic (another plugin's tag 12) refused");
        std::memcpy(bad,out,n);
        bad[4]=2;
        Check(!Decode(bad,n,&h,&p),"another version refused");
        std::memcpy(bad,out,n);
        bad[20]=1;
        Check(!Decode(bad,n,&h,&p),"a reserved byte set refused");
        std::memcpy(bad,out,n);
        std::memset(bad+8,0,8);
        Check(!Decode(bad,n,&h,&p),"sender 0 refused");
        std::memcpy(bad,out,n);
        std::memset(bad+16,0,4);
        Check(!Decode(bad,n,&h,&p),"sequence 0 refused");
        std::memcpy(bad,out,n);
        bad[6]=0;
        Check(!Decode(bad,n,&h,&p),"kind 0 refused");
    }
    Check(!Encode(Good(0,1),payload,4,out,sizeof(out),&n),"kind 0 is not sent");
    Check(!Encode(Good(1,0),payload,4,out,sizeof(out),&n),"sequence 0 is not sent");
    Check(!Encode(Good(1,1),payload,kMostPayload+1,out,sizeof(out),&n),"a payload past kMostPayload is not sent");
    Check(!Encode(Good(1,1),payload,40,out,sizeof(Header)+39,&n),"no room, nothing written");
    Check(Encode(Good(3,1),nullptr,0,out,sizeof(out),&n) && n==sizeof(Header) && Decode(out,n,&h,&p) && h.size==0,"an empty payload");
    Check(!Decode(nullptr,24,&h,&p) && !Decode(out,sizeof(Header)-1,&h,&p),"short or missing data");
    Check(kMostMessage<=0x800 && kMostMessage<0x5E0,"a message fits the game's block reader (0x800) and stream (0x5E0)");

    // ---- the watermark ----
    Gate g;
    Check(g.Admit(5,1) && !g.Admit(5,1),"a repeat is refused");
    Check(g.Admit(5,3) && !g.Admit(5,2),"an older message is refused");
    Check(g.Admit(6,100),"another sender has its own watermark");
    Check(!g.Admit(0,1) && !g.Admit(5,0),"sender or sequence 0 refused");
    Gate w;
    Check(w.Admit(9,0xFFFFFFF0u) && w.Admit(9,3),"the sender's wrap is taken (serial arithmetic)");
    Check(!w.Admit(9,0xFFFFFFF5u),"after the wrap the old numbers are old");
    Gate full;
    for(std::uint64_t s=1;s<=static_cast<std::uint64_t>(Gate::kSenders);++s)Check(full.Admit(s,1),"senders up to the bound");
    Check(!full.Admit(99,1),"a sender past the bound is refused, no watermark forgotten");
    Check(!full.Admit(1,1) && full.Admit(1,2),"the old watermarks still hold");

    // ---- floats ----
    const float fine[3]={1.0f,-2.0f,3.0e6f};
    const float inf[3]={1.0f,std::numeric_limits<float>::infinity(),0.0f};
    const float nan[1]={std::numeric_limits<float>::quiet_NaN()};
    const float huge[1]={2.0e7f};
    Check(Sane(fine,3) && !Sane(inf,3) && !Sane(nan,1) && !Sane(huge,1),"finite and within the world");

    // ---- child identities: one (index, sub) per child, in u16 ----
    Check(kChildPart!=kChildBattery && kChildPart<kChildSpawn && kChildSpawn+0xFF<kChildBattery && kChildBattery+0xFFF<kChildSpawn401,
          "the classes' index ranges do not overlap");
    Check(kTag!=13 && kTag!=14 && kTag!=15 && kTag>3 && kTag>=-14 && kTag<=15,
          "tag 12: not GameObjectBase's 0..3, coop's 13 / 14, the vehicles' 15; one byte in the game's small-int code");
    std::printf("edf41net_test: %d checks passed\n",checks);
    return 0;
}
