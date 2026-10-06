#include "append_log.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <set>
#include <chrono>

int main() {
    wchar_t dir[MAX_PATH],path[MAX_PATH],oldPath[MAX_PATH];
    if(!GetTempPathW(MAX_PATH,dir) || !GetTempFileNameW(dir,L"edf",0,path)
       || !GetTempFileNameW(dir,L"edf",0,oldPath))return 1;
    bool pass=true;
    {
        edf::AppendLog log;
        // A failed initial open must not permanently disable later logging.
        log.Write(L"", "lost\r\n",6);
        log.Write(path,"existing\r\n",10);
    }
    {
        edf::AppendLog log;
        std::vector<std::thread> threads;
        for(int t=0;t<4;++t)threads.emplace_back([&,t] {
            for(int i=0;i<1000;++i) {
                char line[64];
                const int n=sprintf_s(line,"thread=%d item=%d\r\n",t,i);
                log.Write(path,line,static_cast<DWORD>(n));
            }
        });
        for(auto& thread:threads)thread.join();
        // Read while writer is still open: no queue or buffered records to flush.
        std::ifstream input(path,std::ios::binary);
        std::string line;
        std::getline(input,line);pass=pass && line=="existing\r";
        std::set<std::string> lines;
        while(std::getline(input,line))lines.insert(line);
        pass=pass && lines.size()==4000;
        for(int t=0;t<4;++t)for(int i=0;i<1000;++i)
            pass=pass && lines.count("thread="+std::to_string(t)+" item="+std::to_string(i)+"\r")==1;
    }
    constexpr int count=10000;
    const std::string record(200,'x');
    const std::string line=record+"\r\n";
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<count;++i) {
        FILE* f=nullptr;
        if(_wfopen_s(&f,oldPath,L"ab") || !f)return 2;
        fprintf(f,"%s\r\n",record.c_str());fclose(f);
    }
    const auto middle=std::chrono::steady_clock::now();
    {
        edf::AppendLog log;
        for(int i=0;i<count;++i)log.Write(path,line.c_str(),static_cast<DWORD>(line.size()));
    }
    const auto end=std::chrono::steady_clock::now();
    printf("append/retry/concurrent integrity/live visibility: %s\n",pass ? "PASS" : "FAIL");
    printf("10000 x 202-byte records: reopen %.2f ms, persistent %.2f ms\n",
           std::chrono::duration<double,std::milli>(middle-start).count(),
           std::chrono::duration<double,std::milli>(end-middle).count());
    DeleteFileW(path);DeleteFileW(oldPath);
    return pass ? 0 : 3;
}
