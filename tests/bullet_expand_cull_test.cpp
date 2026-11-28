// The bullets' group expansion cut (src/bullet_expand_cull.cpp) against the stock addBody (0x109DA0), on two private
// EDF.dll mappings: the hooks installed into one, the other left stock; both run on the same fake world (the hknp
// reader: isValid / getBody / the property lookup, bodies with world boxes through a fake shape type's calcAabb,
// groups and their lists) and fill their own collector:
//  - the geometry: Sweep::Touches never refuses a box the sweep (the segment swept by the cube of half size
//    max |extent|: the broadphase's box, the ray and the sphere in it) reaches (the exact L-infinity distance by a
//    golden-section search, convex in t), on random segments (zero length, axis-aligned, 10 km coordinates);
//  - random worlds inside a bullet cast (the hooked cast calls: a fake broadphase over an overlap list): the list is
//    stock's in order less only parts the sweep cannot reach, empty exactly when stock's is; as stock (id for id):
//    a filter functor (its calls too), outside any cast, ExpandCull=0, a blast bullet, casts inside a cast (each its
//    own sweep); invalid ids / no shape kept; the world changed between steps (a new generation: boxes moved, nodes
//    on / off, relinked, bodies moved between groups);
//  - an exception out of the filter functor through the cave (its unwind info) and the cast's scope;
//  - the settle: casts left whole until it is chained; chained over a fake damagestats redirect of 0x23426F; one with
//    entries whose hook moves a body onto the sweep ends the generation (the body kept after it; the control without
//    a settle cuts it on its old box), the stale probe counts it; one without entries keeps the generation;
//  - the shadow record: a sample's list left whole, and the cut list mapped back from it (on the list and on a per-id
//    filtered list of it, as the ray's and step 3's lists are) equal to a cut cast's, samples after a settle too;
//  - timing (printed, never judged: the run's priority and the machine's load decide it): a dense swarm, the cost a
//    part of the cut (the box cache hit and the test) over the walk.
// The real narrowphase (Havok's tasks and shapes) does not run here: the debug shadow check compares its hits in the game.
#include "../src/bullet_expand_cull.cpp"
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace crew {
unsigned char* image=nullptr;
unsigned long long testRefusals=0;
void Log(const char* format,...) noexcept {
    char line[512];
    va_list args;
    va_start(args,format);
    vsnprintf(line,sizeof(line),format,args);
    va_end(args);
    if(std::strstr(line,"REFUSED"))++testRefusals;
    std::printf("log: %s\n",line);
}
Config testConfig;
const Config& Cfg() noexcept { return testConfig; }
}  // namespace crew

namespace {
using namespace crew;

constexpr unsigned kSystemGlobal=0x20B2958;
constexpr std::size_t kBodyWorld=0x100,kBodySelf=0x180,kBodyObject=0x188,kBodyGroup=0x190,kBodyBox=0x1A0,kBodyBytes=0x1C0;
constexpr std::size_t kNodeBytes=0xB0,kGroupBytes=0x100;
constexpr std::size_t kCollectorGroups=0x29;
constexpr unsigned kFakeType=5;

template<class T> void Wr(void* p,std::size_t off,T v) noexcept { std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof(v)); }

int failures=0;
bool Check(bool ok,const char* what) {
    if(!ok){++failures;if(failures<=20)std::printf("FAIL: %s\n",what);}
    return ok;
}

unsigned char* stock=nullptr;   // the second mapping
std::vector<unsigned char*> bodyById;
std::vector<unsigned char> invalidId;   // isValid 0 for these

// The hknp reader / property store: R = [W+0x58]+0x20; +0x68 isValid(id), +0x80 getBody(id), +0xE8 (id, key, size).
void* storeVtable[0x40];
struct { unsigned char pad[0x20]; void* vtable; } storeBlock;
struct { unsigned char system[0x10]; unsigned char world[0x100]; } systemBlock;   // the System, +0x10 the world
unsigned char* const world=systemBlock.world;
alignas(16) unsigned char aabbTable[0x100*8];
alignas(16) unsigned char fakeShape[0x40];
unsigned long long aabbCalls=0;
int __fastcall StoreValid(void*,unsigned id) { return id<bodyById.size() && bodyById[id] && !invalidId[id] ? 1 : 0; }
const void* __fastcall StoreBody(void*,unsigned id) { return id<bodyById.size() ? bodyById[id] : nullptr; }
void* __fastcall StoreLookup(void*,unsigned id,unsigned key,unsigned) {
    if(id>=bodyById.size() || !bodyById[id])return nullptr;
    unsigned char* const body=bodyById[id];
    switch(key) {
    case 0x8001: return body+kBodySelf;
    case 0: return body+kBodyObject;
    case 1: return Rd<void*>(body,kBodyGroup) ? body+kBodyGroup : nullptr;
    default: return nullptr;
    }
}
void __fastcall FakeAabb(const void*,const void* body,float* out) {
    ++aabbCalls;
    float box[6];
    std::memcpy(box,static_cast<const unsigned char*>(body)+kBodyBox,sizeof(box));
    out[0]=box[0];out[1]=box[1];out[2]=box[2];out[3]=0;out[4]=box[3];out[5]=box[4];out[6]=box[5];out[7]=0;
}

bool WriteGlobal(unsigned char* at,unsigned rva,const void* value,std::size_t size) noexcept {
    DWORD old=0;
    if(!VirtualProtect(at+rva,size,PAGE_READWRITE,&old))return false;
    std::memcpy(at+rva,value,size);
    DWORD ignored=0;
    VirtualProtect(at+rva,size,old,&ignored);
    return true;
}
bool SetUpStore(unsigned char* at) noexcept {
    storeVtable[kReaderValidSlot]=reinterpret_cast<void*>(&StoreValid);
    storeVtable[kReaderBodySlot]=reinterpret_cast<void*>(&StoreBody);
    storeVtable[0xE8/8]=reinterpret_cast<void*>(&StoreLookup);
    storeBlock.vtable=storeVtable;
    Wr<void*>(world,kWorldHknp,&storeBlock);   // [W+0x58]+0x20 = the reader
    fakeShape[kShapeType]=static_cast<unsigned char>(kFakeType);
    Wr<void*>(aabbTable,kFakeType*kAabbStride+kAabbEntry,reinterpret_cast<void*>(&FakeAabb));
    const void* const table=aabbTable;
    void* const system=systemBlock.system;    // *(image+0x20B2958)+0x10 = W
    return WriteGlobal(at,kSystemGlobal,&system,sizeof(system)) && WriteGlobal(at,kAabbTable,&table,sizeof(table));
}

