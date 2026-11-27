// stab.cpp's hooks (the logic is stab.h).
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// stab.cpp: the gun stabilizer (README 炮管稳定器, docs/camera-re.md §7). InstallStabilizer at load (the plain seat aim's
// step; the AddSe one comes through turretcam.cpp's hook, which calls StabStep in place of the next step); StabFrame from
// every vehicle's input (registers its seats' aims); StabHeld: for a controller of `aim`'s gun before this frame's step
// (turretcam.cpp Steer, EDF6AutoTurret through common/edf/aimlink.h V3), the axes the stabilizer holds it at with no
// command (`held`, rad, the aim's senses) and how much of that is the hull's turn since the last step (`hull`): it steers
// from `held` and takes `hull` out of its want's drift and of the axes' motion it learns from (false: not held; `held`
// the axes as they are, `hull` 0); `frame` (9 floats, may be null; written only when held): the world rows (x, up, nose)
// `held` is seen in, the frame its wants must be seen in too (stab.h HeldIn); StabState: 1 the seat's gun is held, 2
// held but the drive is outrun, 0 not held (vhud.cpp).
using AimStepFn=void(__fastcall*)(void*,const float*);
bool InstallStabilizer() noexcept;
void StabFrame(unsigned char* vehicle) noexcept;
void StabStep(void* aim,const float* in,AimStepFn next) noexcept;
bool StabHeld(const void* aim,float* held,float* hull,float* frame) noexcept;
int StabState(unsigned char* vehicle,unsigned seat) noexcept;
// RemapAimAxis: after `aim`'s step this frame, set its axis `axis` (0 yaw, 1 pitch) to `corrected` and map it onto the
// bones with the whole turn from `before` (its angle before the step) as the stabilizer's own correction is (0x5FC280,
// stab.h Remap); a gun the stabilizer holds has its held line moved with it, so the turn is kept, not pulled back
// (stab.h Shift). False when the bone map is not in (AimAxisAdjustable).
bool AimAxisAdjustable() noexcept;
bool RemapAimAxis(unsigned char* aim,int axis,float before,float corrected) noexcept;
void ResetStabilizer() noexcept;
}  // namespace crew
