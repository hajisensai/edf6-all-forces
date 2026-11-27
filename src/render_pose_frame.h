// Ownership of the matrices used by one native render recording.
// Enqueue the open and retire operations in the same FIFO as the draws.
#pragma once
#include "edf/hash_index.h"
#include "frame_rate_logic.h"
#include <algorithm>
#include <cstring>
#include <atomic>
#include <d3d11.h>
#include <xmmintrin.h>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
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
    struct DrawData { const Matrix* pose; const PackedBone* palette; };

    bool Begin() noexcept {
        State free=State::free;
        if(!state_.compare_exchange_strong(free,State::building,std::memory_order_acquire,std::memory_order_relaxed))return false;
        entries_.clear();
        index_.Clear();
        used_=0;
        paletteUsed_=transformsUsed_=0;
        return true;
    }
    void Observe(const Skeleton& key) noexcept {
        if(!key.instance || !key.bones || !key.live || !key.count || key.count>kMaxBones)return;
        const int match=index_.Find(key.instance);
        if(match>=0) {
            auto& entry=entries_[static_cast<std::size_t>(match)];
            if(!(entry.key==key))entry=Entry{key};
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
        if(key.count>kMaxMatrices-used_ || transformsUsed_==kMaxInstances ||
           !transforms_.Grow(transformsUsed_+1,transformsUsed_,kMaxInstances) ||
           !matrices_.Grow(used_+key.count,used_,kMaxMatrices) || !CopyEntry(entry,copy))return false;
        std::memcpy(transforms_.data()[transformsUsed_].elements,transform,sizeof(Matrix));
        entry.transform=transformsUsed_++;
        return true;
    }

    using CapturePalette=ID3D11Buffer*(*)(const Skeleton&,Matrix*) noexcept;
    template<class Copy>
    bool Seal(Copy copy,CapturePalette palette=nullptr) noexcept {
        std::size_t wanted=used_;
        for(const auto& entry:entries_)
            if(entry.offset==kUncopied && entry.key.count<=kMaxMatrices-wanted)wanted+=entry.key.count;
        const bool canCopy=matrices_.Grow(wanted,used_,kMaxMatrices);
        bool captured=false;
        for(auto& entry:entries_) {
            if(canCopy && entry.offset==kUncopied && entry.key.count<=wanted-used_)CopyEntry(entry,copy);
            captured|=entry.offset!=kUncopied;
        }
        if(!captured) {
            Cancel();
            return false;
        }
        if(palette)for(auto& entry:entries_) {
            if(entry.offset==kUncopied || entry.key.count>kMaxPaletteBones-paletteUsed_)continue;
            const auto wantedPalette=paletteUsed_+entry.key.count;
            if(!inverse_.Grow(wantedPalette,paletteUsed_,kMaxPaletteBones) ||
               !packed_.Grow(wantedPalette,0,kMaxPaletteBones))break;
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
    DrawData Read(const Skeleton& key,ID3D11Buffer* buffer=nullptr) noexcept {
        const int at=index_.Find(key.instance);
        if(at<0)return {};
        auto& entry=entries_[static_cast<std::size_t>(at)];
        if(entry.offset==kUncopied || !(entry.key==key))return {};
        auto* pose=matrices_.data()+entry.offset;
        if(entry.transform!=kNoTransform) {
            // Only the queue consumer can touch this slot after Open().
            framerate::MulBones(pose->elements,key.count,transforms_.data()[entry.transform].elements);
            entry.transform=kNoTransform;
        }
        if(!buffer || entry.buffer!=buffer)return {pose,nullptr};
        const auto offset=entry.paletteOffset;
        if(!entry.paletteReady) {
            for(std::size_t i=0;i<key.count;++i)PackBone(inverse_.data()[offset+i],pose[i],packed_.data()[offset+i]);
            entry.paletteReady=true;
        }
        return {pose,packed_.data()+offset};
    }
    const Matrix* Lookup(const Skeleton& key) noexcept { return Read(key).pose; }

private:
    static constexpr std::size_t kMaxPaletteBones=1u<<17;
    enum class State { free,building,queued,reading };
    static constexpr std::size_t kUncopied=kMaxMatrices;
    static constexpr std::size_t kNoTransform=kMaxInstances;
    struct Entry {
        Skeleton key;
        std::size_t offset=kUncopied,paletteOffset=0,transform=kNoTransform;
        ID3D11Buffer* buffer=nullptr;
        bool paletteReady=false;
    };
    // Raw matrix slots are initialized by successful capture or packing. Growing a free slot never clears spare capacity
    // or copies data from its previous recording. Packed output has no initialized prefix until the consumer opens it.
    template<class T> class Arena {
        static_assert(std::is_trivial_v<T>);
    public:
        T* data() noexcept {return storage_.get();}
        bool Grow(std::size_t wanted,std::size_t preserve,std::size_t limit) noexcept {
            if(capacity_>=wanted)return true;
            const auto enlarged=capacity_+capacity_/2+256;
            const auto next=(std::max)(wanted,(std::min)(enlarged,limit));
            std::unique_ptr<T[]> storage(new(std::nothrow) T[next]);
            if(!storage)return false;
            if(preserve)std::memcpy(storage.get(),storage_.get(),preserve*sizeof(T));
            storage_.swap(storage);
            capacity_=next;
            return true;
        }
    private:
        std::unique_ptr<T[]> storage_;
        std::size_t capacity_=0;
    };
    template<class Copy>
    bool CopyEntry(Entry& entry,Copy copy) noexcept {
        if(!copy(entry.key,matrices_.data()+used_))return false;
        entry.offset=used_;
        used_+=entry.key.count;
        return true;
    }

    std::atomic<State> state_{State::free};
    std::size_t used_=0,paletteUsed_=0,transformsUsed_=0;
    edf::PtrIndex index_;
    std::vector<Entry> entries_;
    Arena<Matrix> matrices_,inverse_,transforms_;
    Arena<PackedBone> packed_;
};
}
