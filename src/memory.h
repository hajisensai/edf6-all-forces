#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstddef>
namespace crew {
bool Readable(const void*,std::size_t,bool writable=false) noexcept;
// Copies `size` bytes of code into a fresh executable page within rel32 reach of `anchor`.
void* AllocateNearCode(const void*,const unsigned char*,std::size_t) noexcept;
void* AllocateNearThunk(const void*,void*) noexcept;
bool RedirectCall(unsigned char*,void*,void*,bool&) noexcept;
}
