// Exercise the production enemy visitor and pose consumers, without loading EDF.dll.
#include "../src/sazabi.cpp"
#include <cstdio>

namespace crew {
unsigned char* image=nullptr;
Config config{};
PlayerFix player{};
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
unsigned char* BoneRecord506(const unsigned char*,const wchar_t*) noexcept { return nullptr; }
bool SeatPoint(const unsigned char*,unsigned,float*,float*) noexcept { return false; }
float GroundClearance(const float*) noexcept { return kNoGround; }
// Unused game entry points from the included runtime translation unit.
ULONGLONG GameMs() noexcept { return 1000; }
ULONGLONG GameFrame() noexcept { return 1; }
float GameStep(ULONGLONG) noexcept { return 1.0f/60.0f; }
void NozzleFlames(const unsigned char*,const float (*)[16],int,const float (*)[2],const float*,ULONGLONG) noexcept {}
void FlareFlames(const unsigned char*,const float (*)[3],const float (*)[3],int,ULONGLONG) noexcept {}
PluginBody BodyOf(const void*) noexcept { return PluginBody::sazabi; }
bool Body506Ok() noexcept { return true; }
void BoardingRequest(unsigned char*) noexcept {}
bool DrillCharge(const unsigned char*,const float*,const float*,float) noexcept { return false; }
bool EmcRoundReady(EmcRound) noexcept { return false; }
RoundObj EmcFire(EmcRound,const unsigned char*,const float*,const float*,float) noexcept { return {}; }
bool RoundSteer(const RoundObj&,const float*,const float*) noexcept { return false; }
bool RoundSize(const RoundObj&,float) noexcept { return false; }
void RoundDrop(RoundObj&) noexcept {}
unsigned char* PlayerHuman() noexcept { return nullptr; }
bool HumanOnFoot(const unsigned char*) noexcept { return false; }
float MapRay(const float*,const float*,float*) noexcept { return -1.0f; }
bool VisitEnemies(const unsigned char*,EnemyVisitor,void*) noexcept { return false; }
bool MapHoldsKeys() noexcept { return true; }
void BodyAttitude(const unsigned char*,const float*,const float*,float,float,float*) noexcept {}
int WeaponLock(const unsigned char*,float*,float*) noexcept { return 0; }
void SazabiSfx(SzSfx,const float*) noexcept {}
void SazabiLoop(SzLoop,const float*,const float*,float) noexcept {}
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
    std::printf("Sazabi runtime: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
