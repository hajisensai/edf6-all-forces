// Ownership of the matrices used by one native render recording.
// Enqueue the open and retire operations in the same FIFO as the draws.
#pragma once
#include "edf/hash_index.h"
#include "frame_rate_logic.h"
#include <cstring>
#include <atomic>
#include <d3d11.h>
#include <xmmintrin.h>
#include <cstdint>
#include <new>
#include <vector>

namespace crew::renderpose {
struct alignas(16) Matrix { float elements[16]; };
struct alignas(16) PackedBone {float columns[12];};

// EDF.dll 0x1100A01..0x1100B41 multiplies inverse bind by world pose and uploads the
// first three columns. Preserve the native pairwise summation and output layout.
inline void PackBone(const Matrix& bind,const Matrix& world,PackedBone& out) noexcept {
    const __m128 p0=_mm_load_ps(world.elements),p1=_mm_load_ps(world.elements+4);
    const __m128 p2=_mm_load_ps(world.elements+8),p3=_mm_load_ps(world.elements+12);
    __m128 rows[4];
    for(int i=0;i<4;++i) {
        const float* const r=bind.elements+i*4;
        const auto a=_mm_add_ps(_mm_mul_ps(_mm_set1_ps(r[1]),p1),_mm_mul_ps(_mm_set1_ps(r[0]),p0));
        const auto b=_mm_add_ps(_mm_mul_ps(_mm_set1_ps(r[3]),p3),_mm_mul_ps(_mm_set1_ps(r[2]),p2));
        rows[i]=i==0 ? _mm_add_ps(a,b) : _mm_add_ps(b,a);
    }
    _MM_TRANSPOSE4_PS(rows[0],rows[1],rows[2],rows[3]);
    for(int i=0;i<3;++i)_mm_store_ps(out.columns+i*4,rows[i]);
}
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
        used_=0;
        paletteUsed_=0;
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
            entries_.push_back({key});
            try {
                index_.Set(key.instance,static_cast<int>(entries_.size()-1));
            } catch(const std::bad_alloc&) {
                entries_.pop_back();
            }
        } catch(const std::bad_alloc&) {}
    }

    template<class Copy>
    bool Stage(const Skeleton& key,const float* transform,Copy copy) noexcept {
        const int at=index_.Find(key.instance);
        if(at<0)return false;
        auto& entry=entries_[static_cast<std::size_t>(at)];
        if(!(entry.key==key))return false;
        if(entry.offset!=kUncopied)return true;  // the first owner (player or carrier) wins
        if(key.count>kMaxMatrices-used_ || !Grow(matrices_,used_+key.count) || !CopyEntry(entry,copy))return false;
        std::memcpy(entry.transform,transform,sizeof(entry.transform));
        entry.pendingTransform=true;
        return true;
    }

    using CapturePalette=ID3D11Buffer*(*)(const Skeleton&,Matrix*) noexcept;
    template<class Copy>
    bool Seal(Copy copy,CapturePalette palette=nullptr) noexcept {
        std::size_t wanted=used_;
        for(const auto& entry:entries_)
            if(entry.offset==kUncopied && entry.key.count<=kMaxMatrices-wanted)wanted+=entry.key.count;
        if(Grow(matrices_,wanted))
            for(auto& entry:entries_)
                if(entry.offset==kUncopied && entry.key.count<=wanted-used_)CopyEntry(entry,copy);
        if(!used_) {
            Cancel();
            return false;
        }
        if(palette)for(auto& entry:entries_) {
            if(entry.offset==kUncopied || entry.key.count>kMaxPaletteBones-paletteUsed_)continue;
            const auto wantedPalette=paletteUsed_+entry.key.count;
            if(!Grow(inverse_,wantedPalette,kMaxPaletteBones) || !Grow(packed_,wantedPalette,kMaxPaletteBones))break;
            entry.buffer=palette(entry.key,inverse_.data()+paletteUsed_);
            if(!entry.buffer)continue;
            entry.paletteOffset=paletteUsed_;
            paletteUsed_=wantedPalette;
        }
        state_.store(State::queued,std::memory_order_release);
        return true;
    }

    void Cancel() noexcept {state_.store(State::free,std::memory_order_release);}
    bool Open() noexcept {
        State queued=State::queued;
        return state_.compare_exchange_strong(queued,State::reading,std::memory_order_acquire,std::memory_order_relaxed);
    }
    void Retire() noexcept {
        for(auto& entry:entries_)if(entry.buffer)entry.buffer->Release();
        state_.store(State::free,std::memory_order_release);
    }
    // A native callback on the consumer itself can rebuild a pose for this draw.
    // Its explicit render-side result then supersedes the recording's copy.
    void UseLive(const void* instance) noexcept {
        const int at=index_.Find(instance);
        if(at>=0)entries_[static_cast<std::size_t>(at)].offset=kUncopied;
    }
    const Matrix* Lookup(const Skeleton& key) noexcept {
        const int at=index_.Find(key.instance);
        if(at<0)return nullptr;
        auto& entry=entries_[static_cast<std::size_t>(at)];
        if(entry.offset==kUncopied || !(entry.key==key))return nullptr;
        auto* pose=matrices_.data()+entry.offset;
        if(entry.pendingTransform) {
            // Only the queue consumer can touch this slot after Open().
            framerate::MulBones(pose->elements,key.count,entry.transform);
            entry.pendingTransform=false;
        }
        return pose;
    }

    const PackedBone* Palette(const Skeleton& key,const Matrix* pose,ID3D11Buffer* buffer) noexcept {
        if(!buffer || !pose)return nullptr;
        const int at=index_.Find(key.instance);
        if(at<0)return nullptr;
        auto& entry=entries_[static_cast<std::size_t>(at)];
        if(entry.offset==kUncopied || entry.buffer!=buffer || !(entry.key==key) ||
           pose!=matrices_.data()+entry.offset || entry.pendingTransform)return nullptr;
        const auto offset=entry.paletteOffset;
        if(!entry.paletteReady) {
            for(std::size_t i=0;i<key.count;++i)PackBone(inverse_[offset+i],pose[i],packed_[offset+i]);
            entry.paletteReady=true;
        }
        return packed_.data()+offset;
    }

private:
    static constexpr std::size_t kMaxPaletteBones=1u<<17;
    enum class State { free,building,queued,reading };
    static constexpr std::size_t kUncopied=kMaxMatrices;
    struct Entry {
        Skeleton key;
        std::size_t offset=kUncopied,paletteOffset=0;
        ID3D11Buffer* buffer=nullptr;
        bool pendingTransform=false,paletteReady=false;
        float transform[16]{};
    };
    template<class T>
    bool Grow(std::vector<T>& storage,std::size_t wanted,std::size_t limit=kMaxMatrices) noexcept {
        if(storage.size()>=wanted)return true;
        const auto enlarged=storage.size()+storage.size()/2+256;
        const auto next=enlarged<limit ? enlarged : limit;
        try {
            storage.resize(wanted>next ? wanted : next);
            return true;
        } catch(const std::bad_alloc&) {return false;}
    }
    template<class Copy>
    bool CopyEntry(Entry& entry,Copy copy) noexcept {
        if(!copy(entry.key,matrices_.data()+used_))return false;
        entry.offset=used_;
        used_+=entry.key.count;
        return true;
    }

    std::atomic<State> state_{State::free};
    std::size_t used_=0,paletteUsed_=0;
    edf::PtrIndex index_;
    std::vector<Entry> entries_;
    std::vector<Matrix> matrices_,inverse_;
    std::vector<PackedBone> packed_;
};
}
