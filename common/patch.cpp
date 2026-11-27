// Code and vtable patch primitives shared by EDF6VehicleCrew and EDF6AutoTurret.
#include "edf/patch.h"
#include "edf/memory.h"
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstring>

namespace edf {
namespace {
std::atomic<unsigned> restoreFailures{0};
}

void RestoreProtection(void* at,std::size_t size,DWORD previous) noexcept {
    DWORD ignored=0;
    if(!VirtualProtect(at,size,previous,&ignored))restoreFailures.fetch_add(1,std::memory_order_relaxed);
}

unsigned ProtectRestoreFailures() noexcept { return restoreFailures.load(std::memory_order_relaxed); }

bool Matches(const unsigned char* image,std::size_t rva,const unsigned char* bytes,std::size_t size) noexcept {
    return image && std::memcmp(image+rva,bytes,size)==0;
}

bool PatchVtableSlot(void** slot,void* expected,void* replacement,Claim kind,const char* owner) noexcept {
    ClaimCode(slot,sizeof(void*),kind,owner);
    DWORD old=0;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))return false;
    const bool ok=InterlockedCompareExchangePointer(slot,replacement,expected)==expected;
    RestoreProtection(slot,sizeof(void*),old);
    return ok;
}

bool ChainVtableSlot(void** slot,void* hook,void** next,const char* owner) noexcept {
    *next=nullptr;
    void* const current=*slot;
    if(!current || current==hook)return false;
    *next=current;
    if(PatchVtableSlot(slot,current,hook,Claim::kLayered,owner))return true;
    *next=nullptr;
    return false;
}

namespace {
// PatchCode's write, its bytes claimed by the caller already (one claim a patch: a conflict is told once).
bool WriteCode(unsigned char* at,const unsigned char* expected,const unsigned char* replacement,std::size_t size) noexcept {
    if(std::memcmp(at,replacement,size)==0)return true;
    if(std::memcmp(at,expected,size)!=0)return false;
    DWORD old=0;
    if(!VirtualProtect(at,size,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(at,replacement,size);
    RestoreProtection(at,size,old);
    FlushInstructionCache(GetCurrentProcess(),at,size);
    return true;
}
// RedirectToThunk's write, claimed by the caller already.
bool WriteRedirect(unsigned char* site,const unsigned char* expected,std::size_t size,const void* thunk,Branch branch,
                   unsigned char* written) noexcept {
    unsigned char code[kMostRedirectBytes];
    if(!expected || !BuildRedirect(site,size,thunk,branch,code) || !WriteCode(site,expected,code,size))return false;
    if(written)std::memcpy(written,code,size);
    return true;
}
}  // namespace

bool PatchCode(unsigned char* at,const unsigned char* expected,const unsigned char* replacement,std::size_t size,
               Claim kind,const char* owner) noexcept {
    ClaimCode(at,size,kind,owner);
    return WriteCode(at,expected,replacement,size);
}

bool BuildRedirect(const unsigned char* site,std::size_t size,const void* thunk,Branch branch,
                   unsigned char* out) noexcept {
    if(!site || !thunk || !out || size<5 || size>kMostRedirectBytes)return false;
    const auto delta=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(site+5);
    if(delta<INT32_MIN || delta>INT32_MAX)return false;
    const auto rel32=static_cast<std::int32_t>(delta);
    out[0]=static_cast<unsigned char>(branch);
    std::memcpy(out+1,&rel32,4);
    std::memset(out+5,0x90,size-5);
    return true;
}

bool RedirectToThunk(unsigned char* site,const unsigned char* expected,std::size_t size,const void* thunk,
                     Branch branch,unsigned char* written,Claim kind,const char* owner) noexcept {
    ClaimCode(site,size,kind,owner);   // before anything is looked at: a second owner is told even when refused
    return WriteRedirect(site,expected,size,thunk,branch,written);
}

const unsigned char* RedirectToCode(unsigned char* site,const unsigned char* expected,std::size_t size,
                                    const unsigned char* code,std::size_t codeSize,Branch branch,
                                    unsigned char* written,Claim kind,const char* owner) noexcept {
    ClaimCode(site,size,kind,owner);
    if(!site || !expected || !Readable(site,size) || std::memcmp(site,expected,size)!=0)return nullptr;
    void* const page=AllocateNearCode(site,code,codeSize);
    if(!page)return nullptr;
    if(WriteRedirect(site,expected,size,page,branch,written))return static_cast<const unsigned char*>(page);
    VirtualFree(page,0,MEM_RELEASE);
    return nullptr;
}

}  // namespace edf
