// Execute the production picker and resolver with real IsPlayer/RemoteRider and synthetic weapon/human memory.
// No game is started and no network or spawning functions are called.
#include "../src/airstrike.cpp"
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
bool SupportCallAt(int,const float*,wchar_t*,std::size_t) noexcept {return false;}
bool testOnline=true;
const Config& Cfg() noexcept { return config; }
bool InSession() noexcept { return testOnline; }
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept { return 3600000; }
int FaultLog(const char*,const EXCEPTION_POINTERS*) noexcept { return EXCEPTION_EXECUTE_HANDLER; }
bool JetLaunch(JetRole,const float*,const float*,const float*,DWORD,const void*,bool) noexcept { return false; }
void HeliCalled(unsigned char*,bool,const float*,DWORD) noexcept {}
unsigned char* SubLaunch(const float*,const float*) noexcept { return nullptr; }
bool JetLaunchBomber(const float*,const float*,const float*,const BombLoad&,DWORD,const void*,JetBody,const void*) noexcept {
    return false;
}
bool JetHolds(const void*) noexcept { return false; }
JetBody BomberBody(const unsigned char*) noexcept { return JetBody::kind; }
unsigned char* JetLaunchThrown(ThrownDrone,const float*,const float*,DWORD,const void*) noexcept { return nullptr; }
online::CopyOwner SetSpawnOwner(online::CopyOwner owner) noexcept { return owner; }   // no copies are made here
online::CopyOwner CopyOwnerOfCaller(const unsigned char*) noexcept { return online::kCopyHost; }
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
    alignas(16) unsigned char weapon[0x1A00]{},local[0x400]{},splitScreen[0x400]{},remote[0x400]{},npc[0x400]{},noPad[0x400]{};
    void* const ifc=weapon+kWeaponIfc;
    LocalHuman(local);LocalHuman(splitScreen);LocalHuman(remote);
    Put<unsigned char>(remote,edf::kRiderNet+edf::kNetFlags,1);
    Put<const void*>(npc,kHumanPad,npc);   // a non-player may have a controller; both player conditions matter
    Put<unsigned char>(noPad,kHumanPlayer,1);
    wchar_t banner[256]{};
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

    picked.store(-1);
    CallPick(1,banner,_countof(banner));
    Check(picked.load()==0,"online next key changes the pick");
    Check(std::wcsstr(banner,kCallLabels[0])!=nullptr,"online banner identifies the chosen call");
    CallPick(-1,banner,_countof(banner));
    Check(picked.load()==-1,"previous key restores each-weapon-own mode");

    // Five different local choices across every call weapon: a remote call's own mark must always win.
    const int choices[]={0,2,6,12,kCallCount-1};
    for(const int chosen:choices) {
        picked.store(-1);
        CallPick(chosen+1,banner,_countof(banner));
        Check(picked.load()==chosen,"online picker reaches each requested choice");
        for(int own=0;own<kCallCount;++own) {
            Put<float>(weapon,kWeaponHitSize,kCalls[own].mark);
            Check(CallOf(ifc,local)==&kCalls[chosen],"local player's call uses the local pick");
            Check(CallOf(ifc,splitScreen)==&kCalls[chosen],"second local player shares this machine's pick");
            Check(CallOf(ifc,remote)==&kCalls[own],"remote player's call keeps its weapon definition");
            Check(CallOf(ifc,nullptr)==&kCalls[own],"unknown caller keeps the weapon definition");
            Check(CallOf(ifc,npc)==&kCalls[own],"NPC call keeps the weapon definition");
            Check(CallOf(ifc,noPad)==&kCalls[own],"player flag without a local controller cannot override");
            Check(At<float>(weapon,kWeaponHitSize)==kCalls[own].mark,"resolution does not rewrite the shared weapon marker");
        }
        Put<float>(weapon,kWeaponHitSize,1.0f);
        Check(CallOf(ifc,local)==nullptr && CallOf(ifc,remote)==nullptr,"stock calls are never converted by the pick");
        Put<float>(weapon,kWeaponHitSize,9999.0f);
        Check(CallOf(ifc,local)==nullptr,"an unknown marker is never converted by the pick");
        CallPick(-chosen-1,banner,_countof(banner));
        Check(picked.load()==-1,"every choice can return to each-weapon-own mode");
        for(int own=0;own<kCallCount;++own) {
            Put<float>(weapon,kWeaponHitSize,kCalls[own].mark);
            Check(CallOf(ifc,local)==&kCalls[own] && CallOf(ifc,remote)==&kCalls[own],"each-own mode resolves both callers normally");
        }
    }

    testOnline=false;
    CallPick(1,banner,_countof(banner));
    Put<float>(weapon,kWeaponHitSize,kCalls[kCallCount-1].mark);
    Check(CallOf(ifc,local)==&kCalls[0],"offline local picker behavior is preserved");
    testOnline=true;
    Check(CallOf(ifc,local)==&kCalls[0],"joining a session does not erase the local selection");
    Check(CallOf(ifc,remote)==&kCalls[kCallCount-1],"joining a session does not apply that selection to remote calls");
    // The pick sent with the call (call_net.h, in the seed's high 32 bits): every pick round-trips with the seed's own low
    // 32 bits kept; a seed nobody marked decodes as no pick.
    const std::uint64_t seeds[]={0ull,1ull,0xFFFFFFFFull,0x123456789ABCDEF0ull,~0ull,0x8000000000000000ull};
    for(const std::uint64_t seed:seeds) {
        Check(callnet::Decode(seed)==callnet::kNoMark,"an unmarked seed carries no pick");
        for(int p=callnet::kOwnCall;p<kCallCount;++p) {
            const std::uint64_t e=callnet::Encode(seed,p);
            Check(callnet::Decode(e)==p,"the pick sent is the pick decoded");
            Check((e&0xFFFFFFFFull)==(seed&0xFFFFFFFFull),"the seed's low 32 bits are kept");
        }
    }
    std::uint64_t lcg=0x9E3779B97F4A7C15ull;
    int falsePicks=0;
    for(int i=0;i<1000000;++i) {
        lcg=lcg*0x5D588B656C078965ull+0x269EC3ull;   // the weapon's own random state's step (0x69355B)
        falsePicks+=callnet::Decode(lcg)!=callnet::kNoMark;
    }
    Check(falsePicks==0,"a million of the weapon's own seeds: none carries the 26-bit mark");
    // Offline nothing is decoded: whatever the received seed holds, origin/main's rule.
    testOnline=false;
    Put<float>(weapon,kWeaponHitSize,kCalls[4].mark);
    picked.store(2);
    for(int sent=callnet::kOwnCall;sent<kCallCount;++sent) {
        Put<std::uint64_t>(weapon,kWeaponRxSeed,callnet::Encode(0x1234ull,sent));
        Check(CallOf(ifc,local)==&kCalls[2],"offline: the local player's call takes this machine's pick, never a seed");
        Check(CallOf(ifc,remote)==&kCalls[4],"offline: any other call keeps the weapon's own, never a seed");
        Check(CallOf(ifc,npc)==&kCalls[4],"offline: an NPC's call keeps the weapon's own");
    }
    // Online, a call of another machine's player: the pick its caller sent, else the weapon's own.
    testOnline=true;
    for(int own=0;own<kCallCount;own+=3) {
        Put<float>(weapon,kWeaponHitSize,kCalls[own].mark);
        for(int sent=callnet::kOwnCall;sent<kCallCount;++sent) {
            Put<std::uint64_t>(weapon,kWeaponRxSeed,callnet::Encode(0xABCDull,sent));
            picked.store((sent+5)%kCallCount);   // this machine's own pick differs from the one sent
            Check(CallOf(ifc,remote)==(sent<0 ? &kCalls[own] : &kCalls[sent]),"a remote call replays the pick its caller sent");
            Check(CallOf(ifc,npc)==&kCalls[own],"an NPC's call never reads a seed");
        }
        Put<std::uint64_t>(weapon,kWeaponRxSeed,0x0123456789ABCDEFull);
        Check(CallOf(ifc,remote)==&kCalls[own],"a remote call whose seed nobody marked keeps the weapon's own");
    }
    // Online, this machine's player: the pick it sent (not its picker now), else this machine's pick; never the received seed.
    Put<float>(weapon,kWeaponHitSize,kCalls[0].mark);
    Put<std::uint64_t>(weapon,kWeaponRxSeed,callnet::Encode(0ull,7));
    picked.store(2);
    Check(CallOf(ifc,local)==&kCalls[2],"no pick sent yet: this machine's pick, the received seed ignored");
    sentPicks[0]=SentPick{weapon,5};
    picked.store(9);
    Check(CallOf(ifc,local)==&kCalls[5],"the caller replays the pick it sent");
    testOnline=false;
    Check(CallOf(ifc,local)==&kCalls[9],"offline the sent record is not used");
    testOnline=true;
    sentPicks[0]=SentPick{};
    Put<float>(weapon,kWeaponHitSize,1.0f);
    Put<std::uint64_t>(weapon,kWeaponRxSeed,callnet::Encode(0ull,3));
    Check(CallOf(ifc,remote)==nullptr,"a stock call is never converted, whatever its seed carries");
    const float supportTarget[3]={0,0,0},observer[3]={0,0,100};support::Route route;
    // 2026-10-09: air support is made in the air at the edge and flies in; no runway, no ground for a crew.
    Check(PlanAirSupport(0,supportTarget,observer,&route,0)==support::Refusal::none && route.from[1]>=150,
          "production entry planner puts the aircraft in the air at the route's height");
    supportSlope=0.05f;supportStep=2.0f;   // a hillside of ledges: no flat ground anywhere
    Check(PlanAirSupport(0,supportTarget,observer,&route,4)==support::Refusal::none,"air support needs no flat ground at all");
    {float slot[3];support::AirFormationSlot(route,3,1.0f,slot);float g=0;MapGroundNear(slot[0],slot[2],0,&g,true);
     Check(slot[1]>=g+75.0f,"every formation slot stands over its own ground");}
    supportSlope=0;supportStep=0;
    supportSky=false;
    Check(PlanAirSupport(0,supportTarget,observer,&route,0)==support::Refusal::noSky,"a roof above the destination refuses air support");
    supportSky=true;supportMeasured=false;
    Check(PlanAirSupport(0,supportTarget,observer,&route,0)==support::Refusal::noArea,"unmeasured bounds cannot be used as an entry");
    supportMeasured=true;
    Check(PlanAirSupport(16,supportTarget,observer,&route,0)==support::Refusal::unsupported,"stationary submarine model cannot fake a physical entry");
    // Installation against a private image with the supported native bomber signatures. A restored
    // legacy takeover installs successfully here and mutates these bytes/vtable, failing this check.
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"private native-profile fixture allocated");
    constexpr unsigned bomberInit=0x5AABB0,radioBomber=0x2B924E,missionBomber=0x5B4423;
    constexpr unsigned planeUpdate=0x5AB240,planeSlot=0x17D3A30+5*8;
    const unsigned char initSignature[]={0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41};
    const unsigned char radioSignature[]={0xE8,0x5D,0x19,0x2F,0x00,0x90,0x48,0x8B,0x4D,0x18};
    const unsigned char missionSignature[]={0xE8,0x88,0x67,0xFF,0xFF,0x90,0xBB,0xFF,0xFF,0xFF};
    const unsigned char updateSignature[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89};
    std::memcpy(image+bomberInit,initSignature,sizeof(initSignature));
    std::memcpy(image+radioBomber,radioSignature,sizeof(radioSignature));
    std::memcpy(image+missionBomber,missionSignature,sizeof(missionSignature));
    std::memcpy(image+planeUpdate,updateSignature,sizeof(updateSignature));
    Put<void*>(image,planeSlot,image+planeUpdate);
    InstallAirstrikes();
    Check(At<void*>(image,planeSlot)==image+planeUpdate,"native bomber update is never intercepted or held awaiting an empty substitute");
    Check(std::memcmp(image+radioBomber,radioSignature,sizeof(radioSignature))==0,
          "Air Raider native payload construction remains untouched");
    Check(std::memcmp(image+missionBomber,missionSignature,sizeof(missionSignature))==0,
          "mission-script native payload construction remains untouched");
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("call picker: %d checks passed\n",checks);
    return 0;
}
