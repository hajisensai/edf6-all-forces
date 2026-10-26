// src/barrage_net_protocol.h offline: a barrage's message round trips, and what is no barrage's is refused.
#include "../src/barrage_net_protocol.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
}  // namespace

int main() {
    using namespace crew::barrage_net;
    Msg m;
    m.kind=4;m.origin=0x1234ABCDu;m.serial=7;m.damage=850.0f;
    m.at[0]=100.5f;m.at[1]=-3.0f;m.at[2]=2000.0f;
    m.heading[0]=0.6f;m.heading[2]=-0.8f;
    for(int i=0;i<32;++i)m.caller[i]=static_cast<unsigned char>(i<20 ? i+1 : i>=24 ? 0x40+i : 0);
    unsigned char b[kWire];
    Check(Encode(m,b,sizeof(b)),"a barrage encodes");
    Msg d;
    Check(Decode(b,sizeof(b),d),"and decodes");
    Check(d.kind==m.kind && d.origin==m.origin && d.serial==m.serial && d.damage==m.damage &&
          !std::memcmp(d.at,m.at,12) && !std::memcmp(d.heading,m.heading,12) && !std::memcmp(d.caller,m.caller,32),"as it was");
    Check(Owned(b,sizeof(b)),"the channel's");
    for(std::size_t i=crew::barrage_net::detail::kUsed;i<kWire;++i)Check(b[i]==0,"zero past what is used");
    Check(!Decode(b,sizeof(b)-1,d),"the wrong size");
    {unsigned char c[kWire];std::memcpy(c,b,kWire);c[4]=2;Check(!Decode(c,kWire,d),"another version");}
    {unsigned char c[kWire];std::memcpy(c,b,kWire);c[8]=static_cast<unsigned char>(crew::barrage::kKinds);Check(!Decode(c,kWire,d),"an unknown kind");}
    {unsigned char c[kWire];std::memcpy(c,b,kWire);c[0]^=1;Check(!Decode(c,kWire,d) && !Owned(c,kWire),"not BRGS");}
    {Msg x=m;x.heading[0]=0;x.heading[2]=0;Check(!Encode(x,b,sizeof(b)),"no heading: no barrage");}
    {Msg x=m;x.serial=0;Check(!Encode(x,b,sizeof(b)),"no serial");}
    {Msg x=m;x.origin=0;Check(!Encode(x,b,sizeof(b)),"no origin");}
    {Msg x=m;x.damage=-1;Check(!Encode(x,b,sizeof(b)),"a negative damage");}
    {Msg x=m;std::memset(x.caller,0,32);Check(!Encode(x,b,sizeof(b)),"no caller");}
    {Msg x=m;x.caller[21]=1;Check(!Encode(x,b,sizeof(b)),"a caller not canonical");}
    {Msg x=m;x.at[0]=NAN;Check(!Encode(x,b,sizeof(b)),"a point that is no number");}
    {Encode(m,b,sizeof(b));
     float bad=5.0f;std::memcpy(b+crew::barrage_net::detail::kHeading,&bad,4);Check(!Decode(b,sizeof(b),d),"a heading past a unit's");}
    // A message heard twice starts one barrage.
    Heard h;
    Check(h.First(1,1) && !h.First(1,1) && h.First(1,2) && h.First(2,1),"each key once");
    for(std::uint32_t i=0;i<Heard::kMost;++i)h.First(9,100+i);
    Check(h.First(1,1),"the oldest forgotten past kMost");
    std::printf("barrage net: %d checks passed\n",checks);
    return 0;
}
