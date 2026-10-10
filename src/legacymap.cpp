// EDF4.1's maps on EDF6 (plan P6): their collision, an EDF4.1 MAD's collision.hkt, is Havok 2014 physics
// (hkpPhysicsData: hkpRigidBody / hkpMoppBvTreeShape / hkpStorageExtendedMeshShape), EDF6's is hknp
// (hknpPhysicsSceneData). EDF.dll reads the 2014 tagfile (its reader is registered, Common::ReadFormatBinaryTagfile2014
// at 0x1FFB548), but the map loader 0x18C5E0 then asks the container for an hknpPhysicsSceneData only (0x18CB31) and
// uses the answer unchecked: [0+0x18] at 0x18CB4B, a crash on any 4.1 map. The ragdoll loader (0x11A5270) reads the
// same way and, finding an hkpPhysicsData, runs Havok's own hkp -> hknp migration 0x160B950 into a container of its
// own (EDF4.1 ragdolls load so: AntHill301, 2026-10-10). So does this, on the map path: the scene lookup at 0x18CB31
// is ours; it answers the stock lookup first (every EDF6 map: unchanged), and only when the container has no scene but
// an hkpPhysicsData migrates it and answers from the migrated container. The migration keeps each rigid body's name
// (0x160ABC0: cinfo+0x18 from hkpRigidBody+0xB8), which is how the map finds its pieces' collision (0x18DD30,
// '<model>.hkt'); MOPP and storage-extended meshes (4.1's) become hknpCompressedMeshShape (0x160BE90). Research:
// jobs/4bf89026/tmp/madload/REPORT.md. The migrated container lives as long as the process (16 bytes per map load;
// the bodies built from it reference what it holds).
#include "crew.h"
#include "memory.h"

namespace crew {
namespace {
constexpr std::size_t kSceneLookup=0x18CB31;        // call FindVariant(container, hknpPhysicsSceneData type, 0)
constexpr std::size_t kFindVariant=0xF09810;        // hkRootLevelContainer: first variant of a type (or null)
constexpr std::size_t kTypeOf=0x9710B0;             // class descriptor -> its reflected type
constexpr std::size_t kPhysicsDataClass=0x1FF8960;  // hkpPhysicsData's descriptor (the ragdoll path, 0x11A53E7)
constexpr std::size_t kMigrate=0x160B950;           // hkpPhysicsMigrationUtils(in, out, 0, 0, migrate constraints)

using FindFn=void*(__fastcall*)(void* container,void* type,void* previous);
using TypeFn=void*(__fastcall*)(const void* klass);
using MigrateFn=bool(__fastcall*)(void* in,void* out,void* a,void* b,bool constraints);

// hkRootLevelContainer: hkArray<NamedVariant> (data, size, capacity and flags), as Havok builds an empty one.
struct RootLevelContainer {
    void* data;
    int size;
    unsigned capacityAndFlags;
};

void* __fastcall FindScene(void* container,void* type,void* previous) {
    const auto find=reinterpret_cast<FindFn>(image+kFindVariant);
    if(void* scene=find(container,type,previous))return scene;
    void* physics=find(container,reinterpret_cast<TypeFn>(image+kTypeOf)(image+kPhysicsDataClass),nullptr);
    if(!physics) {
        Log("LEGACYMAP collision container %p holds neither hknp nor hkp physics (the map will not load)",container);
        return nullptr;
    }
    auto* migrated=new RootLevelContainer{nullptr,0,0x80000000u};
    const bool ok=reinterpret_cast<MigrateFn>(image+kMigrate)(container,migrated,nullptr,nullptr,false);
    void* scene=ok ? find(migrated,type,nullptr) : nullptr;
    Log("LEGACYMAP collision container %p: Havok 2014 hkpPhysicsData %p migrated (ok=%d) -> scene %p",
        container,physics,ok ? 1 : 0,scene);
    return scene;
}
}  // namespace

bool InstallLegacyMap() noexcept {
    bool changed=false;
    return RedirectCall(image+kSceneLookup,image+kFindVariant,reinterpret_cast<void*>(&FindScene),changed);
}
}  // namespace crew
