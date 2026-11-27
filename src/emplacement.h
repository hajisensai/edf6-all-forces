// emplacement.cpp: the fixed emplacements.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// The fixed emplacements (emplacement.cpp): one (a V603 with the marker bone), and its driver's drive stick cleared
// (crew.cpp InputHook, just before the stock input).
bool IsEmplacement(const unsigned char* vehicle) noexcept;
void EmplacementInput(unsigned char* vehicle) noexcept;
}  // namespace crew
