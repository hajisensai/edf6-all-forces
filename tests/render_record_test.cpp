// The render recording's single owner (src/render_record.cpp): the call 0x1198FA4 checked and redirected once however
// many features install, the features' turns run in Slot order (the frame rate's poses, then the first person's boxes)
// before the game's recording with its arguments, the projection turns at each projection with the recording's first
// marked once (none for a projection outside it or after a recording that never projected), and each feature's turn runs without the other registered.
#include "../src/render_record.cpp"
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {
int matches=0,redirects=0;
bool sigOk=true,redirectOk=true;
void* redirectedTo=nullptr;
bool shareOk=true;
int shares=0;
void (*projectHook)(void*,const int*)=nullptr;
unsigned char* executableTestPage=nullptr;
}  // namespace

namespace crew::scopelens {
bool SetBeforeProject(BeforeProjectFn fn) noexcept {
    ++shares;
    if(!shareOk)return false;
    projectHook=fn;
    return true;
}
}  // namespace crew::scopelens

namespace edf {
bool Matches(const unsigned char*,std::size_t rva,const unsigned char* bytes,std::size_t size) noexcept {
    ++matches;
    if(executableTestPage && rva==crew::kRecordDraw)return std::memcmp(executableTestPage,bytes,size)==0;
    return sigOk && rva==crew::kRecordCall-14 && size==19 && bytes[size-5]==0xE8;
}
void* AllocateNearCode(const void*,const unsigned char* code,std::size_t size) noexcept {
    if(!executableTestPage)return nullptr;
    auto* const slot=executableTestPage+0x100;
    std::memcpy(slot,code,size);
    FlushInstructionCache(GetCurrentProcess(),slot,size);
    return slot;
}
bool PatchCode(unsigned char* at,const unsigned char* expected,const unsigned char* replacement,std::size_t size,
               Claim,const char*) noexcept {
    if(!executableTestPage || at!=executableTestPage || std::memcmp(at,expected,size))return false;
    std::memcpy(at,replacement,size);
    FlushInstructionCache(GetCurrentProcess(),at,size);
    return true;
}
bool RedirectCall(unsigned char* site,void* expected,void* replacement,bool& changed,edf::Claim,const char*) noexcept {
    ++redirects;
    if(!redirectOk || site!=crew::image+crew::kRecordCall || expected!=crew::image+crew::kRecordDraw)return false;
    redirectedTo=replacement;
    changed=true;
    return true;
}
}  // namespace edf

namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
}  // namespace crew

namespace {
using namespace crew;
int checks=0,failures=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}

char order[8]{};
int orderAt=0;
std::uint64_t recordArgs[2]{};
void* recordSys=nullptr;
int recordCalls=0;
DWORD nativeThread=0,beforeThread=0,projectThread=0,afterThread=0;
void FrameRateTurn() noexcept { if(orderAt<7)order[orderAt++]='R'; }
void FirstPersonTurn() noexcept {beforeThread=GetCurrentThreadId();if(orderAt<7)order[orderAt++]='F';}
// The projection turns: 'P' / 'Q' the frame rate's / first person's at a first projection, 'p' / 'q' at any other.
void FrameRateProject(void*,const int*,bool first) noexcept { if(orderAt<7)order[orderAt++]=first ? 'P' : 'p'; }
void FirstPersonProject(void*,const int*,bool first) noexcept { if(orderAt<7)order[orderAt++]=first ? 'Q' : 'q'; }
void PoseSnapshotProject(void*,const int*,bool first) noexcept {
    projectThread=GetCurrentThreadId();
    if(first && orderAt<7)order[orderAt++]='S';
}
void PoseSnapshotAfter() noexcept {afterThread=GetCurrentThreadId();if(orderAt<7)order[orderAt++]='E';}
int recordProjections=0;   // the projections the stand-in recording makes
void* StandInRecord(void* sys,std::uint64_t a,std::uint64_t b) {
    nativeThread=GetCurrentThreadId();
    if(orderAt<7)order[orderAt++]='G';
    for(int i=0;i<recordProjections;++i)projectHook(nullptr,nullptr);
    ++recordCalls;recordSys=sys;recordArgs[0]=a;recordArgs[1]=b;
    return reinterpret_cast<void*>(0x77);
}
void Reset() {
    recordState=0;
    entryPatched=false;
    stockDraw=nullptr;
    InitOnceInitialize(&threadStartup);
    for(auto& b:recordBefores)b=nullptr;
    for(auto& a:recordAfters)a=nullptr;
    for(auto& p:recordProjects)p=nullptr;
    recordOpen=false;recordProjections=0;
    matches=redirects=shares=0;sigOk=redirectOk=shareOk=true;redirectedTo=nullptr;projectHook=nullptr;
    image=reinterpret_cast<unsigned char*>(reinterpret_cast<std::uintptr_t>(&StandInRecord)-kRecordDraw);
}
void Frame() {
    std::memset(order,0,sizeof(order));orderAt=0;
    using Hook=void*(*)(void*,std::uint64_t,std::uint64_t);
    void* const r=reinterpret_cast<Hook>(redirectedTo)(reinterpret_cast<void*>(0x55),0x1234,1);
    Check(r==reinterpret_cast<void*>(0x77) && recordSys==reinterpret_cast<void*>(0x55) && recordArgs[0]==0x1234 &&
          recordArgs[1]==1,"the game's recording runs with the call's arguments, its result returned");
}

