// Code and vtable patch primitives shared by EDF6VehicleCrew and EDF6AutoTurret.
#include "edf/patch.h"
#include <cstring>

namespace edf {
bool Matches(const unsigned char* image,std::size_t rva,const unsigned char* bytes,std::size_t size) noexcept {
    return image && std::memcmp(image+rva,bytes,size)==0;
}

bool PatchVtableSlot(void** slot,void* expected,void* replacement) noexcept {
    DWORD old=0;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))return false;
    const bool ok=InterlockedCompareExchangePointer(slot,replacement,expected)==expected;
    VirtualProtect(slot,sizeof(void*),old,&old);
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
    VirtualProtect(at,size,old,&old);
    FlushInstructionCache(GetCurrentProcess(),at,size);
    return true;
}
}  // namespace edf
