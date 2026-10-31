// Reuse the authenticated three-peer fixture and every existing regression, then add the independent pose stream.
#define main ExistingEnemyControlProtocolTests
#include "enemy_control_protocol_test.cpp"
#undef main
namespace {
struct PoseLedger { unsigned calls=0;bool fail=false;std::uint64_t sequence=0;std::vector<unsigned char> bytes; };
std::map<Node*,PoseLedger> poses;
bool ApplyPose(void* user,std::uint64_t id,const LeaseSpec& spec,std::uint64_t sequence,const void* bytes,std::size_t size) {
    auto* n=static_cast<Node*>(user);auto& pose=poses[n];
    Check(n->id!=0,"native host physical state is never overwritten by its own correction stream");
    const auto* view=n->room->Lease(n->id,id);
    Check(view && view->phase==Phase::active && SameSpec(view->spec,spec),"physical correction belongs to the exact active native lease");
    ++pose.calls;pose.sequence=sequence;pose.bytes.assign(static_cast<const unsigned char*>(bytes),static_cast<const unsigned char*>(bytes)+size);
    return !pose.fail;
}
void EnablePose(Room& room) {
    poses.clear();for(auto& n:room.nodes){auto hooks=Backend(n.get());hooks.ApplyPose=&ApplyPose;Check(n->protocol.Configure(hooks),"pose callback bound before native acquisition");}
}
std::vector<unsigned char> Payload(std::size_t size) {
    std::vector<unsigned char> bytes(size);for(std::size_t i=0;i<size;++i)bytes[i]=static_cast<unsigned char>(i*37+size);return bytes;
}
void PoseSuccess() {
    for(std::size_t size:{1u,127u,128u,129u,256u,300u,512u}) {
        Room r;EnablePose(r);Check(r.Request(),"pose owner request queued");r.Settle();const auto id=r.Active();
        auto bytes=Payload(size);
        Check(!r.nodes[1]->protocol.SubmitPose(id,bytes.data(),size,r.now),"controller ownership never grants native physics authority");
        Check(r.nodes[0]->protocol.SubmitPose(id,bytes.data(),size,r.now),"only the native host publishes a bounded physical correction");
        Check(poses[&*r.nodes[1]].calls==0,"queued chunks cannot mutate guest physics before delivery");r.Pump();
        for(unsigned n=1;n<3;++n)Check(poses[&*r.nodes[n]].calls==1 && poses[&*r.nodes[n]].sequence==1 && poses[&*r.nodes[n]].bytes==bytes,"all guests apply one complete identical typed physical payload");
        std::vector<Packet> replay;for(const auto& packet:r.history){const auto m=Unpack(packet);if(m.kind==Kind::Pose)replay.push_back(packet);}
        for(const auto& packet:replay)r.Deliver(packet);
        for(unsigned n=1;n<3;++n)Check(poses[&*r.nodes[n]].calls==1,"completed pose sequence replay never applies twice");
        r.Input();for(const auto& n:r.nodes)Check(n->inputs.back().sequence==1,"host input sequence and seed are independent of physics corrections");
        Check(r.nodes[0]->protocol.SubmitPose(id,bytes.data(),size,r.now),"second physical state uses its own monotonically increasing sequence");r.Pump();
        for(unsigned n=1;n<3;++n)Check(poses[&*r.nodes[n]].calls==2 && poses[&*r.nodes[n]].sequence==2,"second complete state applies once");
    }
}
void PoseAtomic() {
    Room r;EnablePose(r);Check(r.Request(),"atomic physical state request queued");r.Settle();const auto id=r.Active();auto bytes=Payload(512);
    Check(r.nodes[0]->protocol.SubmitPose(id,bytes.data(),bytes.size(),r.now),"complete physical state fits one real SDK envelope");
    std::vector<Packet> packets;while(!r.queue.empty()){packets.push_back(r.queue.front());r.queue.pop_front();}
    for(const auto& packet:packets)for(std::size_t size=0;size<packet.bytes.size();++size)
        Check(r.nodes[packet.to]->protocol.Receive(r.Peer(packet.to,packet.from),r.nodes[packet.from]->puid.c_str(),packet.bytes.data(),size,r.now)==(size>=4),"truncated atomic envelope is refused before any physical mutation");
    for(unsigned n=1;n<3;++n)Check(!poses[&*r.nodes[n]].calls,"partial wire state never updates a physical body or motion cache");
    for(const auto& packet:packets)r.Deliver(packet);
    for(unsigned n=1;n<3;++n)Check(poses[&*r.nodes[n]].calls==1 && poses[&*r.nodes[n]].bytes==bytes,"only complete authenticated state commits all physical caches");
}
void PoseAuthAndBinding() {
    for(unsigned fault=0;fault<8;++fault) {
        Room r;EnablePose(r);Check(r.Request(),"bound pose request queued");r.Settle();const auto id=r.Active();auto bytes=Payload(64);
        Check(r.nodes[0]->protocol.SubmitPose(id,bytes.data(),bytes.size(),r.now),"auth fixture physical correction queued");
        auto packet=r.Find(Kind::Pose,0,1);r.queue.clear();auto m=Unpack(packet);
        bool ignored=false;
        switch(fault) {
        case 0:packet.from=2;ignored=true;break;
        case 1:--m.epoch;ignored=true;break;
        case 2:++m.lease;ignored=true;break;
        case 3:m.spec.target=Id(101);break;
        case 4:m.spec.requesterPlayer=r.nodes[2]->player;break;
        case 5:++m.spec.profileHash;break;
        case 6:++m.request;break;
        case 7:++m.sequence;break;
        }
        if(fault)packet=r.PacketOf(0,1,m);r.Deliver(packet);r.Pump();
        Check(!poses[&*r.nodes[1]].calls,"sender, epoch, exact target/source/profile/nonce and pose sequence bind every state");
        if(ignored)Check(r.nodes[0]->protocol.ActiveCount()==1,"foreign or retired corrections cannot revoke the current body");
        else r.Closed(id,Result::InvalidInput);
    }
    Room r;EnablePose(r);Check(r.Request(),"owner PUID pose fixture request queued");r.Settle();auto bytes=Payload(20);
    Check(r.nodes[0]->protocol.SubmitPose(r.Active(),bytes.data(),bytes.size(),r.now),"owner PUID pose queued");auto p=r.Find(Kind::Pose,0,1);auto m=Unpack(p);r.queue.clear();
    strcpy_s(m.spec.ownerPuid,r.nodes[2]->puid.c_str());p=r.PacketOf(0,1,m);r.Deliver(p);r.Pump();r.Closed(1,Result::InvalidInput);
}
void PoseFailure() {
    {Room r;EnablePose(r);Check(r.Request(),"failed guest physics request queued");r.Settle();auto bytes=Payload(300);poses[&*r.nodes[2]].fail=true;
     Check(r.nodes[0]->protocol.SubmitPose(r.Active(),bytes.data(),bytes.size(),r.now),"native correction submitted before guest failure known");r.Pump();r.Closed(1,Result::ApplyFailed);}
    {Room r;EnablePose(r);Check(r.Request(),"partial send request queued");r.Settle();auto bytes=Payload(300);r.failFrom=0;r.failTo=2;r.failKind=Kind::Pose;
     Check(!r.nodes[0]->protocol.SubmitPose(r.Active(),bytes.data(),bytes.size(),r.now),"any partial physical send failure ends the whole lease");r.Pump();r.Closed(1,Result::SendFailed);
     Check(poses[&*r.nodes[1]].calls<=1 && !poses[&*r.nodes[2]].calls,"complete state may reach one peer before another send fails; all native leases still restore");}
    {Room r;EnablePose(r);auto bytes=Payload(20);Check(!r.nodes[0]->protocol.SubmitPose(1,bytes.data(),bytes.size(),r.now),"no pose before committed native activation");}
    for(std::size_t size:{0u,513u}) {Room r;EnablePose(r);Check(r.Request(),"invalid pose bounds request queued");r.Settle();auto bytes=Payload(513);
     Check(!r.nodes[0]->protocol.SubmitPose(r.Active(),bytes.data(),size,r.now),"unbounded pose cannot write native memory");r.Pump();r.Closed(1,Result::InvalidInput);}
    {Room r;EnablePose(r);Check(r.Request(),"chunk ordering request queued");r.Settle();auto bytes=Payload(300);
     Check(r.nodes[0]->protocol.SubmitPose(r.Active(),bytes.data(),bytes.size(),r.now),"atomic physical envelope queued");auto p=r.Find(Kind::Pose,0,1);r.queue.clear();
     p.bytes[50]=3;p.bytes[48]=1;r.Deliver(p);r.Pump();Check(r.Lease(0,1)->phase==Phase::active && !poses[&*r.nodes[1]].calls,"noncanonical legacy chunk metadata is consumed without physical mutation");}
    {Room r;EnablePose(r);Check(r.Request(),"restore retry physical request queued");r.Settle();auto bytes=Payload(20);poses[&*r.nodes[2]].fail=true;r.nodes[2]->failRestore=true;
     Check(r.nodes[0]->protocol.SubmitPose(r.Active(),bytes.data(),bytes.size(),r.now),"correction queued before physical fault");r.Pump();
     Check(r.Lease(2,1)->phase==Phase::restoring && !r.Lease(2,1)->localRestored && r.nodes[2]->native[1].suppressed,"physics failure retains native baseline while restoration fails");
     r.nodes[2]->failRestore=false;r.Step();r.Closed(1,Result::RestoreFailed);}
}
void PoseIndependentRestoration() {
    Room r;EnablePose(r);Check(r.Request(),"first independent physical lease queued");r.Settle();
    Check(r.Request(2,101),"second player's independent physical lease queued");r.Settle();
    Check(r.Lease(0,1)->phase==Phase::active && r.Lease(0,2)->phase==Phase::active,"two independently owned native bodies active");
    r.dropFrom=2;r.dropTo=0;r.dropKind=Kind::Restored;
    Check(r.nodes[2]->protocol.Exit(2,r.now),"second player exits while first keeps moving");r.Pump();
    Check(r.Lease(0,2)->phase==Phase::restoring && !r.nodes[0]->protocol.Ready(),"new admission waits for second player's outstanding restore ACK");
    auto bytes=Payload(232);Check(r.nodes[0]->protocol.SubmitPose(1,bytes.data(),bytes.size(),r.now),"unrelated restoration never interrupts an already active body's physical correction");r.Pump();
    Check(r.Lease(0,1)->phase==Phase::active && poses[&*r.nodes[1]].calls==1 && poses[&*r.nodes[2]].calls==1,"first body stays controlled on every peer while second restores");
    r.dropFrom=99;r.Step();Check(r.Lease(0,2)->phase==Phase::closed && r.Lease(0,1)->phase==Phase::active,"finishing the independent restoration preserves the live controller");
}
void PoseCodec() {
    Message m;m.kind=Kind::Pose;m.epoch=1;m.lease=2;m.request=3;m.sequence=1;m.spec.profileHash=kProfile;m.spec.requesterPlayer=Player(10);m.spec.target=Id(100);strcpy_s(m.spec.ownerPuid,"player-1");m.inputSize=4;m.capability=1u<<16;
    std::vector<unsigned char> bytes(EncodedSize(m));Check(Encode(m,bytes.data(),bytes.size()),"pose is an explicit canonical ECTL kind");
    auto bad=m;bad.seed=1;Check(!Encode(bad,bytes.data(),bytes.size()),"pose cannot claim input seed authority");
    bad=m;bad.capability=0;Check(!Encode(bad,bytes.data(),bytes.size()),"zero compound count rejected");
    bad=m;bad.capability=5u<<16;Check(!Encode(bad,bytes.data(),bytes.size()),"oversized compound count rejected");
    bad=m;bad.capability=(2u<<16)|2;Check(!Encode(bad,bytes.data(),bytes.size()),"out-of-range chunk index rejected");
    bad=m;bad.capability=2u<<16;Check(!Encode(bad,bytes.data(),bytes.size()),"nonfinal short chunk rejected");
    bad=m;bad.inputSize=0;Check(!Encode(bad,bytes.data(),bytes.size()),"empty physical state rejected");
}
}
int main() {ExistingEnemyControlProtocolTests();PoseCodec();PoseSuccess();PoseAtomic();PoseAuthAndBinding();PoseFailure();PoseIndependentRestoration();std::printf("enemy_control_pose_protocol: %d checks passed\n",checks);return 0;}
