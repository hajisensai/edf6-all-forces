// Production enemy-control bridge plus two pure peers through the real ECTL/input
// codecs. Only native actor/core/transport endpoints are stubs; bridge identity,
// authority, UI-snapshot matching and sampling logic execute unchanged.
#include <Windows.h>
namespace { ULONGLONG FixtureTickCount64() noexcept; }
#define GetTickCount64 FixtureTickCount64
#include "../src/enemy_control_net.cpp"
#undef GetTickCount64
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <vector>

namespace ep=enemy_control_protocol;
namespace ec=crew::enemy_control;
namespace en=crew::enemy_control_net;
namespace {
unsigned checks=0;
void Check(bool pass,const char* expression,int line) {
    ++checks;
    if(!pass){std::fprintf(stderr,"line %d: %s\n",line,expression);std::exit(1);}
}
#define CHECK(value) Check((value),#value,__LINE__)
constexpr const char* kPuid[]={"host001","guest001","observer001"};
unsigned char nativePuid[3]{};
ULONGLONG now=10000;
ULONGLONG FixtureTickCount64() noexcept { return now; }
crew::Config config{};
crew::EnemyControlNetwork callbacks{};
std::uint64_t coreRevision=100;
struct Actor {
    ep::NativeId id{};
    unsigned char object[0x80]{},control[16]{};
    int owner=-1;
    bool target=false,alive=true,registered=true;
    std::uint64_t profile=0;
    crew::ObjRef Ref() const noexcept {return {object,control};}
};
Actor actors[8]{};
ep::NativeId Id(unsigned value) noexcept {
    ep::NativeId id{};id[0]=static_cast<std::uint8_t>(value);id[4]=7;id[12]=5;id[24]=0x71;return id;
}
Actor* ActorById(const unsigned char* id) noexcept {
    if(id)for(auto& actor:actors)if(!std::memcmp(actor.id.data(),id,32))return &actor;
    return nullptr;
}
Actor* ActorByRef(const crew::ObjRef& ref) noexcept {
    for(auto& actor:actors)if(actor.Ref().obj==ref.obj && actor.Ref().ctrl==ref.ctrl)return &actor;
    return nullptr;
}
Actor* ActorByObject(const void* object) noexcept {
    for(auto& actor:actors)if(actor.object==object)return &actor;
    return nullptr;
}
// A player's ID: its object reference (net_object_ref.h: mission slot `slot`), never a NetId.
ep::NativeId Player(unsigned slot) noexcept {
    ep::NativeId id{};crew::netref::MakePlayer(static_cast<int>(slot),id.data());return id;
}
void InitActors() noexcept {
    actors[0].id=Player(10);actors[0].owner=0;actors[1].id=Player(11);actors[1].owner=0;
    actors[2].id=Player(20);actors[2].owner=1;actors[3].id=Player(21);actors[3].owner=1;
    actors[4].id=Player(30);actors[4].owner=2;
    for(unsigned i=5;i<8;++i){actors[i].id=Id(100+i-5);actors[i].target=true;actors[i].profile=0xAABBCC00ull+i;}
    for(auto& actor:actors) {
        actor.alive=true;actor.registered=true;
        edf::Put<const void*>(actor.object,edf::kSelf,actor.object);
        edf::Put<const void*>(actor.object,edf::kSelfCtrl,actor.control);
        edf::Put<LONG>(actor.control,8,1);edf::Put<LONG>(actor.control,12,1);
    }
}
struct CoreLease {
    crew::ObjRef source{},target{};
    std::uint64_t profile=0,revision=0;
    bool captured=false,active=false,local=false,live=true,closing=false;
    char authenticatedPuid[65]{};
    unsigned begins=0,activates=0,applies=0,restores=0;
    std::vector<ec::Input> inputs;
};
struct NativeEnvironment {
    bool hostActions=false,failActionCapture=false,nativeCoreCanReplay=true;
    ec::ActionResult actionResult=ec::ActionResult::applied;
    unsigned quiesceCalls=0,abortCalls=0;
    unsigned normalizeCalls=0;
    unsigned actionCaptureCalls=0,actionApplyCalls=0;std::size_t actionCapacity=0;
    std::deque<std::vector<unsigned char>> acceptedActions;
    std::vector<std::uint64_t> appliedActionSequences;
    std::vector<std::vector<unsigned char>> appliedActions;
    bool online=true,capacityReady=true,strict=true,sealed=true,pose=true,nativeId=true,authority=true,inputValid=true;
    unsigned capacityQueries=0;bool flowBlocked=false,flowUnavailable=false;unsigned flowCalls=0;
    bool corruptNativeId=false,wrongRequesterPuid=false,profileMismatch=false,failBegin=false,failActivate=false,failApply=false;
    bool fallback=false,failPoseCapture=false,failPoseApply=false;
    std::size_t poseSize=ep::kMaxPoseBytes;unsigned poseGeneration=1;
    unsigned poseCaptureCalls=0,poseApplyCalls=0,needsPoseCalls=0;
    std::size_t poseCapacity=0;std::vector<unsigned char> capturedPose,appliedPose;
    unsigned restoreFailures=0;
    unsigned strictCalls=0,sealCalls=0,idCalls=0,authorityCalls=0,prepareCalls=0,describeCalls=0,beginCalls=0,activateCalls=0,applyCalls=0,restoreCalls=0,validateCalls=0;
    unsigned strictWithTarget=0,strictWithoutTarget=0;
    bool strictArguments=true,sealArguments=true;
    std::vector<ep::NativeId> resolvedSources,resolvedTargets;
    std::map<std::uint64_t,CoreLease> leases;
} native;
struct Room;
struct PureNode {
    ec::ActionResult actionResult=ec::ActionResult::applied;
    std::vector<std::uint64_t> actionSequences;std::vector<std::vector<unsigned char>> actions;
    Room* room=nullptr;unsigned global=0;
    ep::Context context{};ep::Protocol protocol{ep::Hooks{}};
    bool rejectPrepare=false,failBegin=false,failActivate=false,failApply=false,failPose=false;
    unsigned poseApplies=0;
    std::map<std::uint64_t,std::vector<unsigned char>> poses;
    std::map<std::uint64_t,std::uint64_t> poseSequences;
    unsigned prepares=0,begins=0,activates=0,applies=0,restores=0;
    struct Baseline {ep::LeaseSpec spec{};bool captured=false,active=false;};
    std::map<std::uint64_t,Baseline> baselines;
    std::map<std::uint64_t,std::vector<ec::Input>> inputs;
};
struct Packet {unsigned from=0,to=0;std::uint64_t sentAt=0;std::vector<unsigned char> bytes;};
Room* room=nullptr;
bool PureSend(void*,std::uint32_t,const void*,std::size_t);
bool PureAuthenticate(void*,const char*,const ep::NativeId&);
bool PureResolve(void*,ep::LeaseSpec*);
bool PurePrepare(void*,std::uint64_t,const ep::LeaseSpec&);
bool PureBegin(void*,std::uint64_t,const ep::LeaseSpec&);
bool PureValidate(void*,std::uint64_t,const ep::LeaseSpec&,const ep::InputMeta&,const void*,std::size_t);
bool PureApply(void*,std::uint64_t,const ep::LeaseSpec&,const ep::InputMeta&,const void*,std::size_t);
bool PureRestore(void*,std::uint64_t,const ep::LeaseSpec&);
bool PureActivate(void*,std::uint64_t,const ep::LeaseSpec&);
bool PurePose(void*,std::uint64_t,const ep::LeaseSpec&,std::uint64_t,const void*,std::size_t);
ep::ActionResult PureAction(void*,std::uint64_t,const ep::LeaseSpec&,std::uint64_t,const void*,std::size_t);
bool BridgeSend(std::uint32_t,const void*,std::size_t) noexcept;
std::uint32_t BridgeTrySend(std::uint32_t,const void*,std::size_t,std::uint32_t,std::uint32_t) noexcept;
struct Room {
    unsigned bridge=0;
    std::uint64_t epoch=0;
    std::unique_ptr<PureNode[]> nodeStorage{new PureNode[3]{}};
    PureNode* nodes=nodeStorage.get();
    std::deque<Packet> queue;
    std::vector<Packet> history;
    explicit Room(unsigned production,unsigned serial,bool capacityReady=true) :bridge(production),epoch(0x6000ull+serial) {
        room=this;native=NativeEnvironment{};native.capacityReady=capacityReady;InitActors();config.enabled=true;
        native.authority=bridge==0;
        for(unsigned i=0;i<3;++i) {
            auto& node=nodes[i];node.room=this;node.global=i;
            node.context={true,i==0,epoch,i ? Peer(i,0) : 0,2,kPuid[i]};
            CHECK(node.protocol.Configure({&node,&PureSend,&PureAuthenticate,&PureResolve,&PurePrepare,&PureBegin,
                &PureValidate,&PureApply,&PureRestore,&PureActivate,&PurePose,&PureAction}));
        }
        TickAll();Pump();now+=1001;TickAll();Pump();
        for(unsigned i=0;i<3;++i)CHECK(Protocol(i).Ready()==capacityReady);
    }
    ~Room() {
        native.restoreFailures=0;crew::ResetEnemyControlNetwork();queue.clear();room=nullptr;
    }
    static unsigned Peer(unsigned receiver,unsigned sender) noexcept {return sender<receiver ? sender+1 : sender;}
    static unsigned Global(unsigned sender,unsigned peer) noexcept {return peer<=sender ? peer-1 : peer;}
    ep::Protocol& Protocol(unsigned node) noexcept {return node==bridge ? crew::protocol : nodes[node].protocol;}
    void TickAll() {
        for(unsigned i=0;i<3;++i)if(i==bridge)
            crew::UpdateEnemyControlNetwork({true,bridge==0,epoch,bridge ? Peer(bridge,0) : 0,2,kPuid[bridge],&BridgeSend,&BridgeTrySend},now);
        else nodes[i].protocol.Tick(nodes[i].context,now);
    }
    bool Queue(unsigned from,std::uint32_t peer,const void* bytes,std::size_t size) {
        if(!peer || peer>2 || !bytes || size<ep::kWirePrefixBytes || size>ep::kWireBytes)return false;
        Packet packet;packet.from=from;packet.to=Global(from,peer);packet.sentAt=now;packet.bytes.resize(size);std::memcpy(packet.bytes.data(),bytes,size);
        ep::Message decoded;CHECK(ep::Decode(packet.bytes.data(),packet.bytes.size(),decoded));
        history.push_back(packet);queue.push_back(packet);return true;
    }
    void Deliver(const Packet& packet,const char* authenticated=nullptr) {
        const char* who=authenticated ? authenticated : kPuid[packet.from];
        const unsigned peer=Peer(packet.to,packet.from);
        if(packet.to==bridge)CHECK(crew::ReceiveEnemyControlNetwork(peer,who,packet.bytes.data(),packet.bytes.size(),now));
        else CHECK(nodes[packet.to].protocol.Receive(peer,who,packet.bytes.data(),packet.bytes.size(),now));
    }
    void One() {CHECK(!queue.empty());const Packet packet=queue.front();queue.pop_front();Deliver(packet);}
    void Pump() {unsigned guard=0;while(!queue.empty()){CHECK(++guard<10000);One();}}
    void Step(unsigned milliseconds=32) {
        CHECK(milliseconds>=ep::kActionFrameMs);now+=milliseconds;TickAll();crew::EnemyControlNetworkFrame();
        for(unsigned i=0;i<3;++i)if(i!=bridge)nodes[i].protocol.TransportFrame(now);Pump();
    }
    const ep::LeaseView* Lease(unsigned node,std::uint64_t id) noexcept {
        for(std::size_t i=0;i<ep::kMaxLeases;++i){const auto* value=Protocol(node).LeaseAt(i);if(value && value->id==id)return value;}
        return nullptr;
    }
    const ep::LeaseView* ForSource(unsigned node,const ep::NativeId& source) noexcept {
        for(std::size_t i=0;i<ep::kMaxLeases;++i){const auto* value=Protocol(node).LeaseAt(i);
            if(value && value->id && value->spec.requesterPlayer==source && value->phase!=ep::Phase::closed)return value;}
        return nullptr;
    }
    ec::Snapshot Snapshot(unsigned source,unsigned target,std::uint64_t lease=0) const noexcept {
        ec::Snapshot value;value.lease=lease;value.profileHash=actors[target].profile;value.revision=++coreRevision;
        value.ownerHumanKey=reinterpret_cast<std::uintptr_t>(actors[source].Ref().obj);
        std::memcpy(value.owner,actors[source].id.data(),32);std::memcpy(value.target,actors[target].id.data(),32);return value;
    }
    ec::Snapshot ActiveSnapshot(unsigned source) {
        Until(source,ep::Phase::active);
        const auto* value=ForSource(bridge,actors[source].id);CHECK(value && value->phase==ep::Phase::active);
        ec::Snapshot snapshot;CHECK(crew::EnemyControlReadSnapshot(actors[source].Ref(),&snapshot));
        CHECK(snapshot.lease==value->id && snapshot.profileHash==value->spec.profileHash);
        return snapshot;
    }
    bool Request(unsigned source,unsigned target) {return callbacks.request(Snapshot(source,target));}
    bool PureRequest(unsigned node,unsigned source,unsigned target) {
        return nodes[node].protocol.Request(actors[source].id,actors[target].id,actors[target].profile,now);
    }
    void Until(unsigned source,ep::Phase phase) {
        unsigned guard=0;
        while(true) {const auto* lease=ForSource(bridge,actors[source].id);if(lease && lease->phase==phase)return;
            if(guard==998) {
                std::fprintf(stderr,"Until stalled bridge=%u epoch=%llu source=%u wanted=%u ready=%u result=%u now=%llu\n",bridge,static_cast<unsigned long long>(epoch),source,static_cast<unsigned>(phase),Protocol(bridge).Ready(),static_cast<unsigned>(Protocol(bridge).LastResult()),static_cast<unsigned long long>(now));
                for(unsigned node=0;node<3;++node)for(std::size_t i=0;i<ep::kMaxLeases;++i){const auto* value=Protocol(node).LeaseAt(i);if(value && value->id)std::fprintf(stderr," node=%u lease=%llu phase=%u result=%u restored=%u\n",node,static_cast<unsigned long long>(value->id),static_cast<unsigned>(value->phase),static_cast<unsigned>(value->result),value->localRestored);}
            }
            CHECK(++guard<1000);
            // Only a real future frame can flush the production guest's batched
            // native ACK. Do not disable its reliable transport to force progress.
            if(queue.empty()){Step();continue;}
            One();}
    }
    unsigned ApplyCount(unsigned node,std::uint64_t id) const {
        if(node==bridge){const auto it=native.leases.find(id);return it==native.leases.end() ? 0 : it->second.applies;}
        const auto it=nodes[node].inputs.find(id);return it==nodes[node].inputs.end() ? 0 : static_cast<unsigned>(it->second.size());
    }
    ec::Input LastInput(unsigned node,std::uint64_t id) const {
        if(node==bridge){const auto it=native.leases.find(id);CHECK(it!=native.leases.end() && !it->second.inputs.empty());return it->second.inputs.back();}
        const auto it=nodes[node].inputs.find(id);CHECK(it!=nodes[node].inputs.end() && !it->second.empty());return it->second.back();
    }
    Packet LastPacket(ep::Kind kind,unsigned from,unsigned to) const {
        for(auto it=history.rbegin();it!=history.rend();++it) {ep::Message value;CHECK(ep::Decode(it->bytes.data(),it->bytes.size(),value));
            if(it->from==from && it->to==to && value.kind==kind)return *it;}
        CHECK(false);return {};
    }
    Packet Make(unsigned from,unsigned to,const ep::Message& message) const {
        Packet packet;packet.from=from;packet.to=to;packet.sentAt=now;packet.bytes.resize(ep::EncodedSize(message));
        CHECK(ep::Encode(message,packet.bytes.data(),packet.bytes.size()));return packet;
    }
    void Closed(std::uint64_t id) {
        unsigned guard=0;
        for(;;) {
            bool done=true;for(unsigned i=0;i<3;++i){const auto* lease=Lease(i,id);done=done && lease && lease->phase==ep::Phase::closed && lease->localRestored;}
            if(done)break;CHECK(++guard<200);Step();
        }
        const auto found=native.leases.find(id);if(found!=native.leases.end())CHECK(!found->second.captured && !found->second.active);
    }
};
bool BridgeSend(std::uint32_t peer,const void* bytes,std::size_t size) noexcept {return room && room->Queue(room->bridge,peer,bytes,size);}
bool PureSend(void* user,std::uint32_t peer,const void* bytes,std::size_t size) {
    const auto& node=*static_cast<PureNode*>(user);return node.room->Queue(node.global,peer,bytes,size);
}
bool PureAuthenticate(void*,const char* puid,const ep::NativeId& source) {
    const auto* actor=ActorById(source.data());return actor && actor->alive && actor->owner>=0 && puid && !std::strcmp(puid,kPuid[actor->owner]);
}
bool PureResolve(void*,ep::LeaseSpec* spec) {
    const auto* target=spec ? ActorById(spec->target.data()) : nullptr;
    return target && target->target && spec->profileHash && spec->profileHash==target->profile;
}
bool PurePrepare(void* user,std::uint64_t,const ep::LeaseSpec& spec) {
    auto& node=*static_cast<PureNode*>(user);++node.prepares;
    const auto* target=ActorById(spec.target.data());return !node.rejectPrepare && target && target->target && target->profile==spec.profileHash;
}
bool PureBegin(void* user,std::uint64_t id,const ep::LeaseSpec& spec) {
    auto& node=*static_cast<PureNode*>(user);++node.begins;node.baselines[id]={spec,true,false};return !node.failBegin;
}
bool PureValidate(void* user,std::uint64_t id,const ep::LeaseSpec&,const ep::InputMeta& meta,const void* bytes,std::size_t size) {
    auto& node=*static_cast<PureNode*>(user);ec::Input input;
    const auto baseline=node.baselines.find(id);
    return baseline!=node.baselines.end() && baseline->second.active && meta.sequence && meta.seed==ep::InputSeed(node.context.epoch,id,meta.sequence) && en::DecodeInput(bytes,size,&input);
}
bool PureApply(void* user,std::uint64_t id,const ep::LeaseSpec&,const ep::InputMeta& meta,const void* bytes,std::size_t size) {
    auto& node=*static_cast<PureNode*>(user);ec::Input input;CHECK(en::DecodeInput(bytes,size,&input));
    input.sequence=meta.sequence;input.seed=meta.seed;node.inputs[id].push_back(input);++node.applies;return !node.failApply;
}
bool PureRestore(void* user,std::uint64_t id,const ep::LeaseSpec&) {
    auto& node=*static_cast<PureNode*>(user);++node.restores;const auto found=node.baselines.find(id);
    if(found!=node.baselines.end()){found->second.captured=false;found->second.active=false;}return true;
}
bool PureActivate(void* user,std::uint64_t id,const ep::LeaseSpec&) {
    auto& node=*static_cast<PureNode*>(user);++node.activates;node.baselines[id].active=true;return !node.failActivate;
}
bool PurePose(void* user,std::uint64_t id,const ep::LeaseSpec& spec,std::uint64_t sequence,const void* bytes,std::size_t size) {
    auto& node=*static_cast<PureNode*>(user);const auto found=node.baselines.find(id);
    CHECK(node.global!=0 && found!=node.baselines.end() && found->second.captured && found->second.active);
    CHECK(ep::SameSpec(found->second.spec,spec) && bytes && size && size<=ep::kMaxPoseBytes && sequence);
    ++node.poseApplies;node.poses[id].assign(static_cast<const unsigned char*>(bytes),static_cast<const unsigned char*>(bytes)+size);
    node.poseSequences[id]=sequence;return !node.failPose;
}
ep::ActionResult PureAction(void* user,std::uint64_t id,const ep::LeaseSpec& spec,std::uint64_t sequence,const void* bytes,std::size_t size) {
    auto& node=*static_cast<PureNode*>(user);const auto found=node.baselines.find(id);
    CHECK(node.global!=0 && found!=node.baselines.end() && found->second.captured && found->second.active);
    CHECK(ep::SameSpec(found->second.spec,spec) && sequence && size && size<=ep::kMaxPoseBytes);
    if(node.actionResult==ec::ActionResult::applied){node.actionSequences.push_back(sequence);node.actions.emplace_back(static_cast<const unsigned char*>(bytes),static_cast<const unsigned char*>(bytes)+size);}
    return node.actionResult;
}
std::uint32_t BridgeTrySend(std::uint32_t peer,const void* bytes,std::size_t size,std::uint32_t byteLimit,std::uint32_t packetLimit) noexcept {
    ++native.flowCalls;CHECK(byteLimit==ep::kActionPeerByteCredits && packetLimit==ep::kActionPeerPacketCredits);
    if(native.flowUnavailable)return EDF6COOP_EXTENSION_SEND_UNAVAILABLE;
    if(native.flowBlocked)return EDF6COOP_EXTENSION_SEND_WOULD_BLOCK;
    return BridgeSend(peer,bytes,size) ? EDF6COOP_EXTENSION_SEND_ACCEPTED : EDF6COOP_EXTENSION_SEND_UNAVAILABLE;
}
std::vector<unsigned char> PoseBytes(std::size_t size,unsigned generation) {
    std::vector<unsigned char> bytes(size);for(std::size_t i=0;i<size;++i)bytes[i]=static_cast<unsigned char>((i*17+generation*37)%251);
    return bytes;
}
}

