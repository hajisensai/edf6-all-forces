// src/airstrike.cpp with real IsPlayer/RemoteRider and synthetic human and image memory (no game, no network, no spawning):
// the player predicates, the flown supports' entry planner, the strikes of the map's bar (2026-10-26: every support is a bar
// entry for pt, its weapon row retired), the retired rows told apart from the live ones, and the stock radio calls left
// wholly native by InstallAirstrikes.
#include "../src/airstrike.cpp"
#include "../src/barrage_plan.h"
#include <cstdio>
#include <cstdlib>

namespace crew {
unsigned char* image=nullptr;
Config config{};
PlayerFix player{};
bool supportSky=true,supportGround=true,supportMeasured=true;
Sea supportSea=Sea::land;
Sea SeaAt(float,float,float* surface) noexcept {*surface=1;return supportSea;}
PlayArea MapPlayArea() noexcept { return {{-1500,-1500},{1500,1500},supportMeasured,0,true}; }
float HavokHalf() noexcept { return 3000.0f; }   // BigWorld off: the stock world
float SupportPassTurn(const SupportAircraft& spec) noexcept { return spec.transportPlane ? 266.0f : 0.0f; }   // jet_flight.cpp FerryTurn
// Ground height under (x, z): flat by default; `supportSlope` m per m rising from the map's centre; `supportStep` a
// ledge every 30 m outward (no flat ground anywhere). Vertical rays meet that same ground.
float supportSlope=0,supportStep=0;
float SupportGround(float x,float z) noexcept {
    const float r=std::sqrt(x*x+z*z);
    return r*supportSlope+(static_cast<int>(r/30.0f)%2 ? supportStep : 0.0f);
}
float MapRay(const float* from,const float* to,float* hit) noexcept {
    if(!supportSky && from[1]<10 && to[1]>10){hit[0]=from[0];hit[1]=10;hit[2]=from[2];return 0.5f;}
    const float g=SupportGround(from[0],from[2]);
    if(to[1]<from[1] && to[1]<g && from[1]>g){hit[0]=from[0];hit[1]=g;hit[2]=from[2];return 0.5f;}
    return -1;
}
bool MapGroundNear(float x,float z,float,float* out,bool) noexcept {*out=SupportGround(x,z);return supportGround;}
// The strikes' two ends (barrage.cpp, ultimate.cpp: not linked here): what they were asked.
int barrageAsked=-1,ultimateAsked=0;float barrageHeading[3]{},barrageDamage=0,ultimateDamage=0;
const unsigned char* strikeCaller=nullptr;
BarrageResult barrageResult=BarrageResult::started;
bool ultimateResult=true;
BarrageResult BarrageStart(int kind,const unsigned char* caller,const float*,const float* heading,float damage) noexcept {
    barrageAsked=kind;strikeCaller=caller;std::memcpy(barrageHeading,heading,12);barrageDamage=damage;return barrageResult;
}
bool UltimateStart(const unsigned char* caller,const float*,float damage,std::uint64_t) noexcept {
    ++ultimateAsked;strikeCaller=caller;ultimateDamage=damage;return ultimateResult;
}
unsigned char* playerHuman=nullptr;
unsigned char* PlayerHuman() noexcept { return playerHuman; }
int unpaidCalls=0;
bool SupportCallUnpaidAt(int,const float*,wchar_t*,std::size_t) noexcept {++unpaidCalls;return true;}
SupportReadiness SupportCallReadiness() noexcept { return {SupportReady::ready,0}; }   // the second waves (airstrike.cpp SupportFollowUpTick)
bool testOnline=true;
const Config& Cfg() noexcept { return config; }
bool InSession() noexcept { return testOnline; }
void Log(const char*,...) noexcept {}
ULONGLONG gameMs=3600000;
ULONGLONG GameMs() noexcept { return gameMs; }
int FaultLog(const char*,const EXCEPTION_POINTERS*) noexcept { return EXCEPTION_EXECUTE_HANDLER; }
void SetMissionPackSaveHook(bool) noexcept {}             // edf5campaign.cpp: the packs' save rows (not this test's)
void MigrateMissionPackSave(unsigned char*) noexcept {}
// weapon_records.cpp (not linked here): the native layout, 0x800 records at GS+0xEB50.
std::uint32_t WeaponRecordCapacity() noexcept { return 0x800; }
unsigned char* WeaponRecord(unsigned char* gs,std::uint32_t id) noexcept { return gs && id<0x800 ? gs+0xEB50+std::size_t{id}*12 : nullptr; }
void WeaponRecordsCheckRows(std::uint32_t) noexcept {}
unsigned char* JetLaunchThrown(ThrownDrone,const float*,const float*,DWORD,const void*) noexcept { return nullptr; }
online::CopyOwner SetSpawnOwner(online::CopyOwner owner) noexcept { return owner; }   // no copies are made here
}

