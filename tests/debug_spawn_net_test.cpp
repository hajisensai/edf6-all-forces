// Three real support-protocol peers, with only native registration replaced by a ledger.
#include "../src/debug_spawn_net.h"
#include "../src/support_protocol.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <vector>

using namespace crew::support_net;
namespace ds=crew::debugspawn;
namespace {
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
void Matrix(float* m,const float* at) noexcept {
    std::memset(m,0,16*sizeof(float));
    m[2]=-1;m[5]=1;m[8]=1;m[15]=1;
    for(unsigned i=0;i<3;++i)m[12+i]=at[i];
}
bool Derive(std::uint32_t ordinal,unsigned char* out) noexcept {
    std::memset(out,0,32);out[0]=42;out[12]=5;
    std::memcpy(out+4,&ordinal,4);std::memcpy(out+24,&ordinal,4);return true;
}
int Enemy() {
    for(int i=0;i<ds::kEntryCount;++i)if(ds::kEntries[i].how==ds::How::enemy)return i;
    Check(false,"fixture has an enemy row");return -1;
}
Plan ValidEnemy(int row) {
    const float at[3]={42,8,-13};float matrix[16];Matrix(matrix,at);Plan p;
    Check(ds::BuildNetworkPlan(row,matrix,&p),"enemy row builds a rigid plan");
    Check(Derive(0x40000000u,p.units[0].netId),"fixture derives native identity");return p;
}
struct Room;
struct Registration { std::uint64_t token=0;Unit unit{};bool alive=true; };
struct Node {
    Room* room=nullptr;unsigned id=0;
    std::unique_ptr<Session> session=std::make_unique<Session>();
    std::uint64_t nonce=0;
    unsigned plans=0,validations=0,spawns=0,destroys=0;
    bool failSpawn=false,remote=false;
    Plan last{};
    std::vector<Registration> registrations;
};
Node* current=nullptr;
struct Packet { unsigned from=0,to=0;Message message{}; };
struct Room {
    std::array<std::unique_ptr<Node>,3> nodes;
    std::deque<Packet> packets;
    std::vector<Packet> history;
    std::uint64_t now=10000;
    unsigned corruptDescriptorTo=99,dropFrom=99,dropTo=99;
    Kind dropKind=Kind::result;
    Room();
    unsigned Peer(unsigned local,unsigned global) const { return global<local ? global+1 : global; }
    unsigned Global(unsigned local,unsigned peer) const { return peer<=local ? peer-1 : peer; }
    void With(unsigned n) { current=nodes[n].get(); }
    void Deliver(const Packet& p) {
        unsigned char wire[kWireSize];Message back;
        Check(Encode(p.message,wire,sizeof(wire)) && Decode(wire,sizeof(wire),back),"delivered packets use the real wire codec");
        With(p.to);current->session->Receive(Peer(p.to,p.from),back,now);
    }
    void Pump() {
        unsigned guard=0;
        while(!packets.empty()) {
            Check(++guard<10000,"protocol traffic stays bounded");
            const Packet p=packets.front();packets.pop_front();
            if(p.from==dropFrom && p.to==dropTo && p.message.kind==dropKind)continue;
            Deliver(p);
        }
    }
    void Step(unsigned ms=100) {
        now+=ms;
        for(unsigned i=0;i<nodes.size();++i){With(i);current->session->Tick(now);}
        Pump();
    }
    void Settle() { for(unsigned i=0;i<6;++i)Step(); }
    void Start() {
        for(unsigned i=0;i<nodes.size();++i){With(i);current->session->Start(i==0,2,i ? Peer(i,0) : 0,now);}
        Settle();
        for(const auto& n:nodes)Check(n->session->Ready(),"all three authenticated peers are ready");
    }
    bool Submit(unsigned node=1,std::uint64_t descriptor=0) {
        const float at[3]={42,8,-13};With(node);
        return current->session->Submit(ds::kNetworkCatalog,at,now,descriptor ? descriptor : ds::Descriptor(ds::kEntries[Enemy()]));
    }
    Packet Find(Kind kind,unsigned to) const {
        for(const auto& p:history)if(p.message.kind==kind && p.to==to)return p;
        Check(false,"expected packet was sent");return {};
    }
    bool Sent(Kind kind) const {
        for(const auto& p:history)if(p.message.kind==kind)return true;
        return false;
    }
};
bool Send(void* ctx,std::uint32_t peer,const Message& message) noexcept {
    auto& n=*static_cast<Node*>(ctx);auto& r=*n.room;
    const unsigned to=r.Global(n.id,peer);Message sent=message,back;
    if(to==r.corruptDescriptorTo && sent.kind==Kind::unit)sent.challenge^=1;
    unsigned char wire[kWireSize];
    if(!Encode(sent,wire,sizeof(wire)) || !Decode(wire,sizeof(wire),back))return false;
    const Packet packet{n.id,to,back};r.packets.push_back(packet);r.history.push_back(packet);return true;
}
std::uint64_t Nonce(void* ctx) noexcept { return ++static_cast<Node*>(ctx)->nonce; }
PlanResult PlanCall(std::uint32_t catalog,const float* at,std::uint64_t descriptor,Plan* out) noexcept {
    ++current->plans;
    if(catalog!=ds::kNetworkCatalog)return PlanResult::refused;
    float matrix[16];Matrix(matrix,at);
    return ds::BuildNetworkPlan(ds::NetworkRow(descriptor),matrix,out) ? PlanResult::ready : PlanResult::refused;
}
bool Validate(const Plan& p) noexcept {
    ++current->validations;return ds::NetworkPlanRow(p)>=0;
}
bool Spawn(std::uint64_t token,const Plan& p,bool remote) noexcept {
    ++current->spawns;current->last=p;current->remote=remote;
    if(ds::NetworkPlanRow(p)<0)return false;
    // Mock the native CreateObject/RegisterObject pair, including partial creation on failure.
    for(unsigned i=0;i<p.count;++i)current->registrations.push_back({token,p.units[i],true});
    return !current->failSpawn;
}
void Destroy(std::uint64_t token) noexcept {
    ++current->destroys;
    for(auto& r:current->registrations)if(r.token==token)r.alive=false;
}
Room::Room() {
    for(unsigned i=0;i<nodes.size();++i) {
        auto n=std::make_unique<Node>();n->room=this;n->id=i;n->nonce=100000+1000*i;
        Backend backend{n.get(),&Send,&Nonce,{&PlanCall,&Validate,&Spawn,&Destroy,&Derive}};
        n->session->Configure(backend);nodes[i]=std::move(n);
    }
    Start();
}
void SuccessAndReplay() {
    Room r;Check(r.Submit(),"client requests an enemy by stable descriptor");r.Settle();
    Check(r.nodes[0]->plans==1 && !r.nodes[1]->plans && !r.nodes[2]->plans,"only host chooses the enemy spawn plan");
    const auto& expected=r.nodes[0]->last;
    for(const auto& n:r.nodes) {
        Check(n->spawns==1 && n->registrations.size()==1 && n->registrations[0].alive,"every peer registers exactly one enemy");
        Check(n->session->IsActive(n->registrations[0].token),"enemy activates after the transaction succeeds");
        const auto& p=n->last;
        Check(p.catalogId==expected.catalogId && p.count==1 && !std::memcmp(p.target,expected.target,sizeof(p.target)),"same enemy target on all peers");
        const auto& a=p.units[0];const auto& b=expected.units[0];
        Check(a.resourceId==b.resourceId && a.variant==b.variant && a.role==b.role &&
              !std::memcmp(a.matrix,b.matrix,sizeof(a.matrix)) && !std::memcmp(a.netId,b.netId,sizeof(a.netId)),
              "resource, full descriptor, matrix and registered native NetId agree across all peers");
        Check(!std::memcmp(n->registrations[0].unit.netId,a.netId,32),"native registration mock uses the supplied canonical NetId");
    }
    Check(!r.nodes[0]->remote && r.nodes[1]->remote && r.nodes[2]->remote,"creation receives host/client authority");
    const auto commit=r.Find(Kind::commit,1),begin=r.Find(Kind::begin,1),request=r.Find(Kind::request,0);
    r.Deliver(commit);r.Deliver(commit);r.Deliver(begin);r.Deliver(request);r.Settle();
    for(const auto& n:r.nodes)Check(n->spawns==1 && n->registrations.size()==1,"duplicate commit and request cannot register more enemies");
}
void PrepareMismatch() {
    Room r;r.corruptDescriptorTo=2;Check(r.Submit(),"descriptor mismatch request submitted");r.Settle();
    Check(r.nodes[2]->validations>0,"mismatched peer validates the complete prepared plan");
    for(const auto& n:r.nodes)Check(!n->spawns && n->registrations.empty() && !n->session->ActiveCount(),"descriptor mismatch at prepare prevents any native creation");
    Check(!r.Sent(Kind::commit) && !r.Sent(Kind::activate),"a rejected prepare cannot commit or activate");
}
void SpawnFailure() {
    Room r;r.nodes[2]->failSpawn=true;Check(r.Submit(),"guest spawn failure request submitted");r.Settle();
    for(const auto& n:r.nodes) {
        Check(n->spawns==1 && n->destroys>=1 && !n->session->ActiveCount(),"one guest creation failure rolls back every peer");
        for(const auto& registration:n->registrations)Check(!registration.alive,"rollback unregisters even partially created enemies");
    }
    Check(!r.Sent(Kind::activate),"failure never emits activation");
    const auto replay=r.Find(Kind::commit,2);r.Deliver(replay);r.Settle();
    Check(r.nodes[2]->spawns==1,"cancelled transaction cannot respawn after a repeated commit");
}
void ActivationGate() {
    Room r;r.dropFrom=2;r.dropTo=0;r.dropKind=Kind::result;
    Check(r.Submit(),"activation gate request submitted");r.Settle();
    for(const auto& n:r.nodes)Check(n->spawns==1 && !n->session->ActiveCount(),"created enemy stays inactive until every spawn result arrives");
    Check(!r.Sent(Kind::activate),"host sends no early activation");
    std::vector<Packet> results;
    for(const auto& p:r.history)if(p.from==2 && p.to==0 && p.message.kind==Kind::result && p.message.ok)results.push_back(p);
    Check(!results.empty(),"fixture withheld the final guest success");r.dropFrom=99;
    for(const auto& p:results)r.Deliver(p);
    r.Settle();
    for(const auto& n:r.nodes)Check(n->session->ActiveCount()==1 && n->spawns==1,"all-peer success opens the activation gate without another creation");
}
void StaleEpoch() {
    Room r;Check(r.Submit(),"old mission request submitted");r.Settle();
    const auto oldEpoch=r.nodes[0]->session->Epoch();
    std::vector<Packet> stale;
    for(const auto& p:r.history)if((p.to==1 && (p.message.kind==Kind::begin || p.message.kind==Kind::unit ||
        p.message.kind==Kind::prepare || p.message.kind==Kind::commit || p.message.kind==Kind::activate)) ||
        (p.to==0 && p.message.kind==Kind::request))stale.push_back(p);
    for(unsigned i=0;i<r.nodes.size();++i){r.With(i);current->session->Stop();}
    r.packets.clear();r.Start();
    Check(r.nodes[0]->session->Epoch()!=oldEpoch,"mission restart rotates the host epoch");
    for(const auto& p:stale)r.Deliver(p);
    r.Settle();
    for(const auto& n:r.nodes)Check(n->spawns==1 && !n->session->ActiveCount(),"complete stale transaction traffic cannot create or activate an enemy in a new epoch");
    r.Step(2500);Check(r.Submit(),"new mission accepts a fresh enemy request");r.Settle();
    for(const auto& n:r.nodes)Check(n->spawns==2 && n->session->ActiveCount()==1,"fresh epoch transaction succeeds after stale traffic");
    Check(std::memcmp(r.nodes[0]->registrations[0].unit.netId,r.nodes[0]->registrations[1].unit.netId,32)!=0,"new mission never reuses a previous native NetId");
}
void CatalogIdentity() {
    std::vector<ds::Entry> reordered(std::begin(ds::kEntries),std::end(ds::kEntries));
    std::reverse(reordered.begin(),reordered.end());
    for(int row=0;row<ds::kEntryCount;++row) {
        const auto& entry=ds::kEntries[row];if(entry.how!=ds::How::enemy)continue;
        const auto descriptor=ds::Descriptor(entry);const auto resource=ds::Resource(entry);
        unsigned matches=0;
        for(const auto& moved:reordered)if(!std::strcmp(moved.id,entry.id)) {
            ++matches;Check(ds::Descriptor(moved)==descriptor && ds::Resource(moved)==resource,"catalog reordering preserves enemy wire identities");
        }
        Check(matches==1 && ds::NetworkRow(resource,descriptor)==row && ds::NetworkRow(descriptor)==row,"full stable descriptor resolves exactly one production catalog row");
    }
    const int row=Enemy();const auto original=ds::kEntries[row];const auto hash=ds::Descriptor(original);
    auto changed=original;changed.sgo=L"/different/enemy.sgo";
    Check(ds::Descriptor(changed)!=hash,"changed enemy model path changes the full descriptor");
    changed=original;changed.lift+=1;
    Check(ds::Descriptor(changed)!=hash,"changed spawn lift changes the full descriptor");
    changed=original;changed.files=L"different.sgo\0";
    Check(ds::Descriptor(changed)!=hash,"changed required files change the full descriptor");
    changed=original;changed.id="different-enemy";
    Check(ds::Descriptor(changed)!=hash,"changed stable name changes the full descriptor");
    Plan p=ValidEnemy(row);p.units[0].variant=ds::Descriptor(changed);
    Check(ds::NetworkPlanRow(p)<0,"same resource with a mismatched full descriptor is refused");
}
void InvalidPlans() {
    const int row=Enemy();Plan p=ValidEnemy(row);const auto valid=p;
    Check(ds::NetworkPlanRow(p)==row,"valid enemy plan resolves its native row");
    Check(!ds::BuildNetworkPlan(-1,p.units[0].matrix,&p) && !ds::BuildNetworkPlan(ds::kEntryCount,p.units[0].matrix,&p),"out-of-range catalog rows rejected");
    Check(!ds::BuildNetworkPlan(row,nullptr,&p) && !ds::BuildNetworkPlan(row,p.units[0].matrix,nullptr),"missing plan inputs rejected");
    for(int i=0;i<ds::kEntryCount;++i)if(ds::kEntries[i].how!=ds::How::enemy)
        Check(!ds::BuildNetworkPlan(i,valid.units[0].matrix,&p),"non-enemy rows never enter enemy network catalog");
    float bad[16];std::memcpy(bad,valid.units[0].matrix,sizeof(bad));bad[0]=std::numeric_limits<float>::quiet_NaN();
    Check(!ds::BuildNetworkPlan(row,bad,&p),"NaN matrix rejected before planning");
    std::memcpy(bad,valid.units[0].matrix,sizeof(bad));bad[12]=std::numeric_limits<float>::infinity();
    Check(!ds::BuildNetworkPlan(row,bad,&p),"infinite position rejected before planning");
    std::memcpy(bad,valid.units[0].matrix,sizeof(bad));bad[5]=2;
    Check(!ds::BuildNetworkPlan(row,bad,&p),"non-rigid scale rejected before planning");
    std::memcpy(bad,valid.units[0].matrix,sizeof(bad));bad[3]=1;
    Check(!ds::BuildNetworkPlan(row,bad,&p),"perspective matrix rejected before planning");
    p=valid;p.catalogId=0;Check(ds::NetworkPlanRow(p)<0,"wrong catalog rejected");
    p=valid;p.count=0;Check(ds::NetworkPlanRow(p)<0,"empty plan rejected");
    p=valid;p.count=2;p.units[1]=p.units[0];Derive(0x40000001u,p.units[1].netId);
    Check(ValidPlan(p) && ds::NetworkPlanRow(p)<0,"otherwise-valid multi-enemy plan rejected by single-row contract");
    p=valid;p.units[0].role=1;Check(ds::NetworkPlanRow(p)<0,"enemy with a crew-parent role rejected");
    p=valid;p.target[0]+=1;Check(ds::NetworkPlanRow(p)<0,"plan target must equal the enemy matrix position");
    p=valid;p.units[0].resourceId=0;Check(ds::NetworkPlanRow(p)<0,"zero resource rejected");
    p=valid;p.units[0].variant=0;Check(ds::NetworkPlanRow(p)<0,"missing full descriptor rejected");
    p=valid;std::memset(p.units[0].netId,0,32);
    Check(ds::NetworkPlanRow(p)<0 && ds::NetworkPlanRow(p,false)==row,"native IDs are mandatory at prepare and optional only while planning");
    p=valid;p.units[0].netId[12]=4;Check(ds::NetworkPlanRow(p)<0,"wrong native identity type rejected");
    p=valid;Derive(0x3fffffffu,p.units[0].netId);Check(ds::NetworkPlanRow(p)<0,"low unreserved native ordinal rejected");
    p=valid;Derive(0x80000000u,p.units[0].netId);Check(ds::NetworkPlanRow(p)<0,"native ordinal overflow rejected");
    p=valid;p.units[0].netId[20]=1;Check(ds::NetworkPlanRow(p)<0,"uninitialized native identity padding rejected");
    Message unit;unit.kind=Kind::unit;unit.epoch=1;unit.transaction=1;unit.unit=p.units[0];
    unsigned char bytes[kWireSize];Check(!Encode(unit,bytes,sizeof(bytes)),"wire codec also rejects malformed native IDs");
    unit.unit=valid.units[0];unit.unit.matrix[5]=2;
    Check(!Encode(unit,bytes,sizeof(bytes)),"wire codec also rejects malformed enemy matrices");
}
}
int main() {
    CatalogIdentity();InvalidPlans();SuccessAndReplay();PrepareMismatch();SpawnFailure();ActivationGate();StaleEpoch();
    std::printf("debug_spawn_net: %d checks passed\n",checks);return 0;
}
