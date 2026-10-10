// The mission start's phase watchdog (src/mission_watch.cpp): every phase logged as it starts, one that does not
// finish reported with the memory state, one that still does not a dump of the process (2026-10-10: an online
// joiner's game stopped in the plugin's mission start, and its log only showed the last phase that had logged).
#include "../src/mission_watch.cpp"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {
std::mutex linesLock;
std::vector<std::string> lines;
int checks=0;
void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
int Count(const char* part) {
    std::lock_guard<std::mutex> lock(linesLock);
    int n=0;
    for(const auto& line:lines)n+=line.find(part)!=std::string::npos;
    return n;
}
bool Logged(const char* part) { return Count(part)>0; }
// Until `part` has been logged `times` times, or 5 s: the watchdog polls every 20 ms here, so no fixed sleep races it.
bool WaitFor(const char* part,int times=1) {
    for(int i=0;i<250;++i){if(Count(part)>=times)return true;Sleep(20);}
    return false;
}
}  // namespace

namespace crew {
void Log(const char* format,...) noexcept {
    char text[1000];va_list args;va_start(args,format);vsnprintf_s(text,sizeof(text),_TRUNCATE,format,args);va_end(args);
    std::lock_guard<std::mutex> lock(linesLock);
    lines.emplace_back(text);
}
void LogMemory(const char* when) noexcept { Log("MEMORY %s",when); }
}  // namespace crew

int main() {
    using namespace crew;
    wchar_t dump[MAX_PATH];
    Check(DumpPath(dump,std::size(dump)) && std::wcsstr(dump,L"EDF6VehicleCrew.hang.dmp"),"dump beside the module");
    DeleteFileW(dump);
    stuckMs=300;dumpMs=900;pollMs=20;

    // A mission start that runs through: each phase logged with the previous one's time, nothing reported stuck.
    WatchMissionPhase("resets");
    Check(writeDump!=nullptr && dumpPath[0],"dbghelp resolved when the watchdog starts, before anything is stuck");
    WatchMissionPhase("jets");
    WatchMissionPhase(nullptr);
    Check(Logged("MISSION phase resets (since none 0 ms)"),"first phase logged");
    Check(Logged("MISSION phase jets (resets "),"next phase names the one before");
    Check(Logged("MISSION phases done (jets "),"the end names the last phase");
    Sleep(1200);
    Check(!Logged("still running"),"a finished mission start is never reported stuck");
    Check(GetFileAttributesW(dump)==INVALID_FILE_ATTRIBUTES,"no dump without a stuck phase");

    // Slow, then done before the dump's time: logged with the memory, no dump.
    WatchMissionPhase("the game's player preload");
    Check(WaitFor("MISSION phase the game's player preload still running after"),"a slow phase reported");
    Check(Logged("MEMORY stuck mission phase"),"with the memory state when it was");
    WatchMissionPhase("resets");
    Check(WaitFor("MISSION phase the game's player preload ended after"),"its end logged");
    Check(!Logged("writing a dump") && GetFileAttributesW(dump)==INVALID_FILE_ATTRIBUTES,"a phase over before the dump's time: none");
    WatchMissionPhase(nullptr);
    Sleep(400);

    // A phase that hangs (the joiner's support soldiers, 2026-10-10): reported once, then a dump naming it.
    WatchMissionPhase("support soldiers");
    Check(WaitFor("MISSION phase support soldiers still running after"),"a stuck phase reported");
    Check(WaitFor("MISSION stuck dump:"),"then dumped");
    Check(Count("MISSION phase support soldiers still running after")==1,"reported once");
    Check(Logged("(phase support soldiers;") && Logged(" KB (phase"),"the dump names the stuck phase and its size");
    WIN32_FILE_ATTRIBUTE_DATA info{};
    Check(GetFileAttributesExW(dump,GetFileExInfoStandard,&info) && (info.nFileSizeLow || info.nFileSizeHigh),
          "the dump file is written");

    // It finishes after all: said so; a later stuck phase is reported but no second dump overwrites the first.
    WatchMissionPhase("support variants");
    Check(WaitFor("MISSION phase support soldiers ended after"),"a stuck phase that ends is logged");
    Check(WaitFor("still running after",3),"a later stuck phase reported too");
    Sleep(1200);
    Check(Count("MISSION stuck dump:")==1,"one dump per process");
    WatchMissionPhase(nullptr);
    DeleteFileW(dump);
    std::printf("mission_watch: %d checks passed\n",checks);
    return 0;
}