namespace {
int checks=0;
void Check(bool condition,const char* description) {
    ++checks;
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",description);std::exit(1);}
}
void LocalHuman(unsigned char* human) {
    crew::Put<const void*>(human,crew::kHumanPad,human);
    crew::Put<unsigned char>(human,crew::kHumanPlayer,1);
}
}

int main() {
    using namespace crew;
    alignas(16) unsigned char local[0x400]{},splitScreen[0x400]{},remote[0x400]{},npc[0x400]{},noPad[0x400]{};
    LocalHuman(local);LocalHuman(splitScreen);LocalHuman(remote);
    Put<unsigned char>(remote,edf::kRiderNet+edf::kNetFlags,1);
    Put<const void*>(npc,kHumanPad,npc);   // a non-player may have a controller; both player conditions matter
    Put<unsigned char>(noPad,kHumanPlayer,1);
    Check(InSession(),"fixture is online");
    Check(IsPlayer(local) && IsPlayer(splitScreen),"both local split-screen players use the real player predicate");
    Check(!IsPlayer(remote) && edf::RemoteRider(remote),"another machine's player is no player of this machine, even with a pad");
    Check(edf::IsAnyPlayer(remote) && edf::IsAnyPlayer(local) && !edf::IsAnyPlayer(npc) && !edf::IsAnyPlayer(nullptr),
          "a player of any machine: this one's and another's, never an NPC");
    alignas(16) unsigned char remoteNoPad[0x400]{};
    Put<unsigned char>(remoteNoPad,kHumanPlayer,1);Put<unsigned char>(remoteNoPad,edf::kRiderNet+edf::kNetFlags,1);
    Check(edf::IsAnyPlayer(remoteNoPad) && !IsPlayer(remoteNoPad),"another machine's player without a pad object here is still a player");
    // A seat: rider +0x260, its control block +0x268 with a live use count. Another machine's player there: no Rider::player
    // (not this machine's keys) but AnyPlayerIn (a player aboard).
    alignas(16) unsigned char seat[0x340]{},ctrlBlock[0x20]{};
    Put<std::int32_t>(ctrlBlock,edf::kCtrlUses,1);
    Put<const void*>(seat,edf::kSeatRiderCtrl,ctrlBlock);
    const unsigned char* const riders[]={local,remote,remoteNoPad,npc};
    const bool anyPlayer[]={true,true,true,false},thisMachine[]={true,false,false,false};
    for(int i=0;i<4;++i) {
        Put<const void*>(seat,edf::kSeatRider,riders[i]);
        Check(edf::AnyPlayerIn(nullptr,seat)==anyPlayer[i],"AnyPlayerIn: a player of any machine in the seat");
        Check((edf::SeatRider(nullptr,seat)==edf::Rider::player)==thisMachine[i],"Rider::player: this machine's player only");
    }
    Put<const void*>(seat,edf::kSeatRiderCtrl,nullptr);
    Check(!edf::AnyPlayerIn(nullptr,seat),"an empty seat holds no player");

    // ---- The flown supports' entry planner ----
    const float supportTarget[3]={0,0,0},observer[3]={0,0,100};support::Route route;
    SupportAircraft call0{},four{};
    Check(SupportAircraftSpec(0,&call0),"the first flown call has a spec");
    four=call0;four.count=4;
    // 2026-10-09: air support is made in the air at the edge and flies in; no runway, no ground for a crew.
    Check(PlanAirSupport(call0,supportTarget,observer,&route)==support::Refusal::none && route.from[1]>=150,
          "production entry planner puts the aircraft in the air at the route's height");
    // Off the map (2026-10-10: made 180 m inside the ground, in plain sight): past the ground's edge (the walls +-1500 stand
    // kVoidMargin inside it) by kOffMap, inside the physical square (crew.h ArrivalHalf: Havok's 3000 less kArrivalRoom).
    Check(std::fmax(std::fabs(route.from[0]),std::fabs(route.from[2]))>=1500.0f+area::kVoidMargin+support::kOffMap-0.5f &&
          std::fabs(route.from[0])<=ArrivalHalf() && std::fabs(route.from[2])<=ArrivalHalf(),"the entry stands off the map, inside the square");
    // The map's preview plans the very same entry (support_warning.cpp), unlogged.
    {support::Route preview;
     Check(PreviewAirRoute(call0,supportTarget,observer,&preview)==support::Refusal::none &&
           !std::memcmp(preview.from,route.from,12) && !std::memcmp(preview.heading,route.heading,12),
           "the preview's entry is the planner's");}
    supportSlope=0.05f;supportStep=2.0f;   // a hillside of ledges: no flat ground anywhere
    Check(PlanAirSupport(four,supportTarget,observer,&route)==support::Refusal::none,"air support needs no flat ground at all");
    {float slot[3];support::AirFormationSlot(route,3,1.0f,slot);float g=0;MapGroundNear(slot[0],slot[2],0,&g,true);
     Check(slot[1]>=g+75.0f,"every formation slot stands over its own ground");}
    supportSlope=0;supportStep=0;
    supportSky=false;
    Check(PlanAirSupport(call0,supportTarget,observer,&route)==support::Refusal::noSky,"a roof above the destination refuses air support");
    supportSky=true;supportMeasured=false;
    Check(PlanAirSupport(call0,supportTarget,observer,&route)==support::Refusal::noArea,"unmeasured bounds cannot be used as an entry");
    supportMeasured=true;
    {int sub=-1;for(int i=0;i<kCallCount;++i)if(SupportSubCatalog(i))sub=i;
     SupportAircraft spec{};
     Check(sub>=0 && !SupportAircraftSpec(sub,&spec) && PlanAirSupport(spec,supportTarget,observer,&route)==support::Refusal::unsupported,
           "stationary submarine model cannot fake a physical entry");}
    // One planner for every entry's spec (merge of #100 and #104): #100's paratroop plane has no jet or heli row
    // (SupportAircraft::transportPlane) and must still plan; a spec that brings no aircraft at all is refused.
    {SupportAircraft plane{};plane.transportPlane=true;plane.count=1;
     Check(PlanAirSupport(plane,supportTarget,observer,&route)==support::Refusal::none && route.from[1]>=150,
           "the transport plane is planned in the air at the edge like any aircraft");
     SupportAircraft nothing{};nothing.count=1;
     Check(PlanAirSupport(nothing,supportTarget,observer,&route)==support::Refusal::unsupported,"a spec with no aircraft is refused");}

    // ---- The supports' prices (tools/calls.py Call.pt) ----
    for(int i=0;i<kCallCount;++i)Check(SupportAirCallPt(i)==kCalls[i].pt && kCalls[i].pt>=1000,"every flown support has its pt");
    Check(SupportAirCallPt(kCallCount)==0 && SupportAirCallPt(-1)==0,"no pt past the flown supports");

    // ---- The strikes (kStrikes): the barrages in barrage_plan.h's order, then the ultimate ----
    Check(SupportStrikeCount()==barrage::kKinds+1,"nine barrages and the ultimate");
    for(int k=0;k<barrage::kKinds;++k)Check(SupportStrikeBarrage(k)==k && kStrikes[k].kind==StrikeKind::barrage,"barrage k is kind k");
    Check(SupportStrikeBarrage(barrage::kKinds)==-1 && !std::wcscmp(SupportStrikeKey(barrage::kKinds),L"ULTIMATE"),"the ultimate last");
    for(int k=0;k<SupportStrikeCount();++k)
        Check(SupportStrikePt(k)>0 && SupportStrikeKey(k) && SupportStrikeName(k)[0] && kStrikes[k].damage>0,"a strike's key, name, pt, damage");
    // Its heading: from the player toward the point, +z with no player.
    alignas(16) unsigned char caller[0x400]{};
    {float h[3];const float at[3]={30,0,40};
     playerHuman=nullptr;SupportStrikeHeading(at,h);Check(h[0]==0 && h[2]==1,"no player: the heading is +z");
     playerHuman=caller;const float me[3]={0,5,0};std::memcpy(caller+0x90,me,12);
     SupportStrikeHeading(at,h);Check(std::fabs(h[0]-0.6f)<1e-5f && std::fabs(h[2]-0.8f)<1e-5f && h[1]==0,"the heading: from the player to the point, flat");}
    {wchar_t note[128];const float at[3]={30,0,40};
     playerHuman=nullptr;
     Check(!SupportStrikeAt(0,at,note,_countof(note)) && barrageAsked<0,"no player of this machine: no strike");
     playerHuman=caller;
     Check(SupportStrikeAt(2,at,note,_countof(note)) && barrageAsked==2 && strikeCaller==caller &&
           barrageDamage==kStrikes[2].damage && std::fabs(barrageHeading[2]-0.8f)<1e-5f,"a barrage starts by this machine's player with its round's damage");
     barrageResult=BarrageResult::full;
     Check(!SupportStrikeAt(1,at,note,_countof(note)) && note[0],"a barrage that cannot start says why");
     const int ult=SupportStrikeCount()-1;
     Check(SupportStrikeAt(ult,at,note,_countof(note)) && ultimateAsked==1 && ultimateDamage==kStrikes[ult].damage,"the ultimate starts");
     ultimateResult=false;
     Check(!SupportStrikeAt(ult,at,note,_countof(note)) && note[0],"an ultimate that cannot start says why");
     Check(!SupportStrikeAt(SupportStrikeCount(),at,note,_countof(note)) && !SupportStrikeAt(-1,at,note,_countof(note)),
           "no strike past the table");}

    // ---- The rows: the live ones owned, the supports' placeholders told apart (and un-owned at a save load) ----
    for(const auto& r:kCallRows)Check(IsCallRow(r.id) && !IsMovedRow(r.id),"a live row is a call row, never a moved one");
    Check(kMovedRowCount==kCallCount+kStrikeCount,"every flown support and strike's row is retired");
    for(const wchar_t* id:kMovedRows)Check(IsMovedRow(id) && !IsCallRow(id) && !std::wcsncmp(id,L"EDF6VC_RETIRED_",15),
                                           "a support's row is its placeholder");
    Check(!IsMovedRow(L"EDF6VC_RETIRED_KATYUSHA") && !IsMovedRow(L"EDF6VC_CALL_INTERCEPTOR"),
          "an uninstalled live call's placeholder, or a support's old id, is no moved row");
    {unsigned char record[12]{1|4,3,3,3,3,3,3,3,3,0,0,0};
     Check(ported::Disown(record) && record[0]==0 && record[4]==3,"un-owned: owned and NEW cleared, stars left");
     Check(!ported::Disown(record),"a record not owned stays as it is");}

    // ---- A sortie's second wave: called for no pt ----
    {int combo=-1;for(int i=0;i<kCallCount;++i)if(kCalls[i].then>=0)combo=i;
     Check(combo>=0,"a combined sortie exists");
     const float at[3]={0,0,0};
     SupportFollowUpQueue(combo,at);gameMs+=kCalls[combo].thenMs;SupportFollowUpTick();
     Check(unpaidCalls==1,"the second wave is called unpaid (paid with its first)");}

    // ---- Installation leaves every stock radio call native ----
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"private native-profile fixture allocated");
    constexpr unsigned bomberInit=0x5AABB0,radioBomber=0x2B924E,missionBomber=0x5B4423,radioCall=0x6A8DFB,satFire=0x6A5A22;
    constexpr unsigned planeUpdate=0x5AB240,planeSlot=0x17D3A30+5*8;
    const unsigned char initSignature[]={0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41};
    const unsigned char radioSignature[]={0xE8,0x5D,0x19,0x2F,0x00,0x90,0x48,0x8B,0x4D,0x18};
    const unsigned char missionSignature[]={0xE8,0x88,0x67,0xFF,0xFF,0x90,0xBB,0xFF,0xFF,0xFF};
    const unsigned char updateSignature[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89};
    // The radio call's IFC_Start (the plugin redirected it until 2026-10-26) and the satellite's fire state's unit load.
    const unsigned char radioCallSignature[]={0xE8,0xA0,0xCF,0xC0,0xFF,0xC6,0x87,0xEC,0x16,0x00,0x00,0x01,0x8B,0x87,0xE0,0x16};
    const unsigned char satFireSignature[]={0x48,0x81,0xC1,0xA0,0x18,0x00,0x00,0xE8,0x62,0x26,0xC1,0xFF};
    std::memcpy(image+bomberInit,initSignature,sizeof(initSignature));
    std::memcpy(image+radioBomber,radioSignature,sizeof(radioSignature));
    std::memcpy(image+missionBomber,missionSignature,sizeof(missionSignature));
    std::memcpy(image+planeUpdate,updateSignature,sizeof(updateSignature));
    std::memcpy(image+radioCall,radioCallSignature,sizeof(radioCallSignature));
    std::memcpy(image+satFire,satFireSignature,sizeof(satFireSignature));
    Put<void*>(image,planeSlot,image+planeUpdate);
    InstallAirstrikes();
    Check(At<void*>(image,planeSlot)==image+planeUpdate,"native bomber update is never intercepted or held awaiting an empty substitute");
    Check(std::memcmp(image+radioBomber,radioSignature,sizeof(radioSignature))==0,
          "Air Raider native payload construction remains untouched");
    Check(std::memcmp(image+missionBomber,missionSignature,sizeof(missionSignature))==0,
          "mission-script native payload construction remains untouched");
    Check(std::memcmp(image+radioCall,radioCallSignature,sizeof(radioCallSignature))==0,
          "the radio call's IFC_Start is no longer redirected: no radio call is a support of the plugin's");
    Check(std::memcmp(image+satFire,satFireSignature,sizeof(satFireSignature))==0,
          "the satellite's fire is no longer redirected: the ultimate is the map's");
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("airstrike: %d checks passed\n",checks);
    return 0;
}
