#include "../src/net_object_ref.h"
#include "../src/support_protocol.h"
#include "../src/support_loadout.h"
#include "../src/support_submarine.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <memory>
#include <vector>
#include <limits>
#include <unordered_map>

using namespace crew::support_net;
namespace {
constexpr std::uint32_t kRescue=7;   // the fixture's sea rescue catalog (Hooks::ownChannel)
int checks=0;
void Check(bool ok,const char* why) { ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);} }
// Plan has ABI padding after target and team. Compare every serialized/adapter field bit-exactly;
// padding is not a wire property, while all 32 bytes of every canonical native ID remain part of it.
bool SamePlanSpecs(const Plan& a,const Plan& b) noexcept {
    if(a.catalogId!=b.catalogId || a.count!=b.count || std::memcmp(a.target,b.target,sizeof(a.target)) ||
       std::memcmp(a.requester,b.requester,sizeof(a.requester)) || a.team!=b.team)return false;
    for(std::uint32_t i=0;i<kMaxUnits;++i) {
        const auto& x=a.units[i];const auto& y=b.units[i];
        if(x.resourceId!=y.resourceId || x.role!=y.role || x.variant!=y.variant ||
           std::memcmp(x.matrix,y.matrix,sizeof(x.matrix)) || std::memcmp(x.netId,y.netId,sizeof(x.netId)))return false;
    }
    return true;
}
void PlanSpecComparison() {
    Plan a{},b{};
    auto* left=reinterpret_cast<unsigned char*>(&a);auto* right=reinterpret_cast<unsigned char*>(&b);
    for(std::size_t offset=offsetof(Plan,target)+sizeof(a.target);offset<offsetof(Plan,units);++offset){left[offset]=0xA5;right[offset]=0x5A;}
    for(std::size_t offset=offsetof(Plan,team)+sizeof(a.team);offset<sizeof(Plan);++offset){left[offset]=0xA5;right[offset]=0x5A;}
    Check(SamePlanSpecs(a,b),"different ABI padding cannot create a false wire/native-ID mismatch");
    b=a;b.catalogId=1;Check(!SamePlanSpecs(a,b),"plan comparison checks catalog");
    b=a;b.count=1;Check(!SamePlanSpecs(a,b),"plan comparison checks count");
    b=a;b.team=1;Check(!SamePlanSpecs(a,b),"plan comparison checks team");
    for(unsigned i=0;i<3;++i){b=a;b.target[i]=1;Check(!SamePlanSpecs(a,b),"plan comparison checks every target coordinate");}
    for(unsigned i=0;i<32;++i){b=a;b.requester[i]=1;Check(!SamePlanSpecs(a,b),"plan comparison checks all 32 requester identity bytes");}
    for(unsigned i=0;i<kMaxUnits;++i) {
        b=a;b.units[i].resourceId=1;Check(!SamePlanSpecs(a,b),"plan comparison checks every unit resource");
        b=a;b.units[i].role=1;Check(!SamePlanSpecs(a,b),"plan comparison checks every unit role");
        for(unsigned j=0;j<8;++j){b=a;b.units[i].variant=std::uint64_t{1}<<(j*8);Check(!SamePlanSpecs(a,b),"plan comparison checks all 64 variant bits");}
        for(unsigned j=0;j<16;++j){b=a;b.units[i].matrix[j]=1;Check(!SamePlanSpecs(a,b),"plan comparison checks all 16 transform components of all 16 units");}
        for(unsigned j=0;j<32;++j){b=a;b.units[i].netId[j]=1;Check(!SamePlanSpecs(a,b),"plan comparison checks all 32 native identity bytes of all 16 units");}
    }
}
struct Room;
struct Node {
    Room* room=nullptr;unsigned id=0;
    std::unique_ptr<Session> session=std::make_unique<Session>();
    bool reject=false,failSpawn=false,pending=false,refusePlan=false,allowDynamic=true,failDerive=false;
    unsigned validations=0,rejectValidationAt=0;
    std::uint32_t pendingCatalog=0;   // pending only for this catalog (0: every one)
    unsigned spawns=0,destroys=0,plans=0,requesterPlans=0,requesterPeer=999;
    unsigned char requester[32]{};std::uint32_t team=0;
    std::uint64_t loadout=0;   // the last plan call's composed load (support_call.h PackSupportLoadout)
    std::uint64_t variant=0;   // given to every planned unit (support_net.h Unit::variant)
    std::uint32_t ext=kExtVariants;unsigned char bloom[32]{};   // its hello's extension (support_variants.h)
    std::uint64_t nonce=0;
    std::uint64_t lastToken=0;
    std::unordered_map<std::uint64_t,bool> active;
    Plan last{};
    bool remote=false;
    std::vector<std::pair<std::uint32_t,RequestStatus>> notices;
    std::vector<std::uint64_t> destroyedTokens;
    std::vector<std::string> refusals;   // the host's causes (Hooks::refused)
};
Node* current=nullptr;
struct Packet { unsigned from,to;Message message; };
struct Room {
    std::vector<std::unique_ptr<Node>> nodes;
    std::deque<Packet> packets;
    std::vector<Packet> history;
    std::uint64_t now=10000;
    unsigned failTo=999,dropTo=999,legacyNode=999;   // legacyNode: an older build, its hello announces legacyCaps only
    std::uint32_t legacyCaps=0,legacyExt=0,legacyDetails=0;
    bool oldHost=false;   // the host's welcome as a build before it carried its capabilities (index 0)
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
    Message sent=message;
    // An older (or newer) build's hello: its capabilities and its extension word as that build sends them (a build before
    // the extension: 0 and an empty filter).
    if(node.id==room.legacyNode && sent.kind==Kind::hello){sent.index=room.legacyCaps;sent.unit=Unit{};sent.unit.resourceId=room.legacyExt;sent.unit.role=room.legacyDetails;}
    if(node.id==0 && room.oldHost && sent.kind==Kind::welcome){sent.catalog=0;sent.ok=0;sent.unit.role=0;sent.unit.resourceId=0;}
    if(node.id==room.legacyNode && !(room.legacyDetails&kDetailTransactionGenerations) &&
       sent.kind==Kind::result && !sent.ok && sent.index==kCancelAckIndex)sent.index=0;
    if(!Encode(sent,wire,sizeof(wire)) || !Decode(wire,sizeof(wire),decoded))return false;
    Packet p{node.id,dst,decoded};room.packets.push_back(p);room.history.push_back(p);return true;
}
std::uint64_t Nonce(void* ctx) noexcept { return ++static_cast<Node*>(ctx)->nonce; }
PlanResult PlanCall(std::uint32_t catalog,const float* at,std::uint64_t loadout,Plan* p) noexcept {
    ++current->plans;current->loadout=loadout;
    if(current->refusePlan)return PlanResult::refused;
    if(current->pending && (!current->pendingCatalog || current->pendingCatalog==catalog))return PlanResult::pending;
    *p=Plan{};p->catalogId=catalog;p->count=16;std::memcpy(p->target,at,sizeof(p->target));
    std::memcpy(p->requester,current->requester,sizeof(p->requester));p->team=current->team;
    for(unsigned i=0;i<p->count;++i) {
        auto& u=p->units[i];u.resourceId=i%2+1;u.variant=current->variant;
        u.matrix[0]=u.matrix[5]=u.matrix[10]=u.matrix[15]=1;
        u.matrix[12]=at[0]+static_cast<float>(i);u.matrix[13]=at[1];u.matrix[14]=at[2];
    }
    return PlanResult::ready;
}
PlanResult PlanForRequester(std::uint32_t requester,std::uint32_t catalog,const float* at,std::uint64_t loadout,Plan* p) noexcept {
    ++current->requesterPlans;current->requesterPeer=requester;
    return PlanCall(catalog,at,loadout,p);
}
bool Validate(const Plan& p) noexcept {
    ++current->validations;
    if(current->reject || (current->rejectValidationAt && current->validations>=current->rejectValidationAt))return false;
    for(unsigned i=0;i<p.count;++i)if(p.units[i].resourceId>2 && p.units[i].resourceId!=kExistingVehicle) {
        if(!current->allowDynamic || p.count!=1 || p.catalogId!=kPreparedDroneCatalog || p.units[i].role || !p.units[i].variant ||
           !p.requester[0] || (p.units[i].resourceId!=crew::kSupportSubDroneResource && !crew::IsCarrierDroneResource(p.units[i].resourceId)))return false;
    }
    return true;
}
bool Spawn(std::uint64_t id,const Plan& p,bool remote) noexcept {
    ++current->spawns;current->active[id]=true;current->last=p;current->remote=remote;current->lastToken=id;
    return !current->failSpawn;
}
void Destroy(std::uint64_t id) noexcept { ++current->destroys;current->active[id]=false;current->destroyedTokens.push_back(id); }
bool Derive(std::uint32_t ordinal,unsigned char* out) noexcept {
    std::memset(out,0,32);std::memcpy(out+4,&ordinal,4);out[0]=42;out[12]=5;
    std::memcpy(out+24,&ordinal,4);return true;
}
bool DeriveForSession(std::uint32_t ordinal,unsigned char* out) noexcept {return !current->failDerive && Derive(ordinal,out);}
void Notice(std::uint32_t request,RequestStatus status) noexcept { current->notices.emplace_back(request,status); }
void Hello(std::uint32_t* ext,unsigned char* bloom) noexcept { *ext=current->ext;std::memcpy(bloom,current->bloom,32); }
void Refused(std::uint32_t,std::uint32_t,const char* why) noexcept { current->refusals.emplace_back(why ? why : ""); }
void RequesterPlanner(Node& node,bool only=false) {
    Backend b{&node,&Send,&Nonce,{&PlanCall,&Validate,&Spawn,&Destroy,&DeriveForSession}};
    b.hooks.notice=&Notice;b.hooks.variants=&Hello;
    b.hooks.ownChannel=[](std::uint32_t catalog) noexcept {return catalog==kRescue;};
    b.hooks.planForRequester=&PlanForRequester;if(only)b.hooks.plan=nullptr;node.session->Configure(b);
}
Room::Room(unsigned count) {
    for(unsigned i=0;i<count;++i) {
        auto n=std::make_unique<Node>();n->id=i;n->room=this;n->nonce=100000+1000*i;
        Backend b{n.get(),&Send,&Nonce,{&PlanCall,&Validate,&Spawn,&Destroy,&DeriveForSession}};
        b.hooks.notice=&Notice;b.hooks.variants=&Hello;b.hooks.refused=&Refused;
        b.hooks.ownChannel=[](std::uint32_t catalog) noexcept {return catalog==kRescue;};
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
    m=Message{};m.kind=Kind::result;m.epoch=22;m.transaction=1;m.index=kCancelAckIndex;
    Check(Encode(m,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),out) && out.index==kCancelAckIndex && !out.ok,"explicit cancellation ACK marker round-trips on unchanged v2 wire");
    m.ok=1;Check(!Encode(m,bytes,sizeof(bytes)),"a positive creation RESULT cannot masquerade as explicit cancellation ACK");
    m.ok=0;m.index=kCancelAckIndex-1;Check(!Encode(m,bytes,sizeof(bytes)),"undefined RESULT markers are rejected");
}
// 2026-10-09 (review): configured weapons are new soldier resources an older peer's Validate refuses mid-transaction.
// Peers announce kCapSoldierVariants in their hello's index; the wire stays v2 both ways.
void Capabilities() {
    Message hello;hello.kind=Kind::hello;hello.challenge=7;hello.request=1;hello.index=kCapabilities;
    unsigned char bytes[kWireSize];Message out;
    Check(Encode(hello,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),out) && out.index==kCapabilities,
          "a capability hello round-trips on the unchanged v2 wire (an older host's same bounds accept it)");
    Check(bytes[4]==2 && bytes[5]==0,"wire version stays 2: older peers keep talking");
    Message welcome;welcome.kind=Kind::welcome;welcome.epoch=5;welcome.challenge=7;welcome.request=1;welcome.catalog=kCapabilities;welcome.ok=1;
    Check(Encode(welcome,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),out) && out.catalog==kCapabilities && out.ok==1,
          "a welcome with the host's capabilities passes the unchanged bounds an older guest checks");
    {Room r(3);r.With(0);
     Check(r.nodes[0]->session->PeersHave(kCapSoldierVariants),"every new peer announced soldier variants");
     Check(r.nodes[0]->session->PeersHave(kCapSoldierVariants|kCapAirborneAir),"and airborne air support");
     Check(r.nodes[0]->session->PeersHaveExtension(kExtSeaRescue) && (kExtensions&kExtSeaRescue),"and the sea rescue entry");
     r.With(1);Check(r.nodes[1]->session->PeersHave(kCapSoldierVariants),"a client is never asked: true");}
    {Room r(3);r.legacyNode=2;r.With(2);r.nodes[2]->session->Stop();r.nodes[2]->session->Start(false,2,1,r.now);r.Settle();
     Check(r.nodes[0]->session->Ready(),"an older client still completes the handshake");
     Check(!r.nodes[0]->session->PeersHave(kCapSoldierVariants),"host knows one peer lacks soldier variants");
     Check(!r.nodes[0]->session->PeersHave(kCapAirborneAir),"host knows one peer cannot make air support in the air");
     Check(!r.nodes[0]->session->PeersHaveExtension(kExtSeaRescue),"host knows one peer has no sea rescue entry (its plan is refused there)");
     Check(r.Submit(),"mixed room request");r.Settle();
     for(const auto& n:r.nodes)Check(n->spawns==1,"mixed room: the v2 plan (rifles) passes the older peer's Validate");}
    // The build just before the sea rescue entry (soldier variants and airborne air, no rescue): every air call still
    // plans, the rescue alone is refused there (support_dispatch.cpp SupportPeersAcceptRescue).
    // (#100's: every capability, the variants extension, no rescue extension.)
    {Room r(3);r.legacyNode=2;r.legacyCaps=kCapabilities;r.legacyExt=kExtVariants;r.With(2);r.nodes[2]->session->Stop();
     r.nodes[2]->session->Start(false,2,1,r.now);r.Settle();
     Check(r.nodes[0]->session->PeersHave(kCapabilities) && r.nodes[0]->session->PeersHaveVariant(kExtVariants,0) &&
           !r.nodes[0]->session->PeersHaveExtension(kExtSeaRescue),
           "a peer one version older: air support, transports, loads and variants yes, the sea rescue no");}
    // The rescue's bit is the catalog's, announced whatever the variants hook says (a hook that reports no variants).
    {Room r(3);r.nodes[2]->ext=0;r.Step(1100);r.Settle();
     Check(r.nodes[0]->session->PeersHaveExtension(kExtSeaRescue) && !r.nodes[0]->session->PeersHaveVariant(kExtVariants,0),
           "every hello of this build announces the sea rescue; the variants only by the hook");}
    static_assert((kCapTransports|kCapLoadout)==12u && kExtSeaRescue==2u && (kExtensions&kExtVariants),
                  "#100's capability bits keep their values; the rescue rides the extension word");
    {Room r(2);r.legacyNode=1;r.With(1);r.nodes[1]->session->Stop();r.nodes[1]->session->Start(false,1,1,r.now);r.Settle();
     r.legacyNode=999;r.Step(1100);r.Settle();
     Check(r.nodes[0]->session->PeersHave(kCapSoldierVariants),"a peer's latest hello decides (an updated peer rejoining)");}
}
void Loadout() {
    // A composed load (support_call.h PackSupportLoadout) rides in the request's challenge: the host's plan sees it; an
    // older host never reads that field of a request (the wire is v2 unchanged).
    Room r(3);const float at[3]={10,20,30};const std::uint64_t load=0x0000000DDDD99998ull;
    r.With(1);Check(current->session->Submit(5,at,r.now,load),"a guest submits a composed request");r.Settle();
    Check(r.nodes[0]->loadout==load && r.nodes[0]->plans>=1,"the host plans with the guest's load");
    for(const auto& n:r.nodes)Check(n->spawns==1,"every peer spawns the composed plan once");
    Message m;m.kind=Kind::request;m.epoch=1;m.request=1;m.catalog=5;m.challenge=load;std::memcpy(m.target,at,12);
    unsigned char wire[kWireSize];Message back;
    Check(Encode(m,wire,sizeof(wire)) && Decode(wire,sizeof(wire),back) && back.challenge==load,"the load crosses the wire as v2");
    Check((kCapabilities&kCapLoadout)!=0,"this version announces kCapLoadout");
}
void Success() {
    Room r(3);for(const auto& n:r.nodes)Check(n->session->Ready(),"authenticated handshake ready");
    Check(r.Submit(),"client submits catalog request");r.Settle();
    Check(r.nodes[0]->loadout==0,"a request with no composed load plans the call's own (loadout 0)");
    for(const auto& n:r.nodes)Check(n->spawns==1 && n->session->ActiveCount()==1,"all peers spawn once");
    Check(!r.nodes[0]->remote && r.nodes[1]->remote && r.nodes[2]->remote,"host authority passed to adapters");
    Check(SamePlanSpecs(r.nodes[0]->last,r.nodes[2]->last),"all sixteen object specs and native IDs identical");
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
        "an old epoch's refusal cannot terminate the new epoch's request");
    r.Settle();r.Step(121000);r.Settle();
    Check(r.nodes[1]->notices.back().second==RequestStatus::timeout,"host planning deadline produces real timeout status, not arbitrary UI timer");

    Room newer(2);newer.Submit();newer.Settle();newer.Step(2500);newer.Submit();
    newer.With(0);newer.nodes[0]->session->Failed(1);newer.Settle();
    Check(newer.nodes[1]->notices.back()==std::make_pair(2u,RequestStatus::active),"previous transaction cancel cannot be misreported as the newer request outcome");
}
}
// 2026-10-09 (the user: colours and loaded vehicles must work online). The hello's `index` has no capability bit left, so
// the extension rides in the hello's unit (never read for a hello): resourceId the extension word, netId a Bloom filter of
// the variant files the peer has; a unit's variant rides in the unit message's unused `challenge`. Older builds: zeros.
void Variants() {
    using crew::BloomAdd;using crew::VariantHash;
    const std::uint64_t a=VariantHash(L"EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO"),b=VariantHash(L"EDF6VC_LO_TANK_4000000000000C81.SGO");
    Message hello;hello.kind=Kind::hello;hello.challenge=7;hello.request=1;hello.index=kCapabilities;
    hello.unit.resourceId=kExtVariants;BloomAdd(hello.unit.netId,a);
    unsigned char bytes[kWireSize];Message out;
    Check(Encode(hello,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),out) && out.unit.resourceId==kExtVariants &&
          !std::memcmp(out.unit.netId,hello.unit.netId,32),"the hello's extension and filter round-trip on the v2 wire");
    Message unit;unit.kind=Kind::unit;unit.epoch=22;unit.transaction=1;unit.unit.resourceId=1;
    unit.unit.matrix[0]=unit.unit.matrix[5]=unit.unit.matrix[10]=unit.unit.matrix[15]=1;Derive(0x40000001,unit.unit.netId);
    unit.challenge=0xC000000000000C81ull;
    Check(Encode(unit,bytes,sizeof(bytes)) && Decode(bytes,sizeof(bytes),out) && out.challenge==unit.challenge,
          "a unit message carries its variant in `challenge`");
    {Room r(3);
     BloomAdd(r.nodes[1]->bloom,a);BloomAdd(r.nodes[1]->bloom,b);BloomAdd(r.nodes[2]->bloom,a);r.Step(1100);r.Settle();
     const auto& host=*r.nodes[0]->session;
     Check(host.PeersHaveVariant(kExtVariants,0),"every peer applies variants");
     Check(host.PeersHaveVariant(kExtVariants,a),"every peer has the coloured soldier's file");
     Check(!host.PeersHaveVariant(kExtVariants,b),"one peer lacks the tank's file: the host plans it stock");
     Check(r.nodes[1]->session->PeersHaveVariant(kExtVariants,b),"a client is never asked: true");
     BloomAdd(r.nodes[2]->bloom,b);r.Step(1100);r.Settle();
     Check(host.PeersHaveVariant(kExtVariants,b),"a peer's next hello (after its installer made the file) decides");
     r.nodes[0]->variant=0xC000000000000C81ull;Check(r.Submit(),"a request");r.Settle();
     for(const auto& n:r.nodes)Check(n->spawns==1 && n->last.units[0].variant==0xC000000000000C81ull &&
                                    n->last.units[15].variant==0xC000000000000C81ull,"every peer's plan has the host's unit variants");}
    {Room r(3);r.legacyNode=2;r.With(2);r.nodes[2]->session->Stop();r.nodes[2]->session->Start(false,2,1,r.now);r.Settle();
     Check(!r.nodes[0]->session->PeersHaveVariant(kExtVariants,0),"an older peer announces no extension: no variants for the room");
     Check(r.nodes[0]->session->Ready(),"and still completes the handshake");}
}
// 2026-10-10: the host's welcome carries its features (support_protocol.h Features: the capabilities and the extension
// word; a guest says when its host is older or newer, or another guest is behind it); the sea rescue's request and the
// map's are each told their own outcome; a host knows who asked.
void RoomBuilds() {
    {Room r(3);r.With(1);
     Check(r.nodes[1]->session->HostCapsKnown() && r.nodes[1]->session->HostCaps()==kFeatures && !r.nodes[1]->session->RoomBehindHost(),
           "a guest knows its host's features (capabilities and extensions) from the welcome");
     std::uint32_t missing=1;
     Check(r.nodes[0]->session->PeersBehind(&missing)==0 && missing==0 && r.nodes[0]->session->PeersAhead()==0,"the same build everywhere");}
    {Room r(3);r.legacyNode=2;r.With(2);r.nodes[2]->session->Stop();r.nodes[2]->session->Start(false,2,1,r.now);r.Settle();r.Step(1100);r.Settle();
     std::uint32_t missing=0;
     Check(r.nodes[0]->session->PeersBehind(&missing)==1 && missing==kFeatures,"the host counts the older guest and what it lacks");
     Check(r.nodes[1]->session->RoomBehindHost(),"the other guest is told a guest of the room is behind the host");}
    // #100's build (every capability, the variants, no sea rescue): behind by exactly the rescue's extension bit, so the
    // version notice names the rescue, never "the same build".
    {Room r(3);r.legacyNode=2;r.legacyCaps=kCapabilities;r.legacyExt=kExtVariants;r.With(2);r.nodes[2]->session->Stop();
     r.nodes[2]->session->Start(false,2,1,r.now);r.Settle();r.Step(1100);r.Settle();
     std::uint32_t missing=0;
     Check(r.nodes[0]->session->PeersBehind(&missing)==1 && missing==FeatureExt(kExtSeaRescue|kExtRescueChannel|kExtOutfit41|kExtBvmPlayers|kExtBackpack|kExtPeerRoster|kExtGravity|kExtFloatGun|kExtUltimate|kExtPlayerVehicle|kExtBarrage) &&
           r.nodes[0]->session->PeersAhead()==0,"a guest without the sea rescue (all four capabilities) is behind, the rescue and the EDF4.1 outfit named");
     Check(r.nodes[1]->session->RoomBehindHost(),"and the other guest is told");}
    // A newer guest: the capability bits are full (kCapabilities<kMaxUnits), so newer is an extension bit this build lacks.
    {Room r(3);r.legacyNode=2;r.legacyCaps=kCapabilities;r.legacyExt=kExtensions|(kExtensions+1);r.With(2);r.nodes[2]->session->Stop();r.nodes[2]->session->Start(false,2,1,r.now);
     r.Settle();r.Step(1100);r.Settle();
     Check(r.nodes[0]->session->PeersAhead()==1 && r.nodes[0]->session->PeersBehind(nullptr)==0,"a newer guest: the host knows");}
    {Room r(2);r.oldHost=true;r.With(1);r.nodes[1]->session->Stop();r.nodes[1]->session->Start(false,1,1,r.now);r.Settle();
     Check(r.nodes[1]->session->Ready() && r.nodes[1]->session->HostCapsKnown() && r.nodes[1]->session->HostCaps()==0,
           "an older host announces nothing: the guest knows it is older");}
    {Room r(2);r.nodes[0]->pending=true;
     Check(r.Submit(1,5),"the map's request");r.Settle();
     const std::uint32_t first=r.nodes[1]->notices.back().first;
     r.Step(2100);
     Check(r.Submit(1,6),"the rescue's request while the map's is still planned");r.Settle();
     const std::uint32_t second=r.nodes[1]->notices.back().first;
     Check(second==first+1 && r.nodes[1]->notices.back().second==RequestStatus::refused,"the second is refused (one plan at a time)");
     r.nodes[0]->pending=false;r.Settle();r.Settle();
     bool firstActive=false;for(const auto& n:r.nodes[1]->notices)firstActive=firstActive || (n.first==first && n.second==RequestStatus::active);
     Check(firstActive,"the earlier request still gets its own outcome after a later one (each kept by id)");
     std::uint64_t token=0;for(std::uint64_t t=1;t<=kMaxTransactions;++t)if(r.nodes[0]->active[t])token=t;
     std::uint32_t peer=99;
     Check(token && r.nodes[0]->session->RequesterOf(token,&peer) && peer==1,"the host knows which peer asked for a transaction");
     Check(!r.nodes[0]->session->RequesterOf(token+50,&peer),"no such transaction: unknown");}
}
// 2026-10-10 (the user: 「救援应该有单独的cd」): the rescue's own channel beside the map's, its own per-player cooldown.
bool Active(Room& r,std::uint32_t catalog) {
    for(const auto& n:r.nodes) {
        bool found=false;
        for(std::uint64_t t=1;t<=kMaxTransactions;++t)found=found || (n->active[t] && n->last.catalogId==catalog);
        if(!found)return false;
    }
    return true;
}
RequestStatus LastFor(Room& r,unsigned node) { return r.nodes[node]->notices.back().second; }
void RescueChannel() {
    {Room r(3);r.nodes[0]->pending=true;r.nodes[0]->pendingCatalog=5;
     Check(r.Submit(1,5),"a map call");r.Settle();
     Check(LastFor(r,1)==RequestStatus::accepted,"the map call is being planned (its ground route)");
     Check(r.Submit(2,kRescue),"another player's rescue meanwhile");r.Settle();
     Check(LastFor(r,2)==RequestStatus::active && r.nodes[1]->last.catalogId==kRescue && r.nodes[2]->last.catalogId==kRescue,
           "the rescue is made on every machine while the map call is still planned");
     Check(r.Submit(1,kRescue),"the map caller's own rescue right after its map call");r.Settle();
     Check(LastFor(r,1)==RequestStatus::active,"no 2 s rate between a map call and a rescue");
     const auto before=r.nodes[2]->notices.size();
     Check(r.Submit(2,kRescue),"the same player's rescue again at once");r.Settle();
     Check(r.nodes[2]->notices.size()>before && LastFor(r,2)==RequestStatus::refused,"refused: its rescue cooldown");
     r.nodes[0]->session->SetRescueCooldown(5000);r.Step(5100);
     Check(r.Submit(2,kRescue),"after the cooldown");r.Settle();
     Check(LastFor(r,2)==RequestStatus::active,"the next rescue comes");
     r.nodes[0]->pending=false;r.Settle();r.Settle();
     bool mapActive=false;for(const auto& n:r.nodes[1]->notices)mapActive=mapActive || n.second==RequestStatus::active;
     Check(mapActive,"the map call finishes on its own channel");}
    {Room r(2);r.nodes[0]->pending=true;r.nodes[0]->pendingCatalog=kRescue;
     Check(r.Submit(1,kRescue),"a rescue being planned");r.Settle();
     r.Step(2100);
     Check(r.Submit(1,kRescue),"the same player's second rescue while the first is in flight");r.Settle();
     Check(LastFor(r,1)==RequestStatus::refused,"one rescue in flight a player");
     Check(r.Submit(1,5),"a map call meanwhile");r.Settle();
     r.nodes[0]->pending=false;r.Settle();r.Settle();
     unsigned active=0;for(const auto& n:r.nodes[1]->notices)active+=n.second==RequestStatus::active;
     Check(active==2,"the map call is not held by the rescue in flight; both made");}
    // A peer without the rescue channel: the rescue waits its turn as before (the map's channel and rate).
    {Room r(3);r.legacyNode=2;r.legacyCaps=kCapabilities;r.legacyExt=kExtVariants|kExtSeaRescue;r.With(2);r.nodes[2]->session->Stop();
     r.nodes[2]->session->Start(false,2,1,r.now);r.Settle();r.Step(1100);r.Settle();
     Check(!r.nodes[0]->session->PeersHaveExtension(kExtRescueChannel) && r.nodes[0]->session->PeersHaveExtension(kExtSeaRescue),
           "an older peer");
     r.nodes[0]->pending=true;r.nodes[0]->pendingCatalog=5;
     Check(r.Submit(1,5),"a map call");r.Settle();
     Check(r.Submit(2,kRescue),"a rescue meanwhile");r.Settle();
     Check(LastFor(r,2)==RequestStatus::accepted && Active(r,kRescue)==false,"one transaction at a time with that peer in the room: it waits");
     r.nodes[0]->pending=false;r.Settle();r.Settle();
     Check(LastFor(r,2)==RequestStatus::active && Active(r,kRescue),"made once the map call is out");}
    // A failed rescue rolls back alone: the map's deployment stays.
    {Room r(3);
     Check(r.Submit(1,5),"a map call");r.Settle();
     Check(Active(r,5),"made everywhere");
     std::uint64_t mapToken=0;for(std::uint64_t t=1;t<=kMaxTransactions;++t)if(r.nodes[2]->active[t])mapToken=t;
     r.nodes[2]->failSpawn=true;
     Check(r.Submit(1,kRescue),"a rescue a peer cannot make");r.Settle();r.Settle();
     Check(LastFor(r,1)!=RequestStatus::active && r.nodes[2]->active[mapToken],"the rescue is cancelled; the map's deployment stays");
     r.nodes[2]->failSpawn=false;
     Check(r.Submit(1,kRescue),"asked again at once");r.Settle();
     Check(LastFor(r,1)==RequestStatus::active,"a rescue that was cancelled started no cooldown (it starts once made everywhere)");}
}
// 2026-10-27 (PR #269 review): a paid request's pt are settled by its outcome (support_points.h Ledger, keyed by the request
// id), so every request this machine made is told one, and an id is never reused.
bool Told(Room& r,unsigned node,std::uint32_t request,RequestStatus status) {
    for(const auto& n:r.nodes[node]->notices)if(n.first==request && n.second==status)return true;
    return false;
}
void LocalOutcomes() {
    // A peer rejoining before anything was made restarts the host's epoch (its transactions cleared): the requests still
    // waiting on every machine are told interrupted, as a suspension tells them.
    {Room r(3);r.nodes[0]->pending=true;
     Check(r.Submit(1,5),"a guest's map call being planned");r.Settle();
     const std::uint32_t first=r.nodes[1]->notices.back().first;
     Check(LastFor(r,1)==RequestStatus::accepted,"it waits for the host's plan");
     r.With(2);r.nodes[2]->session->Stop();r.nodes[2]->session->Start(false,2,1,r.now);r.Settle();
     Check(LastFor(r,1)==RequestStatus::interrupted && r.nodes[1]->notices.back().first==first,
           "the host's new epoch: the guest's waiting request is told interrupted");
     r.nodes[0]->pending=false;r.Step(2100);
     Check(r.Submit(1,5),"the next call");r.Settle();
     const std::uint32_t second=r.nodes[1]->notices.back().first;
     Check(second>first,"request ids keep growing across an epoch (never the cleared one's again)");
     Check(Told(r,1,second,RequestStatus::active) && !Told(r,1,first,RequestStatus::active),"the next is made, told by its own id");}
    {Room r(2);r.nodes[0]->pending=true;
     Check(r.Submit(0,5),"the host's own map call being planned");r.Settle();
     const std::uint32_t first=r.nodes[0]->notices.back().first;
     r.With(1);r.nodes[1]->session->Stop();r.nodes[1]->session->Start(false,1,1,r.now);r.Settle();
     Check(Told(r,0,first,RequestStatus::interrupted),"a peer's rejoin clears the host's transactions: its own request is told interrupted");}
    {Room r(2);r.nodes[0]->pending=true;
     Check(r.Submit(1,5),"a guest's call being planned");r.Settle();
     const std::uint32_t first=r.nodes[1]->notices.back().first;
     r.With(1);r.nodes[1]->session->Stop();
     Check(Told(r,1,first,RequestStatus::interrupted),"leaving the session: a waiting request is told interrupted");
     r.nodes[1]->session->Start(false,1,1,r.now);r.Settle();r.nodes[0]->pending=false;r.Step(2100);
     Check(r.Submit(1,5),"a call in the new session");r.Settle();
     Check(r.nodes[1]->notices.back().first>first && LastFor(r,1)==RequestStatus::active,"its id is new, its outcome its own");}
    // A request waiting while later ones end: it keeps its slot and is told its outcome however many came after it.
    {Room r(2);r.nodes[0]->pending=true;
     Check(r.Submit(1,5),"a call being planned");r.Settle();
     const std::uint32_t first=r.nodes[1]->notices.back().first;
     for(int i=0;i<4;++i){r.Step(100);Check(r.Submit(1,5),"later requests");r.Settle();}
     Check(r.nodes[1]->notices.back().first==first+4 && LastFor(r,1)==RequestStatus::refused,"each refused at once (one plan at a time)");
     r.nodes[0]->pending=false;r.Settle();r.Settle();
     Check(Told(r,1,first,RequestStatus::active),"the first, four requests behind, is still told it was made");}
    // Every slot waiting: another request is not taken (it would push one out untold).
    {Room r(2);r.dropTo=1;r.dropKind=Kind::requestStatus;r.nodes[0]->pending=true;
     for(int i=0;i<static_cast<int>(kRecentRequests);++i){Check(r.Submit(1,5),"a request whose outcome has not come");r.Step(2100);}
     const auto told=r.nodes[1]->notices.size();
     Check(!r.Submit(1,5) && r.nodes[1]->notices.size()==told,"all slots waiting: the next is refused here, nothing sent or told");
     r.dropTo=999;
     for(const auto& p:r.history)if(p.to==1 && p.message.kind==Kind::requestStatus && p.message.index>=static_cast<std::uint32_t>(RequestStatus::refused)){r.Deliver(p);break;}
     Check(r.Submit(1,5),"one told: a slot is free again");}
}
// 2026-10-30 (the user: 「有时候还没办法召唤支援」): one map plan at a time in the room, but another player's call waits its
// turn (Phase::queued) instead of being refused; a player's own second call while its first is out is still refused, and
// the host's log says why.
void MapQueue() {
    {Room r(3);r.nodes[0]->pending=true;
     Check(r.Submit(1,5),"a guest's map call being routed");r.Settle();
     Check(r.Submit(2,5),"another guest's map call meanwhile");r.Settle();
     Check(LastFor(r,2)==RequestStatus::accepted,"queued, not refused");
     Check(r.Submit(0,5),"the host's own call meanwhile");r.Settle();
     Check(LastFor(r,0)==RequestStatus::accepted,"the host's own call queues too");
     const auto plans=r.nodes[0]->plans;r.Step(500);
     Check(r.nodes[0]->plans>plans,"the first is still planned");
     r.Step(2100);
     Check(r.Submit(1,5),"the first caller again while its call is out");r.Settle();
     Check(LastFor(r,1)==RequestStatus::refused && !r.nodes[0]->refusals.empty() &&
           r.nodes[0]->refusals.back()=="its map call in flight or queued","its own second call refused, the cause logged");
     Check(r.nodes[1]->spawns==0,"nothing made while the plan waits");
     r.nodes[0]->pending=false;
     for(int i=0;i<6;++i)r.Settle();
     for(unsigned n=0;n<3;++n)Check(r.nodes[n]->spawns==3,"all three calls made on every machine, one after another");
     Check(LastFor(r,2)==RequestStatus::active && LastFor(r,0)==RequestStatus::active,"each queued caller told its call is out");}
    // A queued call whose plan is refused is told so, and the next one goes on.
    {Room r(3);r.nodes[0]->pending=true;
     Check(r.Submit(1,5),"a map call being routed");r.Settle();
     Check(r.Submit(2,6),"another queued behind it");r.Settle();
     r.nodes[0]->pending=false;r.nodes[0]->refusePlan=true;r.Settle();r.Settle();
     Check(LastFor(r,1)==RequestStatus::refused && LastFor(r,2)==RequestStatus::refused,"both planned in turn, both refused");
     r.nodes[0]->refusePlan=false;r.Step(2100);
     Check(r.Submit(2,6),"asked again");r.Settle();
     Check(LastFor(r,2)==RequestStatus::active,"nothing left queued in the way");}
}
// 2026-10-30: the transport lost a moment (EDF6Coop's room lease or a link going quiet: a hitch) suspends the session; the
// same room resumes it with a new epoch, what was made kept, instead of support shut for the rest of the mission.
void TransportResume() {
    {Room r(3);
     Check(r.Submit(1,5),"a call");r.Settle();
     for(const auto& n:r.nodes)Check(n->session->IsActive(1),"made everywhere");
     const auto epoch=r.nodes[0]->session->Epoch();
     for(unsigned i=0;i<3;++i){r.With(i);r.nodes[i]->session->Suspend();}
     Check(!r.Submit(1,5),"suspended: no request");
     for(unsigned i=0;i<3;++i){r.With(i);r.nodes[i]->session->Start(i==0,2,i==0 ? 0 : r.Peer(i,0),r.now);}
     r.Settle();
     for(const auto& n:r.nodes) {
         Check(n->session->Ready() && !n->session->Suspended(),"the same room resumes");
         Check(n->session->IsActive(1) && n->active[1] && n->destroys==0,"the delivered support is kept across the blip");
     }
     Check(r.nodes[0]->session->Epoch()!=epoch && r.nodes[1]->session->Epoch()==r.nodes[0]->session->Epoch(),"a new epoch everywhere");
     r.Step(2100);Check(r.Submit(2,5),"a call after the blip");r.Settle();
     for(const auto& n:r.nodes)Check(n->spawns==2 && n->session->ActiveCount()==2,"made everywhere beside the kept one");
     Check(r.nodes[2]->last.units[0].netId[4]==16,"native ordinals go on (never reused)");}
    // Only one guest lost its link: it resumes with a new challenge; the host and the other guest take the new epoch and
    // keep what was made.
    {Room r(3);
     Check(r.Submit(1,5),"a call");r.Settle();
     r.With(2);r.nodes[2]->session->Suspend();r.nodes[2]->session->Start(false,2,r.Peer(2,0),r.now);r.Settle();
     for(const auto& n:r.nodes)Check(n->session->Ready() && n->session->IsActive(1) && n->destroys==0,"every machine ready, the support kept");
     r.Step(2100);Check(r.Submit(1,5),"the next call");r.Settle();
     for(const auto& n:r.nodes)Check(n->session->ActiveCount()==2,"made everywhere");}
    // 2026-10-30 review: the two ends of a re-handshake hold different things (the host cancelled its copy, the guest has
    // not heard the cancellation yet). Neither starts its ids over: the host's cancellation reaches the guest in the new
    // epoch (no orphan) and the next call takes a newer id the guest's slot accepts (no slot held for good).
    {Room r(2);
     Check(r.Submit(1,5),"a call");r.Settle();
     r.dropTo=1;r.dropKind=Kind::cancel;
     r.With(0);r.nodes[0]->session->Failed(1);r.Settle();
     Check(!r.nodes[0]->active[1] && r.nodes[1]->active[1],"the host's copy cancelled, its cancellation lost on the way");
     r.With(1);r.nodes[1]->session->Suspend();r.nodes[1]->session->Start(false,1,r.Peer(1,0),r.now);r.Settle();
     Check(r.nodes[1]->session->Ready(),"the guest resumed with a new challenge");
     r.dropTo=999;r.Step(1100);r.Settle();
     Check(!r.nodes[1]->active[1] && r.nodes[1]->session->ActiveCount()==0,"the host's kept cancellation reaches the guest after the re-handshake");
     r.Step(2100);Check(r.Submit(1,5),"the next call");r.Settle();
     Check(LastFor(r,1)==RequestStatus::active,"the next call is made");
     for(const auto& n:r.nodes)Check(n->session->ActiveCount()==1,"made on both ends: its id is newer than the guest's old slot");}
    // A call in flight when the link goes: told interrupted, rolled back; the kept one stays.
    {Room r(2);
     Check(r.Submit(1,5),"a call");r.Settle();
     r.nodes[0]->pending=true;r.Step(2100);
     Check(r.Submit(1,5),"a second being routed");r.Settle();
     const auto second=r.nodes[1]->notices.back().first;
     for(unsigned i=0;i<2;++i){r.With(i);r.nodes[i]->session->Suspend();}
     Check(Told(r,1,second,RequestStatus::interrupted),"the one in flight is told interrupted");
     for(unsigned i=0;i<2;++i){r.With(i);r.nodes[i]->session->Start(i==0,1,i==0 ? 0 : 1,r.now);}
     r.nodes[0]->pending=false;r.Settle();r.Settle();
     for(const auto& n:r.nodes)Check(n->session->ActiveCount()==1 && n->session->IsActive(1),"only the made one, kept");
     r.Step(2100);Check(r.Submit(1,5),"called again");r.Settle();
     Check(LastFor(r,1)==RequestStatus::active,"made");}
    // Another room shape: no resume (its kept per-peer marks would mean other machines); the mission reset starts it.
    {Room r(3);
     Check(r.Submit(1,5),"a call");r.Settle();
     r.With(0);r.nodes[0]->session->Suspend();r.nodes[0]->session->Start(true,3,0,r.now);
     Check(r.nodes[0]->session->Suspended() && !r.nodes[0]->session->Ready() && r.nodes[0]->active[1],"stays suspended, the support kept");}
}
void PvpSupportCodec() {
    static_assert(kVersion==2 && kWireSize==176 && (kFeatures&kWelcomeCatalogMask)<1024 &&
                  WelcomeFeatures(kFeatures&kWelcomeCatalogMask,kExtensions)==kFeatures,"PvP detail does not change the v2 ABI");
    Message begin;begin.kind=Kind::begin;begin.epoch=22;begin.transaction=1;begin.catalog=5;begin.count=1;
    unsigned char wire[kWireSize]{};Message decoded;
    Check(Encode(begin,wire,sizeof(wire)) && Decode(wire,sizeof(wire),decoded),"legacy BEGIN still round-trips");
    bool zero=true;for(unsigned i=68;i<kWireSize;++i)zero=zero && wire[i]==0;
    Check(zero,"legacy BEGIN unused unit and tail stay byte-zero");
    // The requester: a player's reference (net_object_ref.h: slot 41, its tag at bytes 20..23).
    begin.unit.resourceId=kBeginRequesterMetadata;begin.unit.role=2;crew::netref::MakePlayer(41,begin.unit.netId);
    Check(Encode(begin,wire,sizeof(wire)) && Decode(wire,sizeof(wire),decoded) && decoded.unit.role==2 &&
        decoded.unit.resourceId==kBeginRequesterMetadata && !std::memcmp(decoded.unit.netId,begin.unit.netId,32),
        "BEGIN requester identity and authorized side cross the unchanged real v2 codec");
    Check(wire[4]==2 && wire[172]==0 && wire[175]==0,"metadata preserves v2 and the reserved tail");
    begin.unit.role=0;
    Check(Encode(begin,wire,sizeof(wire)) && Decode(wire,sizeof(wire),decoded) && decoded.unit.netId[0]==41,
        "team zero retains requester ownership for a later PvP activation");
    auto malformed=begin;malformed.unit.role=3;
    Check(!Encode(malformed,wire,sizeof(wire)),"unknown PvP team rejected");
    malformed=begin;malformed.unit.netId[20]=1;
    Check(!Encode(malformed,wire,sizeof(wire)),"a requester with its tag broken rejected");
    malformed=begin;std::memset(malformed.unit.netId,0,32);malformed.unit.netId[0]=41;malformed.unit.netId[12]=1;
    Check(!Encode(malformed,wire,sizeof(wire)),"a requester by a NetId rejected (a player is its slot)");
    malformed=begin;std::memset(malformed.unit.netId,0,32);
    Check(!Encode(malformed,wire,sizeof(wire)),"metadata cannot claim an empty requester");
    malformed=begin;malformed.unit.resourceId=123;
    Check(!Encode(malformed,wire,sizeof(wire)),"unknown BEGIN metadata magic rejected");
    malformed=begin;malformed.unit.matrix[15]=1;
    Check(!Encode(malformed,wire,sizeof(wire)),"metadata transform must be zero");
    malformed=begin;malformed.unit.matrix[0]=-0.0f;
    Check(!Encode(malformed,wire,sizeof(wire)),"metadata requires byte-zero transform");
    malformed=begin;malformed.unit.variant=1;
    Check(!Encode(malformed,wire,sizeof(wire)),"BEGIN cannot hide a unit variant");
    malformed=begin;malformed.unit.resourceId=0;
    Check(!Encode(malformed,wire,sizeof(wire)),"unmarked BEGIN cannot carry requester bytes");
    Check(Encode(begin,wire,sizeof(wire)),"valid requester fixture");
    wire[68]^=1;Check(!Decode(wire,sizeof(wire),decoded),"wire decoder rejects unknown BEGIN magic");wire[68]^=1;
    {const unsigned char was=wire[160];wire[160]=1;Check(!Decode(wire,sizeof(wire),decoded),"wire decoder rejects a requester with its tag broken");wire[160]=was;}
    wire[72]=3;Check(!Decode(wire,sizeof(wire),decoded),"wire decoder rejects invalid metadata side");wire[72]=0;
    wire[76]=1;Check(!Decode(wire,sizeof(wire),decoded),"wire decoder rejects dirty metadata fields");
    Plan plan;plan.count=1;plan.units[0].resourceId=1;
    plan.units[0].matrix[0]=plan.units[0].matrix[5]=plan.units[0].matrix[10]=plan.units[0].matrix[15]=1;
    Check(ValidPlan(plan,false),"legacy plan has no requester or team");
    plan.team=1;Check(!ValidPlan(plan,false),"team plan requires a requester");
    crew::netref::MakePlayer(41,plan.requester);Check(ValidPlan(plan,false),"a player's requester permits team one");
    plan.team=0;Check(ValidPlan(plan,false),"requester with stock team is a valid ownership cache");
    plan.team=3;Check(!ValidPlan(plan,false),"plan rejects unsupported team");
    plan.team=2;plan.requester[23]=1;Check(!ValidPlan(plan,false),"plan rejects a requester with its tag broken");
    std::memset(plan.requester,0,32);plan.requester[0]=41;Check(!ValidPlan(plan,false),"plan rejects a requester by its NetId");
}
void PvpSupport() {
    {Room r(3);
     Check(r.nodes[0]->session->PeersHaveDetails(kDetailPvpSupport) && r.nodes[1]->session->HostDetails()==kDetails &&
        r.nodes[1]->session->PeersHaveDetails(kDetailPvpSupport),"host and clients negotiate independent support details");
     crew::netref::MakePlayer(41,r.nodes[0]->requester);r.nodes[0]->team=2;
     RequesterPlanner(*r.nodes[0],true);
     Check(r.nodes[0]->session->Ready(),"requester-aware hook can be the sole host planner");
     const float at[3]={10,20,30};const std::uint64_t load=0xDD99998;
     r.With(2);Check(current->session->Submit(5,at,r.now,load),"second authenticated peer submits support");
     for(auto& packet:r.packets)if(packet.from==2 && packet.message.kind==Kind::request)packet.message.index=9;
     r.Settle();
     Check(r.nodes[0]->requesterPlans>0 && r.nodes[0]->requesterPeer==2 && r.nodes[0]->loadout==load,
        "planner receives authenticated peer index and loadout, never payload index");
     for(const auto& n:r.nodes)Check(n->spawns==1 && n->last.requester[0]==41 && n->last.team==2,
        "every real-wire peer receives identical requester ownership and PvP team");
     const auto begin=Find(r,Kind::begin,1);
     Check(begin.message.unit.resourceId==kBeginRequesterMetadata && begin.message.unit.role==2,
        "host emits explicit BEGIN requester metadata");
     r.Step(2100);r.With(0);const float own[3]={1,2,3};Check(current->session->Submit(5,own,r.now),"host submits its own support");r.Settle();
     Check(r.nodes[0]->requesterPeer==0,"host-local planner requester is peer zero");}
    {Room r(2);crew::netref::MakePlayer(42,r.nodes[0]->requester);r.nodes[0]->team=0;RequesterPlanner(*r.nodes[0]);r.Submit();r.Settle();
     for(const auto& n:r.nodes)Check(n->spawns==1 && n->last.requester[0]==42 && !n->last.team,
        "stock-side deployment retains ownership on every peer");}
    {Room r(3);r.legacyNode=2;r.legacyCaps=kCapabilities;r.legacyExt=kExtensions;r.Step(1100);r.Settle();
     Check(!r.nodes[0]->session->PeersHaveDetails(kDetailPvpSupport),"one legacy peer closes the room metadata gate");
     crew::netref::MakePlayer(42,r.nodes[0]->requester);r.nodes[0]->team=1;RequesterPlanner(*r.nodes[0]);r.Submit();r.Settle();
     for(const auto& n:r.nodes)Check(!n->spawns,"no metadata transaction spawns with a legacy third peer");
     bool began=false;for(const auto& p:r.history)began=began || p.message.kind==Kind::begin;
     Check(!began,"all-peer capability gate runs before any BEGIN metadata is sent");
     std::memset(r.nodes[0]->requester,0,32);r.nodes[0]->team=0;r.Step(2100);r.Submit();r.Settle();
     for(const auto& n:r.nodes)Check(n->spawns==1,"legacy stock support remains usable in a mixed room");
     const auto legacy=Find(r,Kind::begin,1);unsigned char wire[kWireSize]{};Check(Encode(legacy.message,wire,sizeof(wire)),"legacy mixed-room BEGIN encoded");
     bool zero=true;for(unsigned i=68;i<kWireSize;++i)zero=zero && wire[i]==0;
     Check(zero,"mixed-room legacy BEGIN carries no metadata bytes");}
    {Room r(3);r.dropTo=0;r.dropKind=Kind::ready;crew::netref::MakePlayer(42,r.nodes[0]->requester);RequesterPlanner(*r.nodes[0]);r.Submit();r.Settle();
     r.legacyNode=2;r.legacyCaps=kCapabilities;r.legacyExt=kExtensions;r.Step(1100);r.Settle();r.dropTo=999;r.Settle();
     for(const auto& n:r.nodes)Check(!n->spawns,"capability loss before commit cancels a prepared requester transaction");}
    {Room r(3);const auto old=Find(r,Kind::hello,0);
     r.legacyNode=old.from;r.legacyCaps=kCapabilities;r.legacyExt=kExtensions;r.With(old.from);
     r.nodes[old.from]->session->Stop();r.nodes[old.from]->session->Start(false,2,r.Peer(old.from,0),r.now);r.Settle();
     Check(!r.nodes[0]->session->PeersHaveDetails(kDetailPvpSupport),"new challenge replaces the peer capability word with legacy zero");
     r.Deliver(old);Check(!r.nodes[0]->session->PeersHaveDetails(kDetailPvpSupport),"stale prior-mission HELLO cannot restore a capability");
     r.legacyNode=999;r.Step(1100);r.Settle();
     Check(r.nodes[0]->session->PeersHaveDetails(kDetailPvpSupport),"current challenge HELLO can refresh capabilities");
     Packet wrong{};for(const auto& p:r.history)if(p.from==old.from && p.message.kind==Kind::hello)wrong=p;
     wrong.message.challenge+=100;wrong.message.unit.role=0;r.Deliver(wrong);
     Check(r.nodes[0]->session->PeersHaveDetails(kDetailPvpSupport),"wrong challenge with equal mission serial cannot erase current capability");}
    for(bool assembled:{false,true}) {
        Room r(3);crew::netref::MakePlayer(42,r.nodes[0]->requester);RequesterPlanner(*r.nodes[0]);
        if(assembled){r.dropTo=1;r.dropKind=Kind::prepare;}else{r.dropTo=0;r.dropKind=Kind::ready;}
        r.Submit();r.Settle();Packet welcome{};
        for(const auto& p:r.history)if(p.to==1 && p.message.kind==Kind::welcome)welcome=p;
        welcome.message.unit.role=0;r.Deliver(welcome);
        Check(!r.nodes[1]->session->PeersHaveDetails(kDetailPvpSupport),"current authenticated welcome can withdraw host support details");
        Packet step{0,1,{}};step.message.epoch=r.nodes[1]->session->Epoch();step.message.transaction=1;
        step.message.kind=Kind::prepare;r.Deliver(step);step.message.kind=Kind::commit;r.Deliver(step);
        Check(!r.nodes[1]->spawns,"client capability loss while assembling or prepared prevents native creation");
    }
    for(unsigned mode=0;mode<3;++mode) {
        Room r(3);if(mode==0){r.oldHost=true;r.Step(1100);r.Settle();Check(!r.nodes[1]->session->HostDetails(),"legacy host has no details");}
        Packet begin{mode==1 ? 2u : 0u,1,{}};begin.message.kind=Kind::begin;begin.message.epoch=r.nodes[1]->session->Epoch();
        begin.message.transaction=1;begin.message.catalog=5;begin.message.count=1;
        begin.message.unit.resourceId=kBeginRequesterMetadata;begin.message.unit.role=2;begin.message.unit.netId[0]=42;
        if(mode==2)++begin.message.epoch;
        r.Deliver(begin);
        Packet unit=begin;unit.from=0;unit.message=Message{};unit.message.kind=Kind::unit;unit.message.epoch=r.nodes[1]->session->Epoch();
        unit.message.transaction=1;unit.message.unit.resourceId=1;
        unit.message.unit.matrix[0]=unit.message.unit.matrix[5]=unit.message.unit.matrix[10]=unit.message.unit.matrix[15]=1;
        Derive(0x40000001u,unit.message.unit.netId);r.Deliver(unit);
        Packet next=unit;next.message=Message{};next.message.epoch=unit.message.epoch;next.message.transaction=1;
        next.message.kind=Kind::prepare;r.Deliver(next);next.message.kind=Kind::commit;r.Deliver(next);
        Check(!r.nodes[1]->spawns,"unnegotiated, wrong-host or stale-epoch requester BEGIN cannot create objects");
    }
    {Room r(2);const auto old=Find(r,Kind::welcome,1);r.nodes[1]->session->Stop();
     Check(!r.nodes[1]->session->PeersHaveDetails(kDetailPvpSupport) && !r.nodes[1]->session->HostDetails(),"stopped client forgets host detail word");
     r.With(1);r.nodes[1]->session->Start(false,1,1,r.now);r.oldHost=true;r.Settle();
     r.Deliver(old);Check(!r.nodes[1]->session->HostDetails(),"stale welcome challenge cannot restore old host details");
     r.With(0);r.nodes[0]->session->Stop();
     Check(!r.nodes[0]->session->HostDetails(),"stopped host does not advertise usable details");}
}
namespace {
Plan DronePlan(std::uint32_t resource=crew::kSupportSubDroneResource,std::uint64_t parent=901) {
    Plan p{};p.catalogId=kPreparedDroneCatalog;p.count=1;p.team=2;crew::netref::MakePlayer(41,p.requester);
    p.target[0]=10;p.target[1]=20;p.target[2]=30;
    auto& u=p.units[0];u.resourceId=resource;u.variant=parent;
    u.matrix[0]=u.matrix[5]=u.matrix[10]=u.matrix[15]=1;std::memcpy(u.matrix+12,p.target,12);return p;
}
std::uint64_t SubmitDrone(Room& r,const Plan& p) {
    r.With(0);return r.nodes[0]->session->SubmitPrepared(p,r.now,PreparedKind::dynamicDrone);
}
void PreparedDroneAdmission() {
    static_assert(kDetailPvpSupport==1 && kDetailDynamicDrones==2 && kDetailTransactionGenerations==4 && kDetails==7,"independent legacy-compatible detail bits");
    Room r(3);auto p=DronePlan();
    Check(ValidPreparedDronePlan(p),"exact submarine child shape admitted by explicit prepared-drone contract");
    r.With(1);Check(!r.nodes[1]->session->SubmitPrepared(p,r.now,PreparedKind::dynamicDrone),"guest cannot submit a prepared dynamic plan");
    r.With(0);Check(!r.nodes[0]->session->SubmitPrepared(p,r.now),"default mission-crew entry still refuses dynamic plans");
    Check(!r.nodes[0]->session->SubmitPrepared(p,r.now,static_cast<PreparedKind>(99)),"unknown prepared intent rejected");
    for(unsigned mode=0;mode<20;++mode) {
        auto bad=p;
        switch(mode) {
        case 0:bad.catalogId=15;break;
        case 1:bad.catalogId=kMissionCrewCatalog;break;
        case 2:bad.count=2;bad.units[1]=bad.units[0];bad.units[1].resourceId=1;bad.units[1].variant=0;break;
        case 3:bad.units[0].role=1;break;
        case 4:bad.units[0].variant=0;break;
        case 5:std::memset(bad.requester,0,32);bad.team=0;break;
        case 6:bad.requester[20]=1;break;
        case 7:bad.units[0].resourceId=kExistingVehicle;bad.units[0].netId[0]=1;break;
        case 8:bad.units[0].resourceId=crew::kSupportSubmarineResource;break;
        case 9:bad.units[0].resourceId=crew::kSupportSubDroneResource+1;break;
        case 10:bad.units[0].resourceId=crew::kSupportCarrierDroneBase-1;break;
        case 11:bad.units[0].resourceId=crew::kSupportCarrierDroneBase+6;break;
        case 12:bad.units[0].resourceId=crew::kSupportCarrierDroneBase+8;break;
        case 13:bad.units[0].resourceId=crew::kSupportCarrierDroneBase+48;break;
        case 14:bad.units[0].resourceId=crew::kSupportCarrierDroneBase+128;break;
        case 15:bad.units[0].resourceId=1;break;
        case 16:Derive(0x40000001u,bad.units[0].netId);break;
        case 17:bad.units[0].matrix[0]=std::numeric_limits<float>::quiet_NaN();break;
        case 18:bad.target[1]=std::numeric_limits<float>::infinity();break;
        default:bad.team=3;break;
        }
        Check(!ValidPreparedDronePlan(bad) && !SubmitDrone(r,bad),"malformed or unrelated prepared plan fails closed without allocating transaction");
    }
    r.nodes[0]->reject=true;
    Check(!SubmitDrone(r,p) && !r.nodes[0]->destroys,"preflight native Validate rejection returns no token and needs no reservation rollback");
    r.nodes[0]->reject=false;Check(SubmitDrone(r,p)==1,"invalid/preflight-refused attempts consume no transaction or object IDs");r.Settle();
    for(const auto& n:r.nodes)Check(n->spawns==1 && n->session->IsActive(1),"admitted dynamic plan completes real three-peer Session");
    r.With(0);r.nodes[0]->session->Suspend();Check(!SubmitDrone(r,p),"not-ready/suspended host cannot prepare children");
}
void PreparedDroneResources() {
    Room r(3);unsigned total=0;std::vector<std::array<unsigned char,32>> ids;
    for(unsigned item=0;item<37;++item) {
        const auto resource=item ? crew::CarrierDroneResource((item-1)/12,((item-1)/2)%6,((item-1)%2)!=0) : crew::kSupportSubDroneResource;
        auto p=DronePlan(resource,1000+item);const auto token=SubmitDrone(r,p);Check(token==item+1,"all 37 exact dynamic resources receive a host transaction");r.Settle();
        for(const auto& n:r.nodes)Check(n->session->IsActive(token) && n->last.units[0].resourceId==resource &&
            n->last.units[0].variant==p.units[0].variant && !std::memcmp(n->last.requester,p.requester,32) && n->last.team==p.team,
            "kind/slot/ordered and parent/owner/team survive real v2 three-peer codec unchanged");
        std::array<unsigned char,32> id{};std::memcpy(id.data(),r.nodes[0]->last.units[0].netId,32);
        for(const auto& prior:ids)Check(std::memcmp(prior.data(),id.data(),32)!=0,"derived child IDs never reuse an earlier deployment identity");
        ids.push_back(id);++total;
    }
    Check(total==37 && r.nodes[0]->session->ActiveCount()==37,"all supported resource encodings use the normal shared Session capacity");
}
void PreparedDroneLifecycle() {
    Room r(3);const auto p=DronePlan(crew::CarrierDroneResource(2,5,true));const auto token=SubmitDrone(r,p);
    Check(token==1,"explicit dynamic plan accepted before ticking");r.dropTo=0;r.dropKind=Kind::ready;r.Settle();
    for(const auto& n:r.nodes)Check(!n->spawns,"no native child exists until all prepare ACKs arrive");
    const auto begin=Find(r,Kind::begin,1),unit=Find(r,Kind::unit,1),commitPlaceholder=begin;
    static_cast<void>(commitPlaceholder);
    Check(begin.message.catalog==kPreparedDroneCatalog && begin.message.unit.resourceId==kBeginRequesterMetadata && begin.message.unit.role==2,
        "dynamic BEGIN carries the existing authenticated-owner metadata contract");
    Check(unit.message.unit.resourceId==p.units[0].resourceId && unit.message.challenge==p.units[0].variant && unit.message.unit.netId[12]==5,
        "dynamic unit gets a freshly derived native identity and preserved parent key");
    r.dropTo=999;for(const auto& packet:r.history)if(packet.message.kind==Kind::ready && packet.message.ok)r.Deliver(packet);
    r.dropTo=0;r.dropKind=Kind::result;r.Settle();
    for(const auto& n:r.nodes)Check(n->spawns==1 && !n->session->IsActive(token),"all three registered children stay inactive before all commit results");
    Check(SamePlanSpecs(r.nodes[0]->last,r.nodes[2]->last),"entire prepared child spec is identical on every peer");
    r.dropTo=999;for(const auto& packet:r.history)if(packet.message.kind==Kind::result && packet.message.ok)r.Deliver(packet);
    r.dropTo=2;r.dropKind=Kind::activate;r.Settle();
    Check(r.nodes[0]->session->IsActive(token) && r.nodes[1]->session->IsActive(token) && !r.nodes[2]->session->IsActive(token),
        "host sends final activation only after every creation result");
    r.dropTo=999;r.Step(1100);r.Settle();for(const auto& n:r.nodes)Check(n->session->IsActive(token),"late activation reliably converges");
    const auto commit=Find(r,Kind::commit,1);r.Deliver(begin);r.Deliver(unit);r.Deliver(commit);r.Settle();
    for(const auto& n:r.nodes)Check(n->spawns==1,"replayed dynamic BEGIN/unit/commit cannot duplicate a child");
    r.With(0);r.nodes[0]->session->Failed(token);r.Settle();
    for(const auto& n:r.nodes)Check(!n->active[token] && !n->session->IsActive(token) && !n->destroyedTokens.empty() && n->destroyedTokens.back()==token,
        "dynamic rollback destroys exactly its registered token on every peer");
    r.Deliver(commit);r.Settle();Check(r.nodes[1]->spawns==1,"cancelled dynamic tombstone cannot resurrect on late commit");
}
void PreparedDroneFailure() {
    for(unsigned mode=0;mode<5;++mode) {
        Room r(3);const auto p=DronePlan();
        if(mode==0)r.nodes[0]->rejectValidationAt=2;
        if(mode==1)r.nodes[0]->failDerive=true;
        if(mode==2)r.nodes[2]->reject=true;
        if(mode==3){r.failTo=2;r.failKind=Kind::unit;}
        if(mode==4)r.nodes[2]->failSpawn=true;
        const auto token=SubmitDrone(r,p);Check(token==1,"failure fixture first passes host admission");r.Settle();
        Check(r.nodes[0]->destroys>=1 && r.nodes[0]->destroyedTokens.front()==token,
            "accepted token always reaches host Destroy even when failure precedes any native create");
        for(const auto& n:r.nodes)Check(!n->active[token] && !n->session->IsActive(token),"prepare/derive/native/enqueue failure rolls back every peer");
        if(mode<4)for(const auto& n:r.nodes)Check(!n->spawns,"failure before commit creates no native objects");
        else for(const auto& n:r.nodes)Check(n->destroys>=1,"native create failure releases every committed copy");
    }
}
void PreparedDroneChannels() {
    {Room r(3);auto first=DronePlan(crew::CarrierDroneResource(0,0,false));Check(SubmitDrone(r,first)==1,"first carrier slot pending");
     auto same=first;same.units[0].resourceId=crew::CarrierDroneResource(0,0,true);Check(!SubmitDrone(r,same),"ordered bit cannot bypass pending slot mutex");
     same.units[0].resourceId=crew::CarrierDroneResource(2,0,false);Check(!SubmitDrone(r,same),"kind cannot bypass same-parent pending slot mutex");
     same.units[0].resourceId=crew::kSupportSubDroneResource;Check(!SubmitDrone(r,same),"submarine's unslotted child serializes same-parent pending request");
     auto second=first;second.units[0].resourceId=crew::CarrierDroneResource(0,1,false);Check(SubmitDrone(r,second)==2,"another slot of the same carrier can prepare concurrently");
     auto other=first;other.units[0].variant=999;Check(SubmitDrone(r,other)==3,"another carrier can prepare concurrently");
     Check(r.Submit(1,5),"dynamic children do not block ordinary map channel");r.Settle();
     for(const auto& n:r.nodes)Check(n->session->ActiveCount()==4,"independent channels still complete every real prepare/commit/activate");
     Check(SubmitDrone(r,first)==5,"active prior transaction cannot permanently block native-approved reuse of a carrier slot");r.Settle();
     r.With(0);r.nodes[0]->session->Failed(5);r.Settle();
     for(const auto& n:r.nodes)Check(n->session->IsActive(1) && n->session->IsActive(2) && n->session->IsActive(3) && n->session->IsActive(4),
        "one dynamic child rollback leaves other slots/parents/map transaction alive");}
    {Room r(3);r.nodes[0]->pending=true;r.nodes[0]->pendingCatalog=5;Check(r.Submit(1,5),"ordinary map terrain planning pending");r.Settle();
     Check(SubmitDrone(r,DronePlan())==2,"map planning cannot starve independent submarine child channel");r.Settle();
     std::uint64_t childTokens[3]{};
     for(unsigned i=0;i<3;++i){const auto& n=r.nodes[i];childTokens[i]=n->lastToken;Check(n->spawns==1 && n->session->IsActive(childTokens[i]),"child activates while ordinary planner is still pending");}
     r.With(0);r.nodes[0]->session->Failed(2);r.Settle();r.nodes[0]->pending=false;r.Settle();
     for(unsigned i=0;i<3;++i){const auto& n=r.nodes[i];Check(n->session->IsActive(n->lastToken) && !n->active[childTokens[i]],"child cancellation does not cancel pending ordinary transaction");}}
}
// Every replay here crosses the production codec, just as Room::Send does.
void Replay(Room& room,const Packet& packet) {
    unsigned char wire[kWireSize]{};Message decoded;
    Check(Encode(packet.message,wire,sizeof(wire)) && Decode(wire,sizeof(wire),decoded),"generation replay traverses production wire codec");
    room.Deliver({packet.from,packet.to,decoded});
}
void PreparedDroneRecycling() {
    Room room(3);
    Check(room.Submit(1,5),"ordinary root support activates beside recycled dynamic children");room.Settle();
    std::uint64_t roots[3]{};for(unsigned i=0;i<3;++i)roots[i]=room.nodes[i]->lastToken;
    Packet staleBegin{},staleUnit{},stalePrepare{},staleCommit{},staleActivate{},staleCancel{},staleReady{},staleResult{},staleActivated{};
    std::uint64_t firstChild=0;
    for(unsigned i=0;i<3*kMaxTransactions;++i) {
        const auto before=room.history.size();const auto token=SubmitDrone(room,DronePlan());
        Check(token!=0,"completed dynamic cancellations do not consume a mission lifetime slot");
        const bool sameSlot=i==kMaxTransactions-1;
        if(sameSlot){room.dropTo=0;room.dropKind=Kind::ready;}
        const auto spawns=room.nodes[0]->spawns;room.Settle();
        if(sameSlot) {
            Packet ready[3]{};for(std::size_t h=before;h<room.history.size();++h)if(room.history[h].message.kind==Kind::ready)ready[room.history[h].from]=room.history[h];
            Check(ready[1].message.transaction>kMaxTransactions && (ready[1].message.transaction-1)%kMaxTransactions==(staleReady.message.transaction-1)%kMaxTransactions,
                "replay fixture really reuses the exact former wire slot");
            room.dropTo=999;Replay(room,ready[2]);Replay(room,staleReady);room.Settle();
            Check(room.nodes[0]->spawns==spawns,"stale positive READY cannot satisfy a new generation's missing prepare ACK");
            room.dropTo=0;room.dropKind=Kind::result;Replay(room,ready[1]);room.Settle();
            Packet results[3]{};for(std::size_t h=before;h<room.history.size();++h)if(room.history[h].message.kind==Kind::result && room.history[h].message.ok)results[room.history[h].from]=room.history[h];
            room.dropTo=999;Replay(room,results[2]);Replay(room,staleResult);room.Settle();
            Check(!room.nodes[0]->session->IsActive(token),"stale positive RESULT cannot satisfy a new generation's missing create ACK");
            Replay(room,results[1]);room.Settle();
        }
        std::uint64_t children[3]{};
        for(unsigned n=0;n<3;++n){children[n]=room.nodes[n]->lastToken;Check(room.nodes[n]->session->IsActive(children[n]),"new generation activates on every real-wire peer");}
        if(!i)firstChild=token;
        if(!i)for(std::size_t h=before;h<room.history.size();++h) {
            const auto& p=room.history[h];
            if(p.from==0 && p.to==1) {
                if(p.message.kind==Kind::begin)staleBegin=p;
                if(p.message.kind==Kind::unit)staleUnit=p;
                if(p.message.kind==Kind::prepare)stalePrepare=p;
                if(p.message.kind==Kind::commit)staleCommit=p;
                if(p.message.kind==Kind::activate)staleActivate=p;
            }
            if(p.from==1 && p.to==0) {
                if(p.message.kind==Kind::ready)staleReady=p;
                if(p.message.kind==Kind::result)staleResult=p;
                if(p.message.kind==Kind::activated)staleActivated=p;
            }
        }
        if(sameSlot) {
            const auto guestSpawns=room.nodes[1]->spawns;
            for(const auto& packet:{staleBegin,staleUnit,stalePrepare,staleCommit,staleActivate,staleCancel,staleReady,staleResult,staleActivated})Replay(room,packet);
            auto failure=staleResult;failure.message.ok=0;Replay(room,failure);room.Settle();
            for(unsigned n=0;n<3;++n)Check(room.nodes[n]->session->IsActive(children[n]) && room.nodes[n]->session->IsActive(roots[n]),"old generation BEGIN/ACK/cancel/result cannot affect replacement or root support");
            Check(room.nodes[1]->spawns==guestSpawns,"replayed retired BEGIN cannot allocate another callback token or object");
            for(unsigned n=0;n<3;++n){room.With(n);room.nodes[n]->session->Failed(firstChild);}room.Settle();
            Check(room.nodes[0]->session->IsActive(token),"late old native failure callback cannot cancel a reused slot");
        }
        room.With(sameSlot ? 1u : 0u);current->session->Failed(sameSlot ? children[1] : token);room.Settle();
        if(!i)for(std::size_t h=before;h<room.history.size();++h)if(room.history[h].from==0 && room.history[h].to==1 && room.history[h].message.kind==Kind::cancel)staleCancel=room.history[h];
        for(unsigned n=0;n<3;++n)Check(!room.nodes[n]->session->IsActive(children[n]) && room.nodes[n]->session->IsActive(roots[n]),"only fully cancelled child retires while ordinary root remains live");
    }
    for(const auto& node:room.nodes)Check(node->session->ActiveCount()==1,"hundreds of sequential generations retain exactly their ordinary root");
    room.Step(2100);Check(room.Submit(1,kRescue),"rescue request still has capacity after hundreds of dynamic children");room.Settle();
    for(const auto& node:room.nodes)Check(node->session->ActiveCount()==2,"reclaimed storage remains shared with real rescue transactions");
}
void PreparedDroneCapacity() {
    Room room(3);std::vector<std::uint64_t> tokens;
    for(unsigned i=0;i<kMaxTransactions;++i) {
        const auto token=SubmitDrone(room,DronePlan(crew::kSupportSubDroneResource,1000+i));
        Check(token!=0,"all bounded live storage slots remain available concurrently");tokens.push_back(token);room.Settle();
    }
    Check(!SubmitDrone(room,DronePlan(crew::kSupportSubDroneResource,9999)),"concurrent live capacity fails explicitly at the configured bound");
    room.dropTo=2;room.dropKind=Kind::cancel;room.With(0);room.nodes[0]->session->Failed(tokens.front());room.Settle();
    Check(!SubmitDrone(room,DronePlan(crew::kSupportSubDroneResource,9999)),"a cancelled slot remains reserved until every peer acknowledges destruction");
    room.dropTo=999;room.failTo=0;room.failKind=Kind::result;room.Step(1100);room.Settle();
    Check(!SubmitDrone(room,DronePlan(crew::kSupportSubDroneResource,9999)),"lost cancel acknowledgement cannot expose storage for a new generation");
    room.failTo=999;room.Step(1100);room.Settle();
    const auto replacement=SubmitDrone(room,DronePlan(crew::kSupportSubDroneResource,9999));
    Check(replacement!=0,"retransmitted cancellation quorum releases precisely one full-table slot");room.Settle();
    for(const auto& node:room.nodes)Check(node->session->ActiveCount()==kMaxTransactions,"a reused slot preserves all other concurrent transactions");
    Packet oldAck{};for(const auto& packet:room.history)if(packet.from==2 && packet.to==0 && packet.message.kind==Kind::result && !packet.message.ok && packet.message.transaction==1)oldAck=packet;
    room.dropTo=2;room.dropKind=Kind::cancel;room.With(0);room.nodes[0]->session->Failed(replacement);room.Settle();
    Replay(room,oldAck);
    Check(!SubmitDrone(room,DronePlan(crew::kSupportSubDroneResource,10000)),"old generation cancel ACK cannot release a newly cancelled slot with a missing peer");
    room.dropTo=999;room.Step(1100);room.Settle();
    Check(SubmitDrone(room,DronePlan(crew::kSupportSubDroneResource,10000))!=0,"current generation cancel ACK permits another safe reuse of the same sole free slot");room.Settle();
    for(const auto& node:room.nodes)Check(node->session->ActiveCount()==kMaxTransactions,"repeated same-slot reuse never collects the other 127 active roots");
}
void PreparedDroneFailureIsNotCancellationAck() {
    Room room(3);std::vector<std::uint64_t> tokens;
    for(unsigned i=0;i<kMaxTransactions;++i) {
        const auto token=SubmitDrone(room,DronePlan(crew::kSupportSubDroneResource,1000+i));
        Check(token!=0,"fill every live slot before guest-local cancellation failure");tokens.push_back(token);room.Settle();
    }
    const auto epoch=room.nodes[0]->session->Epoch();
    room.failTo=1;room.failKind=Kind::cancel;
    room.With(1);room.nodes[1]->session->Failed(tokens.front());room.Pump();room.Step(1100);room.Settle();
    Check(room.nodes[0]->session->Epoch()==epoch && room.nodes[0]->session->Ready(),"failed CANCEL enqueue retains the current production Session generation");
    Check(!SubmitDrone(room,DronePlan(crew::kSupportSubDroneResource,9999)),"repeated ordinary failure RESULT cannot acknowledge an unreceived host CANCEL or release its slot");
    Packet oldFailure{};for(const auto& packet:room.history)if(packet.from==1 && packet.to==0 && packet.message.kind==Kind::result && !packet.message.ok)oldFailure=packet;
    room.failTo=999;room.Step(1100);room.Settle();
    const auto replacement=SubmitDrone(room,DronePlan(crew::kSupportSubDroneResource,9999));
    Check(replacement!=0,"actual host cancellation acknowledgement releases the slot after enqueue recovers");room.Settle();
    auto oldCancel=oldFailure;oldCancel.from=0;oldCancel.to=1;oldCancel.message.kind=Kind::cancel;
    Replay(room,oldFailure);Replay(room,oldCancel);room.Settle();
    for(const auto& node:room.nodes)Check(node->session->ActiveCount()==kMaxTransactions,"all peers activate the replacement and preserve the other 127 live transactions");
    Check(room.nodes[0]->session->IsActive(replacement),"old ordinary failure and old cancellation cannot affect the live replacement generation");
}
void PreparedDroneCancelledBeforeBegin() {
    Room room(3);
    for(unsigned i=0;i<kMaxTransactions+2;++i) {
        room.nodes[0]->rejectValidationAt=room.nodes[0]->validations+2;
        const auto token=SubmitDrone(room,DronePlan());Check(token!=0,"failed plans before BEGIN also release their slot through cancellation quorum");room.Settle();
        for(const auto& node:room.nodes)Check(!node->spawns && !node->session->IsActive(token),"cancel before BEGIN never invents a child on any peer");
    }
}
void LegacyTransactionCapacity() {
    Room room(3);room.legacyNode=2;room.legacyCaps=kCapabilities;room.legacyExt=kExtensions;
    room.legacyDetails=kDetailPvpSupport|kDetailDynamicDrones;room.Step(1100);room.Settle();
    Check(!room.nodes[0]->session->PeersHaveDetails(kDetailTransactionGenerations),"dynamic-only legacy peer does not claim generation codec support");
    Check(!SubmitDrone(room,DronePlan()),"dynamic admission requires recyclable-slot compatibility before consuming the bounded legacy budget");
    for(unsigned i=0;i<kMaxTransactions;++i) {
        room.Step(2100);Check(room.Submit(0,5),"legacy ordinary support retains every original transaction slot");room.Settle();
        room.With(0);room.nodes[0]->session->Failed(room.nodes[0]->lastToken);room.Settle();
    }
    room.Step(2100);Check(!room.Submit(0,5),"old peer room refuses at its original tombstone bound instead of sending unsupported generation IDs");
    for(const auto& packet:room.history)Check(packet.message.transaction<=kMaxTransactions,"mixed old room never emits a transaction an old v2 decoder rejects");
    Packet currentHello{};for(const auto& packet:room.history)if(packet.from==2 && packet.to==0 && packet.message.kind==Kind::hello)currentHello=packet;
    room.legacyNode=999;currentHello.message.unit.role=kDetails;Replay(room,currentHello);
    Check(room.nodes[0]->session->PeersHaveDetails(kDetailTransactionGenerations),"authenticated current capability refresh can enable generation support");
    Check(!SubmitDrone(room,DronePlan()),"capability refresh cannot retroactively turn an unmarked legacy failure/response into explicit cancellation ACK");
    room.Step(1100);room.Settle();const auto upgraded=SubmitDrone(room,DronePlan());
    Check(upgraded!=0,"refreshed peer's newly processed host CANCEL and explicit ACK can safely release its old tombstones");room.Settle();
    for(const auto& node:room.nodes)Check(node->session->ActiveCount()==1,"all peers converge after legacy tombstones receive real generation ACKs");
    Room oldHost(2);oldHost.oldHost=true;oldHost.Step(1100);oldHost.Settle();
    Packet begin{0,1,{}};begin.message.kind=Kind::begin;begin.message.epoch=oldHost.nodes[1]->session->Epoch();
    begin.message.transaction=kMaxTransactions+1;begin.message.catalog=5;begin.message.count=1;Replay(oldHost,begin);
    auto commit=begin;commit.message.kind=Kind::commit;Replay(oldHost,commit);
    Check(!oldHost.nodes[1]->spawns,"client requires negotiated host generations before accepting a high-ID BEGIN");
}
void GenerationDetailsWithdrawal() {
    for(unsigned mode=0;mode<3;++mode) {
        Room room(3);
        for(unsigned i=0;i<kMaxTransactions;++i) {
            const auto token=SubmitDrone(room,DronePlan());Check(token!=0,"generation withdrawal fixture retires one original wire cycle");room.Settle();
            room.With(0);room.nodes[0]->session->Failed(token);room.Settle();
        }
        if(mode==0){room.dropTo=1;room.dropKind=Kind::prepare;}
        else {room.dropTo=0;room.dropKind=Kind::ready;}
        room.Step(2100);Check(room.Submit(0,5),"ordinary support can use a recycled wire generation");room.Settle();
        const auto spawns=room.nodes[1]->spawns;
        if(mode<2) {
            Packet welcome{};for(const auto& packet:room.history)if(packet.to==1 && packet.message.kind==Kind::welcome)welcome=packet;
            welcome.message.unit.role=kDetails&~kDetailTransactionGenerations;Replay(room,welcome);
            Packet step{0,1,{}};step.message.epoch=room.nodes[1]->session->Epoch();step.message.transaction=kMaxTransactions+1;
            step.message.kind=Kind::prepare;Replay(room,step);step.message.kind=Kind::commit;Replay(room,step);
            Check(room.nodes[1]->spawns==spawns,"client rechecks host generation capability before prepare/create of ordinary high-ID support");
        } else {
            room.legacyNode=2;room.legacyCaps=kCapabilities;room.legacyExt=kExtensions;
            room.legacyDetails=kDetails&~kDetailTransactionGenerations;room.Step(1100);room.Settle();
            for(const auto& node:room.nodes)Check(node->spawns==spawns,"peer generation capability withdrawal prevents ordinary high-ID native creation");
        }
    }
}
void PreparedDroneDetails() {
    {Room r(3);r.legacyNode=2;r.legacyCaps=kCapabilities;r.legacyExt=kExtensions;r.legacyDetails=kDetailPvpSupport;r.Step(1100);r.Settle();
     Check(r.nodes[0]->session->PeersHaveDetails(kDetailPvpSupport) && !r.nodes[0]->session->PeersHaveDetails(kDetailDynamicDrones),
        "PVP-only old endpoint is distinguished from dynamic-child capable endpoint");
     Check(!SubmitDrone(r,DronePlan()),"one old detail endpoint rejects dynamic admission before allocating a token");
     bool began=false;for(const auto& packet:r.history)began=began || packet.message.kind==Kind::begin;
     Check(!began,"no dynamic BEGIN is sent to an old detail endpoint");
     Check(r.Submit(1,5),"old endpoint retains ordinary map support");r.Settle();for(const auto& n:r.nodes)Check(n->spawns==1,"legacy stock support behavior preserved");}
    for(bool assembled:{false,true}) {
        Room r(3);if(assembled){r.dropTo=0;r.dropKind=Kind::ready;}else{r.dropTo=1;r.dropKind=Kind::prepare;}
        Check(SubmitDrone(r,DronePlan())==1,"dynamic capability-loss fixture accepted");r.Settle();
        Packet welcome{};for(const auto& packet:r.history)if(packet.to==1 && packet.message.kind==Kind::welcome)welcome=packet;
        welcome.message.unit.role=kDetailPvpSupport;r.Deliver(welcome);
        Packet step{0,1,{}};step.message.epoch=r.nodes[1]->session->Epoch();step.message.transaction=1;
        step.message.kind=Kind::prepare;r.Deliver(step);step.message.kind=Kind::commit;r.Deliver(step);
        Check(!r.nodes[1]->spawns,"guest rechecks dynamic capability at prepare and immediately before native create");
    }
    for(bool spawned:{false,true}) {
        Room r(3);r.dropTo=0;r.dropKind=spawned ? Kind::result : Kind::ready;
        Check(SubmitDrone(r,DronePlan())==1,"host capability-loss fixture accepted");r.Settle();
        r.legacyNode=2;r.legacyCaps=kCapabilities;r.legacyExt=kExtensions;r.legacyDetails=kDetailPvpSupport;r.Step(1100);r.Settle();
        for(const auto& n:r.nodes)Check(!n->active[1] && !n->session->IsActive(1),"host capability loss before commit/finalize rolls back dynamic transaction");
        if(!spawned)for(const auto& n:r.nodes)Check(!n->spawns,"capability withdrawn before commit never creates children");
        else for(const auto& n:r.nodes)Check(n->spawns==1 && n->destroys>=1,"registered children roll back when capability disappears before activate");
    }
}
} // namespace
int main() { PlanSpecComparison();PvpSupportCodec();PvpSupport();Codec();Capabilities();Variants();Loadout();RoomBuilds();RescueChannel();MapQueue();TransportResume();LocalOutcomes();Success();Failure();Epoch();Existing();ActivationAndTransport();RequestOutcomes();PreparedDroneAdmission();PreparedDroneResources();PreparedDroneLifecycle();PreparedDroneFailure();PreparedDroneChannels();PreparedDroneDetails();PreparedDroneRecycling();PreparedDroneCapacity();PreparedDroneFailureIsNotCancellationAck();PreparedDroneCancelledBeforeBegin();LegacyTransactionCapacity();GenerationDetailsWithdrawal();std::printf("support protocol: %d checks passed\n",checks); }
