// map_signal.h: the M map's signals (F1-F4, the Broken Arrow pings; command protocol v11 Kind::ping) kept for the map
// and for the view (the user, 2026-11-27: 「m里面的新号在世界里面应该也能看见」). One list for the whole machine: this
// machine's own (mapcmd.cpp, a key on the map) and the room's (command_net.cpp, as each one is received, whether the map
// is open or not), each shown kShowMs from when it was made or heard, fading out over its last kFadeMs.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crew {
namespace mapsignal {
constexpr int kKept=8;                       // the oldest gives way to a ninth
constexpr unsigned kKinds=4;                 // F1-F4 (command_protocol.h kPingKinds)
constexpr std::uint64_t kShowMs=6000;        // how long a signal shows, on the map and in the view
constexpr std::uint64_t kFadeMs=2000;        // its last part fading out
struct Signal { float at[3]; unsigned kind; std::uint64_t at_ms; bool mine; };

// The signals in the order they came, oldest first.
struct List {
    Signal s[kKept]{};
    int n=0;
    // False (not kept): no point (not finite) or no kind of F1-F4.
    bool Add(const float* at,unsigned kind,bool mine,std::uint64_t now) noexcept {
        if(!at || kind>=kKinds || !std::isfinite(at[0]) || !std::isfinite(at[1]) || !std::isfinite(at[2]))return false;
        if(n==kKept){std::memmove(s,s+1,sizeof(s[0])*(kKept-1));--n;}
        Signal& x=s[n++];std::memcpy(x.at,at,12);x.kind=kind;x.at_ms=now;x.mine=mine;
        return true;
    }
    // The ones still shown at `now` into `out` (at most `max`), oldest first; the ones past their time dropped.
    int Live(std::uint64_t now,Signal* out,int max) noexcept {
        int kept=0;
        for(int i=0;i<n;++i)if(now-s[i].at_ms<kShowMs)s[kept++]=s[i];
        n=kept;
        int shown=0;
        for(int i=0;i<n && shown<max;++i)out[shown++]=s[i];
        return shown;
    }
};

// How much of it shows `age` ms after it was made: whole until its last kFadeMs, then down to nothing at kShowMs.
inline float Alpha(std::uint64_t age) noexcept {
    if(age>=kShowMs)return 0.0f;
    const std::uint64_t left=kShowMs-age;
    return left>=kFadeMs ? 1.0f : static_cast<float>(left)/static_cast<float>(kFadeMs);
}
}  // namespace mapsignal

// The machine's list (map_signal.cpp). Add: game thread (the map's key, the room's as received); Views: any thread (the
// HUD's draw, the map's readout); Reset: a new mission (mapcmd.cpp ResetMapCommands).
bool AddMapSignal(const float* at,unsigned kind,bool mine) noexcept;
int MapSignalViews(mapsignal::Signal* out,int max) noexcept;
void ResetMapSignals() noexcept;
}  // namespace crew
