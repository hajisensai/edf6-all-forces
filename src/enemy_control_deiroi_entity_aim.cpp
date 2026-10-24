// Exact TypeEntity gate and generated x64 contracts are audited by the paired fixture.
#include "enemy_control_deiroi_entity_aim.h"
#include "enemy_control_deiroi_net.h"
#include "crew.h"
#include "memory.h"
#include "edf/patch.h"
#include <cstring>

namespace crew::enemy_native {
namespace deiroi_entity_aim {
constexpr unsigned kSite=0x6CF95A,kNative=0x6CFA71;
constexpr unsigned char kOriginal[]={0x48,0x85,0xD2,0x0F,0x84,0x0E,0x01,0x00,0x00,0x8B,0x42,0x08,0x85,0xC0};
struct Code {
    unsigned char bytes[512]{};std::size_t size=0;
    void Byte(unsigned char value) noexcept {bytes[size++]=value;}
    void Data(const unsigned char* values,std::size_t count) noexcept {std::memcpy(bytes+size,values,count);size+=count;}
    void U32(std::uint32_t value) noexcept {std::memcpy(bytes+size,&value,4);size+=4;}
    void Ptr(const void* pointer) noexcept {std::memcpy(bytes+size,&pointer,8);size+=8;}
};
unsigned char* admitted=nullptr;void* thunk=nullptr;Code installed{};unsigned char siteBytes[sizeof(kOriginal)]{};
bool __fastcall Skip(const void* parameter) noexcept {
    __try {return DeiroiAcceptedEntityFactoryScope(parameter);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Jump(Code& code,const void* target) noexcept {
    constexpr unsigned char op[]={0xFF,0x25,0,0,0,0};code.Data(op,sizeof(op));code.Ptr(target);
}
void Vectors(Code& code,bool load) noexcept {
    for(unsigned i=0;i<6;++i) {
        const unsigned char op[]={0xF3,0x0F,static_cast<unsigned char>(load ? 0x6F : 0x7F),
            static_cast<unsigned char>(0x44|(i<<3)),0x24,static_cast<unsigned char>(0x20+i*16)};
        code.Data(op,sizeof(op));
    }
}
void Restore(Code& code) noexcept {
    Vectors(code,true);
    constexpr unsigned char op[]={0x48,0x81,0xC4,0x80,0,0,0,0x41,0x5B,0x41,0x5A,0x41,0x59,0x41,0x58,0x5A,0x59,0x58,0x9D};
    code.Data(op,sizeof(op));
}
Code Build() noexcept {
    Code code{};
    // At6CF95A RSP is16-aligned after the original five pushes/sub80.
    // Preserve flags and ALL volatile registers, with32B Windows call shadow space.
    constexpr unsigned char enter[]={0x9C,0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53,
        0x48,0x81,0xEC,0x80,0,0,0,0xFC};
    code.Data(enter,sizeof(enter));Vectors(code,false);
    constexpr unsigned char call[]={0x48,0x8B,0xCD,0x48,0xB8};code.Data(call,sizeof(call));code.Ptr(reinterpret_cast<const void*>(&Skip));
    constexpr unsigned char invoke[]={0xFF,0xD0,0x84,0xC0,0x0F,0x84};code.Data(invoke,sizeof(invoke));const auto branch=code.size;code.U32(0);
    Restore(code);Jump(code,image+kNative); // native create/cleanup/post-create/ID lifecycle stays intact
    const auto fallback=code.size;Restore(code);
    constexpr unsigned char test[]={0x48,0x85,0xD2,0x75,0x0E};code.Data(test,sizeof(test));Jump(code,image+kNative);
    // The overwritten MOV/TEST, then the untouched original JE at6CF968.
    code.Data(kOriginal+9,5);Jump(code,image+kSite+sizeof(kOriginal));
    const auto relative=static_cast<std::uint32_t>(fallback-(branch+4));std::memcpy(code.bytes+branch,&relative,4);return code;
}
}
bool DeiroiEntityFactoryAimReady() noexcept {
    using namespace deiroi_entity_aim;
    __try {return image && image==admitted && thunk && installed.size && Readable(thunk,installed.size) &&
        Readable(image+kSite,sizeof(siteBytes)) && !std::memcmp(thunk,installed.bytes,installed.size) &&
        !std::memcmp(image+kSite,siteBytes,sizeof(siteBytes));}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool InstallDeiroiEntityFactoryAim() noexcept {
    using namespace deiroi_entity_aim;
    __try {
        if(thunk)return DeiroiEntityFactoryAimReady(); // never adopt a foreign replacement
        if(!image || (admitted && image!=admitted) || !Readable(image+kSite,sizeof(kOriginal)) ||
            std::memcmp(image+kSite,kOriginal,sizeof(kOriginal)))return false;
        const Code code=Build();void* allocated=AllocateNearCode(image+kSite,code.bytes,code.size);if(!allocated)return false;
        Code patch{};Jump(patch,allocated);
        if(!edf::PatchCode(image+kSite,kOriginal,patch.bytes,sizeof(kOriginal))) {VirtualFree(allocated,0,MEM_RELEASE);return false;}
        admitted=image;thunk=allocated;installed=code;std::memcpy(siteBytes,patch.bytes,sizeof(siteBytes));
        return DeiroiEntityFactoryAimReady();
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