// The stateful functor: vtable +0x10, bool(functor, unsigned* id).
struct Functor { void** vtable; unsigned calls; unsigned throwAt; };
void* functorVtable[4];
bool __fastcall FunctorCall(void* f,unsigned* id) {
    auto* const fn=static_cast<Functor*>(f);
    const unsigned n=++fn->calls;
    if(fn->throwAt && n==fn->throwAt)throw std::runtime_error("functor");
    return ((*id*7u+n)%3u)!=0;
}

// A bullet core with its collector at +0x800.
struct Collector {
    std::vector<unsigned char> core;
    std::vector<unsigned> buffer;
    explicit Collector(unsigned capacity) : core(0xC00,0), buffer(capacity) {
        Wr<void*>(c(),kCollectorList,buffer.data());
        Wr<unsigned>(c(),kCollectorCount,0);
        Wr<unsigned>(c(),kCollectorCapacity,capacity);
        c()[kCollectorGroups]=1;
    }
    unsigned char* c() noexcept { return core.data()+kCollectorCore; }
    const unsigned char* c() const noexcept { return core.data()+kCollectorCore; }
    void Reset(void* functor,bool blast=false) noexcept {
        Wr<unsigned>(c(),kCollectorCount,0);Wr<void*>(c(),kCollectorFilter,functor);
        Wr<unsigned>(core.data(),kCoreFlags,blast ? kFlagBlast|0x21u : 0x21u);
    }
    unsigned Count() const noexcept { return Rd<unsigned>(c(),kCollectorCount); }
    std::vector<unsigned> Ids() const { return {buffer.begin(),buffer.begin()+Count()}; }
};

unsigned char* Zeroed(std::size_t n) { auto* p=static_cast<unsigned char*>(std::calloc(1,n));if(!p)std::abort();return p; }

// ---- geometry ----
struct Rng {
    std::mt19937 g;
    explicit Rng(unsigned seed) : g(seed) {}
    float U(float lo,float hi) { return std::uniform_real_distribution<float>(lo,hi)(g); }
    unsigned N(unsigned n) { return static_cast<unsigned>(g()%n); }
};
// The exact L-infinity distance from the segment a -> b to the box [lo, hi] (convex in t: golden section).
double PointBoxInf(const double p[3],const float lo[3],const float hi[3]) noexcept {
    double s=0;
    for(int k=0;k<3;++k)s=(std::max)(s,p[k]<lo[k] ? lo[k]-p[k] : p[k]>hi[k] ? p[k]-hi[k] : 0.0);
    return s;
}
double SegmentBoxInf(const float a[3],const float b[3],const float lo[3],const float hi[3]) noexcept {
    const auto at=[&](double t) noexcept {
        double p[3];
        for(int k=0;k<3;++k)p[k]=a[k]+(static_cast<double>(b[k])-a[k])*t;
        return PointBoxInf(p,lo,hi);
    };
    double l=0,h=1;
    const double g=0.6180339887498949;
    for(int i=0;i<200;++i) {
        const double m1=h-(h-l)*g,m2=l+(h-l)*g;
        if(at(m1)<=at(m2))h=m2; else l=m1;
    }
    return (std::min)(at((l+h)/2),(std::min)(at(0),at(1)));
}
double MaxAbs(const float e[3]) noexcept { return (std::max)({std::fabs(static_cast<double>(e[0])),std::fabs(static_cast<double>(e[1])),std::fabs(static_cast<double>(e[2]))}); }

void RandomSegment(Rng& rng,float a[3],float b[3],float field) {
    const float base=rng.N(4)==0 ? 0.0f : rng.U(-9000.0f,9000.0f);
    const float len=rng.N(8)==0 ? 0.0f : rng.U(0.0f,60.0f);
    float d[3]={rng.U(-1,1),rng.U(-1,1),rng.U(-1,1)};
    if(rng.N(4)==0){const unsigned k=rng.N(3);for(unsigned j=0;j<3;++j)if(j!=k)d[j]=0;}
    double m=std::sqrt(static_cast<double>(d[0])*d[0]+static_cast<double>(d[1])*d[1]+static_cast<double>(d[2])*d[2]);
    if(m==0){d[0]=1;m=1;}
    for(int k=0;k<3;++k){a[k]=base+rng.U(-field,field);b[k]=a[k]+static_cast<float>(d[k]/m)*len;}
}
void RandomExtent(Rng& rng,float e[3]) {
    const unsigned kind=rng.N(4);
    for(int k=0;k<3;++k)e[k]=kind==0 ? 0.0f : kind==1 ? rng.U(0.0f,0.5f) : rng.U(0.0f,4.0f);
    if(rng.N(8)==0)e[rng.N(3)]=-e[0];   // a negative component: max |extent|
}
void RandomBoxNear(Rng& rng,const float a[3],const float b[3],float spread,float lo[3],float hi[3]) {
    const float t=rng.U(-0.3f,1.3f);
    for(int k=0;k<3;++k) {
        const float c=a[k]+(b[k]-a[k])*t+rng.U(-spread,spread);
        const float h=rng.N(6)==0 ? 0.0f : rng.U(0.05f,6.0f);
        lo[k]=c-h;hi[k]=c+h;
    }
}

void Geometry() {
    Rng rng(1128);
    unsigned reached=0,refused=0,looseKept=0;
    for(unsigned i=0;i<400000;++i) {
        float a[3],b[3],e[3],lo[3],hi[3];
        RandomSegment(rng,a,b,50.0f);
        RandomExtent(rng,e);
        RandomBoxNear(rng,a,b,rng.N(2) ? 3.0f : 30.0f,lo,hi);
        Sweep s{};
        if(!Check(s.Set(a,b,e),"Set on finite input"))continue;
        const double d=SegmentBoxInf(a,b,lo,hi),r=MaxAbs(e);
        const bool touch=s.Touches(lo,hi);
        if(d<=r+kMargin-1e-6) {
            ++reached;
            if(!touch){Check(false,"a box the sweep reaches refused");std::printf("  d %.6f r %.3f\n",d,r);}
        } else if(touch) ++looseKept;
        else ++refused;
    }
    std::printf("geometry: 400000 cases, %u reached (all kept), %u refused, %u kept loose (within the margin's rounding)\n",reached,refused,looseKept);
    Check(refused>100000,"the test refuses boxes far off");
    const float nan=std::numeric_limits<float>::quiet_NaN(),one[3]{1,1,1},bad[3]{nan,0,0};
    Sweep s{};
    Check(!s.Set(bad,one,one) && !s.Set(one,bad,one) && !s.Set(one,one,bad),"Set refuses a non-finite input");
    float lo[3],hi[3];
    BoxEntry u{};
    BoxCache::Unbounded(u);
    std::memcpy(lo,u.lo,sizeof(lo));std::memcpy(hi,u.hi,sizeof(hi));
    const float z[3]{0,0,0},f[3]{1e4f,1e4f,1e4f};
    Check(s.Set(z,f,z) && s.Touches(lo,hi),"an unbounded box touches every sweep");
}

