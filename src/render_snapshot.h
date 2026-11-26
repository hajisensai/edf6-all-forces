// A single producer publishes a self-contained value to independent render readers.
// The producer builds the next value in private. Readers can retain their own committed
// copy during publication and never wait for simulation or read producer-owned storage.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace crew {
template<class T>
class RenderSnapshot {
    static_assert(std::is_trivially_copyable_v<T>,"render snapshots are copied by value");
    using Word=std::uint64_t;
    static_assert(std::atomic<Word>::is_always_lock_free,"snapshot reads can run on the render thread");
    static constexpr std::size_t kWords=(sizeof(T)+sizeof(Word)-1)/sizeof(Word);
public:
    RenderSnapshot() noexcept {
        for(auto& word:words_)std::atomic_init(&word,Word{0});
    }

    template<class Write>
    void Publish(Write&& write) noexcept {
        write(staging_);
        std::array<Word,kWords> data{};
        std::memcpy(data.data(),&staging_,sizeof(T));
        revision_.fetch_add(1,std::memory_order_relaxed);
        // If a reader observes any new payload word, its acquire fence also observes
        // this odd revision (or later) when it checks whether the copy was whole.
        std::atomic_thread_fence(std::memory_order_release);
        for(std::size_t i=0;i<kWords;++i)words_[i].store(data[i],std::memory_order_relaxed);
        revision_.fetch_add(1,std::memory_order_release);
    }

    Word Revision() const noexcept {return revision_.load(std::memory_order_acquire);}

    // Leaves both outputs unchanged when no complete publication is available.
    bool TryRead(T& out,Word& revision) const noexcept {
        const Word before=Revision();
        if(!before || (before&1))return false;
        std::array<Word,kWords> data;
        for(std::size_t i=0;i<kWords;++i)data[i]=words_[i].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if(revision_.load(std::memory_order_relaxed)!=before)return false;
        std::memcpy(&out,data.data(),sizeof(T));
        revision=before;
        return true;
    }

private:
    T staging_{};
    std::array<std::atomic<Word>,kWords> words_;
    std::atomic<Word> revision_{0};
};
}  // namespace crew
