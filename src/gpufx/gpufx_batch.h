// GPU particle effects (src/gpufx): the taken-over stock draws of one view coalesced into as few instanced draws as
// the stock order allows. Header-only and free of Direct3D (tests/gpufx_sim_test.cpp).
//
// The stock issues one draw per emitter (ParticleRender) and one per efs quad (Texture<quad>::Draw, n = 1 each), in
// its own order. Ours collects them in that order (one record per stock draw: an instance range plus its key) and
// draws them at the world's end. Two records may share one draw only where that cannot change the picture:
//  - consecutive records of one key, always (the stock order kept);
//  - records of one key inside a run of order-free records (additive colour: dst + src, no depth write, so any order
//    sums to the same colour), even with other keys between them. A record that is not order-free (alpha blend,
//    subtract) closes the run: nothing moves across it.
// The groups of a run keep the order of their first records; the records of a group keep the stock order.
#pragma once
#include <cstddef>
#include <vector>

namespace crew::gpufx {

// While the records are collected: how many draws they will coalesce into (the renderer's batch cap is checked
// against this, not against the record count).
template<class Record>
struct Coalescing {
    std::vector<std::size_t> runHeads;   // the first record of each group in the open run
    int draws=0;
    void Reset() noexcept { runHeads.clear();draws=0; }
    // The record about to be collected (not yet appended): whether it opens a new draw (cap it then; false: it
    // joins the last record, consecutive and of its key, or a group of the open run). Added: the bookkeeping once a
    // new record is appended (a record merged into the last one is not added).
    template<class Same,class OrderFree>
    bool OpensDraw(const std::vector<Record>& records,const Record& r,Same same,OrderFree orderFree) const noexcept {
        if(!records.empty() && same(records.back(),r))return false;
        if(!orderFree(r))return true;
        for(const std::size_t h:runHeads)if(same(records[h],r))return false;
        return true;
    }
    template<class Same,class OrderFree>
    void Added(const std::vector<Record>& records,Same same,OrderFree orderFree) {
        const std::size_t at=records.size()-1;
        const Record& r=records[at];
        if(!orderFree(r)){runHeads.clear();++draws;return;}
        for(const std::size_t h:runHeads)if(same(records[h],r))return;
        runHeads.push_back(at);++draws;
    }
};

// At the world's end: `records` (start / count into `instances`, in the stock order) into `out` (one per draw) with
// their instances regrouped into `outInstances`.
template<class Record,class Instance,class Same,class OrderFree>
void Coalesce(const std::vector<Record>& records,const std::vector<Instance>& instances,std::vector<Record>& out,
              std::vector<Instance>& outInstances,Same same,OrderFree orderFree) {
    out.clear();outInstances.clear();
    outInstances.reserve(instances.size());
    std::vector<std::size_t> groups;   // per record of the run: its group's first record
    std::vector<std::size_t> heads;
    const std::size_t n=records.size();
    std::size_t i=0;
    auto append=[&](const Record& r) {
        outInstances.insert(outInstances.end(),instances.begin()+r.start,instances.begin()+r.start+r.count);
    };
    while(i<n) {
        if(!orderFree(records[i])) {
            Record r=records[i];
            r.start=static_cast<decltype(r.start)>(outInstances.size());
            append(records[i]);out.push_back(r);
            ++i;continue;
        }
        std::size_t end=i;
        while(end<n && orderFree(records[end]))++end;
        heads.clear();groups.assign(end-i,0);
        for(std::size_t k=i;k<end;++k) {
            std::size_t g=heads.size();
            for(std::size_t h=0;h<heads.size();++h)if(same(records[heads[h]],records[k])){g=h;break;}
            if(g==heads.size())heads.push_back(k);
            groups[k-i]=g;
        }
        for(std::size_t g=0;g<heads.size();++g) {
            Record r=records[heads[g]];
            r.start=static_cast<decltype(r.start)>(outInstances.size());r.count=0;
            for(std::size_t k=i;k<end;++k)if(groups[k-i]==g){append(records[k]);r.count+=records[k].count;}
            out.push_back(r);
        }
        i=end;
    }
}

}  // namespace crew::gpufx
