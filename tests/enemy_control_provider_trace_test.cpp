// Replays actual typed provider payloads through the production protocol and byte codec.
// The provider traces come from native ABI seams with source-derived update clocks;
// this verifies transport throughput and drain, not real-game skill acceptance.
#define main ProviderTraceUnusedBaseTests
#include "enemy_control_protocol_test.cpp"
#undef main
#include <fstream>
#include <queue>
#include <set>
#include <sstream>
#include <tuple>
#include <limits>
#include <functional>
#include "../src/enemy_control_net_input.h"
#include "../src/enemy_control_capacity.h"

namespace {
using Payload=std::vector<unsigned char>;
struct TraceTick {std::uint64_t tick=0;long long units=-1;std::vector<Payload> packets;};
struct TraceScenario {std::string name;std::vector<TraceTick> ticks;std::uint64_t end=0;long long units=-1;unsigned sourceHz=0;};
unsigned traceSkipped=0,traceScalarRuns=0,traceAggregateRuns=0;
std::size_t traceBackgroundBytes=0;
unsigned traceRttMs=250;
std::uint64_t TraceOneWayMs() noexcept {return traceRttMs/2;}
const std::array<std::uint8_t,crew::enemy_control_net::kInputBytes>& TraceInputBytes() {
    static const auto bytes=[] {std::array<std::uint8_t,crew::enemy_control_net::kInputBytes> value{};crew::enemy_control::Input neutral{};Check(crew::enemy_control_net::EncodeInput(neutral,value.data(),value.size()),"neutral source input uses the actual production semantic codec");return value;}();return bytes;
}
bool TraceValidate(void* user,std::uint64_t id,const LeaseSpec&,const InputMeta& meta,const void* bytes,std::size_t size) {auto& node=*static_cast<Node*>(user);++node.validates;crew::enemy_control::Input decoded{};return !node.rejectInput && crew::enemy_control_net::DecodeInput(bytes,size,&decoded) && size==TraceInputBytes().size() && !std::memcmp(bytes,TraceInputBytes().data(),size) && meta.sequence && meta.seed==InputSeed(node.context.epoch,id,meta.sequence);}
std::vector<AppliedAck> TraceAppliedAcks(const Message& message) {
    std::vector<AppliedAck> result;
    if(message.kind==Kind::ActionAck){AppliedAck ack;ack.lease=message.lease;ack.request=message.request;ack.sequence=message.sequence;ack.signals=ackApplied;ack.success=message.ok ? ackApplied : 0;result.push_back(ack);}
    else if(message.kind==Kind::ActionAckBatch)for(std::size_t i=0;i<AppliedAckCount(message);++i){const auto ack=AppliedAckAt(message,i);if(ack.signals&ackApplied)result.push_back(ack);}
    return result;
}
bool TraceContainsAppliedAck(const Message& message,std::uint64_t lease,std::uint64_t sequence) {for(const auto& ack:TraceAppliedAcks(message))if(ack.lease==lease && ack.sequence==sequence && (ack.success&ackApplied))return true;return false;}
struct SdkPendingLedger {
    using Ack=std::tuple<std::uint64_t,unsigned,unsigned,std::size_t>;
    static constexpr std::size_t kLimit=crew::kEnemyControlPendingBytes;
    std::vector<std::vector<std::size_t>> pending;std::priority_queue<Ack,std::vector<Ack>,std::greater<Ack>> acks;std::size_t peak=0,background=0;
    SdkPendingLedger(unsigned peers,std::size_t reserved):pending(peers,std::vector<std::size_t>(peers,reserved)),peak(reserved),background(reserved) {}
    void Expire(std::uint64_t now) {while(!acks.empty() && std::get<0>(acks.top())<=now){const auto ack=acks.top();acks.pop();auto& bytes=pending[std::get<1>(ack)][std::get<2>(ack)];Check(bytes>=background+std::get<3>(ack),"SDK transport ACK releases one tracked complete datagram exactly once");bytes-=std::get<3>(ack);}}
    bool Track(unsigned from,unsigned to,std::size_t wireBytes,std::uint64_t now,std::uint64_t rawSdkArrival) {Expire(now);const auto bytes=wireBytes+kActionSdkFramingBytes;auto& link=pending[from][to];link+=bytes;peak=std::max(peak,link);acks.emplace(rawSdkArrival+TraceOneWayMs(),from,to,bytes);if(link>=kLimit)std::fprintf(stderr,"SDK_PENDING_OVERFLOW from=%u to=%u tracked_bytes=%zu background_bytes=%zu datagram_bytes=%zu; actual extension admission requires pendingBytes<%zu\n",from,to,link,background,bytes,kLimit);return link<kLimit;}
};
unsigned Nibble(char c) {if(c>='0' && c<='9')return static_cast<unsigned>(c-'0');if(c>='a' && c<='f')return static_cast<unsigned>(c-'a'+10);Check(false,"compact payload is canonical lowercase hex");return 0;}
Payload Hex(const std::string& value) {
    Check(!value.empty() && !(value.size()%2) && value.size()/2<=kMaxPoseBytes,"actual typed trace fits the production action ABI");
    Payload result;result.reserve(value.size()/2);for(std::size_t i=0;i<value.size();i+=2)result.push_back(static_cast<unsigned char>((Nibble(value[i])<<4)|Nibble(value[i+1])));return result;
}
void CapacityAdmission() {
    EDF6CoopExtensionCapacity current{sizeof(current),EDF6COOP_EXTENSION_CAPACITY_VERSION,EDF6COOP_EXTENSION_MAX_PAYLOAD,static_cast<std::uint32_t>(kMaxPeers),crew::kEnemyControlInboxMessages,crew::kEnemyControlPendingBytes,crew::kEnemyControlPendingBytes,EDF6COOP_EXTENSION_CAPACITY_RELIABLE_ORDERED};
    Check(crew::ValidEnemyControlCapacity(current),"new SDK advertises the actual mandatory production capacities");
    auto old=current;old.inboxMessages=256;old.pendingBytes=old.congestedBacklogBytes=256*1024;Check(!crew::ValidEnemyControlCapacity(old),"old 256-inbox/256KiB SDK is explicitly refused for enemy control");
    EDF6CoopExtensionCapacity absent{};Check(!crew::ValidEnemyControlCapacity(absent),"SDK without a capacity export cannot admit native enemy control");
    Room oldRoom;oldRoom.nodes[2]->context.localCapability=0;oldRoom.Step(1100);oldRoom.Settle();
    for(const auto& node:oldRoom.nodes)Check(!node->protocol.Ready() && !node->begins,"one local SDK without capacity proof prevents every native Begin");Check(!oldRoom.Request(),"legacy capacity refusal has no local-only control fallback");
}
void EndLine(std::istringstream& in) {std::string extra;Check(!(in>>extra),"compact trace has no unknown trailing fields");}
std::vector<TraceScenario> ReadTrace(const char* path) {
    std::ifstream file(path);Check(file.is_open(),"open packed provider trace");std::string line;Check(static_cast<bool>(std::getline(file,line)) && line=="EDF6TRACE 1","packed trace version matches");
    std::vector<TraceScenario> result;TraceScenario* scenario=nullptr;TraceTick* tick=nullptr;bool ended=true;std::set<std::string> names;
    while(std::getline(file,line)) {
        if(line.empty())continue;std::istringstream in(line);std::string kind;in>>kind;
        if(kind=="SCENARIO") {Check(ended,"previous actual scenario has a terminal record");std::string name;Check(static_cast<bool>(in>>name) && names.insert(name).second,"actual scenario identity is unique");EndLine(in);result.push_back({});scenario=&result.back();scenario->name=name;tick=nullptr;ended=false;}
        else if(kind=="SKIP") {Check(ended,"metadata-only skip cannot interrupt an actual scenario");std::string name,reason;Check(static_cast<bool>(in>>name>>reason) && names.insert(name).second,"skipped provider metadata has an exact unique identity and reason");EndLine(in);++traceSkipped;std::fprintf(stderr,"TRACE_SKIPPED scenario=%s reason=%s; no throughput acceptance counted\n",name.c_str(),reason.c_str());}
        else if(kind=="CLOCK") {Check(scenario && !ended && scenario->ticks.empty() && !scenario->sourceHz,"explicit source clock precedes every actual timestamp");std::string clock;Check(static_cast<bool>(in>>clock>>scenario->sourceHz) && clock=="fixed-us" && (scenario->sourceHz==30 || scenario->sourceHz==60 || scenario->sourceHz==144),"fixed source timestamps retain their exported native clock");EndLine(in);}
        else if(kind=="TICK") {Check(scenario && !ended,"actual tick belongs to an open scenario");TraceTick next;Check(static_cast<bool>(in>>next.tick>>next.units) && next.units>=-1,"actual tick has a valid native clock and unit count");EndLine(in);Check(scenario->ticks.empty() || next.tick>scenario->ticks.back().tick,"native invocation ticks remain strictly ordered");scenario->ticks.push_back(next);tick=&scenario->ticks.back();}
        else if(kind=="PACKET") {Check(tick && !ended,"actual packet retains its native invocation tick");std::string bytes;Check(static_cast<bool>(in>>bytes),"actual packet has typed bytes");EndLine(in);tick->packets.push_back(Hex(bytes));}
        else if(kind=="END") {Check(scenario && !ended,"terminal belongs to an open actual scenario");Check(static_cast<bool>(in>>scenario->end>>scenario->units) && scenario->units>=-1,"actual terminal has a valid native clock and unit count");EndLine(in);Check(scenario->ticks.empty() || scenario->end>=scenario->ticks.back().tick,"actual terminal does not precede its native ticks");long long total=0;bool known=true;for(const auto& t:scenario->ticks){known=known && t.units>=0;if(t.units>=0)total+=t.units;}Check(!known || scenario->units<0 || total==scenario->units,"native successful-unit accounting matches terminal provenance");ended=true;tick=nullptr;}
        else Check(false,"packed trace record is supported");
    }
    Check(ended && !result.empty(),"packed trace contains complete actual scenarios");return result;
}
struct TraceBus;
std::map<Node*,TraceBus*> traceBuses;
struct Transit {Packet packet{};std::uint64_t due=0,serial=0;bool dropApplication=false;};
struct Later {bool operator()(const Transit& a,const Transit& b) const {return a.due>b.due || (a.due==b.due && a.serial>b.serial);}};
struct TraceBus {
    Room room;const TraceScenario& trace;unsigned hz=0,pollHz=0;
    std::priority_queue<Transit,std::vector<Transit>,Later> wire;
    static constexpr std::size_t kSdkInboxLimit=crew::kEnemyControlInboxMessages,kPollBudget=512;
    SdkPendingLedger ledger{3,traceBackgroundBytes};
    std::deque<Transit> inbox[3];std::size_t inboxPeak=0,pollBacklog=0,offeredPeak=0;unsigned inboxOverflows=0,overflowNode=0;
    std::uint64_t flowDue[3][3]{},delivered[3][3]{},serial=0;
    std::vector<const Payload*> expected;std::deque<const Payload*> uncaptured;
    std::size_t produced=0,captured=0,applied[3]{},ackPrefix[3]{},maxQueue=0,maxWindow=0,exitQueue=0,exitWindow=0;
    std::map<std::tuple<unsigned,unsigned,std::uint64_t,std::uint32_t>,unsigned> chunks;
    std::uint64_t actionChunks=0,actionRetries=0,exitAt=0,startAt=0,lastInput=0,lease=0;
    unsigned quiesces[3]{},aborts[3]{},delayedAcks=0;
    bool sdkDelayed=false,producerDone=false,exiting=false;
    TraceBus(const TraceScenario& value,unsigned cadence,unsigned pollCadence):trace(value),hz(cadence),pollHz(pollCadence) {for(const auto& t:trace.ticks)for(const auto& p:t.packets)expected.push_back(&p);Check(!expected.empty(),"throughput scenario contains actual accepted outcomes");}
    ~TraceBus(){for(const auto& n:room.nodes)traceBuses.erase(n.get());}
    std::size_t Window() const {return captured-std::min(ackPrefix[1],ackPrefix[2]);}
    void Metrics(){maxQueue=std::max(maxQueue,uncaptured.size());maxWindow=std::max(maxWindow,Window());}
    bool Closed() const {for(unsigned i=0;i<3;++i){const auto* l=room.Lease(i,lease);if(!l || l->phase!=Phase::closed)return false;}return true;}
    void Tick(bool sharedFrame=false) {for(auto& n:room.nodes)n->protocol.Tick(n->context,room.now);Pump();if(sharedFrame)for(auto& n:room.nodes)n->protocol.TransportFrame(room.now);}
    void Pump() {
        ledger.Expire(room.now);
        // SDK delivery is independent of the game thread. Every packet due since
        // the preceding shared frame must enter the bounded inbox before polling.
        while(!wire.empty() && wire.top().due<=room.now) {
            const auto t=wire.top();wire.pop();const auto& p=t.packet;
            Check(t.serial>delivered[p.from][p.to],"SDK reliable ordered delivery never lets a later packet overtake the delayed packet");delivered[p.from][p.to]=t.serial;
            const auto offered=inbox[p.to].size()+1;offeredPeak=std::max(offeredPeak,offered);
            if(inbox[p.to].size()==kSdkInboxLimit){++inboxOverflows;overflowNode=p.to;continue;}
            inbox[p.to].push_back(t);inboxPeak=std::max(inboxPeak,inbox[p.to].size());
        }
        // AF's actual production support poll consumes at most 512 envelopes per frame.
        // ACKs are generated by actual protocol application here, never at SDK arrival.
        for(unsigned node=0;node<3;++node) {
            for(std::size_t polled=0;polled<kPollBudget && !inbox[node].empty();++polled) {
                const auto t=inbox[node].front();inbox[node].pop_front();const auto& p=t.packet;
                Message m;Check(Decode(p.bytes.data(),p.bytes.size(),m),"polled production wire packet decodes");Check(!t.dropApplication,"reliable SDK recovery never permanently discards an application envelope");room.Deliver(p);
                if(p.to==0)for(const auto& ack:TraceAppliedAcks(m)){Check((ack.success&ackApplied) && ack.lease==lease,"only actual native application for this exact body is acknowledged");Check(ack.sequence<=applied[p.from],"cumulative ACK never exceeds that guest's actually applied sequence");ackPrefix[p.from]=std::max(ackPrefix[p.from],static_cast<std::size_t>(ack.sequence));}
                Metrics();
            }
            pollBacklog=std::max(pollBacklog,inbox[node].size());
        }
        if(inboxOverflows)std::fprintf(stderr,"SDK_INBOX_OVERFLOW scenario=%s native_hz=%u poll_hz=%u node=%u produced=%zu captured=%zu applied=%zu uncaptured=%zu window=%zu inbox_peak=%zu offered_peak=%zu poll_backlog=%zu overflow_packets=%u elapsed_ms=%llu\n",trace.name.c_str(),hz,pollHz,overflowNode,produced,captured,applied[overflowNode],uncaptured.size(),Window(),inboxPeak,offeredPeak,pollBacklog,inboxOverflows,static_cast<unsigned long long>(room.now-startAt));
        Check(!inboxOverflows,"actual Coop SDK inbox capacity must not invalidate the controlled session");
    }
    void Capture() {while(!uncaptured.empty() && room.nodes[0]->protocol.CanCaptureAction(lease)) {const auto* packet=uncaptured.front();Check(room.nodes[0]->protocol.SubmitAction(lease,packet->data(),packet->size(),room.now),"production protocol accepts the next actual captured outcome");uncaptured.pop_front();++captured;Metrics();}}
};
TraceBus& Bus(void* user) {const auto it=traceBuses.find(static_cast<Node*>(user));Check(it!=traceBuses.end(),"timed backend belongs to an exact live fixture");return *it->second;}
bool TraceSend(void* user,std::uint32_t peer,const void* bytes,std::size_t size) {
    auto& b=Bus(user);auto* node=static_cast<Node*>(user);const unsigned to=b.room.Global(node->id,peer);Check(to<3,"timed SDK targets an exact authenticated peer");Message m;Check(Decode(bytes,size,m) && size==EncodedSize(m),"every outgoing production envelope has its exact canonical variable wire length");Packet p;p.from=node->id;p.to=to;p.bytes.resize(size);std::memcpy(p.bytes.data(),bytes,size);std::vector<std::uint8_t> canonical(EncodedSize(m));Check(Encode(m,canonical.data(),canonical.size()) && canonical==p.bytes,"production encode/decode preserves every wire byte");
    if(m.kind==Kind::Action){++b.actionChunks;const auto key=std::make_tuple(p.from,p.to,m.sequence,m.capability);if(b.chunks[key]++)++b.actionRetries;}
    // ReliableOrdered recovers this very SDK Data packet after one loss. It is
    // never silently discarded after accepted SDK delivery to the application.
    const bool recoverAck=p.from==2 && TraceContainsAppliedAck(m,b.lease,b.expected.size()) && !b.delayedAcks;if(recoverAck)++b.delayedAcks;
    auto rawSdkArrival=b.room.now+TraceOneWayMs();if(recoverAck)rawSdkArrival+=100;
    if(m.kind==Kind::Action && p.from==0 && p.to==2 && !b.sdkDelayed){rawSdkArrival+=100;b.sdkDelayed=true;}
    Check(b.ledger.Track(p.from,p.to,size,b.room.now,rawSdkArrival),"every AF control and action fits actual DirectNet pendingBytes admission");
    const auto due=std::max(rawSdkArrival,b.flowDue[p.from][p.to]);b.flowDue[p.from][p.to]=due;b.wire.push({p,due,++b.serial,false});return true;
}
ActionResult TraceApply(void* user,std::uint64_t id,const LeaseSpec& spec,std::uint64_t sequence,const void* bytes,std::size_t size) {
    auto& b=Bus(user);const auto* n=static_cast<Node*>(user);Check(n->id!=0,"accepted host factory outcome is never replayed on the authority");const auto* l=b.room.Lease(n->id,id);Check(id==b.lease && l && SameSpec(spec,l->spec) && (l->phase==Phase::active || l->phase==Phase::draining),"typed outcome stays bound to its exact active or draining native lease");
    auto& applied=b.applied[n->id];Check(sequence==applied+1 && applied<b.expected.size(),"each guest applies every actual typed outcome exactly once in original order");const auto& expected=*b.expected[applied];Check(size==expected.size() && !std::memcmp(bytes,expected.data(),size),"every guest receives byte-identical actual typed provider payload");++applied;return ActionResult::applied;
}
bool TraceQuiesce(void* user,std::uint64_t,const LeaseSpec&) {auto& b=Bus(user);++b.quiesces[static_cast<Node*>(user)->id];return true;}
bool TraceEmpty(void* user,std::uint64_t,const LeaseSpec&) {const auto& b=Bus(user);return static_cast<Node*>(user)->id!=0 || (b.producerDone && b.uncaptured.empty());}
bool TraceAbort(void* user,std::uint64_t,const LeaseSpec&) {auto& b=Bus(user);++b.aborts[static_cast<Node*>(user)->id];return true;}
void RunTrace(const TraceScenario& trace,unsigned hz,unsigned pollHz) {
    ++traceScalarRuns;TraceBus b(trace,hz,pollHz);for(const auto& n:b.room.nodes){traceBuses[n.get()]=&b;auto h=Backend(n.get());h.ValidateInput=&TraceValidate;h.Send=&TraceSend;h.ApplyAction=&TraceApply;h.QuiesceActions=&TraceQuiesce;h.ActionsEmpty=&TraceEmpty;h.AbortActions=&TraceAbort;Check(n->protocol.Configure(h) && n->protocol.SetSendPacing(true),"install production-paced timed actual-provider backend before acquisition");}
    Check(b.room.Request(),"authenticated source requests actual trace control");const auto acquireAt=b.room.now;
    for(std::uint64_t acquireFrame=0;;++acquireFrame){b.room.now=acquireAt+(acquireFrame*1000+pollHz-1)/pollHz;b.Tick(true);bool active=true;for(const auto& n:b.room.nodes)active=active && n->protocol.ActiveCount()==1;if(active)break;Check(b.room.now-acquireAt<5000,"all-peer frame-paced acquisition completes within the protocol deadline");}
    b.lease=b.room.Active();b.startAt=b.room.now;std::size_t nextTick=0;std::uint64_t frame=0;
    for(;;++frame) {
        b.room.now=b.startAt+(frame*1000+pollHz-1)/pollHz;
        while(nextTick<trace.ticks.size() && (trace.sourceHz ? (trace.ticks[nextTick].tick+999)/1000 : (trace.ticks[nextTick].tick*1000+hz-1)/hz)<=b.room.now-b.startAt) {for(const auto& packet:trace.ticks[nextTick].packets){b.uncaptured.push_back(&packet);++b.produced;}++nextTick;}
        b.Metrics();
        if(!b.exiting && b.produced==b.expected.size()) {
            b.producerDone=true;b.exitQueue=b.uncaptured.size();b.exitWindow=b.Window();Check(b.exitQueue+b.exitWindow>0,"normal exit starts while actual producer backlog or in-flight applied ACKs remain");b.exitAt=b.room.now;b.exiting=true;Check(b.room.nodes[1]->protocol.Exit(b.lease,b.room.now),"owner requests normal exit before final accepted outcomes drain");
        }
        if(!b.exiting && (!b.lastInput || b.room.now-b.lastInput>=33)){Check(b.room.nodes[1]->protocol.SubmitInput(b.lease,TraceInputBytes().data(),TraceInputBytes().size(),b.room.now),"canonical owner heartbeat keeps the real trace acquisition alive");b.lastInput=b.room.now;}
        b.Tick(true);b.Capture();b.Metrics();
        if(b.exiting && b.room.now-b.exitAt>5000){const auto* host=b.room.Lease(0,b.lease);const auto* guest1=b.room.Lease(1,b.lease);const auto* guest2=b.room.Lease(2,b.lease);std::fprintf(stderr,"SCALAR_TRACE_RED scenario=%s native_hz=%u poll_hz=%u rtt_ms=%u produced=%zu captured=%zu applied1=%zu applied2=%zu ack1=%zu ack2=%zu uncaptured=%zu max_uncaptured=%zu window=%zu max_window=%zu close_ms=%llu host_phase=%u host_result=%u guest1_phase=%u guest1_result=%u guest2_phase=%u guest2_result=%u sdk_inbox_peak=%zu sdk_pending_peak_bytes=%zu\n",trace.name.c_str(),hz,pollHz,traceRttMs,b.produced,b.captured,b.applied[1],b.applied[2],b.ackPrefix[1],b.ackPrefix[2],b.uncaptured.size(),b.maxQueue,b.Window(),b.maxWindow,static_cast<unsigned long long>(b.room.now-b.exitAt),host ? static_cast<unsigned>(host->phase) : 99,host ? static_cast<unsigned>(host->result) : 99,guest1 ? static_cast<unsigned>(guest1->phase) : 99,guest1 ? static_cast<unsigned>(guest1->result) : 99,guest2 ? static_cast<unsigned>(guest2->phase) : 99,guest2 ? static_cast<unsigned>(guest2->result) : 99,b.inboxPeak,b.ledger.peak);}
        Check(!b.exiting || b.room.now-b.exitAt<=5000,"normal drain completes within five seconds without fault retirement, including the final closed frame");
        if(b.Closed())break;
        Check(frame<1000000,"actual native invocation replay is bounded");
    }
    b.room.Closed(b.lease,Result::Complete);Check(b.produced==b.expected.size() && b.captured==b.expected.size() && b.uncaptured.empty(),"every actual producer outcome is captured before normal completion");
    for(unsigned i=1;i<3;++i)Check(b.applied[i]==b.expected.size() && b.ackPrefix[i]==b.expected.size(),"all actual outcomes have actual applied ACKs from both guests before Complete");
    for(unsigned i=0;i<3;++i)Check(b.quiesces[i] && !b.aborts[i],"normal quiesce and Complete never invokes Abort");Check(b.sdkDelayed && b.delayedAcks==1 && !b.actionRetries,"both SDK packets recover reliably without any application action retransmission");Check(b.maxWindow<=kActionWindow,"actual in-flight ledger respects the production capture window");
    std::printf("%s,%u,%u,%lld,%zu,%zu,%zu,%zu,%zu,%zu,%llu,%llu,%llu,%u,%zu,%zu,%zu,%zu,%u\n",trace.name.c_str(),hz,pollHz,trace.units,b.produced,b.applied[1],b.maxQueue,b.maxWindow,b.exitQueue,b.exitWindow,static_cast<unsigned long long>(b.room.now-b.exitAt),static_cast<unsigned long long>(b.actionChunks),static_cast<unsigned long long>(b.actionRetries),b.delayedAcks,b.inboxPeak,b.pollBacklog,b.ledger.peak,traceBackgroundBytes,traceRttMs);
}
}
// Eight independent bodies, each replaying one real dual-beam activation.
// This is room aggregate load, not concurrent abilities invented for one body.
#include "../src/enemy_control_capacity.h"
#include "../src/enemy_control_pose_codec.h"
namespace {
struct AggregateTraceBus;
std::map<Node*,AggregateTraceBus*> aggregateTraceBuses;
struct AggregateTraceBus {
    Room room;const TraceScenario& trace;SdkPendingLedger ledger;unsigned peers=0,bodies=0,nativeHz=0,pollHz=0;
    static constexpr std::size_t kSdkInboxLimit=crew::kEnemyControlInboxMessages,kPollBudget=512;
    std::priority_queue<Transit,std::vector<Transit>,Later> wire;
    std::vector<std::deque<Transit>> inbox;
    std::vector<std::vector<std::uint64_t>> flowDue,delivered;
    std::vector<const Payload*> expected;
    std::vector<std::deque<const Payload*>> uncaptured;
    std::vector<NativeId> sources;
    std::vector<unsigned> owners;
    std::vector<std::uint64_t> leases;
    std::map<std::uint64_t,unsigned> leaseBody;
    std::vector<std::size_t> captured,capturedAtExit;
    std::vector<std::uint64_t> posesSent;
    std::vector<std::vector<std::uint64_t>> posesApplied;
    std::vector<std::vector<std::size_t>> applied,ackPrefix;
    std::vector<std::vector<unsigned>> quiesces,aborts;
    std::map<std::tuple<unsigned,unsigned,std::uint64_t,std::uint64_t,std::uint32_t>,unsigned> chunks;
    std::size_t produced=0,totalCaptured=0,maxQueue=0,maxWindow=0,maxBodyWindow=0,exitQueue=0,exitWindow=0;
    std::size_t inboxPeak=0,offeredPeak=0,pollBacklog=0;
    std::uint64_t serial=0,actionChunks=0,actionRetries=0,abortPackets=0,startAt=0,exitAt=0,lastInput=0,lastPose=0,pauseBegin=0,pauseEnd=0;
    std::size_t pausedFrames=0,pausedArrivals=0,pausedInboxPeak=0;
    bool mixedPose=false,stallReceiver=false,faultExit=false;
    unsigned inboxOverflows=0,overflowNode=0,delayedAcks=0,captureCursor=0;
    bool sdkDelayed=false,faultReported=false,producerDone=false,exiting=false;
    AggregateTraceBus(const TraceScenario& value,unsigned count,unsigned bodyCount,unsigned cadence,unsigned pollCadence,bool mixed,bool stall,bool fault)
        :room(count),trace(value),ledger(count,traceBackgroundBytes),peers(count),bodies(bodyCount),nativeHz(cadence),pollHz(pollCadence),
         inbox(count),flowDue(count,std::vector<std::uint64_t>(count)),delivered(count,std::vector<std::uint64_t>(count)),
         uncaptured(bodyCount),sources(bodyCount),owners(bodyCount),leases(bodyCount),captured(bodyCount),capturedAtExit(bodyCount),posesSent(bodyCount),posesApplied(count,std::vector<std::uint64_t>(bodyCount)),
         applied(count,std::vector<std::size_t>(bodyCount)),ackPrefix(count,std::vector<std::size_t>(bodyCount)),
         quiesces(count,std::vector<unsigned>(bodyCount)),aborts(count,std::vector<unsigned>(bodyCount)) {
        mixedPose=mixed;stallReceiver=stall;faultExit=fault;
        Check(SdkPendingLedger::kLimit==crew::kEnemyControlPendingBytes,"aggregate SDK pending capacity follows the production required capability");
        Check(count>=3 && count<=kMaxPeers+1 && bodyCount && bodyCount<=kMaxLeases && cadence && pollCadence,"aggregate dimensions fit actual production room and lease bounds");
        Check(trace.name=="beam_double" && !trace.sourceHz && trace.units==480 && trace.end==421,"aggregate uses one complete actual dual-beam invocation with its original charge and 480 units");
        for(const auto& tick:trace.ticks)for(const auto& packet:tick.packets)expected.push_back(&packet);
        Check(expected.size()==480,"one body contributes exactly 480 actual typed payloads, without repeated cycles");
        for(unsigned body=0;body<bodies;++body){sources[body]=Id(40+body);owners[body]=body%peers;}
    }
    ~AggregateTraceBus(){for(const auto& node:room.nodes)aggregateTraceBuses.erase(node.get());}
    unsigned Body(std::uint64_t lease) const {const auto found=leaseBody.find(lease);Check(found!=leaseBody.end(),"aggregate callback names an exact acquired body");return found->second;}
    std::size_t Queue() const {std::size_t total=0;for(const auto& queue:uncaptured)total+=queue.size();return total;}
    std::size_t BodyWindow(unsigned body) const {std::size_t prefix=captured[body];for(unsigned node=1;node<peers;++node)prefix=std::min(prefix,ackPrefix[node][body]);return captured[body]-prefix;}
    std::size_t Window() const {std::size_t total=0;for(unsigned body=0;body<bodies;++body)total+=BodyWindow(body);return total;}
    std::size_t AppliedFloor() const {std::size_t minimum=expected.size()*bodies;for(unsigned node=1;node<peers;++node){std::size_t total=0;for(auto value:applied[node])total+=value;minimum=std::min(minimum,total);}return minimum;}
    bool Paused(unsigned node) const {return node==2 && pauseBegin && room.now>=pauseBegin && room.now<pauseEnd;}
    const char* Mode() const {return faultExit ? (mixedPose ? "fault-mixed-pose-pressure-upper-bound" : "fault") : (mixedPose ? "mixed-pose-pressure-upper-bound" : "normal");}
    std::uint64_t TotalPoses() const {std::uint64_t total=0;for(auto count:posesSent)total+=count;return total;}
    std::uint64_t PoseAppliedFloor() const {auto minimum=TotalPoses();for(unsigned node=1;node<peers;++node){std::uint64_t total=0;for(auto count:posesApplied[node])total+=count;minimum=std::min(minimum,total);}return minimum;}
    void Metrics(){maxQueue=std::max(maxQueue,Queue());maxWindow=std::max(maxWindow,Window());for(unsigned body=0;body<bodies;++body)maxBodyWindow=std::max(maxBodyWindow,BodyWindow(body));}
    void Report(const char* state) const {
        std::fprintf(stderr,"AGGREGATE_TRACE state=%s scenario=%s mode=%s processes=%u remote=%u bodies=%u source_layout=%s native_hz=%u poll_hz=%u actual_packets_per_body=%zu produced=%zu captured=%zu applied_floor=%zu uncaptured=%zu window=%zu max_queue=%zu max_window=%zu max_body_window=%zu exit_queue=%zu exit_window=%zu close_ms=%llu wire_action_chunks=%llu retried_chunks=%llu wire_abort_packets=%llu delayed_applied_acks=%u sdk_inbox_peak=%zu offered_peak=%zu poll_backlog_peak=%zu sdk_pending_peak_bytes=%zu sdk_background_bytes=%zu overflow_packets=%u overflow_node=%u elapsed_ms=%llu pose_packets=%llu pose_applied_floor=%llu pause_ms=%llu paused_frames=%zu paused_arrivals=%zu paused_inbox_peak=%zu sdk_inbox_capacity=%zu sdk_pending_capacity=%zu rtt_ms=%u\n",state,trace.name.c_str(),Mode(),peers,peers-1,bodies,peers>=bodies ? "distinct-source-processes" : "split-aggregate-upper-bound",nativeHz,pollHz,expected.size(),produced,totalCaptured,AppliedFloor(),Queue(),Window(),maxQueue,maxWindow,maxBodyWindow,exitQueue,exitWindow,static_cast<unsigned long long>(exiting ? room.now-exitAt : 0),static_cast<unsigned long long>(actionChunks),static_cast<unsigned long long>(actionRetries),static_cast<unsigned long long>(abortPackets),delayedAcks,inboxPeak,offeredPeak,pollBacklog,ledger.peak,traceBackgroundBytes,inboxOverflows,overflowNode,static_cast<unsigned long long>(room.now-startAt),static_cast<unsigned long long>(TotalPoses()),static_cast<unsigned long long>(PoseAppliedFloor()),static_cast<unsigned long long>(pauseEnd-pauseBegin),pausedFrames,pausedArrivals,pausedInboxPeak,kSdkInboxLimit,SdkPendingLedger::kLimit,traceRttMs);
    }
    void AcquisitionReport(const char* reason) const {
        std::fprintf(stderr,"AGGREGATE_ACQUISITION reason=%s processes=%u bodies=%u now=%llu\n",reason,peers,bodies,static_cast<unsigned long long>(room.now));
        for(const auto& node:room.nodes){std::fprintf(stderr,"AGGREGATE_NODE node=%u ready=%u active=%u result=%u\n",node->id,node->protocol.Ready() ? 1u : 0u,static_cast<unsigned>(node->protocol.ActiveCount()),static_cast<unsigned>(node->protocol.LastResult()));for(std::size_t slot=0;slot<kMaxLeases;++slot){const auto* value=node->protocol.LeaseAt(slot);if(value->id)std::fprintf(stderr,"AGGREGATE_LEASE node=%u lease=%llu source=%u target=%u phase=%u result=%u\n",node->id,static_cast<unsigned long long>(value->id),value->spec.requesterPlayer[0],value->spec.target[0],static_cast<unsigned>(value->phase),static_cast<unsigned>(value->result));}}
        for(unsigned body=0;body<bodies;++body){const auto* request=room.nodes[owners[body]]->protocol.RequestFor(sources[body]);if(request)std::fprintf(stderr,"AGGREGATE_REQUEST body=%u owner=%u request=%llu lease=%llu result=%u\n",body,owners[body],static_cast<unsigned long long>(request->request),static_cast<unsigned long long>(request->lease),static_cast<unsigned>(request->result));}
    }
    bool Closed() const {for(unsigned node=0;node<peers;++node)for(auto lease:leases){const auto* value=room.Lease(node,lease);if(!value || value->phase!=Phase::closed)return false;}return true;}
    void Tick(){for(auto& node:room.nodes){if(Paused(node->id)){++pausedFrames;continue;}node->protocol.Tick(node->context,room.now);}Pump();for(auto& node:room.nodes)if(!Paused(node->id))node->protocol.TransportFrame(room.now);}
    void Pump(){
        ledger.Expire(room.now);
        // All SDK arrivals due in this frame enter one process-wide bounded inbox
        // before any of that process's sole 512-envelope shared-frame poll.
        std::vector<std::size_t> offered(peers);for(unsigned node=0;node<peers;++node)offered[node]=inbox[node].size();
        while(!wire.empty() && wire.top().due<=room.now){const auto transit=wire.top();wire.pop();const auto& packet=transit.packet;
            Check(transit.serial>delivered[packet.from][packet.to],"aggregate reliable ordered flow cannot overtake an earlier delayed envelope");delivered[packet.from][packet.to]=transit.serial;
            offeredPeak=std::max(offeredPeak,++offered[packet.to]);
            if(inbox[packet.to].size()==kSdkInboxLimit){++inboxOverflows;overflowNode=packet.to;continue;}
            inbox[packet.to].push_back(transit);inboxPeak=std::max(inboxPeak,inbox[packet.to].size());if(Paused(packet.to)){++pausedArrivals;pausedInboxPeak=std::max(pausedInboxPeak,inbox[packet.to].size());}
        }
        if(inboxOverflows){Metrics();Report("RED_SDK_INBOX_OVERFLOW");Check(false,"aggregate load must fit the production advertised SDK inbox capacity");}
        for(unsigned node=0;node<peers;++node){
            if(Paused(node)){pollBacklog=std::max(pollBacklog,inbox[node].size());continue;}
            for(std::size_t polled=0;polled<kPollBudget && !inbox[node].empty();++polled){const auto transit=inbox[node].front();inbox[node].pop_front();const auto& packet=transit.packet;Message message;
                Check(Decode(packet.bytes.data(),packet.bytes.size(),message),"aggregate polled production envelope decodes");Check(!transit.dropApplication,"aggregate reliable SDK recovery never permanently discards an application envelope");room.Deliver(packet);
                if(packet.to==0)for(const auto& ack:TraceAppliedAcks(message)){Check((ack.success&ackApplied)!=0,"aggregate ACK follows actual typed native application");const auto body=Body(ack.lease);Check(ack.sequence<=applied[packet.from][body],"aggregate cumulative ACK cannot exceed that guest body's actual native application");auto& prefix=ackPrefix[packet.from][body];prefix=std::max(prefix,static_cast<std::size_t>(ack.sequence));}
                Metrics();
            }
            pollBacklog=std::max(pollBacklog,inbox[node].size());
        }
    }
    void Capture(){
        // One payload per body per round. Exhaust every currently available
        // production capture slot, then retain the next body's turn next frame.
        bool progress=true;while(progress){progress=false;for(unsigned step=0;step<bodies;++step){const auto body=(captureCursor+step)%bodies;
            if(uncaptured[body].empty() || !room.nodes[0]->protocol.CanCaptureAction(leases[body]))continue;
            const auto* packet=uncaptured[body].front();Check(room.nodes[0]->protocol.SubmitAction(leases[body],packet->data(),packet->size(),room.now),"aggregate round-robin submits actual accepted provider bytes through production capture");
            uncaptured[body].pop_front();++captured[body];++totalCaptured;progress=true;Metrics();
        }captureCursor=(captureCursor+1)%bodies;}
    }
};
AggregateTraceBus& AggregateBus(void* user){const auto found=aggregateTraceBuses.find(static_cast<Node*>(user));Check(found!=aggregateTraceBuses.end(),"aggregate backend belongs to its exact live room");return *found->second;}
bool AggregateAuth(void* user,const char* puid,const NativeId& source){const auto& bus=AggregateBus(user);for(unsigned body=0;body<bus.bodies;++body)if(bus.sources[body]==source)return bus.room.nodes[bus.owners[body]]->puid==puid;return false;}
bool AggregateSend(void* user,std::uint32_t peer,const void* bytes,std::size_t size){auto& bus=AggregateBus(user);const auto* node=static_cast<Node*>(user);const auto to=bus.room.Global(node->id,peer);Check(to<bus.peers,"aggregate SDK target is an exact room member");
    Message message;Check(Decode(bytes,size,message) && size==EncodedSize(message),"aggregate production envelope has its exact canonical variable wire length");
    if(node->id==0 && !bus.faultReported && (message.kind==Kind::Abort || message.kind==Kind::End || message.kind==Kind::Drain) && message.result!=Result::Complete && message.result!=Result::Pending && message.result!=Result::Idle && message.result!=Result::Active){
        bus.faultReported=true;const auto body=bus.Body(message.lease);std::size_t applied=bus.expected.size(),acked=bus.expected.size();for(unsigned guest=1;guest<bus.peers;++guest){applied=std::min(applied,bus.applied[guest][body]);acked=std::min(acked,bus.ackPrefix[guest][body]);}
        std::fprintf(stderr,"AGGREGATE_FIRST_FAULT processes=%u sdk_background_bytes=%zu kind=%u result=%u lease=%llu body=%u elapsed_ms=%llu after_exit_ms=%llu body_captured=%zu body_applied_floor=%zu body_ack_floor=%zu body_uncaptured=%zu body_window=%zu total_captured=%zu total_uncaptured=%zu total_window=%zu\n",bus.peers,traceBackgroundBytes,static_cast<unsigned>(message.kind),static_cast<unsigned>(message.result),static_cast<unsigned long long>(message.lease),body,static_cast<unsigned long long>(bus.room.now-bus.startAt),static_cast<unsigned long long>(bus.exiting ? bus.room.now-bus.exitAt : 0),bus.captured[body],applied,acked,bus.uncaptured[body].size(),bus.BodyWindow(body),bus.totalCaptured,bus.Queue(),bus.Window());
    }
    Packet packet;packet.from=node->id;packet.to=to;packet.bytes.resize(size);std::memcpy(packet.bytes.data(),bytes,size);std::vector<std::uint8_t> canonical(EncodedSize(message));Check(Encode(message,canonical.data(),canonical.size()) && canonical==packet.bytes,"aggregate production wire roundtrip preserves every byte");
    if(message.kind==Kind::Abort)++bus.abortPackets;
    if(message.kind==Kind::Action){++bus.actionChunks;const auto key=std::make_tuple(packet.from,packet.to,message.lease,message.sequence,message.capability);if(bus.chunks[key]++)++bus.actionRetries;}
    auto rawDue=bus.room.now+TraceOneWayMs();if(message.kind==Kind::Action && packet.from==0 && packet.to==2 && !bus.sdkDelayed){rawDue+=100;bus.sdkDelayed=true;if(bus.stallReceiver){bus.pauseBegin=rawDue;bus.pauseEnd=rawDue+150;}}
    if(packet.from==2 && !bus.delayedAcks)for(auto lease:bus.leases)if(TraceContainsAppliedAck(message,lease,bus.expected.size())){rawDue+=100;++bus.delayedAcks;break;}
    if(!bus.ledger.Track(packet.from,packet.to,size,bus.room.now,rawDue)){bus.Metrics();bus.Report("RED_SDK_PENDING_OVERFLOW");Check(false,"all aggregate AF envelopes must fit the actual reliable SDK pending-byte capacity");}
    const auto orderedDue=std::max(rawDue,bus.flowDue[packet.from][packet.to]);bus.flowDue[packet.from][packet.to]=orderedDue;bus.wire.push({packet,orderedDue,++bus.serial,false});return true;
}
std::array<unsigned char,crew::enemy_native::pose::kWireBytes> AggregatePoseBytes(unsigned body,std::uint64_t sequence){
    crew::enemy_native::pose::State state{};state.kind=crew::enemy_native::pose::Kind::deiroi;state.profile=static_cast<std::uint32_t>(kProfile);state.position[0]=static_cast<float>(body);state.position[1]=static_cast<float>(sequence);state.rotation[3]=1;
    std::array<unsigned char,crew::enemy_native::pose::kWireBytes> bytes{};Check(crew::enemy_native::pose::Encode(state,bytes.data(),bytes.size()),"mixed pressure uses the real production pose codec and a canonical legal state");return bytes;
}
bool AggregatePoseApply(void* user,std::uint64_t lease,const LeaseSpec& spec,std::uint64_t sequence,const void* bytes,std::size_t size){
    auto& bus=AggregateBus(user);const auto* node=static_cast<Node*>(user);const auto body=bus.Body(lease);Check(bus.mixedPose && node->id!=0,"only mixed protocol-pressure guests apply synthetic legal pose traffic");const auto* current=bus.room.Lease(node->id,lease);Check(current && SameSpec(spec,current->spec),"mixed pose retains the exact body lease and source identity");
    crew::enemy_native::pose::State decoded{};Check(crew::enemy_native::pose::Decode(bytes,size,&decoded),"each mixed pressure pose decodes with the production semantic validator");const auto expected=AggregatePoseBytes(body,sequence);Check(size==expected.size() && !std::memcmp(bytes,expected.data(),size),"mixed pose payload remains byte-identical across the reliable production wire");Check(sequence==bus.posesApplied[node->id][body]+1,"each guest applies every mixed pressure pose once in order");++bus.posesApplied[node->id][body];return true;
}
ActionResult AggregateApply(void* user,std::uint64_t lease,const LeaseSpec& spec,std::uint64_t sequence,const void* bytes,std::size_t size){auto& bus=AggregateBus(user);const auto* node=static_cast<Node*>(user);Check(node->id!=0,"aggregate host never replays an already accepted native outcome");const auto body=bus.Body(lease);const auto* current=bus.room.Lease(node->id,lease);
    Check(current && SameSpec(spec,current->spec) && spec.requesterPlayer==bus.sources[body] && spec.target==Id(100+body) && (current->phase==Phase::active || current->phase==Phase::draining),"aggregate typed action remains bound to exact body, source, target and live lease");
    auto& applied=bus.applied[node->id][body];Check(sequence==applied+1 && applied<bus.expected.size(),"each guest applies each body's 480 outcomes exactly once in original order");const auto& expected=*bus.expected[applied];Check(size==expected.size() && !std::memcmp(bytes,expected.data(),size),"every aggregate guest receives byte-identical real provider payload for every body");++applied;return ActionResult::applied;
}
bool AggregateQuiesce(void* user,std::uint64_t lease,const LeaseSpec&){auto& bus=AggregateBus(user);++bus.quiesces[static_cast<Node*>(user)->id][bus.Body(lease)];return true;}
bool AggregateEmpty(void* user,std::uint64_t lease,const LeaseSpec&){const auto& bus=AggregateBus(user);return static_cast<Node*>(user)->id!=0 || (bus.producerDone && bus.uncaptured[bus.Body(lease)].empty());}
bool AggregateAbort(void* user,std::uint64_t lease,const LeaseSpec&){auto& bus=AggregateBus(user);++bus.aborts[static_cast<Node*>(user)->id][bus.Body(lease)];return true;}
void RunAggregateTrace(const TraceScenario& trace,unsigned peerCount,unsigned bodies=8,unsigned nativeHz=144,unsigned pollHz=60,bool mixedPose=false,bool stallReceiver=false,bool faultExit=false){
    ++traceAggregateRuns;AggregateTraceBus bus(trace,peerCount,bodies,nativeHz,pollHz,mixedPose,stallReceiver,faultExit);
    for(const auto& node:bus.room.nodes){aggregateTraceBuses[node.get()]=&bus;auto hooks=Backend(node.get());hooks.Send=&AggregateSend;hooks.Authenticate=&AggregateAuth;hooks.ValidateInput=&TraceValidate;hooks.ApplyPose=&AggregatePoseApply;hooks.ApplyAction=&AggregateApply;hooks.QuiesceActions=&AggregateQuiesce;hooks.ActionsEmpty=&AggregateEmpty;hooks.AbortActions=&AggregateAbort;Check(node->protocol.Configure(hooks),"install independent aggregate timed native backend before acquisition");Check(node->protocol.SetSendPacing(true),"aggregate uses actual frame-scoped global production sender pacing");}
    // Admission is intentionally serialized by the production protocol. Acquire
    // each actual body through the full barrier before requesting the next one;
    // the native eight-body burst starts only after all independent leases exist.
    for(unsigned body=0;body<bodies;++body){
        const bool accepted=bus.room.nodes[bus.owners[body]]->protocol.Request(bus.sources[body],Id(100+body),0,bus.room.now);if(!accepted)bus.AcquisitionReport("REQUEST_REFUSED");Check(accepted,"each distinct aggregate source requests its own actual body");
        const auto acquireAt=bus.room.now;
        for(std::uint64_t frame=1;;++frame){bus.room.now=acquireAt+(frame*1000+pollHz-1)/pollHz;
            if(body && (!bus.lastInput || bus.room.now-bus.lastInput>=33)){for(unsigned earlier=0;earlier<body;++earlier)Check(bus.room.nodes[bus.owners[earlier]]->protocol.SubmitInput(bus.leases[earlier],TraceInputBytes().data(),TraceInputBytes().size(),bus.room.now),"already acquired aggregate sources retain real canonical neutral input while the next body acquires");bus.lastInput=bus.room.now;}
            bus.Tick();bool active=true;for(const auto& node:bus.room.nodes)active=active && node->protocol.ActiveCount()==body+1;if(active)break;
            if(bus.room.now-acquireAt>=5000)bus.AcquisitionReport("ACQUIRE_TIMEOUT");Check(bus.room.now-acquireAt<5000,"each aggregate all-peer acquisition completes at the actual shared-frame cadence within deadline");
        }
        const auto* request=bus.room.nodes[bus.owners[body]]->protocol.RequestFor(bus.sources[body]);Check(request && request->result==Result::Active && request->target==Id(100+body) && request->lease,"aggregate acquired outcome remains bound to its exact canonical source");bus.leases[body]=request->lease;Check(bus.leaseBody.emplace(request->lease,body).second,"aggregate body leases are distinct");
    }
    bus.lastInput=0;
    bus.startAt=bus.room.now;std::size_t nextTick=0;
    for(std::uint64_t frame=1;;++frame){bus.room.now=bus.startAt+(frame*1000+pollHz-1)/pollHz;
        while(nextTick<trace.ticks.size() && (trace.ticks[nextTick].tick*1000+nativeHz-1)/nativeHz<=bus.room.now-bus.startAt){for(unsigned body=0;body<bodies;++body)for(const auto& packet:trace.ticks[nextTick].packets){bus.uncaptured[body].push_back(&packet);++bus.produced;}++nextTick;}
        bus.Metrics();
        if(!bus.exiting && bus.produced==bus.expected.size()*bodies){bus.producerDone=true;bus.exitQueue=bus.Queue();bus.exitWindow=bus.Window();Check(bus.exitQueue+bus.exitWindow>0,"aggregate exit starts after final production and before all final payloads are captured or ACKed");bus.exitAt=bus.room.now;bus.exiting=true;
            for(unsigned body=0;body<bodies;++body){bus.capturedAtExit[body]=bus.captured[body];if(faultExit){Check(!bus.uncaptured[body].empty() && bus.captured[body]<bus.expected.size(),"fault begins after every real payload exists and before each body's remaining tail is captured");bus.room.nodes[0]->protocol.Failed(bus.leases[body],bus.room.now);}else Check(bus.room.nodes[bus.owners[body]]->protocol.Exit(bus.leases[body],bus.room.now),"all aggregate owners request normal exit at the same timestamp before capture");}
        }
        if(!bus.exiting && (!bus.lastInput || bus.room.now-bus.lastInput>=33)){for(unsigned body=0;body<bodies;++body)if(!bus.Paused(bus.owners[body]))Check(bus.room.nodes[bus.owners[body]]->protocol.SubmitInput(bus.leases[body],TraceInputBytes().data(),TraceInputBytes().size(),bus.room.now),"each aggregate source maintains its own canonical owner input stream");bus.lastInput=bus.room.now;}
        if(mixedPose && !bus.exiting && (!bus.lastPose || bus.room.now-bus.lastPose>=33)){for(unsigned body=0;body<bodies;++body){const auto sequence=bus.posesSent[body]+1;const auto bytes=AggregatePoseBytes(body,sequence);Check(bus.room.nodes[0]->protocol.SubmitPose(bus.leases[body],bytes.data(),bytes.size(),bus.room.now),"30Hz legal pose traffic exercises mixed protocol pressure, without claiming Boss requires AF physical fallback");++bus.posesSent[body];}bus.lastPose=bus.room.now;}
        bus.Tick();bus.Capture();bus.Metrics();
        if(bus.exiting && bus.room.now-bus.exitAt>5000){bus.Report("RED_DRAIN_TIMEOUT");Check(false,"every aggregate lease must drain and restore normally within five seconds");}
        if(bus.Closed())break;
        Check(frame<1000000,"single aggregate native activation replay is bounded");
    }
    Check(bus.exiting && bus.room.now-bus.exitAt<=5000,"aggregate successful closure never bypasses the five-second restoration deadline");
    const auto wanted=faultExit ? Result::ApplyFailed : Result::Complete;Check(!bus.abortPackets,"normal and faulted drains never retire or emit wire Abort");
    bool normal=true;for(unsigned node=0;node<peerCount;++node)for(auto lease:bus.leases){const auto* current=bus.room.Lease(node,lease);normal=normal && current && current->phase==Phase::closed && current->localRestored && current->result==wanted;}if(!normal){bus.Report("RED_NONCOMPLETE_CLOSURE");Check(false,"aggregate closure preserves the true normal or ApplyFailed result everywhere without retirement");}
    for(unsigned body=0;body<bodies;++body){bus.room.Closed(bus.leases[body],wanted);const auto* outcome=bus.room.nodes[bus.owners[body]]->protocol.RequestFor(bus.sources[body]);Check(outcome && outcome->lease==bus.leases[body] && outcome->result==wanted,"each actual source retains the true normal or ApplyFailed terminal request outcome");Check(bus.captured[body]==bus.expected.size() && bus.uncaptured[body].empty(),"each body captures all 480 real typed outcomes before restoration completes");if(faultExit)Check(bus.captured[body]>bus.capturedAtExit[body],"faulted drain continues capturing the already-produced real tail after the fault");for(unsigned node=1;node<peerCount;++node)Check(bus.applied[node][body]==480 && bus.ackPrefix[node][body]==480,"every guest applies and actually ACKs all 480 outcomes for every body");for(unsigned node=0;node<peerCount;++node)Check(bus.quiesces[node][body] && !bus.aborts[node][body],"every aggregate lease quiesces and restores without Abort or discarded native payloads");}
    if(mixedPose){Check(bus.TotalPoses()>0,"mixed protocol-pressure scenario actually emits 30Hz legal pose snapshots");for(unsigned node=1;node<peerCount;++node)for(unsigned body=0;body<bodies;++body)Check(bus.posesApplied[node][body]==bus.posesSent[body],"every guest applies all synthetic legal pressure poses before the ordered drain boundary");}
    if(stallReceiver)Check(bus.pauseEnd-bus.pauseBegin==150 && bus.pausedFrames && bus.pausedArrivals && bus.pausedInboxPeak,"150ms game-thread stall keeps independent SDK worker arrival staging active");
    Check(bus.produced==bus.expected.size()*bodies && bus.totalCaptured==bus.produced,"aggregate acceptance neither drops nor invents actual native packets");Check(bus.sdkDelayed && bus.delayedAcks==1 && !bus.actionRetries,"aggregate timed room recovers one delayed Action and one delayed applied ACK through reliable SDK delivery without application retries");Check(bus.actionChunks==bus.produced*(peerCount-1),"paced aggregate sends each actual native outcome to each guest exactly once");Check(bus.maxBodyWindow<=kActionWindow && bus.maxWindow<=kMaxActions,"aggregate ledgers respect each lease window and the shared pool bound");bus.Report(faultExit ? "GREEN_APPLYFAILED_DRAINED" : "GREEN_COMPLETE");
}
}


