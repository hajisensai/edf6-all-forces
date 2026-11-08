// The hit layer of a ported 4.1 part (src/edf41_hitlayer.h): on a layer the area damage's query (layer 13) meets. No game needed.
#include "../src/edf41_hitlayer.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

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

    // ---- the Earth Eater's parts: 4.1's layers carried over by the two filter tables (docs/edf41-ufomother401.md 2.7) ----
    const auto row=[](const LayerTable& t,int a) {
        std::uint32_t m=0;
        for(int b=1;b<kLayers;++b)if(Sees(t.row,a,b))m|=1u<<b;
        return m;
    };
    const auto bits=[](std::initializer_list<int> ls) { std::uint32_t m=0; for(int l:ls)m|=1u<<l; return m; };
    // the EDF6 rows docs/raycast-re.md 3 and docs/gravity-re.md read off 0x105510
    Check(row(kEdf6Table,kEdf6MapRay)==bits({15,16,17,18,19,20,26}),"EDF6 map ray 22 meets 15-20 and 26");
    Check(row(kEdf6Table,kEdf6AreaQuery)==bits({5,6,7,8,9,14,15,16,19,20}),"EDF6 area query 13 meets 5-9, 14, 15, 16, 19, 20");
    Check(Sees(kEdf6Table.row,kEdf6MapRay,0x11) && !Sees(kEdf6Table.row,kEdf6AreaQuery,0x11),
          "EventAnimationObject's 0x11: the map ray meets it, the area damage does not (rockets never reached the parts)");
    // 4.1: its map ray (every castRay site's 0x13) meets the panels' 13, not the core's 7 or the batteries' 6
    Check(Sees(kEdf41Table.row,kEdf41MapRay,k41PanelLayer) && !Sees(kEdf41Table.row,kEdf41MapRay,k41KeyframeLayer) &&
          !Sees(kEdf41Table.row,kEdf41MapRay,k41BatteryLayer),"4.1 map ray 19: meets 13, not 7 / 6");
    Check(row(kEdf41Table,k41PanelLayer)==bits({1,2,3,6,7,8,9,11,18,19,21,25,26}),"4.1 layer 13's row (EDF41.exe 0xEBC70)");
    // the body layers 1..8 meet each other alike in both games (what lets them stand by number)
    for(int a=kBodyLayerFirst;a<=kBodyLayerLast;++a)
        for(int b=kBodyLayerFirst;b<=kBodyLayerLast;++b)
            Check(Sees(kEdf6Table.row,a,b)==Sees(kEdf41Table.row,a,b),"body layers 1..8 alike");
    // the choice
    Check(Part41Layer(false,false)==13 && Part41Layer(false,true)==7 && Part41Layer(true,false)==6 && Part41Layer(true,true)==6,
          "4.1: panels 13, keyframe panels (the core) 7, batteries 6");
    Check(Edf6For41(k41PanelLayer)==15,"4.1 13 -> EDF6 15");
    Check(Edf6For41(k41KeyframeLayer)==7,"4.1 7 -> EDF6 7");
    Check(Edf6For41(k41BatteryLayer)==6,"4.1 6 -> EDF6 6");
    Check(Edf6For41(0)==-1 && Edf6For41(40)==-1,"no layer for nonsense");
    for(const int l41:{k41PanelLayer,k41KeyframeLayer,k41BatteryLayer}) {
        const int l6=Edf6For41(l41);
        Check(l6!=0x11,"no 401 part stays on EventAnimationObject's 0x11");
        Check(Sees(kEdf6Table.row,kEdf6AreaQuery,l6),"every 401 part is met by the area damage");
        Check(Sees(kEdf6Table.row,kEdf6MapRay,l6)==Sees(kEdf41Table.row,kEdf41MapRay,l41),"the map ray meets a part iff 4.1's does");
        for(int c=kBodyLayerFirst;c<=kBodyLayerLast;++c)
            Check(Sees(kEdf6Table.row,l6,c)==Sees(kEdf41Table.row,l41,c),"a part meets the body layers as in 4.1");
    }
    // the live table must agree with the constructor's on the rows the choice rests on, or nothing is moved
    Check(LiveAgrees(kEdf6Table.row,15),"live = constructor's: agrees");
    {
        LayerTable live=kEdf6Table;
        live.row[kEdf6AreaQuery]&=~(1u<<15);
        Check(!LiveAgrees(live.row,15),"a different area query row: disagrees");
        live=kEdf6Table;
        live.row[15]^=1u<<4;
        Check(!LiveAgrees(live.row,15) && LiveAgrees(live.row,7),"a different target row: disagrees for that target only");
        Check(!LiveAgrees(nullptr,15),"no live table: disagrees");
    }

    std::printf("edf41_hitlayer: %d checks passed\n",checks);
    return 0;
}
