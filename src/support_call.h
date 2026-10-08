#pragma once
#include <cstddef>
#include <cstdint>
namespace crew {
inline constexpr std::uint32_t kSupportRangerResource=1,kSupportLeaderResource=2,
    kSupportAircraftResource=0x10000,kSupportVehicleResource=0x20000;
// Same catalog as native radio weapons. Map dispatch has no network event yet: refuse online rather
// than creating a private, damaging support copy. Native radio calls keep their existing replication.
int SupportCallCount() noexcept;
const wchar_t* SupportCallName(int index) noexcept;
bool SupportCallAt(int index,const float* target,wchar_t* note,std::size_t capacity) noexcept;
void SupportDispatchTick() noexcept;
void ResetSupportDispatch() noexcept;
void SupportCallStatus(wchar_t* out,std::size_t capacity) noexcept;
}
