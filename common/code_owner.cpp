// Who patched which code bytes (edf/code_owner.h).
#include "edf/code_owner.h"
#include <Windows.h>
#include <cstring>

namespace edf {
namespace {
// A few hundred patches a DLL (EDF6VehicleCrew: about 300 sites); a claim is found by a scan (claims are few and at
// install time, never in a frame's path).
constexpr unsigned kMostClaims=4096;
struct Entry { std::uintptr_t at;std::size_t size;const char* owner;Claim kind; };
Entry entries[kMostClaims];
unsigned used=0,conflicts=0,dropped=0;
ClaimConflict kept[kKeptConflicts];
unsigned keptCount=0;
ConflictSink sink=nullptr;
SRWLOCK lock=SRWLOCK_INIT;

bool SameFile(const char* a,const char* b) noexcept { return a==b || (a && b && !_stricmp(a,b)); }
}  // namespace

const char* SourceName(const char* path) noexcept {
    if(!path)return "?";
    const char* name=path;
    for(const char* p=path;*p;++p)if(*p=='\\' || *p=='/')name=p+1;
    return name;
}

bool ClaimCode(const void* at,std::size_t size,Claim kind,const char* owner) noexcept {
    if(!at || !size)return true;
    const char* const name=SourceName(owner);
    const auto begin=reinterpret_cast<std::uintptr_t>(at);
    ClaimConflict found{};
    bool clash=false;
    ConflictSink to=nullptr;
    AcquireSRWLockExclusive(&lock);
    bool known=false;
    for(unsigned i=0;i<used;++i) {
        const Entry& e=entries[i];
        if(e.at+e.size<=begin || begin+size<=e.at)continue;
        if(e.at==begin && e.size==size && e.kind==kind && SameFile(e.owner,name))known=true;
        if(!clash && kind==Claim::kSole && e.kind==Claim::kSole && !SameFile(e.owner,name)) {
            clash=true;
            found=ClaimConflict{begin,size,e.owner,name};
        }
    }
    if(!known) {
        if(used<kMostClaims)entries[used++]=Entry{begin,size,name,kind};
        else ++dropped;
    }
    if(clash) {
        ++conflicts;
        if(keptCount<kKeptConflicts)kept[keptCount++]=found;
        to=sink;
    }
    ReleaseSRWLockExclusive(&lock);
    if(to)to(found);
    return !clash;
}

void SetClaimConflictSink(ConflictSink to) noexcept {
    ClaimConflict replay[kKeptConflicts];
    unsigned n=0;
    AcquireSRWLockExclusive(&lock);
    sink=to;
    n=keptCount;
    std::memcpy(replay,kept,sizeof(ClaimConflict)*n);
    ReleaseSRWLockExclusive(&lock);
    if(to)
        for(unsigned i=0;i<n;++i)to(replay[i]);
}

const char* CodeOwner(const void* at) noexcept {
    const auto a=reinterpret_cast<std::uintptr_t>(at);
    const char* owner=nullptr;
    AcquireSRWLockShared(&lock);
    for(unsigned i=0;i<used && !owner;++i)
        if(entries[i].kind==Claim::kSole && entries[i].at<=a && a<entries[i].at+entries[i].size)owner=entries[i].owner;
    ReleaseSRWLockShared(&lock);
    return owner;
}

ClaimStats GetClaimStats() noexcept {
    AcquireSRWLockShared(&lock);
    const ClaimStats s{used,conflicts,dropped};
    ReleaseSRWLockShared(&lock);
    return s;
}

void ResetClaims() noexcept {
    AcquireSRWLockExclusive(&lock);
    used=conflicts=dropped=keptCount=0;
    sink=nullptr;
    ReleaseSRWLockExclusive(&lock);
}
}  // namespace edf
