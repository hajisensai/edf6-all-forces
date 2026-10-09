// Actual HeliCommand -> command travel gate -> SelectMode -> FlyMode, no game or installed files.
// Compile /Gy and link /OPT:REF with edf6common and user32; unused game entrypoints are discarded.
#include "../src/heli.cpp"
#include <cstdio>
#include <cstdlib>

namespace crew {
bool NpcDriver(const unsigned char* v) noexcept { return v && SeatCount(v)>0 && SeatRider(SeatAt(const_cast<unsigned char*>(v),0))==Rider::dummy; }

unsigned char* image=nullptr;
// Offline (online_authority.h): no session, every heli run here, the copies' owner unchanged.
bool gunnerFixture=false,gunnerSession=false,gunnerReady=true;
int gunnerQueries=0;
bool InSession() noexcept { return gunnerSession; }
bool OnlineRunsHere(const void*) noexcept { return true; }
online::CopyOwner SetSpawnOwner(online::CopyOwner owner) noexcept { return owner; }
PlayerFix player{};
Config commandConfig{};
const Config& Cfg() noexcept { return commandConfig; }
ULONGLONG GameMs() noexcept { return 10000; }
ULONGLONG GameFrame() noexcept { return 600; }
void Log(const char*,...) noexcept {}
const wchar_t* WeaponFile(const unsigned char*,std::size_t* length) noexcept {
    constexpr wchar_t file[]=L"V_410HELI_GATLING01.SGO";
    *length=sizeof(file)/sizeof(file[0])-1;
    return L"V_410HELI_GATLING01.SGO";
}
bool CommandVehicleLive(const ObjRef& r) noexcept { return static_cast<bool>(r); }
// Fail closed if this command-only scenario reaches any unavailable game service.
void MissingDependency() noexcept { void(*volatile stop)()=std::abort;stop(); }
void SuppressBump(bool) noexcept { MissingDependency(); }
bool LastViewProj(float*) noexcept { MissingDependency();return false; }
bool CameraRay(float*,float*) noexcept { MissingDependency();return false; }
void SetObjectTeam(unsigned char*,std::int32_t) noexcept { MissingDependency(); }
bool IsJet(const void*) noexcept { MissingDependency();return false; }
bool JetInLine(const float*,const float*,const void*) noexcept { MissingDependency();return false; }
void JetFrame(unsigned char*) noexcept { MissingDependency(); }
unsigned char* HeliLaunch(HeliBody,const float*,const float*) noexcept { MissingDependency();return nullptr; }
PluginBody BodyOf(const void*) noexcept { MissingDependency();return PluginBody{}; }
bool IsSub(const void*) noexcept { MissingDependency();return false; }
bool SupportAircraftOwned(const void*) noexcept { return false; }   // no support deployment in this test
bool SubDeck(const float*,float*) noexcept { MissingDependency();return false; }
float SubHullGap(const float*) noexcept { MissingDependency();return 0.0f; }
bool IsPlayerJet(const void*) noexcept { MissingDependency();return false; }
bool InstallNpcGunnerAim() noexcept { MissingDependency();return false; }
bool NpcGunnerAimReady() noexcept { return gunnerReady; }
bool AiGunner(const unsigned char*,const unsigned char*) noexcept {
    if(!gunnerFixture)MissingDependency();++gunnerQueries;return false;
}
bool IsSazabi(const void*) noexcept { return false; }
namespace jet { int LockersOf(const void*,float (*)[3],int) noexcept { MissingDependency();return 0; } }
int MissilesHomingAt(const float*,float,float (*)[3],int) noexcept { MissingDependency();return 0; }
unsigned char* PlayerHuman() noexcept { MissingDependency();return nullptr; }
bool FuelGauge(const void*,FuelReading*) noexcept { MissingDependency();return false; }
PlayArea MapPlayArea() noexcept { MissingDependency();return PlayArea{}; }
bool MoveAreaBox(float*,float*) noexcept { MissingDependency();return false; }
bool MapHoldsKeys() noexcept { MissingDependency();return false; }
bool ReadCommandUnit(const ObjRef&,const char*,const mapcmd::Command&,bool,CommandUnit*) noexcept { MissingDependency();return false; }
float GroundClearance(const float*) noexcept { MissingDependency();return 0.0f; }
float CeilingY() noexcept { MissingDependency();return 0.0f; }
float GameStep(ULONGLONG) noexcept { if(!gunnerFixture)MissingDependency();return 1.0f/60.0f; }
float ClosureIn(const float*,const float*,float,float,float,float,bool*) noexcept { MissingDependency();return 0.0f; }
Gpws GpwsOf(float,bool) noexcept { MissingDependency();return Gpws::none; }
}

