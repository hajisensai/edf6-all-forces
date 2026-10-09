// The launched drill (2026-10-09 feedback, docs/feedback-2026-10-09-ground.md) through the production DrillInput /
// DrillFrame with fake vehicle memory: it bites a ground enemy on its way OUT (its reach is the hull's box carried
// along, not a cylinder round an axis 4.21 m up), its spin is the launch's and never the trigger's, and the weapon's
// live round count says whether the drill is on the hull (1) or launched (0).
#include "../src/drill.cpp"
#include <cstdio>
#include <cstdlib>

namespace crew {
unsigned char imageBytes[0x17DAE00]{};
unsigned char* image=imageBytes;
Config config{};
ULONGLONG now=1000,frameNow=1;
int charges=0;
float lastAt[3]{};
unsigned char spin[0x110]{},parentBone[0x110]{},markerBone[0x110]{};
// One enemy on the ground: its root (object +0x90) on the ground, its lock point 1 m up.
alignas(16) unsigned char enemy[0x400]{};
float enemyLock[3]{};
bool enemyThere=false;
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept { return now; }
ULONGLONG GameFrame() noexcept { return frameNow; }
float GameStep(ULONGLONG ms) noexcept { return static_cast<float>(ms)/1000.0f; }
bool DrillCharge(const unsigned char*,const float*,const float* at,float) noexcept { ++charges;std::memcpy(lastAt,at,12);return true; }
void FlareFlames(const unsigned char*,const float (*)[3],const float (*)[3],int,ULONGLONG) noexcept {}
float MapRay(const float*,const float*,float*) noexcept { return -1.0f; }
bool VisitEnemies(const unsigned char*,EnemyVisitor visit,void* ctx) noexcept {
    if(enemyThere)visit(ctx,enemy,enemyLock);
    return true;
}
bool MapHoldsKeys() noexcept { return true; }
unsigned char* BoneRecord506(const unsigned char*,const wchar_t* name) noexcept {
    return !std::wcscmp(name,kSpinBone) ? spin : !std::wcscmp(name,kParentBone) ? parentBone : markerBone;
}
bool InSession() noexcept { return false; }
bool IsOnlineAuthority(const void*) noexcept { return true; }
bool InstallDrillNet() noexcept { return true; }
bool DrillNetSend(unsigned char*,drill_net::State) noexcept { return true; }
std::int32_t DrillNetController(unsigned char*) noexcept { return -1; }
}
namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
}
int main() {
    using namespace crew;
    config.drill=true;config.drillLaunch=true;config.drillMaxRpm=300.0f;config.drillSpinUpSec=1.8f;config.drillSpinDownSec=2.5f;
    config.drillDamage=2000.0f;config.drillBreak=600.0f;config.drillOverheatSec=30.0f;config.drillCoolSec=8.0f;
    config.drillResumeHeat=0.3f;config.drillKillCool=0.1f;config.drillLaunchButton=0x08;config.drillLaunchRange=60.0f;
    config.drillLaunchSpeed=70.0f;config.drillLaunchDamage=800.0f;config.drillLaunchHeat=0.12f;
    alignas(16) unsigned char vehicle[0x1400]{},seat[0x340]{},human[0x400]{},ctrlBlock[0x20]{},holder[0x40]{},weapon[0x1000]{};
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    Put<const void*>(vehicle,0,image+kVt505);Put<const void*>(vehicle,kSelfCtrl,vehicle+0x1300);
    Put<unsigned char*>(vehicle,kSeats,seat);Put<std::uint64_t>(vehicle,kSeatCount,1);
    Put<void*>(vehicle,kModelInst+kInstBones506,spin);
    std::memcpy(vehicle+kMatrix,identity,64);std::memcpy(parentBone+kBoneWorld,identity,64);
    std::memcpy(spin+kBoneLocal,identity,64);
    // This machine's player in seat 0 (edf::SeatRider: a live control block, a human with a pad and the player flag),
    // on a pad (seat +0x2B0 = 1, the launch button in the seat's button bits).
    Put<std::int32_t>(ctrlBlock,edf::kCtrlUses,1);Put<const void*>(seat,kSeatRiderCtrl,ctrlBlock);Put<const void*>(seat,kSeatRider,human);
    Put<const void*>(human,kHumanPad,human);Put<unsigned char>(human,kHumanPlayer,1);
    Put<unsigned char>(seat,kSeatPad,1);
    // Its one weapon holder, the bit with the stock magazine of 25 still in it (an old install's).
    unsigned char* holders[1]={holder};
    Put<unsigned char**>(seat,kSeatWeapons,holders);Put<std::uint64_t>(seat,kSeatWeaponCount,1);
    Put<unsigned char*>(holder,kHolderWeapon,weapon);Put<std::int32_t>(weapon,kWeaponAmmo,25);
    triggerOk=true;
    auto frame=[&](bool trigger,bool button) {
        Put<float>(seat,kSeatTrigger,trigger ? 1.0f : 0.0f);
        Put<std::uint16_t>(seat,kSeatButtons,button ? 0x08 : 0);
        DrillInput(vehicle);DrillFrame(vehicle);
        now+=16;++frameNow;
    };
    frame(false,false);
    Drill* d=DrillTank(vehicle);
    Check(d && d->player && d->flight==Flight::home,"the player's drill tank, on the hull");
    Check(At<std::int32_t>(weapon,kWeaponAmmo)==1,"on the hull the bit shows its one round, not the cannon's 25");
    // The enemy: 30 m ahead on the ground, 0.5 m aside; its lock point 1 m up (3.2 m under the drill's axis).
    Put<float>(enemy,kPosition,0.5f);Put<float>(enemy,kPosition+4,0.0f);Put<float>(enemy,kPosition+8,30.0f);
    enemyLock[0]=0.5f;enemyLock[1]=1.0f;enemyLock[2]=30.0f;enemyThere=true;
    // Launched with the trigger never pulled: the drill spins at the top at once.
    frame(false,true);
    Check(d->flight==Flight::out,"the launch button sends the drill out");
    Check(d->rpm==config.drillMaxRpm,"launched without the trigger: the jet spins it at the top at once");
    Check(At<std::int32_t>(weapon,kWeaponAmmo)==0,"launched: the round is away");
    // Out: the trigger pulled and let go every other frame changes neither its RPM nor its turn a frame.
    int outBites=0;
    float lastAngle=d->angle;
    bool spinSteady=true;
    for(int i=0;i<200 && d->flight==Flight::out;++i) {
        const int before=charges;
        frame((i&1)!=0,false);
        if(d->flight==Flight::out)outBites+=charges-before;
        const float turned=std::fmod(d->angle-lastAngle+4.0f*kPi,2.0f*kPi);
        lastAngle=d->angle;
        if(d->rpm!=config.drillMaxRpm || std::fabs(turned-kSpinStepMost)>1e-4f)spinSteady=false;
    }
    Check(spinSteady,"in flight the spin is the launch's: the trigger neither starts nor stops it");
    Check(outBites>0 && std::fabs(lastAt[2]-30.0f)<1e-3f && std::fabs(lastAt[1]-1.0f)<1e-3f,
          "on its way out the drill bites the enemy on the ground ahead (its lock point)");
    Check(d->flight==Flight::back,"out to its range, then back");
    // Back to the hull: caught, the round shows again.
    enemyThere=false;
    for(int i=0;i<2000 && d->flight!=Flight::home;++i)frame(false,false);
    Check(d->flight==Flight::home && At<std::int32_t>(weapon,kWeaponAmmo)==1,"caught: the round is back on the hull");
    // An enemy well clear of the drill's reach (6 m aside) is not bitten in flight.
    Put<float>(enemy,kPosition,6.0f);enemyLock[0]=6.0f;enemyThere=true;
    frame(false,true);
    const int before=charges;
    for(int i=0;i<200 && d->flight==Flight::out;++i)frame(false,false);
    Check(charges==before,"an enemy 6 m aside of the flight line is out of its reach");
    std::printf("drill_launch_test: %d checks passed\n",checks);
}
