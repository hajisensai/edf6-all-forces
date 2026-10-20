#include "../src/command_protocol.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>
#include <limits>
using namespace crew;
using namespace crew::command_net;
namespace {
int checks=0;
void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
struct Room;
struct Node {Room* room;unsigned index;std::unique_ptr<Session> session=std::make_unique<Session>();unsigned executed=0;const char* sender=nullptr;
    std::uint32_t lastFlags=0,lastParam=0;mapcmd::Order lastOrder=mapcmd::Order::none;};
struct Packet {unsigned from,to;Message message;};
struct Room {
    std::vector<std::unique_ptr<Node>> nodes;std::deque<Packet> queue;std::vector<Packet> sent;
    std::uint64_t now=10000,epoch=77;unsigned drop=99;Kind dropKind=Kind::result;
    explicit Room(unsigned count=3,bool capable=true,unsigned oldNode=99);
    unsigned Peer(unsigned here,unsigned global)const{return global<here ? global+1 : global;}
    unsigned Global(unsigned here,unsigned peer)const{return peer<=here ? peer-1 : peer;}
    const char* Name(unsigned n)const{return n==0 ? "host" : n==1 ? "client-one" : "client-two";}
    void Deliver(const Packet& p){nodes[p.to]->session->Receive(Peer(p.to,p.from),Name(p.from),p.message,now);}
    void Pump(){while(!queue.empty()){auto p=queue.front();queue.pop_front();if(p.to==drop && p.message.kind==dropKind)continue;Deliver(p);}}
    void Step(unsigned ms=300){now+=ms;for(unsigned i=0;i<nodes.size();++i)nodes[i]->session->Update(true,i==0,epoch,i ? Peer(i,0) : 0,static_cast<unsigned>(nodes.size()-1),now);Pump();}
};
bool Send(void* ptr,std::uint32_t peer,const Message& m) noexcept {
    auto& n=*static_cast<Node*>(ptr);auto& r=*n.room;
    unsigned char bytes[kWireSize];Message decoded;
    if(!Encode(m,bytes,sizeof(bytes)) || !Decode(bytes,sizeof(bytes),decoded))return false;
    if(decoded.version!=m.version)return false;
    Packet p{n.index,r.Global(n.index,peer),decoded};r.queue.push_back(p);r.sent.push_back(p);return true;
}
void Execute(void* ptr,const char* sender,const Request& r,Message::Result* out) noexcept {
    auto& n=*static_cast<Node*>(ptr);++n.executed;n.sender=sender;n.lastFlags=r.flags;n.lastParam=r.command.param;n.lastOrder=r.command.order;
    for(unsigned i=0;i<r.count;++i)out[i]={static_cast<unsigned>(i==1 ? NpcCommandReason::notOwner : NpcCommandReason::none),i==1 ? 0u : 4u};
}
Room::Room(unsigned count,bool capable,unsigned oldNode) {
    for(unsigned i=0;i<count;++i){
        auto n=std::make_unique<Node>();n->room=this;n->index=i;n->session->Configure({n.get(),&Send,(i==0 && !capable) ? nullptr : &Execute});
        if(i==oldNode)n->session->LimitVersion(kMinVersion);   // a version-6 build
        nodes.push_back(std::move(n));
    }
    Step();
}
Request Make(unsigned count=1,mapcmd::Order order=mapcmd::Order::guard) {
    Request r;r.count=count;r.command={order,{20,0,30}};r.requester[0]=200;
    if(order==mapcmd::Order::focus)r.focus[0]=100;
    for(unsigned i=0;i<count && i<kCommandNetUnits;++i)r.units[i][0]=static_cast<unsigned char>(i+1);
    return r;
}
Packet Find(const Room& r,Kind kind,unsigned from){for(const auto& p:r.sent)if(p.from==from && p.message.kind==kind)return p;Check(false,"packet recorded");return {};}
void Codec() {
    Message m;m.kind=Kind::request;m.epoch=7;m.sequence=4;m.request=Make(16,mapcmd::Order::focus);
    unsigned char bytes[kWireSize];std::memset(bytes,0xCD,sizeof(bytes));Message copy;
    Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),copy) && copy.request.count==16,"full canonical command packet round trip");
    Check(Owned(bytes,sizeof(bytes)),"separate magic can route without a second poll");
    for(unsigned i=0;i<sizeof(bytes);++i)Check(!Decode(bytes,i,copy),"truncated packet rejected");
    bytes[4]=static_cast<unsigned char>(kVersion+1);Check(Owned(bytes,sizeof(bytes)) && !Decode(bytes,sizeof(bytes),copy),"newer command version consumed but never executed");
    m.request.units[0][20]=1;Check(!Encode(m,bytes,sizeof(bytes)),"dirty native identity padding rejected");m.request.units[0][20]=0;
    std::memcpy(m.request.units[1],m.request.units[0],32);Check(!Encode(m,bytes,sizeof(bytes)),"duplicate unit cannot execute twice");
    m.request=Make(1);m.request.command.at[1]=std::numeric_limits<float>::infinity();Check(!Encode(m,bytes,sizeof(bytes)),"nonfinite guard point refused");
    m.request=Make(1,mapcmd::Order::focus);std::memset(m.request.focus,0,32);Check(!Encode(m,bytes,sizeof(bytes)),"focus needs explicit registered enemy identity");
    m.request=Make(2);m.request.formationTotal=5;m.request.formationSlots[0]=1;m.request.formationSlots[1]=4;
    Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),copy) && copy.request.formationSlots[1]==4,"mixed-selection global formation indices round trip");
    m.request.formationSlots[1]=1;Check(!Encode(m,bytes,sizeof(bytes)),"duplicate formation slot rejected");
    m.request.formationTotal=97;Check(!Encode(m,bytes,sizeof(bytes)),"formation total bounded to map selection capacity");
    // Version 3: the RTS orders carry their formation slots as guard does; nothing past the last order decodes.
    // Version 4: WITHDRAW (a squad's transport sent off) is an order with no point and no slots.
    m.request=Make(1,mapcmd::Order::withdraw);
    Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),copy) && copy.request.command.order==mapcmd::Order::withdraw,
          "WITHDRAW round trips");
    // Version 5: DISMOUNT ALL (DISMOUNT now leaves the driver and the gunners aboard) and the noPassengers refusal.
    m.request=Make(1,mapcmd::Order::dismountAll);
    Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),copy) && copy.request.command.order==mapcmd::Order::dismountAll,
          "DISMOUNT ALL round trips");
    Check(static_cast<int>(mapcmd::Order::dismountAll)==12,"DISMOUNT ALL is appended (12): the older orders keep their wire values");
    // Version 6: HOLD (13), no point and no formation slots (the authority reads where its unit stands).
    m.request=Make(1,mapcmd::Order::hold);
    Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),copy) && copy.request.command.order==mapcmd::Order::hold,
          "HOLD round trips");
    m.request.formationTotal=1;
    Check(!Encode(m,bytes,sizeof(bytes)),"HOLD carries no formation slots");
    Check(static_cast<int>(mapcmd::Order::hold)==13 && static_cast<int>(mapcmd::Order::formation)==14 &&
          mapcmd::kLastOrder==mapcmd::Order::formation,"HOLD is appended (13), the formation order after it (14)");
    Check(kVersion==7 && kMinVersion==6 && Compatible(6) && Compatible(7) && !Compatible(5) && !Compatible(8),
          "the wire is version 7 (the queue's flag, the formation order), version 6 still spoken");
    for(auto order:{mapcmd::Order::move,mapcmd::Order::attackMove}) {
        m.request=Make(2,order);m.request.formationTotal=3;m.request.formationSlots[0]=0;m.request.formationSlots[1]=2;
        Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),copy) && copy.request.command.order==order &&
              copy.request.formationSlots[1]==2,"a move / attack-move with its global formation slots round trips");
    }
    m.request=Make(2,mapcmd::Order::follow);m.request.formationTotal=3;m.request.formationSlots[1]=2;
    Check(!Encode(m,bytes,sizeof(bytes)),"formation slots only on point orders");
    m.request=Make(1);Check(Encode(m,bytes,sizeof(bytes)),"a guard encodes");
    {   // The order is the ninth word (magic, version, kind, reserved, epoch x2, sequence, count, order).
        constexpr std::size_t kOrderAt=32;Message out;
        const std::uint32_t last=static_cast<std::uint32_t>(mapcmd::kLastOrder),past=last+1;
        unsigned char edit[kWireSize];std::memcpy(edit,bytes,sizeof(edit));
        std::uint32_t was;std::memcpy(&was,edit+kOrderAt,4);
        Check(was==static_cast<std::uint32_t>(mapcmd::Order::guard),"the order word where the codec writes it");
        std::memcpy(edit+kOrderAt,&last,4);
        const std::uint32_t param=mapcmd::FormationParam(-2,-1);std::memcpy(edit+52,&param,4);   // the formation order's own word
        Check(Decode(edit,sizeof(edit),out) && out.request.command.order==mapcmd::kLastOrder,"the last order decodes");
        std::memcpy(edit+kOrderAt,&past,4);
        Check(!Decode(edit,sizeof(edit),out),"an order past the last one is not decoded");
    }
}
// Version 7: the Shift queue's flag and the formation order, in the two words version 6 keeps zero; version 6 still
// encodes and decodes (a v6 peer's packets), and neither carries what it cannot.
void Version7() {
    Message m;m.kind=Kind::request;m.epoch=7;m.sequence=4;m.request=Make(2,mapcmd::Order::move);m.request.flags=kQueued;
    m.request.formationTotal=2;m.request.formationSlots[0]=0;m.request.formationSlots[1]=1;
    unsigned char bytes[kWireSize];Message copy;
    Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),copy) && copy.version==7 && copy.request.flags==kQueued,
          "a queued move round trips at version 7");
    {std::uint32_t flags;std::memcpy(&flags,bytes+12,4);Check(flags==kQueued,"the queue's flag is the word after kind");}
    m.request.flags=kQueued|2u;Check(!Encode(m,bytes,sizeof(bytes)),"an unknown flag refused");
    m.request=Make(1,mapcmd::Order::follow);m.request.flags=kQueued;Check(!Encode(m,bytes,sizeof(bytes)),"only a point order is queued");
    m.request=Make(2,mapcmd::Order::formation);m.request.command.param=mapcmd::FormationParam(-2,4);
    Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),copy) && copy.request.command.order==mapcmd::Order::formation &&
          copy.request.command.param==m.request.command.param,"the formation order and its parameter round trip");
    {std::uint32_t param;std::memcpy(&param,bytes+52,4);Check(param==m.request.command.param,"the parameter is the word after the point");}
    m.request.command.param=0;Check(!Encode(m,bytes,sizeof(bytes)),"a formation order saying nothing refused");
    m.request=Make(1,mapcmd::Order::guard);m.request.command.param=3;Check(!Encode(m,bytes,sizeof(bytes)),"a parameter on another order refused");
    m.request=Make(1,mapcmd::Order::formation);m.request.command.param=mapcmd::FormationParam(2,-1);m.request.formationTotal=1;
    Check(!Encode(m,bytes,sizeof(bytes)),"no formation slots on the formation order");
    // Version 6: the same layout, the two words zero; a queued flag, a parameter or the formation order cannot go at it.
    m=Message{};m.version=6;m.kind=Kind::request;m.epoch=7;m.sequence=5;m.request=Make(1,mapcmd::Order::hold);
    Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),copy) && copy.version==6 && !copy.request.flags,
          "a version-6 HOLD round trips (a version-6 peer's request is read)");
    m.request=Make(1,mapcmd::Order::move);m.request.flags=kQueued;Check(!Encode(m,bytes,sizeof(bytes)),"no queued flag at version 6");
    m.request=Make(1,mapcmd::Order::formation);m.request.command.param=mapcmd::FormationParam(2,-1);
    Check(!Encode(m,bytes,sizeof(bytes)),"no formation order at version 6");
    m.request=Make(1,mapcmd::Order::move);Check(Encode(m,bytes,sizeof(bytes)),"a plain move at version 6");
    {const std::uint32_t queued=kQueued;unsigned char edit[kWireSize];std::memcpy(edit,bytes,sizeof(edit));std::memcpy(edit+12,&queued,4);
     Check(!Decode(edit,sizeof(edit),copy),"a version-6 packet with the queue's word set is not read as queued");}
    {const std::uint32_t order=static_cast<std::uint32_t>(mapcmd::Order::formation);unsigned char edit[kWireSize];
     std::memcpy(edit,bytes,sizeof(edit));std::memcpy(edit+32,&order,4);
     Check(!Decode(edit,sizeof(edit),copy),"no order past HOLD decodes at version 6");}
    Check(RequestVersion(Make(1,mapcmd::Order::move))==6 && RequestVersion(m.request)==6,"a plain order needs version 6");
    Request q=Make(1,mapcmd::Order::move);q.flags=kQueued;Check(RequestVersion(q)==7 && !ValidRequest(q,6) && ValidRequest(q,7),
        "a queued one needs version 7");
}
// The negotiation (B1): both sides of every pair of versions agree the lower one; a version-6 host is never sent a
// queued or formation order (refused before it goes, the map says why), a version-6 client keeps every order it has.
void Negotiation() {
    {   // both version 7: queued and formation orders reach the executor with their flag and parameter
        Room r;Check(r.nodes[1]->session->HostVersion()==7,"version 7 both sides: 7");
        Request q=Make(1,mapcmd::Order::attackMove);q.flags=kQueued;
        Check(r.nodes[1]->session->Submit(q,r.Name(1),r.now)!=0,"a queued attack-move submitted");r.Pump();
        Check(r.nodes[0]->executed==1 && r.nodes[0]->lastFlags==kQueued && r.nodes[0]->lastOrder==mapcmd::Order::attackMove,
              "the host's executor gets the queued flag (its authority keeps the point)");
        r.Step();Request f=Make(1,mapcmd::Order::formation);f.command.param=mapcmd::FormationParam(-2,3);
        Check(r.nodes[1]->session->Submit(f,r.Name(1),r.now)!=0,"a formation order submitted");r.Pump();
        Check(r.nodes[0]->executed==2 && r.nodes[0]->lastParam==f.command.param,"the host's executor gets the formation parameter");
        for(const auto& p:r.sent)if(p.message.kind==Kind::hello)Check(p.message.version==6 && p.message.request.formationTotal==7,
            "a hello goes out at version 6, the sender's highest (7) in it");
    }
    {   // a version-6 host: the version-7 client speaks 6 and refuses what 6 cannot carry, before sending it
        Room r(2,true,0);
        Check(r.nodes[1]->session->Ready() && r.nodes[1]->session->HostVersion()==6,"a version-6 host: the client is ready, at 6");
        Request q=Make(1,mapcmd::Order::move);q.flags=kQueued;
        const auto before=r.sent.size();
        Check(!r.nodes[1]->session->Submit(q,r.Name(1),r.now) && r.sent.size()==before,"a queued move to a version-6 host: not sent");
        Request f=Make(1,mapcmd::Order::formation);f.command.param=mapcmd::FormationParam(1,-1);
        Check(!r.nodes[1]->session->Submit(f,r.Name(1),r.now),"a formation order to a version-6 host: not sent");
        Check(r.nodes[1]->session->Submit(Make(1,mapcmd::Order::move),r.Name(1),r.now)!=0,"a plain move to a version-6 host: sent");
        r.Pump();
        Check(r.nodes[0]->executed==1 && !r.nodes[0]->lastFlags,"the version-6 host executes the plain move");
        for(const auto& p:r.sent)Check(p.message.version==6,"every packet of the pair at version 6");
    }
    {   // a version-6 client of a version-7 host: answered at 6, its orders executed
        Room r(3,true,2);
        Check(r.nodes[2]->session->Ready() && r.nodes[2]->session->HostVersion()==6 && r.nodes[1]->session->HostVersion()==7,
              "the host answers each client at its own version");
        Check(r.nodes[2]->session->Submit(Make(1,mapcmd::Order::hold),r.Name(2),r.now)!=0,"the version-6 client's HOLD");r.Pump();
        Check(r.nodes[0]->executed==1,"executed");
        for(const auto& p:r.sent)if(p.to==2 || p.from==2)Check(p.message.version==6,"everything to and from it at version 6");
        // A version-7 packet claiming to come from it is not executed (above what it agreed).
        Message forged;forged.kind=Kind::request;forged.epoch=r.epoch;forged.sequence=9;forged.request=Make(1,mapcmd::Order::move);
        forged.request.flags=kQueued;
        r.nodes[0]->session->Receive(2,r.Name(2),forged,r.now);r.Pump();
        Check(r.nodes[0]->executed==1,"a version-7 request above a peer's agreed version is not executed");
    }
}
// Kind::ways (v7): the host's Shift queues for the clients' maps -- sent to a version-7 peer when they change, never to
// a version-6 one, all of them again to a peer saying hello, cleared when gone; newest revision wins.
void WaysMessages() {
    Message m;m.kind=Kind::ways;m.epoch=7;m.sequence=3;m.request.count=1;m.request.units[0][0]=9;m.ways.n=2;
    m.ways.at[0][0]=1;m.ways.at[1][2]=-4;
    unsigned char bytes[kWireSize];Message copy;
    Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),copy) && copy.kind==Kind::ways && copy.ways.n==2 &&
          copy.ways.at[1][2]==-4 && !copy.request.formationTotal,"a unit's queue round trips");
    m.version=6;Check(!Encode(m,bytes,sizeof(bytes)),"no ways message at version 6");m.version=7;
    m.ways.n=9;Check(!Encode(m,bytes,sizeof(bytes)),"at most eight points");m.ways.n=1;
    Check(!Encode(m,bytes,sizeof(bytes)),"a point past the count must be zero");m.ways.n=2;
    m.ways.at[0][1]=std::numeric_limits<float>::quiet_NaN();Check(!Encode(m,bytes,sizeof(bytes)),"no NaN point");m.ways.at[0][1]=0;
    m.sequence=0;Check(!Encode(m,bytes,sizeof(bytes)),"a revision is required");

    Room r(3,true,2);   // node 2 is a version-6 build
    const unsigned char unit[32]={5};const float pts[2][3]={{10,0,1},{20,0,2}};
    const auto waysTo=[&](unsigned to){unsigned n=0;for(const auto& p:r.sent)if(p.to==to && p.message.kind==Kind::ways)++n;return n;};
    r.nodes[0]->session->PublishWays(unit,pts,2);r.Pump();
    command_net::Ways got;
    Check(r.nodes[1]->session->WaysOf(unit,&got) && got.n==2 && got.at[1][0]==20,"a version-7 client gets the host's queue");
    Check(!waysTo(2),"a version-6 client is never sent one");
    const auto sent=r.sent.size();r.nodes[0]->session->PublishWays(unit,pts,2);r.Pump();
    Check(r.sent.size()==sent,"unchanged: not sent again");
    // A peer saying hello again (a reconnect, a new member) is sent every queue out now.
    const auto before=waysTo(1);
    Message hello;hello.version=6;hello.epoch=r.epoch;hello.request.formationTotal=7;
    r.nodes[0]->session->Receive(1,r.Name(1),hello,r.now);r.Pump();
    Check(waysTo(1)==before+1,"hello: the queues out sent to it again");
    unsigned char out[4][32];Check(r.nodes[0]->session->PublishedWays(out,4)==1,"the host lists what is out");
    r.nodes[0]->session->PublishWays(unit,nullptr,0);r.Pump();
    Check(!r.nodes[1]->session->WaysOf(unit,&got) && !r.nodes[0]->session->PublishedWays(out,4),"cleared on both sides");
    // An older revision arriving late does not undo a newer one.
    r.nodes[0]->session->PublishWays(unit,pts,1);r.Pump();
    Message late;for(const auto& p:r.sent)if(p.to==1 && p.message.kind==Kind::ways && p.message.ways.n==2)late=p.message;
    r.nodes[1]->session->Receive(1,r.Name(0),late,r.now);
    Check(r.nodes[1]->session->WaysOf(unit,&got) && got.n==1,"an older revision is ignored");
    // A client cannot publish; another client cannot forge the host's.
    r.nodes[1]->session->PublishWays(unit,pts,2);r.Pump();
    Check(r.nodes[1]->session->WaysOf(unit,&got) && got.n==1,"a client publishes nothing");
    ++r.epoch;r.Step();
    Check(!r.nodes[1]->session->WaysOf(unit,&got),"a new mission forgets them");
}
void Protocol() {
    Room r;Check(r.nodes[1]->session->Ready(),"host command capability negotiated on current support epoch");
    Check(r.nodes[1]->session->Submit(Make(2),r.Name(1),r.now)==1,"request queues without pretending to execute");
    Check(r.nodes[1]->session->Result().state==CommandNetworkState::pending && !r.nodes[0]->executed,"local client never writes authority objects");
    r.Pump();const auto& out=r.nodes[1]->session->Result();
    Check(r.nodes[0]->executed==1 && !std::strcmp(r.nodes[0]->sender,"client-one"),"authenticated transport sender reaches executor");
    Check(out.state==CommandNetworkState::completed && out.count==2 && out.units[0].Accepted() && out.units[1].reason==NpcCommandReason::notOwner,
        "per-unit actual acceptance and ownership refusal returned");
    const auto req=Find(r,Kind::request,1);r.Deliver(req);r.Pump();Check(r.nodes[0]->executed==1,"duplicate retry returns cache without re-execution");
    auto altered=req;altered.message.request.command.order=mapcmd::Order::follow;r.Deliver(altered);r.Pump();
    Check(r.nodes[0]->executed==1,"sequence reuse with changed command cannot execute");
    r.Step(20);r.nodes[1]->session->Submit(Make(),r.Name(1),r.now);r.Pump();
    Check(r.nodes[1]->session->Result().state==CommandNetworkState::rateLimited && r.nodes[0]->executed==1,"new request is rate limited explicitly");
    r.Step();r.drop=1;r.nodes[1]->session->Submit(Make(),r.Name(1),r.now);r.Pump();
    Check(r.nodes[0]->executed==2,"authority executes while first result is lost");
    r.Step(1100);Check(r.nodes[0]->executed==2,"application retry deduplicates after lost result");
    r.drop=99;r.Step(1100);Check(r.nodes[1]->session->Result().state==CommandNetworkState::completed,"cached result recovers over reliable route");

    r.Step();r.drop=1;r.nodes[1]->session->Submit(Make(),r.Name(1),r.now);r.Pump();
    auto spoof=Find(r,Kind::result,0);spoof.from=2;spoof.to=1;spoof.message.sequence=r.nodes[1]->session->Result().request;
    spoof.message.request.count=r.nodes[1]->session->Result().count;
    r.Deliver(spoof);Check(r.nodes[1]->session->Result().state==CommandNetworkState::pending,"other client cannot forge host result");
    r.Step(10001);Check(r.nodes[1]->session->Result().state==CommandNetworkState::timedOut,"missing acknowledgement has a real bounded RPC deadline");
    r.drop=99;r.Pump();Check(r.nodes[1]->session->Result().state==CommandNetworkState::timedOut,"late result cannot reopen a completed UI deadline");
    const auto count=r.nodes[0]->executed;++r.epoch;r.Step();r.Deliver(req);r.Pump();Check(r.nodes[0]->executed==count,"old mission request ignored even if id reused");
    Room old(2,false);Check(!old.nodes[1]->session->Ready() && !old.nodes[1]->session->Submit(Make(),old.Name(1),old.now),
        "old or disabled host capability cannot silently accept a command");
    Room rebound(2);rebound.nodes[1]->session->Submit(Make(),rebound.Name(1),rebound.now);rebound.Pump();rebound.Step();
    auto impostor=Find(rebound,Kind::request,1);++impostor.message.sequence;
    const auto packets=rebound.sent.size();
    rebound.nodes[0]->session->Receive(1,"different-authenticated-user",impostor.message,rebound.now);
    Check(rebound.nodes[0]->executed==1 && rebound.sent.size()==packets,"peer index cannot inherit another PUID's capability or result cache");
}
}
int main(){Codec();Version7();Negotiation();WaysMessages();Protocol();std::printf("command protocol: %d checks passed\n",checks);}