namespace crew {
const Config& Cfg() noexcept {return config;}
unsigned sessionQueries=0;
bool InSession() noexcept {++sessionQueries;return native.online;}
void Log(const char*,...) noexcept {}
bool ResolveCommandIdentities(const unsigned char source[32],const unsigned char units[][32],unsigned count,
                              const unsigned char focus[32],CommandIdentities* out) noexcept {
    ++native.strictCalls;native.strictArguments=native.strictArguments && source && !units && !count && out;
    if(!out)return false;*out={};
    const auto* sourceActor=ActorById(source);
    if(source){ep::NativeId copy{};std::memcpy(copy.data(),source,32);native.resolvedSources.push_back(copy);}
    if(focus){++native.strictWithTarget;ep::NativeId copy{};std::memcpy(copy.data(),focus,32);native.resolvedTargets.push_back(copy);}
    else ++native.strictWithoutTarget;
    if(!native.strict || !sourceActor || !sourceActor->alive || sourceActor->target || sourceActor->owner<0)return false;
    out->requester=sourceActor->Ref();out->requesterPuid=native.wrongRequesterPuid ? nativePuid+2 : nativePuid+sourceActor->owner;
    if(focus){const auto* target=ActorById(focus);if(!target || !target->alive)return false;out->focus=target->Ref();}
    return true;
}
bool SupportCommandRequesterMatches(void* puid,const char* authenticated) noexcept {
    ++native.sealCalls;native.sealArguments=native.sealArguments && puid && authenticated;
    if(!native.sealed || !authenticated)return false;
    for(unsigned i=0;i<3;++i)if(puid==nativePuid+i)return !std::strcmp(authenticated,kPuid[i]);
    return false;
}
bool ReadNativeObjectId(const unsigned char* object,unsigned char* out32) noexcept {
    ++native.idCalls;const auto* actor=ActorByObject(object);
    if(!native.nativeId || !actor || !actor->registered || !actor->alive || !out32)return false;
    std::memcpy(out32,actor->id.data(),32);if(native.corruptNativeId)out32[0]^=1;return true;
}
bool IsOnlineAuthority(const void* object) noexcept {++native.authorityCalls;return ActorByObject(object) && native.authority;}
bool EnemyControlLocalCapacityReady() noexcept {++native.capacityQueries;return native.capacityReady;}
void ConfigureEnemyControlNetwork(const EnemyControlNetwork& value) noexcept {callbacks=value;}
bool EnemyControlDescribe(const ObjRef& source,const ObjRef& target,ec::Snapshot* out) noexcept {
    ++native.describeCalls;const auto* sourceActor=ActorByRef(source);const auto* enemy=ActorByRef(target);
    if(!out || !room || !sourceActor || sourceActor->owner!=static_cast<int>(room->bridge) || !enemy || !enemy->target)return false;
    *out=room->Snapshot(static_cast<unsigned>(sourceActor-actors),static_cast<unsigned>(enemy-actors));return true;
}
bool EnemyControlPrepare(const ObjRef& source,const ObjRef& target,std::uint64_t profile,ec::Reason* reason) noexcept {
    ++native.prepareCalls;const auto* sourceActor=ActorByRef(source);const auto* enemy=ActorByRef(target);
    const bool accepted=sourceActor && sourceActor->alive && !sourceActor->target && enemy && enemy->alive && enemy->target && native.pose && profile==(native.profileMismatch ? enemy->profile^0x100ull : enemy->profile);
    if(reason)*reason=accepted ? ec::Reason::none : ec::Reason::nativeMismatch;return accepted;
}
bool EnemyControlBegin(const ObjRef& source,const ObjRef& target,std::uint64_t profile,std::uint64_t id,bool local,const char* authenticatedPuid) noexcept {
    ++native.beginCalls;auto& lease=native.leases[id];lease.source=source;lease.target=target;lease.profile=profile;
    const auto* sourceActor=ActorByRef(source);CHECK(sourceActor && sourceActor->owner>=0 && authenticatedPuid);
    CHECK(!std::strcmp(authenticatedPuid,kPuid[sourceActor->owner]));
    CHECK(local==(static_cast<unsigned>(sourceActor->owner)==room->bridge));
    std::memcpy(lease.authenticatedPuid,authenticatedPuid,std::strlen(authenticatedPuid)+1);
    lease.captured=true;lease.local=local;lease.revision=++coreRevision;++lease.begins;return !native.failBegin;
}
bool EnemyControlReadSnapshot(const ObjRef& source,ec::Snapshot* out) noexcept {
    if(!out)return false;*out={};
    for(const auto& item:native.leases) {
        const auto& lease=item.second;
        if(lease.source.obj!=source.obj || lease.source.ctrl!=source.ctrl || !lease.local || !lease.captured)continue;
        const auto* sourceActor=ActorByRef(source);const auto* target=ActorByRef(lease.target);
        if(!sourceActor || !target)return false;
        out->lease=item.first;out->profileHash=lease.profile;out->revision=lease.revision;
        out->ownerHumanKey=reinterpret_cast<std::uintptr_t>(source.obj);
        out->phase=lease.active ? ec::Phase::active : ec::Phase::pending;
        std::memcpy(out->owner,sourceActor->id.data(),32);std::memcpy(out->target,target->id.data(),32);return true;
    }
    return false;
}
bool EnemyControlActivate(std::uint64_t id) noexcept {
    ++native.activateCalls;auto found=native.leases.find(id);if(found==native.leases.end() || !found->second.captured)return false;
    ++found->second.activates;found->second.active=true;return !native.failActivate;
}
bool EnemyControlNormalizeNetworkInput(std::uint64_t id,ec::Input* input) noexcept {
    ++native.normalizeCalls;if(!input || native.leases.find(id)==native.leases.end())return false;
    bool any=false;for(auto b:input->fullFocusId)any=any || b!=0;
    if(any){const auto* focus=ActorById(input->fullFocusId);if(!focus || !focus->alive || !focus->registered || focus->Ref().obj==native.leases[id].source.obj || focus->Ref().obj==native.leases[id].target.obj)std::memset(input->fullFocusId,0,32);}
    return true;
}
bool EnemyControlValidateInput(std::uint64_t id,const ec::Input& input) noexcept {
    ++native.validateCalls;const auto found=native.leases.find(id);
    return native.inputValid && found!=native.leases.end() && found->second.active && found->second.captured && ec::ValidInput(input);
}
bool EnemyControlApply(std::uint64_t id,const ec::Input& input) noexcept {
    ++native.applyCalls;auto found=native.leases.find(id);if(found==native.leases.end())return false;
    ++found->second.applies;found->second.inputs.push_back(input);return !native.failApply;
}
bool EnemyControlRestore(std::uint64_t id) noexcept {
    ++native.restoreCalls;auto found=native.leases.find(id);if(found!=native.leases.end())++found->second.restores;
    if(native.restoreFailures){--native.restoreFailures;return false;}
    if(found!=native.leases.end()){found->second.captured=false;found->second.active=false;}return true;
}
bool EnemyControlLeaseLive(std::uint64_t id) noexcept {
    const auto found=native.leases.find(id);if(found==native.leases.end() || !found->second.live)return false;
    const auto* target=ActorByRef(found->second.target);return target && target->alive;
}
bool EnemyControlQuiesceNetworkActions(std::uint64_t id) noexcept {
    ++native.quiesceCalls;const auto found=native.leases.find(id);if(found!=native.leases.end())found->second.closing=true;return true;
}
bool EnemyControlNetworkActionsEmpty(std::uint64_t) noexcept {return native.acceptedActions.empty();}
bool EnemyControlAbortNetworkActions(std::uint64_t) noexcept {++native.abortCalls;native.acceptedActions.clear();return true;}
bool EnemyControlNeedsHostActions(std::uint64_t id) noexcept {
    const auto found=native.leases.find(id);
    return native.hostActions && found!=native.leases.end() && found->second.captured && found->second.active &&
        edf::At<const void*>(found->second.target.obj,edf::kSelfCtrl)==found->second.target.ctrl;
}
bool EnemyControlCaptureNetworkAction(std::uint64_t id,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    ++native.actionCaptureCalls;native.actionCapacity=capacity;if(used)*used=0;
    if(!bytes || !used || native.failActionCapture || !EnemyControlNeedsHostActions(id))return false;
    if(native.acceptedActions.empty())return true;
    const auto action=native.acceptedActions.front();native.acceptedActions.pop_front();*used=action.size();
    if(action.size()<=capacity)std::memcpy(bytes,action.data(),action.size());return true;
}
ec::ActionResult EnemyControlApplyNetworkAction(std::uint64_t id,std::uint64_t sequence,const void* bytes,std::size_t size) noexcept {
    ++native.actionApplyCalls;if(!EnemyControlNeedsHostActions(id) || !native.nativeCoreCanReplay || !sequence || !bytes || !size || size>ep::kMaxPoseBytes)return ec::ActionResult::failed;
    if(native.actionResult==ec::ActionResult::applied){native.appliedActionSequences.push_back(sequence);native.appliedActions.emplace_back(static_cast<const unsigned char*>(bytes),static_cast<const unsigned char*>(bytes)+size);}
    return native.actionResult;
}
bool EnemyControlNeedsNetworkPose(std::uint64_t id) noexcept {
    ++native.needsPoseCalls;const auto found=native.leases.find(id);
    return native.fallback && found!=native.leases.end() && found->second.captured && found->second.active;
}
bool EnemyControlCaptureNetworkPose(std::uint64_t id,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    ++native.poseCaptureCalls;native.poseCapacity=capacity;const auto found=native.leases.find(id);
    CHECK(room && room->bridge==0 && native.authority && native.fallback);
    CHECK(found!=native.leases.end() && found->second.captured && found->second.active && found->second.live);
    const auto* target=ActorByRef(found->second.target);CHECK(target && target->alive && target->registered);
    CHECK(bytes && used && capacity==ep::kMaxPoseBytes);
    *used=0;if(native.failPoseCapture)return false;
    native.capturedPose=PoseBytes(native.poseSize,native.poseGeneration);
    const auto copied=native.poseSize<capacity ? native.poseSize : capacity;
    if(copied)std::memcpy(bytes,native.capturedPose.data(),copied);*used=native.poseSize;return true;
}
bool EnemyControlApplyNetworkPose(std::uint64_t id,const void* bytes,std::size_t size) noexcept {
    ++native.poseApplyCalls;const auto found=native.leases.find(id);
    CHECK(room && room->bridge!=0 && !native.authority && native.fallback);
    CHECK(found!=native.leases.end() && found->second.captured && found->second.active && found->second.live);
    const auto* target=ActorByRef(found->second.target);CHECK(target && target->alive && target->registered);
    CHECK(bytes && size && size<=ep::kMaxPoseBytes);
    native.appliedPose.assign(static_cast<const unsigned char*>(bytes),static_cast<const unsigned char*>(bytes)+size);
    return !native.failPoseApply;
}
namespace enemy_control {
bool ValidInput(const Input& input) noexcept {return en::ValidSemanticInput(input) && input.sequence && input.seed;}
}
}

