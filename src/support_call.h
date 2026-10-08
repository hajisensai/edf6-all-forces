#pragma once
#include <cstddef>
namespace crew {
// Same catalog as native radio weapons. Map dispatch has no network event yet: refuse online rather
// than creating a private, damaging support copy. Native radio calls keep their existing replication.
int SupportCallCount() noexcept;
const wchar_t* SupportCallName(int index) noexcept;
bool SupportCallAt(int index,const float* target,wchar_t* note,std::size_t capacity) noexcept;
}
