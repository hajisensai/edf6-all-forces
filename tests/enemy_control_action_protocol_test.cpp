#define main ExistingEnemyControlProtocolTests
#include "enemy_control_protocol_test.cpp"
#undef main
namespace {
struct AppliedActions {ActionResult result=ActionResult::applied;unsigned attempts=0;std::vector<std::uint64_t> sequences;std::vector<std::vector<unsigned char>> payloads;};
std::map<Node*,AppliedActions> actionLedgers;
ActionResult ApplyAction(void* user,std::uint64_t id,const LeaseSpec& spec,std::uint64_t sequence,const void* bytes,std::size_t size) {
    auto* node=static_cast<Node*>(user);auto& ledger=actionLedgers[node];++ledger.attempts;
    Check(node->id!=0,"host accepted action is never executed a second time by its transport");
    const auto* lease=node->room->Lease(node->id,id);Check(lease && (lease->phase==Phase::active || lease->phase==Phase::draining) && SameSpec(spec,lease->spec),"native action targets the exact live controlled identity");
    if(ledger.result==ActionResult::applied){ledger.sequences.push_back(sequence);ledger.payloads.emplace_back(static_cast<const unsigned char*>(bytes),static_cast<const unsigned char*>(bytes)+size);}
    return ledger.result;
}
void EnableActions(Room& room) {actionLedgers.clear();for(auto& node:room.nodes){auto hooks=Backend(node.get());hooks.ApplyAction=&ApplyAction;Check(node->protocol.Configure(hooks),"bind accepted-action backend before acquisition");}}
std::vector<unsigned char> ActionBytes(std::size_t size,unsigned salt=0){std::vector<unsigned char> bytes(size);for(std::size_t i=0;i<size;++i)bytes[i]=static_cast<unsigned char>(i*31+salt);return bytes;}
unsigned CountActions(const Room& room,std::uint64_t sequence=0) {unsigned count=0;for(const auto& packet:room.history){const auto m=Unpack(packet);if(m.kind==Kind::Action && (!sequence || sequence==m.sequence))++count;}return count;}
void ActionsOrderedAndAtomic() {
    for(std::size_t size:{1u,128u,129u,232u,512u}) {
        Room r;EnableActions(r);Check(r.Request(),"accepted-action source requests control");r.Settle();const auto id=r.Active();
        auto a=ActionBytes(size,1),b=ActionBytes(size,2);
        Check(!r.nodes[1]->protocol.SubmitAction(id,a.data(),a.size(),r.now),"controller cannot impersonate the native accepted-action host");
        Check(r.nodes[0]->protocol.SubmitAction(id,a.data(),a.size(),r.now),"host queues actual accepted action");
        Check(r.nodes[0]->protocol.SubmitAction(id,b.data(),b.size(),r.now),"host retains a following transition without replacing the first");
        Check(CountActions(r,1)>0 && CountActions(r,2)>0 && !r.nodes[0]->protocol.ActionIdle(id),"bounded in-flight accepted transitions preserve order without one RTT per batch");
        auto first=r.Find(Kind::Action,0,1);r.Pump();
        for(unsigned i=1;i<3;++i)Check(actionLedgers[r.nodes[i].get()].sequences==std::vector<std::uint64_t>({1,2}) && actionLedgers[r.nodes[i].get()].payloads==std::vector<std::vector<unsigned char>>({a,b}),"every guest applies complete accepted transitions exactly once in host order");
        r.Deliver(first);r.Pump();for(unsigned i=1;i<3;++i)Check(actionLedgers[r.nodes[i].get()].sequences.size()==2,"duplicate old accepted event only repeats ACK");
        r.Input();for(const auto& n:r.nodes)Check(n->inputs.back().sequence==1,"accepted-action serial does not consume player input serial or RNG envelope");
    }
    Room r;EnableActions(r);Check(r.Request(),"compound native action starts");r.Settle();auto bytes=ActionBytes(512);Check(r.nodes[0]->protocol.SubmitAction(r.Active(),bytes.data(),bytes.size(),r.now),"complete action fits one actual SDK envelope");
    std::vector<Packet> packets;while(!r.queue.empty()){packets.push_back(r.queue.front());r.queue.pop_front();}
    for(const auto& packet:packets)for(std::size_t size=0;size<packet.bytes.size();++size)
        Check(r.nodes[packet.to]->protocol.Receive(r.Peer(packet.to,packet.from),r.nodes[packet.from]->puid.c_str(),packet.bytes.data(),size,r.now)==(size>=4),"every truncated complete action is refused before native mutation");
    Check(actionLedgers[r.nodes[1].get()].attempts==0 && actionLedgers[r.nodes[2].get()].attempts==0,"partial SDK envelope cannot invoke native state setter");
    for(const auto& packet:packets)r.Deliver(packet);r.Pump();
    Check(actionLedgers[r.nodes[1].get()].sequences.size()==1 && actionLedgers[r.nodes[2].get()].sequences.size()==1,"complete authenticated envelope commits event once");
}
void ActionsPendingAndAckLoss() {
    {Room r;EnableActions(r);Check(r.Request(),"pending native transition starts");r.Settle();auto bytes=ActionBytes(232);actionLedgers[r.nodes[2].get()].result=ActionResult::pending;
     Check(r.nodes[0]->protocol.SubmitAction(r.Active(),bytes.data(),bytes.size(),r.now),"accepted transition queued while one replica awaits a real native precondition");
     Check(r.nodes[0]->protocol.SubmitAction(r.Active(),bytes.data(),bytes.size(),r.now),"next transition stays retained");r.Pump();
     Check(actionLedgers[r.nodes[2].get()].sequences.empty() && !r.nodes[0]->protocol.ActionIdle(1),"pending native outcome is never reported as successful application");
     r.Step();Check(!r.nodes[0]->protocol.ActionIdle(1) && actionLedgers[r.nodes[1].get()].sequences.size()==2,"100ms retransmission cannot replay an already committed peer");
     actionLedgers[r.nodes[2].get()].result=ActionResult::applied;r.Step();
     for(unsigned i=1;i<3;++i)Check(actionLedgers[r.nodes[i].get()].sequences==std::vector<std::uint64_t>({1,2}),"after true native application all ACKs unlock the next event");}
    {Room r;EnableActions(r);Check(r.Request(),"lost native ACK lease starts");r.Settle();auto bytes=ActionBytes(30);r.dropFrom=2;r.dropTo=0;r.dropKind=Kind::ActionAck;
     Check(r.nodes[0]->protocol.SubmitAction(r.Active(),bytes.data(),bytes.size(),r.now),"first action queued");Check(r.nodes[0]->protocol.SubmitAction(r.Active(),bytes.data(),bytes.size(),r.now),"second action waits");r.Pump();Check(!r.nodes[0]->protocol.ActionIdle(1),"missing applied ACK retains the host window head even when later ACKs arrive");
     r.dropFrom=99;r.Step();for(unsigned i=1;i<3;++i)Check(actionLedgers[r.nodes[i].get()].sequences==std::vector<std::uint64_t>({1,2}),"ACK recovery never re-executes a native action");}
}
void ActionsFailureAndBinding() {
    {Room r;EnableActions(r);Check(r.Request(),"native refusal lease starts");r.Settle();actionLedgers[r.nodes[2].get()].result=ActionResult::failed;auto bytes=ActionBytes(10);
     Check(r.nodes[0]->protocol.SubmitAction(r.Active(),bytes.data(),bytes.size(),r.now),"native host queues accepted transition");r.Pump();r.Closed(1,Result::ApplyFailed);}
    {Room r;EnableActions(r);Check(r.Request(),"pending timeout lease starts");r.Settle();actionLedgers[r.nodes[2].get()].result=ActionResult::pending;auto bytes=ActionBytes(10);
     Check(r.nodes[0]->protocol.SubmitAction(r.Active(),bytes.data(),bytes.size(),r.now),"pending action queued");r.Pump();for(unsigned i=0;i<55;++i)r.Step();r.Closed(1,Result::TimedOut);}
    for(unsigned error=0;error<8;++error){Room r;EnableActions(r);Check(r.Request(),"bound accepted-action lease starts");r.Settle();auto bytes=ActionBytes(232);Check(r.nodes[0]->protocol.SubmitAction(r.Active(),bytes.data(),bytes.size(),r.now),"binding packet queued");auto packet=r.Find(Kind::Action,0,1);r.queue.clear();auto m=Unpack(packet);bool foreign=false;
     switch(error){case 0:packet.from=2;foreign=true;break;case 1:--m.epoch;foreign=true;break;case 2:++m.lease;foreign=true;break;case 3:m.spec.target=Id(105);break;case 4:m.spec.requesterPlayer=r.nodes[2]->player;break;case 5:++m.spec.profileHash;break;case 6:++m.request;break;default:++m.sequence;break;}
     if(error)packet=r.PacketOf(0,1,m);r.Deliver(packet);r.Pump();Check(actionLedgers[r.nodes[1].get()].attempts==0,"forged host, epoch, lease, source, target, profile, nonce or sequence cannot touch native state");if(foreign)Check(r.nodes[0]->protocol.ActiveCount()==1,"foreign retired event does not revoke valid lease");else r.Closed(1,Result::InvalidInput);}
    {Room r;EnableActions(r);Check(r.Request(),"send failure lease starts");r.Settle();auto bytes=ActionBytes(512);r.failFrom=0;r.failTo=2;r.failKind=Kind::Action;
     Check(!r.nodes[0]->protocol.SubmitAction(r.Active(),bytes.data(),bytes.size(),r.now),"partial event send failure ends the lease");r.Pump();r.Closed(1,Result::SendFailed);Check(actionLedgers[r.nodes[1].get()].attempts<=1,"a peer may apply one complete accepted action before another send fails; restoration never claims world-effect rollback");}
}
struct ClosingLedger {bool empty=true,quiesce=true;unsigned quiesces=0,aborts=0,abortFailures=0;};
std::map<Node*,ClosingLedger> closings;
bool Quiesce(void* user,std::uint64_t,const LeaseSpec&){auto& state=closings[static_cast<Node*>(user)];++state.quiesces;return state.quiesce;}
bool Empty(void* user,std::uint64_t,const LeaseSpec&){return closings[static_cast<Node*>(user)].empty;}
bool Abort(void* user,std::uint64_t,const LeaseSpec&){auto& state=closings[static_cast<Node*>(user)];++state.aborts;if(state.abortFailures){--state.abortFailures;return false;}state.empty=true;return true;}
void EnableClosing(Room& r){EnableActions(r);closings.clear();for(auto& node:r.nodes){auto h=Backend(node.get());h.ApplyAction=&ApplyAction;h.QuiesceActions=&Quiesce;h.ActionsEmpty=&Empty;h.AbortActions=&Abort;Check(node->protocol.Configure(h),"bind native quiesce/drain/retirement contract");}}
void ClosingDrainsAcceptedOutcomes() {
    {Room r;EnableClosing(r);Check(r.Request(),"normal closing lease starts");r.Settle();closings[r.nodes[0].get()].empty=false;
     Check(r.nodes[1]->protocol.Exit(1,r.now),"owner exits before host native outbox was captured");r.Pump();
     for(const auto& node:r.nodes)Check(r.Lease(node->id,1)->phase==Phase::draining && !node->restores && closings[node.get()].quiesces && !closings[node.get()].aborts,"normal exit quiesces without restoring or abandoning unsent accepted shots");
     auto bytes=ActionBytes(232);Check(r.nodes[0]->protocol.SubmitAction(1,bytes.data(),bytes.size(),r.now),"already accepted native outcome still transmits while closing");r.Pump();
     Check(r.Lease(0,1)->phase==Phase::draining && actionLedgers[r.nodes[1].get()].sequences.size()==1,"wire ACK alone cannot hide an uncaptured native outbox");
     closings[r.nodes[0].get()].empty=true;r.Step();r.Closed(1,Result::Complete);for(const auto& node:r.nodes)Check(!closings[node.get()].aborts,"normal drained exit uses no fault retirement");}
    {Room r;EnableClosing(r);Check(r.Request(),"retained source identity closing starts");r.Settle();closings[r.nodes[0].get()].empty=false;
     Check(r.nodes[1]->protocol.Exit(1,r.now),"source exits before its old player registry entry retires");r.Pump();
     r.nodes[1]->player=Player(71);auto bytes=ActionBytes(232);Check(r.nodes[0]->protocol.SubmitAction(1,bytes.data(),bytes.size(),r.now),"cached accepted outcome remains bound to original lease after the source leaves active lookup");r.Pump();
     Check(actionLedgers[r.nodes[1].get()].sequences==std::vector<std::uint64_t>({1}) && actionLedgers[r.nodes[2].get()].sequences==std::vector<std::uint64_t>({1}),"draining uses sealed original source identity and retained native body, never adopts a replacement player");
     closings[r.nodes[0].get()].empty=true;r.Step();r.Closed(1,Result::Complete);}
    {Room r;EnableClosing(r);Check(r.Request(),"retriable closing action starts");r.Settle();auto bytes=ActionBytes(232);actionLedgers[r.nodes[2].get()].result=ActionResult::failed;
     Check(r.nodes[0]->protocol.SubmitAction(1,bytes.data(),bytes.size(),r.now),"host accepted event queued");r.Pump();
     Check(r.Lease(0,1)->phase==Phase::draining && !r.nodes[0]->restores && !closings[r.nodes[0].get()].aborts,"a guest refusal cannot discard the host's valid accepted queue");
     actionLedgers[r.nodes[2].get()].result=ActionResult::applied;r.Settle();r.Closed(1,Result::ApplyFailed);
     Check(actionLedgers[r.nodes[2].get()].sequences==std::vector<std::uint64_t>({1}) && !closings[r.nodes[0].get()].aborts,"backend recovery applies original outcome once before restoring faulted control");}
    {Room r;EnableClosing(r);Check(r.Request(),"unrecoverable guest action starts");r.Settle();Check(r.Request(2,101),"independent second body starts");r.Settle();auto bytes=ActionBytes(10);actionLedgers[r.nodes[2].get()].result=ActionResult::failed;
     Check(r.nodes[0]->protocol.SubmitAction(1,bytes.data(),bytes.size(),r.now),"native failure event queued");r.Pump();
     for(unsigned i=0;i<49;++i)r.Step();Check(r.Lease(0,1)->phase==Phase::draining && !closings[r.nodes[0].get()].aborts,"untrusted refusal waits for the host's own bounded action deadline");
     for(unsigned i=0;i<4;++i)r.Step();
     for(const auto& node:r.nodes){const auto* l=r.Lease(node->id,1);Check(l->phase==Phase::closed && l->result==Result::ApplyFailed && l->localRestored && closings[node.get()].aborts,"host deadline explicitly retires failed delivery and safely restores, without claiming Complete");Check(r.Lease(node->id,2)->phase==Phase::active,"one failed accepted outcome cannot retire an unrelated controller");}}
    {Room r;EnableClosing(r);Check(r.Request(),"wrong-spec fault lease starts");r.Settle();closings[r.nodes[0].get()].empty=false;Message m;m.kind=Kind::Fault;m.epoch=777;m.lease=1;m.request=999;m.spec=r.Lease(0,1)->spec;m.result=Result::ApplyFailed;
     r.Deliver(r.PacketOf(2,0,m));Check(r.Lease(0,1)->phase==Phase::active && !closings[r.nodes[0].get()].aborts,"guest fault must bind the current request nonce and full stored spec");}
    {Room r;EnableClosing(r);Check(r.Request(),"context-loss retirement starts");r.Settle();closings[r.nodes[0].get()].empty=false;closings[r.nodes[0].get()].abortFailures=2;
     r.nodes[0]->protocol.Reset(r.now);Check(r.Lease(0,1)->phase==Phase::restoring && !r.Lease(0,1)->localRestored && r.nodes[0]->native[1].suppressed,"failed native abort retains original baseline and weak lease on trusted local context loss");
     r.nodes[0]->protocol.Reset(r.now);r.nodes[0]->protocol.Tick({},++r.now);Check(r.Lease(0,1)->localRestored && !r.nodes[0]->native[1].suppressed,"native fault retirement retries until actual local restoration succeeds");}
}
void ContinuousBatchesSurviveLatency() {
    for(unsigned rtt:{50u,100u,250u}) {
        Room r;EnableActions(r);Check(r.Request(),"continuous native-step lease starts");r.Settle();
        struct Delayed {Packet packet;std::uint64_t at;};std::vector<Delayed> delayed;
        std::deque<unsigned> nativeSteps;unsigned produced=0,maxPending=0,maxBatch=0;
        auto schedule=[&](){while(!r.queue.empty()){delayed.push_back({r.queue.front(),r.now+rtt/2});r.queue.pop_front();}};
        for(unsigned frame=0;frame<1000;++frame) {
            r.now+=frame%3==2 ? 16 : 17; // native 60 Hz, never one state transition per network RTT
            if(produced<480)nativeSteps.push_back(++produced);
            maxPending=(std::max)(maxPending,static_cast<unsigned>(nativeSteps.size()));
            for(auto& node:r.nodes)node->protocol.Tick(node->context,r.now);
            if(r.nodes[0]->protocol.ActionIdle(1) && !nativeSteps.empty()) {
                const unsigned count=(std::min)(21u,static_cast<unsigned>(nativeSteps.size()));maxBatch=(std::max)(maxBatch,count);
                std::vector<unsigned char> bytes(count*4);
                for(unsigned i=0;i<count;++i){const unsigned value=nativeSteps.front();nativeSteps.pop_front();for(unsigned b=0;b<4;++b)bytes[i*4+b]=static_cast<unsigned char>(value>>(b*8));}
                Check(r.nodes[0]->protocol.SubmitAction(1,bytes.data(),bytes.size(),r.now),"batched native transitions fit sustained reliable throughput");
            }
            schedule();
            for(std::size_t i=0;i<delayed.size();) {
                if(delayed[i].at>r.now){++i;continue;}const auto packet=delayed[i].packet;delayed.erase(delayed.begin()+static_cast<std::ptrdiff_t>(i));r.Deliver(packet);schedule();
            }
            Check(r.Lease(0,1)->phase==Phase::active,"50/100/250ms latency never trips normal accepted-event overflow");
            if(produced==480 && nativeSteps.empty() && r.nodes[0]->protocol.ActionIdle(1))break;
        }
        Check(produced==480 && nativeSteps.empty() && maxPending<64 && maxBatch>1,"backpressure batches retain all 60Hz native outcomes without unbounded buffering");
        for(unsigned n=1;n<3;++n) {
            unsigned expected=1;for(const auto& batch:actionLedgers[r.nodes[n].get()].payloads)for(std::size_t at=0;at<batch.size();at+=4) {
                unsigned value=0;for(unsigned b=0;b<4;++b)value|=static_cast<unsigned>(batch[at+b])<<(b*8);
                Check(value==expected++,"delayed native transition batches contain every original step once in order");
            }
            Check(expected==481,"both replicas receive every sustained native step before the run completes");
        }
    }
}
void MultiGunBurstWindowSurvivesLatency() {
    // Conservative synthetic upper stress: 33 simultaneous Beam-like guns, 85 total shots per burst, each gun's
    // adjacent shots seven 60Hz frames apart, and a 240-frame recharge. This does not pretend XL's mixed SGO is 33 Beams.
    for(unsigned rtt:{50u,100u,250u}) {
        Room r;EnableActions(r);Check(r.Request(),"multi-gun accepted-shot lease starts");r.Settle();
        struct Delayed {Packet packet;std::uint64_t at;};std::vector<Delayed> delayed;std::deque<unsigned> shots;
        unsigned produced=0,maxPending=0,maxWindow=0;
        auto schedule=[&](){while(!r.queue.empty()){delayed.push_back({r.queue.front(),r.now+rtt/2});r.queue.pop_front();}};
        for(unsigned frame=0;frame<2800;++frame) {
            r.now+=frame%3==2 ? 16 : 17;
            if(frame<2505){const unsigned local=frame%835;if(local>=240 && (local-240)%7==0)for(unsigned gun=0;gun<33;++gun)shots.push_back(++produced);}
            maxPending=(std::max)(maxPending,static_cast<unsigned>(shots.size()));
            for(auto& node:r.nodes)node->protocol.Tick(node->context,r.now);
            while(r.nodes[0]->protocol.CanCaptureAction(1) && !shots.empty()) {
                const unsigned count=(std::min)(5u,static_cast<unsigned>(shots.size()));std::vector<unsigned char> bytes(count*93,0x5A);
                for(unsigned i=0;i<count;++i){const auto serial=shots.front();shots.pop_front();for(unsigned b=0;b<4;++b)bytes[i*93+b]=static_cast<unsigned char>(serial>>(b*8));}
                Check(r.nodes[0]->protocol.SubmitAction(1,bytes.data(),bytes.size(),r.now),"93-byte real-shot-shape batches fit a bounded reliable window");
            }
            unsigned sentNotApplied=0;
            // CanCapture false with nonempty producer data is expected window backpressure, never native outcome loss.
            if(!r.nodes[0]->protocol.CanCaptureAction(1) && !r.nodes[0]->protocol.ActionIdle(1))sentNotApplied=static_cast<unsigned>(kActionWindow);
            maxWindow=(std::max)(maxWindow,sentNotApplied);schedule();
            for(std::size_t i=0;i<delayed.size();) {
                if(delayed[i].at>r.now){++i;continue;}const auto packet=delayed[i].packet;delayed.erase(delayed.begin()+static_cast<std::ptrdiff_t>(i));r.Deliver(packet);schedule();
            }
            Check(r.Lease(0,1)->phase==Phase::active,"sustained native multi-gun bursts never enter overflow restoration at normal RTT");
            if(frame>=2505 && shots.empty() && r.nodes[0]->protocol.ActionIdle(1))break;
        }
        Check(produced==3u*33u*85u && shots.empty() && r.nodes[0]->protocol.ActionIdle(1),"all conservative multi-gun burst outcomes drain before the test ends");
        Check(maxPending<128 && maxWindow<=kActionWindow,"legacy synthetic burst remains bounded after the larger native-provider window");
        for(unsigned n=1;n<3;++n){unsigned expected=1;for(const auto& batch:actionLedgers[r.nodes[n].get()].payloads){Check(batch.size()%93==0 && batch.size()<=5*93,"every accepted packet keeps all complete 93-byte shot records");for(std::size_t at=0;at<batch.size();at+=93){unsigned serial=0;for(unsigned b=0;b<4;++b)serial|=static_cast<unsigned>(batch[at+b])<<(b*8);Check(serial==expected++,"every shot remains in exact host order once across pipelined latency");}}
            Check(expected==produced+1,"both peers apply every shot from every gun and burst");}
    }
}
void SharedPoolCoversEightNativeControllers() {
    // Production core admits eight bodies. Fill every 128-batch window before delivering one ACK: pool slots
    // above 255 must retain their exact payload and native lease, and ring wrap must not alias another body.
    static_assert(kActionWindow==128 && kMaxActions>=8*kActionWindow,"actual native controller capacity fits the complete pipeline");
    static_assert(sizeof(ActionIndex)>=2,"the shared native action pool cannot use an eight-bit index");
    Room r(9);EnableActions(r);
    for(unsigned owner=1;owner<=8;++owner){Check(r.Request(owner,99+owner),"each actual native controller acquires an independent body");r.Settle();}
    std::vector<std::vector<unsigned char>> expected;
    for(unsigned round=0;round<2;++round) {
        if(!round)for(unsigned peer=1;peer<=8;++peer)actionLedgers[r.nodes[peer].get()].result=ActionResult::pending;
        for(unsigned owner=1;owner<=8;++owner)for(unsigned n=0;n<kActionWindow;++n) {
            auto bytes=ActionBytes(20,owner);bytes[0]=static_cast<unsigned char>(owner);bytes[1]=static_cast<unsigned char>(round);bytes[2]=static_cast<unsigned char>(n);
            Check(r.nodes[0]->protocol.CanCaptureAction(owner),"one slow peer cannot consume another admitted body's full window");
            Check(r.nodes[0]->protocol.SubmitAction(owner,bytes.data(),bytes.size(),r.now),"every controller retains a complete reliable pipeline");expected.push_back(bytes);
        }
        for(unsigned owner=1;owner<=8;++owner)Check(!r.nodes[0]->protocol.CanCaptureAction(owner),"all native windows are backpressured before capture, never overwritten");
        r.Pump();
        if(!round) {
            for(unsigned peer=1;peer<=8;++peer){Check(actionLedgers[r.nodes[peer].get()].payloads.empty(),"all replica pool slots remain retained while native preconditions are pending");actionLedgers[r.nodes[peer].get()].result=ActionResult::applied;}
            for(unsigned owner=1;owner<=8;++owner)Check(!r.nodes[0]->protocol.ActionIdle(owner),"receiving all 1024 batches is never reported as native application");
            r.Step(1);
        }
        for(unsigned peer=1;peer<=8;++peer)Check(actionLedgers[r.nodes[peer].get()].payloads==expected,"pool indices above 255 and ring reuse preserve exact per-body payloads on every peer");
        for(unsigned owner=1;owner<=8;++owner)Check(r.nodes[0]->protocol.ActionIdle(owner),"all body windows release only after every peer applies them");
    }
    for(unsigned owner=1;owner<=8;++owner){Check(r.nodes[owner]->protocol.Exit(owner,r.now),"each independent controller exits");r.Pump();}
    for(unsigned peer=1;peer<=8;++peer) {
        const auto& sequences=actionLedgers[r.nodes[peer].get()].sequences;std::size_t offset=0;
        for(unsigned round=0;round<2;++round)for(unsigned owner=1;owner<=8;++owner)for(unsigned n=0;n<kActionWindow;++n)
            Check(sequences[offset++]==round*kActionWindow+n+1,"action serial remains scoped to its body across shared-pool wrap");
    }
}
void PacedEightControllersRetainAndRotate() {
    Room r(9);EnableClosing(r);
    for(auto& node:r.nodes)Check(node->protocol.SetSendPacing(true),"real sender pacing binds before actor acquisition");
    for(unsigned owner=1;owner<=8;++owner){Check(r.Request(owner,99+owner),"paced native body acquired");r.Settle();}
    for(unsigned owner=1;owner<=8;++owner)for(unsigned n=0;n<kActionWindow;++n) {
        auto bytes=ActionBytes(20,owner);bytes[0]=static_cast<unsigned char>(owner);bytes[1]=static_cast<unsigned char>(n);
        Check(r.nodes[0]->protocol.SubmitAction(owner,bytes.data(),bytes.size(),r.now),"exhausted frame quota retains every native batch instead of reporting SendFailed");
    }
    Check(!CountActions(r),"capture never bypasses the actual shared frame sender budget");
    unsigned frames=0;std::size_t previousHistory=r.history.size();bool all=false;
    for(;frames<160 && !all;++frames) {
        r.now+=16;for(auto& node:r.nodes)node->protocol.Tick(node->context,r.now);
        for(auto& node:r.nodes)node->protocol.TransportFrame(r.now);r.Pump();
        unsigned actionPackets=0;unsigned ackPackets[9]{};
        for(std::size_t i=previousHistory;i<r.history.size();++i){const auto m=Unpack(r.history[i]);if(m.kind==Kind::Action)++actionPackets;if(m.kind==Kind::ActionAck || m.kind==Kind::ActionAckBatch)++ackPackets[r.history[i].from];}
        previousHistory=r.history.size();Check(actionPackets<=kActionFramePackets,"sender quota is global across all body leases and destination peers");
        for(unsigned peer=1;peer<=8;++peer)Check(ackPackets[peer]<=kActionFrameAcks,"each real frame coalesces contiguous native application into bounded cumulative ACKs");
        all=true;for(unsigned owner=1;owner<=8;++owner){Check(r.Lease(0,owner)->phase==Phase::active,"normal sender backpressure does not fault any body");all=all && r.nodes[0]->protocol.ActionIdle(owner);}
    }
    Check(all && frames<100,"eight full native windows drain fairly inside the actual five-second action deadline");
    for(unsigned peer=1;peer<=8;++peer) {
        unsigned perBody[9]{};const auto& ledger=actionLedgers[r.nodes[peer].get()];
        for(std::size_t i=0;i<ledger.payloads.size();++i){const auto& bytes=ledger.payloads[i];const unsigned owner=bytes[0];Check(owner>=1 && owner<=8 && bytes[1]==perBody[owner] && ledger.sequences[i]==perBody[owner]+1,"round-robin transport keeps each body's original payload and serial exactly once");++perBody[owner];}
        for(unsigned owner=1;owner<=8;++owner)Check(perBody[owner]==kActionWindow,"each admitted body receives its complete quota without source starvation");
    }
    for(unsigned owner=1;owner<=8;++owner){Check(r.nodes[owner]->protocol.Exit(owner,r.now),"paced controller exits after actual all-peer application");r.Pump();}
    for(unsigned peer=0;peer<=8;++peer)Check(!closings[r.nodes[peer].get()].aborts,"ordinary paced delivery never retires accepted effects as a fault");
}
void PacedSdkCreditsRetainUnsentBatches() {
    for(std::size_t payloadSize:{20u,37u,93u,512u}) {
    Room r(9);EnableActions(r);for(auto& node:r.nodes)Check(node->protocol.SetSendPacing(true),"SDK byte pacing binds before native admission");
    for(unsigned owner=1;owner<=8;++owner){Check(r.Request(owner,99+owner),"credit stress body acquired");r.Settle();}
    const auto bytes=ActionBytes(payloadSize,9);
    for(unsigned owner=1;owner<=8;++owner)for(unsigned n=0;n<kActionWindow;++n)Check(r.nodes[0]->protocol.SubmitAction(owner,bytes.data(),bytes.size(),r.now),"full native batch remains stored before any transport quota");
    const auto prepare=Unpack(r.Find(Kind::Prepare,0,1));Message premature;premature.kind=Kind::ActionAck;premature.epoch=777;premature.lease=1;premature.request=prepare.request;premature.spec=r.Lease(0,1)->spec;premature.sequence=kActionWindow;premature.ok=true;premature.result=Result::Complete;
    r.Deliver(r.PacketOf(1,0,premature));Check(!r.nodes[0]->protocol.ActionIdle(1) && !CountActions(r),"authenticated cumulative ACK cannot cover a native batch which was never sent");
    for(unsigned frame=0;frame<80;++frame){r.now+=16;r.nodes[0]->protocol.Tick(r.nodes[0]->context,r.now);r.nodes[0]->protocol.TransportFrame(r.now);}
    std::size_t sentBytes[9]{},sentPackets[9]{};
    for(const auto& packet:r.history)if(Unpack(packet).kind==Kind::Action){sentBytes[packet.to]+=packet.bytes.size()+kActionSdkFramingBytes;++sentPackets[packet.to];}
    for(unsigned peer=1;peer<=8;++peer)Check(sentBytes[peer]<kActionPeerByteCredits && sentPackets[peer]<=kActionPeerPacketCredits && (sentPackets[peer]==kActionPeerPacketCredits || sentBytes[peer]+kWireBytes+kActionSdkFramingBytes>=kActionPeerByteCredits),"small and large initial/retry datagrams share strict per-link SDK packet and framed-byte budgets before any peer poll");
    for(unsigned owner=1;owner<=8;++owner)Check(r.Lease(0,owner)->phase==Phase::active && !r.nodes[0]->protocol.ActionIdle(owner),"ordinary SDK credit backpressure never becomes SendFailed or drops uncaptured outcomes");
    r.Pump();bool all=false;
    for(unsigned frame=0;frame<160 && !all;++frame){r.now+=16;for(auto& node:r.nodes)node->protocol.Tick(node->context,r.now);for(auto& node:r.nodes)node->protocol.TransportFrame(r.now);r.Pump();all=true;for(unsigned owner=1;owner<=8;++owner)all=all && r.nodes[0]->protocol.ActionIdle(owner);}
    Check(all,"actual cumulative applied ACKs release shared SDK credits and every retained native batch drains");
    for(unsigned peer=1;peer<=8;++peer)Check(actionLedgers[r.nodes[peer].get()].payloads.size()==8*kActionWindow,"SDK credit recovery applies all eight full windows once on every peer");
    }
}
void PacedThirtyTwoPeerControlBarriers() {
    Room r(33);EnableClosing(r);for(auto& node:r.nodes)Check(node->protocol.SetReliableOrderedTransport(true),"all 32 participants bind the actual reliable SDK mode");
    for(unsigned owner=1;owner<=8;++owner)Check(r.Request(owner,99+owner),"concurrent UI request is delivered to the authenticated host");r.Pump();
    unsigned prepares=0;for(const auto& packet:r.history)if(Unpack(packet).kind==Kind::Prepare)++prepares;
    Check(prepares==32,"one pending native setup is admitted; other simultaneous sources receive an explicit busy result");
    r.Settle();for(unsigned owner=2;owner<=8;++owner){Check(r.nodes[owner]->protocol.RequestFor(r.nodes[owner]->player)->result==Result::Unavailable,"busy refusal belongs to its exact source instead of mutating native actors");Check(r.Request(owner,99+owner),"a refused source may retry after the current setup completes");r.Settle();}
    for(const auto& node:r.nodes)Check(node->protocol.ActiveCount()==8,"all eight native bodies may coexist after bounded admission");
    for(unsigned owner=1;owner<=8;++owner)Check(r.nodes[owner]->protocol.Exit(owner,r.now),"all eight owners quiesce in the same real frame without serializing their native exit");r.Pump();
    std::size_t maximumHostQueue=0;unsigned frames=0;
    auto pump=[&](){unsigned guard=0;while(!r.queue.empty()){std::size_t host=0;for(const auto& packet:r.queue)if(!packet.to)++host;maximumHostQueue=(std::max)(maximumHostQueue,host);Check(host<256,"32-peer shared host inbox cannot be flooded by per-body barrier replies");Check(++guard<20000,"bounded control-plane traffic");const auto packet=r.queue.front();r.queue.pop_front();r.Deliver(packet);}};
    r.now+=1000;
    for(;frames<12;++frames){for(auto& node:r.nodes)node->protocol.Tick(node->context,r.now);for(auto& node:r.nodes)node->protocol.TransportFrame(r.now);pump();bool closed=true;for(unsigned owner=1;owner<=8;++owner)for(unsigned peer=0;peer<=32;++peer)closed=closed && r.Lease(peer,owner)->phase==Phase::closed;if(closed)break;r.now+=32;}
    Check(frames<12 && maximumHostQueue<=64,"same-frame periodic Hello plus eight Drain/Restored barriers remain bounded and finish promptly");
    for(unsigned owner=1;owner<=8;++owner)r.Closed(owner,Result::Complete);
    for(const auto& node:r.nodes)Check(!closings[node.get()].aborts,"normal 32-peer parallel exit never abandons accepted outcomes");
}
void CapacityProofChangesAreRealContextBoundaries() {
    Room r;auto context=r.nodes[0]->context;context.localCapability=0;r.nodes[0]->protocol.Tick(context,++r.now);r.Pump();
    Check(!r.nodes[0]->protocol.Ready() && !r.Request(0),"missing local capacity proof cannot admit even a local host's online enemy request");
    context.localCapability=kCapability;r.nodes[0]->context=context;r.nodes[0]->protocol.Tick(context,++r.now);r.Step(1001);r.Settle();Check(r.Request(),"all-party capacity proof may admit control after a genuine new handshake");r.Settle();
    const auto capacityLease=r.Active();
    r.nodes[0]->restoreFailures=4;context.localCapability=0;r.nodes[0]->protocol.Tick(context,++r.now);
    Check(!r.nodes[0]->protocol.Ready() && r.Lease(0,capacityLease) && r.Lease(0,capacityLease)->phase==Phase::restoring && !r.Lease(0,capacityLease)->localRestored,"capacity proof loss retains a failed native restoration journal");
    r.nodes[0]->context=context;r.Settle();Check(!r.nodes[0]->protocol.Ready() && !r.nodes[0]->native[capacityLease].suppressed,"proof failure cannot silently fall back to local-only online control");
}
void BatchedControlCodecAndFailurePriority() {
    Message batch;batch.kind=Kind::ActionAckBatch;batch.epoch=77;batch.ok=true;batch.result=Result::Complete;batch.input[0]=2;batch.input[1]=2;batch.inputSize=static_cast<std::uint16_t>(8+2*kAckEntryBytes);auto* out=batch.input.data()+8;
    auto row=[&](std::uint64_t lease,std::uint16_t signals,std::uint16_t success,std::uint64_t sequence,std::uint64_t results){wire::Put(out,lease,8);wire::Put(out,lease+10,8);wire::Put(out,sequence,8);wire::Put(out,0,8);wire::Put(out,signals,2);wire::Put(out,success,2);wire::Put(out,results,8);wire::Put(out,0,4);};
    row(1,ackApplied,ackApplied,3,static_cast<unsigned>(Result::Complete));row(2,ackDrained|ackFault,ackDrained,0,static_cast<std::uint64_t>(Result::ApplyFailed)<<30);
    std::vector<unsigned char> encoded(EncodedSize(batch));Check(Encode(batch,encoded.data(),encoded.size()),"one canonical peer batch binds multiple sealed lease/nonce replies");Message back;Check(Decode(encoded.data(),encoded.size(),back) && AppliedAckCount(back)==2 && AppliedAckAt(back,0).sequence==3 && AckResult(AppliedAckAt(back,1),6)==Result::ApplyFailed,"control result bits and exact native application sequence survive the real codec");
    for(unsigned error=0;error<13;++error){auto bad=batch;switch(error){case 0:bad.input[0]=1;break;case 1:bad.input[1]=0;break;case 2:bad.input[2]=1;break;case 3:bad.input[8+kAckEntryBytes]=1;break;case 4:bad.input[8+8]=0;bad.input[8+9]=0;break;case 5:bad.input[8+16]=0;break;case 6:bad.input[8+24]=1;break;case 7:bad.input[8+32]=128;break;case 8:bad.input[8+34]=128;break;case 9:bad.input[8+36]=31;break;case 10:bad.input[8+44]=1;break;case 11:bad.lease=9;break;default:bad.spec.profileHash=1;break;}Check(!Encode(bad,encoded.data(),encoded.size()),"malformed batch version/count/sort/nonce/sequence/token/signals/results/padding or outer identity is refused before effects");}
    for(bool fault:{false,true}) {
        Room r;EnableClosing(r);for(auto& node:r.nodes)Check(node->protocol.SetReliableOrderedTransport(true),"batched control backend enabled");Check(r.Request(),"control priority body requests");r.Settle();closings[r.nodes[2].get()].quiesce=false;
        Check(r.nodes[1]->protocol.Exit(1,r.now),"owner exits before final remote quiesce");r.Pump();r.Step(32);Check(r.Lease(0,1)->phase==Phase::draining,"pending native quiesce is never reported as complete");
        if(fault)r.nodes[2]->protocol.Failed(1,r.now);closings[r.nodes[2].get()].quiesce=true;r.Settle();r.Closed(1,fault ? Result::ApplyFailed : Result::Complete);
        for(const auto& node:r.nodes)Check(!closings[node.get()].aborts,"native quiesce recovery and same-batch Fault precede terminal success without timeout abandonment");
    }
}
void ActionsBoundedAndIndependent() {
    {Room r;EnableActions(r);Check(r.Request(),"bounded host event queue starts");r.Settle();auto bytes=ActionBytes(20);for(unsigned i=0;i<kActionWindow;++i)Check(r.nodes[0]->protocol.SubmitAction(1,bytes.data(),bytes.size(),r.now),"accepted native events remain retained up to explicit session bound");
     Check(!r.nodes[0]->protocol.SubmitAction(1,bytes.data(),bytes.size(),r.now),"queue overflow stops control rather than dropping a native action");r.Pump();r.Closed(1,Result::ApplyFailed);
     Check(r.Request(2,101),"freed action pool admits fresh controlled identity after failure");r.Settle();Check(r.nodes[0]->protocol.SubmitAction(2,bytes.data(),bytes.size(),r.now),"new lease can use reclaimed action pool");r.Pump();Check(actionLedgers[r.nodes[1].get()].sequences.back()==1,"action sequence is scoped to fresh lease");}
    {Room r;EnableActions(r);Check(r.Request(),"first independent lease starts");r.Settle();Check(r.Request(2,101),"second independent lease starts");r.Settle();r.dropFrom=2;r.dropTo=0;r.dropKind=Kind::Restored;Check(r.nodes[2]->protocol.Exit(2,r.now),"second actor restores");r.Pump();auto bytes=ActionBytes(20);
     Check(!r.nodes[0]->protocol.Ready() && r.nodes[0]->protocol.SubmitAction(1,bytes.data(),bytes.size(),r.now),"another player's restore never interrupts actual accepted events on a live body");r.Pump();Check(actionLedgers[r.nodes[1].get()].sequences.size()==1 && r.Lease(0,1)->phase==Phase::active,"independent live actor retains control");}
}
}

