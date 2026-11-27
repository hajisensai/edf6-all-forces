// edf::RedirectToThunk / RedirectToCode / BuildRedirect (common/patch.cpp, edf/patch.h; design review 1126 P9) on a
// scratch code page of this process, read-only and executable as EDF.dll's code is: the site becomes a call (or jump)
// to the thunk and nops to its size, only while it holds the expected bytes; the page's protection is put back; the
// bytes are claimed for the calling file (edf/code_owner.h), refused redirects too; a thunk out of rel32 reach, a size
// it does not write, or bytes that differ leave the site untouched; RedirectToCode's page is given back when the site
// is not written; and the written call runs into the thunk and back.
#include "edf/code_owner.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
int checks=0,failures=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) {
        ++failures;
        std::printf("FAIL %s\n",what);
    }
}

// Nine stock bytes at +0 (two LEAs, as the berserker's publication site), then ret.
constexpr unsigned char kStock[]={0x4C,0x8D,0x44,0x24,0x48,0x48,0x8D,0x55,0xB7};

unsigned char* ScratchPage() {
    auto* const page=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    std::memset(page,0xCC,0x1000);
    std::memcpy(page,kStock,sizeof(kStock));
    page[sizeof(kStock)]=0xC3;
    DWORD old=0;
    VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&old);
    return page;
}
DWORD Protection(const void* at) {
    MEMORY_BASIC_INFORMATION m{};
    return VirtualQuery(at,&m,sizeof(m))==sizeof(m) ? m.Protect : 0;
}
unsigned char* Landing(unsigned char* site) {
    std::int32_t rel=0;
    std::memcpy(&rel,site+1,4);
    return site+5+rel;
}
bool Nops(const unsigned char* at,std::size_t n) {
    for(std::size_t i=0;i<n;++i)
        if(at[i]!=0x90)return false;
    return true;
}

void Builds() {
    unsigned char out[edf::kMostRedirectBytes];
    alignas(16) static unsigned char site[64];
    Check(edf::BuildRedirect(site,7,site+0x20,edf::Branch::kJump,out) && out[0]==0xE9 && Landing(out)-out==site+0x20-site &&
          Nops(out+5,2),"a jump: E9, rel32 from the site's end, nops to the size");
    Check(!edf::BuildRedirect(site,4,site+0x20,edf::Branch::kCall,out),"under 5 bytes: not written");
    Check(!edf::BuildRedirect(site,edf::kMostRedirectBytes+1,site+0x20,edf::Branch::kCall,out),"over the most: not written");
    Check(!edf::BuildRedirect(site,5,nullptr,edf::Branch::kCall,out),"no thunk: not written");
    const auto* distant=reinterpret_cast<const unsigned char*>(site)+0x80000000ull+0x100;
    Check(!edf::BuildRedirect(site,5,distant,edf::Branch::kCall,out),"a thunk 2 GB on: out of reach");
}

