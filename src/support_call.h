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
// What the map's support bar shows of the catalog (hud.cpp MapSupportBar): an entry's icon, and whether a call can be
// asked for now (one dispatcher serves every entry: a request still being planned, its cooldown after a delivery).
enum class SupportIcon : std::uint8_t { jet, heli, carrier, gunship, sub, squad, platoon, tank, apc, truck };
SupportIcon SupportCallIcon(int index) noexcept;
enum class SupportReady : std::uint8_t { ready, planning, cooldown, off };
struct SupportReadiness { SupportReady state; int seconds; };   // seconds: the cooldown left
SupportReadiness SupportCallReadiness() noexcept;
}
