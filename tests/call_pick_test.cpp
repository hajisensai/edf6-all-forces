// Execute the production picker and resolver with real IsPlayer/RemoteRider and synthetic weapon/human memory.
// No game is started and no network or spawning functions are called.
#include "../src/airstrike.cpp"
#include <cstdio>
#include <cstdlib>

namespace crew {
unsigned char* image=nullptr;
Config config{};
PlayerFix player{};
bool testOnline=true;
const Config& Cfg() noexcept { return config; }
bool InSession() noexcept { return testOnline; }
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept { return 3600000; }
int FaultLog(const char*,const EXCEPTION_POINTERS*) noexcept { return EXCEPTION_EXECUTE_HANDLER; }
bool JetLaunch(JetRole,const float*,const float*,const float*,DWORD,const void*,bool) noexcept { return false; }
unsigned char* HeliLaunch(HeliBody,const float*,const float*) noexcept { return nullptr; }
void HeliCalled(unsigned char*,bool,const float*,DWORD) noexcept {}
unsigned char* SubLaunch(const float*,const float*) noexcept { return nullptr; }
bool JetLaunchBomber(const float*,const float*,const float*,const BombLoad&,DWORD,const void*,JetBody,const void*) noexcept {
    return false;
}
bool JetHolds(const void*) noexcept { return false; }
JetBody BomberBody(const unsigned char*) noexcept { return JetBody::kind; }
unsigned char* JetLaunchThrown(ThrownDrone,const float*,const float*,DWORD,const void*) noexcept { return nullptr; }
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
    alignas(16) unsigned char weapon[0x1700]{},local[0x400]{},splitScreen[0x400]{},remote[0x400]{},npc[0x400]{},noPad[0x400]{};
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
    // The pick sent with the call (call_net.h): every heading carries every pick and back, moved at most 2^-11 of itself;
    // a heading no modded machine wrote (zero, random bits) almost never decodes.
    Check(callnet::Decode(0.0f)==callnet::kNoMark,"a zero heading carries no pick");
    const float headings[]={0.0f,0.001f,-0.5f,1.0f,3.14159f,-3.14159f,6.28f,-1.0e-7f};
    for(const float h:headings)
        for(int p=callnet::kOwnCall;p<kCallCount;++p) {
            const float e=callnet::Encode(h,p);
            Check(callnet::Decode(e)==p,"the pick sent is the pick decoded");
            Check(std::fabs(e-h)<=std::fabs(h)*0.0005f+1e-30f,"the heading moves by at most 2^-11 of itself");
            Check(callnet::Decode(callnet::Encode(e,p))==p,"encoding twice keeps the pick");
        }
    std::uint32_t lcg=12345u;
    int falsePicks=0;
    for(int i=0;i<100000;++i) {
        lcg=lcg*1664525u+1013904223u;
        const float f=callnet::Float(lcg);
        falsePicks+=std::isfinite(f) && callnet::Decode(f)!=callnet::kNoMark;
    }
    Check(falsePicks<1500,"random headings decode as a pick at most 1.5% of the time (7 check bits)");
    // CallOf with a sent pick: the caller's and the others' copies of the weapon take it over their local state.
    for(int own=0;own<kCallCount;own+=3) {
        Put<float>(weapon,kWeaponHitSize,kCalls[own].mark);
        for(int sent=callnet::kOwnCall;sent<kCallCount;++sent) {
            Put<float>(weapon,kWeaponHeading,callnet::Encode(1.25f,sent));
            const Call* const want=sent<0 ? &kCalls[own] : &kCalls[sent];
            picked.store((sent+5)%kCallCount);   // this machine's own pick differs from the one sent
            Check(CallOf(ifc,remote)==want,"a remote call replays the pick its caller sent");
            Check(CallOf(ifc,local)==want,"the caller replays the pick it sent, not its picker now");
            Check(CallOf(ifc,npc)==want,"whoever the owner, the sent pick decides");
        }
    }
    Put<float>(weapon,kWeaponHitSize,kCalls[0].mark);
    Put<float>(weapon,kWeaponHeading,1.25f);
    Check(callnet::Decode(1.25f)==callnet::kNoMark,"the fixture's plain heading carries no pick");
    picked.store(2);
    Check(CallOf(ifc,local)==&kCalls[2] && CallOf(ifc,remote)==&kCalls[0],"no pick sent: the local picker for the local call only");
    Put<float>(weapon,kWeaponHitSize,1.0f);
    Put<float>(weapon,kWeaponHeading,callnet::Encode(1.25f,3));
    Check(CallOf(ifc,remote)==nullptr,"a stock call is never converted, whatever its heading carries");
    std::printf("call picker: %d checks passed\n",checks);
    return 0;
}
