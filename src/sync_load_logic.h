// The sync load probe's per-call bookkeeping on the game thread (src/sync_load_probe.cpp; tests/sync_load_logic_test.cpp).
// Free of Windows and of the game: the hooks hand it the frame (the address of their return-address slot, one value for
// every call site of one 0x84E80 invocation) and QPC ticks.
#pragma once

namespace crew::syncload {
struct Call {
    const void* frame;
    const void* key;
    long long begin;   // at the cache lock (0x84EBF): the lock's wait counts
    long long wait;    // the lock and the first lookup
    bool miss,missing;
};

template<unsigned N>
struct CallStack {
    Call calls[N]{};
    unsigned depth=0;

    // A new invocation at `frame`. The stack grows down, so the invocations still running (its callers) have frames above
    // it; an entry at or below it is one an exception unwound (its exit never ran): dropped, else the stack never empties
    // again and nothing more is logged. Null past N (that invocation is not timed).
    Call* Begin(const void* frame,long long begin) noexcept {
        while(depth && calls[depth-1].frame<=frame)--depth;
        if(depth>=N)return nullptr;
        Call& c=calls[depth++];
        c=Call{frame,nullptr,begin,0,false,false};
        return &c;
    }
    // The innermost invocation, when it is the one at `frame`.
    Call* Mine(const void* frame) noexcept { return depth && calls[depth-1].frame==frame ? &calls[depth-1] : nullptr; }
    // The invocation at `frame` returns: true with it in `out` when it was recorded. Entries below it are unwound ones.
    bool End(const void* frame,Call& out) noexcept {
        while(depth && calls[depth-1].frame<frame)--depth;
        if(!depth || calls[depth-1].frame!=frame)return false;
        out=calls[--depth];
        return true;
    }
};

// The STATS line's totals; Since gives the part after an earlier snapshot.
struct Totals {
    unsigned long long calls,loads,waits,missing,dropped;
    double loadMs,waitMs;
    Totals Since(const Totals& was) const noexcept {
        return Totals{calls-was.calls,loads-was.loads,waits-was.waits,missing-was.missing,dropped-was.dropped,
                      loadMs-was.loadMs,waitMs-was.waitMs};
    }
};
}  // namespace crew::syncload
