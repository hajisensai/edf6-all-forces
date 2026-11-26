#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace edf::detail {
// Game page addresses have twelve zero low bits. Keep the two access bits and a checked tick tag there so that a
// hit reads one machine word. Clear before the tag is reused, including cached denials.
class PageAccessCache {
public:
    static constexpr std::uintptr_t kPage=0x1000;
    static constexpr std::size_t kSlots=8192;
    static constexpr int kMissing=-1;

    void Begin(std::uint64_t tick) noexcept {
        if(tick==tick_)return;
        tick_=tick;
        stamp_=(stamp_+kStampStep)&(kPage-1);
        if(!stamp_) {
            std::memset(values_,0,sizeof(values_));
            stamp_=kStampStep;
        }
    }

    int Access(std::uintptr_t page) const noexcept {
        const auto packed=values_[Slot(page)];
        return (packed&~kAccessMask)==(page|stamp_) ? static_cast<int>(packed&kAccessMask) : kMissing;
    }

    void Put(std::uintptr_t page,unsigned char access) noexcept {
        values_[Slot(page)]=page|stamp_|access;
    }

private:
    static constexpr std::uintptr_t kAccessMask=3,kStampStep=kAccessMask+1;
    static std::size_t Slot(std::uintptr_t page) noexcept {
        const auto number=page/kPage;
        // Fold in the next bits to avoid constant eviction for equally aligned heap arenas 32 MiB apart.
        return (number^(number/kSlots))&(kSlots-1);
    }

    std::uint64_t tick_=0;
    std::uintptr_t stamp_=kStampStep;
    std::uintptr_t values_[kSlots]{};
};
}
