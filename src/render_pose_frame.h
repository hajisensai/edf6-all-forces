// Ownership of the matrices used by one native render recording.
// Enqueue the open and retire operations in the same FIFO as the draws.
#pragma once
#include "edf/hash_index.h"
#include <atomic>
#include <cstdint>
#include <new>
#include <vector>

namespace crew::renderpose {
struct alignas(16) Matrix { float elements[16]; };
struct Skeleton {
    const void* instance;
    const void* bones;
    const Matrix* live;
    std::size_t count;
    bool operator==(const Skeleton& o) const noexcept {
        return instance==o.instance && bones==o.bones && live==o.live && count==o.count;
    }
};

class Frame {
public:
    static constexpr std::size_t kMaxBones=1024;   // native palette scratch holds 1024 3x4 matrices
    static constexpr std::size_t kMaxMatrices=1u<<19,kMaxInstances=1u<<15;

    bool Begin() noexcept {
        State free=State::free;
        if(!state_.compare_exchange_strong(free,State::building,std::memory_order_acquire,std::memory_order_relaxed))return false;
        entries_.clear();
        index_.Clear();
        return true;
    }
    void Observe(const Skeleton& key) noexcept {
        if(!key.instance || !key.bones || !key.live || !key.count || key.count>kMaxBones)return;
        const int match=index_.Find(key.instance);
        if(match>=0) {
            entries_[static_cast<std::size_t>(match)].key=key;
            return;
        }
        if(entries_.size()>=kMaxInstances)return;
        try {
            entries_.push_back({key,kUncopied});
            try {
                index_.Set(key.instance,static_cast<int>(entries_.size()-1));
            } catch(const std::bad_alloc&) {
                entries_.pop_back();
            }
        } catch(const std::bad_alloc&) {}
    }

    template<class Copy>
    bool Seal(Copy copy) noexcept {
        std::size_t wanted=0;
        for(const auto& entry:entries_)
            if(entry.key.count<=kMaxMatrices-wanted)wanted+=entry.key.count;
        try {
            if(matrices_.size()<wanted)matrices_.resize(wanted);
        } catch(const std::bad_alloc&) {
            Cancel();
            return false;
        }
        std::size_t used=0;
        for(auto& entry:entries_) {
            if(entry.key.count>wanted-used)continue;
            if(!copy(entry.key,matrices_.data()+used))continue;
            entry.offset=used;
            used+=entry.key.count;
        }
        if(!used) {
            Cancel();
            return false;
        }
        state_.store(State::queued,std::memory_order_release);
        return true;
    }

    void Cancel() noexcept {state_.store(State::free,std::memory_order_release);}
    bool Open() noexcept {
        State queued=State::queued;
        return state_.compare_exchange_strong(queued,State::reading,std::memory_order_acquire,std::memory_order_relaxed);
    }
    void Retire() noexcept {state_.store(State::free,std::memory_order_release);}
    // A native callback on the consumer itself can rebuild a pose for this draw.
    // Its explicit render-side result then supersedes the recording's copy.
    void UseLive(const void* instance) noexcept {
        const int at=index_.Find(instance);
        if(at>=0)entries_[static_cast<std::size_t>(at)].offset=kUncopied;
    }
    const Matrix* Lookup(const Skeleton& key) const noexcept {
        const int at=index_.Find(key.instance);
        if(at<0)return nullptr;
        const auto& entry=entries_[static_cast<std::size_t>(at)];
        if(entry.offset==kUncopied || !(entry.key==key))return nullptr;
        return matrices_.data()+entry.offset;
    }

private:
    enum class State { free,building,queued,reading };
    static constexpr std::size_t kUncopied=kMaxMatrices;
    struct Entry { Skeleton key; std::size_t offset; };
    std::atomic<State> state_{State::free};
    edf::PtrIndex index_;
    std::vector<Entry> entries_;
    std::vector<Matrix> matrices_;
};
}
