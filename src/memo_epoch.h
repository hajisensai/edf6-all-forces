#pragma once
#include "edf/hash_index.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace memoepoch {
// A memo key for per-frame answers: a new epoch each frame, and again each kStallMs a frame does not move (paused,
// loading, or the frame clock not found) so a stalled frame never keeps an answer for good. Not the wall clock itself:
// a slow frame (33-75 ms at stress) crossing a short wall-clock step re-judged every target 2-3 times a frame.
constexpr std::uint64_t kStallMs=250;
struct Clock {
    std::uint64_t frame=~0ull,since=0,epoch=0;
    std::uint64_t Epoch(std::uint64_t now,std::uint64_t wallMs) noexcept {
        if(now!=frame || wallMs-since>=kStallMs){frame=now;since=wallMs;++epoch;}
        return epoch;
    }
    // Epochs only grow: no key from before the reset comes back.
    void Reset() noexcept { frame=~0ull;since=0; }
};

// A per-epoch memo keyed by object (wastedfire.cpp's per-target verdicts): open addressing with linear probing over cells
// stamped with the epoch that wrote them. A cell of another epoch is empty, so each epoch starts empty without a clear
// (the cells are kept), and every key of the epoch keeps its answer for the whole epoch, however many there are.
// Why (bench:4000, 2026-11-28): the memo it replaces was a direct-mapped array of 4096 slots, one key a slot. With
// 4000 targets a quarter to a third of them (more for some allocation strides) lost their slot to another target each
// frame: each ask of such a target re-read it (stepobj::Reads, its kind) and the parallel Decide's peek found it
// missing (-1, "plan"), so a soldier's whole pick went back to the game thread. Grows (doubling: the current epoch's
// entries moved) when the epoch's keys fill half the cells; growth throws std::bad_alloc (the caller then answers
// without keeping). Epoch 0 is no epoch (the cells start in it): Clock's epochs start at 1. One writer (the game
// thread); Find is const and reads only, for readers while no write runs (the parallel Decide).
template<class V> class Table {
public:
    const V* Find(const void* key,std::uint64_t epoch) const noexcept {
        if(!key || !epoch || epoch!=live || cells.empty())return nullptr;
        for(std::size_t i=edf::HashOf(key)&mask;;i=(i+1)&mask) {
            const Cell& c=cells[i];
            if(c.epoch!=epoch)return nullptr;
            if(c.key==key)return &c.value;
        }
    }
    // The value kept for `key` in `epoch` (a new one V{} when it has none): the epoch becomes the live one (an older
    // epoch's keys are gone). `epoch` not 0 and never older than the live one (Clock's epochs only grow).
    V& Make(const void* key,std::uint64_t epoch) {
        if(epoch!=live){live=epoch;count=0;}
        if((count+1)*2>cells.size())Grow(cells.empty() ? 64 : cells.size()*2);
        for(std::size_t i=edf::HashOf(key)&mask;;i=(i+1)&mask) {
            Cell& c=cells[i];
            if(c.epoch!=epoch){c.key=key;c.epoch=epoch;c.value=V{};++count;return c.value;}
            if(c.key==key)return c.value;
        }
    }
    // Room for `n` keys of an epoch without growing.
    void Reserve(std::size_t n) { std::size_t size=cells.empty() ? 64 : cells.size();while(size<n*2+2)size*=2;if(size>cells.size())Grow(size); }
    void Clear() noexcept { for(Cell& c:cells)c=Cell{};live=0;count=0; }
    std::size_t Size() const noexcept { return count; }
    std::size_t Capacity() const noexcept { return cells.size(); }

private:
    struct Cell { const void* key=nullptr; std::uint64_t epoch=0; V value{}; };
    std::vector<Cell> cells;
    std::size_t mask=0,count=0;
    std::uint64_t live=0;
    // The live epoch's cells moved into `size` new ones (built aside: a failed allocation leaves the memo as it was).
    void Grow(std::size_t size) {
        std::vector<Cell> grown(size);
        const std::size_t m=size-1;
        for(const Cell& c:cells) {
            if(!live || c.epoch!=live)continue;
            std::size_t i=edf::HashOf(c.key)&m;
            while(grown[i].epoch==live)i=(i+1)&m;
            grown[i]=c;
        }
        cells.swap(grown);
        mask=m;
    }
};
}
