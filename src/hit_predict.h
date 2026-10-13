// Whether a shot fired now would hurt its target (the user, 2026-10-12: 「这个不止金色飞船啊，还有类似传送塔、新型传送飞船，有可能都打不
// 到。加个预测之类的？」; docs/feedback-2026-10-12-ai-targeting.md). Pure logic, no game state: hitpredict.cpp asks the game
// what it knows (the target's own hit-acceptance callback on a predicted hit, its window's state and time left) and this
// decides; tools/hit_predict_check.cpp tests it offline.
//
//  - Intercept: where to aim so a round of `speed` (m/s; 0: hitscan) dropping `gravity` (m/s^2) meets a target moving at
//    `vel`, and its time of flight.
//  - Window: a target whose damage window opens and shuts (a teleportation ship's hatch, an anchor while its ship shields
//    it, the invincible flag a script sets). Its openings and how long each stayed open are watched; with the time left
//    readable (a ship's close timer) that is used, else the time it has stayed open so far against how long it stayed
//    open before.
//  - Decide: fire (open, the predicted hit accepted, the window still open when the round gets there), wait (shut now but
//    due to open within kPreAimMs: keep it as the target, aim, hold the trigger), or skip.
#pragma once
#include <cmath>
#include <cstdint>

namespace crew {
namespace hitpredict {
constexpr std::uint64_t kPreAimMs=2500;   // a window due to open within this: the target kept and aimed at, no shot
constexpr float kEase=0.5f;               // a new observed duration's weight in its running mean
constexpr float kOpenSlack=0.85f;         // fire only while the round lands within this share of the open time expected

// The aim point and time of flight (s) for a round from `from` at `speed` with `gravity` at a target at `at` moving
// `vel`: two passes of lead (the flight time to the led point), the drop added back. speed<=0: the target point, t 0.
inline float Intercept(const float* from,const float* at,const float* vel,float speed,float gravity,float* aim) noexcept {
    for(int i=0;i<3;++i)aim[i]=at[i];
    if(!(speed>0.0f))return 0.0f;
    float t=0.0f;
    for(int pass=0;pass<3;++pass) {
        const float d[3]={aim[0]-from[0],aim[1]-from[1],aim[2]-from[2]};
        t=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2])/speed;
        for(int i=0;i<3;++i)aim[i]=at[i]+(vel ? vel[i]*t : 0.0f);
        aim[1]+=0.5f*(gravity>0.0f ? gravity : 0.0f)*t*t;
    }
    return t;
}

// One target's damage window as watched.
struct Window {
    bool seen,open;
    int opens;                    // openings watched
    std::uint64_t openedAt,shutAt;
    float openMs,periodMs;        // running means: how long it stays open, from one opening to the next (0: not known)
};
// The window open or not at `ms`.
inline void Observe(Window& w,bool open,std::uint64_t ms) noexcept {
    if(!w.seen){w.seen=true;w.open=open;w.openedAt=open ? ms : 0;w.shutAt=open ? 0 : ms;return;}
    if(open==w.open)return;
    if(open) {
        if(w.opens>0 && w.openedAt) {
            const float p=static_cast<float>(ms-w.openedAt);
            w.periodMs=w.periodMs>0.0f ? w.periodMs+(p-w.periodMs)*kEase : p;
        }
        ++w.opens;w.openedAt=ms;
    } else {
        if(w.openedAt && w.opens>0) {
            const float o=static_cast<float>(ms-w.openedAt);
            w.openMs=w.openMs>0.0f ? w.openMs+(o-w.openMs)*kEase : o;
        }
        w.shutAt=ms;
    }
    w.open=open;
}

enum class Verdict : std::uint8_t { fire, wait, skip };

// What `shooter` does with a target whose window is `w` (nullptr: it has none, always open) at `ms`: a round landing
// `flightMs` from now; the predicted hit `accepted` (the target's own callback says it counts) or not; `leftMs` the time
// its window stays open, read from the game (<0: not readable).
inline Verdict Decide(const Window* w,std::uint64_t ms,float flightMs,bool accepted,float leftMs) noexcept {
    const bool open=!w || !w->seen || w->open;
    if(open) {
        if(!accepted)return Verdict::skip;
        if(leftMs>=0.0f)return flightMs<leftMs ? Verdict::fire : Verdict::skip;
        if(w && w->openMs>0.0f && w->openedAt) {
            const float sofar=static_cast<float>(ms-w->openedAt);
            if(sofar+flightMs>w->openMs*kOpenSlack)return Verdict::skip;   // shut before it gets there, as it went before
        }
        return Verdict::fire;
    }
    // Shut: due to open soon by its rhythm (an opening a period after the last one)?
    if(w->opens>0 && w->periodMs>0.0f) {
        const float next=static_cast<float>(w->openedAt)+w->periodMs;
        const float in=next-static_cast<float>(ms);
        // Due within kPreAimMs, or overdue by less than a period (an opening a little late); past that its rhythm is gone.
        if(in<=static_cast<float>(kPreAimMs) && in>-w->periodMs)return Verdict::wait;
    }
    return Verdict::skip;
}

}  // namespace hitpredict
}  // namespace crew
