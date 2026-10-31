// Three authenticated game-thread peers through the production byte codec and protocol. Only native actor operations
// are mocks: their ledger retains captured AI/input baselines until Restore succeeds. No game or transport SDK needed.
// Compile this file alone, C++17 /W4 /WX /utf-8 /permissive-; the production implementation is header-only.
#include "../src/enemy_control_protocol.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>
using namespace enemy_control_protocol;
namespace {
int checks=0;
void Check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
NativeId Id(unsigned value){NativeId id{};id[0]=static_cast<std::uint8_t>(value);id[12]=5;id[24]=71;return id;}
// A player's ID: its object reference (net_object_ref.h: mission slot `slot`), never a NetId.
NativeId Player(unsigned slot){NativeId id{};crew::netref::MakePlayer(static_cast<int>(slot),id.data());return id;}
constexpr std::uint64_t kProfile=0x1234567812345678ull;
constexpr std::uint8_t kInput[]={0x42,0x17,0x80,0};
struct Room;
struct Baseline {LeaseSpec spec{};bool suppressed=true;};
struct Node {
    Room* room=nullptr;unsigned id=0;std::string puid;NativeId player{},secondPlayer{};Context context{};Protocol protocol;
    bool rejectPrepare=false,failBegin=false,failActivate=false,failApply=false,rejectInput=false,failRestore=false,rejectResolve=false;
    unsigned restoreFailures=0,prepares=0,begins=0,applies=0,restores=0,resolves=0,validates=0,activates=0;
    std::map<std::uint64_t,Baseline> native;
    std::vector<InputMeta> inputs;
};
struct Packet {unsigned from=0,to=0;std::vector<std::uint8_t> bytes;};
struct Room {
    std::vector<std::unique_ptr<Node>> nodes;std::deque<Packet> queue;std::vector<Packet> history;
    std::uint64_t now=10000;unsigned legacy=99,dropFrom=99,dropTo=99,failFrom=99,failTo=99;
    unsigned oldVersion=99;   // this node's datagrams go out as version 3 (a build before the camera-relative move)
    Kind dropKind=Kind::Started,failKind=Kind::Input;bool holdActivationReplies=false;
    explicit Room(unsigned count=3,unsigned legacyPeer=99,unsigned oldPeer=99);
    unsigned Peer(unsigned local,unsigned global) const {return global<local ? global+1 : global;}
    unsigned Global(unsigned local,unsigned peer) const {return peer<=local ? peer-1 : peer;}
    void Deliver(const Packet& p,const char* auth=nullptr) {
        Check(nodes[p.to]->protocol.Receive(Peer(p.to,p.from),auth ? auth : nodes[p.from]->puid.c_str(),p.bytes.data(),p.bytes.size(),now),"owned ECTL packet claimed exactly once");
    }
    void Pump(){unsigned guard=0;while(!queue.empty()){
        Check(++guard<20000,"bounded reliable protocol traffic");const Packet p=queue.front();queue.pop_front();Message m;
        if(p.from==oldVersion){Deliver(p);continue;}   // an older build's: delivered as it is (the receiver decides)
        Check(Decode(p.bytes.data(),p.bytes.size(),m),"every transported datagram decodes");
        if(p.from==dropFrom && p.to==dropTo && m.kind==dropKind)continue;
        if(holdActivationReplies && p.from==2 && p.to==0 && (m.kind==Kind::Activated || m.kind==Kind::Fault))continue;
        Deliver(p);
    }}
    void Step(unsigned ms=100){now+=ms;for(auto& n:nodes)n->protocol.Tick(n->context,now);for(auto& n:nodes)n->protocol.TransportFrame(now);Pump();}
    void Settle(){for(unsigned i=0;i<4;++i)Step();}
    bool Request(unsigned owner=1,unsigned target=100,std::uint64_t profile=0){return nodes[owner]->protocol.Request(nodes[owner]->player,Id(target),profile,now);}
    void Input(unsigned owner=1){const auto lease=Active(owner);Check(nodes[owner]->protocol.SubmitInput(lease,kInput,sizeof(kInput),now),"owner submits canonical opaque input");Pump();}
    std::uint64_t Active(unsigned node=0) const {
        for(std::size_t i=0;i<kMaxLeases;++i){const auto* l=nodes[node]->protocol.LeaseAt(i);if(l->phase==Phase::active)return l->id;}
        Check(false,"active fixture lease exists");return 0;
    }
    const LeaseView* Lease(unsigned node,std::uint64_t id) const {
        for(std::size_t i=0;i<kMaxLeases;++i){const auto* l=nodes[node]->protocol.LeaseAt(i);if(l->id==id)return l;}return nullptr;
    }
    Packet Find(Kind kind,unsigned from,unsigned to) const {
        for(const auto& p:history){Message m;if(p.from==from && p.to==to && Decode(p.bytes.data(),p.bytes.size(),m) && m.kind==kind)return p;}
        Check(false,"expected wire packet recorded");return {};
    }
    bool Sent(Kind kind) const {for(const auto& p:history){Message m;if(Decode(p.bytes.data(),p.bytes.size(),m) && m.kind==kind)return true;}return false;}
    Packet PacketOf(unsigned from,unsigned to,const Message& m) const {Packet p;p.from=from;p.to=to;p.bytes.resize(EncodedSize(m));Check(Encode(m,p.bytes.data(),p.bytes.size()),"fixture packet encodes canonically");return p;}
    void Closed(std::uint64_t id,Result result){for(const auto& n:nodes){const auto* l=Lease(n->id,id);if(!(l && l->phase==Phase::closed && l->localRestored && l->result==result))std::fprintf(stderr,"closure node=%u lease=%llu phase=%u result=%u wanted=%u restored=%d now=%llu\n",n->id,static_cast<unsigned long long>(id),l ? static_cast<unsigned>(l->phase) : 99,l ? static_cast<unsigned>(l->result) : 99,static_cast<unsigned>(result),l && l->localRestored,static_cast<unsigned long long>(now));Check(l && l->phase==Phase::closed && l->localRestored && l->result==result,"all-peer restore barrier closes with the true result");for(const auto& b:n->native)Check(!b.second.suppressed,"native baseline restored on every peer");}}
};
bool Send(void* user,std::uint32_t peer,const void* bytes,std::size_t size){
    auto& n=*static_cast<Node*>(user);auto& r=*n.room;Message m;
    if(!Decode(bytes,size,m))return false;const unsigned to=r.Global(n.id,peer);
    if(n.id==r.failFrom && to==r.failTo && m.kind==r.failKind)return false;
    if(n.id==r.legacy && m.kind==Kind::Hello)m.capability=0;
    Packet p=r.PacketOf(n.id,to,m);
    if(n.id==r.oldVersion)p.bytes[4]=4;
    r.history.push_back(p);r.queue.push_back(p);return true;
}
bool Authenticate(void* user,const char* puid,const NativeId& player){
    const auto& n=*static_cast<Node*>(user);for(const auto& peer:n.room->nodes)if(peer->puid==puid)return peer->player==player || (PlayerId(peer->secondPlayer) && peer->secondPlayer==player);return false;
}
bool Resolve(void* user,LeaseSpec* spec){auto& n=*static_cast<Node*>(user);++n.resolves;if(n.rejectResolve || spec->target[0]<100 || spec->target[0]>=200)return false;spec->profileHash=kProfile;return true;}
bool Prepare(void* user,std::uint64_t,const LeaseSpec& spec){auto& n=*static_cast<Node*>(user);++n.prepares;return !n.rejectPrepare && spec.profileHash==kProfile;}
bool Begin(void* user,std::uint64_t id,const LeaseSpec& spec){auto& n=*static_cast<Node*>(user);++n.begins;n.native[id]={spec,true};return !n.failBegin;}
bool Validate(void* user,std::uint64_t id,const LeaseSpec&,const InputMeta& meta,const void* bytes,std::size_t size){
    auto& n=*static_cast<Node*>(user);++n.validates;
    return !n.rejectInput && size==sizeof(kInput) && !std::memcmp(bytes,kInput,size) && meta.sequence && meta.seed==InputSeed(n.context.epoch,id,meta.sequence);
}
bool Apply(void* user,std::uint64_t,const LeaseSpec&,const InputMeta& meta,const void*,std::size_t){auto& n=*static_cast<Node*>(user);++n.applies;n.inputs.push_back(meta);return !n.failApply;}
bool Restore(void* user,std::uint64_t id,const LeaseSpec&){auto& n=*static_cast<Node*>(user);++n.restores;
    if(n.failRestore || n.restoreFailures){if(n.restoreFailures)--n.restoreFailures;return false;}
    const auto it=n.native.find(id);if(it!=n.native.end())it->second.suppressed=false;return true;
}
bool Activate(void* user,std::uint64_t,const LeaseSpec&){auto& n=*static_cast<Node*>(user);++n.activates;return !n.failActivate;}
Hooks Backend(Node* n){return {n,&Send,&Authenticate,&Resolve,&Prepare,&Begin,&Validate,&Apply,&Restore,&Activate};}
Room::Room(unsigned count,unsigned legacyPeer,unsigned oldPeer):legacy(legacyPeer),oldVersion(oldPeer){
    for(unsigned i=0;i<count;++i){auto n=std::make_unique<Node>();n->id=i;n->room=this;n->puid="player-"+std::to_string(i);n->player=Player(10+i);nodes.push_back(std::move(n));}
    for(auto& n:nodes){n->context={true,n->id==0,777,n->id ? Peer(n->id,0) : 0,count-1,n->puid.c_str()};Check(n->protocol.Configure(Backend(n.get())),"startup backend accepted");}
    Settle();
}
Message Unpack(const Packet& p){Message m;Check(Decode(p.bytes.data(),p.bytes.size(),m),"read recorded wire message");return m;}
void Codec(){
    Message m;m.kind=Kind::Request;m.epoch=123;m.request=7;m.spec.requesterPlayer=Player(10);m.spec.target=Id(100);
    std::vector<std::uint8_t> bytes(EncodedSize(m));Check(Encode(m,bytes.data(),bytes.size()),"request codec accepts canonical native identities");
    Check(bytes[0]=='E' && bytes[1]=='C' && bytes[2]=='T' && bytes[3]=='L' && bytes[4]==7,"explicit ECTL/version7 byte prefix");
    Message back;Check(Decode(bytes.data(),bytes.size(),back) && back.spec.target==m.spec.target,"semantic fields roundtrip without native struct layout");
    Room r;
    for(std::size_t n=0;n<bytes.size();++n){Check(!Decode(bytes.data(),n,back),"all truncated datagrams rejected");Check(r.nodes[0]->protocol.Receive(1,r.nodes[1]->puid.c_str(),bytes.data(),n,r.now)==(n>=4),"owned malformed/truncated magic is consumed, foreign prefix is not");}
    bytes.back()=1;Check(!Decode(bytes.data(),bytes.size(),back),"reserved tail must be zero");bytes.back()=0;
    bytes[4]=2;Check(!Decode(bytes.data(),bytes.size(),back) && r.nodes[0]->protocol.Receive(1,r.nodes[1]->puid.c_str(),bytes.data(),bytes.size(),r.now),"unsupported ECTL version is rejected and still claimed");
    m.spec.requesterPlayer[20]=1;Check(!Encode(m,bytes.data(),bytes.size()),"native ID padding cannot carry uninitialized bytes");
    m.spec.requesterPlayer={};Check(!Encode(m,bytes.data(),bytes.size()),"empty native player ID rejected");
    m.spec.requesterPlayer=Player(10);strcpy_s(m.spec.ownerPuid,"forged-owner");Check(!Encode(m,bytes.data(),bytes.size()),"owner identity cannot be supplied in a request payload");
}
void VariableWireBounds() {
    static_assert(kWirePrefixBytes==192 && kWireBytes==704 && kWireBytes<=1024,"every complete native batch fits the real SDK maximum");
    Message m;m.kind=Kind::Action;m.epoch=123;m.lease=5;m.request=7;m.sequence=1;m.spec.profileHash=kProfile;
    m.spec.requesterPlayer=Player(10);m.spec.target=Id(100);strcpy_s(m.spec.ownerPuid,"player-1");m.capability=1u<<16;
    for(std::size_t size=1;size<=kMaxPoseBytes;++size) {
        m.input={};m.inputSize=static_cast<std::uint16_t>(size);for(std::size_t i=0;i<size;++i)m.input[i]=static_cast<std::uint8_t>(i*31+size);
        std::vector<std::uint8_t> bytes(EncodedSize(m));Check(bytes.size()==kWirePrefixBytes+size && Encode(m,bytes.data(),bytes.size()),"variable wire stores exactly the complete typed batch with no fixed unused padding");
        Message back;Check(Decode(bytes.data(),bytes.size(),back) && back.inputSize==size && back.input==m.input,"all actual typed batch lengths roundtrip strictly");
        bytes.push_back(0);Check(!Decode(bytes.data(),bytes.size(),back),"appended zero padding is rejected rather than accepted as another encoding");bytes.pop_back();
        bytes[189]=255;bytes[190]=255;Check(!Decode(bytes.data(),bytes.size(),back),"oversized declared length is rejected before reading outside the provided envelope");
    }
    m.input={};m.inputSize=0;std::vector<std::uint8_t> bytes(kWirePrefixBytes);Check(!Encode(m,bytes.data(),bytes.size()),"an empty actual action cannot be disguised as a control envelope");
    m.kind=Kind::Hello;m.request=9;m.inputSize=1;m.input[0]=7;bytes.resize(EncodedSize(m));Check(!Encode(m,bytes.data(),bytes.size()),"control messages cannot carry hidden action payload bytes");
}
void Success(){
    for(unsigned owner=0;owner<2;++owner){Room r;for(const auto& n:r.nodes)Check(n->protocol.Ready(),"all authenticated capability hellos required for readiness");
        Check(r.Request(owner),"host or guest requests control");r.Settle();const auto id=r.Active();
        for(const auto& n:r.nodes){const auto* l=r.Lease(n->id,id);Check(l && l->phase==Phase::active && l->spec.profileHash==kProfile && !std::strcmp(l->spec.ownerPuid,r.nodes[owner]->puid.c_str()),"host resolves one profile and authenticated owner for every peer");Check(n->begins==1 && n->activates==1,"each native actor baseline captured and later activated once");}
        r.Input(owner);
        for(const auto& n:r.nodes)Check(n->applies==1 && n->validates==1 && n->inputs[0].seed==r.nodes[0]->inputs[0].seed && n->inputs[0].sequence==1,"host and both clients validate/apply accepted input once with the same deterministic seed");
        Check(r.nodes[owner]->protocol.Exit(id,r.now),"owner requests exit");r.Pump();r.Closed(id,Result::Complete);
    }
}
void CapabilityAndProfiles(){
    {Room r(3,2);for(const auto& n:r.nodes)Check(!n->protocol.Ready(),"one unsupported peer prevents admission everywhere");Check(!r.Request() && !r.nodes[0]->begins,"capability mismatch cannot begin native control");}
    {Room r;Check(r.Request(1,220),"unknown native target request reaches host");r.Settle();Check(!r.nodes[0]->begins && r.nodes[0]->protocol.LastResult()==Result::Unsupported,"host refuses a target without a resolved native profile");}
    {Room r;Check(r.Request(1,100,456),"explicit profile request queued");r.Settle();Check(!r.nodes[0]->prepares && !r.nodes[0]->begins,"mismatched requested profile rejected before prepare");}
    {Room r;r.nodes[2]->rejectPrepare=true;Check(r.Request(),"unsupported local profile request queued");r.Settle();for(const auto& n:r.nodes)Check(!n->begins,"any peer missing native support prevents every Begin");r.Closed(1,Result::PrepareFailed);}
    {Room r(34);for(const auto& n:r.nodes)Check(!n->protocol.Ready() && n->protocol.LastResult()==Result::Unsupported,"over-limit roster is unavailable, never truncated");}
    // Version 4 (the camera-relative move): a room with a version-3 build in it is refused everywhere, not run with the
    // enemy moving mirrored on that machine. Its hello never decodes on the host; ours never decodes there.
    {Room r(3,99,2);Check(!r.nodes[0]->protocol.Ready(),"a version-3 peer: the host is never ready");
     Check(!r.Request() && !r.nodes[0]->begins,"a version-3 peer in the room: no control begins");
     Check(!r.nodes[2]->begins && !r.nodes[2]->prepares,"the version-3 peer prepares and begins nothing");}
}
void Authentication(){
    Room r;Message m;m.kind=Kind::Request;m.epoch=777;m.request=50;m.spec.requesterPlayer=r.nodes[2]->player;m.spec.target=Id(100);
    r.Deliver(r.PacketOf(1,0,m));r.Pump();Check(!r.nodes[0]->resolves && r.nodes[0]->protocol.LastResult()==Result::Unauthorized,"authenticated PUID must own the requested player NativeID");
    m.request=51;m.spec.requesterPlayer=r.nodes[1]->player;r.Deliver(r.PacketOf(1,0,m),r.nodes[2]->puid.c_str());r.Pump();
    Check(!r.nodes[0]->resolves,"sender cannot impersonate another transport PUID");
    m.request=52;m.epoch=776;r.Deliver(r.PacketOf(1,0,m));Check(!r.nodes[0]->resolves,"stale shared epoch rejected");
    Check(r.Request(0),"host request still works after rejected spoofs");r.Settle();const auto id=r.Active();
    auto prepare=r.Find(Kind::Prepare,0,1);prepare.from=2;r.Deliver(prepare);Check(r.nodes[1]->begins==1,"a guest cannot impersonate host authority");
    Check(!r.nodes[2]->protocol.Exit(id,r.now),"non-owner guest cannot request exit");
    m={};m.kind=Kind::Exit;m.epoch=777;m.lease=id;r.Deliver(r.PacketOf(2,0,m));r.Pump();Check(r.nodes[0]->protocol.ActiveCount()==1,"forged non-owner exit cannot revoke another player");
}
void Conflicts(){
    Room r;Check(r.Request(),"first owner request queued");r.Settle();Check(r.Request(2,100),"conflicting target request queued");r.Settle();
    Check(r.nodes[0]->protocol.ActiveCount()==1 && r.nodes[0]->protocol.LastResult()==Result::Conflict,"one target cannot have two controllers");
    Check(r.Request(1,101),"same owner's second target request queued");r.Settle();Check(r.nodes[0]->protocol.ActiveCount()==1,"one player cannot control two targets");
    Check(r.Request(2,101),"independent second owner request queued");r.Settle();Check(r.nodes[0]->protocol.ActiveCount()==2,"independent players can control different targets");
}
void SplitScreenIdentity(){
    Room r;r.nodes[1]->secondPlayer=Player(50);
    Check(r.Request(1,100),"split-screen primary source request queued");r.Settle();
    Check(r.nodes[1]->protocol.Request(r.nodes[1]->secondPlayer,Id(100),0,r.now),"same PUID secondary source requests occupied target");r.Settle();
    Check(r.nodes[0]->protocol.LastResult()==Result::Conflict && r.nodes[0]->protocol.ActiveCount()==1,"target exclusivity uses canonical target identity across split-screen sources");
    Check(r.nodes[1]->protocol.Request(r.nodes[1]->secondPlayer,Id(101),0,r.now),"same PUID secondary source requests its own target");r.Settle();
    for(const auto& n:r.nodes)Check(n->protocol.ActiveCount()==2,"two source player IDs sharing a transport PUID may hold independent leases");
    Check(r.Request(1,102),"primary source requests a second target");r.Settle();
    Check(r.nodes[0]->protocol.LastResult()==Result::Conflict && r.nodes[0]->protocol.ActiveCount()==2,"each canonical source player ID still owns at most one target");
}
void SetupBarrier(){
    {Room r;r.dropFrom=2;r.dropTo=0;r.dropKind=Kind::Prepared;Check(r.Request(),"prepare barrier request queued");r.Settle();for(const auto& n:r.nodes)Check(!n->begins,"Begin waits for every Prepared ACK");}
    {Room r;r.dropFrom=2;r.dropTo=0;r.dropKind=Kind::Started;Check(r.Request(),"start barrier request queued");r.Settle();
        for(const auto& n:r.nodes)Check(n->begins==1 && !n->activates && !n->protocol.ActiveCount(),"every Begin must ACK before the native Activate callback or UI activation");
        Check(!r.nodes[1]->protocol.SubmitInput(1,kInput,sizeof(kInput),r.now),"owner input forbidden before all Begin ACKs");
        Message early;early.kind=Kind::InputRequest;early.epoch=777;early.lease=1;early.sequence=1;early.spec.profileHash=kProfile;early.inputSize=sizeof(kInput);std::memcpy(early.input.data(),kInput,sizeof(kInput));
        r.Deliver(r.PacketOf(1,0,early));for(const auto& n:r.nodes)Check(!n->applies,"forged early input cannot run a skill");
        r.dropFrom=99;const auto started=r.Find(Kind::Started,2,0);r.Deliver(started);r.Pump();Check(r.nodes[0]->protocol.ActiveCount()==1,"final Begin ACK activates all peers");r.Input();
    }
    {Room r;r.nodes[2]->failBegin=true;Check(r.Request(),"partial Begin failure queued");r.Settle();r.Closed(1,Result::BeginFailed);for(const auto& n:r.nodes)Check(!n->applies,"partial Begin failure never admits input");}
}
void ActivationFailure(){
    for(unsigned failing:{0u,2u}){Room r;r.nodes[failing]->failActivate=true;Check(r.Request(),"native Activate failure request queued");r.Settle();r.Closed(1,Result::BeginFailed);for(const auto& n:r.nodes)Check(!n->applies,"native activation failure admits no owner input");}
    Room missing;auto hooks=Backend(missing.nodes[2].get());hooks.Activate=nullptr;
    Check(missing.nodes[2]->protocol.Configure(hooks),"inactive startup may replace its backend");missing.Step(1100);missing.Settle();
    for(const auto& n:missing.nodes)Check(!n->protocol.Ready(),"a backend without native Activate cannot advertise control capability");
}
void ReplayAndInputFailure(){
    {Room r;Check(r.Request(),"input replay request queued");r.Settle();r.Input();
        const auto request=r.Find(Kind::InputRequest,1,0),accepted1=r.Find(Kind::Input,0,1),accepted2=r.Find(Kind::Input,0,2);
        r.Deliver(request);r.Deliver(accepted1);r.Deliver(accepted2);r.Pump();for(const auto& n:r.nodes)Check(n->applies==1,"owner and authority input replays apply no second skill edge");
        auto newer=Unpack(request);newer.sequence=3;r.Deliver(r.PacketOf(1,0,newer));r.Pump();newer.sequence=2;r.Deliver(r.PacketOf(1,0,newer));r.Pump();
        for(const auto& n:r.nodes)Check(n->applies==2 && n->inputs.back().sequence==2,"late owner samples cannot overwrite the latest accepted sample");
        auto bad=Unpack(accepted1);bad.sequence=4;bad.seed=InputSeed(777,1,4);r.Deliver(r.PacketOf(0,1,bad));r.Pump();r.Closed(1,Result::InvalidInput);
    }
    {Room r;Check(r.Request(),"invalid opaque input request queued");r.Settle();const std::uint8_t bad[]={0xff};Check(r.nodes[1]->protocol.SubmitInput(1,bad,sizeof(bad),r.now),"owner transports opaque data without inventing action fields");r.Pump();r.Closed(1,Result::InvalidInput);for(const auto& n:r.nodes)Check(!n->applies,"host rejects invalid canonical core input before broadcast");}
    {Room r;Check(r.Request(),"guest apply failure request queued");r.Settle();r.nodes[2]->failApply=true;r.Input();r.Closed(1,Result::ApplyFailed);}
    {Room r;Check(r.Request(),"host apply failure request queued");r.Settle();r.nodes[0]->failApply=true;r.Input();r.Closed(1,Result::ApplyFailed);}
    {Room r;Check(r.Request(),"input send failure request queued");r.Settle();r.failFrom=0;r.failTo=2;r.failKind=Kind::Input;Check(r.nodes[1]->protocol.SubmitInput(1,kInput,sizeof(kInput),r.now),"input queued before host reliable-send failure");r.Pump();r.Closed(1,Result::SendFailed);Check(!r.nodes[0]->protocol.Ready(),"host send failure closes admission instead of continuing a split control stream");}
}
void Restoration(){
    {Room r;Check(r.Request(),"restore retry request queued");r.Settle();const auto id=r.Active();const auto spec=r.Lease(2,id)->spec;
        r.nodes[2]->restoreFailures=2;Check(r.nodes[1]->protocol.Exit(id,r.now),"owner exit queued");r.Pump();
        const auto* held=r.Lease(2,id);Check(held->phase==Phase::restoring && !held->localRestored && SameSpec(held->spec,spec),"failed restoration retains lease identity and original native baseline");
        Check(!r.Sent(Kind::Ended) && r.nodes[0]->protocol.LastResult()==Result::RestoreFailed,"partial restoration is explicitly failed/pending, never complete");
        Check(!r.nodes[2]->protocol.Configure(Backend(r.nodes[2].get())),"cannot replace hooks responsible for an unrestored baseline");
        r.Step();r.Step();r.Settle();r.Closed(id,Result::Complete);Check(r.nodes[2]->restores>=3,"native restoration retries until a successful all-peer barrier");
    }
    {Room r;Check(r.Request(),"next-tick restore request queued");r.Settle();r.nodes[2]->failRestore=true;
        Check(r.nodes[1]->protocol.Exit(1,r.now),"owner starts next-tick cleanup");r.Pump();const auto attempts=r.nodes[2]->restores;
        r.Step(1);Check(r.nodes[2]->restores>attempts,"native restoration retries on the next game tick rather than waiting for a wire retry interval");
        r.nodes[2]->failRestore=false;r.Step(1);r.Settle();r.Closed(1,Result::Complete);
    }
    {Room r;Check(r.Request(),"host exit request queued");r.Settle();Check(r.nodes[0]->protocol.Exit(1,r.now),"host can revoke any owner's lease");r.Pump();r.Closed(1,Result::Complete);}
    {Room r;Check(r.Request(),"dead target request queued");r.Settle();r.nodes[2]->protocol.Failed(1,r.now);r.Pump();r.Closed(1,Result::ApplyFailed);}
    {Room r;Check(r.Request(),"disconnect restoration request queued");r.Settle();const auto spec=r.Lease(2,1)->spec;r.nodes[2]->failRestore=true;
        for(auto& n:r.nodes){n->context.ready=false;n->protocol.Tick(n->context,r.now);}r.Pump();
        Check(r.Lease(2,1)->phase==Phase::restoring && !r.Lease(2,1)->localRestored && SameSpec(spec,r.Lease(2,1)->spec),"connection loss cannot discard an unrestored target or baseline");
        r.Step();Check(r.nodes[2]->native[1].suppressed && !r.nodes[2]->protocol.Ready(),"failed disconnected cleanup stays held and unavailable");
        r.nodes[2]->failRestore=false;r.Step();r.Closed(1,Result::SessionLost);
    }
}
void RestoreAckAndTimeout(){
    {Room r;Check(r.Request(),"restore ACK barrier request queued");r.Settle();r.dropFrom=2;r.dropTo=0;r.dropKind=Kind::Restored;
        Check(r.nodes[0]->protocol.Exit(1,r.now),"host starts restore ACK barrier");r.Pump();
        for(const auto& n:r.nodes){const auto* l=r.Lease(n->id,1);Check(l->phase==Phase::restoring && l->localRestored && l->result!=Result::Complete,"native restore alone is still pending until all-peer final ACK");Check(n->protocol.LastResult()!=Result::Complete,"exit is not reported complete before the final restore ACK");}
        Check(!r.Sent(Kind::Ended),"no finalized exit before every native restore ACK");
        r.dropFrom=99;const auto ack=r.Find(Kind::Restored,2,0);r.Deliver(ack);r.Pump();r.Closed(1,Result::Complete);
    }
    {Room r;Check(r.Request(),"restore timeout request queued");r.Settle();r.nodes[2]->failRestore=true;
        Check(r.nodes[1]->protocol.Exit(1,r.now),"owner begins restoration with a failed native peer");r.Pump();
        for(unsigned i=0;i<61;++i)r.Step();
        Check(r.Lease(2,1)->phase==Phase::restoring && !r.Lease(2,1)->localRestored && r.nodes[2]->native[1].suppressed,"timeout preserves the original captured baseline when native restore still fails");
        Check(!r.nodes[0]->protocol.Ready() && r.nodes[0]->protocol.LastResult()!=Result::Complete && !r.Sent(Kind::Ended),"restore deadline fails the session closed and never hides unfinished cleanup");
        r.nodes[2]->failRestore=false;r.Settle();r.Closed(1,Result::TimedOut);
    }
    {Room r;Check(r.Request(),"explicit Reset native-failure request queued");r.Settle();r.nodes[2]->failRestore=true;
        r.nodes[2]->protocol.Reset(r.now);const auto* l=r.Lease(2,1);
        Check(l->phase==Phase::restoring && !l->localRestored && l->spec.target==Id(100),"Reset cannot erase a failed native restoration or target identity");
        r.nodes[2]->context.ready=false;r.Step();Check(r.Lease(2,1)->phase==Phase::restoring,"disconnected ticks keep retrying Reset cleanup");
        r.nodes[2]->failRestore=false;r.Step();Check(r.Lease(2,1)->phase==Phase::closed && r.Lease(2,1)->result==Result::SessionLost,"Reset cleanup closes only after native restore confirms success");
    }
}
void LimitsAndAuthority(){
    {Room r(18);for(unsigned owner=1;owner<=16;++owner){Check(r.Request(owner,99+owner),"lease capacity request queued");r.Settle();}
        Check(r.nodes[0]->protocol.ActiveCount()==16,"declared lease capacity admits every independent source");
        Check(r.Request(17,130),"over-capacity request reaches host");r.Settle();Check(r.nodes[0]->protocol.LastResult()==Result::Unsupported && r.nodes[0]->protocol.ActiveCount()==16,"lease overflow refuses explicitly without overwriting a native baseline");
    }
    {Room r;Check(r.Request(),"wrong-host input request queued");r.Settle();r.Input();auto accepted=r.Find(Kind::Input,0,1);auto m=Unpack(accepted);m.sequence=2;m.seed=InputSeed(777,1,2);
        accepted=r.PacketOf(2,1,m);r.Deliver(accepted);Check(r.nodes[1]->applies==1,"a different client cannot inject accepted host input");
        m.seed^=1;r.Deliver(r.PacketOf(0,1,m));r.Pump();r.Closed(1,Result::InvalidInput);
    }
}
void PerSourceOutcomes(){
    Room r;r.nodes[1]->secondPlayer=Player(50);
    Check(r.Request(1,100),"P1 asynchronous request queued");
    Check(r.nodes[1]->protocol.Request(r.nodes[1]->secondPlayer,Id(220),0,r.now),"P2 independent unsupported target request queued");
    Check(r.nodes[1]->protocol.RequestFor(r.nodes[1]->player)->result==Result::Pending &&
          r.nodes[1]->protocol.RequestFor(r.nodes[1]->secondPlayer)->result==Result::Pending,"each local source has its own pending outcome before any host reply");
    r.Pump();
    const auto* primary=r.nodes[1]->protocol.RequestFor(r.nodes[1]->player);
    const auto* secondary=r.nodes[1]->protocol.RequestFor(r.nodes[1]->secondPlayer);
    Check(primary && primary->result==Result::Active && primary->lease==1 && primary->profileHash==kProfile && primary->target==Id(100),"successful local request correlates actual profile and lease");
    Check(secondary && secondary->result==Result::Unsupported && !secondary->lease && secondary->target==Id(220),"pre-Begin host refusal correlates the other source without stealing P1's success");
    Check(r.Request(0,101),"another member's lease queued");r.Pump();
    Check(r.nodes[0]->protocol.Exit(2,r.now),"another member's lease exits");r.Pump();
    Check(primary->result==Result::Active && secondary->result==Result::Unsupported,"unrelated Activate/Complete updates never overwrite local source results");
    r.nodes[1]->failRestore=true;Check(r.nodes[1]->protocol.Exit(1,r.now),"P1 starts failed native restore");r.Pump();
    Check(primary->result==Result::RestoreFailed && primary->lease==1 && secondary->result==Result::Unsupported,"restore failure is reported only to the owning source and retains its lease");
    r.nodes[1]->failRestore=false;r.Settle();Check(primary->result==Result::Complete && secondary->result==Result::Unsupported,"all-peer restoration completes the correct source alone");
    r.nodes[0]->secondPlayer=Player(51);
    Check(!r.nodes[0]->protocol.Request(r.nodes[0]->secondPlayer,Id(220),0,r.now),"host's own unsupported target refuses immediately");
    const auto* localRejected=r.nodes[0]->protocol.RequestFor(r.nodes[0]->secondPlayer);
    Check(localRejected && localRejected->result==Result::Unsupported && !localRejected->lease,"host-local refusal also has a source-correlated terminal record");
    Check(r.Request(2,102),"remote success after local refusal queued");r.Pump();
    Check(localRejected->result==Result::Unsupported,"remote success cannot hide a host-local source refusal");
    Check(r.nodes[1]->protocol.Request(r.nodes[1]->secondPlayer,Id(103),0,r.now),"request awaiting a host reply queued before disconnect");
    r.nodes[1]->protocol.Reset(r.now);
    Check(r.nodes[1]->protocol.RequestFor(r.nodes[1]->secondPlayer)->result==Result::SessionLost,"Reset terminates local pending requests even when no lease was allocated");
}
void RequestDeadline(){
    {Room r;r.dropFrom=1;r.dropTo=0;r.dropKind=Kind::Request;Check(r.Request(),"request held before host planning");r.Settle();
        for(unsigned i=0;i<52;++i)r.Step();
        Check(r.nodes[1]->protocol.RequestFor(r.nodes[1]->player)->result==Result::TimedOut,"request with no lease has a finite correlated outcome while heartbeats remain healthy");
        const auto late=r.Find(Kind::Request,1,0);r.dropFrom=99;r.Deliver(late);r.Pump();
        for(const auto& n:r.nodes)Check(!n->begins,"a late host Prepare cannot activate a source whose local request already expired");
        Check(r.nodes[1]->protocol.RequestFor(r.nodes[1]->player)->result==Result::TimedOut,"late setup cancellation preserves the original source timeout");
        Check(r.Request(),"fresh request after expired setup queued");r.Settle();Check(r.nodes[1]->protocol.RequestFor(r.nodes[1]->player)->result==Result::Active,"a new request explicitly replaces the old timeout");
    }
    {Room r;r.dropFrom=0;r.dropTo=1;r.dropKind=Kind::Reject;Check(r.Request(1,220),"unsupported request with a lost rejection queued");r.Settle();
        for(unsigned i=0;i<52;++i)r.Step();
        const auto* outcome=r.nodes[1]->protocol.RequestFor(r.nodes[1]->player);
        Check(outcome && outcome->result==Result::TimedOut && !outcome->lease,"lost refusal cannot leave a source pending forever");
        r.dropFrom=99;const auto late=r.Find(Kind::Reject,0,1);r.Deliver(late);
        Check(outcome->result==Result::TimedOut,"a replayed late refusal cannot rewrite a terminal source outcome");
    }
}
void ActivationAckBarrier(){
    {Room r;r.dropFrom=2;r.dropTo=0;r.dropKind=Kind::Activated;Check(r.Request(),"activation ACK barrier request queued");r.Pump();
        for(const auto& n:r.nodes)Check(n->activates==1 && !n->protocol.ActiveCount() && !n->applies,"native Activate alone grants neither UI control nor a skill before every Activated ACK");
        Check(!r.nodes[1]->protocol.SubmitInput(1,kInput,sizeof(kInput),r.now),"guest owner input stays neutral while an activation ACK is missing");
        Check(!r.Sent(Kind::Active),"final active grant is not sent before the activation barrier");
        r.dropFrom=99;const auto ack=r.Find(Kind::Activated,2,0);r.Deliver(ack);r.Pump();
        for(const auto& n:r.nodes)Check(n->protocol.ActiveCount()==1,"final active grant reaches all activated peers");r.Input();
        const auto activate=r.Find(Kind::Activate,0,1);r.Deliver(activate);r.Deliver(ack);r.Pump();Check(r.nodes[1]->activates==1 && r.nodes[1]->applies==1,"duplicate activation command/ACK cannot activate or fire twice");
    }
    {Room r;r.nodes[2]->failActivate=true;r.holdActivationReplies=true;Check(r.Request(0),"host-owned activation failure with delayed fault queued");r.Pump();
        Check(r.Lease(0,1)->phase==Phase::activating && !r.nodes[0]->protocol.SubmitInput(1,kInput,sizeof(kInput),r.now),"host input is rejected while a remote activation failure has not arrived");
        for(const auto& n:r.nodes)Check(!n->applies,"delayed guest fault cannot allow an irreversible native attack on the host");
        r.holdActivationReplies=false;r.Deliver(r.Find(Kind::Activated,2,0));r.Pump();r.Closed(1,Result::BeginFailed);
    }
    {Room r;r.dropFrom=2;r.dropTo=0;r.dropKind=Kind::Activated;Check(r.Request(),"activation deadline request queued");r.Pump();for(unsigned i=0;i<61;++i)r.Step();
        r.Closed(1,Result::TimedOut);for(const auto& n:r.nodes)Check(!n->applies,"activation deadline restores every baseline without accepting a skill");
    }
}
bool CancelCurrent(Room& r,unsigned node=1){const auto* request=r.nodes[node]->protocol.RequestFor(r.nodes[node]->player);Check(request!=nullptr,"source has a request nonce to cancel");return r.nodes[node]->protocol.Cancel(request->source,request->target,request->profileHash,request->request,r.now);}
void PendingCancellation(){
    {Room r;Check(r.Request(),"unallocated candidate request queued");const auto original=r.queue.front();r.queue.pop_front();
        Check(CancelCurrent(r),"candidate without a lease can request cancellation");const auto cancel=r.Find(Kind::Cancel,1,0);
        const auto* view=r.nodes[1]->protocol.RequestFor(r.nodes[1]->player);Check(view->cancelPending && view->result==Result::Pending && !view->lease,"unallocated cancellation remains pending until authenticated host ACK");
        r.Pump();Check(!view->cancelPending && view->result==Result::Complete && !view->lease,"host ACK completes a no-lease cancellation");
        r.Deliver(original);r.Deliver(cancel);r.Pump();for(const auto& n:r.nodes)Check(!n->begins && !n->activates,"consumed cancellation tombstone prevents late requests and duplicates from creating control");
        Check(CancelCurrent(r),"already completed cancellation is idempotent");
        Check(r.Request(1,101),"fresh candidate after cancellation queued");r.Pump();const auto fresh=view->request;
        Check(view->result==Result::Active && !r.nodes[1]->protocol.Cancel(r.nodes[1]->player,Id(100),0,fresh-1,r.now),"old candidate nonce/target cannot cancel a replacement request");
        r.Deliver(cancel);r.Pump();Check(view->result==Result::Active && r.nodes[0]->protocol.ActiveCount()==1,"replayed old cancellation cannot end a new lease");
    }
    for(Kind stage:{Kind::Prepared,Kind::Started,Kind::Activated}) {Room r;r.dropFrom=2;r.dropTo=0;r.dropKind=stage;Check(r.Request(),"allocated setup cancellation queued");r.Pump();
        const auto* request=r.nodes[1]->protocol.RequestFor(r.nodes[1]->player);Check(request->lease==1 && request->result==Result::Pending,"candidate can already carry a lease before final active grant");
        const auto nonce=request->request,hash=request->profileHash;
        Check(!r.nodes[1]->protocol.Cancel(request->source,Id(199),hash,nonce,r.now) && !r.nodes[1]->protocol.Cancel(request->source,request->target,hash+1,nonce,r.now) &&
              !r.nodes[1]->protocol.Cancel(request->source,request->target,hash,nonce+1,r.now),"cancel requires the exact source, target, profile and request tuple");
        Message forged;forged.kind=Kind::Cancel;forged.epoch=777;forged.request=nonce;forged.spec.requesterPlayer=request->source;forged.spec.target=request->target;forged.spec.profileHash=hash;
        r.Deliver(r.PacketOf(2,0,forged));r.Pump();Check(r.Lease(0,1)->phase!=Phase::restoring,"another PUID cannot cancel an authenticated source player's candidate");
        Check(CancelCurrent(r),"allocated but not granted candidate cancels by its request identity");r.Pump();r.Closed(1,Result::Complete);
        Check(request->result==Result::Complete && !request->cancelPending,"allocated cancellation completes only after all native restore ACKs");
        for(const auto& n:r.nodes)Check(!n->applies,"pending cancellation never executes a native attack");
    }
    {Room r;Check(r.Request(),"cancel ACK retransmission candidate queued");const auto old=r.queue.front();r.queue.pop_front();
        r.dropFrom=0;r.dropTo=1;r.dropKind=Kind::CancelAck;Check(CancelCurrent(r),"no-lease cancel queued before dropped host ACK");r.Pump();
        const auto* request=r.nodes[1]->protocol.RequestFor(r.nodes[1]->player);Check(request->cancelPending && request->result!=Result::Complete,"lost cancel ACK cannot be reported as a completed exit");
        r.Step();r.dropFrom=99;r.Step();Check(!request->cancelPending && request->result==Result::Complete,"cancel retries until a real host ACK arrives");r.Deliver(old);r.Pump();for(const auto& n:r.nodes)Check(!n->begins,"retried cancellation still blocks its delayed request");
    }
    {Room r;r.dropFrom=2;r.dropTo=0;r.dropKind=Kind::Prepared;Check(r.Request(),"cancel restore failure candidate queued");r.Pump();r.nodes[2]->failRestore=true;Check(CancelCurrent(r),"allocated cancellation queued with failed remote restoration");r.Pump();
        const auto* request=r.nodes[1]->protocol.RequestFor(r.nodes[1]->player);Check(request->cancelPending && request->result!=Result::Complete,"cancel retains pending status while any native baseline is unrestored");
        r.nodes[2]->failRestore=false;r.Settle();r.Closed(1,Result::Complete);Check(!request->cancelPending && request->result==Result::Complete,"cancel finishes after remote native restoration succeeds");
    }
}
void MoveHost(Room& r,unsigned host,std::uint64_t epoch){
    for(auto& n:r.nodes){n->context.host=n->id==host;n->context.hostPeer=n->id==host ? 0 : r.Peer(n->id,host);n->context.epoch=epoch;}
}
void HostMigration(){
    for(bool failedRestore:{false,true}) {Room r;Check(r.Request(),"old host's lease one queued");r.Pump();const auto oldPrepare=r.Find(Kind::Prepare,0,2);
        if(!failedRestore){Check(r.nodes[1]->protocol.Exit(1,r.now),"old host's lease one exits completely");r.Pump();r.Closed(1,Result::Complete);}
        else r.nodes[2]->failRestore=true;
        MoveHost(r,1,778);r.queue.clear();r.Settle();
        if(failedRestore){const auto* held=r.Lease(2,1);Check(held && held->phase==Phase::restoring && !held->localRestored && held->spec.target==Id(100),"new epoch cannot erase old host's failed native restoration");
            Check(!r.nodes[2]->protocol.Ready() && !r.nodes[1]->protocol.Ready(),"unrestored old actor blocks adoption and new-room admission");r.nodes[2]->failRestore=false;r.Settle();}
        for(const auto& n:r.nodes)Check(n->protocol.Ready(),"every restored peer adopts the new host's epoch");
        Check(r.nodes[2]->protocol.RequestFor(r.nodes[2]->player)==nullptr,"new epoch clears old published request identities after restoration");
        r.Deliver(oldPrepare);r.Pump();const auto before=r.nodes[2]->begins;
        Check(r.Request(2,101),"new host's first lease request queued");r.Pump();Check(r.Active(1)==1,"a different host may legitimately allocate native lease one");
        for(const auto& n:r.nodes){const auto* lease=r.Lease(n->id,1);Check(lease && lease->phase==Phase::active && lease->spec.target==Id(101),"new epoch lease one resolves independently of old host's closed lease one");}
        Check(r.nodes[2]->begins==before+1,"stale old-epoch Prepare never begins an extra native controller");
    }
}
void ResetReplay(){
    for(bool rotate:{false,true}){Room r;Check(r.Request(),"old generation request queued");r.Settle();r.Input();
        const auto oldPrepare=r.Find(Kind::Prepare,0,1),oldBegin=r.Find(Kind::Begin,0,1),oldInput=r.Find(Kind::Input,0,1),oldRequest=r.Find(Kind::Request,1,0);
        for(auto& n:r.nodes){n->protocol.Reset(r.now);if(rotate)++n->context.epoch;}
        r.queue.clear();r.Settle();
        r.Deliver(oldPrepare);r.Deliver(oldBegin);r.Deliver(oldInput);r.Deliver(oldRequest);r.Pump();
        for(const auto& n:r.nodes)Check(n->begins==1 && n->applies==1,"old setup/request/input cannot resurrect after Reset with either same or rotated epoch");
        Check(r.Request(),"fresh request accepted after cleanup");r.Settle();Check(r.Active()>1,"lease IDs remain monotonic across Reset");
        std::uint64_t newestRequest=0;
        for(const auto& p:r.history){const auto message=Unpack(p);if(p.from==1 && message.kind==Kind::Request)newestRequest=message.request;}
        Check(newestRequest>Unpack(oldRequest).request,"owner request IDs are not reused across Reset");
        for(const auto& n:r.nodes)Check(n->begins==2,"fresh post-Reset transaction begins exactly once");
    }
}
}
int main(){Codec();VariableWireBounds();Success();CapabilityAndProfiles();Authentication();Conflicts();SplitScreenIdentity();SetupBarrier();ActivationFailure();ReplayAndInputFailure();Restoration();RestoreAckAndTimeout();LimitsAndAuthority();PerSourceOutcomes();RequestDeadline();ActivationAckBarrier();PendingCancellation();HostMigration();ResetReplay();std::printf("enemy_control_protocol: %d checks passed\n",checks);return 0;}
