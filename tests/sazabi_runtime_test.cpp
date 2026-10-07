// Exercise the production enemy visitor and pose consumers, without loading EDF.dll.
#include "../src/sazabi.cpp"
#include <cstdio>

namespace crew {
unsigned char* image=nullptr;
Config config{};
bool netSession=false,netAuthority=true;
std::int32_t netDriver=42;
ULONGLONG testNow=1000;
int emcCalls=0,damageCalls=0,enemyVisits=0,soundCalls=0;
sazabi_net::State lastNetwork;
PlayerFix player{};
const void* visibleObjects[4]{};
float visiblePositions[4][3]{};
int visibleCount=0;
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
unsigned char* BoneRecord506(const unsigned char*,const wchar_t*) noexcept { return nullptr; }
bool SeatPoint(const unsigned char*,unsigned,float*,float*) noexcept { return false; }
float GroundClearance(const float*) noexcept { return kNoGround; }
// Unused game entry points from the included runtime translation unit.
ULONGLONG GameMs() noexcept { return testNow; }
ULONGLONG GameFrame() noexcept { return 1; }
float GameStep(ULONGLONG) noexcept { return 1.0f/60.0f; }
void NozzleFlames(const unsigned char*,const float (*)[16],int,const float (*)[2],const float*,ULONGLONG) noexcept {}
void FlareFlames(const unsigned char*,const float (*)[3],const float (*)[3],int,ULONGLONG) noexcept {}
PluginBody BodyOf(const void*) noexcept { return PluginBody::sazabi; }
bool Body506Ok() noexcept { return true; }
void BoardingRequest(unsigned char*) noexcept {}
bool DrillCharge(const unsigned char*,const float*,const float*,float) noexcept { return false; }
bool EmcRoundReady(EmcRound) noexcept { return false; }
RoundObj EmcFire(EmcRound,const unsigned char*,const float*,const float*,float damage) noexcept {
    ++emcCalls;if(damage>0)++damageCalls;return {};
}
bool RoundSteer(const RoundObj&,const float*,const float*) noexcept { return false; }
bool RoundSize(const RoundObj&,float) noexcept { return false; }
void RoundDrop(RoundObj&) noexcept {}
unsigned char* PlayerHuman() noexcept { return nullptr; }
bool HumanOnFoot(const unsigned char*) noexcept { return false; }
float MapRay(const float*,const float*,float*) noexcept { return -1.0f; }
bool VisitEnemies(const unsigned char*,EnemyVisitor visitor,void* context) noexcept {
    ++enemyVisits;
    for(int i=0;i<visibleCount;++i)visitor(context,visibleObjects[i],visiblePositions[i]);
    return visibleCount>0;
}
bool MapHoldsKeys() noexcept { return true; }
void BodyAttitude(const unsigned char*,const float*,const float*,float,float,float*) noexcept {}
int WeaponLock(const unsigned char*,float*,float*) noexcept { return 0; }
void SazabiSfx(SzSfx,const float*) noexcept { ++soundCalls; }
void SazabiLoop(SzLoop,const float*,const float*,float) noexcept {}
bool InSession() noexcept { return netSession; }
bool IsOnlineAuthority(const void*) noexcept { return netAuthority; }
bool InstallSazabiNet() noexcept { return true; }
std::int32_t SazabiNetController(unsigned char*) noexcept { return netDriver; }
std::int32_t SazabiNetReference(const void*) noexcept { return -1; }
bool SazabiNetSend(unsigned char*,sazabi_net::State s) noexcept { lastNetwork=s;return true; }
}

namespace {
int checks=0,failures=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
bool Near(const float* a,const float* b) {
    for(int i=0;i<3;++i)if(std::fabs(a[i]-b[i])>1e-4f)return false;
    return true;
}
}

