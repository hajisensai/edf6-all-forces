// Large per-feature tables grow by one value-initialised row when all existing rows are occupied.
// Native hooks can keep a row's address. Rows stay allocated and callers recycle them in place.
// Existing readers see the published prefix as of begin(), even if a new row is appended mid-loop.
// The caller serializes binding and resetting row contents, as it does for native object state.
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

    std::size_t Size() const noexcept {return count_.load(std::memory_order_acquire);}
    T& operator[](std::size_t i) noexcept {return *rows_[i];}
    const T& operator[](std::size_t i) const noexcept {return *rows_[i];}

    template<class Free>
    T* FindOrAppend(const char* what,Free&& free) noexcept {
        for(T& row:*this)if(free(row))return &row;
        return Append(what);
    }

    // Failure stops further growth and logs once. Existing rows can still be reused.
    __declspec(noinline) T* Append(const char* what) noexcept {
        AcquireSRWLockExclusive(&growth_);
        const auto next=count_.load(std::memory_order_relaxed);
        if(next==N || growthFailed_) {
            ReleaseSRWLockExclusive(&growth_);
            return nullptr;
        }
        T* row=new(std::nothrow) T{};
        if(row) {
            rows_[next]=row;
            count_.store(next+1,std::memory_order_release);
        } else growthFailed_=true;
        ReleaseSRWLockExclusive(&growth_);
        if(!row)Log("MEMORY %s: cannot allocate row %zu (%zu bytes)",what,next,sizeof(T));
        return row;
    }

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
    T* rows_[N]{};
    std::atomic<std::size_t> count_{0};
    SRWLOCK growth_=SRWLOCK_INIT;
    bool growthFailed_=false;
};
}
