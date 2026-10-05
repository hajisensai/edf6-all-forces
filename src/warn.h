// warn.cpp: the cockpit's warnings of the aircraft the player flies (a plugin jet, a rotor craft, a stock helicopter),
// decided once a frame in one place for the eyes (hud.cpp: the annunciator, the ground cue, the RWR scope) and the ears
// (jetaudio.cpp: the callouts, the stall horn, the launch warble). Game thread, except WarnLatest (any thread).
#pragma once
#include "crew.h"

namespace crew {
// The warnings, in the annunciator's order (most urgent first). Warnings (red) are the ones that kill now: PULL UP, STALL,
// a missile coming, the gear not down that low and slow; the rest are cautions (amber).
enum Warn : int { kWarnPullUp, kWarnMissile, kWarnStall, kWarnGear, kWarnTerrain, kWarnSinkRate, kWarnLock, kWarnGearSpeed,
                  kWarnWow, kWarnCount };
constexpr bool IsWarning(int w) noexcept { return w==kWarnPullUp || w==kWarnMissile || w==kWarnStall || w==kWarnGear; }
struct Warnings {
    unsigned on;                  // 1 << Warn for each lit
    ULONGLONG litAt[kWarnCount];  // GetTickCount64 each last came on (the annunciator flashes a new one)
    ULONGLONG launchAt;           // ...a missile not coming for it a moment before began to (0: none yet): a launch
    ULONGLONG tick;               // published
};
constexpr ULONGLONG kLaunchMs=2500;   // a launch's flash and warble
// The ground-proximity windows (a real GPWS's look-ahead): caution within kTerrainSeconds, PULL UP within kPullUpSeconds.
constexpr float kPullUpSeconds=3.0f,kTerrainSeconds=6.0f;
// A helicopter's SINK RATE near the ground (its landing's): down faster than kSinkWarn m/s under kSinkLow m.
constexpr float kSinkWarn=4.0f,kSinkLow=10.0f;
// Seconds until an aircraft at `pos` moving `vel` (m/s, climbing `climb` m/s), `clear` m over the ground (kNoGround:
// none under it), hits the ground or what stands on it, looking `within` s ahead; -1 none. Sinking no faster than
// `gentle` m/s (a landing) onto the ground under it is not an impact; running into something kRise m higher than that
// ground is, whatever the sink (`rising` then: TERRAIN, else SINK RATE). One map ray.
float ClosureIn(const float* pos,const float* vel,float climb,float clear,float gentle,float within,bool* rising) noexcept;
Gpws GpwsOf(float impactIn,bool rising) noexcept;
void WarnTick() noexcept;                 // once a game frame (crew.cpp FrameTick): decided, published and sounded
bool WarnLatest(Warnings* out) noexcept;  // the last published (hud.cpp HudPublish); false with no aircraft flown
}  // namespace crew