struct TransportBacklog {
    std::array<std::size_t,kMaxPeers+1> bytes{},packets{};
    bool delayTransportAck=true;unsigned blocked=0,accepted=0;
} transportBacklog;
SendResult BoundedTransportSend(void* user,std::uint32_t peer,const void* bytes,std::size_t size) {
    auto* node=static_cast<Node*>(user);Check(node->id==0,"only the native host sends accepted actions");
    const auto framed=size+kActionSdkFramingBytes;
    if(transportBacklog.bytes[peer]+framed>kActionPeerByteCredits || transportBacklog.packets[peer]>=kActionPeerPacketCredits){++transportBacklog.blocked;return SendResult::wouldBlock;}
    if(!Send(user,peer,bytes,size))return SendResult::unavailable;
    ++transportBacklog.accepted;
    if(transportBacklog.delayTransportAck && peer==1){transportBacklog.bytes[peer]+=framed;++transportBacklog.packets[peer];}
    return SendResult::accepted;
}
void AppliedAckDoesNotReleaseSdkBacklog() {
    Room r;EnableActions(r);transportBacklog={};
    for(auto& node:r.nodes){auto h=Backend(node.get());h.ApplyAction=&ApplyAction;h.TrySendAction=&BoundedTransportSend;
        Check(node->protocol.Configure(h),"bind typed real SDK admission");Check(node->protocol.SetReliableOrderedTransport(true),"use production ordered route");}
    Check(r.Request(),"independent SDK backlog starts");r.Settle();const auto id=r.Active();const auto bytes=ActionBytes(512);unsigned submitted=0;
    for(unsigned frame=0;frame<80;++frame){
        for(unsigned batch=0;batch<32 && r.nodes[0]->protocol.CanCaptureAction(id);++batch){Check(r.nodes[0]->protocol.SubmitAction(id,bytes.data(),bytes.size(),r.now),"accepted result remains in bounded native action pool");++submitted;}
        r.Step(16);
    }
    Check(transportBacklog.blocked && transportBacklog.packets[1]<=kActionPeerPacketCredits && transportBacklog.bytes[1]<=kActionPeerByteCredits,"transport ACK loss blocks on actual SDK backlog even after native Applied ACKs");
    Check(r.Lease(0,id)->phase==Phase::active && r.nodes[0]->protocol.Ready() && !r.nodes[0]->protocol.CanCaptureAction(id),"wouldBlock retains the lease and full native window without SendFailed");
    Check(actionLedgers[r.nodes[2].get()].sequences.size()>actionLedgers[r.nodes[1].get()].sequences.size(),"one blocked SDK link cannot prevent other peers consuming queued actions");
    // Native Applied ACKs above deliberately did not touch transportBacklog. Only the independent SDK ACK frees it.
    transportBacklog.bytes={};transportBacklog.packets={};transportBacklog.delayTransportAck=false;
    for(unsigned frame=0;frame<16 && !r.nodes[0]->protocol.ActionIdle(id);++frame)r.Step(16);
    Check(r.nodes[0]->protocol.ActionIdle(id),"real transport ACK recovery drains every retained action");
    for(unsigned node=1;node<3;++node){const auto& sequences=actionLedgers[r.nodes[node].get()].sequences;Check(sequences.size()==submitted,"no accepted native outcome is lost or replayed after SDK pressure");for(unsigned i=0;i<submitted;++i)Check(sequences[i]==i+1,"wouldBlock cannot consume an unsent action sequence");}
    Check(r.nodes[0]->protocol.Exit(id,r.now),"drained body exits normally");r.Settle();r.Closed(id,Result::Complete);
}

int main(){AppliedAckDoesNotReleaseSdkBacklog();ExistingEnemyControlProtocolTests();ActionsOrderedAndAtomic();ActionsPendingAndAckLoss();ActionsFailureAndBinding();ActionsBoundedAndIndependent();SharedPoolCoversEightNativeControllers();PacedEightControllersRetainAndRotate();PacedSdkCreditsRetainUnsentBatches();PacedThirtyTwoPeerControlBarriers();CapacityProofChangesAreRealContextBoundaries();BatchedControlCodecAndFailurePriority();ContinuousBatchesSurviveLatency();ClosingDrainsAcceptedOutcomes();MultiGunBurstWindowSurvivesLatency();std::printf("enemy_control_action_protocol: %d checks passed\n",checks);return 0;}
