// The object message layer's pure part (src/object_net.h): the kinds' registry, the class table, the gates' ordering, and
// two kinds on one object (a seat pick and an EDF4.1 event, one sender, one sequence counter) kept apart: each reaches only
// its own receiver and each kind's gate orders only its own messages. The native side runs on the real EDF.dll in
// tests/drill_net_native_test.cpp and tests/edf41net_native_test.cpp (both kinds through the same chained slot there).
#include "../src/object_net.h"
#include "../src/seat_net.h"
#include "../src/edf41net.h"
#include "../src/drill_net.h"
#include "../src/proteus_net.h"
#include "../src/sazabi_net.h"
#include "../src/andryusha_net.h"
#include "../src/dreadnought_net.h"
#include "../src/seat_turret_net.h"
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
using namespace crew;

// Two kinds' receivers on one object, as payload.cpp's and edf41net.cpp's are.
struct Heard { int picks=0,events=0,refused=0; seat_net::Pick pick{}; std::uint8_t kind=0; };
Heard heard;
object_net::BasicGate<64> pickGate;   // payload.cpp: a seat's stream
edf41net::Gate eventGate;             // edf41net.cpp: the object's record
void PickReceived(unsigned char*,const void* block,std::size_t size) noexcept {
    seat_net::Pick p;
    if(!seat_net::Decode(block,size,p) || !pickGate.Admit(p.head,true,false,p.head.controller)){++heard.refused;return;}
    ++heard.picks;heard.pick=p;
}
void EventReceived(unsigned char*,const void* block,std::size_t size) noexcept {
    edf41net::Header h;const unsigned char* payload=nullptr;
    if(!edf41net::Decode(static_cast<const unsigned char*>(block),size,&h,&payload) ||
       !eventGate.AdmitSequence(h.head.sender,h.head.sequence)){++heard.refused;return;}
    ++heard.events;heard.kind=h.kind;
}
void OtherReceived(unsigned char*,const void*,std::size_t) noexcept {}

// What object_net.cpp's Dispatch does with a tag 15 block: its magic's receiver, or on to the next receiver (false).
template<class K> bool Route(const K& kinds,unsigned char* object,const void* block,std::size_t size) {
    const object_net::Handler fn=kinds.Find(object_net::MagicOf(block,size));
    if(!fn)return false;
    fn(object,block,size);
    return true;
}
// object_net::Stamp's head: one sender, one counter for every kind.
std::uint32_t counter=0;
void Stamp(object_net::Head& h) { h.sender=0x5EED;h.sequence=object_net::NextSequence(counter); }
}  // namespace

