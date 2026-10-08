#include "real_driver_native.h"
#include "crew.h"
#include "memory.h"

namespace crew {
namespace {
constexpr unsigned kClearCall=0x573A8A,kClearInput=0x62C120;
constexpr unsigned kReadSetup=0x62D6E0,kSetupDtors=0x1765220;
constexpr unsigned kAnnounceBoard=0x5763E0,kAnnounceExit=0x576310;
constexpr unsigned char kBoardSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x81};
constexpr unsigned char kExitSig[]={0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x81,0xEC,0x30,6,0,0};
constexpr unsigned char kClearContext[]={
    0x48,0x8B,0x8E,0x40,0x15,0,0,0x48,0x81,0xC1,0xC0,2,0,0,0xE8,0x91,0x86,0x0B,0,
    0x8B,0x86,0x8C,0x15,0,0};
constexpr unsigned char kClearSig[]={0x33,0xC0,0x48,0xC7,0x41,0x0C,0,0,0x80,0x3F,0x48,0x89,1};
constexpr unsigned char kReadSetupSig[]={0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x40,0x48,0x8B,0xDA};
constexpr unsigned char kPathFlagSig[]={0x48,0x8B,0x86,0x10,0x0E,0,0,0x48,0x85,0xC0,0x74,4,0x83,0x48,0x68,0x20};
using ClearFn=void(__fastcall*)(void*);
using SetupFn=void(__fastcall*)(void*,void*);
ClearFn nextClear=nullptr;
NpcSeatInputOwnedFn ownsInput=nullptr;
bool ready=false;

void __fastcall RealDriverClearHook(void* input) noexcept {
    // This call belongs only to Human's no-controller branch. Keep its personal
    // input reset and all other Human processing; only the owned seat is spared.
    auto seat=static_cast<unsigned char*>(input)-0x2C0;
    if(ready && ownsInput && ownsInput(seat))return;
    nextClear(input);
}

bool ApplyMissionSetup(unsigned char* vehicle) noexcept {
    alignas(16) unsigned char setup[0x40]{};
    alignas(16) unsigned char scratch[0x40]{};
    Put<std::uint16_t>(setup,0x10,0xFFFF);
    bool applied=false;
    __try {
        reinterpret_cast<SetupFn>(image+kReadSetup)(vehicle,setup);
        reinterpret_cast<SetupFn const*>(At<void* const*>(vehicle,0))[46](vehicle,setup);
        applied=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("Real NPC driver: mission setup failed");
    }
    __try {
        const auto type=At<std::uint16_t>(setup,0x10);
        if(type!=0xFFFF)reinterpret_cast<SetupFn const*>(image+kSetupDtors)[type](setup,scratch);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("Real NPC driver: mission setup release failed");
        applied=false;
    }
    return applied;
}
}

bool InstallRealDriverNative(NpcSeatInputOwnedFn ownsSeatInput) noexcept {
    if(ready)return ownsInput==ownsSeatInput;
    if(!ownsSeatInput)return false;
    bool matches=false;
    __try {
        matches=Matches(kClearCall-14,kClearContext,sizeof(kClearContext)) &&
            Matches(kClearInput,kClearSig,sizeof(kClearSig)) &&
            Matches(kReadSetup,kReadSetupSig,sizeof(kReadSetupSig)) &&
            Matches(0x6330F0,kPathFlagSig,sizeof(kPathFlagSig)) &&
            Matches(kAnnounceBoard,kBoardSig,sizeof(kBoardSig)) &&
            Matches(kAnnounceExit,kExitSig,sizeof(kExitSig));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if(!matches){Log("Real NPC driver off: native profile mismatch");return false;}
    nextClear=reinterpret_cast<ClearFn>(image+kClearInput);
    ownsInput=ownsSeatInput;
    bool changed=false;
    const bool installed=RedirectCall(image+kClearCall,image+kClearInput,
        reinterpret_cast<void*>(&RealDriverClearHook),changed);
    ready=installed && changed;
    Log("Real NPC driver native input=%d",ready);
    return ready;
}

bool RealDriverNativeReady() noexcept { return ready; }

namespace {
bool LocalNpc(unsigned char* human) noexcept {
    return ready && human && Readable(human,0x1828) &&
        !At<void*>(human,kHumanPad) && !human[kHumanPlayer] && !(human[0x128]&1);
}
}

bool AnnounceNpcBoarding(unsigned char* human) noexcept {
    if(!LocalNpc(human) || !At<void*>(human,0x1540))return false;
    auto ctrl=At<unsigned char*>(human,0x1550);
    if(!ctrl || !Readable(ctrl,12) || At<LONG>(ctrl,8)<=0)return false;
    // 5763E0 has no player-only gate: it accepts any local registered Human,
    // then emits vehicle reference id, seat index and ++human->rideSequence.
    reinterpret_cast<ClearFn>(image+kAnnounceBoard)(human);
    return true;
}

bool AnnounceNpcDismount(unsigned char* human) noexcept {
    // SeatKick's 10000015 handler 5701B0 clears riding state and applies exit
    // placement but does not announce it. Unlike 5763E0, 576310 has no remote
    // owner guard, so this wrapper must reject remote NPC copies itself.
    if(!LocalNpc(human) || At<void*>(human,0x1540) || At<void*>(human,0x1550))return false;
    reinterpret_cast<ClearFn>(image+kAnnounceExit)(human);
    return true;
}

bool PrepareNpcVehicle(unsigned char* vehicle,bool spawned) noexcept {
    if(!ready || !vehicle || !Readable(vehicle,0xE20))return false;
    if(spawned && !ApplyMissionSetup(vehicle))return false;
    __try {
        // Exact non-spawning side effects from RideAi 6330A2..633100. Its E30=1
        // is deliberately absent: snapshot 6301D6 would call RideAi again.
        auto holders=At<unsigned char*>(vehicle,0x638);
        const auto count=At<std::size_t>(vehicle,0x648);
        if(count && (!holders || count>64 || !Readable(holders,count*0x48)))return false;
        for(std::size_t i=0;i<count;++i) {
            auto weapon=At<unsigned char*>(holders+i*0x48,0x10);
            if(weapon && !Readable(weapon,0x8B7))return false;
        }
        auto path=At<unsigned char*>(vehicle,0xE10);
        if(path && !Readable(path,0x6C))return false;
        for(std::size_t i=0;i<count;++i) {
            auto weapon=At<unsigned char*>(holders+i*0x48,0x10);
            if(weapon)weapon[0x8B6]=0;
        }
        if(path)Put<std::uint32_t>(path,0x68,At<std::uint32_t>(path,0x68)|0x20);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("Real NPC driver: vehicle preparation failed");return false;
    }
}
}
