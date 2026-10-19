// Production public session predicate, private image and original native predicate bytes. No game process.
#include "../src/netprobe.cpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace crew {
unsigned char* image=nullptr;
Config config{};
unsigned hostQueries=0;
const Config& Cfg() noexcept {return config;}
bool IsHelicopter(const void*) noexcept {return true;}
void Log(const char*,...) noexcept {}
}
namespace {
int checks=0;
void Check(bool value,const char* why) {++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct Graph {
    alignas(16) unsigned char manager[0x40]{},modes[2][0x18]{},raw[2][0x200]{};
    const void* table[2]{};
    void Build(int online) {
        using namespace crew;
        Put<void*>(manager,kModeTable,table);Put<std::uint64_t>(manager,0x28,2);Put<std::uint64_t>(manager,kModeCount,2);
        Put<int>(manager,kCurrentMode,0);
        for(unsigned i=0;i<2;++i) {table[i]=modes[i];Put<void*>(modes[i],kModeRaw,raw[i]);Put<int>(raw[i],kRawRelative,0);Put<int>(raw[i],kRawOnline,online);}
    }
};
bool __fastcall Host() {++crew::hostQueries;return true;}
void Jump(unsigned char* at,const void* function) {
    at[0]=0x48;at[1]=0xB8;std::memcpy(at+2,&function,8);at[10]=0xFF;at[11]=0xE0;
}
void ProbeStubs() {
    using namespace crew;
    for(unsigned i=1;i<4;++i)std::memcpy(image+kSigs[i].rva,kSigs[i].bytes,sizeof(kSigs[i].bytes));
    // Keep the checked host prologue. Its first native call becomes a nearby jump to the counter;
    // then unwind only that prologue. Offline must never reach this synthetic host operation.
    const auto proxy=image+0x1000;Jump(proxy,reinterpret_cast<const void*>(&Host));
    const auto call=image+kIsHost+15;call[0]=0xE8;
    const auto relative=static_cast<std::int32_t>(proxy-(call+5));std::memcpy(call+1,&relative,4);
    const unsigned char tail[]={0x48,0x83,0xC4,0x30,0x5F,0xC3};std::memcpy(call+5,tail,sizeof(tail));
}
template<class Fn> double Bench(const char* name,unsigned iterations,Fn fn) {
    volatile unsigned hits=0;
    const auto begin=std::chrono::steady_clock::now();
    for(unsigned i=0;i<iterations;++i)hits+=fn() ? 1u : 0u;
    const auto elapsed=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count();
    Check(hits==iterations,"benchmark executes a live online predicate on every iteration");
    const double perCall=elapsed/iterations;std::printf("BENCH %s: %u calls, %.1f ns/call\n",name,iterations,perCall);return perCall;
}
}
int main() {
    using namespace crew;
    Check(!InSession(),"query before EDF image binding is safely offline without caching absence");
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x20C0000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"private image allocated");
    Check(!InSession(),"unsupported predicate bytes are refused");
    std::memcpy(image+kOnline,kOnlineCode,sizeof(kOnlineCode));ProbeStubs();
    FlushInstructionCache(GetCurrentProcess(),image,0x20C0000);
    using Native=bool(__fastcall*)(const void*);
    const auto native=reinterpret_cast<Native>(image+kOnline);
    unsigned char vehicle[0x1DA0]{};config.debug=true;
    Check(!InSession(),"null startup singleton is safely offline after the code becomes available");
    NetProbe(vehicle);Check(hostQueries==0,"startup probe never queries room ownership without a session");
    Graph first{},second{};first.Build(0);second.Build(1);
    Put<void*>(image,kGameStatus,first.manager);Put<int>(first.manager,kCurrentMode,-1);Put<std::uint64_t>(first.manager,kModeCount,0);
    Check(!native(nullptr) && !InSession(),"constructed manager with native no-mode sentinel stays offline");
    Put<int>(first.manager,kCurrentMode,0);
    Check(!InSession(),"partially populated manager cannot select an entry outside its actual count");
    first.Build(0);
    Check(InSession()==native(nullptr) && !InSession(),"normal offline mode preserves the original predicate");
    NetProbe(vehicle);Check(hostQueries==0,"offline probe does not call the host function");
    for(const int online:{1,-3,(std::numeric_limits<int>::max)(),0}) {
        Put<int>(first.raw[0],kRawOnline,online);
        Check(InSession()==native(reinterpret_cast<void*>(1)) && InSession()==(online!=0),
              "all native zero/nonzero flag values and ignored input argument preserve semantics");
    }
    Put<int>(first.manager,kCurrentMode,1);Put<int>(first.raw[1],kRawOnline,1);
    Check(InSession()==native(nullptr) && InSession(),"current mode selects its own table entry");
    NetProbe(vehicle);Check(hostQueries==1,"online probe asks room ownership after a real session query");
    Put<void*>(image,kGameStatus,nullptr);
    Check(!InSession(),"destroyed singleton immediately becomes offline");
    NetProbe(vehicle);Check(hostQueries==1,"teardown probe never calls host after the singleton is cleared");
    Put<void*>(image,kGameStatus,second.manager);
    Check(InSession() && InSession()==native(nullptr),"recreated online manager is observed without retry or delay");
    Put<void*>(image,kGameStatus,first.manager);Put<int>(first.raw[1],kRawOnline,0);
    Check(!InSession(),"replaced singleton does not retain the previous manager's online result");
    Put<void*>(image,kGameStatus,second.manager);
    Check(InSession(),"a second replacement is read on the very next query");
    Put<int>(second.manager,kCurrentMode,-2);Check(!InSession(),"invalid negative mode index is refused before table arithmetic");
    Put<int>(second.manager,kCurrentMode,2);Check(!InSession(),"mode count is an actual bounds check");
    Put<int>(second.manager,kCurrentMode,0);Put<std::uint64_t>(second.manager,kModeCount,UINT64_MAX);
    Check(!InSession(),"overflowing mode table extent is refused without an arbitrary mode-count limit");
    second.Build(1);
    auto* inaccessible=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS));
    Check(inaccessible!=nullptr,"unreadable lifecycle storage allocated");
    Put<void*>(image,kGameStatus,inaccessible);Check(!InSession(),"unreadable manager is unknown without a native call");
    Put<void*>(image,kGameStatus,second.manager);Put<void*>(second.manager,kModeTable,inaccessible);
    Check(!InSession(),"unreadable mode table is refused");second.Build(1);
    second.table[0]=inaccessible;Check(!InSession(),"unreadable selected mode is refused");second.Build(1);
    Put<void*>(second.modes[0],kModeRaw,inaccessible);Check(!InSession(),"unreadable selected raw mode is refused");second.Build(1);
    Put<void*>(second.manager,kModeTable,nullptr);Check(!InSession(),"missing table during teardown is offline");second.Build(1);
    second.table[0]=nullptr;Check(!InSession(),"missing selected mode during teardown is offline");second.Build(1);
    Put<void*>(second.modes[0],kModeRaw,nullptr);Check(!InSession(),"missing raw mode during teardown is offline");second.Build(1);
    // Relative mode data is signed in the actual machine code. Cover both sides of the raw node.
    auto* displaced=second.raw[0]+0x100;Put<int>(displaced,kRawRelative,-0x88);Put<int>(displaced-0x20,0,1);
    Put<void*>(second.modes[0],kModeRaw,displaced);
    Check(InSession() && InSession()==native(nullptr),"signed negative raw displacement keeps native semantics");second.Build(1);
    auto* split=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(split!=nullptr,"split raw data allocation created");DWORD previous=0;
    Check(VirtualProtect(split+0x1000,0x1000,PAGE_NOACCESS,&previous)!=0,"online flag page made unreadable");
    Put<int>(split,kRawRelative,0x1000-static_cast<int>(kRawOnline));Put<void*>(second.modes[0],kModeRaw,split);
    Check(!InSession(),"unreadable final native flag is refused");second.Build(1);
    auto* guarded=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE|PAGE_GUARD));
    Check(guarded!=nullptr,"guarded lifecycle storage allocated");Put<void*>(image,kGameStatus,guarded);
    Check(!InSession(),"guard page is rejected without probing its data");MEMORY_BASIC_INFORMATION region{};
    Check(VirtualQuery(guarded,&region,sizeof(region))==sizeof(region) && (region.Protect&PAGE_GUARD)!=0,
          "session query does not consume a guard page");
    Put<void*>(image,kGameStatus,second.manager);
    Check(InSession() && InSession()==native(nullptr),"valid manager recovers after every unreadable graph without cached offline state");
    config.debug=false;
    const auto nativeCost=Bench("original native session",1000000,[native](){return native(nullptr);});
    const auto productionCost=Bench("production lifecycle session",1000000,[](){return InSession();});
    std::printf("BENCH lifecycle overhead: %.1f ns/call (%.1fx native)\n",productionCost-nativeCost,productionCost/nativeCost);
    Put<void*>(image,kGameStatus,nullptr);Check(!InSession(),"final teardown remains safe after the hot online path");
    VirtualFree(guarded,0,MEM_RELEASE);VirtualFree(split,0,MEM_RELEASE);VirtualFree(inaccessible,0,MEM_RELEASE);
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("net_session_lifecycle_test: %d checks passed\n",checks);
}