int main() {
    // ---- the kinds' registry ----
    {
        object_net::Kinds<3> k;
        Check(!k.Add(0,&PickReceived) && !k.Add(seat_net::kPickMagic,nullptr),"no magic or no receiver: refused");
        Check(k.Add(seat_net::kPickMagic,&PickReceived) && k.Add(seat_net::kPickMagic,&PickReceived),"the same receiver again is fine");
        Check(!k.Add(seat_net::kPickMagic,&EventReceived) && k.Find(seat_net::kPickMagic)==&PickReceived,
              "a magic is one file's: a second receiver for it refused, the first kept");
        Check(k.Add(edf41net::kMagic,&EventReceived) && k.Add(drill_net::kMagic,&OtherReceived) && k.Count()==3,"three kinds");
        Check(!k.Add(proteus_net::kMagic,&OtherReceived) && !k.Find(proteus_net::kMagic),"a full registry refuses, nothing evicted");
        Check(!k.Find(0) && !k.Find(0x12345678),"no magic, an unknown magic: no receiver (passed on)");
    }
    // Every kind the plugin registers has its own magic, and they fit the registry.
    const std::uint32_t magics[]={drill_net::kMagic,proteus_net::kMagic,sazabi_net::kMagic,seat_net::kPickMagic,edf41net::kMagic,
        andryusha_net::kMagic,dreadnought_net::kMagic,seat_turret_net::kMagic};
    constexpr std::size_t kinds=sizeof(magics)/sizeof(magics[0]);
    for(std::size_t i=0;i<kinds;++i) {
        Check(magics[i]!=0,"a kind's magic is never 0");
        for(std::size_t j=i+1;j<kinds;++j)Check(magics[i]!=magics[j],"every kind its own magic");
    }
    Check(kinds<=object_net::kMostKinds,"the plugin's kinds fit the registry");

    // ---- the class table ----
    Check(object_net::kClassCount==5,"five classes chained by name");
    for(std::size_t i=0;i<object_net::kClassCount;++i) {
        const auto& c=object_net::kClasses[i];
        Check(object_net::ClassOf(c.id)==&c && c.name && *c.name,"each class once, named");
        Check(c.stockReceive==object_net::kVehicleReceive || c.stockReceive==object_net::kObjectReceive,"a vehicle's or GameObjectBase's receiver");
        for(std::size_t j=i+1;j<object_net::kClassCount;++j)Check(object_net::kClasses[j].table!=c.table,"one row a table");
    }
    // The slots the kinds chained by hand before (docs/drill-re.md 8.5, docs/proteus-online.md, docs/sazabi-online.md;
    // edf41net's copies of 0x17D4E90 / 0x17C9F50).
    Check(object_net::SlotRva(*object_net::ClassOf(object_net::Class::drill505),object_net::kReceiveSlot)==0x17DB0D0,"505 slot 17");
    Check(object_net::SlotRva(*object_net::ClassOf(object_net::Class::bigBegaruta),object_net::kReceiveSlot)==0x17DEE98,"BigBegaruta slot 17");
    Check(object_net::SlotRva(*object_net::ClassOf(object_net::Class::v506),object_net::kReceiveSlot)==0x17DB4C8,"506 slot 17");
    Check(object_net::ClassOf(object_net::Class::eventAnimationObject)->table==0x17D4E90 &&
          object_net::ClassOf(object_net::Class::ufoCarrier508)->table==0x17C9F50,"the EDF4.1 classes' tables");
    Check(object_net::ClassOf(object_net::Class::eventAnimationObject)->stockReceive==0x54D770 &&
          object_net::ClassOf(object_net::Class::drill505)->stockReceive==0x6325B0,"GameObjectBase's and the vehicles' receivers");

    // ---- one tag for every kind ----
    Check(object_net::kTag==15 && drill_net::kTag==object_net::kTag && edf41net::kTag==object_net::kTag,
          "one tag (GameObjectBase 0..3, vehicles 4..5, coop 13 / 14 elsewhere)");

    // ---- the ordering alone (AdmitSequence) and through Admit ----
    {
        edf41net::Gate g;
        Check(g.AdmitSequence(5,1) && !g.AdmitSequence(5,1) && g.AdmitSequence(5,3) && !g.AdmitSequence(5,2),"repeats and older refused");
        Check(!g.AdmitSequence(0,1) && !g.AdmitSequence(5,0),"sender or sequence 0 refused");
        Check(g.AdmitSequence(6,0xFFFFFFF0u) && g.AdmitSequence(6,3) && !g.AdmitSequence(6,0xFFFFFFF5u),"across the wrap");
        object_net::BasicGate<2> a;object_net::Head h{1,1,7,4,-1};
        Check(!a.Admit(h,true,false,3) && a.Admit(h,true,false,-1) && !a.AdmitSequence(7,4),"Admit is the epoch then the same ordering");
    }

    // ---- two kinds on one object ----
    {
        object_net::Kinds<object_net::kMostKinds> k;
        Check(k.Add(seat_net::kPickMagic,&PickReceived) && k.Add(edf41net::kMagic,&EventReceived),"both kinds registered");
        unsigned char object[0x200]{};
        // Interleaved on one sender: pick 1, event 2, pick 3, event 4.
        seat_net::Pick p1;p1.holder=1;Stamp(p1.head);
        edf41net::Header e2;e2.kind=3;Stamp(e2.head);
        seat_net::Pick p3;p3.holder=2;Stamp(p3.head);
        edf41net::Header e4;e4.kind=5;Stamp(e4.head);
        Check(p1.head.sequence==1 && e2.head.sequence==2 && p3.head.sequence==3 && e4.head.sequence==4,"one counter for both kinds");
        unsigned char m2[edf41net::kMostMessage],m4[edf41net::kMostMessage];std::size_t n2=0,n4=0;
        const unsigned char body[8]={9,8,7};
        Check(edf41net::Encode(e2,body,sizeof(body),m2,sizeof(m2),&n2) && edf41net::Encode(e4,nullptr,0,m4,sizeof(m4),&n4),"events encoded");
        // The newer event first: the pick gate never saw it, the event gate's watermark is the event's own.
        Check(Route(k,object,m4,n4) && heard.events==1 && heard.kind==5 && heard.picks==0,"event 4 reaches only the event receiver");
        Check(Route(k,object,&p1,sizeof(p1)) && heard.picks==1 && heard.pick.holder==1 && heard.events==1,
              "pick 1 after event 4 is taken: another kind's newer sequence does not hold it back");
        Check(Route(k,object,m2,n2) && heard.events==1 && heard.refused==1,"event 2 after event 4: refused by the event gate alone");
        Check(Route(k,object,&p3,sizeof(p3)) && heard.picks==2 && heard.pick.holder==2,"pick 3: the pick gate's own order");
        Check(Route(k,object,&p1,sizeof(p1)) && heard.picks==2 && heard.refused==2,"pick 1 replayed: refused by the pick gate");
        // A block of one kind is never the other's: each receiver decodes only its own layout.
        seat_net::Pick d;edf41net::Header eh;const unsigned char* payload=nullptr;
        Check(!seat_net::Decode(m4,sizeof(seat_net::Pick),d) && !edf41net::Decode(reinterpret_cast<const unsigned char*>(&p3),sizeof(p3),&eh,&payload),
              "a pick does not decode as an event, an event not as a pick");
        // A kind nobody registered goes on to the next receiver, untouched.
        drill_net::State drill;drill.sender=1;drill.sequence=1;
        const Heard before=heard;
        Check(!Route(k,object,&drill,sizeof(drill)) && heard.picks==before.picks && heard.events==before.events &&
              heard.refused==before.refused,"an unregistered kind (the drill's on a non-drill) passes on");
    }
    std::printf("object_net_test: %d checks passed\n",checks);
    return 0;
}
