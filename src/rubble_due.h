// The rubble scan's throttle (physics.cpp HeavyChassis -> MarkRubble, which walks every Havok body of the world): each
// chassis at most once per kRubbleScanMs. Pure logic, no game state; tools/rubble_due_check.cpp tests it offline.
//
// Until 2026-11-07 an 8-slot LRU: a stress mission's 10+ vehicles evicted each other every call, so every chassis step
// scanned the world. Now one slot per chassis in an open-addressed table; a slot whose last scan is kRubbleScanMs old
// says nothing (that chassis is due anyway), so another chassis may take it over and the table never needs clearing.
#pragma once
#include <cstdint>

namespace rubble {
constexpr std::uint64_t kRubbleScanMs=250;
constexpr int kSlots=1024;   // chassis stepped within kRubbleScanMs that each keep their own throttle (a power of 2)

struct Slot { const void* wrapper; std::uint64_t at; };
struct Table { Slot slot[kSlots]; };

inline unsigned Home(const void* wrapper) noexcept {
    const auto p=reinterpret_cast<std::uintptr_t>(wrapper);
    return static_cast<unsigned>((static_cast<std::uint64_t>(p>>4)*0x9E3779B97F4A7C15ull)>>32)&(kSlots-1);
}

// Whether `wrapper`'s chassis scans the rubble at `now` (ms); a yes is remembered as its last scan.
inline bool Due(Table& t,const void* wrapper,std::uint64_t now) noexcept {
    const unsigned home=Home(wrapper);
    Slot* take=nullptr;
    for(unsigned k=0;k<kSlots;++k) {
        Slot& s=t.slot[(home+k)&(kSlots-1)];
        if(s.wrapper==wrapper) {
            if(now-s.at<kRubbleScanMs)return false;
            s.at=now;
            return true;
        }
        // Slots are taken over, never emptied: an empty one ends the probe, the wrapper is not further on.
        if(!s.wrapper){if(!take)take=&s;break;}
        if(!take && now-s.at>=kRubbleScanMs)take=&s;
    }
    // Every slot held by a chassis scanned within kRubbleScanMs (more than kSlots of them): its home slot.
    if(!take)take=&t.slot[home];
    *take=Slot{wrapper,now};
    return true;
}
}  // namespace rubble
