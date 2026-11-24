// The tables' key index (common/edf/hash_index.h) offline, against std::unordered_map as the reference: random inserts,
// overwrites and erases (backward shift), growth past many doublings, Clear keeping the cells, a growth whose allocation
// fails leaving the index as it was.
//   cmake --build build --target hash_index_check && build\hash_index_check.exe      (exit code 1 on a failure)
#include "edf/hash_index.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <unordered_map>
#include <vector>

// Allocations failed on demand (the growth's std::bad_alloc).
static bool failAllocations=false;
void* operator new(std::size_t n) {
    if(failAllocations)throw std::bad_alloc();
    if(void* p=std::malloc(n ? n : 1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,long long a=0,long long b=0) {
    ++cases;
    if(!ok){++failures;std::printf("FAIL: %s (%lld, %lld)\n",what,a,b);}
}
}  // namespace

int main() {
    // Object-like keys: 16-aligned addresses in one block (what the game's objects look like to the index).
    constexpr int kKeys=20000;
    std::vector<unsigned char> block(static_cast<std::size_t>(kKeys)*16+16);
    const auto key=[&](int i){ return static_cast<const void*>(block.data()+static_cast<std::size_t>(i)*16); };

    edf::PtrIndex index;
    std::unordered_map<const void*,int> ref;
    std::mt19937 rng(7);
    int mismatches=0;
    for(int step=0;step<400000;++step) {
        const int i=static_cast<int>(rng()%kKeys);
        const unsigned op=rng()%10;
        if(op<5){index.Set(key(i),step);ref[key(i)]=step;}
        else if(op<8){index.Erase(key(i));ref.erase(key(i));}
        else {
            const auto it=ref.find(key(i));
            mismatches+=index.Find(key(i))!=(it==ref.end() ? -1 : it->second);
        }
    }
    for(int i=0;i<kKeys;++i){const auto it=ref.find(key(i));mismatches+=index.Find(key(i))!=(it==ref.end() ? -1 : it->second);}
    Check(mismatches==0,"random set / erase / find agree with unordered_map",mismatches);
    Check(index.Size()==ref.size(),"size",static_cast<long long>(index.Size()),static_cast<long long>(ref.size()));
    Check(index.Size()*2<=index.Capacity(),"at most half full",static_cast<long long>(index.Size()),static_cast<long long>(index.Capacity()));

    const std::size_t cap=index.Capacity();
    index.Clear();
    bool empty=index.Size()==0;
    for(int i=0;i<kKeys;++i)empty=empty && index.Find(key(i))==-1;
    Check(empty && index.Capacity()==cap,"Clear: no key, the cells kept");
    for(int i=0;i<kKeys/4;++i)index.Set(key(i),i);
    Check(index.Capacity()==cap,"refilled after Clear without growing",static_cast<long long>(index.Capacity()),static_cast<long long>(cap));
    Check(index.Find(nullptr)==-1,"the null key is never stored");

    // Pairs: (shooter, target), the same shooter with many targets and the same target with many shooters.
    edf::PairIndex pairs;
    int n=0;
    for(int s=0;s<200;++s)for(int t=0;t<50;++t)pairs.Set(edf::PtrPair{key(s),key(1000+t)},n++);
    bool pairOk=pairs.Size()==10000;
    n=0;
    for(int s=0;s<200;++s)for(int t=0;t<50;++t)pairOk=pairOk && pairs.Find(edf::PtrPair{key(s),key(1000+t)})==n++;
    pairOk=pairOk && pairs.Find(edf::PtrPair{key(1000),key(0)})==-1;   // the pair the other way round is another
    Check(pairOk,"10000 shooter-target pairs, each found as itself");

    // A growth whose allocation fails (Set at the half-full mark, Reserve): std::bad_alloc out, the index as it was.
    {
        edf::PtrIndex grown;
        for(int i=0;i<32;++i)grown.Set(key(i),i);   // 64 cells half full: the 33rd key grows them
        const std::size_t grownCap=grown.Capacity();
        bool threw=false;
        failAllocations=true;
        try { grown.Set(key(32),32); } catch(const std::bad_alloc&) { threw=true; }
        failAllocations=false;
        bool kept=threw && grown.Size()==32 && grown.Capacity()==grownCap && grown.Find(key(32))==-1;
        for(int i=0;i<32;++i)kept=kept && grown.Find(key(i))==i;
        Check(kept,"Set: a failed growth throws and leaves every key found",static_cast<long long>(grown.Size()),
              static_cast<long long>(grown.Capacity()));
        threw=false;
        failAllocations=true;
        try { grown.Reserve(1000); } catch(const std::bad_alloc&) { threw=true; }
        failAllocations=false;
        kept=threw && grown.Size()==32 && grown.Capacity()==grownCap;
        for(int i=0;i<32;++i)kept=kept && grown.Find(key(i))==i;
        Check(kept,"Reserve: a failed growth throws and leaves every key found");
        grown.Set(key(32),32);
        grown.Erase(key(3));
        kept=grown.Size()==32 && grown.Capacity()==grownCap*2 && grown.Find(key(32))==32 && grown.Find(key(3))==-1;
        for(int i=0;i<32;++i)kept=kept && (i==3 || grown.Find(key(i))==i);
        Check(kept,"after the failed growths the next one grows and the index keeps working");
    }

    std::printf("hash_index_check: %d/%d passed\n",cases-failures,cases);
    return failures ? 1 : 0;
}
