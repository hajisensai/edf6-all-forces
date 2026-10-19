// A retained native weak identity has three states. Unreadable storage while
// strong owners still exist is pending restoration, never proof of destruction.
#pragma once
#include "memory.h"
#include <cstring>

namespace crew::enemy_identity {
enum class State { current,gone,unknown };
inline State Read(const void* object,const void* control,std::size_t extent=0x38) noexcept {
    if(!object && !control)return State::gone;
    if(!object || !control)return State::unknown;
    __try {
        if(!Readable(control,12))return State::unknown;
        LONG strong=0;std::memcpy(&strong,static_cast<const unsigned char*>(control)+8,sizeof(strong));
        if(strong<=0)return State::gone;
        if(!Readable(object,0x38))return State::unknown;
        const void* actual=nullptr;std::memcpy(&actual,static_cast<const unsigned char*>(object)+0x30,sizeof(actual));
        if(actual!=control)return State::gone;
        return Readable(object,extent) ? State::current : State::unknown;
    } __except(EXCEPTION_EXECUTE_HANDLER){return State::unknown;}
}
}