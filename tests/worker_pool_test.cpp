// src/worker_pool.cpp: every index runs exactly once for every thread count, faults stay in their call and come
// back, the workers compute with the caller's MXCSR, and the pool survives a shutdown.
#include "worker_pool.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <immintrin.h>
#include <vector>

namespace {
int failures=0;
void Expect(bool ok,const char* what) {
    if(!ok){std::printf("FAIL %s\n",what);++failures;}
}

struct Count { std::vector<std::atomic<int>>* hits; std::atomic<DWORD>* other; DWORD caller; };
void Hit(void* ctx,int i) noexcept {
    auto* c=static_cast<Count*>(ctx);
    (*c->hits)[static_cast<std::size_t>(i)].fetch_add(1);
    if(GetCurrentThreadId()!=c->caller)c->other->store(GetCurrentThreadId());
    volatile double x=0;   // a little work so the workers get a share
    for(int k=0;k<2000;++k)x=x+k*0.5;
}

void Faulty(void* ctx,int i) noexcept {
    auto* hits=static_cast<std::vector<std::atomic<int>>*>(ctx);
    if(i==7) {
        volatile int* null=nullptr;
        *null=1;   // an access violation in one call
    }
    (*hits)[static_cast<std::size_t>(i)].fetch_add(1);
}

struct Bits { std::uint32_t out[64]; float a[64]; };
void Divide(void* ctx,int i) noexcept {
    auto* b=static_cast<Bits*>(ctx);
    volatile float n=b->a[i],d=3.0f;
    const float q=n/d;
    std::memcpy(&b->out[i],&q,4);
}

void Counts(int limit,int count) {
    crew::workers::SetLimit(limit);
    std::vector<std::atomic<int>> hits(static_cast<std::size_t>(count));
    std::atomic<DWORD> other{0};
    Count c{&hits,&other,GetCurrentThreadId()};
    for(int round=0;round<50;++round) {
        for(auto& h:hits)h.store(0);
        crew::workers::Fault f;
        const int faults=crew::workers::Run(&Hit,&c,count,&f);
        Expect(faults==0 && f.count==0,"no faults");
        bool once=true;
        for(auto& h:hits)once=once && h.load()==1;
        Expect(once,"every index exactly once");
    }
    if(limit==1)Expect(other.load()==0,"limit 1 runs on the caller alone");
    else if(crew::workers::Threads()>1 && count>8)Expect(other.load()!=0,"a worker took a share");
}
}  // namespace

int main() {
    for(const int n:{1,2,3,7,64,301})Counts(1,n);
    for(const int n:{1,2,3,7,64,301})Counts(0,n);
    Counts(2,100);

    {   // a fault ends its own call only
        crew::workers::SetLimit(0);
        std::vector<std::atomic<int>> hits(40);
        crew::workers::Fault f;
        const int faults=crew::workers::Run(&Faulty,&hits,40,&f);
        Expect(faults==1 && f.count==1,"one fault counted");
        Expect(f.index==7 && f.code==EXCEPTION_ACCESS_VIOLATION,"the fault's index and code");
        Expect(!f.dumped,"no writer: no dump");
        int ran=0;
        for(auto& h:hits)ran+=h.load();
        Expect(ran==39 && hits[7].load()==0,"the other calls ran");
    }

    {   // the caller's rounding mode on every thread
        Bits ref{},got{};
        for(int i=0;i<64;++i)ref.a[i]=got.a[i]=1.0f+static_cast<float>(i)*0.37f;
        const unsigned old=_mm_getcsr();
        _mm_setcsr((old&~_MM_ROUND_MASK)|_MM_ROUND_UP);
        crew::workers::SetLimit(1);
        crew::workers::Run(&Divide,&ref,64);
        crew::workers::SetLimit(0);
        crew::workers::Run(&Divide,&got,64);
        _mm_setcsr(old);
        Expect(std::memcmp(ref.out,got.out,sizeof ref.out)==0,"same bits with the caller's MXCSR");
        Bits nearest{};
        for(int i=0;i<64;++i)nearest.a[i]=ref.a[i];
        crew::workers::Run(&Divide,&nearest,64);
        Expect(std::memcmp(ref.out,nearest.out,sizeof ref.out)!=0,"the rounding mode reached the tasks");
    }

    crew::workers::Shutdown();
    Counts(0,64);   // made again after a shutdown
    crew::workers::Shutdown();

    if(failures){std::printf("worker_pool: %d failure(s)\n",failures);return 1;}
    std::printf("worker_pool: ok (%d threads)\n",crew::workers::Threads());
    return 0;
}
