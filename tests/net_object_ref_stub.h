// The game-thread object references (src/net_object_resolve.h) for a fixture that fakes ReadNativeObjectId and
// ResolveMarkIdentities and has no players: every object is its NetId, found as the mark resolver's target. Include it
// once, inside no namespace, after those two fakes are declared.
#pragma once
#include "../src/command_identity.h"
#include "../src/net_object_ref.h"
#include "../src/support_soldier.h"
#include <cstring>

namespace crew {
namespace netref_stub {
int ambiguousSlot=-1;   // a player slot two objects answer (net_object_resolve.h NetResolved::ambiguous)
}
bool EncodeNetRef(const ObjRef& object,unsigned char out[32]) noexcept {
    unsigned char id[32]{};
    std::memset(out,0,32);
    return object.obj && ReadNativeObjectId(static_cast<const unsigned char*>(object.obj),id) && netref::MakeNative(id,out);
}
bool EncodeNetRef(const void* object,unsigned char out[32]) noexcept { return EncodeNetRef(ObjRef{object,nullptr},out); }
bool ResolveNetRef(const unsigned char* ref,NetLife,NetResolved* out) noexcept {
    *out=NetResolved{};
    if(netref::IsPlayer(ref,&out->slot)) {   // no players in the fixture's world: not here, or two of the slot
        out->ambiguous=out->slot==netref_stub::ambiguousSlot;
        return true;
    }
    MarkIdentities found{};
    if(!ResolveMarkIdentities(nullptr,ref,&found))return false;
    if(found.target){out->obj=found.target;out->found=true;}
    return true;
}
}
