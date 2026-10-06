// Execute production NextLockTarget against stand-in game entry points and an observable remote weapon copy.
// The type-6 recorder applies the fields cleared by EDF+0x692482; the remote tick models EDF+0x691E50..0x691E68.
// No game is loaded, and no network message leaves this process.
#include "../src/lockon.cpp"
#include <cstdio>
#include <cstdlib>

namespace crew {
unsigned char* image=nullptr;
Config config{};
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept { return 1000; }
bool CameraRay(float*,float*) noexcept { return false; }
bool IsStoreWeapon(const unsigned char*) noexcept { return false; }
}

namespace {
using namespace crew;
constexpr std::size_t kImageSize=0x700000,kCancelRva=0x695150;
constexpr std::size_t kRemoteEntry=0xC88,kRemoteCtrl=0xC90,kRemoteProgress=0xC98,kSoundTimer=0xCB8;
const unsigned char cancelHead[]={0x40,0x53,0x48,0x81,0xEC,0x40,0x06,0x00,0x00,0x48,0x8B,0x05,0xF8,0x9E,0x95,0x01};
alignas(16) unsigned char weapon[0xD00]{},remote[0xD00]{},listHead[0x28]{},lockNode[0x28]{},target[0x60]{},locked[0x60]{},ctrl[0x10]{};
int checks=0,cancelCalls=0,dropCalls=0,clearCalls=0;

void Check(bool condition,const char* what) {
    ++checks;
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}

void Jump(std::size_t rva,const void* to) {
    unsigned char code[12]={0x48,0xB8};
    std::memcpy(code+2,&to,8);code[10]=0xFF;code[11]=0xE0;
    std::memcpy(image+rva,code,sizeof(code));
    FlushInstructionCache(GetCurrentProcess(),image+rva,sizeof(code));
}

void __fastcall Cancel(void* w) {
    Check(w==weapon,"cancel receives the weapon, not the pending-entry subobject");
    Check(dropCalls==0 && At<const void*>(weapon,kLocking)==target,"notify before dropping the local lock");
    Check(At<std::uint64_t>(weapon,kLockCount)==1,"notification keeps the completed lock list");
    ++cancelCalls;
    // Type 6: the receiver drops its weak entry, clears its control block and progress (0x692482..0x6924C0).
    Put<const void*>(remote,kRemoteEntry,nullptr);Put<const void*>(remote,kRemoteCtrl,nullptr);
    Put<std::uint32_t>(remote,kRemoteProgress,0);
}

void __fastcall Drop(void* entry) {
    Check(entry==weapon+kLocking,"drop receives weapon+C70");
    Check(cancelCalls==1 && !At<const void*>(remote,kRemoteEntry),"remote cancel precedes the local drop");
    ++dropCalls;
    // The local part (0x68FEE0): the pending weak reference and lock progress, not the completed list.
    Put<const void*>(entry,0,nullptr);Put<const void*>(entry,8,nullptr);Put<std::uint32_t>(entry,0x10,0);
}

void __fastcall Clear(void* w) {
    Check(w==weapon,"full clear receives the weapon");
    ++clearCalls;
    Put<std::uint64_t>(weapon,kLockCount,0);
    Put<const void*>(weapon,kLocking,nullptr);Put<const void*>(weapon,kLockingCtrl,nullptr);
}

void RemoteTick() {
    // Without type 6, the real remote tick keeps counting this old target as being locked, even without a new pick.
    auto entry=At<unsigned char*>(remote,kRemoteEntry);
    if(entry && At<const void*>(remote,kRemoteCtrl))
        Put<std::uint32_t>(entry,0x34,At<std::uint32_t>(entry,0x34)+1);
}
}  // namespace

int main() {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,kImageSize,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"allocate a stand-in image");
    std::memcpy(image+kClearLock,kClearLockSig,sizeof(kClearLockSig));
    std::memcpy(image+kDropLocking,kDropLockingSig,sizeof(kDropLockingSig));
    std::memcpy(image+kPick,kPickSig,sizeof(kPickSig));
    // A missing cancellation signature must disable partial drops, even if the local drop function matches.
    InstallLockon();
    Check(clearOk && !dropOk,"unverified cancel sender cannot enable partial cancellation");
    std::memcpy(image+kCancelRva,cancelHead,sizeof(cancelHead));
    InstallLockon();
    Check(clearOk && dropOk,"matching cancel and drop signatures enable partial cancellation");
    Jump(kCancelRva,reinterpret_cast<const void*>(&Cancel));
    Jump(kDropLocking,reinterpret_cast<const void*>(&Drop));
    Jump(kClearLock,reinterpret_cast<const void*>(&Clear));
    Put<const void*>(weapon,kLockList,listHead);Put<std::uint64_t>(weapon,kLockCount,1);
    Put<const void*>(listHead,0,lockNode);Put<const void*>(lockNode,0,listHead);
    Put<const void*>(lockNode,kNodeEntry,locked);Put<const void*>(lockNode,kNodeCtrl,ctrl);
    Put<std::int32_t>(ctrl,8,1);
    Put<const void*>(weapon,kLocking,target);Put<const void*>(weapon,kLockingCtrl,ctrl);
    Put<float>(weapon,kLockProgress,12.0f);Put<std::uint32_t>(weapon,kLockFailed,7);
    Put<float>(weapon,kSoundTimer,19.0f);
    Put<const void*>(remote,kRemoteEntry,target);Put<const void*>(remote,kRemoteCtrl,ctrl);
    Put<float>(remote,kRemoteProgress,12.0f);
    RemoteTick();
    Check(At<std::uint32_t>(target,0x34)==1,"the stand-in remote initially reports the pending target");

    NextLockTarget(weapon);
    Check(cancelCalls==1 && dropCalls==1 && clearCalls==0,"partial cycle cancels then drops, without full clear");
    Check(!At<const void*>(weapon,kLocking) && !At<const void*>(weapon,kLockingCtrl) &&
          At<float>(weapon,kLockProgress)==0.0f,"the local pending lock is cleared");
    Check(At<std::uint64_t>(weapon,kLockCount)==1 && At<const void*>(listHead,0)==lockNode &&
          At<const void*>(lockNode,kNodeEntry)==locked,"completed lock and list links survive the target cycle");
    Check(At<std::uint32_t>(weapon,kLockFailed)==0 && At<float>(weapon,kSoundTimer)==0.0f,
          "failure and lock-sound timers reset as in stock cancellation");
    Check(skip.weapon==weapon && skip.count==1 && skip.entries[0]==target,"only the dropped target is cycled away");
    // No new candidate and therefore no type-5 replacement packet arrives.
    for(int frame=0;frame<120;++frame)RemoteTick();
    Check(At<std::uint32_t>(target,0x34)==1 && !At<const void*>(remote,kRemoteCtrl) &&
          At<float>(remote,kRemoteProgress)==0.0f,"no replacement candidate leaves no ghost lock on the remote copy");
    NextLockTarget(weapon);
    Check(clearCalls==1 && cancelCalls==1 && dropCalls==1,"with no pending lock, the existing full-clear path is used");
    Check(skip.count==1 && skip.entries[0]==locked,"full cycle records the completed lock as skipped");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("lockon: %d production cancellation checks passed\n",checks);
    return 0;
}
