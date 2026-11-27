// missile.cpp: the guided missiles, their homing and the flares.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
void ResetMissiles() noexcept;    // missile.cpp
// The plugin's missiles guided by proportional navigation with a proximity fuse (missile.cpp).
bool InstallMissiles() noexcept;
// missile.cpp: a guided round now homing on a point within `radius` m of `at` (its lock point there)
bool MissileHoming(const float* at,float radius) noexcept;
// missile.cpp: MissileHoming's rounds: where they are, at most `most`; how many (all of them, past `most` too)
int MissilesHomingAt(const float* at,float radius,float (*pos)[3],int most) noexcept;
// missile.cpp: flares. A flare dropped by `owner` at `at` (m/s `vel`): the rounds homing there may take it for their
// target. FlaresStep moves them once a frame; FlaresOf: `owner`'s burning flares, at most `most` (their places, speeds).
// `nose`: the jet's nose (the aspect); `pairStart`: the first flare of a drop (each drop is judged once a round).
void FlareDrop(const void* owner,const float* at,const float* vel,const float* nose,bool pairStart) noexcept;
void FlaresStep() noexcept;
// missile.cpp: round `b` (`age` frames into its flight, at `pos` flying along `dir`) tracked for the missile warnings and
// the flares, homing on the lock held at `lockAt` (entry, control block): the point it flies at (its lock point, or the
// flare that fooled it) into `aim` and that point's velocity (m a frame; zero at first and after a jump) into `tv`.
// False: no live lock (it flies on).
bool TrackRound(const unsigned char* b,std::int32_t age,const float* pos,const float* dir,const unsigned char* lockAt,
                float* aim,float* tv) noexcept;
int FlaresOf(const void* owner,float (*at)[3],float (*vel)[3],int most) noexcept;
}  // namespace crew
