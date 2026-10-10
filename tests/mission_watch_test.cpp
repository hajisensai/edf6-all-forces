// The mission start's phase watchdog (src/mission_watch.cpp): every phase logged as it starts, one that does not
// finish reported with the memory state and a dump of the process (2026-10-10: an online joiner's game stopped in
// the plugin's mission start, and its log only showed the last phase that had logged something of its own).
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
bool Logged(const char* part) {
    std::lock_guard<std::mutex> lock(linesLock);
    for(const auto& line:lines)if(line.find(part)!=std::string::npos)return true;
    return false;
}
int Count(const char* part) {
    std::lock_guard<std::mutex> lock(linesLock);
    int n=0;
    for(const auto& line:lines)n+=line.find(part)!=std::string::npos;
    return n;
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
    stuckMs=1500;

    // A mission start that runs through: each phase logged with the previous one's time, nothing reported stuck.
    WatchMissionPhase("resets");
    WatchMissionPhase("jets");
    WatchMissionPhase(nullptr);
    Check(Logged("MISSION phase resets (since none 0 ms)"),"first phase logged");
    Check(Logged("MISSION phase jets (resets "),"next phase names the one before");
    Check(Logged("MISSION phases done (jets "),"the end names the last phase");
    Sleep(2600);
    Check(!Logged("still running"),"a finished mission start is never reported stuck");
    Check(GetFileAttributesW(dump)==INVALID_FILE_ATTRIBUTES,"no dump without a stuck phase");

    // A phase that hangs (the joiner's support soldiers, 2026-10-10): reported once, with memory and a dump.
    WatchMissionPhase("support soldiers");
    Sleep(3600);
    Check(Count("MISSION phase support soldiers still running after")==1,"a stuck phase is reported once");
    Check(Logged("MEMORY stuck mission phase"),"the memory state when it was stuck");
    Check(Logged("MISSION stuck dump:") && Logged("(phase support soldiers;"),"the dump names the stuck phase");
    WIN32_FILE_ATTRIBUTE_DATA info{};
    Check(GetFileAttributesExW(dump,GetFileExInfoStandard,&info) && (info.nFileSizeLow || info.nFileSizeHigh),
          "the dump file is written");

    // It finishes after all: said so; a later stuck phase is reported but no second dump overwrites the first.
    WatchMissionPhase("support variants");
    Sleep(1200);
    Check(Logged("MISSION phase support soldiers ended after"),"a stuck phase that ends is logged");
    Sleep(2600);
    Check(Count("still running after")==2 && Count("MISSION stuck dump:")==1,"one dump per process");
    WatchMissionPhase(nullptr);
    DeleteFileW(dump);
    std::printf("mission_watch: %d checks passed\n",checks);
    return 0;
}
