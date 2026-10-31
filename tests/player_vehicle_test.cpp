// The players' own loaded jets (src/player_vehicle_logic.h): the room value, the PVEH wire and the decision, run on
// several fake machines that deliver each other's messages in whatever order a test picks. The property: for one
// request every machine makes the same jet (the loaded one only when every machine has it), or the machine that cannot
// know says so (Landing::found false) and makes the stock twin.
#include "../src/player_vehicle_logic.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using namespace crew;
using namespace crew::playerveh;
int checks=0,failures=0;
void Check(bool ok,const char* what) { ++checks; if(!ok){++failures;std::printf("FAIL %s\n",what);} }

VehicleLoadout Jet(VehicleBody body,const wchar_t* text) {
    VehicleLoadout l{};wchar_t why[160];
    const bool ok=ParseVehicleLoadout(text,body,&l,why,160);
    if(!ok)std::printf("(bad loadout %ls: %ls)\n",text,why);
    return l;
}
Variant Of(const VehicleLoadout& l) { return Variant{l.body,LoadoutVariant(l)}; }

void RoomValue() {
    VehicleLoadout own[kPlayerVehicleCount]{};
    own[0]=Jet(VehicleBody::pfighter,L"MK82:12,AAM_S:2");
    own[3]=Jet(VehicleBody::rinterceptor,L"AAM_L:12");
    char value[kValueMost+1];
    Check(OwnValue(own,kPlayerVehicleCount,value,sizeof(value)),"own value made");
    Variant back[kPlayerVehicleCount];int n=0;
    Check(ParseValue(value,back,&n) && n==2 && Same(back[0],Of(own[0])) && Same(back[1],Of(own[3])),"own value reads back");
    Check(std::strlen(value)<=kValueMost,"fits the attribute");
    VehicleLoadout all[kPlayerVehicleCount]{};
    for(int i=0;i<kPlayerVehicleCount;++i)all[i]=Jet(kPlayerVehicles[i].body,L"AAM_S:2,AAM_S:2,AAM_S:2,AAM_S:2,AAM_S:2");
    Check(OwnValue(all,kPlayerVehicleCount,value,sizeof(value)) && std::strlen(value)<=kValueMost,"all five fit");
    VehicleLoadout none[kPlayerVehicleCount]{};
    none[1]=Jet(VehicleBody::pstrike,L"GUNS");
    Check(!OwnValue(none,kPlayerVehicleCount,value,sizeof(value)),"guns alone / stock: nothing published (the stock twin's guns)");
    // Untrusted values.
    char token[20];
    TokenOf(Of(own[0]),token,sizeof(token));
    char built[64];
    const char* bad[]={"","1;","2;","1;,","1;5000000000000C81",   // a support body (the multirole) is no player's
                       "1;6zzzzzzzzzzzzzzzz","1;6000000000000000",nullptr};
    for(int i=0;bad[i];++i)Check(!ParseValue(bad[i],back,&n) && n==0,"a bad value refused whole");
    std::snprintf(built,sizeof(built),"1;%s,%s",token,token);
    Check(!ParseValue(built,back,&n),"two loadouts of one body refused");
    std::snprintf(built,sizeof(built),"1;%s,",token);
    Check(!ParseValue(built,back,&n),"a trailing comma refused");
    // Over the weight: a variant the parser refuses is no token.
    VehicleLoadout heavy{};heavy.body=VehicleBody::rfighter;heavy.count=1;heavy.store[0]=3;heavy.rounds[0]=static_cast<std::uint8_t>(RoundCode(40));
    Check(!Valid(Of(heavy)) && !TokenOf(Of(heavy),token,sizeof(token)),"an over-weight jet is never a token");
    std::snprintf(built,sizeof(built),"1;%c%016llX",static_cast<char>('0'+static_cast<int>(heavy.body)),
                  static_cast<unsigned long long>(LoadoutVariant(heavy)));
    Check(!ParseValue(built,back,&n),"and never read from a member");
}

