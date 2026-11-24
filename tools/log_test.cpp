// common/log.h offline: lines from several threads all written once with a millisecond timestamp, the flush, the
// rotation to .log.1 past the limit, and the crash append beside the open writer handle.
#include "log.h"
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {
void Say(const char* format,...) {
    va_list args;va_start(args,format);edf::logfile::Line(format,args);va_end(args);
}

std::vector<std::string> Lines(const wchar_t* path) {
    std::vector<std::string> out;
    std::ifstream input(path,std::ios::binary);
    std::string line;
    while(std::getline(input,line))out.push_back(line);
    return out;
}

bool failed=false;
void Check(bool ok,const char* what) {
    printf("%s: %s\n",what,ok ? "PASS" : "FAIL");
    failed=failed || !ok;
}
}  // namespace

int main() {
    wchar_t dir[MAX_PATH],path[MAX_PATH],oldPath[MAX_PATH+2];
    if(!GetTempPathW(MAX_PATH,dir) || !GetTempFileNameW(dir,L"edf",0,path))return 1;
    DeleteFileW(path);
    swprintf_s(oldPath,L"%ls.1",path);
    DeleteFileW(oldPath);

    Say("before start");   // no path yet: nothing, no writer thread
    Check(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES,"no line before Start");

    constexpr long long kMax=256<<10;
    edf::logfile::Start(path,kMax);
    std::vector<std::thread> threads;
    for(int t=0;t<4;++t)threads.emplace_back([t] {
        for(int i=0;i<1000;++i)Say("thread=%d item=%d",t,i);
    });
    for(auto& thread:threads)thread.join();
    edf::logfile::Flush(false);
    {
        const auto lines=Lines(path);
        const std::regex stamp(R"(^\[\d\d:\d\d:\d\d\.\d\d\d\] thread=\d item=\d+\r$)");
        std::set<std::string> seen;
        bool stamped=true;
        for(const auto& line:lines) {
            stamped=stamped && std::regex_match(line,stamp);
            seen.insert(line.substr(line.find(']')+2));
        }
        bool all=lines.size()==4000;
        for(int t=0;t<4;++t)for(int i=0;i<1000;++i)
            all=all && seen.count("thread="+std::to_string(t)+" item="+std::to_string(i)+"\r")==1;
        Check(all,"4 threads x 1000 lines, each written once");
        Check(stamped,"millisecond timestamps");
    }

    const char crash[]="CRASH test line\r\n";
    edf::logfile::Crash(crash,sizeof(crash)-1);
    {
        const auto lines=Lines(path);
        Check(!lines.empty() && lines.back()=="CRASH test line\r","crash line appended beside the writer's handle");
    }

    // Past kMax: the log renamed to .log.1 and a new one begun.
    for(int i=0;i<8000;++i) {
        Say("filler %05d %s",i,"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
        if(i%500==499)edf::logfile::Flush(false);
    }
    edf::logfile::Flush(false);
    {
        WIN32_FILE_ATTRIBUTE_DATA now{},old{};
        const bool both=GetFileAttributesExW(path,GetFileExInfoStandard,&now) && GetFileAttributesExW(oldPath,GetFileExInfoStandard,&old);
        Check(both,"rotated to .log.1");
        Check(both && !now.nFileSizeHigh && now.nFileSizeLow<=kMax && !old.nFileSizeHigh && old.nFileSizeLow<=kMax,
              "both files within the limit");
        const auto lines=Lines(path);
        Check(!lines.empty() && lines.front().rfind("(log continued:",0)==0,"the new log says where the rest is");
        Check(!lines.empty() && lines.back().find("filler 07999")!=std::string::npos,"the last line in the new log");
    }
    return failed ? 3 : 0;
}
