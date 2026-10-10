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
struct Node {Room* room;unsigned index;std::unique_ptr<Session> session=std::make_unique<Session>();unsigned executed=0;const char* sender=nullptr;};
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
    Packet p{n.index,r.Global(n.index,peer),decoded};r.queue.push_back(p);r.sent.push_back(p);return true;
}
void Execute(void* ptr,const char* sender,const Request& r,Message::Result* out) noexcept {
    auto& n=*static_cast<Node*>(ptr);++n.executed;n.sender=sender;
    for(unsigned i=0;i<r.count;++i)out[i]={static_cast<unsigned>(i==1 ? NpcCommandReason::notOwner : NpcCommandReason::none),i==1 ? 0u : 4u};
}
Room::Room(unsigned count,bool capable) {
    for(unsigned i=0;i<count;++i){auto n=std::make_unique<Node>();n->room=this;n->index=i;n->session->Configure({n.get(),&Send,(i==0 && !capable) ? nullptr : &Execute});nodes.push_back(std::move(n));}
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
    Check(static_cast<int>(mapcmd::Order::dismountAll)==12 && mapcmd::kLastOrder==mapcmd::Order::dismountAll,
          "DISMOUNT ALL is appended (12): the older orders keep their wire values");
    Check(kVersion==5,"the wire is version 5 (DISMOUNT ALL): an older peer refuses it instead of misreading it");
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
        Check(Decode(edit,sizeof(edit),out) && out.request.command.order==mapcmd::kLastOrder,"the last order (withdraw) decodes");
        std::memcpy(edit+kOrderAt,&past,4);
        Check(!Decode(edit,sizeof(edit),out),"an order past the last one is not decoded");
    }
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
int main(){Codec();Protocol();std::printf("command protocol: %d checks passed\n",checks);}