// Both features: one redirect, the frame rate first, the first person second, then the game.
void Both() {
    Reset();
    Check(renderrecord::Install(),"install: the frame rate's");
    renderrecord::Set(renderrecord::Slot::kFrameRate,&FrameRateTurn);
    Check(renderrecord::Install(),"install: the first person's (already in)");
    renderrecord::Set(renderrecord::Slot::kFirstPerson,&FirstPersonTurn);
    Check(matches==1 && redirects==1 && redirectedTo==reinterpret_cast<void*>(&RecordHook),
          "two features: the call site checked and redirected once, to the single hook");
    Frame();
    Check(!std::strcmp(order,"RFG"),"order: the frame rate's poses, the first person's boxes, then the recording");
    // Registered the other way round: the order is the slots', not the registrations'.
    Reset();
    renderrecord::Install();
    renderrecord::Set(renderrecord::Slot::kFirstPerson,&FirstPersonTurn);
    renderrecord::Set(renderrecord::Slot::kFrameRate,&FrameRateTurn);
    Frame();
    Check(!std::strcmp(order,"RFG"),"order: by slot whatever installs first");
    Check(shares==1 && projectHook==&ProjectHook,"the projection shared once, to the single hook");
    // The projection turns: in slot order at each projection, `first` at the recording's first only.
    renderrecord::SetProject(renderrecord::Slot::kFirstPerson,&FirstPersonProject);
    renderrecord::SetProject(renderrecord::Slot::kFrameRate,&FrameRateProject);
    recordProjections=2;
    Frame();
    Check(!std::strcmp(order,"RFGPQpq"),"projections: both turns in slot order, the recording's first once");
    recordProjections=0;
    Frame();
    std::memset(order,0,sizeof(order));orderAt=0;
    projectHook(nullptr,nullptr);
    Check(!std::strcmp(order,"pq"),"a recording that never projected is closed when it returns: a later projection is no first");
    std::memset(order,0,sizeof(order));orderAt=0;
    recordOpen=false;
    projectHook(nullptr,nullptr);
    Check(!std::strcmp(order,"pq"),"a projection outside the recording: no first");
    renderrecord::SetProject(renderrecord::Slot::kFrameRate,nullptr);
    recordProjections=1;
    Frame();
    Check(!std::strcmp(order,"RFGQ"),"a projection turn cleared: the other's only");
    renderrecord::SetProject(renderrecord::Slot::kPoseSnapshot,&PoseSnapshotProject);
    renderrecord::SetAfter(renderrecord::Slot::kPoseSnapshot,&PoseSnapshotAfter);
    Frame();
    Check(!std::strcmp(order,"RFGQSE"),"pose snapshots seal after other projection hooks and retire after recording");
    recordProjections=0;
    Frame();
    Check(!std::strcmp(order,"RFGE"),"an unprojected recording still closes its pose capture");
}

