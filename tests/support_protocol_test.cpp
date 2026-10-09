#include "../src/support_protocol.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>
#include <limits>

using namespace crew::support_net;
namespace {
int checks=0;
void Check(bool ok,const char* why) { ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);} }
struct Room;
struct Node {
    Room* room=nullptr;unsigned id=0;
    std::unique_ptr<Session> session=std::make_unique<Session>();
    bool reject=false,failSpawn=false,pending=false,refusePlan=false;
    unsigned spawns=0,destroys=0,plans=0;
    std::uint64_t nonce=0;
    bool active[kMaxTransactions+1]{};
    Plan last{};
    bool remote=false;
    std::vector<std::pair<std::uint32_t,RequestStatus>> notices;
};
Node* current=nullptr;
struct Packet { unsigned from,to;Message message; };
struct Room {
    std::vector<std::unique_ptr<Node>> nodes;
    std::deque<Packet> packets;
    std::vector<Packet> history;
    std::uint64_t now=10000;
    unsigned failTo=999,dropTo=999,legacyNode=999;   // legacyNode: an older build, its hello announces no capability
    Kind failKind=Kind::cancel,dropKind=Kind::cancel;
    explicit Room(unsigned count);
    unsigned Peer(unsigned node,unsigned global) const { return global<node ? global+1 : global; }
    unsigned Global(unsigned node,unsigned peer) const { return peer<=node ? peer-1 : peer; }
    void With(unsigned n) { current=nodes[n].get(); }
    void Pump() {
        unsigned guard=0;
        while(!packets.empty()) {
            Check(++guard<20000,"bounded protocol traffic");
            const auto p=packets.front();packets.pop_front();
            if(p.to==dropTo && p.message.kind==dropKind)continue;
            With(p.to);current->session->Receive(Peer(p.to,p.from),p.message,now);
        }
    }
    void Step(unsigned ms=100) {
        now+=ms;
        for(unsigned i=0;i<nodes.size();++i){With(i);current->session->Tick(now);}
        Pump();
    }
    void Settle() { for(int i=0;i<6;++i)Step(); }
    bool Submit(unsigned node=1,unsigned catalog=5) {
        const float at[3]={10,20,30};With(node);return current->session->Submit(catalog,at,now);
    }
    void Deliver(const Packet& p) { With(p.to);current->session->Receive(Peer(p.to,p.from),p.message,now); }
};
bool Send(void* ctx,std::uint32_t peer,const Message& message) noexcept {
    auto& node=*static_cast<Node*>(ctx);auto& room=*node.room;const auto dst=room.Global(node.id,peer);
    if(dst==room.failTo && message.kind==room.failKind)return false;
    unsigned char wire[kWireSize];Message decoded;
    Message sent=message;if(node.id==room.legacyNode && sent.kind==Kind::hello)sent.index=0;   // protocol v2 before capabilities
    if(!Encode(sent,wire,sizeof(wire)) || !Decode(wire,sizeof(wire),decoded))return false;
    Packet p{node.id,dst,decoded};room.packets.push_back(p);room.history.push_back(p);return true;
}
std::uint64_t Nonce(void* ctx) noexcept { return ++static_cast<Node*>(ctx)->nonce; }
PlanResult PlanCall(std::uint32_t catalog,const float* at,Plan* p) noexcept {
    ++current->plans;
    if(current->refusePlan)return PlanResult::refused;
    if(current->pending)return PlanResult::pending;
    *p=Plan{};p->catalogId=catalog;p->count=16;std::memcpy(p->target,at,sizeof(p->target));
    for(unsigned i=0;i<p->count;++i) {
        auto& u=p->units[i];u.resourceId=i%2+1;
        u.matrix[0]=u.matrix[5]=u.matrix[10]=u.matrix[15]=1;
        u.matrix[12]=at[0]+static_cast<float>(i);u.matrix[13]=at[1];u.matrix[14]=at[2];
    }
    return PlanResult::ready;
}
bool Validate(const Plan& p) noexcept {
    if(current->reject)return false;
    for(unsigned i=0;i<p.count;++i)if(p.units[i].resourceId>2 && p.units[i].resourceId!=kExistingVehicle)return false;
    return true;
}
bool Spawn(std::uint64_t id,const Plan& p,bool remote) noexcept {
    ++current->spawns;current->active[id]=true;current->last=p;current->remote=remote;
    return !current->failSpawn;
}
void Destroy(std::uint64_t id) noexcept { ++current->destroys;current->active[id]=false; }
bool Derive(std::uint32_t ordinal,unsigned char* out) noexcept {
    std::memset(out,0,32);std::memcpy(out+4,&ordinal,4);out[0]=42;out[12]=5;
    std::memcpy(out+24,&ordinal,4);return true;
}
void Notice(std::uint32_t request,RequestStatus status) noexcept { current->notices.emplace_back(request,status); }
Room::Room(unsigned count) {
    for(unsigned i=0;i<count;++i) {
        auto n=std::make_unique<Node>();n->id=i;n->room=this;n->nonce=100000+1000*i;
        Backend b{n.get(),&Send,&Nonce,{&PlanCall,&Validate,&Spawn,&Destroy,&Derive}};
        b.hooks.notice=&Notice;
        n->session->Configure(b);nodes.push_back(std::move(n));
    }
    for(unsigned i=0;i<count;++i){With(i);current->session->Start(i==0,count-1,i==0 ? 0 : Peer(i,0),now);}
    Settle();
}
Packet Find(const Room& r,Kind kind,unsigned to) {
    for(const auto& p:r.history)if(p.message.kind==kind && p.to==to)return p;
    Check(false,"packet exists");return {};
}
void Codec() {
    Message m;m.kind=Kind::unit;m.epoch=22;m.transaction=1;m.unit.resourceId=1;
    m.unit.matrix[0]=m.unit.matrix[5]=m.unit.matrix[10]=m.unit.matrix[15]=1;Derive(0x76543210,m.unit.netId);
    unsigned char bytes[kWireSize];std::memset(bytes,0xCC,sizeof(bytes));
    Check(Encode(m,bytes,sizeof(bytes)),"encode full high ordinal and matrix");
    Check(bytes[0]=='S' && bytes[1]=='P' && bytes[2]=='R' && bytes[3]=='T',"wire is explicitly little endian");
    Check(bytes[172]==0 && bytes[175]==0,"wire has no uninitialized padding");
    Message out;Check(Decode(bytes,sizeof(bytes),out) && !std::memcmp(out.unit.netId,m.unit.netId,32),"identity exact roundtrip");
    for(std::size_t n=0;n<sizeof(bytes);++n)Check(!Decode(bytes,n,out),"all truncations rejected");
    bytes[175]=1;Check(!Decode(bytes,sizeof(bytes),out),"reserved tail rejected");bytes[175]=0;
    m.unit.matrix[0]=std::numeric_limits<float>::quiet_NaN();Check(!Encode(m,bytes,sizeof(bytes)),"NaN cannot reach native spawn");
    m.unit.matrix[0]=2;Check(!Encode(m,bytes,sizeof(bytes)),"nonrigid matrix rejected");
    m.unit.matrix[0]=1;m.unit.netId[12]=4;Check(!Encode(m,bytes,sizeof(bytes)),"wrong native identity type rejected");
    m.unit.netId[12]=5;m.unit.netId[20]=1;Check(!Encode(m,bytes,sizeof(bytes)),"native padding cannot carry uninitialized memory");
}
// 2026-10-09 (review): configured weapons are new soldier resources an older peer's Validate refuses mid-transaction.
// Peers announce kCapSoldierVariants in their hello's index; the wire stays v2 both ways.
void Capabilities() {
    Message hello;hello.kind=Kind::hello;hello.challenge=7;hello.request=1;hello.index=kCapabilities;
    unsigned char bytes[kWireSize];Message out;
    Check(Encode(hello,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),out) && out.index==kCapabilities,
          "a capability hello round-trips on the unchanged v2 wire (an older host's same bounds accept it)");
    Check(bytes[4]==2 && bytes[5]==0,"wire version stays 2: older peers keep talking");
    {Room r(3);r.With(0);
     Check(r.nodes[0]->session->PeersHave(kCapSoldierVariants),"every new peer announced soldier variants");
     Check(r.nodes[0]->session->PeersHave(kCapSoldierVariants|kCapAirborneAir),"and airborne air support");
     r.With(1);Check(r.nodes[1]->session->PeersHave(kCapSoldierVariants),"a client is never asked: true");}
    {Room r(3);r.legacyNode=2;r.With(2);r.nodes[2]->session->Stop();r.nodes[2]->session->Start(false,2,1,r.now);r.Settle();
     Check(r.nodes[0]->session->Ready(),"an older client still completes the handshake");
     Check(!r.nodes[0]->session->PeersHave(kCapSoldierVariants),"host knows one peer lacks soldier variants");
     Check(!r.nodes[0]->session->PeersHave(kCapAirborneAir),"host knows one peer cannot make air support in the air");
     Check(r.Submit(),"mixed room request");r.Settle();
     for(const auto& n:r.nodes)Check(n->spawns==1,"mixed room: the v2 plan (rifles) passes the older peer's Validate");}
    {Room r(2);r.legacyNode=1;r.With(1);r.nodes[1]->session->Stop();r.nodes[1]->session->Start(false,1,1,r.now);r.Settle();
     r.legacyNode=999;r.Step(1100);r.Settle();
     Check(r.nodes[0]->session->PeersHave(kCapSoldierVariants),"a peer's latest hello decides (an updated peer rejoining)");}
}
void Success() {
    Room r(3);for(const auto& n:r.nodes)Check(n->session->Ready(),"authenticated handshake ready");
    Check(r.Submit(),"client submits catalog request");r.Settle();
    for(const auto& n:r.nodes)Check(n->spawns==1 && n->session->ActiveCount()==1,"all peers spawn once");
    Check(!r.nodes[0]->remote && r.nodes[1]->remote && r.nodes[2]->remote,"host authority passed to adapters");
    Check(!std::memcmp(&r.nodes[0]->last,&r.nodes[2]->last,sizeof(Plan)),"all sixteen object specs and native IDs identical");
    r.Deliver(Find(r,Kind::commit,1));r.Deliver(Find(r,Kind::begin,1));r.Deliver(Find(r,Kind::request,0));r.Settle();
    Check(r.nodes[1]->spawns==1 && r.nodes[0]->spawns==1,"replayed request/commit never duplicates native objects");
    auto forged=Find(r,Kind::commit,1);forged.from=2;forged.message.transaction=2;r.Deliver(forged);
    Check(r.nodes[1]->spawns==1,"client cannot impersonate authenticated host");
    r.With(2);r.nodes[2]->session->Failed(1);r.Settle();
    for(const auto& n:r.nodes)Check(!n->active[1] && n->session->ActiveCount()==0,"late deployment failure rolls back entire room");
    r.Deliver(Find(r,Kind::commit,1));Check(r.nodes[1]->spawns==1,"cancelled tombstone prevents resurrection");
}
void Failure() {
    {Room r(3);r.nodes[2]->reject=true;r.Submit();r.Settle();for(const auto& n:r.nodes)Check(n->spawns==0,"one missing resource prevents every spawn");}
    {Room r(3);r.nodes[2]->failSpawn=true;r.Submit();r.Settle();for(const auto& n:r.nodes)Check(!n->active[1],"partial native create failure rolls back all peers");}
    {Room r(3);r.failTo=2;r.failKind=Kind::unit;r.Submit();r.Settle();for(const auto& n:r.nodes)Check(n->spawns==0,"reliable queue failure cancels prepare");}
    {Room r(3);r.dropTo=2;r.dropKind=Kind::unit;r.Submit();r.Settle();for(const auto& n:r.nodes)Check(n->spawns==0,"missing unit cannot commit partial plan");}
    {Room r(3);r.dropTo=2;r.dropKind=Kind::commit;r.Submit();r.Settle();r.Step(21000);r.Settle();for(const auto& n:r.nodes)Check(!n->active[1],"missing result times out and destroys all copies");}
    {Room r(2);r.nodes[0]->pending=true;r.Submit();r.Settle();Check(r.nodes[0]->plans>1 && !r.nodes[0]->spawns,"incremental navigation stays pending");r.nodes[0]->pending=false;r.Settle();Check(r.nodes[0]->spawns==1,"pending navigation can complete");}
    {Room r(2);r.nodes[0]->pending=true;r.Submit();r.Settle();r.Step(30000);r.nodes[0]->pending=false;r.Settle();Check(r.nodes[0]->spawns==1,"terrain planner can take longer than network ACK deadline");}
    {Room r(2);r.nodes[0]->pending=true;r.Submit();r.Settle();r.Step(121000);r.nodes[0]->pending=false;r.Settle();Check(!r.nodes[0]->spawns,"terrain planner has finite 120-second deadline");}
    {Room r(3);r.Submit();r.Settle();r.failTo=2;r.failKind=Kind::cancel;
        r.With(0);r.nodes[0]->session->Failed(1);r.Settle();Check(r.nodes[2]->active[1],"fixture rejects initial cancel enqueue");
        r.failTo=999;r.Step(1100);r.Settle();for(const auto& n:r.nodes)Check(!n->active[1],"cancel retries until all peers acknowledge rollback");}
}
void Epoch() {
    Room r(3);r.Submit();r.Settle();
    const auto commit=Find(r,Kind::commit,1),hello=Find(r,Kind::hello,0),welcome=Find(r,Kind::welcome,1);
    const auto oldEpoch=r.nodes[0]->session->Epoch();
    for(unsigned i=0;i<3;++i) {
        r.With(i);r.nodes[i]->session->Stop();
        r.nodes[i]->session->Start(i==0,2,i==0 ? 0 : r.Peer(i,0),r.now);
    }
    r.Settle();
    Check(r.nodes[0]->session->Epoch()!=oldEpoch,"explicit mission reset rotates host epoch");
    for(const auto& n:r.nodes)Check(!n->active[1],"explicit mission reset releases previous support");
    const auto epoch=r.nodes[0]->session->Epoch();r.Deliver(hello);r.Deliver(welcome);r.Deliver(commit);r.Settle();
    Check(r.nodes[0]->session->Epoch()==epoch && r.nodes[1]->session->Epoch()==epoch,"stale hello/welcome cannot roll back epoch");
    Check(r.nodes[1]->spawns==1,"previous level commit never respawns");
    r.Step(2500);Check(r.Submit(),"new epoch accepts request");r.Settle();
    r.With(0);r.nodes[0]->session->Failed(1);r.Settle();
    Check(r.nodes[0]->active[2] && r.nodes[0]->session->IsActive(2),"old asynchronous failure token cannot cancel a new mission transaction");
    const auto& previous=commit.message;static_cast<void>(previous);
    Check(r.nodes[0]->last.units[0].netId[4]==16,"native ordinals not reused after epoch reset");
    for(unsigned i=0;i<3;++i){r.With(i);r.nodes[i]->session->Stop();Check(!r.nodes[i]->active[2],"explicit mission teardown removes support");}
}
void ActivationAndTransport() {
    Room r(3);r.dropTo=0;r.dropKind=Kind::result;r.Submit();r.Settle();
    for(const auto& n:r.nodes)Check(n->spawns==1 && !n->session->IsActive(1),"native seating gate stays closed before all spawn results");
    r.dropTo=999;
    for(const auto& p:r.history)if(p.message.kind==Kind::result && p.message.ok)r.Deliver(p);
    r.failTo=2;r.failKind=Kind::activate;r.Settle();
    Check(r.nodes[0]->session->IsActive(1) && r.nodes[1]->session->IsActive(1) && !r.nodes[2]->session->IsActive(1),
        "only host all-peer finalize activates a client");
    r.failTo=999;r.Step(1100);r.Settle();Check(r.nodes[2]->session->IsActive(1),"failed finalize enqueue is retried until acknowledgement");
    r.With(0);r.nodes[0]->session->Start(true,3,0,r.now);
    Check(r.nodes[0]->active[1] && r.nodes[0]->session->IsActive(1) && !r.nodes[0]->session->Ready(),
        "roster rebind preserves active actor and blocks requests instead of clearing ledger");
    r.nodes[0]->session->Failed(1);Check(r.nodes[0]->active[1],"transport suspension cannot perform unilateral actor rollback");
    r.With(1);r.nodes[1]->session->Suspend();Check(r.nodes[1]->active[1],"quiet link or lobby churn preserves delivered actors");
}
void Existing() {
    Room r(3);Plan p;p.catalogId=kMissionCrewCatalog;p.count=2;
    for(unsigned i=0;i<2;++i)p.units[i].matrix[0]=p.units[i].matrix[5]=p.units[i].matrix[10]=p.units[i].matrix[15]=1;
    p.units[0].resourceId=kExistingVehicle;p.units[0].netId[0]=66;p.units[0].netId[12]=1;
    p.units[1].resourceId=1;p.units[1].role=1;
    r.With(1);Check(!r.nodes[1]->session->SubmitPrepared(p,r.now),"client cannot submit arbitrary existing vehicle identity");
    Check(!r.Submit(1,kMissionCrewCatalog),"reserved internal catalog excluded from client requests");
    r.With(0);Check(r.nodes[0]->session->SubmitPrepared(p,r.now)==1,"host submits registered mission vehicle migration");r.Settle();
    for(const auto& n:r.nodes) {
        Check(n->spawns==1 && n->last.units[0].netId[0]==66 && n->last.units[0].netId[12]==1,"existing vehicle identity preserved on every peer");
        Check(n->last.units[1].netId[12]==5 && n->last.units[1].role==1,"new real crew gets derived identity and vehicle reference");
    }
    r.With(0);Check(!r.nodes[0]->session->SubmitPrepared(p,r.now),"same registered vehicle cannot receive duplicate crew transaction");
}
void RequestOutcomes() {
    Room r(3);r.nodes[0]->refusePlan=true;Check(r.Submit(),"client request queued before host environment refusal");
    Packet forged{2,1,{}};forged.message.kind=Kind::requestStatus;forged.message.epoch=r.nodes[1]->session->Epoch();
    forged.message.request=1;forged.message.index=static_cast<std::uint32_t>(RequestStatus::refused);
    r.Deliver(forged);Check(r.nodes[1]->notices.size()==1 && r.nodes[1]->notices.back().second==RequestStatus::accepted,
        "another client cannot forge a refusal");
    r.Settle();
    Check(r.nodes[1]->spawns==0 && r.nodes[1]->notices.size()==2 &&
        r.nodes[1]->notices.back()==std::make_pair(1u,RequestStatus::refused),"pre-begin refusal has an authenticated correlated terminal outcome");
    Check(r.nodes[0]->notices.empty() && r.nodes[2]->notices.empty(),"remote planner outcome does not overwrite host or unrelated client UI");
    Packet refused{};
    for(const auto& p:r.history)if(p.to==1 && p.message.kind==Kind::requestStatus &&
        p.message.index==static_cast<std::uint32_t>(RequestStatus::refused))refused=p;
    r.Deliver(refused);r.Deliver(refused);r.Deliver(Find(r,Kind::request,0));r.Pump();
    Check(r.nodes[1]->notices.size()==2,"duplicate terminal replies and replayed request notify once");
    for(unsigned i=0;i<3;++i) {
        r.With(i);r.nodes[i]->session->Stop();r.nodes[i]->session->Start(i==0,2,i==0 ? 0 : r.Peer(i,0),r.now);
    }
    r.Settle();r.nodes[0]->refusePlan=false;r.nodes[0]->pending=true;r.Step(2500);r.Submit();
    const auto notices=r.nodes[1]->notices.size();r.Deliver(refused);
    Check(r.nodes[1]->notices.size()==notices && r.nodes[1]->notices.back().second==RequestStatus::accepted,
        "old epoch refusal cannot terminate new epoch request with the same request number");
    r.Settle();r.Step(121000);r.Settle();
    Check(r.nodes[1]->notices.back().second==RequestStatus::timeout,"host planning deadline produces real timeout status, not arbitrary UI timer");

    Room newer(2);newer.Submit();newer.Settle();newer.Step(2500);newer.Submit();
    newer.With(0);newer.nodes[0]->session->Failed(1);newer.Settle();
    Check(newer.nodes[1]->notices.back()==std::make_pair(2u,RequestStatus::active),"previous transaction cancel cannot be misreported as the newer request outcome");
}
}
int main() { Codec();Capabilities();Success();Failure();Epoch();Existing();ActivationAndTransport();RequestOutcomes();std::printf("support protocol: %d checks passed\n",checks); }
