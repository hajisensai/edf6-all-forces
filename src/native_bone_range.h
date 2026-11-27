#pragma once
#include "edf/layout.h"

namespace crew::native_bones {
constexpr std::ptrdiff_t kStride=0x110;
constexpr unsigned kOwnedLocally=8,kParent=0x10,kLocal=0x70,kWorld=0xB0;

// Compiled in its own AVX translation unit; call only after checking CPU and OS support.
void ApplyAvx(unsigned char* records,int begin,int end) noexcept;
}
