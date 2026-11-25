// The code owner registry (common/code_owner.cpp, edf/code_owner.h) through the patch primitives themselves, on a code
// page of this process: two source files redirecting one call site is a conflict (sent to the sink, replayed when the
// sink comes later), whatever the second redirect did (refused here: the site no longer calls what it expected, as the
// first person's culling was when the frame rate redirected 0x1198FA4 first, #395); a chain (kLayered) and the same file
// again are not; PatchCode over a part of a claimed site is; bytes next to it are not; the owner is the calling file
// (__builtin_FILE() filled at the call), compared by name.
#include "edf/code_owner.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
int checks=0,failures=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) {
        ++failures;
        std::printf("FAIL %s\n",what);
    }
}

struct Seen { std::uintptr_t at;std::size_t size;std::string first,second; };
std::vector<Seen> seen;
void Sink(const edf::ClaimConflict& c) noexcept { seen.push_back(Seen{c.at,c.size,c.first,c.second}); }

int __cdecl HookA() { return 1; }
int __cdecl HookB() { return 2; }
int __cdecl HookChain() { return 3; }

// A page holding `call target` at +0 (E8 rel32) and the target (ret) at +0x100.
unsigned char* CodePage() {
    auto* const page=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    std::memset(page,0xCC,0x1000);
    page[0]=0xE8;
    const std::int32_t rel=0x100-5;
    std::memcpy(page+1,&rel,4);
    page[0x100]=0xC3;
    return page;
}
unsigned char* Target(unsigned char* site) {
    std::int32_t rel=0;
    std::memcpy(&rel,site+1,4);
    return site+5+rel;
}

void Redirects() {
    edf::ResetClaims();seen.clear();
    unsigned char* const page=CodePage();
    unsigned char* const stock=page+0x100;
    bool changed=false;
    Check(edf::RedirectCall(page,stock,reinterpret_cast<void*>(&HookA),changed,edf::Claim::kSole,"D:\\x\\src\\first.cpp") &&
          changed,"the first file's redirect is written");
    Check(edf::GetClaimStats().claims==1 && !edf::GetClaimStats().conflicts,"its 5 bytes claimed, no conflict");
    Check(edf::RedirectCall(page,stock,reinterpret_cast<void*>(&HookA),changed,edf::Claim::kSole,"src/first.cpp")==false &&
          !edf::GetClaimStats().conflicts,"the same file again (another spelling of its path): refused by the bytes, no conflict");
    // The second file: the site no longer calls the stock target, so its redirect is refused, and the conflict is told.
    Check(!edf::RedirectCall(page,stock,reinterpret_cast<void*>(&HookB),changed,edf::Claim::kSole,"src/second.cpp") && !changed,
          "a second file's redirect of the site is refused (the site calls the first's hook)");
    const edf::ClaimStats s=edf::GetClaimStats();
    Check(s.conflicts==1,"the second file's claim is a conflict");
    Check(seen.empty(),"no sink yet: nothing sent");
    edf::SetClaimConflictSink(&Sink);
    Check(seen.size()==1 && seen[0].at==reinterpret_cast<std::uintptr_t>(page) && seen[0].size==5 &&
          seen[0].first=="first.cpp" && seen[0].second=="second.cpp",
          "the sink set later gets the conflict replayed: the site, the first and the second file by name");
    Check(std::strcmp(edf::CodeOwner(page+3),"first.cpp")==0,"the site's owner is the first file");
    // A chain over whatever the site calls now: layered, no conflict either way.
    unsigned char* const now=Target(page);
    Check(edf::RedirectCall(page,now,reinterpret_cast<void*>(&HookChain),changed,edf::Claim::kLayered,"src/perf_probe.cpp") &&
          changed && seen.size()==1,"a layered chain over the first's redirect: written, no conflict");
    // A third file writing two bytes inside the site: a conflict (sent at once now).
    const unsigned char expected[2]={page[2],page[3]};
    const unsigned char nops[2]={0x90,0x90};
    edf::PatchCode(page+2,expected,nops,2,edf::Claim::kSole,"src/third.cpp");
    Check(seen.size()==2 && seen[1].first=="first.cpp" && seen[1].second=="third.cpp" &&
          seen[1].at==reinterpret_cast<std::uintptr_t>(page+2),"PatchCode over a part of the site: a conflict, sent at once");
    // Bytes right after the site: no conflict.
    const unsigned char after[1]={page[5]};
    const unsigned char nop[1]={0x90};
    Check(edf::PatchCode(page+5,after,nop,1,edf::Claim::kSole,"src/third.cpp") && seen.size()==2,
          "PatchCode right after the site: no conflict");
    // The owner filled at the call: this file.
    unsigned char* const other=page+0x200;
    edf::ClaimCode(other,4);
    Check(std::strcmp(edf::CodeOwner(other),"code_owner_test.cpp")==0,"the default owner is the calling file (__builtin_FILE)");
    edf::ResetClaims();
    VirtualFree(page,0,MEM_RELEASE);
}

void Vtables() {
    edf::ResetClaims();seen.clear();
    edf::SetClaimConflictSink(&Sink);
    void* table[2]={reinterpret_cast<void*>(&HookA),reinterpret_cast<void*>(&HookB)};
    Check(edf::PatchVtableSlot(&table[0],reinterpret_cast<void*>(&HookA),reinterpret_cast<void*>(&HookB),edf::Claim::kSole,
                               "src/one.cpp"),"a vtable slot patched");
    void* next=nullptr;
    Check(edf::ChainVtableSlot(&table[0],reinterpret_cast<void*>(&HookChain),&next,"src/two.cpp") && seen.empty(),
          "a chain over it: layered, no conflict");
    Check(!edf::PatchVtableSlot(&table[0],reinterpret_cast<void*>(&HookA),reinterpret_cast<void*>(&HookB),edf::Claim::kSole,
                                "src/three.cpp") && seen.size()==1 && seen[0].first=="one.cpp" && seen[0].second=="three.cpp",
          "a second sole patch of the slot: a conflict");
    edf::ResetClaims();
}

void Room() {
    edf::ResetClaims();seen.clear();
    static unsigned char bytes[edf::kKeptConflicts*2+8];
    // Conflicts past the kept ones are counted, not replayed.
    for(unsigned i=0;i<edf::kKeptConflicts+4;++i) {
        edf::ClaimCode(bytes+i,1,edf::Claim::kSole,"a.cpp");
        edf::ClaimCode(bytes+i,1,edf::Claim::kSole,"b.cpp");
    }
    edf::SetClaimConflictSink(&Sink);
    Check(edf::GetClaimStats().conflicts==edf::kKeptConflicts+4 && seen.size()==edf::kKeptConflicts,
          "conflicts past the kept ones: counted, the kept ones replayed");
    edf::ResetClaims();
}
}  // namespace

int main() {
    Redirects();
    Vtables();
    Room();
    std::printf("code_owner: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
