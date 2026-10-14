// EDF4.1 script natives that EDF6's BVM executor (EDF5_OLD_SCRIPT, the runtime of the converted EDF4.1 missions) has no
// entry for. pylib/bvm41.py writes their 4.1 calls as the ids below; the executor routed those ids to no handler.
//
// SetEncountReportRadius(object, radius): how far an object reports its encounter. EDF4.1 table 1 id 3000 (EDF41.exe
// 0x14B2E5: pops the radius (float, top) and the object handle (int), writes the float to the object's +0x240, which
// game_object_encount[0] fills at 0x28CF66). EDF5 has no native for it; EDF6 keeps the field (+0x470, filled from
// game_object_encount at 0x546118) and a BVM-style helper 0x228120(bvm-0x30, handle, radius) that resolves the handle
// through 0x20C150 and writes +0x470, but no table entry leads to it (AngelScript has the native,
// "void SetEncountReportRadius(::Object , float )"). DLC mission M508 sets it on its 18 Deiroi.
//
// The executor's native dispatcher 0x20FF70 (rsi = the script stack, r13 = the executor, r14 = this): table 1
// (op 0x2D) jumps on the id's low 3 bits through the RVA table 0x2148BC (0x20FFF5..0x210014, edx = the id, rbx = the
// image), entries 3, 4, 5 and 7 being 0x2100C3, the common end (no handler: hands rsi back as the stack). No EDF5 or
// EDF6 script and no converted EDF4.1 script calls a table-1 id of those low bits (tools/check_bvm41.py, plugin id check).
// Entry 3 is pointed at a stub that takes id kSetEncountId only (any other id goes on to 0x2100C3 as before): it pops
// the two arguments as table 0 3020's handler does (0x213D8B: sub rsi,0x10; lea rcx,[r14-0x30]; movss xmm2,[rsi+0x10];
// mov edx,[rsi+8]; call 0x2288C0 -- 0x2288C0 and 0x228120 share prologue and handle lookup), calls 0x228120 and
// continues at 0x2100C3: the stack falls by 2 slots, as 4.1's native's. The stub's page is above the image, inside the
// table entry's 32-bit reach (the entry is an unsigned offset from the image base, 0x21000A..0x210011).
// EDF.dll TimeDateStamp 0x678CCB46; every byte the patch relies on is checked first.
#include "crew.h"
#include "memory.h"

#include <cstdint>
#include <cstring>

namespace crew {
namespace {
constexpr std::uint32_t kTimeDateStamp=0x678CCB46;
constexpr std::uint32_t kSetEncountId=3003;            // table 1; bvm41.py SET_ENCOUNT_REPORT_RADIUS
constexpr std::size_t kDispatchEntry=0x20FFBB,kTable1Switch=0x20FFF5,kTable1Jumps=0x2148BC,kCommonEnd=0x2100C3;
constexpr std::size_t kSlot=3;
constexpr std::size_t kSetEncount=0x228120,kSetLifeHandler=0x213D8B;

// mov rsi,r8; mov r13,rdx; mov r14,rcx; mov rdi,r8; test r9d,r9d (the registers the stub relies on)
const unsigned char kDispatchEntryCode[]={0x49,0x8B,0xF0,0x4C,0x8B,0xEA,0x4C,0x8B,0xF1,0x49,0x8B,0xF8,0x45,0x85,0xC9};
// mov edx,[rbp+0x240]; mov eax,edx; and eax,7; movsxd rcx,eax; lea rbx,[image]; mov eax,[rbx+rcx*4+0x2148BC];
// add rax,rbx; jmp rax
const unsigned char kTable1SwitchCode[]={0x8B,0x95,0x40,0x02,0x00,0x00,0x8B,0xC2,0x83,0xE0,0x07,0x48,0x63,0xC8,0x48,
    0x8D,0x1D,0xF6,0xFF,0xDE,0xFF,0x8B,0x84,0x8B,0xBC,0x48,0x21,0x00,0x48,0x03,0xC3,0xFF,0xE0};
// mov rdx,rsi; mov rcx,r13; call 0x2276E0 (the stack handed back)
const unsigned char kCommonEndCode[]={0x48,0x8B,0xD6,0x49,0x8B,0xCD,0xE8,0x12,0x76,0x01,0x00};
// 0x228120 whole: (rcx, edx handle, xmm2 radius) -> 0x20C150(rcx, edx); if found, [obj+0x470] = radius
const unsigned char kSetEncountCode[]={0x48,0x83,0xEC,0x38,0x0F,0x29,0x74,0x24,0x20,0x0F,0x28,0xF2,0xE8,0x1F,0x40,
    0xFE,0xFF,0x48,0x85,0xC0,0x74,0x08,0xF3,0x0F,0x11,0xB0,0x70,0x04,0x00,0x00,0x0F,0x28,0x74,0x24,0x20,0x48,0x83,
    0xC4,0x38,0xC3};
// table 0 3020's handler, the argument pops the stub copies
const unsigned char kSetLifeHandlerCode[]={0x48,0x83,0xEE,0x10,0x49,0x8D,0x4E,0xD0,0xF3,0x0F,0x10,0x56,0x10,0x8B,0x56,
    0x08};

std::uint32_t TimeDateStamp() noexcept {
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
    return nt->FileHeader.TimeDateStamp;
}
std::size_t SizeOfImage() noexcept {
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    return reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew)->OptionalHeader.SizeOfImage;
}

// `code` in a fresh executable page above the image, its offset from the image base below 2 GB (a table entry is an
// unsigned 32-bit offset).
unsigned char* AllocateAboveImage(const unsigned char* code,std::size_t size) noexcept {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const std::uintptr_t grain=info.dwAllocationGranularity ? info.dwAllocationGranularity : 0x10000;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    const std::uintptr_t first=(base+SizeOfImage()+grain-1)&~(grain-1);
    for(std::uintptr_t at=first;at+0x1000-base<0x7FFF0000;at+=grain) {
        auto* page=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(at),0x1000,MEM_RESERVE|MEM_COMMIT,
                                                            PAGE_READWRITE));
        if(!page)continue;
        if(reinterpret_cast<std::uintptr_t>(page)!=at) {
            VirtualFree(page,0,MEM_RELEASE);
            continue;
        }
        std::memcpy(page,code,size);
        DWORD old=0;
        if(!VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&old)) {
            VirtualFree(page,0,MEM_RELEASE);
            return nullptr;
        }
        FlushInstructionCache(GetCurrentProcess(),page,size);
        return page;
    }
    return nullptr;
}

