// booster.cpp: the jets' afterburner flames, smoke and flares' flames.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
void ResetBoosters() noexcept;    // booster.cpp
bool InstallBoosters() noexcept;                   // booster.cpp: the carrier's nozzle flames (stock Booster)
// booster.cpp: an aircraft's exhaust flames on its model's nozzles (exhaust_nozzles.h: a jet's exits, the carrier's four
// pods), burning `intensity` (0..1), `burner` longer.
void JetFlames(const unsigned char* v,float intensity,bool burner,ULONGLONG ms) noexcept;
// booster.cpp: flames on nozzles placed in the world (+z out of each, rows unit), `size[i]` (length, width m) and `level[i]` (0 out .. 1) each; at most 10.
void NozzleFlames(const unsigned char* v,const float (*m)[16],int n,const float (*size)[2],const float* level,ULONGLONG ms) noexcept;
void JetSmoke(const unsigned char* v,bool on,ULONGLONG ms) noexcept;   // booster.cpp: an arriving aircraft's smoke trails (its flames' nozzles)
// booster.cpp: the flares' fire, drawn as Booster flames on `v` (their owner) at `at`, trailing against `vel`.
void FlareFlames(const unsigned char* v,const float (*at)[3],const float (*vel)[3],int n,ULONGLONG ms) noexcept;
}  // namespace crew
