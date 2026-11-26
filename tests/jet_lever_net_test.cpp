// src/jet_lever_protocol.h offline: a jet's lever in one byte (its afterburner kept), when a state goes out (a move past
// kStep, the afterburner or a flag at once, the heartbeat), when one heard stands, and the AFJL message.
#include "../src/jet_lever_protocol.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
}  // namespace

int main() {
    using namespace crew::jetlever;
    // The byte.
    Check(Quantize(0.0f)==0 && Quantize(-1.0f)==0 && Quantize(1.0f)==255 && Quantize(2.0f)==255,"clamped to 0..255");
    Check(Quantize(std::numeric_limits<float>::quiet_NaN())==0 && Quantize(std::numeric_limits<float>::infinity())==0,
          "not finite: 0");
    Check(Quantize(crew::jet::wing::kCruiseThrottle)==140,"cruise 0.55 is 140");
    for(int i=0;i<=1000;++i) {
        const float l=static_cast<float>(i)/1000.0f;
        const std::uint8_t q=Quantize(l);
        if(Burner(l)!=Burner(Lever(q))){std::fprintf(stderr,"lever %.3f byte %u\n",l,q);Check(false,"the afterburner kept by its byte");}
        const float back=Lever(q),off=back>l ? back-l : l-back;
        if(off>1.5f/255.0f){std::fprintf(stderr,"lever %.3f byte %u\n",l,q);Check(false,"within a step and a half of 255");}
    }
    Check(!Burner(Lever(Quantize(0.95f))) && Burner(Lever(Quantize(0.9501f))) && Burner(Lever(Quantize(0.951f))),
          "the threshold sits where the lever's is");

    // Pacing.
    Pace p{};
    Check(Due(p,100,0,5000),"never sent: due");
    Sent(p,100,0,5000);
    Check(!Due(p,100,0,5050) && !Due(p,100,0,5999),"still: none before the heartbeat");
    Check(Due(p,100,0,6000),"the heartbeat");
    Check(!Due(p,100+kStep-1,0,5500),"a move under kStep waits");
    Check(!Due(p,100+kStep,0,5050),"a move of kStep inside kGapMs waits");
    Check(Due(p,100+kStep,0,5100) && Due(p,100-kStep,0,5100),"a move of kStep after kGapMs goes");
    Check(Due(p,100,kParked,5001) && Due(p,100,kOff,5001),"a flag changed: at once");
    Sent(p,241,0,7000);
    Check(Lever(241)<=0.95f && Lever(243)>0.95f,"241 cold, 243 lit");
    Check(Due(p,243,0,7001),"the afterburner lit: at once, under kStep and kGapMs");
    Sent(p,243,0,7001);
    Check(Due(p,241,0,7002),"and out: at once");
    // A whole flight at 60 Hz: the lever swept up in 0.7 s and held 10 s sends a few a second while it moves, one a
    // second still.
    {
        Pace f{};int sent=0,sentStill=0;
        for(int frame=0;frame<60*11;++frame) {
            const std::uint64_t ms=100000+static_cast<std::uint64_t>(frame)*1000/60;
            const float l=frame<42 ? 0.3f+0.7f*static_cast<float>(frame)/42.0f : 1.0f;
            const std::uint8_t q=Quantize(l);
            if(Due(f,q,0,ms)){Sent(f,q,0,ms);++sent;if(frame>=42)++sentStill;}
        }
        std::printf("sweep and hold: %d states (%d held)\n",sent,sentStill);
        Check(sent<=20,"a sweep and a hold: at most 20 states in 11 s");
        Check(sentStill>=9 && sentStill<=11,"held: one a second");
        Check(f.lever==255,"the last one sent is the lever held");
    }

    // Freshness.
    Check(!Fresh(0,100),"never heard");
    Check(Fresh(1000,1000) && Fresh(1000,1000+kSilentMs-1) && !Fresh(1000,1000+kSilentMs),"heard until kSilentMs");
    Check(!Fresh(2000,1000),"from a clock past ours: not trusted");

    // The message.
    Msg m{};
    for(int i=0;i<20;++i)m.jet[i]=static_cast<unsigned char>(i*7+1);
    m.jet[28]=9;
    m.lever=Quantize(0.8f);m.flags=kRotor|kParked;
    unsigned char b[kWire];
    Check(Encode(m,b,sizeof(b)),"a lever encodes");
    Check(Owned(b,sizeof(b)) && TakesState(b,sizeof(b)),"the channel's, a state");
    Msg d{};
    Check(Decode(b,sizeof(b),d),"and decodes");
    Check(!std::memcmp(d.jet,m.jet,32) && d.lever==m.lever && d.flags==m.flags,"as it was");
    for(std::size_t i=crew::jetlever::detail::kUsed;i<kWire;++i)Check(b[i]==0,"zero past what is used");
    Check(!Decode(b,sizeof(b)-1,d),"the wrong size");
    {unsigned char c[kWire];std::memcpy(c,b,kWire);c[4]=static_cast<unsigned char>(kVersion+1);Check(!Decode(c,kWire,d),"another version");}
    {unsigned char c[kWire];std::memcpy(c,b,kWire);c[0]^=1;Check(!Owned(c,kWire) && !Decode(c,kWire,d),"another magic");}
    {unsigned char c[kWire];std::memcpy(c,b,kWire);c[8+32+1]=8;Check(!Decode(c,kWire,d),"flags not ours");}
    {unsigned char c[kWire];std::memcpy(c,b,kWire);c[8+21]=1;Check(!Decode(c,kWire,d),"a jet not a NetId (bytes 20..23)");}
    {Msg x=m;std::memset(x.jet,0,32);Check(!Encode(x,b,sizeof(b)),"no jet");
     crew::netref::MakePlayer(3,x.jet);Check(!Encode(x,b,sizeof(b)),"a player is no jet");
     x=m;x.flags=0x80;Check(!Encode(x,b,sizeof(b)),"flags not ours: not sent");}
    Check(kWire<1024,"one payload");
    std::printf("jet_lever_net: %d checks ok\n",checks);
    return 0;
}