int main() {
    using namespace crew;
    Enemies enemies{};
    int objects[kMostTargets+2]{};
    for(int i=0;i<kMostTargets;++i) {
        const float at[3]={static_cast<float>(100+i),0,0};
        SeeEnemy(&enemies,&objects[i],at);
    }
    Check(enemies.n==kMostTargets,"fills every target slot");
    Check(enemies.obj[kMostTargets-1]==&objects[kMostTargets-1],"last slot holds a real enemy");
    const float nearPoint[3]={1,0,0},farPoint[3]={200,0,0};
    SeeEnemy(&enemies,&objects[kMostTargets],nearPoint);
    Check(enemies.n==kMostTargets && enemies.obj[0]==&objects[kMostTargets],"a late near enemy replaces the farthest");
    Check(enemies.obj[kMostTargets-1]==&objects[kMostTargets-2],"only the farthest enemy was evicted");
    SeeEnemy(&enemies,&objects[kMostTargets+1],farPoint);
    Check(enemies.obj[0]==&objects[kMostTargets] && enemies.obj[kMostTargets-1]==&objects[kMostTargets-2],"a late farther enemy leaves the list unchanged");
    bool sorted=true,allReal=true;
    for(int i=0;i<enemies.n;++i){allReal=allReal && enemies.obj[i];if(i)sorted=sorted && enemies.d2[i-1]<=enemies.d2[i];}
    Check(sorted && allReal,"a full target list stays sorted and contains no phantom origin enemy");

    // Real Pose writes and BoneAt/DockPoint reads, interleaved across two different rigs/poses.
    // Both bone arrays are already resolved, so no game model lookup is needed.
    alignas(16) unsigned char vehicle[kModelInst506+kInstBones506+sizeof(void*)]{};
    alignas(16) unsigned char records[2][sazabi::kBoneCount][kBoneLocal506+64]{};
    Mech a{},b{};
    for(int j=0;j<2;++j) {
        Mech& m=j==0 ? a : b;
        m.rigOk=true;
        for(int i=0;i<16;++i)m.root[i]=i%5==0 ? 1.0f : 0.0f;
        for(int i=0;i<sazabi::kBoneCount;++i) {
            m.rec[i]=records[j][i];
            m.rig.joint[i][0]=static_cast<float>(i%3)*(j==0 ? 1.0f : 4.0f);
            m.rig.joint[i][1]=static_cast<float>(i)*(j==0 ? 0.5f : 2.0f);
            m.rig.joint[i][2]=static_cast<float>(i%5);
        }
    }
    b.pose.aim=1.0f;b.pose.aimPitch=0.5f;b.pose.guard=1.0f;
    Pose(a,vehicle);
    float muzzle[3],dock[3],blade[3];
    BoneAt(a,sazabi::kMuzzle,nullptr,muzzle);
    DockPoint(a,0,dock);
    BoneAt(a,sazabi::kAxeBlade,nullptr,blade);
    Pose(b,vehicle);
    float got[3],other[3];
    BoneAt(a,sazabi::kMuzzle,nullptr,got);
    BoneAt(b,sazabi::kMuzzle,nullptr,other);
    Check(!Near(muzzle,other),"second mech has a distinguishable pose");
    Check(Near(muzzle,got),"aim and NPC muzzle remain this mech's after another mech is posed");
    DockPoint(a,0,got);
    Check(Near(dock,got),"funnel dock remains this mech's after another mech is posed");
    BoneAt(a,sazabi::kAxeBlade,nullptr,got);
    Check(Near(blade,got),"melee blade remains this mech's after another mech is posed");

    // Run the production target collection and funnel flight: A vanishes while a funnel still attacks B.
    visibleCount=4;
    for(int i=0;i<visibleCount;++i) {
        visibleObjects[i]=&objects[i];
        visiblePositions[i][0]=static_cast<float>(50+i*10);
        visiblePositions[i][1]=13.0f;
    }
    FlyFunnels(a,vehicle,false,1.0f/60.0f);
    Check(a.arms.funnelSlot[1]==&objects[1],"B initially occupies target slot 1");
    auto& onB=a.arms.funnels[0];
    onB.phase=FunnelPhase::station;onB.target=1;onB.wait=1.0f;
    std::memcpy(onB.at,visiblePositions[1],12);
    float bPosition[3];std::memcpy(bPosition,onB.at,12);
    auto& onA=a.arms.funnels[1];
    onA.phase=FunnelPhase::station;onA.target=0;onA.wait=1.0f;
    std::memcpy(onA.at,visiblePositions[0],12);
    for(int i=0;i<3;++i){visibleObjects[i]=visibleObjects[i+1];std::memcpy(visiblePositions[i],visiblePositions[i+1],12);}
    visibleCount=3;
    FlyFunnels(a,vehicle,false,1.0f/60.0f);
    Check(a.arms.funnelSlot[0]==&objects[1] && onB.target==0,"B's funnel follows B to its compacted slot");
    Check(onB.phase==FunnelPhase::station && Near(onB.at,bPosition),"B's funnel stays on station without teleporting to C");
    Check(onA.phase==FunnelPhase::transit,"the lost A target is reacquired through a flight path");
    std::printf("Sazabi runtime: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