void ToThunk() {
    edf::ResetClaims();
    unsigned char* const page=ScratchPage();
    unsigned char* const thunk=page+0x800;
    unsigned char written[sizeof(kStock)]{};
    unsigned char wrong[sizeof(kStock)];
    std::memcpy(wrong,kStock,sizeof(wrong));
    wrong[3]^=1;
    Check(!edf::RedirectToThunk(page,wrong,sizeof(kStock),thunk,edf::Branch::kCall,written,edf::Claim::kSole,"src/a.cpp") &&
          !std::memcmp(page,kStock,sizeof(kStock)),"other bytes expected: refused, the site untouched");
    Check(edf::CodeOwner(page+8) && !std::strcmp(edf::CodeOwner(page+8),"a.cpp"),"a refused redirect is claimed too");
    const auto* distant=page+0x100000000ull;
    Check(!edf::RedirectToThunk(page,kStock,sizeof(kStock),distant,edf::Branch::kCall,written,edf::Claim::kSole,"src/a.cpp") &&
          !std::memcmp(page,kStock,sizeof(kStock)),"a thunk out of reach: refused, the site untouched");
    Check(edf::RedirectToThunk(page,kStock,sizeof(kStock),thunk,edf::Branch::kCall,written,edf::Claim::kSole,"src/a.cpp"),
          "the stock bytes: redirected");
    Check(page[0]==0xE8 && Landing(page)==thunk && Nops(page+5,sizeof(kStock)-5),"a call to the thunk, then nops");
    Check(!std::memcmp(written,page,sizeof(kStock)),"`written` holds what was written");
    Check(page[sizeof(kStock)]==0xC3,"the byte after the site untouched");
    Check(Protection(page)==PAGE_EXECUTE_READ && !edf::ProtectRestoreFailures(),"the page's protection put back");
    Check(edf::GetClaimStats().conflicts==0,"one file: no conflict");
    // The same redirect again: already there, true, nothing claimed twice.
    Check(edf::RedirectToThunk(page,kStock,sizeof(kStock),thunk,edf::Branch::kCall,nullptr,edf::Claim::kSole,"src/a.cpp") &&
          edf::GetClaimStats().claims==1,"the same redirect again: already written, one claim");
    // Another file to its own thunk over the written site: refused by the bytes, the conflict told once.
    Check(!edf::RedirectToThunk(page,kStock,sizeof(kStock),page+0x900,edf::Branch::kCall,nullptr,edf::Claim::kSole,
                                "src/b.cpp") && edf::GetClaimStats().conflicts==1 && Landing(page)==thunk,
          "a second file: refused, one conflict, the first redirect kept");
    VirtualFree(page,0,MEM_RELEASE);
}

// The thunk (mov eax,7; ret) reached by the redirected site's call: the site's code (call; nops; ret) returns 7.
void Runs() {
    edf::ResetClaims();
    unsigned char* const page=ScratchPage();
    const unsigned char code[]={0xB8,0x07,0x00,0x00,0x00,0xC3};
    const unsigned char* const thunk=edf::RedirectToCode(page,kStock,sizeof(kStock),code,sizeof(code));
    Check(thunk && Landing(page)==thunk && !std::memcmp(thunk,code,sizeof(code)),"RedirectToCode: the code near the site");
    Check(thunk && Protection(thunk)==PAGE_EXECUTE_READ,"its page executable, read-only");
    Check(edf::CodeOwner(page) && !std::strcmp(edf::CodeOwner(page),"redirect_thunk_test.cpp"),
          "the owner is the calling file (its default argument)");
    if(thunk)Check(reinterpret_cast<int(*)()>(page)()==7,"the redirected site runs the thunk and comes back");
    // Not the stock bytes now: nothing written, the page given back (VirtualAlloc'd pages counted by a query near it).
    const unsigned char* const again=edf::RedirectToCode(page,kStock,sizeof(kStock),code,sizeof(code));
    Check(!again && Landing(page)==thunk,"not the stock bytes: no page, the site as it was");
    if(thunk)VirtualFree(const_cast<unsigned char*>(thunk),0,MEM_RELEASE);
    VirtualFree(page,0,MEM_RELEASE);
}

// A page made for the stub above `from` and ending by `below`; none when no room is left between them.
void Above() {
    unsigned char* const base=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x10000,MEM_RESERVE,PAGE_NOACCESS));
    const unsigned char code[]={0xC3};
    auto* const page=static_cast<unsigned char*>(edf::AllocateCodeAbove(base,base+0x40000000,code,sizeof(code)));
    Check(page && page>=base+0x10000 && page+0x1000<=base+0x40000000 && page[0]==0xC3 &&
          Protection(page)==PAGE_EXECUTE_READ,"AllocateCodeAbove: past the reserved range, before the limit, executable");
    Check(!edf::AllocateCodeAbove(base,base+0x1000,code,sizeof(code)),"no room below the limit: none");
    if(page)VirtualFree(page,0,MEM_RELEASE);
    VirtualFree(base,0,MEM_RELEASE);
}
}  // namespace

int main() {
    Builds();
    ToThunk();
    Runs();
    Above();
    std::printf("redirect_thunk_test: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
