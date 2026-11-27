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
#include "plugin_log.h"
#include <Windows.h>
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

    // A loop reads the rows pointer once. begin() loads it and carries the end of that same load; end() is only the
    // marker the loop compares with. Loading it again in end() could straddle another thread's first Allocate() and
    // give begin = nullptr, end = rows + N: a loop from null through N rows. A cursor converts to the row pointer
    // (`stream - streams.begin()`, a null check); Last() is its end, null whenever the cursor is.
    struct End {};
    template<class P>
    class Cursor {
    public:
        constexpr Cursor(P at,P last) noexcept:at_(at),last_(last) {}
        operator P() const noexcept {return at_;}
        P Last() const noexcept {return last_;}
        decltype(auto) operator*() const noexcept {return *at_;}
        P operator->() const noexcept {return at_;}
        Cursor& operator++() noexcept {++at_;return *this;}
        bool operator!=(End) const noexcept {return at_!=last_;}
        bool operator==(End) const noexcept {return at_==last_;}
    private:
        P at_;
        P last_;
    };
    Cursor<T*> begin() noexcept {T* r=rows.load(std::memory_order_acquire);return {r,r ? r+N : nullptr};}
    Cursor<const T*> begin() const noexcept {const T* r=rows.load(std::memory_order_acquire);return {r,r ? r+N : nullptr};}
    End end() const noexcept {return {};}
    // Only valid once Allocate() has succeeded.
    T& operator[](std::size_t i) noexcept {return rows.load(std::memory_order_acquire)[i];}
    const T& operator[](std::size_t i) const noexcept {return rows.load(std::memory_order_acquire)[i];}

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

// Stable rows grown on demand when contiguous storage is unnecessary.
template<class T,std::size_t N>
class IncrementalRows {
public:
    constexpr IncrementalRows() noexcept=default;
    IncrementalRows(const IncrementalRows&)=delete;
    IncrementalRows& operator=(const IncrementalRows&)=delete;

    std::size_t Size() const noexcept {return count_.load(std::memory_order_acquire);}
    T& operator[](std::size_t i) noexcept {return *rows_[i];}
    const T& operator[](std::size_t i) const noexcept {return *rows_[i];}

    template<class Free>
    T* FindOrAppend(const char* what,Free&& free) noexcept {
        for(T& row:*this)if(free(row))return &row;
        return Append(what);
    }

    T* Append(const char* what) noexcept {return Grow(N,what);}

    struct End {};
    template<class Ref>
    struct Cursor {
        T* const* at;
        T* const* last;
        Ref operator*() const noexcept {return **at;}
        Cursor& operator++() noexcept {++at;return *this;}
        bool operator!=(End) const noexcept {return at!=last;}
    };
    Cursor<T&> begin() noexcept {return {rows_,rows_+Size()};}
    Cursor<const T&> begin() const noexcept {return {rows_,rows_+Size()};}
    End end() const noexcept {return {};}

private:
    // N requests the next slot. Growth failure logs once, with any initialized rows still available.
    __declspec(noinline) T* Grow(std::size_t index,const char* what) noexcept {
        AcquireSRWLockExclusive(&growth_);
        auto next=count_.load(std::memory_order_relaxed);
        if(index==N)index=next;
        std::size_t failed=N;
        if(index<N && !growthFailed_) {
            for(;next<=index;++next) {
                T* const row=new(std::nothrow) T{};
                if(!row) {
                    growthFailed_=true;
                    failed=next;
                    break;
                }
                rows_[next]=row;
            }
            count_.store(next,std::memory_order_release);
        }
        T* const result=index<next ? rows_[index] : nullptr;
        ReleaseSRWLockExclusive(&growth_);
        if(failed!=N)Log("MEMORY %s: cannot allocate row %zu (%zu bytes)",what,failed,sizeof(T));
        return result;
    }

    T* rows_[N]{};
    std::atomic<std::size_t> count_{0};
    SRWLOCK growth_=SRWLOCK_INIT;
    bool growthFailed_=false;
};
}
