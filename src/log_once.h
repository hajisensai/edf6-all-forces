// A diagnostic logged once per distinct answer (2026-10-12, the testers' logs: heli.cpp LogSea compared each answer
// only with the one before, so a helicopter over a shore, its probe flipping water/land/water, wrote a line per flip:
// 26 363 of the 31 723 lines of report #11, pushing the rest out of the 12 MB the installer sends back).
//
// SeenOnce keeps the keys it was shown; Fresh is true the first time a key is seen and false after. When the table is
// full the first refused key says so once (Full), and the rest are only counted (Dropped): the line that would have been
// lost is never written silently. Pure (no game, no allocation): tests/log_once_test.cpp checks it offline.
#pragma once
#include <cstddef>
#include <cstdint>

namespace crew {

template<std::size_t N>
class SeenOnce {
public:
    enum class Verdict : std::uint8_t { seen, fresh, full };
    // fresh: log it; full: the table just filled, log that further answers go unlogged; seen: say nothing.
    Verdict Check(std::uint64_t key) noexcept {
        for(std::size_t i=0;i<count_;++i)if(keys_[i]==key)return Verdict::seen;
        if(count_<N){keys_[count_++]=key;return Verdict::fresh;}
        return dropped_++==0 ? Verdict::full : Verdict::seen;
    }
    void Clear() noexcept { count_=0;dropped_=0; }
    std::size_t Count() const noexcept { return count_; }
    std::size_t Dropped() const noexcept { return dropped_; }

private:
    std::uint64_t keys_[N]{};
    std::size_t count_=0,dropped_=0;
};

}  // namespace crew
