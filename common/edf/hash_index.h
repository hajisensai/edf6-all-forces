// A key -> int index for the plugins' per-object tables (soldiers, squads, a frame's enemies, shooter-target pairs) that
// must not cap a battle: open addressing with linear probing, no allocation per insert (the cells grow by doubling when
// half full and are kept, Clear() included), erase by backward shift (no tombstones, so a long mission does not slow it).
// The empty key is K{} (a null pointer / a pair of them): the tables never key a null object. Game thread (or under the
// owner's lock), like the tables it indexes. tools/hash_index_check.cpp tests it offline.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace edf {

inline std::uint64_t HashMix(std::uint64_t x) noexcept {   // splitmix64's finalizer: object addresses are 16-aligned
    x^=x>>30;x*=0xBF58476D1CE4E5B9ull;x^=x>>27;x*=0x94D049BB133111EBull;x^=x>>31;
    return x;
}
inline std::uint64_t HashOf(const void* p) noexcept { return HashMix(reinterpret_cast<std::uintptr_t>(p)); }

// Two objects (a shooter and its target).
struct PtrPair {
    const void* a=nullptr;
    const void* b=nullptr;
    bool operator==(const PtrPair& o) const noexcept { return a==o.a && b==o.b; }
};
inline std::uint64_t HashOf(const PtrPair& k) noexcept { return HashMix(HashOf(k.a)^(reinterpret_cast<std::uintptr_t>(k.b)*0x9E3779B97F4A7C15ull)); }

template<class K> class HashIndex {
public:
    // The value stored for `key`, -1 when none.
    int Find(const K& key) const noexcept {
        if(cells.empty() || key==K{})return -1;
        for(std::size_t i=HashOf(key)&mask;;i=(i+1)&mask) {
            if(cells[i].key==key)return cells[i].value;
            if(cells[i].key==K{})return -1;
        }
    }
    // `key` -> `value` (inserted or overwritten).
    void Set(const K& key,int value) {
        if(key==K{})return;
        if((used+1)*2>cells.size())Grow();
        for(std::size_t i=HashOf(key)&mask;;i=(i+1)&mask) {
            if(cells[i].key==key){cells[i].value=value;return;}
            if(cells[i].key==K{}){cells[i]={key,value};++used;return;}
        }
    }
    void Erase(const K& key) noexcept {
        if(cells.empty() || key==K{})return;
        std::size_t i=HashOf(key)&mask;
        for(;;i=(i+1)&mask) {
            if(cells[i].key==K{})return;
            if(cells[i].key==key)break;
        }
        // Backward shift: pull later cells of the run into the hole when their home is not between the hole and them.
        for(std::size_t j=i;;) {
            j=(j+1)&mask;
            if(cells[j].key==K{})break;
            const std::size_t home=HashOf(cells[j].key)&mask;
            if(((j-home)&mask)>=((j-i)&mask)){cells[i]=cells[j];i=j;}
        }
        cells[i]=Cell{};--used;
    }
    // Every key gone, the cells kept (a frame's table: no allocation next frame).
    void Clear() noexcept {
        if(!used)return;
        for(auto& c:cells)c=Cell{};
        used=0;
    }
    std::size_t Size() const noexcept { return used; }
    std::size_t Capacity() const noexcept { return cells.size(); }
    // Room for `n` keys without growing.
    void Reserve(std::size_t n) { while(cells.size()<n*2+2)Grow(); }

private:
    struct Cell { K key{}; int value=0; };
    std::vector<Cell> cells;
    std::size_t used=0,mask=0;
    void Grow() {
        std::vector<Cell> old;old.swap(cells);
        cells.assign(old.empty() ? 64 : old.size()*2,Cell{});
        mask=cells.size()-1;used=0;
        for(const Cell& c:old)if(!(c.key==K{}))Set(c.key,c.value);
    }
};

using PtrIndex=HashIndex<const void*>;
using PairIndex=HashIndex<PtrPair>;

}  // namespace edf
