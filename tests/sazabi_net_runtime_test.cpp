// The same production translation unit and game stand-ins used by the runtime regression, with network-specific cases.
#define main ExistingRuntimeChecks
#include "sazabi_runtime_test.cpp"
#undef main
int main() {
    using namespace crew;
    config.enabled=config.sazabi=true;netSession=true;netAuthority=false;installed=true;
    alignas(16) unsigned char vehicle[0x2100]{},control[16]{};
    alignas(16) unsigned char records[sazabi::kBoneCount][0x110]{};
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    Put<void*>(vehicle,kSelfCtrl,control);Put<LONG>(control,8,1);Put<std::uint16_t>(vehicle,0x128,1);
    Put<void*>(vehicle,kBody,control);std::memcpy(vehicle+kMatrix,identity,64);
    Mech* m=Make(vehicle);m->rigOk=true;
    for(int i=0;i<sazabi::kBoneCount;++i) {
        m->rec[i]=records[i];std::memcpy(records[i]+kBoneWorld506,identity,64);
        m->rig.joint[i][1]=static_cast<float>(i)*0.2f;
    }
    Pose(*m,vehicle);float neutral[16];std::memcpy(neutral,records[sazabi::kUpperArmL]+kBoneLocal506,64);
    sazabi_net::State s;s.sender=17;s.sequence=1;s.controller=42;s.flags=sazabi_net::kDriven|sazabi_net::kAir;
    s.pose.guard=1;s.pose.swing=0.4f;s.pose.combo=1;s.pose.air=1;s.pose.aim=1;s.arms.guardShare=0.2f;
    s.funnels[0].phase=3;s.funnels[0].at[0]=100;s.funnels[0].at[1]=30;s.funnels[0].target=777;
    s.shots[0].sequence=1;s.shots[0].from[0]=100;s.shots[0].at[0]=140;
    s.sounds[0].sequence=1;s.sounds[0].kind=static_cast<std::uint32_t>(SzSfx::whoosh);
    m->arms.lockOn=m->arms.hasAssist=m->arms.lockKeyHeld=true;
    m->arms.lockOnObj=m->arms.assistObj=vehicle;
    SazabiNetReceived(vehicle,s);const int visits=enemyVisits;
    Check(!m->arms.lockOn && !m->arms.hasAssist && !m->arms.lockKeyHeld && !m->arms.lockOnObj,
          "remote authority discards the former pilot's local lock target and input latch");
    Drive(*m,vehicle,testNow);
    Check(m->net.remote && m->driven && !m->active,"remote pilot replays without owning physics");
    Check(m->pose.guard==1 && m->pose.swing==0.4f && m->pose.combo==1,"guard/swing/combo reach the production pose");
    Check(std::memcmp(neutral,records[sazabi::kUpperArmL]+kBoneLocal506,64)!=0,"replicated guard writes the shield arm's bone");
    Check(m->pose.funnelOut[0] && m->pose.funnelAt[0][0]==100 && m->arms.funnels[0].target==-1,
          "unknown async target id uses authoritative world position, never a local target-list index");
    Check(enemyVisits==visits && damageCalls==0 && emcCalls==1,"remote emits zero-damage beam without querying or attacking enemies");
    const int sounds=soundCalls;Drive(*m,vehicle,testNow);
    Check(emcCalls==1 && soundCalls==sounds,"one-shot beams and sounds are not replayed every frame");
    float lin[3]{11,12,13},ang[3]{21,22,23};m->active=true;m->frame=GameFrame();
    Check(!SazabiBodyStep(vehicle,lin,ang) && lin[0]==11 && ang[0]==21,"remote snapshot can never override stock body synchronization");
    m->net.remote=false;
    Check(!SazabiBodyStep(vehicle,lin,ang),"driver authority change blocks old physics even before the next replay frame");
    m->net.remote=true;
    alignas(16) unsigned char hit[0x80]{};Put<float>(hit,0x38,20);Put<float>(hit,0x50,100);
    MessageRestore restore{};SazabiMessage(vehicle,kMsgDamage,hit,&restore);
    Check(At<float>(hit,0x50)==20 && restore.was==100,"target owner uses driver's replicated shield share before native damage");
    Put<float>(hit,0x50,100);netDriver=99;restore={};SazabiMessage(vehicle,kMsgDamage,hit,&restore);
    Check(At<float>(hit,0x50)==100 && !restore.at,"old driver guard does not survive a seat epoch change");
    netDriver=42;testNow+=1600;SazabiMessage(vehicle,kMsgDamage,hit,&restore);
    Check(At<float>(hit,0x50)==100,"stale snapshot cannot leave an immortal shield");
    Drive(*m,vehicle,testNow);Check(!m->driven && m->arms.guard==0,"expired replay clears action state");
    s.sequence=2;s.shots[0].sequence=2;s.shots[0].age=60000;s.pose.guard=0;s.pose.swing=-1;
    SazabiNetReceived(vehicle,s);Drive(*m,vehicle,testNow);
    Check(emcCalls==1,"late join/old one-shot history does not spawn stale beams");
    netDriver=-1;s.sender=19;s.sequence=1;s.controller=-1;s.pose.guard=1;SazabiNetReceived(vehicle,s);
    s.sender=17;s.sequence=100;s.controller=42;s.pose.guard=0;SazabiNetReceived(vehicle,s);
    Check(m->arms.guard==1,"old client's newer packet cannot overwrite host takeover");
    netAuthority=true;Drive(*m,vehicle,testNow);
    Check(!m->net.remote,"host takeover leaves replay mode");
    netSession=false;Drive(*m,vehicle,testNow);
    Check(!m->net.networked && !m->net.remote,"session exit clears network state");
    ResetSazabi();Check(Find(vehicle)==nullptr,"mission reset discards object-bound replay and watermarks");
    std::printf("sazabi_net_runtime_test: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
