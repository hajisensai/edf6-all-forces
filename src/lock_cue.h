// The cockpit's lock tone, as a few cues rather than a tone for as long as the lock lasts (the user, 2026-10-31:
// 「载具飞弹锁定的声音有点吵耳朵 改成刚开始响几次然后就停下来了比较好」; ini LockToneBeeps / LockToneLockedMs). Locking
// beeps kBeepMs every period(progress) ms (quickening as the lock closes in) for `maxBeeps` beeps, then is silent; locked,
// the long tone sounds `lockedMs` ms, then is silent, and a lock that holds on one target stays silent. The count starts
// over when the weapon or the target changes, or the state falls back (0, or locked -> locking). maxBeeps = 0: as it was,
// the beeps and the tone for as long as the lock lasts.
// Pure (tests/lock_cue_test.cpp); jetaudio.cpp LockTone plays what Step says.
#pragma once

namespace lock_cue {

constexpr unsigned long long kBeepMs=70,kBeepSlow=450,kBeepFast=120;   // a beep's length; the gap from beep to beep: slow at the start of the lock, fast at its end
constexpr float kLockedRatio=1.6f;                                      // the locked tone's pitch over the beeps'
constexpr int kBeepsCounted=1000000;                                    // the count stops here (maxBeeps = 0 never silences)

struct LockCue {
    const void* weapon=nullptr;
    const void* target=nullptr;
    int state=0;                         // 0 none, 1 locking, 2 locked
    int beeps=0;                         // the beeps begun on this lock
    unsigned long long lastBeep=0,lockedAt=0;
};

struct ToneOut {
    bool on=false;          // sounding now
    float ratio=1.0f;       // its pitch over the base tone
};

// The gap before the next beep of a lock `progress` (0..1) of the way.
inline unsigned long long Period(float progress) noexcept {
    const float p=progress<0.0f ? 0.0f : progress>1.0f ? 1.0f : progress;
    return kBeepSlow-static_cast<unsigned long long>(p*static_cast<float>(kBeepSlow-kBeepFast));
}

// One frame: `weapon` / `target` the lock's (null when unknown), `state` 2 locked, 1 locking, 0 none, `progress` of the
// lock time, `nowMs` any monotonic clock.
inline ToneOut Step(LockCue& c,const void* weapon,const void* target,int state,float progress,unsigned long long nowMs,
                    int maxBeeps,unsigned long long lockedMs) noexcept {
    if(state<=0 || weapon!=c.weapon || target!=c.target || state<c.state)c=LockCue{};
    if(state<=0)return ToneOut{};
    if(c.state==0){c.weapon=weapon;c.target=target;}
    if(state>=2) {
        if(c.state!=2){c.state=2;c.lockedAt=nowMs;}
        return ToneOut{maxBeeps<=0 || nowMs-c.lockedAt<lockedMs,kLockedRatio};
    }
    if(c.state==0){c.state=1;c.beeps=1;c.lastBeep=nowMs;}
    else if((maxBeeps<=0 ? c.beeps<kBeepsCounted : c.beeps<maxBeeps) && nowMs>=c.lastBeep+Period(progress)){
        c.lastBeep=nowMs;++c.beeps;
    }
    return ToneOut{nowMs-c.lastBeep<kBeepMs,1.0f};
}

}  // namespace lock_cue
