// 对象更新多线程（src/object_update_mt_logic.h）对私有映射的 EDF.dll 逐位核对原版 0x6ED800：
//  ① 纯计算：工作线程算出的 T / M / 节点包围盒 / 并集 与原版 0x6E9130 和原版跑完后的内存逐位相同；
//  ② 登记 + 提交：同一假世界跑原版与引擎，整块内存与所有 Havok 调用（假 vtable 记录）逐位相同；
//  ③ 0 / 1 / 8 个工作线程、两种 MXCSR 结果相同；工作线程异常交回原版（guard page）、part 过多交回原版；
//  ④ flush 触发：窗口外 / 非游戏线程现算、重复登记、容量、窗口结束、过滤层判断、闸门 cave 实跑；
// 再给一组串行原版 vs 登记 + 并行的耗时（中位数）。没有 EDF.dll 时 SKIP（77）。
#include "object_update_mt_logic.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <cstdio>
#include <random>
#include <vector>

namespace {
using namespace crew::objmt;
unsigned char* image=nullptr;
int failures=0;
#define CHECK(cond,...) do{if(!(cond)){std::printf("FAIL %s:%d: ",__FILE__,__LINE__);std::printf(__VA_ARGS__);std::puts("");++failures;}}while(0)

// ---- 假 Havok 世界：H+0x18 世界（激活 / 停用 / 设姿态），H+0x20 运动（是否已加入 / 运动记录 / 运动状态）----
std::vector<unsigned char> calls;   // 记录（只在游戏线程上写：提交与原版都在游戏线程）
template<class T> void Put(const T& v) {const auto* p=reinterpret_cast<const unsigned char*>(&v);calls.insert(calls.end(),p,p+sizeof(T));}
void PutBytes(const void* p,std::size_t n) {const auto* b=static_cast<const unsigned char*>(p);calls.insert(calls.end(),b,b+n);}

struct World;
World* world=nullptr;
constexpr unsigned kIds=512;
#pragma warning(push)
#pragma warning(disable:4324)
struct alignas(16) Record { float transform[16]; unsigned char pad[0x40]; unsigned motion; unsigned char tail[0x7C]; };
struct alignas(16) State { unsigned char bytes[0x100]; };
struct alignas(16) Havok {
    unsigned char head[0x18];
    void** worldVt;    // +0x18
    void** motionVt;   // +0x20
    unsigned char mid[0x128-0x28];
    int* table128;     // +0x128
    unsigned char gap[8];
    int* table138;     // +0x138
    unsigned char tail[0x40];
};
#pragma warning(pop)
static_assert(offsetof(Havok,worldVt)==0x18 && offsetof(Havok,motionVt)==0x20 && offsetof(Havok,table128)==0x128 && offsetof(Havok,table138)==0x138);
static_assert(offsetof(Record,motion)==0x80);

struct World {
    unsigned char* base;
    std::size_t size,used;
    Havok* h;
    Record* records;
    State* states;
    void* Take(std::size_t n,std::size_t align=16) {used=(used+align-1)&~(align-1);void* p=base+used;used+=n;return used<=size ? p : nullptr;}
};
// 世界刚体（容器 K+0x68）用 0x10000 起的 ID，奇数的算「已加入」：0x11B0940 不激活、0x11B1710 停用。
// part / 节点刚体一律未加入（0x11B0B20 走 0x98F400 + vt+0x88 那条）。
constexpr unsigned kWorldIds=0x10000;
std::uint64_t WorldIsAdded(void*,unsigned id) noexcept {return id>=kWorldIds && (id&1);}
void* WorldRecord(void*,unsigned id) noexcept {return &world->records[id%kIds];}
void* WorldState(void*,unsigned motion) noexcept {return &world->states[motion%kIds];}
void WorldActivate(void*,unsigned id,int a,int b) noexcept {Put('A');Put(id);Put(a);Put(b);}
void WorldDeactivate(void*,const unsigned* ids,int n,int b) noexcept {Put('D');Put(n);Put(b);for(int i=0;i<n;++i)Put(ids[i]);}
void WorldSetPose(void*,unsigned id,const float* m,const double* pos,int flag) noexcept {Put('P');Put(id);PutBytes(m,48);PutBytes(pos,32);Put(flag);}
void WorldOther(void*,unsigned id,const void*,const void*,int) noexcept {Put('X');Put(id);}
void* worldVtable[64]{};
void* motionVtable[64]{};

struct Shape { unsigned char bytes[0x80]; };

// 一组容器（确定性：同一个 seed 在同一块内存里建出逐字节相同的世界）。
struct Layout { std::vector<unsigned char*> containers; unsigned char* faultBind=nullptr; };
float Uniform(std::mt19937& rng,float lo,float hi) {return std::uniform_real_distribution<float>(lo,hi)(rng);}
Layout Build(World& w,unsigned seed,unsigned count,unsigned maxParts,bool faultPage) {
    std::memset(w.base,0,w.size);
    w.used=0;
    Layout out;
    std::mt19937 rng(seed);
    if(faultPage) {   // 第一个容器的第一个 bind 单独占一页（guard page 测试）
        out.faultBind=static_cast<unsigned char*>(w.Take(4096,4096));
    }
    w.h=static_cast<Havok*>(w.Take(sizeof(Havok)));
    w.h->worldVt=worldVtable;w.h->motionVt=motionVtable;
    w.h->table128=static_cast<int*>(w.Take(kIds*12));
    w.h->table138=static_cast<int*>(w.Take(std::size_t{kWorldIds}*8));
    for(unsigned i=0;i<kWorldIds*2;++i)w.h->table138[i]=-1;
    w.records=static_cast<Record*>(w.Take(sizeof(Record)*kIds));
    w.states=static_cast<State*>(w.Take(sizeof(State)*kIds));
    for(unsigned i=0;i<kIds;++i) {
        w.records[i].motion=(i*7)%kIds;
        for(auto& f:w.records[i].transform)f=Uniform(rng,-1,1);
        // 一半的状态全零（静止），一半带小速度
        if(i%2)for(unsigned j=0;j<0x100/4;++j){const float f=Uniform(rng,-0.5f,0.5f);std::memcpy(w.states[i].bytes+4*j,&f,4);}
    }
    auto* const holder=static_cast<unsigned char*>(w.Take(0x60));
    std::memcpy(holder+kHolderWorld,&w.h,8);
    unsigned nextId=1;
    const auto newBody=[&]() {
        auto* const b=static_cast<unsigned char*>(w.Take(0x110));
        const unsigned id=nextId++%kIds;
        std::memcpy(b+kBodyId,&id,4);std::memcpy(b+kBodyHolder,&holder,8);
        return b;
    };
    const auto put=[](unsigned char* at,const void* v,std::size_t n){std::memcpy(at,v,n);};
    for(unsigned c=0;c<count;++c) {
        auto* const C=static_cast<unsigned char*>(w.Take(0x200));
        out.containers.push_back(C);
        auto* const owner=static_cast<unsigned char*>(w.Take(0x60));
        put(C+kCOwner,&owner,8);
        auto* const k=C+kCCompound;
        const bool compound=rng()%6!=0;
        auto* const sentinel=static_cast<unsigned char*>(w.Take(0x20));
        put(k+kKNodes,&sentinel,8);
        if(compound) {
            auto* const wb=newBody();put(k+kKWorldBody,&wb,8);
            const unsigned wid=kWorldIds+c;put(wb+kBodyId,&wid,4);
            auto* const shape=static_cast<unsigned char*>(w.Take(sizeof(Shape)));put(k+kKShape,&shape,8);
            for(unsigned j=0;j<8;++j){const float f=Uniform(rng,-3,3);put(shape+0x40+4*j,&f,4);}
        }
        // 节点（环形链表，sentinel 自己是头）
        const unsigned nodeCount=compound ? rng()%12 : 0;
        std::vector<unsigned char*> nodes;
        unsigned char* prev=sentinel;
        for(unsigned i=0;i<nodeCount;++i) {
            auto* const n=static_cast<unsigned char*>(w.Take(0xB0));
            put(prev,&n,8);prev=n;
            auto* const nb=newBody();put(n+kNodeBody,&nb,8);
            for(unsigned j=0;j<4;++j){const float f=Uniform(rng,-1,1);put(n+kNodeLocal+4*j,&f,4);}
            for(unsigned j=4;j<8;++j){const float f=Uniform(rng,0,1);put(n+kNodeLocal+4*j,&f,4);}
            if(rng()%40==0){const float nan=std::numeric_limits<float>::quiet_NaN();put(n+kNodeLocal,&nan,4);}
            for(unsigned j=0;j<16;++j){const float f=Uniform(rng,-1,1);put(n+kNodeMatrix+4*j,&f,4);}
            n[kNodeActive]=rng()%5!=0;
            nodes.push_back(n);
        }
        put(prev,&sentinel,8);
        // parts
        const unsigned partCount=rng()%(maxParts+1);
        auto* const parts=static_cast<unsigned char*>(w.Take(std::size_t{partCount}*kPartStride+16));
        put(C+kCParts,&parts,8);
        const std::uint64_t pc=partCount;put(C+kCPartCount,&pc,8);
        for(unsigned i=0;i<partCount;++i) {
            auto* const p=parts+i*kPartStride;
            p[kPartActive]=rng()%6!=0;
            const unsigned r=rng()%10;
            unsigned mode=r<5 ? 2u : r<9 ? 1u : r%2 ? 0u : 3u;
            if(mode==1 && nodes.empty())mode=2;
            put(p+kPartMode,&mode,4);
            unsigned char* bind=(faultPage && c==0 && i==0) ? out.faultBind : static_cast<unsigned char*>(w.Take(0x50));
            for(unsigned j=0;j<16;++j){const float f=Uniform(rng,-1,1);put(bind+4*j,&f,4);}
            auto* const slot=static_cast<unsigned char*>(w.Take(8));
            auto* const bone=static_cast<unsigned char*>(w.Take(0x100));
            for(unsigned j=0;j<16;++j){const float f=Uniform(rng,-2,2);put(bone+0xB0+4*j,&f,4);}
            put(slot,&bone,8);put(bind+0x40,&slot,8);put(p+kPartBind,&bind,8);
            auto* const body=newBody();put(p+kPartBody,&body,8);
            auto* const node=nodes.empty() ? nullptr : nodes[rng()%nodes.size()];
            put(p+kPartNode,&node,8);
            put(p+0xB8,&k,8);
        }
    }
    // 静止且姿态相同（0x11BAD40 为真，跳过）的情况：让一部分运动记录等于该 part 的原版 T
    const auto partTransform=reinterpret_cast<void(*)(void*,void*)>(image+kPartTransform);
    for(auto* C:out.containers) {
        auto* const parts=At<unsigned char*>(C,kCParts);
        for(std::uint64_t i=0;i<At<std::uint64_t>(C,kCPartCount);++i) {
            auto* const p=parts+i*kPartStride;
            if(At<unsigned>(p,kPartMode)!=2 || rng()%3)continue;
            if(faultPage && At<unsigned char*>(p,kPartBind)==out.faultBind)continue;
            alignas(16) float t[16];
            partTransform(p,t);
            const unsigned id=At<unsigned>(At<unsigned char*>(p,kPartBody),kBodyId);
            std::memcpy(w.records[id%kIds].transform,t,sizeof(t));
            std::memset(w.states[w.records[id%kIds].motion].bytes,0,sizeof(State));
        }
    }
    return out;
}

struct Snapshot { std::vector<unsigned char> memory,log; };
Snapshot Take(const World& w) {return {std::vector<unsigned char>(w.base,w.base+w.size),calls};}
bool Same(const Snapshot& a,const Snapshot& b,const char* what) {
    if(a.log!=b.log){std::printf("  %s: Havok call log differs (%zu vs %zu bytes)\n",what,a.log.size(),b.log.size());return false;}
    if(a.memory!=b.memory) {
        std::size_t i=0;while(a.memory[i]==b.memory[i])++i;
        std::printf("  %s: memory differs first at +0x%zX\n",what,i);return false;
    }
    return true;
}

void RunStock(const Layout& l) {
    const auto stock=reinterpret_cast<void(*)(void*)>(image+kBonesToProxies);
    for(auto* c:l.containers)stock(c);
}
void RunEngine(Engine& e,const Layout& l) {
    e.Open();
    for(auto* c:l.containers)e.Register(c);
    e.Close();
}

// ① 纯计算 vs 原版
void PureCompute(World& w) {
    const Layout l=Build(w,11,300,24,false);
    const Stock s=ResolveStock(image);
    Arena arena;
    std::vector<unsigned char> buf(std::size_t{32}<<20);
    arena.base=reinterpret_cast<unsigned char*>((reinterpret_cast<std::uintptr_t>(buf.data())+15)&~std::uintptr_t{15});
    arena.size=buf.size()-16;
    std::vector<Result> results(l.containers.size());
    unsigned parts2=0,parts1=0,nodesSeen=0;
    for(std::size_t i=0;i<l.containers.size();++i) {
        results[i].status=Compute(s,l.containers[i],results[i],arena);
        CHECK(results[i].status==kOk,"compute status %u",results[i].status);
        for(unsigned j=0;j<results[i].actionCount;++j) {
            const Action& a=results[i].actions[j];
            alignas(16) float t[16],mine[16];
            s.partTransform(a.part,t);
            if(a.mode==2){std::memcpy(mine,a.m,64);++parts2;}
            else {s.matrixToTransform(mine,a.m);++parts1;}
            CHECK(!std::memcmp(t,mine,64),"part transform differs from 0x6E9130");
        }
    }
    calls.clear();
    RunStock(l);   // 原版跑完后的节点 / 并集，与计算结果对比
    for(std::size_t i=0;i<l.containers.size();++i) {
        const Result& r=results[i];
        auto* const k=l.containers[i]+kCCompound;
        for(unsigned j=0;j<r.actionCount;++j)
            if(r.actions[j].mode==1)   // 原版 0x109B50 写进节点的矩阵（同一节点多个 part 时最后一个）
                for(unsigned q=r.actionCount;q-->0;)
                    if(r.actions[q].mode==1 && r.actions[q].target==r.actions[j].target) {
                        CHECK(!std::memcmp(static_cast<unsigned char*>(r.actions[q].target)+kNodeMatrix,r.actions[q].m,64),"node matrix differs");
                        break;
                    }
        for(unsigned j=0;j<r.nodeCount;++j) {
            ++nodesSeen;
            CHECK(!std::memcmp(static_cast<unsigned char*>(r.nodes[j].node)+kNodeBox,r.nodes[j].box,32),"node box differs");
        }
        if(r.compound && r.any)CHECK(!std::memcmp(k+kKAabb,r.aabb,32),"union differs");
    }
    std::printf("pure compute: %zu containers, %u mode-2 parts, %u mode-1 parts, %u nodes bit-identical\n",l.containers.size(),parts2,parts1,nodesSeen);
}

// ② ③ 登记 + 提交 == 原版；0 / 1 / 8 线程、两种 MXCSR
void Equivalence(World& w) {
    for(const unsigned csr:{0x1F80u,0x9FC0u}) {
        const unsigned saved=_mm_getcsr();
        _mm_setcsr(csr);
        calls.clear();
        RunStock(Build(w,23,400,24,false));
        const Snapshot ref=Take(w);
        _mm_setcsr(saved);
        if(csr==0x1F80u) {   // 覆盖面：原版走过的各条路径都要有
            unsigned bodies=0,nodes=0,activates=0,deactivates=0;
            for(std::size_t i=0;i<ref.log.size();) {
                const unsigned char tag=ref.log[i];
                if(tag=='A'){++activates;i+=13;}
                else if(tag=='D'){int n;std::memcpy(&n,&ref.log[i+1],4);++deactivates;i+=9+4*static_cast<std::size_t>(n);}
                else if(tag=='P'){int flag;std::memcpy(&flag,&ref.log[i+85],4);(flag ? nodes : bodies)++;i+=89;}
                else {CHECK(false,"unexpected Havok call %c (0x11B0B20's added-body path)",tag);break;}
            }
            const Layout l=Build(w,23,400,24,false);
            unsigned active2=0,nan=0;
            for(auto* c:l.containers) {
                auto* const parts=At<unsigned char*>(c,kCParts);
                for(std::uint64_t i=0;i<At<std::uint64_t>(c,kCPartCount);++i)active2+=parts[i*kPartStride+kPartActive] && At<unsigned>(parts+i*kPartStride,kPartMode)==2;
            }
            RunStock(l);
            for(auto* c:l.containers)nan+=At<float>(c+kCCompound,kKAabb)==-1.0f && At<float>(c+kCCompound,kKAabb+16)==1.0f;
            std::printf("coverage: %u body poses set, %u skipped (at rest and unchanged), %u node poses, %u activations, %u deactivations, %u NaN unions\n",
                        bodies,active2-bodies,nodes,activates,deactivates,nan);
            CHECK(bodies && active2>bodies && nodes && activates && deactivates && nan,"a stock path is not covered");
            // 反向对照：少跑一个容器必须被 Same 判为不同
            calls.clear();
            const Layout m=Build(w,23,400,24,false);
            const auto stock=reinterpret_cast<void(*)(void*)>(image+kBonesToProxies);
            for(std::size_t i=0;i+1<m.containers.size();++i)stock(m.containers[i]);
            CHECK(!Same(ref,Take(w),"negative control (expected)"),"comparison cannot tell a skipped container");
        }
        for(const unsigned threads:{0u,1u,8u}) {
            for(const bool shadow:{false,true}) {
                auto* const e=new Engine();
                CHECK(e->Init(image,threads),"engine init");
                unsigned mismatches=0;
                static unsigned* counter;counter=&mismatches;
                if(shadow){e->shadowEvery=1;e->onMismatch=[](void*,const char* what,const char* when){std::printf("  mismatch %s (%s)\n",what,when);++*counter;};}
                const Layout l=Build(w,23,400,24,false);
                calls.clear();
                _mm_setcsr(csr);
                RunEngine(*e,l);
                _mm_setcsr(saved);
                CHECK(Same(ref,Take(w),"engine"),"engine with %u threads (MXCSR %X, shadow %d) differs from stock",threads,csr,shadow);
                CHECK(e->stats.committed==l.containers.size() && e->stats.handedBack==0,"committed %u of %zu",e->stats.committed,l.containers.size());
                CHECK(!shadow || (e->stats.shadowed>0 && mismatches==0 && e->stats.mismatches==0),"shadow %u checked %u mismatched",e->stats.shadowed,mismatches);
                e->Shutdown();
                delete e;
            }
        }
    }
    std::puts("register + commit: 400 containers identical to stock (memory and Havok calls), 0/1/8 threads, 2 MXCSR modes, shadow clean");
}

// 交回原版：工作线程异常（guard page，只触发一次）、part 过多。
void HandBack(World& w) {
    {
        calls.clear();
        RunStock(Build(w,31,40,24,true));
        const Snapshot ref=Take(w);
        auto* const e=new Engine();
        CHECK(e->Init(image,2),"engine init");
        static unsigned faults;faults=0;
        e->onFault=[](EXCEPTION_POINTERS*,void*){++faults;};
        const Layout l=Build(w,31,40,24,true);
        DWORD old;
        CHECK(VirtualProtect(l.faultBind,4096,PAGE_READWRITE|PAGE_GUARD,&old),"guard");
        calls.clear();
        RunEngine(*e,l);
        CHECK(Same(ref,Take(w),"fault"),"fault hand-back differs from stock");
        CHECK(faults==1 && e->stats.faults==1 && e->stats.handedBack==1 && e->disabled,"faults %u handed back %u",e->stats.faults,e->stats.handedBack);
        e->Open();CHECK(!e->open,"engine stays off after a fault");e->Close();
        e->Shutdown();delete e;
    }
    {
        calls.clear();
        RunStock(Build(w,37,3,1200,false));
        const Snapshot ref=Take(w);
        auto* const e=new Engine();
        CHECK(e->Init(image,2),"engine init");
        const Layout l=Build(w,37,3,1200,false);
        calls.clear();
        RunEngine(*e,l);
        CHECK(Same(ref,Take(w),"overflow"),"overflow hand-back differs from stock");
        unsigned big=0;for(auto* c:l.containers)big+=At<std::uint64_t>(c,kCPartCount)>kMaxParts;
        CHECK(e->stats.overflows==big,"overflows %u, containers over the limit %u",e->stats.overflows,big);
        e->Shutdown();delete e;
    }
    std::puts("hand-back: worker fault (guard page) and part overflow run the stock code, results identical");
}

// ④ flush 触发与过滤
std::uint64_t gateCalls=0;
void* gateSrc=nullptr;
unsigned gateArg=0;
void GateProbe(void* src,unsigned arg) noexcept {++gateCalls;gateSrc=src;gateArg=arg;}
void Triggers(World& w) {
    auto* const e=new Engine();
    CHECK(e->Init(image,2),"engine init");
    Layout l=Build(w,41,20,8,false);
    // 窗口外：现算
    e->Register(l.containers[0]);
    CHECK(e->stats.inlineCalls==1 && !e->pending,"outside the window runs inline");
    // 窗口内重复登记：先 flush（dup）
    e->Open();
    e->Register(l.containers[0]);
    e->Register(l.containers[1]);
    CHECK(e->pending==2 && e->IsPending(l.containers[0]) && e->IsPending(l.containers[1]) && !e->IsPending(l.containers[2]),"pending set");
    e->Register(l.containers[0]);
    CHECK(e->stats.flushes[static_cast<unsigned>(Reason::kDuplicate)]==1 && e->pending==1,"duplicate flush");
    // 别的线程登记：现算
    struct Ctx { Engine* e; unsigned char* c; } ctx{e,l.containers[3]};
    const HANDLE t=CreateThread(nullptr,0,[](void* p)->DWORD{auto* x=static_cast<Ctx*>(p);x->e->Register(x->c);return 0;},&ctx,0,nullptr);
    WaitForSingleObject(t,INFINITE);CloseHandle(t);
    CHECK(e->stats.inlineCalls==2 && e->pending==1,"other thread runs inline");
    // 插件 / 闸门 flush
    e->Flush(Reason::kRayQuery);
    CHECK(e->stats.flushes[static_cast<unsigned>(Reason::kRayQuery)]==1 && !e->pending && !e->IsPending(l.containers[0]),"explicit flush");
    e->Flush(Reason::kRayQuery);
    CHECK(e->stats.flushes[static_cast<unsigned>(Reason::kRayQuery)]==1,"flush with nothing pending is not counted");
    e->Close();
    e->Open();
    for(unsigned i=0;i<20;++i)e->Register(l.containers[i]);
    CHECK(e->pending==20,"20 pending");
    e->Flush(Reason::kPlugin);
    CHECK(e->stats.flushes[static_cast<unsigned>(Reason::kPlugin)]==1 && !e->pending,"plugin flush");
    e->Close();
    // 窗口结束
    e->Open();e->Register(l.containers[5]);e->Close();
    CHECK(e->stats.flushes[static_cast<unsigned>(Reason::kWindowEnd)]>=1 && !e->pending,"window end flush");
    e->Shutdown();delete e;

    // 容量：引擎任务表满
    {
        auto* const big=new Engine();
        CHECK(big->Init(image,0),"engine init");
        Layout lots=Build(w,43,Engine::kMaxTasks+5,0,false);
        calls.clear();
        RunEngine(*big,lots);
        CHECK(big->stats.flushes[static_cast<unsigned>(Reason::kCapacity)]==1 && big->stats.committed==Engine::kMaxTasks+5,"capacity flush %u",big->stats.flushes[static_cast<unsigned>(Reason::kCapacity)]);
        big->Shutdown();delete big;
    }

    // 过滤：层 5 / 6 互相碰；组号一律当会碰；与代理层都不碰的层不 flush
    std::uint32_t layers[32]{};
    layers[5]=(1u<<1)|(1u<<5);layers[6]=1u<<2;layers[1]=1u<<5;layers[2]=1u<<6;layers[9]=1u<<9;
    CHECK(FilterMayHitProxies(1,layers) && FilterMayHitProxies(2,layers) && FilterMayHitProxies(5,layers),"proxy layers");
    CHECK(!FilterMayHitProxies(9,layers) && !FilterMayHitProxies(3,layers),"unrelated layers");
    CHECK(FilterMayHitProxies(9|(1u<<16),layers),"group bits");
    layers[9]|=1u<<6;
    CHECK(FilterMayHitProxies(9,layers),"layer that collides with 6");

    // 闸门 cave 实跑：sub rsp,8; jmp cave ... cave 末尾回到 add rsp,8; ret。挪走的原字节 = mov rax,rcx（或 r8）。
    auto* const code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    for(const Src src:{Src::kRcx,Src::kR8}) {
        std::memset(code,0xCC,4096);
        unsigned char* const entry=code;
        unsigned char* const back=code+64;
        unsigned char* const cave=code+128;
        const unsigned char movRaxRcx[]{0x48,0x89,0xC8},movRaxR8[]{0x4C,0x89,0xC0};
        const unsigned char* const displaced=src==Src::kRcx ? movRaxRcx : movRaxR8;
        static volatile std::uint32_t pending=0;
        const std::size_t n=BuildGateCave(cave,kCaveMax,&pending,src,0x1234,reinterpret_cast<const void*>(&GateProbe),displaced,3,back);
        CHECK(n>0,"cave built");
        const unsigned char head[]{0x48,0x83,0xEC,0x08,0xE9};
        std::memcpy(entry,head,5);
        const std::int32_t rel=static_cast<std::int32_t>(cave-(entry+9));
        std::memcpy(entry+5,&rel,4);
        const unsigned char tail[]{0x48,0x83,0xC4,0x08,0xC3};
        std::memcpy(back,tail,5);
        FlushInstructionCache(GetCurrentProcess(),code,4096);
        using Fn=void*(*)(void*,void*,void*);
        const auto fn=reinterpret_cast<Fn>(entry);
        void* const a=reinterpret_cast<void*>(0x1111);void* const c=reinterpret_cast<void*>(0x3333);
        gateCalls=0;
        void* r=fn(a,nullptr,c);
        CHECK(r==(src==Src::kRcx ? a : c) && gateCalls==0,"cave fast path");
        pending=1;
        r=fn(a,nullptr,c);
        CHECK(r==(src==Src::kRcx ? a : c) && gateCalls==1 && gateSrc==(src==Src::kRcx ? a : c) && gateArg==0x1234,"cave slow path");
        pending=0;
    }
    VirtualFree(code,0,MEM_RELEASE);
    std::puts("flush triggers: outside window / other thread inline, duplicate, explicit, capacity, window end, filter layers, gate cave ok");
}

// 真 EDF.dll 上的指纹与站点字节（安装前核对的同一份表）
void Signatures() {
    for(const auto& r:kCodeRanges)CHECK(Fnv1a64(image+r.begin,r.end-r.begin)==r.fnv,"fingerprint EDF+0x%X",r.begin);
    CHECK(!std::memcmp(image+kWindowOpen,kCallLock,5) && !std::memcmp(image+kWindowClose,kCallUnlock,5),"window calls");
    CHECK(!std::memcmp(image+kRegisterInsect,kCallInsect,5) && !std::memcmp(image+kRegisterHuman,kCallHuman,5),"register calls");
    for(const auto& g:kGateSites)CHECK(!std::memcmp(image+g.site,g.bytes,g.size),"gate site EDF+0x%X",g.site);
    std::printf("signatures: %zu code ranges, 4 calls, %u gate sites match\n",std::size(kCodeRanges),kGateCount);
}

double Median(std::array<double,5> v) {std::sort(v.begin(),v.end());return v[2];}
void Timing(World& w) {
    const Layout l=Build(w,53,600,16,false);
    const auto stock=reinterpret_cast<void(*)(void*)>(image+kBonesToProxies);
    auto* const e=new Engine();
    const DWORD cores=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    const unsigned workers=cores>1 ? std::min<unsigned>(cores-1,Engine::kMaxThreads) : 0;
    e->Init(image,workers);
    std::array<double,5> a{},b{};
    for(unsigned round=0;round<5;++round) {
        for(unsigned pass=0;pass<2;++pass) {
            calls.clear();
            const auto t0=std::chrono::steady_clock::now();
            for(unsigned rep=0;rep<10;++rep) {
                if((pass^round)&1){e->Open();for(auto* c:l.containers)e->Register(c);e->Close();}
                else for(auto* c:l.containers)stock(c);
            }
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count()/10;
            ((pass^round)&1 ? b : a)[round]=ms;
        }
    }
    const double compute=static_cast<double>(e->stats.computeTicks.load());
    LARGE_INTEGER f;QueryPerformanceFrequency(&f);
    const double commitMs=static_cast<double>(e->stats.commitTicks)*1000/static_cast<double>(f.QuadPart)/50,computeMs=compute*1000/static_cast<double>(f.QuadPart)/50;
    std::printf("timing: 600 containers a frame, stock %.3f ms -> registered + %u workers %.3f ms (median of five); "
                "of which compute %.3f ms (parallel, summed over threads), serial commit %.3f ms\n",Median(a),workers,Median(b),computeMs,commitMs);
    e->Shutdown();delete e;
}
}  // namespace

