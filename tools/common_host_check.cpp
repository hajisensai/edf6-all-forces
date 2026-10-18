// edf6common's small helpers both plugins use at load (common/host.cpp, common/patch.cpp): the strict ini number,
// the DLL file line, a code patch putting the page's protection back.
#include "edf/host.h"
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
    std::printf(failures ? "common_host_check: %d failures\n" : "common_host_check: ok\n",failures);
    return failures ? 1 : 0;
}