void Wire() {
    Msg m;m.kind=Kind::have;m.origin=7;m.count=2;
    m.have[0]=Of(Jet(VehicleBody::pfighter,L"MK82:12"));m.have[1]=Of(Jet(VehicleBody::rmultirole,L"RKT:40"));
    unsigned char bytes[kMostSize];
    std::size_t size=Encode(m,bytes,sizeof(bytes));
    Msg back;
    Check(size==kHead+4+18 && Decode(bytes,size,back) && back.count==2 && Same(back.have[1],m.have[1]),"have round trip");
    Check(!Decode(bytes,size-1,back),"a have of the wrong size refused");
    bytes[4]=1;Check(!Decode(bytes,size,back),"an older version (the requester its NetId) refused");
    bytes[4]=2;Check(!Decode(bytes,size,back),"version 2 (the player's tag at bytes 0..3) refused");
    bytes[4]=4;Check(!Decode(bytes,size,back),"a newer version refused");
    // The requester: a player's reference (net_object_ref.h), mission player 1.
    Msg d;d.kind=Kind::decide;d.origin=9;d.seq=3;d.variant=m.have[0];crew::netref::MakePlayer(1,d.requester);
    size=Encode(d,bytes,sizeof(bytes));
    Check(size==kDecideSize && Decode(bytes,size,back) && back.seq==3 && Same(back.variant,d.variant) && SameId(back.requester,d.requester),
          "decide round trip");
    d.variant.bits=0;size=Encode(d,bytes,sizeof(bytes));
    Check(size && Decode(bytes,size,back) && back.variant.bits==0,"a stock decision travels");
    d.variant.body=VehicleBody::fighter;Check(!Encode(d,bytes,sizeof(bytes)),"a support body is no player's decision");
    d.variant.body=VehicleBody::pfighter;std::memset(d.requester,0,32);Check(!Encode(d,bytes,sizeof(bytes)),"no requester: refused");
    d.requester[0]=1;d.requester[20]=5;Check(!Encode(d,bytes,sizeof(bytes)),"a requester ID not canonical refused");
    std::memset(d.requester,0,32);d.requester[0]=1;d.requester[31]=2;Check(!Encode(d,bytes,sizeof(bytes)),"a requester by its NetId refused");
    crew::netref::MakePlayer(1,d.requester);
    Msg s;s.kind=Kind::sync;s.origin=4;s.variant.body=VehicleBody::pstrike;s.seq=11;crew::netref::MakePlayer(9,s.requester);
    size=Encode(s,bytes,sizeof(bytes));
    Check(size==kSyncSize && Decode(bytes,size,back) && back.kind==Kind::sync && back.seq==11,"sync round trip");
    Msg zero=s;zero.origin=0;Check(!Encode(zero,bytes,sizeof(bytes)),"no origin: refused");
    Check(!Owned("XXXX",4),"another channel's magic is not ours");
}

void Paths() {
    wchar_t path[160];
    const Variant v=Of(Jet(VehicleBody::rinterceptor,L"AAM_L:12"));
    Check(LoadedPath(v,path,160) && !std::wcsncmp(path,L"app:/object/edf6vc_lo_rinterceptor_",35) && std::wcsstr(path,L".sgo"),
          "a loaded jet's lower-case path");
    Check(TwinOfPath(L"app:/object/edf6vc_pjet_fighter.sgo")==0 && TwinOfPath(L"APP:/OBJECT/EDF6VC_FLY_MULTIROLE.SGO")==4 &&
          TwinOfPath(L"app:/object/edf6vc_fly_gunship.sgo")<0 && TwinOfPath(L"app:/object/edf6vc_jet_fighter.sgo")<0 && TwinOfPath(nullptr)<0,
          "the twins a request makes, nothing else");
    for(int i=0;i<kPlayerVehicleCount;++i) {
        const VehicleBodyRow* row=VehicleBodyOf(kPlayerVehicles[i].body);
        Check(row && !_wcsicmp(row->base,kPlayerVehicles[i].twin) && VehicleBodyOfKey(kPlayerVehicles[i].key)==kPlayerVehicles[i].body &&
              PlayerVehicleOfKey(kPlayerVehicles[i].key)==i && IsPlayerBody(kPlayerVehicles[i].body),"a player vehicle's body is built on its twin");
    }
    Check(!IsPlayerBody(VehicleBody::fighter) && !IsPlayerBody(VehicleBody::tank),"support bodies are not a player's");
}

