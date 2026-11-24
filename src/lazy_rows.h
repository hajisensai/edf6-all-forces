// Large per-feature row tables that live on the heap from their first use instead of in the
// DLL image (mem-4). A writable image section is committed in full when the DLL is mapped, so
// the enemy-control and support streams cost every player ~100 MB of commit even when nobody
// ever controls an enemy. LazyRows<T,N> is constant-initialised (all zero: it lands in .bss and
// needs no dynamic initialiser) and reads as empty until Allocate() runs: iteration yields no
// rows, which is exactly what N default rows that nobody holds looked like to every caller.
// Allocate() makes all N rows at once, value-initialised like the former `T rows[N]{}`, and the
// lazyBlock is never freed, so pointers into it stay valid for the rest of the process.
// If the allocation fails the table stays empty for good: the caller's feature refuses to bind
// (one log line), never a half-initialised table.
#pragma once
#include "crew.h"
#include <atomic>
#include <cstddef>
#include <new>

namespace crew {
template<class T,std::size_t N>
class LazyRows {
public:
    constexpr LazyRows() noexcept=default;
    LazyRows(const LazyRows&)=delete;
    LazyRows& operator=(const LazyRows&)=delete;

    // Makes the rows if they do not exist yet; false (logged once) when the heap refuses them.
    // The fast path stays inline; the allocation itself is out of line so callers inside __try
    // blocks carry no new[] unwinding.
    bool Allocate(const char* what) noexcept {return rows.load(std::memory_order_acquire) || Make(what);}
    bool Allocated() const noexcept {return rows.load(std::memory_order_acquire)!=nullptr;}
    static constexpr std::size_t size() noexcept {return N;}

    T* begin() noexcept {return rows.load(std::memory_order_acquire);}
    T* end() noexcept {T* r=begin();return r ? r+N : nullptr;}
    const T* begin() const noexcept {return rows.load(std::memory_order_acquire);}
    const T* end() const noexcept {const T* r=begin();return r ? r+N : nullptr;}
    // Only valid once Allocate() has succeeded.
    T& operator[](std::size_t i) noexcept {return begin()[i];}
    const T& operator[](std::size_t i) const noexcept {return begin()[i];}

private:
    __declspec(noinline) bool Make(const char* lazyFeature) noexcept {
        if(failed.load(std::memory_order_relaxed))return false;
        T* lazyBlock=new(std::nothrow) T[N]();
        if(!lazyBlock) {
            if(!failed.exchange(true))Log("MEMORY %s: cannot allocate %zu bytes; this feature stays off",lazyFeature,sizeof(T)*N);
            return false;
        }
        T* lazyNone=nullptr;
        if(!rows.compare_exchange_strong(lazyNone,lazyBlock,std::memory_order_acq_rel))delete[] lazyBlock;   // another thread made them first
        return true;
    }
    std::atomic<T*> rows{nullptr};
    std::atomic<bool> failed{false};
};
}
