// The game's std::wstring (MSVC x64, EDF.dll TimeDateStamp 0x678CCB46): a 0x20-byte object, the characters in place
// (+0, 8 wchar_t with the NUL) while the capacity +0x18 is 7, else a pointer at +0; the size at +0x10. Built, given text
// and destroyed by the game's own functions (its allocator owns the buffer): std::wstring::assign(ptr, len) 0x3D450 and
// the destructor 0x3D3C0 (both checked by tools/room_dialog_sigs.py and tools/outfit41_sigs.py before anything calls
// them). The layout half is pure (tests/game_dialog_test.cpp runs it offline); the calls take the module's base.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace edf::wstr {
inline constexpr std::size_t kBytes=0x20,kSize=0x10,kCapacity=0x18,kInPlace=7;
inline constexpr std::size_t kAssignRva=0x3D450,kDestroyRva=0x3D3C0;

// An empty string as the game's constructor leaves it (size 0, capacity 7: in place).
inline void Init(unsigned char* s) noexcept {
    std::memset(s,0,kBytes);
    const std::uint64_t cap=kInPlace;
    std::memcpy(s+kCapacity,&cap,sizeof(cap));
}
inline std::uint64_t Size(const unsigned char* s) noexcept { std::uint64_t v;std::memcpy(&v,s+kSize,sizeof(v));return v; }
inline std::uint64_t Capacity(const unsigned char* s) noexcept { std::uint64_t v;std::memcpy(&v,s+kCapacity,sizeof(v));return v; }
// Its characters (in place or on the heap).
inline const wchar_t* Data(const unsigned char* s) noexcept {
    if(Capacity(s)<=kInPlace)return reinterpret_cast<const wchar_t*>(s);
    const wchar_t* p;std::memcpy(&p,s,sizeof(p));
    return p;
}

// The game's assign / destructor (base: EDF.dll's image).
using AssignFn=void*(__fastcall*)(void*,const wchar_t*,std::size_t);
using DestroyFn=void(__fastcall*)(void*);
inline void Assign(unsigned char* base,unsigned char* s,const wchar_t* text) noexcept {
    reinterpret_cast<AssignFn>(base+kAssignRva)(s,text ? text : L"",text ? std::wcslen(text) : 0);
}
inline void Destroy(unsigned char* base,unsigned char* s) noexcept { reinterpret_cast<DestroyFn>(base+kDestroyRva)(s); }
}  // namespace edf::wstr
