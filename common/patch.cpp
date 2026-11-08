// Code and vtable patch primitives shared by EDF6VehicleCrew and EDF6AutoTurret.
#include "edf/patch.h"
#include "edf/memory.h"
#include <atomic>
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
bool RedirectImportCall(unsigned char* site,const unsigned char* sig,void* hook,void** slotValue) noexcept {
    if(!Readable(site,6) || std::memcmp(site,sig,6)!=0)return false;
    std::int32_t rel=0;
    std::memcpy(&rel,sig+2,4);
    void* const* slot=reinterpret_cast<void* const*>(site+6+rel);
    if(!Readable(slot,8) || !*slot)return false;
    if(*slotValue && *slotValue!=*slot)return false;   // several sites of one import share its slot
    *slotValue=*slot;
    void* const thunk=AllocateNearThunk(site,hook);
    if(!thunk)return false;
    const auto delta=static_cast<unsigned char*>(thunk)-(site+5);
    if(delta>0x7FFFFFF0 || delta<-0x7FFFFFF0){VirtualFree(thunk,0,MEM_RELEASE);return false;}
    unsigned char code[6]={0xE8,0,0,0,0,0x90};
    const auto rel32=static_cast<std::int32_t>(delta);
    std::memcpy(code+1,&rel32,4);
    if(PatchCode(site,sig,code,sizeof(code)))return true;
    VirtualFree(thunk,0,MEM_RELEASE);
    return false;
}
}  // namespace edf
