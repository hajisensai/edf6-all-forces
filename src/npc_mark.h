// A mark/hover owns only a weak control-block reference, never the game object. Validate every use.
#pragma once
#include "crew.h"

namespace crew::npcmark {
constexpr std::size_t kFlags=0x18;
constexpr unsigned char kDeleted=4;

inline bool Alive(const ObjRef& ref) noexcept {
    // Readable caches regions for a tick; a removed object's page can change protection inside that tick.
    __try {
        if(!ref.obj || !ref.ctrl || !Readable(ref.ctrl,0x10) || At<long>(ref.ctrl,8)<=0)return false;
        const auto object=static_cast<const unsigned char*>(ref.obj);
        return Readable(object,kDead+1) && ref.Is(object) && !(object[kFlags]&kDeleted) && !object[kDead];
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

inline ObjRef Capture(const void* object) noexcept {
    __try {
        if(!Readable(object,kSelfCtrl+sizeof(void*)))return {};
        const ObjRef ref=ObjRef::Of(object);
        return Alive(ref) ? ref : ObjRef{};
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}

// The NPCs' side of the mark (their priority on it, the map's focus order): the custom NPC AI's.
inline bool Enabled() noexcept { return Cfg().enabled && Cfg().customNpcAi; }
// The Q mark itself (marking, keeping, showing, sharing; qmark.h): the plugin and its key, the AI or not. It replaces the
// stock spot (VanillaSpot=0), so it may not hang on the custom NPC AI (the user, 2026-10-10).
inline bool MarkOn() noexcept { return Cfg().enabled && Cfg().npcMarkKey>0; }

// Same MSVC weak-pointer contract as jet.cpp HoldRef/DropRef: pin the identity token until the cached reference goes.
// Without this, both an object address and its freed control-block address can be reused between two frames.
inline void Assign(ObjRef& held,const ObjRef& next) noexcept {
    if(held.obj==next.obj && held.ctrl==next.ctrl)return;
    if(next.ctrl)InterlockedIncrement(reinterpret_cast<volatile long*>(
        static_cast<unsigned char*>(const_cast<void*>(next.ctrl))+0xC));
    const ObjRef old=held;
    held=next;
    if(!old.ctrl)return;
    auto ctrl=static_cast<unsigned char*>(const_cast<void*>(old.ctrl));
    if(InterlockedDecrement(reinterpret_cast<volatile long*>(ctrl+0xC))==0) {
        using Delete=void(*)(void*);
        (*reinterpret_cast<Delete* const*>(ctrl))[1](ctrl);
    }
}
}  // namespace crew::npcmark