int main(int argc,char** argv) {
    std::vector<const char*> files;for(int i=1;i<argc;++i){if(!std::strcmp(argv[i],"--rtt-ms")){if(++i==argc)return 2;const std::string value=argv[i];if(value!="250" && value!="600")return 2;traceRttMs=static_cast<unsigned>(std::strtoul(argv[i],nullptr,10));}else if(!std::strcmp(argv[i],"--background-kib")){if(++i==argc)return 2;const std::string value=argv[i];if(value!="0" && value!="32" && value!="64")return 2;traceBackgroundBytes=static_cast<std::size_t>(std::strtoul(argv[i],nullptr,10))*1024;}else files.push_back(argv[i]);}
    if(files.empty()){std::fprintf(stderr,"usage: enemy_control_provider_trace_test [--rtt-ms 250|600] [--background-kib 0|32|64] packed-trace.dat [more-traces.dat ...]\n");return 2;}
    CapacityAdmission();
    std::printf("scenario,native_hz,poll_hz,actual_units,produced_packets,applied_packets_per_guest,max_uncaptured_packets,max_window,exit_uncaptured_packets,exit_window,close_ms,wire_action_chunks,retried_chunks,delayed_applied_acks,sdk_inbox_peak,poll_backlog_peak,sdk_pending_bytes_peak,sdk_background_bytes,rtt_ms\n");
    for(const auto* file:files)for(const auto& trace:ReadTrace(file)){if(trace.sourceHz){RunTrace(trace,trace.sourceHz,trace.sourceHz);if(trace.sourceHz!=60)RunTrace(trace,trace.sourceHz,60);}else {for(unsigned hz:{30u,60u,144u})RunTrace(trace,hz,hz);RunTrace(trace,144,60);}if(trace.name=="beam_double"){RunAggregateTrace(trace,3);RunAggregateTrace(trace,8);RunAggregateTrace(trace,33);RunAggregateTrace(trace,33,8,144,60,true,true,false);RunAggregateTrace(trace,33,8,144,60,true,true,true);}}
    std::printf("enemy_control_provider_trace: %d checks passed; %u scalar cases, %u aggregate cases, %u metadata scenarios skipped; native ABI trace replay, not real-game acceptance\n",checks,traceScalarRuns,traceAggregateRuns,traceSkipped);return 0;
}
