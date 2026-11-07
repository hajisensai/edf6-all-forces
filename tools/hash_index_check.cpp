// The tables' key index (common/edf/hash_index.h) offline, against std::unordered_map as the reference: random inserts,
// overwrites and erases (backward shift), growth past many doublings, Clear keeping the cells.
//   cmake --build build --target hash_index_check && build\hash_index_check.exe      (exit code 1 on a failure)
#include "edf/hash_index.h"
#include <cstdio>
#include <random>
#include <unordered_map>
#include <vector>

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

    std::printf("hash_index_check: %d/%d passed\n",cases-failures,cases);
    return failures ? 1 : 0;
}
