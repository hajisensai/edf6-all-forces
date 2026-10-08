#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstring>

namespace crew {
// A recently stepped address is not a lifetime guarantee. Read game memory only before publishing, and tolerate a
// vehicle removed since the last input frame. In particular no external object is dereferenced under the HUD lock.
bool CommandVehicleLive(const ObjRef& ref) noexcept {
    __try {
        auto* v=static_cast<unsigned char*>(const_cast<void*>(ref.obj));
        return v && Readable(v,kSeatCount+8) && !v[kDead] && ref.Is(v) && SeatCount(v)>0 &&
               NpcDriver(v);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ReadCommandUnit(const ObjRef& ref,const char* name,const Command& cmd,bool air,CommandUnit* out) noexcept {
    __try {
        if(!CommandVehicleLive(ref))return false;
        CommandUnit unit{ref.obj,name,cmd,air,{}};
        std::memcpy(unit.pos,static_cast<const unsigned char*>(ref.obj)+kPosition,12);
        if(!std::isfinite(unit.pos[0]+unit.pos[1]+unit.pos[2]))return false;
        *out=unit;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