namespace {
void AssertInputEverywhere(Room& current,std::uint64_t lease,unsigned expected,std::uint64_t sequence) {
    for(unsigned i=0;i<3;++i) {
        CHECK(current.ApplyCount(i,lease)==expected);
        const auto input=current.LastInput(i,lease);CHECK(input.sequence==sequence);
        CHECK(input.seed==ep::InputSeed(current.epoch,lease,sequence));
    }
}
void StartProductionController(unsigned production,unsigned serial) {
    Room current(production,serial);const unsigned source=production==0 ? 0 : 2;
    CHECK(callbacks.online && callbacks.request && callbacks.input && callbacks.stop);
    CHECK(callbacks.online());
    const auto initial=current.Snapshot(source,5);
    CHECK(!callbacks.input(initial,{}));CHECK(native.applyCalls==0);
    CHECK(current.Request(source,5));
    if(production==1) {
        crew::EnemyControlNetworkStatus pending;
        CHECK(crew::ReadEnemyControlNetworkStatus(actors[source].id.data(),&pending));
        CHECK(pending.request && !pending.lease && pending.result==ep::Result::Pending && pending.profileHash==initial.profileHash);
        CHECK(!std::memcmp(pending.source,initial.owner,32) && !std::memcmp(pending.target,initial.target,32));
    }
    current.Until(source,ep::Phase::preparing);
    const auto preparing=*current.ForSource(production,actors[source].id);
    auto waiting=current.Snapshot(source,5,preparing.id);
    CHECK(!callbacks.input(waiting,{}));CHECK(native.applyCalls==0 && native.beginCalls==0);
    auto injectEarlyGuestInput=[&]() {
        if(production!=1)return;
        ep::Message input;input.kind=ep::Kind::InputRequest;input.epoch=current.epoch;input.lease=preparing.id;
        input.sequence=9000;input.seed=999;input.spec.profileHash=waiting.profileHash;
        input.inputSize=static_cast<std::uint16_t>(en::kInputBytes);
        CHECK(en::EncodeInput({},input.input.data(),en::kInputBytes));
        current.Deliver(current.Make(1,0,input));
        CHECK(current.ApplyCount(0,preparing.id)==0 && current.ApplyCount(1,preparing.id)==0 && current.ApplyCount(2,preparing.id)==0);
    };
    injectEarlyGuestInput();
    current.Until(source,ep::Phase::beginning);
    CHECK(native.beginCalls==1 && native.activateCalls==0);
    CHECK(crew::EnemyControlReadSnapshot(actors[source].Ref(),&waiting)); // fresh valid native pending snapshot
    CHECK(!callbacks.input(waiting,{}));CHECK(native.applyCalls==0);
    injectEarlyGuestInput();
    current.Until(source,ep::Phase::activating);
    CHECK(native.activateCalls==1);
    CHECK(crew::EnemyControlReadSnapshot(actors[source].Ref(),&waiting));
    CHECK(!callbacks.input(waiting,{}));CHECK(native.applyCalls==0);
    injectEarlyGuestInput(); // even native Activate is insufficient until every peer ACKs it
    current.Pump();auto snapshot=current.ActiveSnapshot(source);const auto lease=snapshot.lease;
    CHECK(native.describeCalls==0);CHECK(native.leases[lease].local);
    CHECK(native.leases[lease].source.obj==actors[source].Ref().obj && native.leases[lease].source.ctrl==actors[source].Ref().ctrl);
    CHECK(native.leases[lease].target.obj==actors[5].Ref().obj && native.leases[lease].target.ctrl==actors[5].Ref().ctrl);
    for(const auto& identity:native.resolvedSources)CHECK(identity==actors[source].id);
    for(const auto& identity:native.resolvedTargets)CHECK(identity==actors[5].id);
    CHECK(native.strictArguments && native.sealArguments && native.strictWithTarget && native.strictWithoutTarget);
    CHECK(native.idCalls && native.authorityCalls && native.prepareCalls && native.activateCalls==1);
    CHECK(native.authority==(production==0));
    ec::Input input;input.absoluteAim=true;input.move[0]=0.25f;input.sequence=777;input.seed=888;
    input.absoluteAim=true;input.aimYaw=0.35f;input.aimPitch=-0.2f;
    CHECK(callbacks.input(snapshot,input));current.Pump();AssertInputEverywhere(current,lease,1,1);
    for(unsigned i=0;i<3;++i){const auto acceptedAim=current.LastInput(i,lease);CHECK(acceptedAim.absoluteAim && acceptedAim.aimYaw==input.aimYaw && acceptedAim.aimPitch==input.aimPitch);}
    const auto accepted=current.LastPacket(ep::Kind::Input,0,production==0 ? 1 : production);
    current.Deliver(accepted);CHECK(current.ApplyCount(accepted.to,lease)==1);
    for(unsigned i=0;i<3;++i)CHECK(current.ApplyCount(i,lease)==1);
    CHECK(current.LastInput(production,lease).sequence!=input.sequence && current.LastInput(production,lease).seed!=input.seed);

    const unsigned applies=native.applyCalls,validates=native.validateCalls,restores=native.restoreCalls;
    auto wrong=snapshot;wrong.owner[0]^=1;CHECK(!callbacks.input(wrong,input) && !callbacks.stop(wrong));
    wrong=snapshot;wrong.target[0]^=1;CHECK(!callbacks.input(wrong,input) && !callbacks.stop(wrong));
    wrong=snapshot;wrong.profileHash^=1;CHECK(!callbacks.input(wrong,input) && !callbacks.stop(wrong));
    wrong=snapshot;++wrong.lease;CHECK(!callbacks.input(wrong,input) && !callbacks.stop(wrong));
    wrong=snapshot;++wrong.revision;CHECK(!callbacks.input(wrong,input) && !callbacks.stop(wrong));
    wrong=snapshot;++wrong.ownerHumanKey;CHECK(!callbacks.input(wrong,input) && !callbacks.stop(wrong));
    CHECK(native.applyCalls==applies && native.validateCalls==validates && native.restoreCalls==restores);

    // A forged owner sequence/seed is never the host's authoritative input metadata.
    if(production==1) {
        ep::Message message;message.kind=ep::Kind::InputRequest;message.epoch=current.epoch;message.lease=lease;
        message.sequence=9000;message.seed=123456;message.spec.profileHash=snapshot.profileHash;
        message.inputSize=static_cast<std::uint16_t>(en::kInputBytes);
        CHECK(en::EncodeInput(input,message.input.data(),en::kInputBytes));
        current.Deliver(current.Make(1,0,message));current.Pump();AssertInputEverywhere(current,lease,2,2);
    }
    CHECK(callbacks.stop(snapshot));current.Pump();current.Closed(lease);
    CHECK(native.strictCalls && native.sealCalls);
}
void GuestThroughProductionHost() {
    Room current(0,3);CHECK(current.PureRequest(1,2,5));current.Pump();
    const auto* live=current.ForSource(0,actors[2].id);CHECK(live && live->phase==ep::Phase::active);
    const auto lease=live->id;CHECK(native.beginCalls==1 && !native.leases[lease].local && native.describeCalls==0);
    ec::Input input;input.absoluteAim=true;input.active[0]={41,2};unsigned char bytes[en::kInputBytes]{};CHECK(en::EncodeInput(input,bytes,sizeof(bytes)));
    CHECK(current.nodes[1].protocol.SubmitInput(lease,bytes,sizeof(bytes),now));current.Pump();AssertInputEverywhere(current,lease,1,1);
    CHECK(current.nodes[1].protocol.Exit(lease,now));current.Pump();current.Closed(lease);
}
void AuthenticationAndPreparation() {
    unsigned serial=10;
    {Room current(0,serial++);CHECK(!current.Request(2,5));current.Pump();CHECK(native.beginCalls==0 && native.prepareCalls==0);CHECK(native.sealCalls>0);}
    {Room current(0,serial++);CHECK(current.PureRequest(1,0,5));current.Pump();
        CHECK(native.beginCalls==0 && native.prepareCalls==0 && native.sealCalls>0);
        const auto* rejected=current.nodes[1].protocol.RequestFor(actors[0].id);CHECK(rejected && rejected->result==ep::Result::Unauthorized);}
    {Room current(0,serial++);native.wrongRequesterPuid=true;CHECK(!current.Request(0,5));current.Pump();CHECK(native.beginCalls==0 && native.prepareCalls==0);}
    {Room current(0,serial++);native.sealed=false;CHECK(!current.Request(0,5));current.Pump();CHECK(native.beginCalls==0 && native.prepareCalls==0);}
    {Room current(0,serial++);native.strict=false;CHECK(!current.Request(0,5));current.Pump();CHECK(native.beginCalls==0 && native.sealCalls==0);}
    for(unsigned production=0;production<2;++production) {
        const unsigned source=production==0 ? 0 : 2;
        {Room current(production,serial++);actors[5].registered=false;const bool accepted=current.Request(source,5);current.Pump();
            CHECK(production ? accepted : !accepted);CHECK(native.beginCalls==0 && native.idCalls>0);}
        {Room current(production,serial++);native.corruptNativeId=true;const bool accepted=current.Request(source,5);current.Pump();
            CHECK(production ? accepted : !accepted);CHECK(native.beginCalls==0 && native.idCalls>0);}
        {Room current(production,serial++);native.authority=production!=0;const bool accepted=current.Request(source,5);current.Pump();
            CHECK(production ? accepted : !accepted);CHECK(native.beginCalls==0 && native.authorityCalls>0);}
        {Room current(production,serial++);native.pose=false;const bool accepted=current.Request(source,5);current.Pump();
            CHECK(production ? accepted : !accepted);CHECK(native.beginCalls==0 && native.prepareCalls>0);}
        {Room current(production,serial++);native.profileMismatch=true;const bool accepted=current.Request(source,5);current.Pump();
            CHECK(production ? accepted : !accepted);CHECK(native.beginCalls==0 && native.prepareCalls>0);}
        {Room current(production,serial++);auto missing=current.Snapshot(source,5);missing.profileHash=0;
            const bool accepted=callbacks.request(missing);current.Pump();CHECK(production ? accepted : !accepted);
            CHECK(native.beginCalls==0 && native.prepareCalls==0);}
        {Room current(production,serial++);auto wrong=current.Snapshot(source,5);wrong.profileHash^=1;const bool accepted=callbacks.request(wrong);current.Pump();
            CHECK(production ? accepted : !accepted);CHECK(native.beginCalls==0);}
    }
    {Room current(0,serial++);auto invalid=current.Snapshot(0,5);invalid.owner[20]=1;const auto before=native.strictCalls;
        CHECK(!callbacks.request(invalid));CHECK(native.strictCalls==before);}
    {Room current(0,serial++);crew::UpdateEnemyControlNetwork({false,true,current.epoch,0,2,kPuid[0],&BridgeSend},now);
        CHECK(callbacks.online());crew::EnemyControlNetworkStatus status;CHECK(crew::ReadEnemyControlNetworkStatus(nullptr,&status));
        CHECK(status.online && !status.ready);CHECK(!current.Request(0,5));CHECK(native.beginCalls==0 && native.prepareCalls==0);}
}
void LocalCapacityProofGatesEnemyControl() {
    // An API-v1 support endpoint can remain online while the separate capacity
    // query is missing or proves the older, smaller reliable inbox/backlog.
    static_assert(EDF6COOP_EXTENSION_VERSION==1,"ordinary support ABI remains API v1");
    EDF6CoopExtensionCapacity proof{sizeof(EDF6CoopExtensionCapacity),EDF6COOP_EXTENSION_CAPACITY_VERSION,
        EDF6COOP_EXTENSION_MAX_PAYLOAD,32,crew::kEnemyControlInboxMessages,crew::kEnemyControlPendingBytes,
        crew::kEnemyControlPendingBytes,EDF6COOP_EXTENSION_CAPACITY_RELIABLE_ORDERED|EDF6COOP_EXTENSION_CAPACITY_BOUNDED_TRY_SEND};
    CHECK(crew::ValidEnemyControlCapacity(proof));auto oldSdk=proof;
    oldSdk.inboxMessages=1024;oldSdk.pendingBytes=oldSdk.congestedBacklogBytes=256*1024;
    CHECK(!crew::ValidEnemyControlCapacity(oldSdk));
    unsigned serial=210;
    for(unsigned production=0;production<2;++production)for(bool missingQuery:{false,true}) {
        const bool ready=missingQuery ? false : crew::ValidEnemyControlCapacity(oldSdk);
        Room current(production,serial++,ready);const unsigned source=production ? 2 : 0;
        CHECK(callbacks.online() && native.capacityQueries);
        CHECK(!current.Request(source,5));current.Pump();
        CHECK(!native.beginCalls && !native.describeCalls && !native.prepareCalls);
        crew::EnemyControlNetworkStatus status;CHECK(crew::ReadEnemyControlNetworkStatus(nullptr,&status));
        CHECK(status.online && !status.ready);
        unsigned advertisements=0;
        for(const auto& packet:current.history)if(packet.from==production) {
            ep::Message message;CHECK(ep::Decode(packet.bytes.data(),packet.bytes.size(),message));
            if(message.kind==ep::Kind::Hello || message.kind==ep::Kind::Welcome){CHECK(!message.capability);++advertisements;}
        }
        CHECK(advertisements);for(unsigned node=0;node<3;++node)CHECK(!current.Protocol(node).Ready() && !current.Protocol(node).ActiveCount());
    }
}
void CapacityProofLossRetainsNativeRestoreJournal() {
    unsigned serial=220;
    for(unsigned production=0;production<2;++production) {
        // A negative host proof must revoke already welcomed idle guests on the
        // current authenticated HELLO, rather than waiting for five-second expiry.
        {Room current(production,serial++);native.capacityReady=false;const auto changedAt=now;current.Step(1001);
            CHECK(now-changedAt<ep::kTimeoutMs && callbacks.online());
            for(unsigned node=0;node<3;++node)CHECK(!current.Protocol(node).Ready());
            CHECK(!current.Request(production ? 2 : 0,5) && !native.beginCalls);}
        Room current(production,serial++);const unsigned source=production ? 2 : 0;
        CHECK(current.Request(source,5));current.Pump();const auto snapshot=current.ActiveSnapshot(source);
        const auto begins=native.beginCalls,restoreCalls=native.restoreCalls;native.restoreFailures=100;native.capacityReady=false;
        current.Step();const auto* retained=current.Lease(production,snapshot.lease);
        CHECK(retained && retained->phase==ep::Phase::restoring && !retained->localRestored);
        CHECK(retained->spec.requesterPlayer==actors[source].id && retained->spec.target==actors[5].id && retained->spec.profileHash==snapshot.profileHash);
        CHECK(native.leases[snapshot.lease].captured && native.restoreCalls>restoreCalls && callbacks.online());
        CHECK(!current.Protocol(production).Ready() && !callbacks.request(current.Snapshot(source,6)) && native.beginCalls==begins);
        const auto restoreAttempts=native.restoreCalls;current.Step();retained=current.Lease(production,snapshot.lease);
        CHECK(retained && retained->phase==ep::Phase::restoring && !retained->localRestored && native.leases[snapshot.lease].captured && native.restoreCalls>restoreAttempts);
        crew::EnemyControlNetworkStatus status;CHECK(crew::ReadEnemyControlNetworkStatus(nullptr,&status));CHECK(status.online && !status.ready);
        native.restoreFailures=0;current.Step();CHECK(!native.leases[snapshot.lease].captured && !native.leases[snapshot.lease].active);
        current.Step(1001);for(unsigned node=0;node<3;++node)CHECK(!current.Protocol(node).Ready());
        CHECK(native.beginCalls==begins && !callbacks.input(snapshot,{}) && !callbacks.stop(snapshot));
        // A fresh authenticated world may resume only after the retained native
        // journal succeeded and every peer advertised current capacity again.
        native.capacityReady=true;++current.epoch;for(unsigned node=0;node<3;++node)if(node!=production)current.nodes[node].context.epoch=current.epoch;
        current.Step(1001);for(unsigned node=0;node<3;++node)CHECK(current.Protocol(node).Ready());
        CHECK(current.Request(source,6));current.Pump();const auto next=current.ActiveSnapshot(source);CHECK(native.beginCalls==begins+1);
        CHECK(!callbacks.input(snapshot,{}) && !callbacks.stop(snapshot));CHECK(callbacks.stop(next));current.Pump();current.Closed(next.lease);
    }
}
void PerSourceRejectedStatus() {
    Room current(0,35);CHECK(current.Request(0,5));current.Pump();const auto active=current.ActiveSnapshot(0);
    auto invalid=current.Snapshot(1,6);invalid.profileHash^=1;
    CHECK(!callbacks.request(invalid));current.Pump();
    crew::EnemyControlNetworkStatus rejected,other;
    CHECK(crew::ReadEnemyControlNetworkStatus(actors[1].id.data(),&rejected));
    CHECK(rejected.request && !rejected.lease && rejected.result==ep::Result::Unsupported && rejected.profileHash==invalid.profileHash);
    CHECK(!std::memcmp(rejected.source,invalid.owner,32) && !std::memcmp(rejected.target,invalid.target,32));
    CHECK(crew::ReadEnemyControlNetworkStatus(actors[0].id.data(),&other));
    CHECK(other.lease==active.lease && other.phase==ep::Phase::active && other.result==ep::Result::Active);
    CHECK(current.PureRequest(1,2,7));current.Pump(); // unrelated guest changes the room's last outcome
    CHECK(current.Protocol(0).LastResult()==ep::Result::Active);
    CHECK(crew::ReadEnemyControlNetworkStatus(actors[1].id.data(),&rejected));
    CHECK(!rejected.lease && rejected.result==ep::Result::Unsupported && rejected.profileHash==invalid.profileHash);
    CHECK(!std::memcmp(rejected.source,invalid.owner,32) && !std::memcmp(rejected.target,invalid.target,32));
    CHECK(callbacks.stop(active));current.Pump();current.Closed(active.lease);
    const auto* guest=current.ForSource(0,actors[2].id);CHECK(guest && guest->phase==ep::Phase::active);
    const auto guestLease=guest->id;CHECK(current.nodes[1].protocol.Exit(guestLease,now));current.Pump();current.Closed(guestLease);
}
void SplitScreenAndSampling(unsigned production,unsigned serial) {
    Room current(production,serial);const unsigned first=production==0 ? 0 : 2,second=first+1;
    CHECK(current.Request(first,5));current.Pump();const auto one=current.ActiveSnapshot(first);
    CHECK(current.Request(second,6));current.Pump();const auto two=current.ActiveSnapshot(second);CHECK(one.lease!=two.lease);
    crew::EnemyControlNetworkStatus statusOne,statusTwo;
    CHECK(crew::ReadEnemyControlNetworkStatus(actors[first].id.data(),&statusOne));
    CHECK(crew::ReadEnemyControlNetworkStatus(actors[second].id.data(),&statusTwo));
    CHECK(statusOne.lease==one.lease && statusTwo.lease==two.lease && statusOne.profileHash==actors[5].profile && statusTwo.profileHash==actors[6].profile);
    CHECK(!std::memcmp(statusOne.source,actors[first].id.data(),32) && !std::memcmp(statusTwo.source,actors[second].id.data(),32));
    CHECK(native.leases[one.lease].source.obj==actors[first].Ref().obj && native.leases[two.lease].source.obj==actors[second].Ref().obj);
    const unsigned beforeValidate=native.validateCalls,beforeApply=native.applyCalls,beforeRestore=native.restoreCalls;
    auto mixed=one;std::memcpy(mixed.owner,two.owner,32);CHECK(!callbacks.input(mixed,{}) && !callbacks.stop(mixed));
    CHECK(current.ForSource(production,actors[first].id)->phase==ep::Phase::active && current.ForSource(production,actors[second].id)->phase==ep::Phase::active);
    CHECK(native.validateCalls==beforeValidate && native.applyCalls==beforeApply && native.restoreCalls==beforeRestore);
    ec::Input input;input.move[0]=0.1f;input.absoluteAim=true;input.aimYaw=0.1f;input.aimPitch=0.2f;CHECK(callbacks.input(one,input));current.Pump();AssertInputEverywhere(current,one.lease,1,1);
    now+=1;input.move[0]=0.2f;input.aimYaw=0.4f;input.aimPitch=0.1f;CHECK(callbacks.input(one,input));
    now+=1;input.move[0]=0.3f;input.look[1]=0.4f;input.aimYaw=0.8f;input.aimPitch=-0.3f;CHECK(callbacks.input(one,input));
    CHECK(current.ApplyCount(production,one.lease)==1);
    now+=30;crew::EnemyControlNetworkFrame();current.Pump();CHECK(current.ApplyCount(production,one.lease)==1);
    ++now;crew::EnemyControlNetworkFrame();current.Pump();AssertInputEverywhere(current,one.lease,2,2);
    CHECK(current.LastInput(production,one.lease).move[0]==0.3f && current.LastInput(production,one.lease).look[1]==0.4f);
    for(unsigned i=0;i<3;++i){const auto aim=current.LastInput(i,one.lease);CHECK(aim.absoluteAim && aim.aimYaw==0.8f && aim.aimPitch==-0.3f);}
    ++now;input.move[0]=0.4f;CHECK(callbacks.input(one,input)); // queued look sample
    input.active[0]={51,2};CHECK(callbacks.input(one,input));current.Pump();AssertInputEverywhere(current,one.lease,3,3);
    CHECK(current.LastInput(production,one.lease).active[0].phase==2);
    ++now;input.active[0]={51,1};CHECK(callbacks.input(one,input));current.Pump();AssertInputEverywhere(current,one.lease,4,4);
    ++now;input.look[0]=0.7f;CHECK(callbacks.input(one,input)); // held look queued
    input.active[0]={};CHECK(callbacks.input(one,input));current.Pump();AssertInputEverywhere(current,one.lease,5,5);
    CHECK(current.LastInput(production,one.lease).active[0].id==0);
    now+=34;crew::EnemyControlNetworkFrame();current.Pump();CHECK(current.ApplyCount(production,one.lease)==5);
    CHECK(current.ApplyCount(production,two.lease)==0);
    CHECK(callbacks.stop(one));current.Pump();current.Closed(one.lease);
    CHECK(current.ForSource(production,actors[second].id)->phase==ep::Phase::active);
    CHECK(callbacks.input(two,{}));current.Pump();AssertInputEverywhere(current,two.lease,1,1);
    CHECK(callbacks.stop(two));current.Pump();current.Closed(two.lease);
}
void FailureAndRestore(unsigned production,unsigned serial) {
    const unsigned source=production==0 ? 0 : 2;
    {Room current(production,serial++);CHECK(current.Request(source,5));current.Pump();const auto snapshot=current.ActiveSnapshot(source);
        native.failApply=true;const bool queuedInput=callbacks.input(snapshot,{});CHECK(production ? queuedInput : !queuedInput);current.Pump();current.Closed(snapshot.lease);CHECK(native.applyCalls==1 && native.restoreCalls>0);
        for(unsigned i=0;i<3;++i)CHECK(current.ApplyCount(i,snapshot.lease)==1);}
    {Room current(production,serial++);CHECK(current.Request(source,5));current.Pump();const auto snapshot=current.ActiveSnapshot(source);
        native.wrongRequesterPuid=true;CHECK(!callbacks.input(snapshot,{}));crew::EnemyControlNetworkFrame();
        current.Pump();current.Closed(snapshot.lease);CHECK(native.applyCalls==0 && native.restoreCalls>0);}
    {Room current(production,serial++);native.failBegin=true;const bool accepted=current.Request(source,5);CHECK(accepted);
        current.Pump();for(unsigned i=0;i<4;++i)current.Step();
        CHECK(native.beginCalls==1 && native.activateCalls==0 && native.applyCalls==0 && native.restoreCalls>0);
        for(const auto& lease:native.leases)CHECK(!lease.second.captured && !lease.second.active);}
    {Room current(production,serial++);native.failActivate=true;CHECK(current.Request(source,5));current.Pump();for(unsigned i=0;i<5;++i)current.Step();
        CHECK(native.beginCalls==1 && native.activateCalls==1 && native.applyCalls==0 && native.restoreCalls>0);
        for(const auto& lease:native.leases)CHECK(!lease.second.captured && !lease.second.active);}
    {Room current(production,serial++);CHECK(current.Request(source,5));current.Pump();const auto snapshot=current.ActiveSnapshot(source);
        actors[5].alive=false;crew::EnemyControlNetworkFrame();current.Pump();current.Closed(snapshot.lease);CHECK(native.restoreCalls>0);}
    {Room current(production,serial++);CHECK(current.Request(source,5));current.Pump();const auto snapshot=current.ActiveSnapshot(source);
        native.authority=!native.authority;crew::EnemyControlNetworkFrame();current.Pump();current.Closed(snapshot.lease);CHECK(native.restoreCalls>0);}
    {Room current(production,serial++);CHECK(current.Request(source,5));current.Pump();const auto snapshot=current.ActiveSnapshot(source);
        native.restoreFailures=1;CHECK(callbacks.stop(snapshot));current.Pump();
        const auto* waiting=current.Lease(production,snapshot.lease);CHECK(waiting && waiting->phase==ep::Phase::restoring && !waiting->localRestored);
        CHECK(native.leases[snapshot.lease].captured);CHECK(waiting->spec.profileHash==snapshot.profileHash && waiting->spec.requesterPlayer==actors[source].id && waiting->spec.target==actors[5].id);
        ++now;crew::EnemyControlNetworkFrame();current.Pump();CHECK(!native.leases[snapshot.lease].captured);
        for(unsigned i=0;i<4;++i)current.Step(101);current.Closed(snapshot.lease);}
    {Room current(production,serial++);CHECK(current.Request(source,5));current.Pump();const auto snapshot=current.ActiveSnapshot(source);
        native.restoreFailures=2;crew::ResetEnemyControlNetwork();current.Pump();
        const auto* waiting=current.Lease(production,snapshot.lease);CHECK(waiting && waiting->phase==ep::Phase::restoring && !waiting->localRestored);
        CHECK(native.leases[snapshot.lease].captured && callbacks.online());
        ++now;crew::EnemyControlNetworkFrame();CHECK(native.leases[snapshot.lease].captured);
        ++now;crew::EnemyControlNetworkFrame();CHECK(!native.leases[snapshot.lease].captured);
        waiting=current.Lease(production,snapshot.lease);CHECK(waiting && waiting->phase==ep::Phase::closed && waiting->localRestored);
        crew::EnemyControlNetworkStatus status;CHECK(crew::ReadEnemyControlNetworkStatus(actors[source].id.data(),&status));CHECK(status.online && !status.ready);
        CHECK(!current.Request(source,5));}
}
void ReadyContextCannotSkipRestore(unsigned production,unsigned serial) {
    Room current(production,serial);const unsigned source=production==0 ? 0 : 2;
    CHECK(current.Request(source,5));current.Pump();const auto snapshot=current.ActiveSnapshot(source);
    native.restoreFailures=100;crew::ResetEnemyControlNetwork();current.Pump();
    const unsigned begins=native.beginCalls;
    ++now;const auto nextEpoch=current.epoch+1000;
    crew::UpdateEnemyControlNetwork({true,production==0,nextEpoch,production ? Room::Peer(production,0) : 0,2,kPuid[production],&BridgeSend},now);
    const auto* old=current.Lease(production,snapshot.lease);
    CHECK(old && old->phase==ep::Phase::restoring && !old->localRestored && old->spec.requesterPlayer==actors[source].id && old->spec.target==actors[5].id && old->spec.profileHash==snapshot.profileHash);
    CHECK(native.leases[snapshot.lease].captured && !current.Protocol(production).Ready());
    CHECK(!callbacks.request(current.Snapshot(source,6)));CHECK(native.beginCalls==begins);
    old=current.Lease(production,snapshot.lease);CHECK(old && old->phase==ep::Phase::restoring && !old->localRestored);
    native.restoreFailures=0;++now;crew::EnemyControlNetworkFrame();CHECK(!native.leases[snapshot.lease].captured);
    CHECK(native.beginCalls==begins);
}
void PendingCancellationUsesCurrentUiBinding() {
    Room current(1,89);
    auto first=current.Snapshot(2,5);CHECK(callbacks.request(first));
    crew::EnemyControlNetworkStatus pending;CHECK(crew::ReadEnemyControlNetworkStatus(actors[2].id.data(),&pending));
    CHECK(pending.request && !pending.lease && pending.result==ep::Result::Pending);first.request=pending.request;
    CHECK(callbacks.stop(first));CHECK(crew::ReadEnemyControlNetworkStatus(actors[2].id.data(),&pending));
    CHECK(pending.phase==ep::Phase::restoring && pending.result==ep::Result::Pending && !pending.lease);current.Pump();
    CHECK(native.beginCalls==0 && native.activateCalls==0 && native.applyCalls==0);
    CHECK(!current.ForSource(1,actors[2].id));
    for(unsigned i=0;i<3;++i)CHECK(current.Protocol(i).ActiveCount()==0);
    if(const auto* cancelled=current.ForSource(0,actors[2].id)) {
        const auto cancelledId=cancelled->id;unsigned frames=0;
        while(current.Lease(0,cancelledId)->phase!=ep::Phase::closed){CHECK(++frames<20);current.Step();}
    }
    auto retry=current.Snapshot(2,5);CHECK(retry.revision!=first.revision);CHECK(callbacks.request(retry));
    CHECK(crew::ReadEnemyControlNetworkStatus(actors[2].id.data(),&pending));
    CHECK(pending.request!=first.request && pending.result==ep::Result::Pending);retry.request=pending.request;
    CHECK(!callbacks.stop(first));first.request=0;CHECK(!callbacks.stop(first)); // revision remains stale even without a nonce hint
    current.Until(2,ep::Phase::preparing);
    CHECK(native.beginCalls==0);
    CHECK(crew::ReadEnemyControlNetworkStatus(actors[2].id.data(),&pending));
    CHECK(pending.request==retry.request && pending.lease && pending.result==ep::Result::Pending);
    retry.lease=pending.lease;
    CHECK(callbacks.stop(retry));CHECK(crew::ReadEnemyControlNetworkStatus(actors[2].id.data(),&pending));
    CHECK(pending.phase==ep::Phase::restoring && pending.result==ep::Result::Pending);current.Pump();current.Closed(pending.lease);
    CHECK(native.beginCalls==0 && native.activateCalls==0 && native.applyCalls==0);
    CHECK(!current.ForSource(1,actors[2].id));
    for(unsigned i=0;i<3;++i)CHECK(current.Protocol(i).ActiveCount()==0);
}
void EpochPendingSampleDoesNotCrossWorld() {
    Room current(1,90);CHECK(current.Request(2,5));current.Pump();const auto previous=current.ActiveSnapshot(2);
    ec::Input held;held.absoluteAim=true;held.active[0]={61,1};held.move[0]=0.1f;
    CHECK(callbacks.input(previous,held));current.Pump();CHECK(current.ApplyCount(1,previous.lease)==1);
    ++now;held.move[0]=0.9f;held.look[0]=0.8f;CHECK(callbacks.input(previous,held));
    CHECK(current.ApplyCount(1,previous.lease)==1);
    // A different host/world can legitimately restart its own lease counter at 1.
    // Keep source/target/profile/native references identical to exercise stale
    // sample identity, rather than relying on a different actor to reject it.
    for(unsigned i=0;i<3;++i)if(i!=current.bridge) {
        auto& node=current.nodes[i];node.protocol.Reset(now);
        auto fresh=std::make_unique<ep::Protocol>(ep::Hooks{&node,&PureSend,&PureAuthenticate,&PureResolve,&PurePrepare,&PureBegin,&PureValidate,&PureApply,&PureRestore,&PureActivate,&PurePose,&PureAction});
        node.protocol=std::move(*fresh);
        node.baselines.clear();node.inputs.clear();node.context.epoch=current.epoch+1;
    }
    current.queue.clear();++current.epoch;++now;current.TickAll();current.Pump();
    now+=1001;current.TickAll();current.Pump();CHECK(current.Protocol(1).Ready());
    CHECK(!native.leases[previous.lease].captured);
    CHECK(current.Request(2,5));current.Pump();const auto next=current.ActiveSnapshot(2);
    CHECK(next.lease==previous.lease && next.profileHash==previous.profileHash && !std::memcmp(next.owner,previous.owner,32) && !std::memcmp(next.target,previous.target,32));
    CHECK(next.revision!=previous.revision);
    CHECK(!callbacks.input(previous,held) && !callbacks.stop(previous));
    const unsigned applications=current.ApplyCount(1,next.lease);
    now+=34;crew::EnemyControlNetworkFrame();current.Pump();
    CHECK(current.ApplyCount(1,next.lease)==applications);
    CHECK(current.ApplyCount(0,next.lease)==0 && current.ApplyCount(2,next.lease)==0);
    CHECK(callbacks.stop(next));current.Pump();current.Closed(next.lease);
}


unsigned PosePacketCount(const Room& current) {
    unsigned count=0;for(const auto& packet:current.history){ep::Message message;CHECK(ep::Decode(packet.bytes.data(),packet.bytes.size(),message));if(message.kind==ep::Kind::Pose)++count;}return count;
}
std::vector<Packet> TakePackets(Room& current,unsigned recipient,ep::Kind kind) {
    std::vector<Packet> out;std::deque<Packet> kept;
    while(!current.queue.empty()) {
        const auto packet=current.queue.front();current.queue.pop_front();ep::Message message;
        CHECK(ep::Decode(packet.bytes.data(),packet.bytes.size(),message));
        if(packet.to==recipient && message.kind==kind)out.push_back(packet);else kept.push_back(packet);
    }
    current.queue=kept;return out;
}
std::vector<Packet> TakePosePackets(Room& current,unsigned recipient) {return TakePackets(current,recipient,ep::Kind::Pose);}
// Transport delivers one exact-length envelope. Every shorter prefix and an
// extra padding byte must be rejected before any native apply or ACK, on the
// production bridge and on the pure peers alike.
void RejectNonCanonicalSizes(Room& current,const Packet& packet) {
    ep::Message decoded;CHECK(ep::Decode(packet.bytes.data(),packet.bytes.size(),decoded));
    CHECK(packet.bytes.size()==ep::EncodedSize(decoded));
    const auto inputCalls=native.applyCalls,poseCalls=native.poseApplyCalls,actionCalls=native.actionApplyCalls;
    const auto peerInputs=current.nodes[packet.to].applies,peerPoses=current.nodes[packet.to].poseApplies;
    const auto peerActions=current.nodes[packet.to].actions.size(),queued=current.queue.size();
    const auto attempt=[&](const void* bytes,std::size_t size) {
        ep::Message rejected;CHECK(!ep::Decode(bytes,size,rejected));
        const unsigned peer=Room::Peer(packet.to,packet.from);
        const bool claimed=packet.to==current.bridge ?
            crew::ReceiveEnemyControlNetwork(peer,kPuid[packet.from],bytes,size,now) :
            current.nodes[packet.to].protocol.Receive(peer,kPuid[packet.from],bytes,size,now);
        CHECK(claimed==ep::Owned(bytes,size));
        CHECK(native.applyCalls==inputCalls && native.poseApplyCalls==poseCalls && native.actionApplyCalls==actionCalls);
        CHECK(current.nodes[packet.to].applies==peerInputs && current.nodes[packet.to].poseApplies==peerPoses &&
            current.nodes[packet.to].actions.size()==peerActions && current.queue.size()==queued);
    };
    for(std::size_t size=0;size<packet.bytes.size();++size)attempt(packet.bytes.data(),size);
    auto padded=packet.bytes;padded.push_back(0);attempt(padded.data(),padded.size());
}
void HostPoseSamplingAndAtomicDelivery() {
    Room current(0,100);native.fallback=true;CHECK(ep::kVersion==6 && ep::kCapability==5);
    CHECK(current.Request(0,5));current.Until(0,ep::Phase::beginning);
    crew::EnemyControlNetworkFrame();CHECK(native.poseCaptureCalls==0 && PosePacketCount(current)==0);
    current.Until(0,ep::Phase::activating);crew::EnemyControlNetworkFrame();
    CHECK(native.poseCaptureCalls==0 && PosePacketCount(current)==0);current.Pump();
    const auto snapshot=current.ActiveSnapshot(0);CHECK(!std::strcmp(native.leases[snapshot.lease].authenticatedPuid,kPuid[0]));
    crew::EnemyControlNetworkFrame();CHECK(native.poseCaptureCalls==1 && native.poseCapacity==ep::kMaxPoseBytes);
    CHECK(native.capturedPose==PoseBytes(ep::kMaxPoseBytes,1) && PosePacketCount(current)==2);
    const auto envelopes=TakePosePackets(current,1);CHECK(envelopes.size()==1);
    ep::Message message;CHECK(ep::Decode(envelopes[0].bytes.data(),envelopes[0].bytes.size(),message));
    CHECK(message.sequence==1 && message.epoch==current.epoch && message.lease==snapshot.lease && message.request);
    CHECK(message.capability==(1u<<16) && message.inputSize==ep::kMaxPoseBytes && envelopes[0].bytes.size()==ep::kWireBytes);
    CHECK(message.spec.requesterPlayer==actors[0].id && message.spec.target==actors[5].id && message.spec.profileHash==snapshot.profileHash);
    CHECK(!std::strcmp(message.spec.ownerPuid,kPuid[0]));RejectNonCanonicalSizes(current,envelopes[0]);
    CHECK(current.nodes[1].poseApplies==0);current.Deliver(envelopes[0]);CHECK(current.nodes[1].poseApplies==1);
    current.Pump();CHECK(current.nodes[2].poseApplies==1 && native.poseApplyCalls==0);
    CHECK(current.nodes[1].poses[snapshot.lease]==native.capturedPose && current.nodes[2].poses[snapshot.lease]==native.capturedPose);
    now+=32;crew::EnemyControlNetworkFrame();CHECK(native.poseCaptureCalls==1);
    ++now;++native.poseGeneration;crew::EnemyControlNetworkFrame();CHECK(native.poseCaptureCalls==2);current.Pump();
    CHECK(current.nodes[1].poseApplies==2 && current.nodes[2].poseApplies==2);
    CHECK(current.nodes[1].poseSequences[snapshot.lease]==2 && current.nodes[1].poses[snapshot.lease]==PoseBytes(ep::kMaxPoseBytes,2));
    CHECK(native.applyCalls==0 && current.ApplyCount(1,snapshot.lease)==0);
    CHECK(callbacks.stop(snapshot));current.Pump();current.Closed(snapshot.lease);
    const auto captureCount=native.poseCaptureCalls;now+=34;crew::EnemyControlNetworkFrame();CHECK(native.poseCaptureCalls==captureCount);
}
void GuestPoseAuthenticationAndAtomicDelivery() {
    Room current(1,101);native.fallback=true;CHECK(current.Request(2,5));current.Until(2,ep::Phase::activating);
    const auto* pending=current.ForSource(1,actors[2].id);CHECK(pending);const auto id=pending->id;
    const auto first=PoseBytes(ep::kMaxPoseBytes,3);CHECK(!current.Protocol(0).SubmitPose(id,first.data(),first.size(),now));
    CHECK(native.poseApplyCalls==0);current.Pump();const auto snapshot=current.ActiveSnapshot(2);
    CHECK(!std::strcmp(native.leases[id].authenticatedPuid,kPuid[1]));crew::EnemyControlNetworkFrame();
    CHECK(native.poseCaptureCalls==0);CHECK(!current.Protocol(1).SubmitPose(id,first.data(),first.size(),now));
    CHECK(current.Protocol(0).SubmitPose(id,first.data(),first.size(),now));auto envelopes=TakePosePackets(current,1);CHECK(envelopes.size()==1);
    auto forged=envelopes[0];forged.from=2;current.Deliver(forged,kPuid[0]);current.Deliver(envelopes[0],kPuid[2]);
    CHECK(native.poseApplyCalls==0 && current.ForSource(1,actors[2].id)->phase==ep::Phase::active);
    RejectNonCanonicalSizes(current,envelopes[0]);CHECK(native.poseApplyCalls==0);current.Deliver(envelopes[0]);CHECK(native.poseApplyCalls==1);
    current.Pump();CHECK(native.appliedPose==first && current.nodes[2].poses[id]==first && current.nodes[2].poseApplies==1);
    current.Deliver(envelopes[0]);CHECK(native.poseApplyCalls==1);
    const auto next=PoseBytes(385,4);now+=33;CHECK(current.Protocol(0).SubmitPose(id,next.data(),next.size(),now));
    envelopes=TakePosePackets(current,1);CHECK(envelopes.size()==1);
    ep::Message last;CHECK(ep::Decode(envelopes[0].bytes.data(),envelopes[0].bytes.size(),last));
    CHECK(last.inputSize==next.size() && last.sequence==2 && envelopes[0].bytes.size()==ep::kWirePrefixBytes+next.size());
    RejectNonCanonicalSizes(current,envelopes[0]);CHECK(native.poseApplyCalls==1 && native.appliedPose==first);
    current.Deliver(envelopes[0]);current.Pump();CHECK(native.poseApplyCalls==2 && native.appliedPose==next);
    CHECK(native.poseCaptureCalls==0 && current.ApplyCount(1,id)==0);
    CHECK(callbacks.stop(snapshot));current.Pump();current.Closed(id);
}
void PoseFailureAndLoss() {
    unsigned serial=110;
    for(unsigned failure=0;failure<3;++failure) {
        Room current(0,serial++);native.fallback=true;CHECK(current.Request(0,5));current.Pump();const auto snapshot=current.ActiveSnapshot(0);
        if(failure==0)native.failPoseCapture=true;else native.poseSize=failure==1 ? 0 : ep::kMaxPoseBytes+1;
        crew::EnemyControlNetworkFrame();current.Pump();current.Closed(snapshot.lease);
        CHECK(native.poseCaptureCalls==1 && native.poseApplyCalls==0 && native.restoreCalls && PosePacketCount(current)==0);
    }
    {Room current(1,serial++);native.fallback=true;CHECK(current.Request(2,5));current.Pump();const auto snapshot=current.ActiveSnapshot(2);
        crew::EnemyControlNetworkFrame();native.failPoseApply=true;native.restoreFailures=100;
        const auto bytes=PoseBytes(512,6);CHECK(current.Protocol(0).SubmitPose(snapshot.lease,bytes.data(),bytes.size(),now));current.Pump();current.Step();
        const auto* lease=current.Lease(1,snapshot.lease);CHECK(lease && lease->phase==ep::Phase::restoring && !lease->localRestored);
        CHECK(native.poseApplyCalls==1 && native.leases[snapshot.lease].captured);
        native.restoreFailures=0;++now;crew::EnemyControlNetworkFrame();current.Pump();for(unsigned i=0;i<2;++i)current.Step(101);current.Closed(snapshot.lease);
        CHECK(!native.leases[snapshot.lease].captured);}
    {Room current(1,serial++);native.fallback=true;CHECK(current.Request(2,5));current.Pump();const auto snapshot=current.ActiveSnapshot(2);
        crew::EnemyControlNetworkFrame();const auto bytes=PoseBytes(512,7);
        CHECK(current.Protocol(0).SubmitPose(snapshot.lease,bytes.data(),bytes.size(),now));const auto envelopes=TakePosePackets(current,1);CHECK(envelopes.size()==1);
        RejectNonCanonicalSizes(current,envelopes[0]);current.Pump();CHECK(native.poseApplyCalls==0);
        now+=500;crew::EnemyControlNetworkFrame();current.Pump();CHECK(current.ForSource(1,actors[2].id)->phase==ep::Phase::active);
        ++now;crew::EnemyControlNetworkFrame();current.Pump();current.Closed(snapshot.lease);
        CHECK(native.poseApplyCalls==0 && native.restoreCalls);current.Deliver(envelopes[0]);CHECK(native.poseApplyCalls==0);}
    {Room current(1,serial++);native.fallback=true;CHECK(current.Request(2,5));current.Pump();const auto snapshot=current.ActiveSnapshot(2);
        crew::EnemyControlNetworkFrame();const auto bytes=PoseBytes(512,8);now+=400;
        CHECK(current.Protocol(0).SubmitPose(snapshot.lease,bytes.data(),bytes.size(),now));current.Pump();CHECK(native.poseApplyCalls==1);
        now+=500;crew::EnemyControlNetworkFrame();current.Pump();CHECK(current.ForSource(1,actors[2].id)->phase==ep::Phase::active);
        ++now;crew::EnemyControlNetworkFrame();current.Pump();current.Closed(snapshot.lease);CHECK(native.restoreCalls);}
}
void PoseRevalidatesNativeActorBinding() {
    unsigned serial=150;
    for(unsigned invalid=0;invalid<7;++invalid) {
        Room current(1,serial++);native.fallback=true;CHECK(current.Request(2,5));current.Pump();const auto snapshot=current.ActiveSnapshot(2);
        crew::EnemyControlNetworkFrame();const auto bytes=PoseBytes(512,12);
        CHECK(current.Protocol(0).SubmitPose(snapshot.lease,bytes.data(),bytes.size(),now));
        const auto envelopes=TakePosePackets(current,1);CHECK(envelopes.size()==1);
        const auto sealCalls=native.sealCalls,strictCalls=native.strictCalls;
        switch(invalid) {
        case 0:native.strict=false;break;
        case 1:native.wrongRequesterPuid=true;break;
        case 2:actors[5].registered=false;break;
        case 3:native.nativeId=false;break;
        case 4:native.corruptNativeId=true;break;
        case 5:native.authority=true;break;
        default:actors[5].alive=false;break;
        }
        current.Deliver(envelopes[0]);current.Pump();current.Closed(snapshot.lease);
        CHECK(native.poseApplyCalls==0 && native.restoreCalls>0 && native.strictCalls>strictCalls);
        if(invalid!=0)CHECK(native.sealCalls>sealCalls);
    }
    for(unsigned invalid=0;invalid<5;++invalid) {
        Room current(1,serial++);native.fallback=true;CHECK(current.Request(2,5));current.Pump();const auto snapshot=current.ActiveSnapshot(2);
        const auto bytes=PoseBytes(512,13);CHECK(current.Protocol(0).SubmitPose(snapshot.lease,bytes.data(),bytes.size(),now));
        const auto envelopes=TakePosePackets(current,1);CHECK(envelopes.size()==1);
        ep::Message message;CHECK(ep::Decode(envelopes[0].bytes.data(),envelopes[0].bytes.size(),message));
        switch(invalid) {
        case 0:message.spec.requesterPlayer=actors[3].id;break; // same PUID, distinct split-screen source
        case 1:message.spec.target=actors[6].id;break;
        case 2:message.spec.profileHash^=1;break;
        case 3:std::memset(message.spec.ownerPuid,0,sizeof(message.spec.ownerPuid));std::memcpy(message.spec.ownerPuid,kPuid[2],std::strlen(kPuid[2])+1);break;
        default:++message.request;break;
        }
        current.Deliver(current.Make(0,1,message));current.Pump();current.Closed(snapshot.lease);
        CHECK(native.poseApplyCalls==0 && native.restoreCalls>0);
    }
    for(unsigned invalid=0;invalid<2;++invalid) {
        Room current(0,serial++);native.fallback=true;CHECK(current.Request(0,5));current.Pump();const auto snapshot=current.ActiveSnapshot(0);
        if(invalid)native.leases[snapshot.lease].live=false;else actors[5].alive=false;
        crew::EnemyControlNetworkFrame();current.Pump();current.Closed(snapshot.lease);
        CHECK(native.poseCaptureCalls==0 && native.restoreCalls>0 && PosePacketCount(current)==0);
    }
}
void NonFallbackDoesNotProducePose() {
    for(unsigned production=0;production<2;++production) {
        Room current(production,130+production);CHECK(current.Request(production ? 2 : 0,5));current.Pump();current.ActiveSnapshot(production ? 2 : 0);
        crew::EnemyControlNetworkFrame();now+=501;crew::EnemyControlNetworkFrame();current.Pump();
        CHECK(native.needsPoseCalls && native.poseCaptureCalls==0 && native.poseApplyCalls==0 && PosePacketCount(current)==0);
        CHECK(current.ForSource(production,actors[production ? 2 : 0].id)->phase==ep::Phase::active);
    }
}
unsigned ActionPacketCount(const Room& current,std::uint64_t sequence=0) {
    unsigned count=0;for(const auto& packet:current.history){ep::Message m;CHECK(ep::Decode(packet.bytes.data(),packet.bytes.size(),m));if(m.kind==ep::Kind::Action && (!sequence || sequence==m.sequence))++count;}return count;
}
unsigned AckSignals(const Room& current,unsigned from,std::uint64_t lease,std::uint64_t* applied=nullptr,std::uint64_t* endToken=nullptr) {
    unsigned signals=0;
    for(const auto& packet:current.history)if(packet.from==from) {
        ep::Message message;CHECK(ep::Decode(packet.bytes.data(),packet.bytes.size(),message));
        if(message.kind!=ep::Kind::ActionAckBatch)continue;
        CHECK(message.input[0]==2 && message.inputSize==8+ep::kAckEntryBytes*ep::AppliedAckCount(message));
        for(std::size_t i=0;i<ep::AppliedAckCount(message);++i) {
            const auto ack=ep::AppliedAckAt(message,i);if(ack.lease!=lease)continue;signals|=ack.signals;
            if(applied && (ack.signals&ep::ackApplied))*applied=ack.sequence;
            if(endToken && (ack.signals&ep::ackRestored))*endToken=ack.endToken;
        }
    }
    return signals;
}
bool SuccessfulAppliedAck(const Room& current,unsigned from,std::uint64_t lease) {
    for(const auto& packet:current.history)if(packet.from==from) {
        ep::Message message;CHECK(ep::Decode(packet.bytes.data(),packet.bytes.size(),message));
        for(std::size_t i=0;i<ep::AppliedAckCount(message);++i){const auto ack=ep::AppliedAckAt(message,i);
            if(ack.lease==lease && (ack.signals&ep::ackApplied) && (ack.success&ep::ackApplied))return true;}
    }
    return false;
}
void ProductionSignalBatchesRespectNativeBarriers() {
    for(bool failed:{false,true}) {
        Room current(1,230+failed);CHECK(current.Request(2,5));current.Pump();const auto snapshot=current.ActiveSnapshot(2);
        CHECK((AckSignals(current,1,snapshot.lease)&(ep::ackPrepared|ep::ackStarted|ep::ackActivated))==(ep::ackPrepared|ep::ackStarted|ep::ackActivated));
        if(failed){native.failApply=true;CHECK(callbacks.input(snapshot,{}));current.Pump();CHECK(native.applyCalls==1);}
        else {CHECK(callbacks.stop(snapshot));current.Pump();}
        current.Closed(snapshot.lease);
        std::uint64_t endToken=0;const auto signals=AckSignals(current,1,snapshot.lease,nullptr,&endToken);
        CHECK((signals&ep::ackRestored) && endToken==1);
        if(failed)CHECK((signals&(ep::ackDrained|ep::ackFault))==(ep::ackDrained|ep::ackFault));
        const auto end=current.LastPacket(ep::Kind::End,0,1);ep::Message ending;CHECK(ep::Decode(end.bytes.data(),end.bytes.size(),ending));CHECK(ending.sequence==endToken);
        std::uint64_t prior=0;
        for(const auto& packet:current.history)if(packet.from==1) {
            ep::Message message;CHECK(ep::Decode(packet.bytes.data(),packet.bytes.size(),message));
            CHECK(!ep::AckSignalFor(message.kind)); // these seven signals share the same bounded batch outbox
            if(message.kind==ep::Kind::ActionAckBatch){if(prior)CHECK(packet.sentAt-prior>=ep::kAppliedAckMs);prior=packet.sentAt;}
        }
        CHECK(prior);
    }
}
void HostNormalizesStaleFocusWithoutRevokingControl() {
    Room current(0,200);CHECK(current.PureRequest(1,2,5));current.Pump();const auto lease=current.ForSource(0,actors[2].id)->id;ec::Input input;input.absoluteAim=true;
    std::memcpy(input.fullFocusId,actors[6].id.data(),32);unsigned char bytes[en::kInputBytes]{};CHECK(en::EncodeInput(input,bytes,sizeof(bytes)));
    CHECK(current.Protocol(1).SubmitInput(lease,bytes,sizeof(bytes),now));current.Pump();
    CHECK(native.normalizeCalls==1 && !std::memcmp(current.LastInput(0,lease).fullFocusId,actors[6].id.data(),32));
    actors[6].alive=false;CHECK(current.Protocol(1).SubmitInput(lease,bytes,sizeof(bytes),now));current.Pump();
    for(unsigned node=0;node<3;++node){const auto accepted=current.LastInput(node,lease);CHECK(accepted.sequence==2 && current.Lease(node,lease)->phase==ep::Phase::active);for(auto b:accepted.fullFocusId)CHECK(!b);}
    CHECK(native.normalizeCalls==2 && !native.restoreCalls);
}
void HostExitDrainsNativeAndWireOutboxes() {
    Room current(0,201);native.hostActions=true;CHECK(current.Request(0,5));current.Pump();const auto snapshot=current.ActiveSnapshot(0);
    const auto first=PoseBytes(232,40),last=PoseBytes(232,41);native.acceptedActions.push_back(first);
    crew::EnemyControlNetworkFrame();CHECK(native.actionCaptureCalls==2 && native.acceptedActions.empty());
    native.acceptedActions.push_back(last);
    CHECK(callbacks.stop(snapshot));current.Pump();CHECK(current.Lease(0,snapshot.lease)->phase==ep::Phase::draining && !native.restoreCalls && native.leases[snapshot.lease].closing);
    CHECK(native.quiesceCalls && !native.abortCalls && native.acceptedActions.size()==1);
    current.Step();CHECK(native.acceptedActions.empty());
    CHECK(current.nodes[1].actions==std::vector<std::vector<unsigned char>>({first}) && current.nodes[2].actions==current.nodes[1].actions);
    current.Step();
    CHECK(current.nodes[1].actions==std::vector<std::vector<unsigned char>>({first,last}) && current.nodes[2].actions==current.nodes[1].actions);
    current.Step();current.Closed(snapshot.lease);CHECK(native.restoreCalls && !native.abortCalls);
}

void ProductionSdkBackpressureRetainsAcceptedResults() {
    Room current(0,221);native.hostActions=true;CHECK(current.Request(0,5));current.Pump();const auto snapshot=current.ActiveSnapshot(0);
    const auto bytes=PoseBytes(512,91);native.acceptedActions.push_back(bytes);native.flowBlocked=true;
    crew::EnemyControlNetworkFrame();current.Step();
    CHECK(native.flowCalls && !ActionPacketCount(current) && !native.restoreCalls && current.Lease(0,snapshot.lease)->phase==ep::Phase::active);
    CHECK(native.acceptedActions.empty() && !current.Protocol(0).ActionIdle(snapshot.lease));
    native.flowBlocked=false;current.Step();current.Step();
    CHECK(current.nodes[1].actions==std::vector<std::vector<unsigned char>>({bytes}) && current.nodes[2].actions==current.nodes[1].actions);
    CHECK(current.Protocol(0).ActionIdle(snapshot.lease) && !native.restoreCalls);
    CHECK(callbacks.stop(snapshot));current.Pump();current.Closed(snapshot.lease);
}

void HostAcceptedBatchesUseBackpressure() {
    Room current(0,190);native.hostActions=true;CHECK(current.Request(0,5));current.Pump();const auto snapshot=current.ActiveSnapshot(0);
    std::vector<std::vector<unsigned char>> expected;
    for(unsigned n=0;n<ep::kActionWindow+1;++n){expected.push_back(PoseBytes(232,30+n));native.acceptedActions.push_back(expected.back());}
    crew::EnemyControlNetworkFrame();CHECK(native.actionCaptureCalls==ep::kActionWindow && native.actionCapacity==ep::kMaxPoseBytes && native.acceptedActions.size()==1);
    CHECK(!ActionPacketCount(current)); // capture cannot send ahead of the next shared-frame quota
    for(unsigned n=0;n<8;++n){++now;crew::EnemyControlNetworkFrame();}
    CHECK(native.actionCaptureCalls==ep::kActionWindow && native.acceptedActions.size()==1 && !ActionPacketCount(current));
    now+=24;crew::EnemyControlNetworkFrame();
    CHECK(ActionPacketCount(current,1)==2 && ActionPacketCount(current,ep::kActionPeerFramePackets)==2 && !ActionPacketCount(current,ep::kActionPeerFramePackets+1));
    CHECK(native.acceptedActions.size()==1); // full native window survives the sender's per-peer quota
    current.Pump();CHECK(current.nodes[1].actions.size()==ep::kActionPeerFramePackets && current.nodes[2].actions==current.nodes[1].actions);
    current.Step();CHECK(native.actionCaptureCalls==ep::kActionWindow+2 && native.acceptedActions.empty());
    CHECK(current.nodes[1].actions.size()==2*ep::kActionPeerFramePackets && current.nodes[2].actions==current.nodes[1].actions);
    unsigned frames=0;
    while(current.nodes[1].actions.size()<ep::kActionWindow) {
        CHECK(++frames<=ep::kActionWindow/ep::kActionPeerFramePackets);
        const auto delivered=current.nodes[1].actions.size();current.Step();
        CHECK(current.nodes[1].actions.size()==delivered+ep::kActionPeerFramePackets && current.nodes[2].actions==current.nodes[1].actions);
    }
    CHECK(current.nodes[1].actions.size()==ep::kActionWindow);
    current.Step();CHECK(current.nodes[1].actions==expected && current.nodes[2].actions==expected);
    CHECK(!native.actionApplyCalls);CHECK(callbacks.stop(snapshot));current.Pump();current.Closed(snapshot.lease);
}
void GuestAcceptedActionsRequireActualNativeApplication() {
    Room current(1,191);native.hostActions=true;CHECK(current.Request(2,5));current.Pump();const auto snapshot=current.ActiveSnapshot(2);
    native.actionResult=ec::ActionResult::pending;const auto first=PoseBytes(512,32),second=PoseBytes(64,33);
    CHECK(current.Protocol(0).SubmitAction(snapshot.lease,first.data(),first.size(),now));CHECK(current.Protocol(0).SubmitAction(snapshot.lease,second.data(),second.size(),now));
    const auto envelopes=TakePackets(current,1,ep::Kind::Action);CHECK(envelopes.size()==2);
    for(std::size_t i=0;i<envelopes.size();++i) {
        ep::Message message;CHECK(ep::Decode(envelopes[i].bytes.data(),envelopes[i].bytes.size(),message));
        CHECK(message.capability==(1u<<16) && message.sequence==i+1 && message.inputSize==(i ? second.size() : first.size()));
        RejectNonCanonicalSizes(current,envelopes[i]);CHECK(!native.actionApplyCalls);
    }
    auto forged=envelopes[0];forged.from=2;current.Deliver(forged,kPuid[0]);current.Deliver(envelopes[0],kPuid[2]);
    CHECK(!native.actionApplyCalls && current.ForSource(1,actors[2].id)->phase==ep::Phase::active);
    for(const auto& envelope:envelopes)current.Deliver(envelope);
    current.Pump();CHECK(native.actionApplyCalls==1 && native.appliedActions.empty() && !current.Protocol(0).ActionIdle(snapshot.lease));
    ++now;crew::EnemyControlNetworkFrame();CHECK(native.actionApplyCalls==2 && native.appliedActions.empty());
    native.actionResult=ec::ActionResult::applied;++now;crew::EnemyControlNetworkFrame();current.Pump();
    CHECK(native.appliedActions==std::vector<std::vector<unsigned char>>({first,second}) && native.appliedActionSequences==std::vector<std::uint64_t>({1,2}));
    CHECK(!current.Protocol(0).ActionIdle(snapshot.lease)); // native apply is recorded before the next batched ACK
    current.Step();CHECK(current.Protocol(0).ActionIdle(snapshot.lease));
    std::uint64_t appliedSequence=0;CHECK((AckSignals(current,1,snapshot.lease,&appliedSequence)&ep::ackApplied) && appliedSequence==2);
    CHECK(current.nodes[2].actions==native.appliedActions && !native.actionCaptureCalls);
    const auto appliedCalls=native.actionApplyCalls;for(const auto& envelope:envelopes)current.Deliver(envelope);current.Pump();CHECK(native.actionApplyCalls==appliedCalls);
    ep::Message old;CHECK(ep::Decode(envelopes[0].bytes.data(),envelopes[0].bytes.size(),old));--old.epoch;
    current.Deliver(current.Make(0,1,old));CHECK(native.actionApplyCalls==appliedCalls);
    CHECK(callbacks.stop(snapshot));current.Pump();current.Closed(snapshot.lease);
}
void ActionsRevalidateNativeIdentityAndFailures() {
    for(unsigned kind=0;kind<3;++kind){Room current(1,192+kind);native.hostActions=true;CHECK(current.Request(2,5));current.Pump();const auto snapshot=current.ActiveSnapshot(2);const auto bytes=PoseBytes(232,34);
        CHECK(current.Protocol(0).SubmitAction(snapshot.lease,bytes.data(),bytes.size(),now));
        if(kind==0)native.wrongRequesterPuid=true;else if(kind==1)actors[5].registered=false;else native.actionResult=ec::ActionResult::failed;
        current.Pump();CHECK(native.appliedActions.empty());if(kind<2)CHECK(!native.actionApplyCalls);
        // Registry lookup loss cannot discard a previously host-accepted effect.
        // Closing uses the Core's original retained weak identity, then restores.
        current.Closed(snapshot.lease);CHECK(native.restoreCalls>0);
        if(kind<2){CHECK(native.actionApplyCalls<=1 && native.appliedActions.size()==native.actionApplyCalls);if(native.actionApplyCalls)CHECK(native.appliedActions==std::vector<std::vector<unsigned char>>({bytes}));}
        else CHECK(native.appliedActions.empty());}
    for(bool changedWeak:{false,true}) {
        Room current(1,240+changedWeak);native.hostActions=true;
        // This peer models the real host's native drain contract, so failed
        // application cannot be treated as permission to discard its outbox.
        CHECK(current.nodes[0].protocol.Configure({&current.nodes[0],&PureSend,&PureAuthenticate,&PureResolve,&PurePrepare,&PureBegin,
            &PureValidate,&PureApply,&PureRestore,&PureActivate,&PurePose,&PureAction,
            +[](void*,std::uint64_t,const ep::LeaseSpec&){return true;},
            +[](void*,std::uint64_t,const ep::LeaseSpec&){return true;},
            +[](void*,std::uint64_t,const ep::LeaseSpec&){return true;}}));
        CHECK(current.Request(2,5));current.Pump();const auto snapshot=current.ActiveSnapshot(2);
        const auto bytes=PoseBytes(232,42);CHECK(current.Protocol(0).SubmitAction(snapshot.lease,bytes.data(),bytes.size(),now));
        if(changedWeak)edf::Put<const void*>(actors[5].object,edf::kSelfCtrl,actors[6].control);else native.nativeCoreCanReplay=false;
        const auto deadline=now+ep::kTimeoutMs;current.Pump();CHECK(native.appliedActions.empty());
        while(now<=deadline){current.Step();CHECK(native.appliedActions.empty() && !SuccessfulAppliedAck(current,1,snapshot.lease));}
        CHECK(native.abortCalls>0);current.Closed(snapshot.lease);CHECK(native.restoreCalls>0 && native.appliedActions.empty());
        if(changedWeak)CHECK(!native.actionApplyCalls);else CHECK(native.actionApplyCalls>0);
    }
    for(bool oversize:{false,true}){Room current(0,196+oversize);native.hostActions=true;CHECK(current.Request(0,5));current.Pump();const auto snapshot=current.ActiveSnapshot(0);
        if(oversize)native.acceptedActions.push_back(PoseBytes(ep::kMaxPoseBytes+1,35));else native.failActionCapture=true;
        crew::EnemyControlNetworkFrame();current.Pump();current.Closed(snapshot.lease);CHECK(native.restoreCalls>0 && !ActionPacketCount(current));}
    Room current(0,199);CHECK(current.Request(0,5));current.Pump();crew::EnemyControlNetworkFrame();CHECK(!native.actionCaptureCalls && !native.actionApplyCalls && !ActionPacketCount(current));
}
void RetiredPoseSlotsDoNotRevokeNewBody() {
    for(unsigned production=0;production<2;++production) {
        Room current(production,180+production);native.fallback=true;
        CHECK(current.Request(production ? 2 : 0,5));current.Pump();const auto snapshot=current.ActiveSnapshot(production ? 2 : 0);
        for(unsigned i=0;i<ec::kMaxControllers;++i)crew::poseSamples[i]={10000+i,now,now,now};
        if(!production) {
            crew::EnemyControlNetworkFrame();current.Pump();
            CHECK(native.poseCaptureCalls==1 && current.nodes[1].poseApplies==1 && current.nodes[2].poseApplies==1);
        } else {
            const auto bytes=PoseBytes(232,20);CHECK(current.Protocol(0).SubmitPose(snapshot.lease,bytes.data(),bytes.size(),now));current.Pump();
            CHECK(native.poseApplyCalls==1 && native.appliedPose==bytes);
        }
        CHECK(current.ForSource(production,actors[production ? 2 : 0].id)->phase==ep::Phase::active);
        CHECK(!native.restoreCalls);CHECK(callbacks.stop(snapshot));current.Pump();current.Closed(snapshot.lease);
    }
}
void EpochRejectsOldPoseEnvelope() {
    Room current(1,140);native.fallback=true;CHECK(current.Request(2,5));current.Pump();const auto previous=current.ActiveSnapshot(2);
    crew::EnemyControlNetworkFrame();const auto bytes=PoseBytes(512,10);
    CHECK(current.Protocol(0).SubmitPose(previous.lease,bytes.data(),bytes.size(),now));const auto old=TakePosePackets(current,1);CHECK(old.size()==1);
    RejectNonCanonicalSizes(current,old[0]);current.Pump();CHECK(native.poseApplyCalls==0);
    for(unsigned i=0;i<3;++i)if(i!=current.bridge) {
        auto& node=current.nodes[i];node.protocol.Reset(now);
        auto fresh=std::make_unique<ep::Protocol>(ep::Hooks{&node,&PureSend,&PureAuthenticate,&PureResolve,&PurePrepare,&PureBegin,&PureValidate,&PureApply,&PureRestore,&PureActivate,&PurePose,&PureAction});
        node.protocol=std::move(*fresh);
        node.baselines.clear();node.inputs.clear();node.poses.clear();node.poseSequences.clear();node.context.epoch=current.epoch+1;
    }
    current.queue.clear();++current.epoch;++now;current.TickAll();current.Pump();now+=1001;current.TickAll();current.Pump();
    CHECK(current.Protocol(1).Ready() && !native.leases[previous.lease].captured);CHECK(current.Request(2,5));current.Pump();
    const auto next=current.ActiveSnapshot(2);CHECK(next.lease==previous.lease && next.profileHash==previous.profileHash && next.revision!=previous.revision);
    for(const auto& envelope:old)current.Deliver(envelope);CHECK(native.poseApplyCalls==0 && current.ForSource(1,actors[2].id)->phase==ep::Phase::active);
    crew::EnemyControlNetworkFrame();const auto fresh=PoseBytes(512,11);CHECK(current.Protocol(0).SubmitPose(next.lease,fresh.data(),fresh.size(),now));
    const auto envelopes=TakePosePackets(current,1);CHECK(envelopes.size()==1);RejectNonCanonicalSizes(current,envelopes[0]);CHECK(native.poseApplyCalls==0);
    current.Deliver(envelopes[0]);current.Pump();CHECK(native.poseApplyCalls==1 && native.appliedPose==fresh);
    CHECK(callbacks.stop(next));current.Pump();current.Closed(next.lease);
}

}
int main() {
    // No game session singleton exists while EDFModLoader loads the plugin.
    CHECK(crew::sessionQueries==0);
    CHECK(crew::InstallEnemyControlNetwork());
    CHECK(crew::sessionQueries==0);
    CHECK(callbacks.request && callbacks.input && callbacks.stop && callbacks.online);
    crew::EnemyControlNetworkStatus initial;
    CHECK(crew::ReadEnemyControlNetworkStatus(nullptr,&initial));
    CHECK(!initial.online && !initial.ready && initial.result==ep::Result::Unavailable);
    CHECK(crew::InstallEnemyControlNetwork());
    CHECK(crew::sessionQueries==0);
    StartProductionController(0,1);StartProductionController(1,2);
    GuestThroughProductionHost();AuthenticationAndPreparation();LocalCapacityProofGatesEnemyControl();CapacityProofLossRetainsNativeRestoreJournal();PerSourceRejectedStatus();
    SplitScreenAndSampling(0,40);SplitScreenAndSampling(1,41);
    FailureAndRestore(0,50);FailureAndRestore(1,60);
    ReadyContextCannotSkipRestore(0,80);ReadyContextCannotSkipRestore(1,81);
    PendingCancellationUsesCurrentUiBinding();EpochPendingSampleDoesNotCrossWorld();
    HostPoseSamplingAndAtomicDelivery();GuestPoseAuthenticationAndAtomicDelivery();PoseFailureAndLoss();
    NonFallbackDoesNotProducePose();EpochRejectsOldPoseEnvelope();PoseRevalidatesNativeActorBinding();RetiredPoseSlotsDoNotRevokeNewBody();
    ProductionSignalBatchesRespectNativeBarriers();HostNormalizesStaleFocusWithoutRevokingControl();HostExitDrainsNativeAndWireOutboxes();HostAcceptedBatchesUseBackpressure();ProductionSdkBackpressureRetainsAcceptedResults();GuestAcceptedActionsRequireActualNativeApplication();ActionsRevalidateNativeIdentityAndFailures();
    std::printf("enemy_control_net_fixture: %u checks passed (production host/guest + real three-peer codec bus)\n",checks);return 0;
}
