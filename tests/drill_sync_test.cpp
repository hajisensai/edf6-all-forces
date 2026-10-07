// Run the production receive/frame/bone-pose path with fake vehicle memory, not a second flight implementation.
#include "../src/drill.cpp"
#include <cstdio>
#include <cstdlib>

namespace crew {
unsigned char imageBytes[0x17DAE00]{};
unsigned char* image=imageBytes;
Config config{};
ULONGLONG now=1000,frameNow=1;
bool session=true,authority=false;
int sent=0,charges=0,jets=0;
std::int32_t driver=42;
drill_net::State lastSent;
unsigned char spin[0x110]{},parentBone[0x110]{},markerBone[0x110]{};
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept { return now; }
ULONGLONG GameFrame() noexcept { return frameNow; }
float GameStep(ULONGLONG ms) noexcept { return static_cast<float>(ms)/1000.0f; }
bool DrillCharge(const unsigned char*,const float*,const float*,float) noexcept { ++charges;return true; }
void FlareFlames(const unsigned char*,const float (*)[3],const float (*)[3],int count,ULONGLONG) noexcept { jets=count; }
float MapRay(const float*,const float*,float*) noexcept { return -1.0f; }
bool VisitEnemies(const unsigned char*,EnemyVisitor,void*) noexcept { return false; }
bool MapHoldsKeys() noexcept { return true; }
unsigned char* BoneRecord506(const unsigned char*,const wchar_t* name) noexcept {
    return !std::wcscmp(name,kSpinBone) ? spin : !std::wcscmp(name,kParentBone) ? parentBone : markerBone;
}
bool InSession() noexcept { return session; }
bool IsOnlineAuthority(const void*) noexcept { return authority; }
bool InstallDrillNet() noexcept { return true; }
bool DrillNetSend(unsigned char*,drill_net::State s) noexcept { ++sent;lastSent=s;return true; }
std::int32_t DrillNetController(unsigned char*) noexcept { return driver; }
}
namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
}
int main() {
    using namespace crew;
    alignas(16) unsigned char vehicle[0x1400]{},seat[0x340]{};
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    Put<const void*>(vehicle,0,image+kVt505);Put<const void*>(vehicle,kSelfCtrl,vehicle+0x1300);
    Put<unsigned char*>(vehicle,kSeats,seat);Put<std::uint64_t>(vehicle,kSeatCount,1);
    Put<std::uint16_t>(vehicle,0x128,1);Put<void*>(vehicle,kModelInst+kInstBones506,spin);
    std::memcpy(vehicle+kMatrix,identity,64);std::memcpy(parentBone+kBoneWorld,identity,64);
    std::memcpy(spin+kBoneLocal,identity,64);
    triggerOk=true;
    drill_net::State s;s.sender=7;s.sequence=1;s.controller=driver;s.phase=drill_net::Phase::out;
    s.pos[0]=10;s.pos[1]=5;s.pos[2]=20;s.dir[2]=s.axis[2]=1;s.rpm=300;s.speed=90;s.flown=20;
    DrillInput(vehicle); // no local player: returns early, but must not prevent receive/frame presentation
    DrillNetReceived(vehicle,s);
    DrillFrame(vehicle);
    Drill* d=DrillTank(vehicle);
    Check(d && !d->player && d->flight==Flight::out,"remote copy launches despite no local-player input");
    Check(At<float>(spin,kBoneLocal+12*4)==10 && At<float>(spin,kBoneLocal+14*4)==20,
          "remote flight reaches the actual spin bone local pose");
    Check(jets==1 && charges==0 && sent==0,"remote draws jet but neither damages nor echoes the event");
    const float position=d->pos[2];
    now+=50;++frameNow;DrillFrame(vehicle);
    Check(d->pos[2]==position,"remote does not run an independent map collision or flight simulation");
    s.sequence=4;s.phase=drill_net::Phase::back;s.pos[2]=100;s.flown=100;s.backAgeMs=200;
    DrillNetReceived(vehicle,s);
    Check(d->flight==Flight::back && d->flown==100 && d->backAt==now-200,"snapshot recovers missing flight states and integrator");
    s.sequence=3;s.phase=drill_net::Phase::out;DrillNetReceived(vehicle,s);
    Check(d->flight==Flight::back,"reordered launch does not restart return");
    s.sequence=5;s.phase=drill_net::Phase::home;DrillNetReceived(vehicle,s);
    now+=50;++frameNow;DrillFrame(vehicle);
    Check(d->flight==Flight::home && jets==0 && At<float>(spin,kBoneLocal+14*4)==0,"catch restores hull bone and extinguishes jet");
    driver=99;s.sender=8;s.controller=99;s.sequence=1;s.phase=drill_net::Phase::out;DrillNetReceived(vehicle,s);
    s.sender=7;s.controller=42;s.sequence=6;s.phase=drill_net::Phase::home;DrillNetReceived(vehicle,s);
    Check(d->flight==Flight::out,"old driver cannot overwrite new driver's flight");
    driver=-1;s.sender=9;s.controller=-1;s.sequence=1;s.phase=drill_net::Phase::back;
    DrillNetReceived(vehicle,s);
    s.sender=8;s.controller=99;s.sequence=50;s.phase=drill_net::Phase::out;DrillNetReceived(vehicle,s);
    Check(d->flight==Flight::back,"old client newer packet cannot override host empty/NPC takeover");
    driver=100;s.sender=10;s.controller=100;s.sequence=1;s.phase=drill_net::Phase::out;DrillNetReceived(vehicle,s);
    s.sender=9;s.controller=-1;s.sequence=2;s.phase=drill_net::Phase::home;DrillNetReceived(vehicle,s);
    Check(d->flight==Flight::out,"old host catch cannot override a new registered driver");
    authority=true;DrillNetReceived(vehicle,s);
    Check(d->flight==Flight::out,"authority ignores incoming state");
    d->flight=Flight::home;d->sentAt=0;now+=50;++frameNow;DrillFrame(vehicle);
    const int first=sent;
    now+=499;++frameNow;DrillFrame(vehicle);Check(sent==first,"home resend is bounded");
    now+=1;++frameNow;DrillFrame(vehicle);Check(sent==first+1 && lastSent.phase==drill_net::Phase::home,"lost catch recovers from periodic home state");
    d->flight=Flight::out;d->sentAt=now;d->sentFlight=Flight::home;SendState(vehicle,*d,now);
    Check(sent==first+2,"phase transition sends immediately");
    now+=50;SendState(vehicle,*d,now);Check(sent==first+3,"flight snapshot retries every 50 game ms");
    session=false;d->launchAsked=true;now+=50;++frameNow;DrillFrame(vehicle);
    Check(d->flight==Flight::home && !d->launchAsked && !d->networked,"session exit clears flight and queued launch");
    session=true;authority=false;Put<const void*>(vehicle,kSelfCtrl,vehicle+0x1310);
    s.sender=7;s.controller=driver;s.sequence=1;s.phase=drill_net::Phase::out;DrillNetReceived(vehicle,s);
    d=DrillTank(vehicle);Check(d->flight==Flight::out,"reused object address has fresh ObjRef replay state");
    Put<const void*>(vehicle,0,image);s.sequence=2;s.phase=drill_net::Phase::home;DrillNetReceived(vehicle,s);
    Check(d->flight==Flight::out,"wrong vehicle class cannot receive drill state");
    ResetDrills();Check(!drills[0].ref,"mission reset discards network watermarks with object state");
    std::printf("drill_sync_test: %d checks passed\n",checks);
}
