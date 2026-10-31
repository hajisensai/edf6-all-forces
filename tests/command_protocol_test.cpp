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
    explicit Room(unsigned count=3,bool capable=true);
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
Room::Room(unsigned count,bool capable) {
    for(unsigned i=0;i<count;++i){
        auto n=std::make_unique<Node>();n->room=this;n->index=i;n->session->Configure({n.get(),&Send,(i==0 && !capable) ? nullptr : &Execute});
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
    Check(kVersion==8 && kMinVersion==8 && Compatible(8) && !Compatible(7) && !Compatible(6) && !Compatible(9),
          "the wire is version 8 (the requester its mission slot); no older version spoken");
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
// The Shift queue's flag and the formation order, in the word after `kind` and the one after the point. A build before
// version 8 (its requester a NetId, which a stock mission numbers per machine) is neither read nor written to.
void Version8() {
    Message m;m.kind=Kind::request;m.epoch=7;m.sequence=4;m.request=Make(2,mapcmd::Order::move);m.request.flags=kQueued;
    m.request.formationTotal=2;m.request.formationSlots[0]=0;m.request.formationSlots[1]=1;
    unsigned char bytes[kWireSize];Message copy;
    Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),copy) && copy.version==kVersion && copy.request.flags==kQueued,
          "a queued move round trips");
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
    m=Message{};m.kind=Kind::request;m.epoch=7;m.sequence=5;m.request=Make(1,mapcmd::Order::hold);
    Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),copy),"a HOLD round trips");
    for(std::uint32_t old:{6u,7u}) {
        unsigned char edit[kWireSize];std::memcpy(edit,bytes,sizeof(edit));std::memcpy(edit+4,&old,4);
        Check(Owned(edit,sizeof(edit)) && !Decode(edit,sizeof(edit),copy),"an older build's request is consumed, never read");
    }
    m.version=7;Check(!Encode(m,bytes,sizeof(bytes)),"nothing goes out at an older version");
    Check(ValidRequest(Make(1,mapcmd::Order::move)) && !ValidRequest(Make(1,mapcmd::Order::move),7),"a request travels at version 8 alone");
}
// The negotiation: every hello at kMinVersion with the sender's highest in it; an older build is never answered.
void Negotiation() {
    {   // queued and formation orders reach the executor with their flag and parameter
        Room r;Check(r.nodes[1]->session->HostVersion()==kVersion,"both sides: version 8");
        Request q=Make(1,mapcmd::Order::attackMove);q.flags=kQueued;
        Check(r.nodes[1]->session->Submit(q,r.Name(1),r.now)!=0,"a queued attack-move submitted");r.Pump();
        Check(r.nodes[0]->executed==1 && r.nodes[0]->lastFlags==kQueued && r.nodes[0]->lastOrder==mapcmd::Order::attackMove,
              "the host's executor gets the queued flag (its authority keeps the point)");
        r.Step();Request f=Make(1,mapcmd::Order::formation);f.command.param=mapcmd::FormationParam(-2,3);
        Check(r.nodes[1]->session->Submit(f,r.Name(1),r.now)!=0,"a formation order submitted");r.Pump();
        Check(r.nodes[0]->executed==2 && r.nodes[0]->lastParam==f.command.param,"the host's executor gets the formation parameter");
        for(const auto& p:r.sent)if(p.message.kind==Kind::hello)Check(p.message.version==kMinVersion && p.message.request.formationTotal==kVersion,
            "a hello goes out at the lowest version, the sender's highest in it");
    }
    {   // an older build (version 7 and before: its requester a NetId): no capability for its hello, its requests not run
        Room r(2);const auto before=r.sent.size();
        Message hello;hello.version=7;hello.epoch=r.epoch;hello.request.formationTotal=7;
        r.nodes[0]->session->Receive(1,r.Name(1),hello,r.now);r.Pump();
        Check(r.sent.size()==before,"an older build's hello: not answered");
        Message forged;forged.version=7;forged.kind=Kind::request;forged.epoch=r.epoch;forged.sequence=9;forged.request=Make(1,mapcmd::Order::move);
        r.nodes[0]->session->Receive(1,r.Name(1),forged,r.now);r.Pump();
        Check(r.nodes[0]->executed==0,"an older build's request is not executed");
    }
}
// Kind::ways: the host's Shift queues for the clients' maps -- sent to every capable peer when they change, all of them
// again to a peer saying hello, cleared when gone; newest revision wins.
void WaysMessages() {
    Message m;m.kind=Kind::ways;m.epoch=7;m.sequence=3;m.request.count=1;m.request.units[0][0]=9;m.ways.n=2;
    m.ways.order=static_cast<std::uint32_t>(mapcmd::Order::move);m.ways.current[0]=7;m.ways.current[2]=-2;
    m.ways.at[0][0]=1;m.ways.at[1][2]=-4;
    unsigned char bytes[kWireSize];Message copy;
    Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),copy) && copy.kind==Kind::ways && copy.ways.n==2 &&
          copy.ways.at[1][2]==-4 && !copy.request.formationTotal,"a unit's queue round trips");
    Check(copy.ways.order==m.ways.order && copy.ways.current[0]==7 && copy.ways.current[2]==-2,"with its current order and point");
    m.ways.order=static_cast<std::uint32_t>(mapcmd::Order::follow);Check(!Encode(m,bytes,sizeof(bytes)),"its current order is a point order");
    m.ways.order=static_cast<std::uint32_t>(mapcmd::Order::move);
    {Message clear=m;clear.ways=command_net::Ways{};Check(Encode(clear,bytes,sizeof(bytes)),"a clearing: no order, no point");
     clear.ways.current[0]=1;Check(!Encode(clear,bytes,sizeof(bytes)),"a clearing carries no current point");}
    m.version=7;Check(!Encode(m,bytes,sizeof(bytes)),"no ways message at an older version");m.version=kVersion;
    m.ways.n=9;Check(!Encode(m,bytes,sizeof(bytes)),"at most eight points");m.ways.n=1;
    Check(!Encode(m,bytes,sizeof(bytes)),"a point past the count must be zero");m.ways.n=2;
    m.ways.at[0][1]=std::numeric_limits<float>::quiet_NaN();Check(!Encode(m,bytes,sizeof(bytes)),"no NaN point");m.ways.at[0][1]=0;
    m.sequence=0;Check(!Encode(m,bytes,sizeof(bytes)),"a revision is required");

    Room r(3);
    const unsigned char unit[32]={5};const float pts[2][3]={{10,0,1},{20,0,2}};
    const mapcmd::Command now{mapcmd::Order::attackMove,{3,0,4}},moved{mapcmd::Order::move,{3,0,4}};
    const auto waysTo=[&](unsigned to){unsigned n=0;for(const auto& p:r.sent)if(p.to==to && p.message.kind==Kind::ways)++n;return n;};
    r.nodes[0]->session->PublishWays(unit,now,pts,2);r.Pump();
    command_net::Ways got;
    Check(r.nodes[1]->session->WaysOf(unit,&got) && got.n==2 && got.at[1][0]==20,"a client gets the host's queue");
    Check(got.order==static_cast<std::uint32_t>(mapcmd::Order::attackMove) && got.current[2]==4,"...and the unit's current order and point");
    Check(waysTo(2)==1 && r.nodes[2]->session->WaysOf(unit,&got),"so does every other client");
    const auto sent=r.sent.size();r.nodes[0]->session->PublishWays(unit,now,pts,2);r.Pump();
    Check(r.sent.size()==sent,"unchanged: not sent again");
    r.nodes[0]->session->PublishWays(unit,moved,pts,2);r.Pump();
    Check(r.sent.size()>sent && r.nodes[1]->session->WaysOf(unit,&got) && got.order==static_cast<std::uint32_t>(mapcmd::Order::move),
          "only its current order changed: sent");
    {const auto before=r.sent.size();r.nodes[0]->session->PublishWays(unit,mapcmd::Command{mapcmd::Order::follow,{}},pts,2);r.Pump();
     Check(r.sent.size()==before,"a queue whose current order is no point order: not published");}
    // A peer saying hello again (a reconnect, a new member) is sent every queue out now.
    const auto before=waysTo(1);
    Message hello;hello.version=kMinVersion;hello.epoch=r.epoch;hello.request.formationTotal=kVersion;
    r.nodes[0]->session->Receive(1,r.Name(1),hello,r.now);r.Pump();
    Check(waysTo(1)==before+1,"hello: the queues out sent to it again");
    unsigned char out[4][32];Check(r.nodes[0]->session->PublishedWays(out,4)==1,"the host lists what is out");
    r.nodes[0]->session->PublishWays(unit,mapcmd::Command{},nullptr,0);r.Pump();
    Check(!r.nodes[1]->session->WaysOf(unit,&got) && !r.nodes[0]->session->PublishedWays(out,4),"cleared on both sides");
    // An older revision arriving late does not undo a newer one.
    r.nodes[0]->session->PublishWays(unit,now,pts,1);r.Pump();
    Message late;for(const auto& p:r.sent)if(p.to==1 && p.message.kind==Kind::ways && p.message.ways.n==2)late=p.message;
    r.nodes[1]->session->Receive(1,r.Name(0),late,r.now);
    Check(r.nodes[1]->session->WaysOf(unit,&got) && got.n==1,"an older revision is ignored");
    // A client cannot publish; another client cannot forge the host's.
    r.nodes[1]->session->PublishWays(unit,now,pts,2);r.Pump();
    Check(r.nodes[1]->session->WaysOf(unit,&got) && got.n==1,"a client publishes nothing");
    ++r.epoch;r.Step();
    Check(!r.nodes[1]->session->WaysOf(unit,&got),"a new mission forgets them");
}
// A client's short drop in the same epoch (2026-10-20 review): the host clears a queue while the client is away (the
// clearing is never heard), the client comes back: the host's capability forgets what it kept, the queues still out are
// sent again, the cleared one is drawn no more.
void WaysReconnect() {
    Room r(2);
    const unsigned char gone[32]={7},kept[32]={8};const float pts[2][3]={{10,0,1},{20,0,2}};
    const mapcmd::Command now{mapcmd::Order::move,{3,0,4}};
    r.nodes[0]->session->PublishWays(gone,now,pts,2);r.nodes[0]->session->PublishWays(kept,now,pts,1);r.Pump();
    command_net::Ways got;
    Check(r.nodes[1]->session->WaysOf(gone,&got) && r.nodes[1]->session->WaysOf(kept,&got),"both queues drawn on the client");
    // The client drops: not ready (its transport down), the epoch the same.
    r.now+=300;r.nodes[1]->session->Update(false,false,r.epoch,1,1,r.now);
    r.nodes[0]->session->PublishWays(gone,mapcmd::Command{},nullptr,0);r.Pump();   // cleared while it is away: unheard
    Check(r.nodes[1]->session->WaysOf(gone,&got),"the clearing was not heard (the client was down)");
    r.Step(1100);   // back: hello, the host's capability, then every queue out
    Check(r.nodes[1]->session->Ready(),"the client is ready again");
    Check(!r.nodes[1]->session->WaysOf(gone,&got),"the queue the host cleared while it was away is gone");
    Check(r.nodes[1]->session->WaysOf(kept,&got) && got.n==1,"the queue still out is drawn again, from the resend");
    // A later change still applies (the revisions the host keeps go on from the resend).
    r.nodes[0]->session->PublishWays(kept,now,pts,2);r.Pump();
    Check(r.nodes[1]->session->WaysOf(kept,&got) && got.n==2,"a later change after the reconnect applies");
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
int main(){Codec();Version8();Negotiation();WaysMessages();WaysReconnect();Protocol();std::printf("command protocol: %d checks passed\n",checks);}
