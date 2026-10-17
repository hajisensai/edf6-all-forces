// The hit layer of a ported 4.1 part (src/edf41_hitlayer.h): on a layer the area damage's query (layer 13) meets. No game needed.
#include "../src/edf41_hitlayer.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",what); std::exit(1); }
}
using namespace crew::edf41layer;
}  // namespace

int main() {
    Check(LayerOf(0x11u)==0x11,"layer: the low 5 bits");
    Check(LayerOf((5u<<15)|(3u<<10)|(2u<<5)|7u)==7,"layer: system group / subsystem bits ignored");

    std::uint32_t t[kLayers];
    for(std::uint32_t& w:t)w=0xFFFFFFFFu;
    Check(Sees(t,13,0x11),"all-on table: everything met");
    t[13]&=~(1u<<0x11);
    Check(!Sees(t,13,0x11) && Sees(t,13,7),"layer 13 without 0x11");
    Check(!Sees(t,-1,7) && !Sees(t,13,32) && !Sees(nullptr,13,7),"out of range / no table");

    // the query does not meet EventAnimationObject's 0x11, it meets the hull's 7: the part moves to 7
    Check(HitLayer(0x11,7,13,t)==7,"0x11 not met, the hull's 7 is: 7");
    // already met: left alone
    for(std::uint32_t& w:t)w=0xFFFFFFFFu;
    Check(HitLayer(0x11,7,13,t)==0x11,"0x11 met: unchanged");
    // neither met: unchanged (logged by the caller)
    t[13]=0;
    Check(HitLayer(0x11,7,13,t)==0x11,"nothing met: unchanged");
    // no table found: the hull's layer (the run's evidence)
    Check(HitLayer(0x11,7,13,nullptr)==7,"no table: the hull's");
    Check(HitLayer(7,7,13,nullptr)==7,"already the hull's");
    Check(HitLayer(-1,7,13,nullptr)==-1 && HitLayer(0x11,-1,13,nullptr)==0x11,"unknown layers: unchanged");

    std::printf("edf41_hitlayer: %d checks passed\n",checks);
    return 0;
}
