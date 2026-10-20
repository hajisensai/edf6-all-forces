// The vehicles' tag 15 kinds (src/seat_net.h): one head, one gate, the seat pick's format; the drill, Proteus and Sazabi
// blocks keep their old wire layout under it (an older peer reads them as before).
#include "../src/seat_net.h"
#include "../src/drill_net.h"
#include "../src/proteus_net.h"
#include "../src/sazabi_net.h"
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
}
int main() {
    using namespace crew;
    using seat_net::Head;using seat_net::Pick;
    // Every kind's block starts with the common head at the same offsets (drill, Proteus, Sazabi: their old layouts).
    Check(offsetof(drill_net::State,sender)==offsetof(Head,sender) && offsetof(drill_net::State,controller)==offsetof(Head,controller) &&
          offsetof(proteus_net::State,sequence)==offsetof(Head,sequence) && offsetof(sazabi_net::State,controller)==offsetof(Head,controller),
          "the kinds share the head's offsets");
    Check(sizeof(drill_net::State)==96 && sizeof(proteus_net::State)==136 && sizeof(sazabi_net::State)==888,
          "the old kinds' wire sizes are unchanged (older peers decode them)");
    const std::uint32_t magics[]={drill_net::kMagic,proteus_net::kMagic,sazabi_net::kMagic,seat_net::kPickMagic};
    for(int i=0;i<4;++i)for(int j=i+1;j<4;++j)Check(magics[i]!=magics[j],"every kind its own magic");
    Check(drill_net::kTag==seat_net::kTag && proteus_net::kTag==seat_net::kTag && sazabi_net::kTag==seat_net::kTag && seat_net::kTag==15,
          "one tag (coop keeps 13 and 14)");
    drill_net::State drill;drill.sender=3;drill.sequence=7;drill.controller=42;
    Check(seat_net::MagicOf(&drill,sizeof(drill))==drill_net::kMagic && seat_net::MagicOf(&drill,3)==0,"the magic picks the kind");
    Head h{};
    Check(seat_net::ReadHead(&drill,sizeof(drill),&h) && h.sender==3 && h.sequence==7 && h.controller==42,"the head of a drill block");
    Check(!seat_net::ReadHead(&drill,sizeof(Head)-1,&h),"a block shorter than a head has none");

    // The seat pick.
    Pick p;p.head.sender=17;p.head.sequence=1;p.head.controller=42;p.seat=1;p.holder=3;p.flags=seat_net::kNpc;
    Pick d{};
    Check(seat_net::Decode(&p,sizeof(p),d) && d.seat==1 && d.holder==3 && d.flags==seat_net::kNpc && d.head.controller==42,
          "a pick round trip");
    Check(!seat_net::Decode(&p,sizeof(p)-1,d) && !seat_net::Decode(&p,sizeof(p)+1,d),"a pick of another size");
    Pick bad=p;bad.head.version=2;Check(!seat_net::Valid(bad),"another version");
    bad=p;bad.head.magic=drill_net::kMagic;Check(!seat_net::Valid(bad),"another kind");
    bad=p;bad.seat=seat_net::kMostSeats;Check(!seat_net::Valid(bad),"a seat out of range");
    bad=p;bad.holder=-2;Check(!seat_net::Valid(bad),"a holder below none");
    bad=p;bad.holder=static_cast<std::int32_t>(seat_net::kMostHolders);Check(!seat_net::Valid(bad),"a holder out of range");
    bad=p;bad.flags=2;Check(!seat_net::Valid(bad),"unknown flags");
    bad=p;bad.reserved=1;Check(!seat_net::Valid(bad),"nonzero reserved bytes");
    bad=p;bad.head.sequence=0;Check(!seat_net::Valid(bad),"sequence 0 is never sent");
    bad=p;bad.head.sender=0;Check(!seat_net::Valid(bad),"no sender");
    bad=p;bad.head.controller=-2;Check(!seat_net::Valid(bad),"an epoch below the host's");
    Pick none=p;none.holder=-1;Check(seat_net::Valid(none),"none picked is a pick");

    // The gate: duplicates, reorder, epochs, wrap, the authority's own messages, a full room.
    seat_net::BasicGate<4> g;
    Check(!g.Admit(p.head,false,false,42),"offline: nothing admitted");
    Check(!g.Admit(p.head,true,true,42),"the machine running the seat ignores picks for it");
    Check(!g.Admit(p.head,true,false,7),"another epoch (a rider who left) is refused");
    Check(g.Admit(p.head,true,false,42),"a new sender's first pick");
    Check(!g.Admit(p.head,true,false,42),"a duplicate");
    Head later=p.head;later.sequence=3;Check(g.Admit(later,true,false,42),"a newer one");
    Head older=p.head;older.sequence=2;Check(!g.Admit(older,true,false,42),"an older one after it");
    Head wrap=p.head;wrap.sender=99;wrap.sequence=0xFFFFFFFFu;Check(g.Admit(wrap,true,false,42),"before the wrap");
    wrap.sequence=1;Check(g.Admit(wrap,true,false,42),"across the wrap");
    for(std::uint64_t s=200;s<202;++s){Head x=p.head;x.sender=s;Check(g.Admit(x,true,false,42),"room for four senders");}
    Head fifth=p.head;fifth.sender=500;Check(!g.Admit(fifth,true,false,42),"a full gate evicts no watermark");
    later.sequence=4;Check(g.Admit(later,true,false,42),"a known sender still passes when full");
    std::uint32_t counter=0xFFFFFFFEu;
    Check(seat_net::NextSequence(counter)==0xFFFFFFFFu && seat_net::NextSequence(counter)==1,"the sender's sequence skips 0");
    // The old kinds' gates still order by their own head.
    drill_net::Gate dg;drill.phase=drill_net::Phase::home;
    Check(dg.Admit(drill,true,false,42) && !dg.Admit(drill,true,false,42),"the drill's gate over the common one");

    // When a pick goes out.
    Check(seat_net::PickDue(false,1000,0) && seat_net::PickDue(true,1001,1000) && !seat_net::PickDue(false,1001,1000) &&
          seat_net::PickDue(false,1000+seat_net::kPickRefreshMs,1000),"at once on a change, else every refresh");
    Check(seat_net::kPickStaleMs>2*seat_net::kPickRefreshMs,"a receiver keeps a pick across a lost refresh or two");
    Check(!seat_net::Replicated(false,2) && !seat_net::Replicated(true,0) && seat_net::Replicated(true,1),
          "only a registered copy in a session sends");
    std::printf("seat_net_test: %d checks passed\n",checks);
}
