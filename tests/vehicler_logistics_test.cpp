// src/vehicler_logistics_logic.h / _net.h offline (docs/newclass-re/vehicler.md §5 M4): a kill's worth and whose it is, the
// host's book (one book a killed object, the gauge's cap, Hold / Settle / Refund balanced, a second hold refused, a new
// mission clears it), the ZWLG codec (round trips; a wrong size, magic, version, kind or field refused), a guest's mirror.
#include "../src/vehicler_logistics_net.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
using namespace crew::zwlog;
// A native reference (net_object_ref.h): a NetId with bytes 20..23 zero.
void Native(unsigned char out[32],unsigned char n) {
    std::memset(out,0,32);
    out[0]=n;out[1]=0x5A;out[28]=0x11;
}
}  // namespace

int main() {
    // ---- a kill's worth ----
    Check(Credit(VictimInfo{true,false,0.0f})==1,"an enemy's body: 1 point");
    Check(Credit(VictimInfo{true,true,0.0f})==0,"a part of one: none");
    Check(Credit(VictimInfo{false,false,0.0f})==0,"anything not an enemy: none");
    Check(Credit(VictimInfo{true,false,40.0f})==1,"no tiers by size yet");

    // ---- whose kill ----
    {
        Shooter s;s.self=3;
        Check(Attribute(s)==3,"a Vehicler player on foot: his own");
        s.self=-1;
        Check(Attribute(s)==-1,"an NPC soldier / another class's player: nobody's");
        Shooter v;v.vehicle=true;v.seats=3;v.rider[0]=5;v.rider[1]=-1;v.rider[2]=7;
        v.weaponSeat=-1;
        Check(Attribute(v)==5,"a vehicle, the weapon not known: the driver");
        v.weaponSeat=2;
        Check(Attribute(v)==7,"the gunner seat's weapon: that gunner");
        v.weaponSeat=1;
        Check(Attribute(v)==-1,"a seat an NPC holds: nobody's (not the driver's)");
        v.weaponSeat=9;
        Check(Attribute(v)==5,"a seat past the vehicle's (no seat holds it): the driver");
        Shooter npc;npc.vehicle=true;npc.seats=2;npc.rider[0]=-1;npc.rider[1]=-1;
        Check(Attribute(npc)==-1,"an NPC vehicle: nobody's");
        Shooter none;none.vehicle=true;
        Check(Attribute(none)==-1,"a vehicle with no seats read: nobody's");
        Shooter bad;bad.self=kSlotsMost;
        Check(Attribute(bad)==-1,"a slot past the mission's: nobody's");
    }

    // ---- the book ----
    Ledger l;l.Start(40);
    unsigned char a[32],b[32],c[32];Native(a,1);Native(b,2);Native(c,3);
    unsigned added=9;
    Check(l.Book(a,4,1,&added)==Booked::kept && added==1 && l.Points(4)==1,"a kill: 1 point for its player");
    Check(l.Book(a,4,1,&added)==Booked::seen && added==0 && l.Points(4)==1,"the same victim again: booked once");
    Check(l.Book(a,6,1)==Booked::seen && l.Points(6)==0,"the same victim for another player (two reports): once");
    Check(l.Book(b,4,0)==Booked::nothing && l.Book(b,-1,1)==Booked::nothing && l.Book(b,kSlotsMost,1)==Booked::nothing,
          "no points / no player: nothing (and the victim not taken)");
    Check(l.Book(b,4,1)==Booked::kept && l.Points(4)==2,"that victim still books once it is a kill");
    Check(l.Book(nullptr,4,1)==Booked::kept && l.Book(nullptr,4,1)==Booked::kept && l.Points(4)==4,
          "a kill with no reference: booked as it comes (one hook call a death)");
    for(int i=0;i<60;++i){unsigned char v[32];Native(v,static_cast<unsigned char>(10+i));l.Book(v,4,1);}
    Check(l.Points(4)==40,"the gauge caps it");
    Check(l.Book(c,4,1)==Booked::capped && l.Points(4)==40,"past the cap: nothing added");
    Check(l.Book(c,4,1)==Booked::seen,"a capped kill is still booked (not credited later)");
    // A row a player, as many as kRowsMost.
    {
        Ledger m;m.Start(40);
        for(int i=0;i<kRowsMost;++i)Check(m.Book(nullptr,100+i,1)==Booked::kept,"a row each player");
        Check(m.Book(nullptr,999,1)==Booked::full,"no row left for one more");
        Check(m.Book(nullptr,100,1)==Booked::kept && m.Points(100)==2,"an existing row still books");
    }

    // ---- Hold / Settle / Refund ----
    std::uint64_t t1=0,t2=0;
    Check(!l.Hold(9,20,&t1) && !t1,"no row: not held");
    Check(l.Hold(4,20,&t1) && t1 && l.Points(4)==20 && l.Find(4)->held==20,"held: taken from the points, kept apart");
    Check(!l.Hold(4,20,&t2) && !t2 && l.Points(4)==20,"a second call while one is held: refused, nothing taken");
    Check(l.Book(c,4,1)==Booked::seen,"(c was booked)");
    {unsigned char v[32];Native(v,200);Check(l.Book(v,4,1)==Booked::capped,"points + held at the gauge: a kill adds nothing");}
    Check(l.Refund(t1) && l.Points(4)==40 && l.Find(4)->held==0,"refunded: all back, within the gauge");
    Check(!l.Refund(t1) && !l.Settle(t1) && l.Points(4)==40,"a ticket closes once");
    Check(l.Hold(4,40,&t2) && t2!=t1 && l.Points(4)==0,"held again (a new ticket)");
    Check(l.Settle(t2) && l.Points(4)==0 && l.Find(4)->held==0,"settled: spent");
    Check(!l.Hold(4,1,&t1),"too few points: not held");
    Check(!l.Hold(4,0,&t1),"a price of nothing: not held");
    {
        Ledger m;m.Start(40);m.Book(nullptr,1,8);m.Book(nullptr,1,8);m.Book(nullptr,1,8);
        Check(!m.Hold(1,41,&t1),"a price past the gauge: not held");
        const unsigned before=m.Points(1);
        std::uint64_t t=0;
        for(int i=0;i<5;++i){Check(m.Hold(1,4,&t),"hold");if(i%2)m.Settle(t);else m.Refund(t);}
        Check(m.Points(1)==before-8 && m.Find(1)->held==0,"three refunds and two settles of 4: 8 spent, nothing left held");
    }
    // The gauge lowered: points brought within it, a held call kept.
    {
        Ledger m;m.Start(40);for(int i=0;i<5;++i)m.Book(nullptr,2,8);
        std::uint64_t t=0;m.Hold(2,20,&t);
        Check(m.Cap(30) && m.Points(2)==10 && m.Find(2)->held==20,"capped: the points, the held kept");
        Check(m.Refund(t) && m.Points(2)==30,"the refund fits the new gauge");
    }
    // A new mission: every row and the victims seen dropped.
    l.Start(40);
    Check(l.Points(4)==0 && !l.Find(4),"a new mission: no row");
    Check(l.Book(a,4,1)==Booked::kept,"a new mission: a victim of the last one books again");
    Check(!l.Settle(t2) && !l.Refund(t2),"a ticket of the last mission: closed");

    // ---- the wire ----
    {
        Msg m;m.kind=Kind::credit;Native(m.victim,7);crew::netref::MakePlayer(3,m.player);m.points=1;
        unsigned char bytes[kCreditWire];
        Check(Encode(m,bytes,sizeof(bytes)),"a credit encodes");
        Msg o;
        Check(Decode(bytes,sizeof(bytes),o) && o.kind==Kind::credit && !std::memcmp(o.victim,m.victim,32) &&
              !std::memcmp(o.player,m.player,32) && o.points==1,"and decodes the same");
        Check(!Decode(bytes,sizeof(bytes)-1,o) && !Encode(m,bytes,sizeof(bytes)-1),"a wrong size: refused");
        unsigned char big[kStateWire]{};std::memcpy(big,bytes,sizeof(bytes));
        Check(!Decode(big,sizeof(big),o),"a credit at the state's size: refused");
        unsigned char bad[kCreditWire];
        std::memcpy(bad,bytes,sizeof(bad));bad[0]^=1;Check(!Decode(bad,sizeof(bad),o),"a wrong magic: refused");
        std::memcpy(bad,bytes,sizeof(bad));bad[4]=2;Check(!Decode(bad,sizeof(bad),o),"another version: refused");
        std::memcpy(bad,bytes,sizeof(bad));bad[8]=4;Check(!Decode(bad,sizeof(bad),o),"an unknown kind: refused");
        std::memcpy(bad,bytes,sizeof(bad));bad[76]=0;Check(!Decode(bad,sizeof(bad),o),"no points: refused");
        std::memcpy(bad,bytes,sizeof(bad));bad[76]=kMostCredit+1;Check(!Decode(bad,sizeof(bad),o),"too many points: refused");
        // The victim a player's reference (a player is no kill), the player a NetId's: refused both ways.
        Msg w=m;crew::netref::MakePlayer(2,w.victim);
        Check(!Encode(w,bytes,sizeof(bytes)),"a player as the victim: not encoded");
        w=m;Native(w.player,9);
        Check(!Encode(w,bytes,sizeof(bytes)),"a NetId as whose kill: not encoded");
        Check(Encode(m,bytes,sizeof(bytes)),"(again)");
        std::memcpy(bad,bytes,sizeof(bad));bad[44+20]=0x41;bad[44+21]=0x46;bad[44+22]=0x50;bad[44+23]=0x52;bad[44]=0;bad[45]=0x04;
        Check(!Decode(bad,sizeof(bad),o),"a player slot past the mission's: refused");
        std::memcpy(bad,bytes,sizeof(bad));bad[12+20]=1;Check(!Decode(bad,sizeof(bad),o),"a victim that is no canonical NetId: refused");
        std::memcpy(bad,bytes,sizeof(bad));std::memset(bad+12,0,32);Check(!Decode(bad,sizeof(bad),o),"no victim: refused");
        Check(!Decode(bytes,4,o) && !Decode(nullptr,0,o),"too short / nothing: refused");
    }
    {
        Msg m;m.kind=Kind::ack;Native(m.victim,8);
        unsigned char bytes[kCreditWire];
        Check(Encode(m,bytes,sizeof(bytes)),"an ack encodes");
        Msg o;
        Check(Decode(bytes,sizeof(bytes),o) && o.kind==Kind::ack && !std::memcmp(o.victim,m.victim,32),"and decodes");
        unsigned char bad[kCreditWire];std::memcpy(bad,bytes,sizeof(bad));bad[50]=1;
        Check(Decode(bad,sizeof(bad),o),"an ack's tail is not read (zero as sent, nothing in it)");
    }
    {
        Ledger h;h.Start(40);
        unsigned char v[32];Native(v,1);h.Book(v,5,1);h.Book(nullptr,5,8);h.Book(nullptr,9,3);
        std::uint64_t t=0;h.Hold(5,4,&t);
        const Msg m=StateOf(h);
        Check(m.count==2 && m.gauge==40,"the state: a row each Vehicler");
        unsigned char bytes[kStateWire];
        Check(Encode(m,bytes,sizeof(bytes)),"a state encodes");
        Msg o;
        Check(Decode(bytes,sizeof(bytes),o) && o.kind==Kind::state && o.serial==h.serial && o.count==2,"and decodes");
        Mirror mirror;mirror.Start();
        Check(!mirror.heard && !mirror.Find(5),"a guest before the host's word: nothing");
        Hear(mirror,o);
        Check(mirror.heard && mirror.Find(5) && mirror.Find(5)->points==5 && mirror.Find(5)->held==4 && mirror.Find(9)->points==3,
              "a guest shows the host's values, not its own count");
        unsigned char bad[kStateWire];
        std::memcpy(bad,bytes,sizeof(bad));bad[20]=kRowsMost+1;Check(!Decode(bad,sizeof(bad),o),"too many rows: refused");
        std::memcpy(bad,bytes,sizeof(bad));bad[24+2]=50;Check(!Decode(bad,sizeof(bad),o),"a row past the gauge: refused");
        std::memcpy(bad,bytes,sizeof(bad));bad[24+8]=bad[24];bad[24+9]=bad[25];Check(!Decode(bad,sizeof(bad),o),"one slot twice: refused");
        std::memcpy(bad,bytes,sizeof(bad));bad[24+6]=1;Check(!Decode(bad,sizeof(bad),o),"a flag not defined: refused");
        Check(!Decode(bytes,kCreditWire,o),"a state at the credit's size: refused");
        // A guest that hosts now: the host's last word, a held call given back.
        Ledger n;n.Start(30);
        Adopt(n,mirror);
        Check(n.gauge==40 && n.Points(5)==9 && n.Find(5)->held==0 && n.Points(9)==3,"the new host starts from the old one's word");
    }
    std::printf("vehicler_logistics: %d checks passed\n",checks);
    return 0;
}
