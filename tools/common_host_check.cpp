// edf6common's small helpers both plugins use at load (common/host.cpp, common/patch.cpp): the strict ini number,
// the DLL file line, a code patch putting the page's protection back; Readable's batched page asks (PrefetchReadable).
#include "edf/host.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <cstdio>
#include <cstring>

namespace {
int failures=0;
void Expect(bool ok,const char* what) {
    if(!ok){std::printf("FAIL %s\n",what);++failures;}
}

bool Parsed(const wchar_t* text,float want) {
    float v=-12345.0f;
    return edf::ParseFloat(text,&v) && v==want;
}

bool Refused(const wchar_t* text) {
    float v=7.0f;
    return !edf::ParseFloat(text,&v) && v==7.0f;
}
}  // namespace

int main() {
    Expect(Parsed(L"1.5",1.5f),"plain decimal");
    Expect(Parsed(L"  -2  ",-2.0f),"blanks around");
    Expect(Parsed(L"1e2",100.0f),"exponent");
    Expect(Parsed(L"0",0.0f),"zero");
    Expect(Refused(L""),"empty");
    Expect(Refused(L"   "),"blank");
    Expect(Refused(L"12abc"),"trailing letters");
    Expect(Refused(L"1,5"),"decimal comma");
    Expect(Refused(L"inf"),"infinity");
    Expect(Refused(L"nan"),"nan");
    Expect(Refused(L"1e99"),"overflow");
    Expect(Refused(nullptr),"null");

    char line[96];
    edf::DescribeModuleFile(nullptr,line,sizeof(line));   // the exe itself
    Expect(std::strstr(line," bytes, written ")!=nullptr && std::strstr(line," UTC")!=nullptr,"module file line");
    std::printf("module file: %s\n",line);

    // A code patch on a read-only executable page: written, the page read-only again, no restore failure counted.
    auto* page=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Expect(page!=nullptr,"page");
    if(page) {
        const unsigned char stock[]={0x48,0x89,0x5C,0x24},patched[]={0xE9,0x01,0x02,0x03},other[]={0x90,0x90,0x90,0x90};
        std::memcpy(page,stock,sizeof(stock));
        DWORD old=0;
        VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&old);
        Expect(!edf::PatchCode(page,other,patched,sizeof(patched)),"refuses unexpected bytes");
        Expect(std::memcmp(page,stock,sizeof(stock))==0,"unexpected bytes left alone");
        Expect(edf::PatchCode(page,stock,patched,sizeof(patched)),"patches the expected bytes");
        Expect(std::memcmp(page,patched,sizeof(patched))==0,"bytes written");
        MEMORY_BASIC_INFORMATION info{};
        Expect(VirtualQuery(page,&info,sizeof(info)) && info.Protect==PAGE_EXECUTE_READ,"protection put back");
        Expect(edf::PatchCode(page,stock,patched,sizeof(patched)),"already patched is true");
        Expect(edf::ProtectRestoreFailures()==0,"no restore failure counted");
        VirtualFree(page,0,MEM_RELEASE);
    }
    // PrefetchReadable: the pages of a batch asked in one call (64 pages a call), Readable's answers after it the ones it
    // gives on its own (read-write, read-only, no access, a guard page, decommitted), with no call of its own.
    {
        constexpr int kPages=100;
        auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,kPages*0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        Expect(block!=nullptr,"block");
        if(block) {
            DWORD old=0;
            VirtualProtect(block+3*0x1000,0x1000,PAGE_READONLY,&old);
            VirtualProtect(block+5*0x1000,0x1000,PAGE_NOACCESS,&old);
            VirtualProtect(block+7*0x1000,0x1000,PAGE_READWRITE|PAGE_GUARD,&old);
            VirtualFree(block+9*0x1000,0x1000,MEM_DECOMMIT);
            for(int i=20;i<kPages;i+=2)block[i*0x1000]=1;   // some in the working set, some never touched
            const void* ptrs[kPages+2];
            for(int i=0;i<kPages;++i)ptrs[i]=block+i*0x1000;
            ptrs[kPages]=nullptr;
            ptrs[kPages+1]=block+11*0x1000+0xC00;   // across two pages
            const auto truth=[&](int i,bool writable) {
                if(!ptrs[i])return false;
                const auto at=reinterpret_cast<std::uintptr_t>(ptrs[i]);
                for(std::uintptr_t page=at&~std::uintptr_t{0xFFF};page<at+0x800;page+=0x1000) {
                    MEMORY_BASIC_INFORMATION info{};
                    if(!VirtualQuery(reinterpret_cast<void*>(page),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
                       (info.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
                    const DWORD p=info.Protect&0xFF;
                    if(p!=PAGE_READWRITE && p!=PAGE_READONLY)return false;
                    if(writable && p!=PAGE_READWRITE)return false;
                }
                return true;
            };
            bool done=false;
            for(int attempt=0;attempt<200 && !done;++attempt) {
                const ULONGLONG tick=GetTickCount64();
                edf::TakeReadableStats();
                edf::PrefetchReadable(ptrs,0x800,kPages+2);
                const edf::ReadableStats batch=edf::TakeReadableStats();
                int wrong=0;
                for(int i=0;i<kPages+2;++i)
                    wrong+=(edf::Readable(ptrs[i],0x800)!=truth(i,false)) || (edf::Readable(ptrs[i],0x800,true)!=truth(i,true));
                const edf::ReadableStats after=edf::TakeReadableStats();
                if(GetTickCount64()!=tick)continue;   // the tick turned in between: the cache's answers are gone, again
                done=true;
                Expect(batch.wsCalls==2 && batch.wsPages==kPages,"100 pages asked in two calls");
                Expect(wrong==0,"Readable's answers after the batch are the pages' own");
                Expect(after.wsCalls==0,"Readable asks the working set nothing more in the tick");
                edf::PrefetchReadable(ptrs,0x800,kPages+2);
                Expect(edf::TakeReadableStats().wsCalls==0 || GetTickCount64()!=tick,"a batch already kept is not asked again");
            }
            Expect(done,"a tick long enough to test the batch in");
            VirtualFree(block,0,MEM_RELEASE);
        }
    }
    std::printf(failures ? "common_host_check: %d failures\n" : "common_host_check: ok\n",failures);
    return failures ? 1 : 0;
}
