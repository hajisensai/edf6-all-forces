// The game's CreateObject, one definition for the plugin's scene objects (before 2026-10-20 each of airdrop.cpp,
// carrierlaser.cpp, debug_spawn.cpp, edf41classes.cpp, jet_bay.cpp, playerjet.cpp and ultimate.cpp kept its own copy of
// the address, the InitParam and the call).
//   0x11945E0(objectManager, const float matrix[16], const wchar_t* sgoPath, InitParam*) -> object or null: builds the SGO's
//   class (the SGO preloaded with 0x7A3780) at the matrix and puts it in the scene. Its InitParam is the class's; the plain
//   InitParamBase@SceneObject (vtable 0x1762068, 0x30 bytes, the rest zero) is what the stock scripts' plain objects get
//   (docs/player-jet-re.md §8, docs/debug-spawn-re.md). The object manager *(EDF+0x20B2958); delete 0x118A1B0 marks an
//   object deleted (+0x18 bit 2), the manager frees it later.
// The call may fault on a class that throws on its data (an SGO the plugin got wrong): the callers keep their __try (an
// SEH frame here would hide which feature it was).
#pragma once
#include "crew.h"
#include "memory.h"
#include <cstring>

namespace crew::scene {
constexpr unsigned kCreateObject=0x11945E0,kInitParamBaseVtable=0x1762068,kDeleteObject=0x118A1B0,kObjectManager=0x20B2958;
constexpr std::size_t kObjFlags=0x18;
constexpr unsigned char kObjDeleted=4;
struct alignas(16) InitParamBase { const void* vtable; unsigned char rest[0x28]; };
static_assert(sizeof(InitParamBase)==0x30,"InitParamBase@SceneObject's size");
using CreateFn=unsigned char*(*)(void*,const float*,const wchar_t*,void*);

inline void* ObjectManager() noexcept { return At<void*>(image,kObjectManager); }
// The object `sgo` built at `matrix` with the class's own InitParam `param` (an InitParamBase derivative), or null (no
// object manager: outside a mission's scene). The matrix is copied first: the SceneObject constructor 0x1189C00 loads its
// rows with MOVAPS (1189CB0..1189CCE; the native tests support_soldier_create_native / create_jet_native fault on a
// misaligned one), and a plain float[16] is aligned to 4 only.
inline unsigned char* CreateWith(const float* matrix,const wchar_t* sgo,void* param) noexcept {
    void* const mgr=ObjectManager();
    if(!mgr)return nullptr;
    alignas(16) float m[16];
    std::memcpy(m,matrix,sizeof(m));
    return reinterpret_cast<CreateFn>(image+kCreateObject)(mgr,m,sgo,param);
}
// The same with the plain InitParamBase.
inline unsigned char* CreateSceneObject(const float* matrix,const wchar_t* sgo) noexcept {
    InitParamBase param{image+kInitParamBaseVtable,{}};
    return CreateWith(matrix,sgo,&param);
}
inline void DeleteSceneObject(void* o) noexcept { reinterpret_cast<void(*)(void*)>(image+kDeleteObject)(o); }
}  // namespace crew::scene