// --- Several machines ---
struct Packet { int from,to; std::vector<unsigned char> bytes; };
struct Machine {
    Engine engine;
    unsigned char id[32]{};
    std::uint32_t origin=0;
    bool feature=true;
    VehicleLoadout own[kPlayerVehicleCount]{};
};
struct World {
    std::vector<Machine*> m;
    std::vector<Packet> wire;
    std::uint32_t generation=1;
    void Room() {
        for(std::size_t i=0;i<m.size();++i) {
            bool has[kPeersMost]{};int n=0;
            for(std::size_t j=0;j<m.size();++j)if(j!=i)has[n++]=m[j]->feature;
            m[i]->engine.Room(generation,n,has);
        }
    }
    // Machine i's peer index of machine j (1..: the others in order).
    static int PeerOf(int i,int j) { return j<i ? j+1 : j; }
    void Send(int from,int to,const Msg& msg) {
        if(!m[from]->feature || !m[to]->feature)return;   // a channel goes only to a peer that announced it
        unsigned char b[kMostSize];
        const std::size_t n=Encode(msg,b,sizeof(b));
        Check(n>0,"every message the engine makes encodes");
        wire.push_back(Packet{from,to,std::vector<unsigned char>(b,b+n)});
    }
    void Broadcast(int from,const Msg& msg) { for(std::size_t j=0;j<m.size();++j)if(static_cast<int>(j)!=from)Send(from,static_cast<int>(j),msg); }
    void Deliver(bool drop=false,int dropTo=-1) {
        std::vector<Packet> now;now.swap(wire);
        for(auto& p:now) {
            if(drop && p.to==dropTo)continue;
            Msg msg;
            Check(Decode(p.bytes.data(),p.bytes.size(),msg),"every message sent decodes");
            if(msg.kind==Kind::have)m[p.to]->engine.Heard(PeerOf(p.to,p.from),msg);
            else m[p.to]->engine.Received(msg);
        }
    }
    void Haves() { for(std::size_t i=0;i<m.size();++i)Broadcast(static_cast<int>(i),m[i]->engine.Have(m[i]->origin)); Deliver(); }
};
Machine* NewMachine(std::uint8_t tag,VehicleLoadout own0) {
    auto* x=new Machine;
    crew::netref::MakePlayer(tag,x->id);x->origin=tag*1000u+1;   // its player's reference (net_object_ref.h)
    x->own[0]=own0;
    return x;
}
// Begin a mission: each machine preloads its own and every other member's player loadouts it has files of
// (`lacks[i]`: machine i lacks every other member's files).
void Begin(World& w,const std::vector<bool>& lacks) {
    for(std::size_t i=0;i<w.m.size();++i) {
        std::vector<Variant> pre;
        for(std::size_t j=0;j<w.m.size();++j) {
            if(j!=i && lacks[i])continue;
            for(const auto& l:w.m[j]->own)if(l.On() && l.count)pre.push_back(Of(l));
        }
        w.m[i]->engine.Begin(w.m[i]->own,kPlayerVehicleCount,pre.data(),static_cast<int>(pre.size()));
    }
    w.Room();
    w.Haves();
}
// Machine r's player requests body `b`; `deliverFirst`: the decision reaches the others before their configure.
// Returns the bits each machine makes it with (requester first), and whether each knew.
struct Outcome { std::vector<std::uint64_t> bits; std::vector<bool> found; };
Outcome Request(World& w,int r,VehicleBody b,bool deliverFirst,bool drop=false,int dropTo=-1) {
    const Msg d=w.m[r]->engine.Decide(w.m[r]->id,b,w.m[r]->origin,true);
    w.Broadcast(r,d);
    std::vector<std::uint32_t> seq(w.m.size());
    seq[r]=d.seq;
    if(deliverFirst)w.Deliver(drop,dropTo);
    for(std::size_t i=0;i<w.m.size();++i)if(static_cast<int>(i)!=r)seq[i]=w.m[i]->engine.Count(w.m[r]->id,b);
    if(!deliverFirst)w.Deliver(drop,dropTo);
    Outcome o;
    for(std::size_t i=0;i<w.m.size();++i) {
        const auto l=w.m[i]->engine.Land(w.m[r]->id,b,seq[i]);
        o.bits.push_back(l.bits);o.found.push_back(l.found);
    }
    return o;
}
bool AllSame(const Outcome& o) { for(auto b:o.bits)if(b!=o.bits[0])return false; return true; }