std::size_t BuildStub(unsigned char* out) noexcept {
    const auto setEncount=reinterpret_cast<std::uint64_t>(image+kSetEncount);
    const auto commonEnd=reinterpret_cast<std::uint64_t>(image+kCommonEnd);
    std::size_t n=0;
    out[n++]=0x81; out[n++]=0xFA;                                     // cmp edx,kSetEncountId
    std::memcpy(out+n,&kSetEncountId,4); n+=4;
    out[n++]=0x75; out[n++]=0x1C;                                     // jne other (28 bytes on)
    std::memcpy(out+n,kSetLifeHandlerCode,sizeof(kSetLifeHandlerCode)); // the two pops, rcx = this-0x30
    n+=sizeof(kSetLifeHandlerCode);
    out[n++]=0x48; out[n++]=0xB8;                                     // mov rax,0x228120; call rax
    std::memcpy(out+n,&setEncount,8); n+=8;
    out[n++]=0xFF; out[n++]=0xD0;
    out[n++]=0x48; out[n++]=0xB8;                                     // other: mov rax,0x2100C3; jmp rax
    std::memcpy(out+n,&commonEnd,8); n+=8;
    out[n++]=0xFF; out[n++]=0xE0;
    return n;
}
}  // namespace

bool InstallEdf41Natives() noexcept {
    static bool tried=false,ready=false;
    if(tried)return ready;
    tried=true;
    std::uint32_t entry=0;
    std::memcpy(&entry,image+kTable1Jumps+4*kSlot,4);
    if(TimeDateStamp()!=kTimeDateStamp || !Matches(kDispatchEntry,kDispatchEntryCode,sizeof(kDispatchEntryCode)) ||
       !Matches(kTable1Switch,kTable1SwitchCode,sizeof(kTable1SwitchCode)) ||
       !Matches(kCommonEnd,kCommonEndCode,sizeof(kCommonEndCode)) ||
       !Matches(kSetEncount,kSetEncountCode,sizeof(kSetEncountCode)) ||
       !Matches(kSetLifeHandler,kSetLifeHandlerCode,sizeof(kSetLifeHandlerCode)) || entry!=kCommonEnd) {
        Log("EDF41NATIVES off: EDF.dll is not the known build (stamp %08X, table 1 entry %u = %08X)",TimeDateStamp(),
            static_cast<unsigned>(kSlot),entry);
        return false;
    }
    unsigned char code[64];
    const std::size_t size=BuildStub(code);
    unsigned char* stub=AllocateAboveImage(code,size);
    if(!stub) {
        Log("EDF41NATIVES off: no page above the image for the stub");
        return false;
    }
    const auto offset=static_cast<std::uint32_t>(stub-image);
    unsigned char* slot=image+kTable1Jumps+4*kSlot;
    DWORD old=0;
    if(!VirtualProtect(slot,4,PAGE_EXECUTE_READWRITE,&old)) {
        VirtualFree(stub,0,MEM_RELEASE);
        Log("EDF41NATIVES off: the jump table is not writable");
        return false;
    }
    std::memcpy(slot,&offset,4);
    VirtualProtect(slot,4,old,&old);
    FlushInstructionCache(GetCurrentProcess(),slot,4);
    ready=true;
    Log("EDF41NATIVES table 1 id %u = SetEncountReportRadius (EDF+%zX), stub at EDF+%X",
        static_cast<unsigned>(kSetEncountId),kSetEncount,static_cast<unsigned>(offset));
    return true;
}
}  // namespace crew
