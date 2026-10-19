// Link this fixture with the separate production src/thermal_models.cpp unit.
// No native code is loaded/executed: a private PE/RTTI image carries its layout.
#include "../src/crew.h"
#include "../src/thermal_models.h"
#include "edf/memory.h"
#include <array>
#include <cstdio>

namespace crew { unsigned char* image=nullptr; }
namespace edf {
bool Readable(const void* p,std::size_t n,bool) noexcept {
    if(!p)return false;
    MEMORY_BASIC_INFORMATION m{};
    if(!VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
    const auto at=reinterpret_cast<std::uintptr_t>(p),end=reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize;
    return at<=end && n<=end-at;
}
}  // namespace edf

namespace {
int checks=0,failed=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failed;std::printf("FAIL: %s\n",what);}
}
template<class T> void Set(void* p,std::size_t off,T value) {
    std::memcpy(static_cast<unsigned char*>(p)+off,&value,sizeof(value));
}
// One complete native class and a nonvirtual primary base at offset zero.
const void* Rtti(unsigned rva,const char* name) {
    auto* p=crew::image;
    Set(p,rva+0u,std::uint32_t(1));Set(p,rva+4u,std::uint32_t(0));Set(p,rva+8u,std::uint32_t(0));
    Set(p,rva+12u,rva+0x100u);Set(p,rva+16u,rva+0x200u);Set(p,rva+20u,rva);
    std::memcpy(p+rva+0x110u,name,std::strlen(name)+1);
    Set(p,rva+0x208u,std::uint32_t(1));Set(p,rva+0x20Cu,rva+0x300u);Set(p,rva+0x300u,rva+0x400u);
    Set(p,rva+0x400u,rva+0x100u);Set(p,rva+0x408u,std::int32_t(0));
    Set(p,rva+0x40Cu,std::int32_t(-1));Set(p,rva+0x410u,std::int32_t(0));
    return p+rva;
}
void Model(void* model) {
    Set(model,0,crew::image+0x17C4030);
    Set(model,0x10,reinterpret_cast<void*>(1)); // registered Umbra object, never dereferenced
}
using Clone=std::array<const void*,3>; // [-1] COL followed by two readable slots
using Object=std::array<unsigned char,0x2000>;
}  // namespace

int main() {
    crew::image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!crew::image)return 2;
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(crew::image);
    dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x100;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(crew::image+0x100);
    nt->Signature=IMAGE_NT_SIGNATURE;nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.TimeDateStamp=0x678CCB46;nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfImage=0x2200000;

    const Clone insectClone{Rtti(0x1801000,".?AVInsectBase@@"),nullptr,nullptr};
    const Clone humanoidClone{Rtti(0x1802000,".?AVHumanoidBase@@"),nullptr,nullptr};
    const Clone unknownClone{Rtti(0x1803000,".?AVUnknownObject@@"),nullptr,nullptr};
    std::array<std::uint32_t,6> foreignCol{1,0,0,0,0,0};
    const Clone foreignClone{foreignCol.data(),nullptr,nullptr};
    const void* out[8]{};
    Object object{};
    Model(object.data()+0x630);
    Set(object.data(),0,insectClone.data()+1);
    Check(crew::thermal::EnemyModels(object.data(),out,8)==1 && out[0]==object.data()+0x630,
          "EDF4.1-style external vtable clone retaining native COL resolves its model");
    Check(crew::thermal::EnemyModel(object.data())==object.data()+0x630,"single-model convenience accessor");

    // The exact same object address changes class. A cache of the object/layout
    // rather than its current vtable would keep returning the insect's +0x630.
    std::array<unsigned char,0x710*3> models{};
    for(unsigned i=0;i<3;++i)Model(models.data()+i*0x710);
    Set(object.data(),0,humanoidClone.data()+1);
    Set(object.data(),0x6C8,models.data());Set(object.data(),0x6D8,std::size_t(3));
    Check(crew::thermal::EnemyModels(object.data(),out,8)==3,"same address with humanoid vtable enumerates all three body/limb entries");
    for(unsigned i=0;i<3;++i)Check(out[i]==models.data()+i*0x710,"humanoid entry has its own model identity");
    Check(crew::thermal::EnemyModels(object.data(),out,2)==2,"caller capacity bounds the written model pointers");

    // Array storage belongs to the native manager and can change between updates.
    std::array<unsigned char,0x710*2> replacement{};
    for(unsigned i=0;i<2;++i)Model(replacement.data()+i*0x710);
    Set(object.data(),0x6C8,replacement.data());Set(object.data(),0x6D8,std::size_t(2));
    Check(crew::thermal::EnemyModels(object.data(),out,8)==2 && out[0]==replacement.data() && out[1]==replacement.data()+0x710,
          "current native array and count replace the previous frame's pointers");
    Set(replacement.data(),0x10,static_cast<void*>(nullptr));
    Check(crew::thermal::EnemyModels(object.data(),out,8)==1 && out[0]==replacement.data()+0x710,
          "unregistered model is excluded without losing a registered sibling");

    Set(object.data(),0,foreignClone.data()+1);
    Check(crew::thermal::EnemyModels(object.data(),out,8)==0,"foreign COL cannot borrow a known object layout");
    Set(object.data(),0,unknownClone.data()+1);
    Check(crew::thermal::EnemyModels(object.data(),out,8)==0,"unknown native RTTI does not guess a model member");
    Set(object.data(),0,insectClone.data()+1);
    Check(crew::thermal::EnemyModels(object.data(),out,8)==1 && out[0]==object.data()+0x630,
          "same object can return from rejected classes to its original vtable");
    Check(crew::thermal::EnemyModels(nullptr,out,8)==0 && crew::thermal::EnemyModels(object.data(),nullptr,8)==0 &&
          crew::thermal::EnemyModels(object.data(),out,0)==0,"empty inputs produce no pointers");

    VirtualFree(crew::image,0,MEM_RELEASE);crew::image=nullptr;
    std::printf("thermal models: %d checks, %d failures\n",checks,failed);
    return failed ? 1 : 0;
}
