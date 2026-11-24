// The autopilot's heap probe (tests/autopilot/heap_probe.cpp, mem-3) offline: what it reports as born after the start
// and still alive is exactly what this test left alive, by the call path that allocated it; freed and reallocated
// blocks are not counted twice, and VirtualAlloc blocks are counted apart.
#include "heap_probe.h"

#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
int failures=0;
void Check(bool ok,const char* what) {
    std::printf("%s: %s\n",ok ? "ok" : "FAILED",what);
    if(!ok)++failures;
}

// The probe's rows, kept in fixed storage (no heap use inside the hooks' reach while counting).
char rows[256][512];
int rowCount=0;
void Capture(const char* format,...) noexcept {
    if(rowCount>=256)return;
    va_list args;va_start(args,format);
    std::vsnprintf(rows[rowCount],sizeof(rows[rowCount]),format,args);
    va_end(args);
    ++rowCount;
}

void* volatile kept[300]{};

__declspec(noinline) void LeakHundred() {
    for(int i=0;i<100;++i)kept[i]=HeapAlloc(GetProcessHeap(),0,1000);
}
__declspec(noinline) void FreeHundred() {
    for(int i=0;i<100;++i) {
        void* p=HeapAlloc(GetProcessHeap(),0,3000);
        HeapFree(GetProcessHeap(),0,p);
    }
}
__declspec(noinline) void GrowFifty() {   // realloc'd blocks: counted once, at their last size
    for(int i=0;i<50;++i) {
        void* p=HeapAlloc(GetProcessHeap(),0,10);
        kept[100+i]=HeapReAlloc(GetProcessHeap(),0,p,5000);
    }
}
__declspec(noinline) void ReserveOne() {
    kept[200]=VirtualAlloc(nullptr,1<<20,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    void* gone=VirtualAlloc(nullptr,1<<20,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    VirtualFree(gone,0,MEM_RELEASE);
}

const char* Row(const char* needle) {
    for(int i=0;i<rowCount;++i)if(std::strstr(rows[i],needle))return rows[i];
    return nullptr;
}
}  // namespace

int main() {
    Check(autopilot::InstallHeapProbe(&Capture,1,true),"armed");
    autopilot::HeapProbeMark("start");
    Check(Row("HEAPLIVE start (mark 1): counting starts")!=nullptr,"counting starts at the first mark");
    Check(Row("HEAPPROBE import entries swapped")!=nullptr,"import entries swapped");
    LeakHundred();
    FreeHundred();
    GrowFifty();
    ReserveOne();
    const int before=rowCount;
    autopilot::HeapProbeMark("check");
    for(int i=0;i<rowCount;++i)std::printf("  %s\n",rows[i]);
    Check(rowCount>before,"the second mark dumps");
    const char* leak=Row("in 100 (heap 100, VA 0): heap_probe_test.exe+");
    Check(leak && std::strstr(leak,"HEAPSITE check 0.095 MB"),"the hundred kept 1000-byte blocks: one site, 0.095 MB");
    const char* grown=Row("in 50 (heap 50, VA 0): heap_probe_test.exe+");
    Check(grown && std::strstr(grown,"HEAPSITE check 0.238 MB"),"the fifty reallocated blocks at 5000 bytes each");
    const char* va=Row("in 1 (heap 0, VA 1): heap_probe_test.exe+");
    Check(va && std::strstr(va,"HEAPSITE check 1.000 MB"),"the kept VirtualAlloc block, the released one gone");
    Check(!Row("0.286 MB") && !Row("1.000 MB in 2"),"the freed blocks not counted");
    Check(Row("HEAPMOD check heap_probe_test.exe")!=nullptr,"by module: this test");
    std::printf("%d failure(s)\n",failures);
    return failures ? 1 : 0;
}
