// Code and vtable patch primitives shared by EDF6VehicleCrew and EDF6AutoTurret.
#include "edf/patch.h"
#include <atomic>
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

bool PatchVtableSlot(void** slot,void* expected,void* replacement) noexcept {
    DWORD old=0;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))return false;
    const bool ok=InterlockedCompareExchangePointer(slot,replacement,expected)==expected;
    RestoreProtection(slot,sizeof(void*),old);
    return ok;
}

bool ChainVtableSlot(void** slot,void* hook,void** next) noexcept {
    *next=nullptr;
    void* const current=*slot;
    if(!current || current==hook)return false;
    *next=current;
    if(PatchVtableSlot(slot,current,hook))return true;
    *next=nullptr;
    return false;
}

bool PatchCode(unsigned char* at,const unsigned char* expected,const unsigned char* replacement,std::size_t size) noexcept {
    if(std::memcmp(at,replacement,size)==0)return true;
    if(std::memcmp(at,expected,size)!=0)return false;
    DWORD old=0;
    if(!VirtualProtect(at,size,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(at,replacement,size);
    RestoreProtection(at,size,old);
    FlushInstructionCache(GetCurrentProcess(),at,size);
    return true;
}
}  // namespace edf