int main(int argc,char** argv) {
    if(argc<2 || !argv[1][0] || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: object update multithread comparison requires EDF.dll");
        return 77;
    }
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    if(!image)return 1;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=0x678CCB46){std::puts("SKIP: not the supported EDF.dll");return 77;}
    worldVtable[0x68/8]=reinterpret_cast<void*>(&WorldActivate);
    worldVtable[0x80/8]=reinterpret_cast<void*>(&WorldDeactivate);
    worldVtable[0x88/8]=reinterpret_cast<void*>(&WorldSetPose);
    worldVtable[0xA0/8]=reinterpret_cast<void*>(&WorldOther);
    motionVtable[0x70/8]=reinterpret_cast<void*>(&WorldIsAdded);
    motionVtable[0x80/8]=reinterpret_cast<void*>(&WorldRecord);
    motionVtable[0xF8/8]=reinterpret_cast<void*>(&WorldState);
    World w{};
    w.size=std::size_t{64}<<20;
    w.base=static_cast<unsigned char*>(VirtualAlloc(nullptr,w.size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    world=&w;
    Signatures();
    PureCompute(w);
    Equivalence(w);
    HandBack(w);
    Triggers(w);
    Timing(w);
    if(failures){std::printf("object update multithread: %d failures\n",failures);return 1;}
    std::puts("object update multithread: all checks passed");
    return 0;
}
