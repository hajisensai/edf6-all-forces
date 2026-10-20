// A FarEventObject of the plugin's: a model drawn where the plugin puts it, nothing more (the parachute's canopy,
// playerjet.cpp; the ultimate strike's magic circles and shock waves, ultimate.cpp). docs/player-jet-re.md §8: its update
// (vtable slot 5, 0x5C4FB0) only renders its model at its matrix (+0x60..+0x9F) and ticks an optional animation, never
// writing that matrix, so the plugin's write each frame moves it (H); its physics body comes only from a "ragdoll" entry
// (0x5C4970, H) the plugin's SGOs have none of: nothing collides with it (M). Made with the plain InitParamBase
// (scene_object.h), its class checked by its vtable (another class: deleted at once), put on the neutral team (hostile
// to nobody: no enemy turns on it, M). Remembered by its address and weak-this control block (ObjRef): a new object at the
// same address is not taken for it. Game thread only.
#pragma once
#include "crew.h"
#include "memory.h"
#include "scene_object.h"
#include <cstring>

namespace crew::farobj {
constexpr unsigned kVtable=0x17D5DA0,kUpdate=0x5C4FB0;
constexpr std::size_t kMatrixAt=0x60;

enum class Made : std::uint8_t { made, faulted, none, notFar };
inline const char* MadeText(Made m) noexcept {
    switch(m) {
        case Made::made: return "made";
        case Made::faulted: return "the game faulted building it";
        case Made::none: return "the game made no object";
        case Made::notFar: return "not a FarEventObject (deleted)";
    }
    return "?";
}

// Whether `r` is still the object made: there, a FarEventObject, the same control block, not deleted.
inline bool Live(const ObjRef& r) noexcept {
    const auto* o=static_cast<const unsigned char*>(r.obj);
    return o && Readable(o,kTeam+4) && At<const void*>(o,0)==image+kVtable && r.Is(o) && !(o[scene::kObjFlags]&scene::kObjDeleted);
}
// `sgo` (preloaded) at `matrix`; `why` (optional) what came of it. Empty when not made.
inline ObjRef Make(const wchar_t* sgo,const float* matrix,Made* why=nullptr) noexcept {
    Made m=Made::none;
    unsigned char* o=nullptr;
    __try { o=scene::CreateSceneObject(matrix,sgo); }
    __except(EXCEPTION_EXECUTE_HANDLER){m=Made::faulted;o=nullptr;}
    ObjRef r{};
    if(o) {
        __try {
            if(At<const void*>(o,0)!=image+kVtable){scene::DeleteSceneObject(o);m=Made::notFar;}
            else {SetObjectTeam(o,kTeamNeutral);r=ObjRef::Of(o);m=Made::made;}
        } __except(EXCEPTION_EXECUTE_HANDLER){m=Made::faulted;r=ObjRef{};}
    }
    if(why)*why=m;
    return r;
}
// Put at `matrix` (16 floats); false: gone (then forgotten by the caller).
inline bool Move(const ObjRef& r,const float* matrix) noexcept {
    __try {
        if(!Live(r))return false;
        std::memcpy(static_cast<unsigned char*>(const_cast<void*>(r.obj))+kMatrixAt,matrix,64);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Deleted while it is there; forgotten. False: it was gone already.
inline bool Drop(ObjRef& r) noexcept {
    const ObjRef was=r;
    r=ObjRef{};
    __try {
        if(!Live(was))return false;
        scene::DeleteSceneObject(const_cast<void*>(was.obj));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew::farobj
