// edf::RedirectImportCall (edf/patch.h), apart from the other patch primitives: it is the one that needs the near thunk
// of memory.cpp, and a test standing in its own Readable / AllocateNearCode / RedirectCall (the fixtures over a fake
// image: tests/enemy_control_pvp_berserker_test.cpp) links patch.cpp without memory.cpp.
#include "edf/patch.h"
#include "edf/memory.h"
#include <cstdint>
#include <cstring>

namespace edf {
bool RedirectImportCall(unsigned char* site,const unsigned char* sig,void* hook,void** slotValue,
                        const char* owner) noexcept {
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
    if(PatchCode(site,sig,code,sizeof(code),Claim::kSole,owner))return true;
    VirtualFree(thunk,0,MEM_RELEASE);
    return false;
}
}  // namespace edf
