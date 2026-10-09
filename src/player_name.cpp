// A player's name from the game's own name tag source (EDF.dll TimeDateStamp 0x678CCB46; the user, 2026-10-10: the Q mark
// names its marker by "真名"). tests/player_name_native_audit.py pins every fact used here:
//  - The multiplayer name tag's draw 0x804C90 takes each player's name from 0x7FFBD0, which reads the soldier's user
//    (shared_ptr: object +0x1ED0, control block +0x1ED8), takes a strong reference to it (lock inc [ctrl+8]) and calls
//    0x784830(&name, &user, false), name an empty std::wstring (size 0, capacity 7).
//  - 0x784830 assigns the name (the user's nickname at user +0x78, 0x12ABF50, or its platform name, 0x12ABFF0, as the
//    session says) and on every path releases the reference it was handed (returns 0 no user, 1, 2).
//  - 0x3D3C0 frees a std::wstring's heap buffer (capacity 8 and up) through the game's own allocator and leaves it empty.
// So this is the name tag's exact call, made with this plugin's own std::wstring-shaped buffer and reference.
#include "crew.h"
#include "player_name.h"
#include "memory.h"
#include <cstdint>
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kNameOf=0x784830,kTidy=0x3D3C0;
constexpr std::size_t kUser=0x1ED0,kUserCtrl=0x1ED8,kCtrlStrong=8;
const unsigned char kNameOfCode[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x50,0x48};
const unsigned char kTidyCode[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x51,0x18,0x48,0x8B,0xD9,0x48,0x83,0xFA};
// MSVC x64 std::wstring: the characters in place up to 7 (else a heap pointer), the size, the capacity.
struct WString { union { wchar_t buf[8]; wchar_t* ptr; }; std::size_t size,cap; };
static_assert(sizeof(WString)==32,"std::wstring is 32 bytes");
struct UserRef { void* user; void* ctrl; };
using NameOfFn=std::int32_t(__fastcall*)(WString*,UserRef*,bool);
using TidyFn=void(__fastcall*)(WString*);
}  // namespace

bool ReadPlayerName(const void* soldier,wchar_t* out,std::size_t count) noexcept {
    if(!out || !count)return false;
    out[0]=0;
    if(!image || !Matches(kNameOf,kNameOfCode,sizeof(kNameOfCode)) || !Matches(kTidy,kTidyCode,sizeof(kTidyCode)))return false;
    WString name{};name.cap=7;
    bool ok=false;
    __try {
        const auto* p=static_cast<const unsigned char*>(soldier);
        if(!Readable(p,kUserCtrl+8))return false;
        UserRef ref{At<void*>(p,kUser),At<void*>(p,kUserCtrl)};
        if(!ref.user || !ref.ctrl || !Readable(ref.ctrl,0x10) || At<long>(ref.ctrl,kCtrlStrong)<=0)return false;
        // The reference handed over: the callee releases it (as the name tag's own copy).
        InterlockedIncrement(reinterpret_cast<volatile long*>(static_cast<unsigned char*>(ref.ctrl)+kCtrlStrong));
        reinterpret_cast<NameOfFn>(image+kNameOf)(&name,&ref,false);
        const wchar_t* text=name.cap>=8 ? name.ptr : name.buf;
        if(name.size && name.size<4096 && Readable(text,(name.size+1)*sizeof(wchar_t))) {
            FitName(text,name.size,out,count);
            ok=out[0]!=0;
        }
        reinterpret_cast<TidyFn>(image+kTidy)(&name);
    } __except(EXCEPTION_EXECUTE_HANDLER){out[0]=0;return false;}
    return ok;
}
}  // namespace crew