// Each alone (the frame rate off, or the first person off).
void Alone() {
    Reset();
    renderrecord::Install();
    renderrecord::Set(renderrecord::Slot::kFirstPerson,&FirstPersonTurn);
    Frame();
    Check(!std::strcmp(order,"FG"),"the first person alone (FrameRateCap=0): its turn, then the recording");
    Reset();
    renderrecord::Install();
    renderrecord::Set(renderrecord::Slot::kFrameRate,&FrameRateTurn);
    Frame();
    Check(!std::strcmp(order,"RG"),"the frame rate alone (first person off): its turn, then the recording");
    renderrecord::Set(renderrecord::Slot::kFrameRate,nullptr);
    Frame();
    Check(!std::strcmp(order,"G"),"a turn cleared: only the recording");
}

// Refused: the signature or the redirect; no second try, nothing registers.
void Refused() {
    Reset();
    sigOk=false;
    Check(!renderrecord::Install() && redirects==0,"install: a call site not as read is not redirected");
    sigOk=true;
    Check(!renderrecord::Install() && matches==1,"install: refused once, not retried by the next feature");
    renderrecord::Set(renderrecord::Slot::kFrameRate,&FrameRateTurn);
    renderrecord::SetProject(renderrecord::Slot::kFrameRate,&FrameRateProject);
    Check(!recordBefores[0] && !recordProjects[0],"refused: nothing registers");
    Reset();
    redirectOk=false;
    Check(!renderrecord::Install() && redirects==1,"install: a redirect refused");
    Check(!renderrecord::Install() && redirects==1,"install: the refused redirect not retried");
    Reset();
    shareOk=false;
    Check(!renderrecord::Install() && redirects==0,"install: the projection not shared, the recording not redirected");
    Reset();
    renderrecord::Install();
    renderrecord::Set(renderrecord::Slot::kCount,&FrameRateTurn);
    Check(!recordBefores[0] && !recordBefores[1],"a slot out of range is ignored");
}
DWORD producerId=0;
constexpr DWORD kExampleFault=0xE1740601;
void* WorkerContracts(void* value,std::uint64_t a,std::uint64_t b) {
    Check(GetCurrentThreadId()!=producerId,"native body runs on the recording worker");
    Check(GetLastError()==17 && (_mm_getcsr()&_MM_ROUND_MASK)==_MM_ROUND_DOWN,
          "the caller's floating-point mode and last error follow the recording");
    Check(a==0x1122334455667788ull && b==0x8877665544332211ull,"full-width native arguments survive the handoff");
    _mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_UP);
    SetLastError(24);
    return value;
}
void* NativeFailure(void*,std::uint64_t,std::uint64_t) {
    const ULONG_PTR information=0x44;
    RaiseException(kExampleFault,0,1,&information);
    return nullptr;
}
bool ReceiveNativeFailure() noexcept {
    __try {
        recordThread.Invoke(&NativeFailure,nullptr,0,0);
    } __except(GetExceptionCode()==kExampleFault ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}
void* CppFailure(void*,std::uint64_t,std::uint64_t) {throw std::runtime_error("native C++ failure");}
void* ReturnWorker(void*,std::uint64_t,std::uint64_t) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(GetCurrentThreadId()));
}
void* NestedOnWorker(void*,std::uint64_t,std::uint64_t) {return recordThread.Invoke(&ReturnWorker,nullptr,0,0);}