int main() {
    using namespace crew;
    int checks=0;
    auto check=[&](bool ok,const char* what){++checks;if(!ok){std::printf("FAIL %s\n",what);std::exit(1);}};
    alignas(16) unsigned char vehicle[0x2100]{},ctrl[16]{},enemy[0x200]{};
    Put<void*>(vehicle,kSelfCtrl,ctrl);Put<int>(ctrl,8,1);Put<void*>(enemy,kSelfCtrl,ctrl);
    Heli& h=helis[0];h=Heli{};h.ref=ObjRef::Of(vehicle);h.seenFrame=GameFrame();h.seen=GameMs();
    h.type=&kHeliTypes[1];h.top=30.0f;h.stopDecel=10.0f;
    h.hold[0]=-100.0f;
    h.target=ObjRef::Of(enemy);h.circleUntil=GameMs()+10000;h.extend=true;
    check(static_cast<bool>(h.target),"fixture has a live previous target");
    const Command guard{Order::guard,{800.0f,0.0f,0.0f}};
    check(HeliCommand(vehicle,guard),"409 accepts guard command");
    check(h.cmdMoving && !h.target && !h.circleUntil && !h.extend,"new order clears old attack and requires arrival");
    float pos[3]={0.0f,80.0f,0.0f};
    Sense s{};s.pos=pos;s.type=h.type;s.fwd[2]=1.0f;s.dt=1.0f/60.0f;s.ms=GameMs();s.guardOrbit=true;
    s.dist=100.0f;s.aimRange=300.0f;s.rocketsLeft=true;
    s.engage=!CommandMoving(h,pos,h.post); // an enemy remains available, as in the reported screenshot
    check(!s.engage && SelectMode(h,s)==Mode::guard,"enemy cannot override travel to the new post");
    const Want first=FlyMode(h,s,SelectMode(h,s));
    check(first.vel[0]>0.0f,"production guard velocity heads toward the assigned point");
    int steps=0;
    for(;steps<3600 && CommandMoving(h,pos,h.post);++steps) {
        const Want w=FlyMode(h,s,Mode::guard);
        pos[0]+=w.vel[0]*s.dt;pos[2]+=w.vel[2]*s.dt;
    }
    check(steps<3600 && !h.cmdMoving,"production guard guidance arrives despite an available enemy");
    s.engage=true;
    check(SelectMode(h,s)==Mode::aim,"409 may attack again after reaching the post");
    pos[0]=guard.at[0]+200.0f;pos[2]=0;
    check(!CommandMoving(h,pos,h.post),"combat has hysteresis beyond the arrival ring");
    pos[0]=guard.at[0]+commandConfig.heliRange+1.0f;
    check(CommandMoving(h,pos,h.post),"leaving the assigned combat area returns to the post");
    check(HeliCommand(vehicle,Command{Order::none,{}}) && !h.cmdMoving && !h.guard && h.hold[0]==-100.0f,
          "release restores the original duty and clears travel");
    check(!CommandMoving(h,pos,h.post),"ordinary uncommanded combat remains unrestricted by this gate");
    check(HeliCommand(vehicle,Command{Order::follow,{}}),"follow accepted");
    const float leader[3]={1000,0,1000};
    check(CommandMoving(h,pos,leader),"follow first joins the player instead of retaining its old fight");
    // Enhanced door gunners are independent of local pilot controls; the online aim hook is required online only.
    alignas(16) unsigned char gunSeats[3*edf::kSeatStride]{},remotePilot[0x500]{};
    image=reinterpret_cast<unsigned char*>(0x10000000);
    Put<const void*>(vehicle,0,image+kVt410);Put<void*>(vehicle,kSeats,gunSeats);Put<std::uint64_t>(vehicle,kSeatCount,3);
    Put<void*>(gunSeats,kSeatRider,remotePilot);Put<void*>(gunSeats,kSeatRiderCtrl,ctrl);
    remotePilot[edf::kHumanPlayer]=1;Put<std::uint16_t>(remotePilot,0x128,1);
    check(SeatRider(gunSeats)==Rider::other && AnyPlayerIn(gunSeats),"fixture is another machine's player pilot");
    gunnerFixture=gunnerSession=true;doorOk=true;commandConfig.heliDoorGuns=true;
    CrewDoorGuns(vehicle);check(gunnerQueries==2,"remote player's pilot seat does not suppress local NPC gunner checks");
    gunnerQueries=0;gunnerReady=false;CrewDoorGuns(vehicle);
    check(gunnerQueries==0,"online door gunners stay off if native aim bridge could not install");
    gunnerSession=false;CrewDoorGuns(vehicle);
    check(gunnerQueries==2,"offline door gunners do not require the network aim bridge");
    remotePilot[edf::kHumanPlayer]=0;Put<std::uint16_t>(remotePilot,0x128,2);
    h.ref=ObjRef::Of(vehicle);h.seenFrame=GameFrame();h.reap=true;h.top=37.0f;deleteOk=true;
    HeliReap(nullptr);
    check(h.ref.Is(vehicle) && h.reap && h.top==37.0f,
          "withdrawal never deletes a helicopter under real NPCs or erases their flight state");
    remotePilot[edf::kHumanPlayer]=1;Put<std::uint16_t>(remotePilot,0x128,1);
    HeliReap(nullptr);
    check(!h.ref,"player takeover releases called flight ownership without deleting aircraft");
    std::printf("heli_command_test: %d checks passed\n",checks);
    return 0;
}
