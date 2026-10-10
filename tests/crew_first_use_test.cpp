// Run the production auto-crew and boarding-entrance paths against native-layout seats.
#include "../src/crew.cpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
namespace crew {
void MissionCrewVehicleFrame(unsigned char*) noexcept {}
void SupportDispatchTick() noexcept {}
void ResetMissionCrew() noexcept {}
bool InstallMissionCrewHooks(const unsigned*,std::size_t) noexcept { return true; }
bool NpcCanYieldSeat(const unsigned char*) noexcept { return false; }
bool NpcMoveSeat(unsigned char*,unsigned,int) noexcept { return false; }
unsigned char* image=nullptr;PlayerFix player{};
// Offline (online_authority.h): an NPC rider may be seated, through the vehicle's own RideAi (the fixture's vtable).
bool OnlineMaySeatNpc(const void*) noexcept { return true; }
bool SeatNpcRider(unsigned char* v,bool spawned) noexcept {
    reinterpret_cast<void(__fastcall* const*)(void*,bool)>(At<void* const*>(v,0))[kSlotRideAi](v,spawned);
    return true;
}
namespace {
Config testConfig{};
unsigned char testVehicle[0x2000]{},testSeats[2*kSeatStride]{},testHuman[0x1600]{},testDummy[0x400]{},testCtrl[16]{};
void* testVtable[80]{};
int calls=0,failures=0,checks=0;
bool testJet=false;
bool testDrone=false;   // JetFliesItself: one of the plugin's launched drones
float testDoor[3]={30.3f,0.0f,1.8f};
bool testHail=false,testComing=false;float testHailAt[3]={500.0f,150.0f,0.0f};   // PlayerJetHailHint
float testFloor=exitground::kNoFloor,warpedTo[3]{};int warpCount=0;   // ExitGroundTick's world
float testLower=exitground::kNoFloor;
void Check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL %s\n",why);}}
void __fastcall Ride(void*,bool){++calls;}
void Time(unsigned ms){clock.game=3600000+ms;clock.wall=GetTickCount64();player.at=clock.game;}
void Occupy(unsigned seat,unsigned char* rider){Put<void*>(testSeats+seat*kSeatStride,kSeatRider,rider);Put<void*>(testSeats+seat*kSeatStride,kSeatRiderCtrl,rider ? testCtrl : nullptr);}
void Setup(){
    ResetCrew();exitWatch=ExitWatch{};std::memset(testVehicle,0,sizeof(testVehicle));std::memset(testSeats,0,sizeof(testSeats));
    std::memset(testHuman,0,sizeof(testHuman));std::memset(testDummy,0,sizeof(testDummy));
    calls=0;testJet=false;testDrone=false;testConfig=Config{};testConfig.debug=false;pauseOk=false;
    testVtable[kSlotRideAi]=reinterpret_cast<void*>(&Ride);
    Put<void*>(testVehicle,0,testVtable);Put<void*>(testVehicle,kSelfCtrl,testCtrl);
    Put<void*>(testVehicle,kSeats,testSeats);Put<std::uint64_t>(testVehicle,kSeatCount,2);
    Put<std::int32_t>(testVehicle,kTeam,kTeamVehicle);Put<int>(testCtrl,8,1);
    testHuman[kHumanPlayer]=1;Put<void*>(testHuman,kHumanPad,testHuman);Put<unsigned>(testHuman,0x31C,1);
    for(unsigned i=0;i<2;++i){Put<unsigned>(testSeats+i*kSeatStride,0x30,1);Put<unsigned>(testSeats+i*kSeatStride,0x34,1);}
    Put<void*>(testDummy,0,image+kDummyRiderVtable);player=PlayerFix{};Time(100);
}
}
const Config& Cfg() noexcept{return testConfig;}
void Log(const char*,...) noexcept{}
void SeePlayer(const float* p,std::int32_t team) noexcept{std::memcpy(player.pos,p,12);player.team=team;player.at=GameMs();}
bool IsPlayerJet(const void*) noexcept{return false;}
bool IsSazabi(const void*) noexcept{return false;}
void SazabiFrame(unsigned char*) noexcept{}
bool PlayerJetHolds(const void*) noexcept{return false;}
bool SidecarHoldsPlayer(const void*) noexcept{return false;}
bool IsPrimerVehicle(const void*) noexcept{return false;}
bool IsHelicopter(const void*) noexcept{return false;}
bool HeliCrewed(const void*) noexcept{return false;}
bool IsJet(const void*) noexcept{return testJet;}
bool JetFliesItself(const void*) noexcept{return testDrone;}
bool PlayerJetBoardable(const void*) noexcept{return testJet;}
unsigned char* PlayerHuman() noexcept{return testHuman;}
bool SeatPoint(const unsigned char*,unsigned,float* point,float* reach) noexcept{std::memcpy(point,testDoor,12);*reach=2.3f;return true;}
bool InstallNpcAi() noexcept{return false;}
bool PlayerJetHailHint(const float*,float* at,float* distance,bool* coming) noexcept{
    if(!testHail)return false;
    std::memcpy(at,testHailAt,12);*distance=520.0f;*coming=testComing;return true;}
bool MapGroundNear(float,float,float,float* h,bool) noexcept{if(testFloor==exitground::kNoFloor)return false;*h=testFloor;return true;}
float MapFloorRay(const float* a,const float* b,float* hit) noexcept {
    float y=exitground::kNoFloor;
    for(float h:{testFloor,testLower})if(h<=a[1] && h>=b[1] && h>y)y=h;
    if(y==exitground::kNoFloor)return -1.0f;
    hit[0]=a[0];hit[1]=y;hit[2]=a[2];return a[1]-y;
}
bool WarpHuman(unsigned char* h,const float* p) noexcept{std::memcpy(warpedTo,p,12);std::memcpy(h+kPosition,p,12);++warpCount;return true;}
void NpcPostInput(unsigned char*) noexcept{}
void NpcGunnersInput(unsigned char*) noexcept{}
// Unrelated production hooks are linked but must never run in this fixture.
void (*volatile unexpectedHook)()=&std::abort;
bool BumpSuppressed(void) noexcept{unexpectedHook();return {};}
void ViewTick(void) noexcept{unexpectedHook();return;}
void BigWorldProbe(void) noexcept{unexpectedHook();return;}
void JetSound(unsigned char *) noexcept{unexpectedHook();return;}
void JetSoundTick(void) noexcept{unexpectedHook();return;}
void LockSound(unsigned char *) noexcept{unexpectedHook();return;}
void VehicleSound(unsigned char *) noexcept{unexpectedHook();return;}
void ReloadConfigIfChanged(void) noexcept{unexpectedHook();return;}
void JetReap(void const *) noexcept{unexpectedHook();return;}
void ShieldVehicle(unsigned char *) noexcept{unexpectedHook();return;}
enum crew::PluginBody BodyOf(void const *) noexcept{unexpectedHook();return {};}
void VehicleRamFrame(unsigned char *) noexcept{unexpectedHook();return;}
void DrillInput(unsigned char *) noexcept{unexpectedHook();return;}
void DrillFrame(unsigned char *) noexcept{unexpectedHook();return;}
void EmcInput(unsigned char *) noexcept{unexpectedHook();return;}
void EmcFrame(unsigned char *) noexcept{unexpectedHook();return;}
void EmcTick(void) noexcept{unexpectedHook();return;}
void SazabiSoundTick(void) noexcept{unexpectedHook();return;}
void SidecarFrame(unsigned char *) noexcept{unexpectedHook();return;}
bool SidecarBoard(unsigned char *,unsigned char *) noexcept{unexpectedHook();return {};}
void HighCamFrame(unsigned char *) noexcept{unexpectedHook();return;}
void SightZoomStock(unsigned char *) noexcept{unexpectedHook();return;}
void TurretCamFrame(unsigned char *) noexcept{unexpectedHook();return;}
void StabFrame(unsigned char *) noexcept{unexpectedHook();return;}
bool IsSub(void const *) noexcept{unexpectedHook();return {};}
void SubFrame(unsigned char *) noexcept{unexpectedHook();return;}
void CarrierLaserFrame(unsigned char const *) noexcept{unexpectedHook();return;}
bool GunshipCrewSeats(void const *) noexcept{unexpectedHook();return {};}
unsigned int GunshipBoardSeat(void) noexcept{unexpectedHook();return {};}
void BoardingTick(void) noexcept{unexpectedHook();return;}
void const * BoardingOnly(void) noexcept{unexpectedHook();return {};}
void PlayerJetFrame(unsigned char *) noexcept{unexpectedHook();return;}
void LauncherFrame(unsigned char *) noexcept{unexpectedHook();return;}
bool PlayerJetOwnSight(void const *) noexcept{unexpectedHook();return {};}
bool PlayerHeliOwnSight(void const *) noexcept{unexpectedHook();return {};}
void HeliSightFrame(unsigned char *) noexcept{unexpectedHook();return;}
void NetProbe(unsigned char *) noexcept{unexpectedHook();return;}
void PlayerEjectTick(void) noexcept{unexpectedHook();return;}
bool IsGroundRobo(void const *) noexcept{unexpectedHook();return {};}
void GroundFrame(unsigned char *) noexcept{unexpectedHook();return;}
void HeliCueStep(unsigned char *) noexcept{unexpectedHook();return;}
void HeliFrame(unsigned char *) noexcept{unexpectedHook();return;}
float MapRay(float const *,float const *,float *) noexcept{unexpectedHook();return {};}
void HeliReap(void const *) noexcept{unexpectedHook();return;}
void RescueTick(void) noexcept{unexpectedHook();return;}
void HudSee(unsigned char *) noexcept{unexpectedHook();return;}
void HudPublish(void) noexcept{unexpectedHook();return;}
bool PlayerStockOwnSight(void const *) noexcept{unexpectedHook();return {};}
void StockHudFrame(unsigned char *) noexcept{unexpectedHook();return;}
void PlayAreaTick(void) noexcept{unexpectedHook();return;}
void PayloadFrame(unsigned char *) noexcept{unexpectedHook();return;}
void SeatSwitchFrame(unsigned char *) noexcept{unexpectedHook();return;}
void ProteusFrame(unsigned char *) noexcept{unexpectedHook();return;}
float GroundClearance(float const *) noexcept{unexpectedHook();return {};}
void WarnTick(void) noexcept{unexpectedHook();return;}
}
int main(){
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image)return 2;
    Setup();Crew(testVehicle,0);Time(10000);Crew(testVehicle,0);
    Check(calls==0,"unused parked ground vehicle stays empty after crew delay");
    Occupy(1,testHuman);Time(11000);Crew(testVehicle,0);Occupy(1,nullptr);Time(20000);Crew(testVehicle,0);
    Check(calls==0,"only visiting a passenger seat does not authorize an NPC driver");
    Occupy(0,testHuman);Time(21000);Crew(testVehicle,0);
    Check(FindState(testVehicle)->playerAt!=0,"driving seat records first use");
    Occupy(0,nullptr);Time(22000);Crew(testVehicle,0);Time(24000);Crew(testVehicle,0);
    Check(calls==0,"a used vehicle waits out its empty delay");
    Time(26000);Crew(testVehicle,0);Check(calls==1,"NPC may take over after the player drove and left");
    // The same use as above, a launched drone this time: no one is ever recruited for it (the plugin flies it empty).
    Setup();testDrone=true;Occupy(0,testHuman);Time(21000);Crew(testVehicle,0);Occupy(0,nullptr);
    Time(22000);Crew(testVehicle,0);Time(26000);Crew(testVehicle,0);Time(40000);Crew(testVehicle,0);
    Check(calls==0,"a launched drone left by the player is never given an NPC crew");
    Setup();Occupy(0,testDummy);Time(20000);Crew(testVehicle,0);
    Check(calls==0 && SeatRider(SeatAt(testVehicle,0))==Rider::dummy,"mission NPC is retained untouched");
    Setup();Crew(testVehicle,0);testJet=true;BoardingEntrance entry{};
    Check(PlayerBoardingEntrance(&entry) && entry.distance>30.0f && !entry.inReach,"large aircraft entrance appears before stock prompt reach");
    Check(entry.at[0]==testDoor[0] && entry.at[2]==testDoor[2],"entrance uses native locator instead of hull origin");
    Put<float>(testHuman,kPosition,30.3f);Put<float>(testHuman,kPosition+8,1.8f);
    Check(PlayerBoardingEntrance(&entry) && entry.inReach,"reaching the actual native door enables ready cue");
    Put<void*>(testHuman,kHumanVehicleCtrl,testCtrl);
    Check(!PlayerBoardingEntrance(&entry),"entrance cue disappears when riding");
    Put<void*>(testHuman,kHumanVehicleCtrl,nullptr);testHuman[kDead]=1;
    Check(!PlayerBoardingEntrance(&entry),"dead player gets no boarding cue");testHuman[kDead]=0;
    Put<unsigned>(testHuman,0x31C,2);Check(!PlayerBoardingEntrance(&entry),"incompatible soldier class cannot receive a false entrance");
    Put<unsigned>(testHuman,0x31C,1);Time(5000);
    Check(!PlayerBoardingEntrance(&entry),"stale vehicle entry is not dereferenced for a cue");
    // Nothing to board near them, one of ours up in the air: the hail hint on it (the carrier held 150 m up).
    testHail=true;
    Check(PlayerBoardingEntrance(&entry) && entry.hail && !entry.coming && !entry.inReach && entry.at[1]==150.0f,
          "no boardable aircraft near: the hail key's aircraft is marked");
    testComing=true;
    Check(PlayerBoardingEntrance(&entry) && entry.hail && entry.coming,"a called aircraft is marked as coming down");
    testComing=false;Time(100);Crew(testVehicle,0);
    Check(PlayerBoardingEntrance(&entry) && !entry.hail,"a boardable entrance near them comes before the hail hint");
    Put<void*>(testHuman,kHumanVehicleCtrl,testCtrl);
    Check(!PlayerBoardingEntrance(&entry),"riding: no hail hint either");
    Put<void*>(testHuman,kHumanVehicleCtrl,nullptr);testHail=false;
    // Off a vehicle, in the floor (exit_ground.h): put on it, inside the watch only, and only after a ride.
    auto exitCase=[&](float y,float floor,unsigned after,bool rode) {
        Setup();testFloor=floor;warpCount=0;
        if(rode){Put<void*>(testHuman,kHumanVehicleCtrl,testCtrl);ExitGroundTick();}
        Put<void*>(testHuman,kHumanVehicleCtrl,nullptr);Put<float>(testHuman,kPosition+4,y);ExitGroundTick();
        Time(100+after);ExitGroundTick();
        testFloor=exitground::kNoFloor;
        return warpCount;
    };
    Check(exitCase(-1.0f,0.0f,0,true)==1 && warpedTo[1]==exitground::kLift,"a soldier put down a metre in the floor is put on it");
    Check(exitCase(0.0f,0.05f,0,true)==0,"feet a few cm in a slope are left standing");
    Check(exitCase(-1.0f,0.0f,0,false)==0,"no ride, no exit: a soldier walking is not touched");
    Check(exitCase(-12.0f,0.0f,0,true)==0,"a floor 12 m over them (a deck, a roof) is not theirs");
    testLower=0.0f;
    Check(exitCase(3.5f,6.0f,0,true)==0,"a legal bridge-under exit remains below the nearer deck");
    Check(exitCase(6.5f,6.0f,0,true)==0,"an exit above a bridge stays above it");
    testLower=exitground::kNoFloor;
    {
        Setup();testFloor=0.0f;warpCount=0;
        Put<void*>(testHuman,kHumanVehicleCtrl,testCtrl);ExitGroundTick();
        ResetCrew(); // the next mission may reuse the exact same Human allocation
        Put<void*>(testHuman,kHumanVehicleCtrl,nullptr);Put<float>(testHuman,kPosition+4,-1.0f);ExitGroundTick();
        Check(warpCount==0,"a mission reset cancels the old ride even when the Human address is reused");
        testFloor=exitground::kNoFloor;
    }
    {
        Setup();testFloor=0.0f;warpCount=0;
        Put<void*>(testHuman,kHumanVehicleCtrl,testCtrl);ExitGroundTick();
        testHuman[kDead]=1;ExitGroundTick();testHuman[kDead]=0;
        Put<void*>(testHuman,kHumanVehicleCtrl,nullptr);Put<float>(testHuman,kPosition+4,-1.0f);ExitGroundTick();
        Check(warpCount==0,"revival cannot inherit a pre-death exit watch");
        testFloor=exitground::kNoFloor;
    }
    {
        Setup();testFloor=0.0f;warpCount=0;
        Put<void*>(testHuman,kHumanVehicleCtrl,testCtrl);ExitGroundTick();testConfig.enabled=false;
        Put<void*>(testHuman,kHumanVehicleCtrl,nullptr);Put<float>(testHuman,kPosition+4,-1.0f);ExitGroundTick();
        Check(warpCount==0,"disabling the plugin cancels the exit correction");
        testFloor=exitground::kNoFloor;
    }
    {   // in the floor only after the watch: not touched
        Setup();testFloor=0.0f;warpCount=0;
        Put<void*>(testHuman,kHumanVehicleCtrl,testCtrl);ExitGroundTick();
        Put<void*>(testHuman,kHumanVehicleCtrl,nullptr);ExitGroundTick();
        Time(100+kExitWatchMs+500);Put<float>(testHuman,kPosition+4,-1.0f);ExitGroundTick();
        testFloor=exitground::kNoFloor;
        Check(warpCount==0,"past the exit watch the soldier is the game's again");
    }
    {   // Turning off the plugin must give back its own line counts even in empty seats.
        Setup();Crew(testVehicle,0);
        unsigned char line[0x200]{}, weapon[0x2000]{}, holder[0x100]{};
        unsigned char* holders[9]{};float points[8*4]{};
        Put<void*>(line,0,image+kAimLineVtable);
        Put<int>(line,kAimLineSegments,8);
        Put<void*>(line,kAimLinePoints+8,points);
        Put<std::uint64_t>(line,kAimLinePoints+0x10,8);
        Put<void*>(weapon,kWeaponAimLine,line);Put<void*>(holder,kHolderWeapon,weapon);
        holders[8]=holder;
        Put<void*>(testSeats,kSeatWeapons,holders);Put<std::uint64_t>(testSeats,kSeatWeaponCount,9);
        SetLine(*FindState(testVehicle),line,LineWant::hide,reinterpret_cast<float*>(testVehicle+kPosition));
        Check(At<int>(line,kAimLineSegments)==0,"production hide owns the native count");
        testConfig.enabled=false;AimLines(testVehicle);
        Check(At<int>(line,kAimLineSegments)==8,"disabled restores empty-seat ninth-holder line without querying custom HUD");
        Check(HiddenOf(*FindState(testVehicle),line)==nullptr,"restoration releases ownership");
        Put<int>(line,kAimLineSegments,5);AimLines(testVehicle);
        Check(At<int>(line,kAimLineSegments)==5,"disabled does not replace another owner's native count");
    }
    {
        unsigned char system[0xD00]{};int scene=0;
        Put<void*>(image,kSystem,system);Put<void*>(system,0,image+kSystemVtable);
        Put<void*>(system,0x68,&scene);Put<std::uint32_t>(system,0xCE0,123);
        frameClockOk=true;frameClock.Reset();SeeFrame(testVehicle);
        const auto first=GameFrame();
        for(unsigned i=0;i<1024;++i)SeeFrame(reinterpret_cast<void*>(std::uintptr_t{0x10000}+i*16));
        Check(GameFrame()==first,"1024 different vehicles in one native scene step advance no extra frame");
        Put<std::uint32_t>(system,0xCE0,124);SeeFrame(testHuman);
        Check(GameFrame()==first+1,"entire previous population replaced still advances on native step");
        Put<std::uint32_t>(system,0xCE0,0);SeeFrame(testVehicle);
        Check(GameFrame()==first+2,"native scene reset in same allocation stays monotonic");
        frameClock.native=0xffffffffu;Put<std::uint32_t>(system,0xCE0,0);SeeFrame(testVehicle);
        Check(GameFrame()==first+3,"native counter wrap advances one frame");
        const auto before=GameFrame();tickFrame=before;ResetCrew();
        Check(GameFrame()>before && tickFrame==0,"mission reset invalidates frame consumers and native baseline");
        frameClockOk=false;Put<void*>(image,kSystem,nullptr);
    }
    std::printf("crew_first_use_test: %d checks, %d failed\n",checks,failures);
    VirtualFree(image,0,MEM_RELEASE);return failures ? 1 : 0;
}