void ThreadMigration() {
    Reset();
    renderrecord::Install();
    stockDraw=&StandInRecord;
    entryPatched=true;
    renderrecord::Set(renderrecord::Slot::kFirstPerson,&FirstPersonTurn);
    renderrecord::SetProject(renderrecord::Slot::kPoseSnapshot,&PoseSnapshotProject);
    renderrecord::SetAfter(renderrecord::Slot::kPoseSnapshot,&PoseSnapshotAfter);
    const DWORD main=GetCurrentThreadId();
    recordProjections=1;
    Frame();
    Check(!std::strcmp(order,"FGSE"),"the entire native mission call and its recording callbacks ran in order");
    Check(nativeThread!=main && nativeThread==beforeThread && nativeThread==projectThread && nativeThread==afterThread,
          "the native call, pose publication, projection and retirement share the separate recording thread");
    const DWORD worker=nativeThread;
    orderAt=0;
    std::memset(order,0,sizeof(order));
    RecordEntry(nullptr,0,0);
    Check(!std::strcmp(order,"G") && nativeThread==worker,"the original loading/menu entry uses the same worker");
    const auto expectedWorker=reinterpret_cast<void*>(static_cast<std::uintptr_t>(worker));
    Check(recordThread.Invoke(&NestedOnWorker,nullptr,0,0)==expectedWorker,
          "a recording callback can re-enter on its own worker without waiting on itself");

    producerId=main;
    const auto oldMxcsr=_mm_getcsr();
    _mm_setcsr((oldMxcsr&~_MM_ROUND_MASK)|_MM_ROUND_DOWN);
    SetLastError(17);
    void* const tag=reinterpret_cast<void*>(0x8B);
    const auto response=recordThread.Invoke(&WorkerContracts,tag,0x1122334455667788ull,0x8877665544332211ull);
    const DWORD lastError=GetLastError();
    const auto mxcsr=_mm_getcsr();
    _mm_setcsr(oldMxcsr);
    Check(response==tag && lastError==24 && (mxcsr&_MM_ROUND_MASK)==_MM_ROUND_UP,
          "the native result and updated caller context return across the handoff");

    bool caught=false;
    try {recordThread.Invoke(&CppFailure,nullptr,0,0);}
    catch(const std::runtime_error& error){caught=!std::strcmp(error.what(),"native C++ failure");}
    Check(caught && ReceiveNativeFailure(),"C++ and native exception paths resume on the synchronous caller");
    Check(recordThread.Invoke(&ReturnWorker,nullptr,0,0)==expectedWorker,"a failed recording did not strand the worker");
    unsigned int countA=0,countB=0;
    const auto submit=[&](unsigned int& count) {
        for(unsigned i=0;i<20;++i)if(recordThread.Invoke(&ReturnWorker,nullptr,0,0)==expectedWorker)++count;
    };
    std::thread a([&]{submit(countA);}),b([&]{submit(countB);});
    a.join();b.join();
    Check(countA==20 && countB==20,"overlapping producer calls complete without sharing a stack job");
    recordThread.Stop();
}
void NativeEntryTrampoline() {
    Reset();
    executableTestPage=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(executableTestPage!=nullptr,"private executable fixture allocated");
    if(!executableTestPage)return;
    image=reinterpret_cast<unsigned char*>(reinterpret_cast<std::uintptr_t>(executableTestPage)-kRecordDraw);
    recordState=1;  // the existing mission call-site owner already installed
    std::memcpy(executableTestPage,kDrawEntrySig,sizeof(kDrawEntrySig));
    executableTestPage[sizeof(kDrawEntrySig)]=0x5D;  // pop rbp from the native prefix
    JumpAbsolute(executableTestPage+sizeof(kDrawEntrySig)+1,reinterpret_cast<void*>(&StandInRecord));
    FlushInstructionCache(GetCurrentProcess(),executableTestPage,0x1000);
    const auto stub=reinterpret_cast<RecordDrawFn>(executableTestPage);
    const DWORD main=GetCurrentThreadId();
    executableTestPage[0]^=1;
    Check(!renderrecord::InstallThread(),"an unverified native entry is never hooked");
    executableTestPage[0]^=1;
    const bool installed=renderrecord::InstallThread();
    Check(installed && executableTestPage[0]==0xE9,"the whole-function entry installs a real trampoline");
    if(installed) {
        const auto result=stub(nullptr,0x4141,0x4242);
        Check(result==reinterpret_cast<void*>(0x77) && recordArgs[0]==0x4141 && recordArgs[1]==0x4242 &&
              nativeThread!=main,"a direct stock entry executes its native body on the recording worker");
        std::memcpy(executableTestPage,kDrawEntrySig,5);
        FlushInstructionCache(GetCurrentProcess(),executableTestPage,5);
        recordThread.Stop();
    }
    VirtualFree(executableTestPage,0,MEM_RELEASE);
    executableTestPage=nullptr;
    Reset();
}
}  // namespace

int main() {
    Both();
    Alone();
    Refused();
    ThreadMigration();
    NativeEntryTrampoline();
    std::printf("render_record: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