// ---- the fake world ----
// A new batch step: what the hooked step call does before the step (a new generation, the step's first).
unsigned NewStep() noexcept {
    stepNo.fetch_add(1);
    const unsigned g=Bump();
    stepStart.store(g);
    return g;
}
struct Fake {
    std::vector<unsigned char*> bodies,groups;
    std::vector<std::vector<unsigned char*>> lists;   // a group's nodes, in order
    unsigned char object[16]{};
    ~Fake() { for(auto* b:bodies)std::free(b);for(auto* g:groups)std::free(g);for(auto& l:lists)for(auto* n:l)std::free(n); }
    void Link(unsigned g) noexcept {   // the group's circular list from `lists[g]`
        unsigned char* const head=Rd<unsigned char*>(groups[g],kGroupList);
        unsigned char* prev=head;
        for(auto* n:lists[g]){Wr<void*>(prev,0,n);prev=n;}
        Wr<void*>(prev,0,head);
    }
};
void PlaceBox(unsigned char* body,Rng& rng,float field) {
    float box[6];
    for(int k=0;k<3;++k){const float c=rng.U(-field,field),h=rng.N(8)==0 ? 0.0f : rng.U(0.1f,4.0f);box[k]=c-h;box[3+k]=c+h;}
    std::memcpy(body+kBodyBox,box,sizeof(box));
}
void NewBody(Fake& f,Rng& rng,unsigned id,float field) {
    auto* const b=Zeroed(kBodyBytes);
    Wr<unsigned>(b,kBodyId,id);
    Wr<void*>(b,kBodyWorld,world);
    Wr<void*>(b,kBodySelf,b);
    Wr<void*>(b,kBodyObject,(rng.N(8)) ? static_cast<void*>(f.object) : nullptr);
    Wr<void*>(b,kBodyShape,rng.N(40)==0 ? nullptr : static_cast<void*>(fakeShape));
    PlaceBox(b,rng,field);
    f.bodies.push_back(b);bodyById[id]=b;
    invalidId[id]=rng.N(40)==0 ? 1 : 0;
}
void Build(Fake& f,Rng& rng,unsigned nBodies,unsigned nGroups,unsigned mostList,unsigned groupedPct,unsigned onPct,float field) {
    bodyById.assign(nBodies+1,nullptr);
    invalidId.assign(nBodies+1,0);
    for(unsigned i=1;i<=nBodies;++i)NewBody(f,rng,i,field);
    for(unsigned g=0;g<nGroups;++g) {
        auto* const group=Zeroed(kGroupBytes);
        auto* const head=Zeroed(kNodeBytes);
        Wr<void*>(group,kGroupList,head);
        f.groups.push_back(group);
        f.lists.emplace_back();
        const unsigned len=mostList ? rng.N(mostList+1) : 0;
        for(unsigned k=0;k<len;++k) {
            auto* const n=Zeroed(kNodeBytes);
            Wr<void*>(n,kNodeBody,f.bodies[rng.N(nBodies)]);
            n[kNodeOn]=rng.N(100)<onPct ? 1 : 0;
            f.lists[g].push_back(n);
        }
        f.Link(g);
    }
    for(auto* b:f.bodies)Wr<void*>(b,kBodyGroup,(nGroups && rng.N(100)<groupedPct) ? f.groups[rng.N(nGroups)] : nullptr);
}
// A new step: boxes moved, nodes switched, lists relinked, bodies regrouped; the cache's generation bumped.
void Mutate(Fake& f,Rng& rng,float field) {
    for(unsigned k=0,n=1+rng.N(8);k<n && !f.groups.empty();++k) {
        const unsigned g=rng.N(static_cast<unsigned>(f.groups.size()));
        auto& list=f.lists[g];
        switch(rng.N(4)) {
        case 0: if(!list.empty())list[rng.N(static_cast<unsigned>(list.size()))][kNodeOn]^=1;break;
        case 1: if(!list.empty())Wr<void*>(list[rng.N(static_cast<unsigned>(list.size()))],kNodeBody,f.bodies[rng.N(static_cast<unsigned>(f.bodies.size()))]);break;
        case 2: if(list.size()>1){std::swap(list[rng.N(static_cast<unsigned>(list.size()))],list[rng.N(static_cast<unsigned>(list.size()))]);f.Link(g);}break;
        default: Wr<void*>(f.bodies[rng.N(static_cast<unsigned>(f.bodies.size()))],kBodyGroup,f.groups[rng.N(static_cast<unsigned>(f.groups.size()))]);break;
        }
    }
    for(unsigned k=0,n=rng.N(static_cast<unsigned>(f.bodies.size())+1);k<n;++k)PlaceBox(f.bodies[rng.N(static_cast<unsigned>(f.bodies.size()))],rng,field);
    NewStep();
}

