// Large per-feature tables grow by one value-initialised row when all existing rows are occupied.
// Native hooks can keep a row's address. Rows stay allocated and callers recycle them in place.
// Existing readers see the published prefix as of begin(), even if a new row is appended mid-loop.
// The caller serializes binding and resetting row contents, as it does for native object state.
#pragma once
#include "plugin_log.h"
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

    std::size_t Size() const noexcept {return count_.load(std::memory_order_acquire);}
    T& operator[](std::size_t i) noexcept {return *rows_[i];}
    const T& operator[](std::size_t i) const noexcept {return *rows_[i];}

    T* Get(std::size_t index) noexcept {return index<Size() ? rows_[index] : nullptr;}

    // For rows paired with another fixed table by index. Publish only the prefix needed by that slot.
    T* Ensure(std::size_t index,const char* what) noexcept {
        if(T* row=Get(index))return row;
        return index<N ? Grow(index,what) : nullptr;
    }

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