void Machines() {
    const VehicleLoadout a=Jet(VehicleBody::pfighter,L"MK82:12,AAM_S:2"),b=Jet(VehicleBody::pfighter,L"AAM_M:6");
    // Everyone has every file: the loaded jet everywhere, whichever arrives first.
    {World w;w.m={NewMachine(1,a),NewMachine(2,b),NewMachine(3,{})};Begin(w,{false,false,false});
     Outcome o=Request(w,0,VehicleBody::pfighter,false);
     Check(AllSame(o) && o.bits[0]==LoadoutVariant(a) && o.found[1] && o.found[2],"all have it: every machine makes A's loaded jet (decide after configure)");
     o=Request(w,0,VehicleBody::pfighter,true);
     Check(AllSame(o) && o.bits[0]==LoadoutVariant(a),"a second request: the same (decide before configure)");
     o=Request(w,1,VehicleBody::pfighter,false);
     Check(AllSame(o) && o.bits[0]==LoadoutVariant(b),"B's request: B's own loadout, not A's");
     o=Request(w,2,VehicleBody::pfighter,false);
     Check(AllSame(o) && o.bits[0]==0,"C has none: the stock twin everywhere");
     o=Request(w,0,VehicleBody::pstrike,false);
     Check(AllSame(o) && o.bits[0]==0,"A has no loadout for the strike jet: stock everywhere");
     for(auto* x:w.m)delete x;}
    // A machine lacks A's file: A decides stock, everyone stock.
    {World w;w.m={NewMachine(1,a),NewMachine(2,{}),NewMachine(3,{})};Begin(w,{false,false,true});
     const Outcome o=Request(w,0,VehicleBody::pfighter,false);
     Check(AllSame(o) && o.bits[0]==0,"one machine lacks the file: the stock twin everywhere");
     for(auto* x:w.m)delete x;}
    // An older build (no channel): it makes the stock twin; so does everyone.
    {World w;w.m={NewMachine(1,a),NewMachine(2,{}),NewMachine(3,{})};w.m[2]->feature=false;Begin(w,{false,false,false});
     const Outcome o=Request(w,0,VehicleBody::pfighter,false);
     Check(AllSame(o) && o.bits[0]==0 && !o.found[2],"an older peer: stock everywhere (it never hears, makes the stock twin)");
     for(auto* x:w.m)delete x;}
    // A peer whose have has not arrived yet: stock (never a guess).
    {World w;w.m={NewMachine(1,a),NewMachine(2,{})};
     w.m[0]->engine.Begin(w.m[0]->own,kPlayerVehicleCount,nullptr,0);
     const Variant av=Of(a);
     w.m[0]->engine.Begin(w.m[0]->own,kPlayerVehicleCount,&av,1);w.m[1]->engine.Begin(w.m[1]->own,kPlayerVehicleCount,&av,1);
     w.Room();
     const Outcome o=Request(w,0,VehicleBody::pfighter,false);
     Check(AllSame(o) && o.bits[0]==0,"no have heard yet: stock everywhere");
     w.Haves();
     const Outcome later=Request(w,0,VehicleBody::pfighter,false);
     Check(AllSame(later) && later.bits[0]==LoadoutVariant(a),"after the haves: loaded everywhere");
     for(auto* x:w.m)delete x;}
    // The requester's own file missing: stock even if the others have it.
    {World w;w.m={NewMachine(1,a),NewMachine(2,{})};
     const Variant av=Of(a);
     w.m[0]->engine.Begin(w.m[0]->own,kPlayerVehicleCount,nullptr,0);w.m[1]->engine.Begin(w.m[1]->own,kPlayerVehicleCount,&av,1);
     w.Room();w.Haves();
     const Outcome o=Request(w,0,VehicleBody::pfighter,false);
     Check(AllSame(o) && o.bits[0]==0,"the requester lacks its own file: stock everywhere");
     for(auto* x:w.m)delete x;}
    // A decision lost on its way: that machine cannot know (found false) and makes stock: the one residual mismatch,
    // which the game log names (player_vehicle.cpp: "landed with no decision heard").
    {World w;w.m={NewMachine(1,a),NewMachine(2,{}),NewMachine(3,{})};Begin(w,{false,false,false});
     const Outcome o=Request(w,0,VehicleBody::pfighter,false,true,2);
     Check(o.bits[0]==LoadoutVariant(a) && o.bits[1]==o.bits[0] && o.bits[2]==0 && !o.found[2],"a lost decision: that machine says it did not know");
     for(auto* x:w.m)delete x;}
    // Interleaved requesters and bodies: each request's number lines up on every machine.
    {World w;w.m={NewMachine(1,a),NewMachine(2,b),NewMachine(3,{})};
     w.m[1]->own[3]=Jet(VehicleBody::rinterceptor,L"AAM_L:12");
     Begin(w,{false,false,false});
     const VehicleBody bodies[]={VehicleBody::pfighter,VehicleBody::rinterceptor};
     unsigned seed=7;
     bool same=true,loaded=false;
     for(int step=0;step<300;++step) {
         seed=seed*1103515245u+12345u;
         const int r=static_cast<int>((seed>>16)%3);
         const VehicleBody body=bodies[(seed>>20)&1];
         const Outcome o=Request(w,r,body,((seed>>24)&1)!=0);
         same&=AllSame(o);
         loaded|=o.bits[0]!=0;
         const int at=PlayerVehicleOfBody(body);
         same&=o.bits[0]==(at>=0 && w.m[r]->own[at].On() && w.m[r]->own[at].count ? LoadoutVariant(w.m[r]->own[at]) : 0ull);
     }
     Check(same && loaded,"300 random requests: every machine makes each one alike, the requester's own loadout");
     for(auto* x:w.m)delete x;}
    // A late joiner: synced to each requester's next number, its counts line up with the room's.
    {// The room's engines keep their counts (mid-mission); the joiner starts its own at 0, then hears A's next number
     // (A's Joined: Syncs).
     Msg syncs[kPlayerVehicleCount];
     World w2;w2.m={NewMachine(1,a),NewMachine(2,{})};Begin(w2,{false,false});
     for(int i=0;i<3;++i)Request(w2,0,VehicleBody::pfighter,false);
     Machine* d2=NewMachine(4,{});
     const Variant pre[]={Of(a)};
     d2->engine.Begin(d2->own,kPlayerVehicleCount,pre,1);
     w2.m.push_back(d2);++w2.generation;w2.Room();w2.Haves();
     const int n=w2.m[0]->engine.Syncs(w2.m[0]->id,w2.m[0]->origin,syncs,kPlayerVehicleCount);
     for(int i=0;i<n;++i)w2.Send(0,2,syncs[i]);
     w2.Deliver();
     const Outcome o=Request(w2,0,VehicleBody::pfighter,false);
     Check(AllSame(o) && o.bits[0]==LoadoutVariant(a) && o.found[2],"a late joiner synced: its count matches the room's 4th request");
     Machine* d3=NewMachine(5,{});d3->engine.Begin(d3->own,kPlayerVehicleCount,pre,1);
     w2.m.push_back(d3);++w2.generation;w2.Room();w2.Haves();
     const Outcome unsynced=Request(w2,0,VehicleBody::pfighter,false);
     Check(!unsynced.found[3] && unsynced.bits[3]==0,"an unsynced joiner's count is off: it does not know, and makes stock (logged)");
     for(auto* x:w2.m)delete x;}
    // Offline: the own loaded jet when its file is preloaded.
    {Engine e;const Variant av=Of(a);VehicleLoadout own[kPlayerVehicleCount]{};own[0]=a;
     e.Begin(own,kPlayerVehicleCount,&av,1);
     unsigned char id[32]{};crew::netref::MakePlayer(1,id);
     const Msg d=e.Decide(id,VehicleBody::pfighter,1,false);
     Check(d.variant.bits==LoadoutVariant(a) && e.Land(id,VehicleBody::pfighter,d.seq).bits==d.variant.bits,"offline: the own loaded jet");
     e.Begin(own,kPlayerVehicleCount,nullptr,0);
     const Msg s=e.Decide(id,VehicleBody::pfighter,1,false);
     Check(s.variant.bits==0,"offline without the file: stock");}
}
}  // namespace

int main() {
    RoomValue();
    Wire();
    Paths();
    Machines();
    std::printf(failures ? "player_vehicle_test: %d of %d FAILED\n" : "player_vehicle_test: %d checks passed\n",failures ? failures : checks,checks);
    return failures ? 1 : 0;
}
