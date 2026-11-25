// The plugin's online channels counted for the PERF NET CHANNELS line (perf_probe.cpp reports and zeroes them every
// 5 s; net-1 of docs/plan-2026-11-24-optimization.md). Only counts: what is sent or received is not touched. Header
// only, so the offline tests that build plugin_net.cpp / net_object_resolve.cpp need nothing more.
#pragma once
#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace crew::netcount {
struct Row { std::atomic<unsigned long long> key{0};std::atomic<unsigned> msgs{0};std::atomic<unsigned long long> bytes{0}; };
constexpr unsigned kRows=32;
inline Row out[kRows],in[kRows];
inline std::atomic<unsigned> walks{0};
inline std::atomic<long long> walkTicks{0};
// One message under `key` (its channel's head word, sent or received): its row found or taken (open addressing; a key
// keeps its row, the 33rd distinct key is not counted).
inline void Count(Row* rows,std::uint32_t key,std::size_t bytes) noexcept {
    const unsigned long long k=static_cast<unsigned long long>(key)+1;
    for(unsigned i=0;i<kRows;++i) {
        Row& r=rows[(key*2654435761u+i)%kRows];
        unsigned long long was=r.key.load(std::memory_order_relaxed);
        if(was==0 && r.key.compare_exchange_strong(was,k,std::memory_order_relaxed))was=k;
        if(was!=k)continue;
        r.msgs.fetch_add(1,std::memory_order_relaxed);r.bytes.fetch_add(bytes,std::memory_order_relaxed);
        return;
    }
}
inline void Out(std::uint32_t head,std::size_t bytes) noexcept { Count(out,head,bytes); }
inline void In(std::uint32_t head,std::size_t bytes) noexcept { Count(in,head,bytes); }
// One native world walk of the NetId resolver and its QPC ticks.
inline void Walk(LONGLONG ticks) noexcept {
    walks.fetch_add(1,std::memory_order_relaxed);walkTicks.fetch_add(ticks,std::memory_order_relaxed);
}
}  // namespace crew::netcount