// The fake broadphase (the hooked mapping's 0x11A70B0): addBody for each overlapped id. `from` the CastArgs.
struct CastArgs {
    float from[4],to[4],extent[4];
    const std::vector<unsigned>* ids;
    Collector* nested;
    CastArgs* nestedArgs;
    unsigned nestAt;
};
std::uintptr_t __fastcall FakeCast(void* collector,void* w,void* from,void*,void*) {
    const auto& o=*static_cast<const CastArgs*>(from);
    const auto addBody=reinterpret_cast<void(__fastcall*)(void*,unsigned)>(image+kAddBody);
    for(unsigned i=0;i<o.ids->size();++i) {
        if(o.nested && i==o.nestAt)
            BulletCast(o.nested->c(),w,o.nestedArgs->from,o.nestedArgs->to,o.nestedArgs->extent);
        addBody(collector,(*o.ids)[i]);
    }
    return 0x5A;
}
std::uintptr_t Cast(Collector& c,CastArgs& a) { return BulletCast(c.c(),world,a.from,a.to,a.extent); }
bool PatchCast() noexcept {
    unsigned char code[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto to=reinterpret_cast<std::uintptr_t>(&FakeCast);
    std::memcpy(code+2,&to,8);
    DWORD old=0;
    if(!VirtualProtect(image+kCast,sizeof(code),PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(image+kCast,code,sizeof(code));
    DWORD ignored=0;
    VirtualProtect(image+kCast,sizeof(code),old,&ignored);
    FlushInstructionCache(GetCurrentProcess(),image+kCast,sizeof(code));
    return true;
}

void StockRun(Collector& c,const std::vector<unsigned>& ids) {
    const auto addBody=reinterpret_cast<void(__fastcall*)(void*,unsigned)>(stock+kAddBody);
    for(const unsigned id:ids)addBody(c.c(),id);
}
bool Same(const Collector& a,const Collector& b,const char* what,unsigned round) {
    if(a.Count()==b.Count() && std::memcmp(a.buffer.data(),b.buffer.data(),a.Count()*sizeof(unsigned))==0)return true;
    ++failures;
    std::printf("FAIL %s round %u: stock %u ids, hooked %u\n",what,round,a.Count(),b.Count());
    return false;
}
// A part the sweep can reach: not a box (unbounded), an invalid id, or within max |extent| (L-infinity) of the segment.
bool Reachable(unsigned id,const CastArgs& a) {
    if(id>=bodyById.size() || !bodyById[id] || invalidId[id] || !Rd<void*>(bodyById[id],kBodyShape))return true;
    float box[6];
    std::memcpy(box,bodyById[id]+kBodyBox,sizeof(box));
    return SegmentBoxInf(a.from,a.to,box,box+3)<=MaxAbs(a.extent);
}
// Hooked = stock's ids in order less only unreachable ones (the kept-one's extra aside); empty exactly when stock's is.
unsigned long long cutParts=0,cutTaken=0;
bool Subset(const Collector& s,const Collector& h,const CastArgs& a,const char* what,unsigned round) {
    const auto whole=s.Ids(),cut=h.Ids();
    std::size_t j=0;
    bool onlyUnreachable=true;
    for(const unsigned id:whole) {
        if(j<cut.size() && cut[j]==id){++j;continue;}
        if(Reachable(id,a))onlyUnreachable=false;
    }
    cutParts+=whole.size();cutTaken+=whole.size()-cut.size();
    if(j==cut.size() && onlyUnreachable && whole.empty()==cut.empty())return true;
    ++failures;
    std::printf("FAIL %s round %u: stock %zu ids, hooked %zu: subsequence %d, only unreachable out %d\n",what,round,whole.size(),
                cut.size(),j==cut.size(),onlyUnreachable);
    return false;
}
std::vector<unsigned> Overlaps(const Fake& f,Rng& rng,unsigned most) {
    std::vector<unsigned> ids;
    const unsigned n=rng.N(most+1),size=static_cast<unsigned>(f.bodies.size());
    // Clumps: a few bodies' neighbourhood (the ids near them) again and again, as a sweep through a swarm.
    // Invalid ids only as group parts (stock's addBody fetches the overlapped body itself: a live one).
    for(unsigned i=0;i<n;++i) {
        const unsigned id=1+(rng.N(4)==0 ? rng.N(size) : (rng.N(8)+(i/16)*8)%size);
        if(!invalidId[id])ids.push_back(id);
    }
    return ids;
}
void RandomArgs(Rng& rng,CastArgs& a,float field) {
    float from[3],to[3];
    RandomSegment(rng,from,to,field);
    for(int k=0;k<3;++k){a.from[k]=from[k];a.to[k]=to[k];}
    a.from[3]=a.to[3]=1.0f;
    RandomExtent(rng,a.extent);
    a.extent[3]=0;
    a.nested=nullptr;a.nestedArgs=nullptr;a.nestAt=0;
}

bool RandomWorlds() {
    Rng rng(1234);
    unsigned casts=0,outsideRuns=0,filteredRuns=0,nested=0,off=0,blast=0,cut=0;
    constexpr float kField=40.0f;
    for(unsigned w=0;w<60;++w) {
        Fake f;
        Build(f,rng,1+rng.N(300),rng.N(40),rng.N(3)==0 ? 0 : 1+rng.N(60),rng.N(100),rng.N(101),kField);
        NewStep();
        Collector a(1u<<20),b(1u<<20),na(1u<<20),nb(1u<<20);
        Functor fa{functorVtable,0,0},fb{functorVtable,0,0};
        for(unsigned round=0;round<40;++round) {
            const auto ids=Overlaps(f,rng,200);
            CastArgs args{};
            RandomArgs(rng,args,kField);
            const unsigned kind=rng.N(8);
            testConfig.expandCull=kind!=3;
            if(kind==0) {   // outside a cast
                a.Reset(nullptr);b.Reset(nullptr);
                StockRun(a,ids);
                const auto addBody=reinterpret_cast<void(__fastcall*)(void*,unsigned)>(image+kAddBody);
                for(const unsigned id:ids)addBody(b.c(),id);
                if(!Same(a,b,"outside",round))return false;
                ++outsideRuns;
            } else if(kind==1) {   // a filter functor (in a cast)
                fa.calls=fb.calls=0;
                a.Reset(&fa);b.Reset(&fb);
                StockRun(a,ids);
                args.ids=&ids;
                if(Cast(b,args)!=0x5A){++failures;std::puts("FAIL: the cast's return value");return false;}
                if(!Same(a,b,"functor",round))return false;
                if(fa.calls!=fb.calls){++failures;std::printf("FAIL functor calls round %u: %u / %u\n",round,fa.calls,fb.calls);return false;}
                ++filteredRuns;
            } else if(kind==2 && !ids.empty()) {   // a cast inside the cast: each its own sweep
                const auto inner=Overlaps(f,rng,100);
                CastArgs innerArgs{};
                RandomArgs(rng,innerArgs,kField);
                innerArgs.ids=&inner;
                const unsigned at=rng.N(static_cast<unsigned>(ids.size()));
                a.Reset(nullptr);b.Reset(nullptr);na.Reset(nullptr);nb.Reset(nullptr);
                StockRun(a,ids);
                StockRun(na,inner);
                args.ids=&ids;args.nested=&nb;args.nestedArgs=&innerArgs;args.nestAt=at;
                Cast(b,args);
                if(!Subset(a,b,args,"nested outer",round) || !Subset(na,nb,innerArgs,"nested inner",round))return false;
                ++nested;
            } else if(kind==3 || kind==4) {   // ExpandCull=0 / a blast bullet: as stock
                a.Reset(nullptr,kind==4);b.Reset(nullptr,kind==4);
                StockRun(a,ids);
                args.ids=&ids;
                Cast(b,args);
                if(!Same(a,b,kind==3 ? "ExpandCull=0" : "blast",round))return false;
                ++(kind==3 ? off : blast);
            } else {
                a.Reset(nullptr);b.Reset(nullptr);
                StockRun(a,ids);
                args.ids=&ids;
                Cast(b,args);
                if(!Subset(a,b,args,"cast",round))return false;
                ++cut;
            }
            ++casts;
            if(rng.N(2))Mutate(f,rng,kField);
        }
    }
    testConfig.expandCull=true;
    std::printf("random worlds: %u casts compared (%u cut, %u nested, %u outside, %u with a functor, %u ExpandCull=0, %u blast); "
                "cut casts' lists %llu ids, %llu taken out (%.1f%%)\n",casts,cut,nested,outsideRuns,filteredRuns,off,blast,cutParts,cutTaken,
                cutParts ? 100.0*static_cast<double>(cutTaken)/static_cast<double>(cutParts) : 0.0);
    return true;
}

// Keep-one: a group none of whose parts the sweep reaches, overlapped into an empty list, puts its first active part.
bool KeepOne() {
    Rng rng(3);
    Fake f;
    bodyById.assign(4,nullptr);
    invalidId.assign(4,0);
    for(unsigned i=1;i<=3;++i)NewBody(f,rng,i,10.0f);
    for(unsigned i=1;i<=3;++i) {
        Wr<void*>(bodyById[i],kBodyShape,fakeShape);Wr<void*>(bodyById[i],kBodyObject,f.object);invalidId[i]=0;
        const float box[6]={1000.0f+i,1000,1000,1001.0f+i,1001,1001};
        std::memcpy(bodyById[i]+kBodyBox,box,sizeof(box));
    }
    auto* const group=Zeroed(kGroupBytes);
    Wr<void*>(group,kGroupList,Zeroed(kNodeBytes));
    f.groups.push_back(group);f.lists.emplace_back();
    for(unsigned i=0;i<3;++i){auto* n=Zeroed(kNodeBytes);Wr<void*>(n,kNodeBody,bodyById[3-i]);n[kNodeOn]=i!=0;f.lists[0].push_back(n);}
    f.Link(0);
    for(unsigned i=1;i<=3;++i)Wr<void*>(bodyById[i],kBodyGroup,group);
    NewStep();
    Collector a(64),b(64);
    const std::vector<unsigned> ids{1,2,1};
    CastArgs args{};
    args.extent[0]=args.extent[1]=args.extent[2]=0.5f;args.to[0]=10.0f;args.ids=&ids;
    a.Reset(nullptr);b.Reset(nullptr);
    StockRun(a,ids);
    Cast(b,args);
    // Stock: (2,1) three times; the cut: the first active part (id 2) once, the later walks add none (the list non-empty).
    const auto cut=b.Ids();
    return Check(a.Count()==6 && cut.size()==1 && cut[0]==2,"keep-one: the first active part, once");
}

bool Throws() {
    Rng rng(5);
    Fake f;
    Build(f,rng,50,5,20,100,100,20.0f);
    Collector b(1u<<16);
    Functor fb{functorVtable,0,3};
    const std::vector<unsigned> ids{1,2,3,4,5,6,7,8,9,10};
    CastArgs args{};
    args.to[0]=5.0f;args.extent[0]=1.0f;args.ids=&ids;
    const auto addBody=reinterpret_cast<void(__fastcall*)(void*,unsigned)>(image+kAddBody);
    const auto through=[&](auto&& run) { fb.calls=0;b.Reset(&fb);try{run();}catch(const std::runtime_error&){return true;}return false; };
    const bool stockOk=through([&]{StockRun(b,ids);});
    const bool direct=through([&]{for(const unsigned id:ids)addBody(b.c(),id);});
    const bool framed=through([&]{FakeCast(b.c(),world,args.from,nullptr,nullptr);});
    const bool cast=through([&]{Cast(b,args);});
    if(!stockOk || !direct || !framed || !cast || !self || self->cast!=nullptr) {
        ++failures;
        std::printf("FAIL: the functor's exception: stock %d, through the cave %d, with a frame between %d, through the cast %d\n",
                    stockOk,direct,framed,cast);
        return false;
    }
    std::puts("exception: through the cave (and a frame above it) and the cast's scope");
    return true;
}

// `enemies` groups of 40 parts in a field (each enemy's parts within a few metres of it); a cast overlaps 12 enemies
// (10 parts each, as the broadphase would hand them) along a 20 m segment through the first of them. One step: the
// 1024 casts (a new generation each rep). Scattered: the nodes and bodies allocated in a shuffled order among padding.
void Timing(unsigned enemies,bool scattered) {
    constexpr unsigned kParts=40;
    Rng rng(9);
    Fake f;
    std::vector<unsigned char*> pads;
    bodyById.assign(enemies*kParts+1,nullptr);
    invalidId.assign(enemies*kParts+1,0);
    std::vector<unsigned> order(enemies*kParts);
    for(unsigned i=0;i<order.size();++i)order[i]=i;
    if(scattered)std::shuffle(order.begin(),order.end(),rng.g);
    std::vector<float> centre(enemies*3);
    for(auto& c:centre)c=rng.U(-500.0f,500.0f);
    std::vector<unsigned char*> nodes(order.size());
    for(const unsigned i:order) {
        const unsigned id=1+i,e=i/kParts;
        auto* const b=Zeroed(kBodyBytes);
        Wr<unsigned>(b,kBodyId,id);Wr<void*>(b,kBodyWorld,world);Wr<void*>(b,kBodySelf,b);Wr<void*>(b,kBodyObject,f.object);
        Wr<void*>(b,kBodyShape,fakeShape);
        float box[6];
        for(int k=0;k<3;++k){const float c=centre[e*3+k]+rng.U(-3.0f,3.0f),h=rng.U(0.2f,0.8f);box[k]=c-h;box[3+k]=c+h;}
        std::memcpy(b+kBodyBox,box,sizeof(box));
        f.bodies.push_back(b);bodyById[id]=b;
        nodes[i]=Zeroed(kNodeBytes);Wr<void*>(nodes[i],kNodeBody,b);nodes[i][kNodeOn]=1;
        if(scattered)pads.push_back(Zeroed(0x200+rng.N(0x400)));
    }
    for(unsigned e=0;e<enemies;++e) {
        auto* const group=Zeroed(kGroupBytes);
        Wr<void*>(group,kGroupList,Zeroed(kNodeBytes));
        f.groups.push_back(group);f.lists.emplace_back();
        for(unsigned p=0;p<kParts;++p){f.lists[e].push_back(nodes[e*kParts+p]);Wr<void*>(bodyById[1+e*kParts+p],kBodyGroup,group);}
        f.Link(e);
    }
    std::vector<std::vector<unsigned>> sweeps(1024);
    std::vector<CastArgs> args(1024);
    for(unsigned s=0;s<1024;++s) {
        const unsigned first=rng.N(enemies);
        for(unsigned k=0;k<12;++k){const unsigned e=k ? rng.N(enemies) : first;for(unsigned p=0;p<10;++p)sweeps[s].push_back(1+e*kParts+rng.N(kParts));}
        CastArgs& a=args[s];
        for(int k=0;k<3;++k){a.from[k]=centre[first*3+k]-10.0f*(k==0);a.to[k]=centre[first*3+k]+10.0f*(k==0);a.extent[k]=0.3f;}
        a.from[3]=a.to[3]=1.0f;
        a.ids=&sweeps[s];
    }
    Collector a(1u<<16),b(1u<<16);
    const auto addBody=reinterpret_cast<void(__fastcall*)(void*,unsigned)>(image+kAddBody);
    const bool wasCounting=counting;
    counting=false;
    testConfig.expandCull=true;
    const auto time=[&](int how) {
        const auto t0=std::chrono::steady_clock::now();
        for(unsigned rep=0;rep<5;++rep) {
            NewStep();
            for(unsigned s=0;s<1024;++s) {
                if(how==2){b.Reset(nullptr);Cast(b,args[s]);}
                else if(how==1){b.Reset(nullptr);for(const unsigned id:sweeps[s])addBody(b.c(),id);}
                else{a.Reset(nullptr);StockRun(a,sweeps[s]);}
            }
        }
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
    };
    time(0);time(2);   // warm
    const unsigned long long calls0=aabbCalls;
    const double s=time(0),w=time(1),h=time(2);
    const unsigned long long computes=aabbCalls-calls0;
    // The parts the walks went over (stock's list lengths) and the cut's kept.
    unsigned long long parts=0,kept=0;
    for(unsigned i=0;i<1024;++i){a.Reset(nullptr);StockRun(a,sweeps[i]);parts+=a.Count();b.Reset(nullptr);Cast(b,args[i]);kept+=b.Count();}
    parts*=5;kept*=5;
    counting=wasCounting;
    const double perPart=parts ? (h-w)*1e6/static_cast<double>(parts) : 0.0;
    std::printf("timing %s, %u enemies: %u casts, %llu parts walked, %.1f%% culled, %llu box computes; stock %.2f ms, as stock "
                "outside a cast %.2f ms, cut %.2f ms: %+.2f ns a part over the walk\n",scattered ? "scattered" : "packed",enemies,5*1024,
                parts,parts ? 100.0*static_cast<double>(parts-kept)/static_cast<double>(parts) : 0.0,computes,s,w,h,perPart);
    for(auto* p:pads)std::free(p);
}

// The cache alone: a part's box (a hit) and the test, over ids in a group's order.
void TimingJudge(unsigned enemies) {
    const unsigned kIds=enemies*40;
    Rng rng(11);
    std::vector<unsigned char*> bodies(kIds+1,nullptr);
    bodyById.assign(kIds+1,nullptr);invalidId.assign(kIds+1,0);
    for(unsigned id=1;id<=kIds;++id) {
        auto* const b=Zeroed(kBodyBytes);
        Wr<void*>(b,kBodyShape,fakeShape);
        float box[6];
        for(int k=0;k<3;++k){const float c=rng.U(-500.0f,500.0f);box[k]=c-0.5f;box[3+k]=c+0.5f;}
        std::memcpy(b+kBodyBox,box,sizeof(box));
        bodyById[id]=bodies[id]=b;
    }
    Thread* const t=Self();
    Bodies bs{};
    if(!t || !BodiesOf(world,bs)){++failures;std::puts("FAIL: the judge's thread / reader");return;}
    Sweep sw{};
    const float from[3]{0,0,0},to[3]{20,0,0},ext[3]{0.3f,0.3f,0.3f};
    sw.Set(from,to,ext);
    // Groups' walks (an enemy's 40 parts, ids together, enemies at random) and, the worst, ids at random.
    std::vector<unsigned> groups(1u<<20),scatter(1u<<20);
    for(std::size_t k=0;k<groups.size();k+=40){const unsigned e=rng.N(enemies);for(unsigned p=0;p<40;++p)groups[k+p]=1+e*40+p;}
    for(auto& id:scatter)id=1+rng.N(kIds);
    const bool wasCounting=counting;
    counting=false;
    unsigned long long touched=0;
    const auto run=[&](const std::vector<unsigned>& ids) {
        const unsigned gen=NewStep();
        for(const unsigned id:ids){const BoxEntry& e=BoxOf(*t,bs,id,gen);touched+=sw.Touches(e.lo,e.hi);}   // fill
        const auto t0=std::chrono::steady_clock::now();
        for(unsigned rep=0;rep<8;++rep)
            for(const unsigned id:ids){const BoxEntry& e=BoxOf(*t,bs,id,gen);touched+=sw.Touches(e.lo,e.hi);}
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count()*1e6/(8.0*static_cast<double>(ids.size()));
    };
    const double ng=run(groups),ns=run(scatter);
    counting=wasCounting;
    std::printf("judge (the cached box and the test, %u parts' boxes): %.2f ns a part in groups' order, %.2f ns at random (%llu touched)\n",
                kIds,ng,ns,touched);
    t->stats=Stats{};
    for(unsigned id=1;id<=kIds;++id)std::free(bodies[id]);
    bodyById.assign(1,nullptr);invalidId.assign(1,0);
}

// ---- the settle's generation (0x23426F chained over damagestats' redirect) ----
// Two parts of one group: A on the segment (0..10 on x), B far; a cast overlapping A puts A (and B as stock).
struct Pair {
    Fake f;
    void Place(unsigned id,float x) { const float box[6]={x,-0.5f,-0.5f,x+1.0f,0.5f,0.5f};std::memcpy(bodyById[id]+kBodyBox,box,sizeof(box)); }
    Pair() {
        Rng rng(17);
        bodyById.assign(3,nullptr);invalidId.assign(3,0);
        for(unsigned i=1;i<=2;++i) {
            NewBody(f,rng,i,10.0f);
            Wr<void*>(bodyById[i],kBodyShape,fakeShape);Wr<void*>(bodyById[i],kBodyObject,f.object);invalidId[i]=0;
        }
        Place(1,2.0f);Place(2,1000.0f);
        auto* const group=Zeroed(kGroupBytes);
        Wr<void*>(group,kGroupList,Zeroed(kNodeBytes));
        f.groups.push_back(group);f.lists.emplace_back();
        for(unsigned i=1;i<=2;++i) {
            auto* const n=Zeroed(kNodeBytes);
            Wr<void*>(n,kNodeBody,bodyById[i]);n[kNodeOn]=1;f.lists[0].push_back(n);Wr<void*>(bodyById[i],kBodyGroup,group);
        }
        f.Link(0);
    }
};
const std::vector<unsigned> pairIds{1};
CastArgs PairArgs() {
    CastArgs a{};
    a.from[3]=a.to[3]=1.0f;a.to[0]=10.0f;a.extent[0]=a.extent[1]=a.extent[2]=0.3f;a.ids=&pairIds;
    return a;
}
std::vector<unsigned> PairCast() { Collector b(64);CastArgs a=PairArgs();b.Reset(nullptr);Cast(b,a);return b.Ids(); }
// The list against stock's on the world as it is now, as Subset judges it: stock's in order less only unreachable
// parts, empty exactly when stock's is (a quiet check: the control expects false).
bool PairStock(const std::vector<unsigned>& cut) {
    Collector a(64);
    const CastArgs args=PairArgs();
    a.Reset(nullptr);StockRun(a,pairIds);
    const auto whole=a.Ids();
    std::size_t j=0;
    for(const unsigned id:whole) {
        if(j<cut.size() && cut[j]==id){++j;continue;}
        if(Reachable(id,args))return false;
    }
    return j==cut.size() && whole.empty()==cut.empty();
}

// Before the settle is chained every cast is left whole (as stock), counted unarmed.
bool Unarmed() {
    Pair p;
    NewStep();
    const unsigned long long u0=totals.unarmed.load();
    const auto ids=PairCast();
    return Check(!armed.load() && ids==std::vector<unsigned>({1,2}) && totals.unarmed.load()==u0+1,
                 "before the settle is chained: the cast left whole, counted unarmed");
}

int settleHookCalls=0;
unsigned char* settleMoves=nullptr;   // the fake damagestats hook's death: this body onto the segment
std::uintptr_t __fastcall FakeSettle(unsigned char* queue,void*,void*,std::uintptr_t r9) {
    ++settleHookCalls;
    if(settleMoves && queue && Rd<std::uint64_t>(queue,kQueueCount)) {
        const float box[6]={5.0f,-0.5f,-0.5f,6.0f,0.5f,0.5f};
        std::memcpy(settleMoves+kBodyBox,box,sizeof(box));
    }
    return r9+1;
}
void* SiteTarget(unsigned rva) noexcept { std::int32_t rel=0;std::memcpy(&rel,image+rva+1,4);return image+rva+5+rel; }

bool SettleChain() {
    // damagestats' redirect first (kFeatures), as the game's load has it; the settle chained over it (kLast).
    bool changed=false;
    if(!Check(edf::RedirectCall(image+kSettleCall,image+kSettle,reinterpret_cast<void*>(&FakeSettle),changed,edf::Claim::kSole,
                                "damagestats.cpp") && changed,"the fake damagestats redirect"))return false;
    void* const theirs=SiteTarget(kSettleCall);
    if(!Check(InstallBulletExpandCullSettle() && armed.load(),"the settle chained, the cut armed"))return false;
    if(!Check(reinterpret_cast<void*>(settleNext)==theirs && SiteTarget(kSettleCall)!=theirs,
              "the settle chained over damagestats' thunk"))return false;
    Pair p;
    alignas(8) unsigned char queue[0x40]{};
    // The control: B moved onto the segment in the step with no settle: the cached box leaves it out (what a settle
    // that moved a body would do without the new generation).
    NewStep();
    bool ok=Check(PairCast()==std::vector<unsigned>({1}),"armed: the far part cut");
    p.Place(2,5.0f);
    ok=Check(PairCast()==std::vector<unsigned>({1}) && !PairStock({1}),"the control: a body moved in the step, no settle: cut on its old box")&&ok;
    p.Place(2,1000.0f);
    NewStep();
    ok=Check(PairCast()==std::vector<unsigned>({1}),"a new step: the far part cut")&&ok;
    // A settle with no entries: through to damagestats' hook, the generation kept.
    const unsigned g0=boxGen.load();
    const unsigned long long n0=settleCount.load(),r0=totals.rechecked.load(),st0=totals.stale.load();
    settleMoves=bodyById[2];
    const int calls0=settleHookCalls;
    ok=Check(Settle(queue,nullptr,nullptr,41)==42 && settleHookCalls==calls0+1 && boxGen.load()==g0 && settleCount.load()==n0 &&
             !self->afterSettle,"a settle with no entries: through the chain, the generation kept")&&ok;
    // With entries: the hook's death moves B onto the segment; a new generation, the next cast fills both boxes again.
    Wr<std::uint64_t>(queue,kQueueCount,1);
    ok=Check(Settle(queue,nullptr,nullptr,7)==8 && settleHookCalls==calls0+2 && boxGen.load()!=g0 && settleCount.load()==n0+1 &&
             self->afterSettle,"a settle with entries: a new generation")&&ok;
    const auto after=PairCast();
    ok=Check(after==std::vector<unsigned>({1,2}) && PairStock(after) && !self->afterSettle,
             "after the settle: the moved part kept (its box filled again)")&&ok;
    ok=Check(totals.rechecked.load()==r0+2 && totals.stale.load()==st0+1,"the stale probe: 2 boxes filled again, 1 changed")&&ok;
    settleMoves=nullptr;
    NewStep();
    std::printf("settle: chained over damagestats' redirect; a settle with entries ends the generation (%d hook calls)\n",settleHookCalls);
    return ok;
}

// The shadow sample's record: the list left whole (stock's), and the cut list mapped back from it (CutList) is the cut
// cast's, on the 0x818 list itself and on a per-id filtered list of it (the 0x8C8 / 0x8D8 lists' 0x1079D0).
bool Records() {
    Rng rng(4242);
    constexpr float kField=40.0f;
    const bool wasShadowing=shadowing;
    shadowing=true;
    unsigned recorded=0,afterSettles=0,differing=0,emptied=0;
    const unsigned long long ss0=totals.settleSampled.load();
    std::vector<unsigned> cutList,mapped,filteredWhole,filteredCut;
    const auto pass=[](unsigned id) noexcept { return ((id*2654435761u)>>7)%3u!=0; };
    for(unsigned w=0;w<30 && !failures;++w) {
        Fake f;
        Build(f,rng,1+rng.N(300),1+rng.N(40),1+rng.N(60),rng.N(100),rng.N(101),kField);
        NewStep();
        Collector a(1u<<20),b(1u<<20),c(1u<<20);
        for(unsigned round=0;round<40;++round) {
            const auto ids=Overlaps(f,rng,200);
            CastArgs args{};
            RandomArgs(rng,args,kField);
            args.ids=&ids;
            Thread* const t=Self();
            const bool settle=rng.N(2)!=0;
            if(settle){t->afterSettle=true;t->settleTick=7;} else t->tick=255;
            a.Reset(nullptr);b.Reset(nullptr);c.Reset(nullptr);
            StockRun(a,ids);
            Cast(b,args);   // the sample: left whole
            Sample& m=t->samples[(t->nextSample+kSamples-1)%kSamples];
            if(!Same(a,b,"sample (left whole)",round))break;
            if(!Check(m.core==b.core.data() && m.afterSettle==settle && m.list==b.Ids() && m.keep.size()<=m.list.size(),
                      "the sample recorded, complete"))break;
            Cast(c,args);   // the cut cast at the same generation
            cutList=c.Ids();
            if(!Check(CutList(m,m.list.data(),static_cast<unsigned>(m.list.size()),mapped) && mapped==cutList,
                      "the record's cut of the list is the cut cast's"))break;
            filteredWhole.clear();filteredCut.clear();
            for(const unsigned id:m.list)if(pass(id))filteredWhole.push_back(id);
            for(const unsigned id:cutList)if(pass(id))filteredCut.push_back(id);
            if(!Check(CutList(m,filteredWhole.data(),static_cast<unsigned>(filteredWhole.size()),mapped) && mapped==filteredCut,
                      "the record's cut of a filtered list is the filtered cut list"))break;
            ++recorded;afterSettles+=settle ? 1u : 0u;differing+=cutList.size()!=m.list.size() ? 1u : 0u;
            emptied+=filteredCut.empty() && !filteredWhole.empty() ? 1u : 0u;
            m.core=nullptr;
            if(rng.N(2))Mutate(f,rng,kField);
        }
    }
    shadowing=wasShadowing;
    std::printf("shadow records: %u samples (%u after a settle, %u whose cut differs, %u filtered lists the cut empties) mapped "
                "back to the cut casts' lists\n",recorded,afterSettles,differing,emptied);
    return Check(recorded==1200 && totals.settleSampled.load()==ss0+afterSettles && differing>100,"the records");
}

bool LoadStockCopy(const char* dll) {
    char dir[MAX_PATH];
    if(!GetTempPathA(MAX_PATH,dir))return false;
    const std::string path=std::string(dir)+"edf6_expand_cull_"+std::to_string(GetCurrentProcessId())+"_EDF.dll";
    if(!CopyFileA(dll,path.c_str(),FALSE))return false;
    stock=reinterpret_cast<unsigned char*>(LoadLibraryExA(path.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES));
    // The mapping holds the file open; deleted when the process ends.
    if(stock)MoveFileExA(path.c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT);
    return stock && stock!=image;
}
}  // namespace

int main(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    Geometry();
    if(argc<2 || !argv[1][0] || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts(failures ? "FAIL: the geometry" : "SKIP: the expansion comparison requires EDF.dll (the geometry passed)");
        return failures ? 1 : 77;
    }
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    if(!image)return 1;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=0x678CCB46)return 1;
    if(!LoadStockCopy(argv[1])){std::puts("FAIL: a second (stock) mapping");return 1;}
    functorVtable[2]=reinterpret_cast<void*>(&FunctorCall);
    if(!SetUpStore(image) || !SetUpStore(stock)){std::puts("FAIL: the reader");return 1;}
    testConfig.expandCull=true;
    testConfig.perfProbe=1;
    testConfig.debug=false;   // the shadow check runs the real narrowphase: in the game only
    if(!InstallBulletExpandCull() || testRefusals){std::puts("FAIL: not installed");return 1;}
    if(!edf::Matches(stock,kWalk,kWalkStock,sizeof(kWalkStock))){std::puts("FAIL: the stock mapping was patched");return 1;}
    if(edf::Matches(image,kWalk,kWalkStock,sizeof(kWalkStock))){std::puts("FAIL: the walk not redirected");return 1;}
    for(const unsigned site:kCastCalls)
        if(CallsTo(site,kCast)){std::printf("FAIL: the cast call %X not redirected\n",site);return 1;}
    if(CallsTo(kStepCall,kStep)){std::puts("FAIL: the step call not redirected");return 1;}
    if(!CallsTo(kBuildCall,kBuild) || !CallsTo(kRayBuildCall,kRayBuild)){std::puts("FAIL: a build call redirected without debug");return 1;}
    if(!CallsTo(kSettleCall,kSettle)){std::puts("FAIL: the settle call redirected before the load's end");return 1;}
    if(!PatchCast()){std::puts("FAIL: the fake cast");return 1;}
    Unarmed();
    if(!SettleChain() || failures){std::printf("FAIL: %d checks\n",failures);return 1;}
    Records();
    RandomWorlds();
    KeepOne();
    Throws();
    if(failures){std::printf("FAIL: %d checks\n",failures);return 1;}
    ReportBulletExpandCull(1);
    Timing(400,false);
    Timing(4000,true);
    TimingJudge(400);
    TimingJudge(4000);
    if(failures){std::printf("FAIL: %d checks\n",failures);return 1;}
    std::puts("bullet expand cull: every list stock's in order less only parts the sweep cannot reach");
    return 0;
}
