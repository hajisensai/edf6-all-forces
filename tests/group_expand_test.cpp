// The group expansion (src/group_expand.cpp) against the stock addBody (0x109DA0), on two private EDF.dll mappings:
// the hooks installed into one, the other left stock; both run on the same fake world (property store, bodies,
// groups and their lists) and fill their own collector, and the lists are compared id for id.
//  - random worlds: bodies with and without a group, lists sharing bodies, nodes off, empty lists, bodies overlapped
//    many times in one cast; inside a bullet cast (the hooked cast call: a fake broadphase over the overlap list),
//    outside any cast, and with a stateful filter functor; casts inside a cast; the world changed between casts
//    (nodes on / off, relinked, bodies moved between groups), every cast compared;
//  - the debug shadow path (every copy checked here) with no mismatches;
//  - an exception out of the filter functor through the cave (its unwind info) and the cast's scope;
//  - timing: a dense swarm (groups of 40 bodies, each overlapped many times a cast).
#include "../src/group_expand.cpp"
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
unsigned long long testMismatchLines=0;
void Log(const char* format,...) noexcept {
    char line[512];
    va_list args;
    va_start(args,format);
    vsnprintf(line,sizeof(line),format,args);
    va_end(args);
    if(std::strstr(line,"MISMATCH"))++testMismatchLines;
    std::printf("log: %s\n",line);
}
Config testConfig;
const Config& Cfg() noexcept { return testConfig; }
}  // namespace crew

namespace {
using namespace crew;

constexpr unsigned kSystemGlobal=0x20B2958;
constexpr std::size_t kBodyWorld=0x100,kBodySelf=0x180,kBodyObject=0x188,kBodyGroup=0x190,kBodyBytes=0x1A0;
constexpr std::size_t kNodeBytes=0xB0,kGroupBytes=0x100,kCollectorBytes=0x100;
constexpr std::size_t kCollectorGroups=0x29;

template<class T> void Wr(void* p,std::size_t off,T v) noexcept { std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof(v)); }

unsigned char* stock=nullptr;   // the second mapping
std::vector<unsigned char*> bodyById;

// The property store: S = [W+0x58]+0x20, its vtable +0x68 (valid) and +0xE8 (id, key, size: the value's address).
void* storeVtable[0x40];
struct { unsigned char pad[0x20]; void* vtable; } storeBlock;   // P, P+0x20 the store
struct { unsigned char system[0x10]; unsigned char world[0x100]; } systemBlock;   // the System, +0x10 the world
unsigned char* const world=systemBlock.world;
int __fastcall StoreValid(void*) { return 1; }
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

bool WriteGlobal(unsigned char* at,unsigned rva,const void* value,std::size_t size) noexcept {
    DWORD old=0;
    if(!VirtualProtect(at+rva,size,PAGE_READWRITE,&old))return false;
    std::memcpy(at+rva,value,size);
    DWORD ignored=0;
    VirtualProtect(at+rva,size,old,&ignored);
    return true;
}
bool SetUpStore(unsigned char* at) noexcept {
    storeVtable[0x68/8]=reinterpret_cast<void*>(&StoreValid);
    storeVtable[0xE8/8]=reinterpret_cast<void*>(&StoreLookup);
    storeBlock.vtable=storeVtable;
    Wr<void*>(world,0x58,&storeBlock);        // [W+0x58]+0x20 = the store
    void* const system=systemBlock.system;    // *(image+0x20B2958)+0x10 = W
    return WriteGlobal(at,kSystemGlobal,&system,sizeof(system));
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

struct Collector {
    unsigned char raw[kCollectorBytes]{};
    std::vector<unsigned> buffer;
    explicit Collector(unsigned capacity) : buffer(capacity) {
        Wr<void*>(raw,kCollectorList,buffer.data());
        Wr<unsigned>(raw,kCollectorCount,0);
        Wr<unsigned>(raw,kCollectorCapacity,capacity);
        raw[kCollectorGroups]=1;
    }
    void Reset(void* functor) noexcept { Wr<unsigned>(raw,kCollectorCount,0);Wr<void*>(raw,kCollectorFilter,functor); }
    unsigned Count() const noexcept { return Rd<unsigned>(raw,kCollectorCount); }
};

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
unsigned char* Zeroed(std::size_t n) { auto* p=static_cast<unsigned char*>(std::calloc(1,n));if(!p)std::abort();return p; }

void Build(Fake& f,std::mt19937& rng,unsigned nBodies,unsigned nGroups,unsigned mostList,unsigned groupedPct,unsigned onPct) {
    bodyById.assign(nBodies+1,nullptr);
    for(unsigned i=1;i<=nBodies;++i) {
        auto* const b=Zeroed(kBodyBytes);
        Wr<unsigned>(b,kBodyId,i);
        Wr<void*>(b,kBodyWorld,world);
        Wr<void*>(b,kBodySelf,b);
        Wr<void*>(b,kBodyObject,(rng()%8) ? static_cast<void*>(f.object) : nullptr);
        f.bodies.push_back(b);bodyById[i]=b;
    }
    for(unsigned g=0;g<nGroups;++g) {
        auto* const group=Zeroed(kGroupBytes);
        auto* const head=Zeroed(kNodeBytes);
        Wr<void*>(group,kGroupList,head);
        f.groups.push_back(group);
        f.lists.emplace_back();
        const unsigned len=mostList ? rng()%(mostList+1) : 0;
        for(unsigned k=0;k<len;++k) {
            auto* const n=Zeroed(kNodeBytes);
            Wr<void*>(n,kNodeBody,f.bodies[rng()%nBodies]);
            n[kNodeOn]=(rng()%100)<onPct ? 1 : 0;
            f.lists[g].push_back(n);
        }
        f.Link(g);
    }
    for(auto* b:f.bodies)Wr<void*>(b,kBodyGroup,(nGroups && (rng()%100)<groupedPct) ? f.groups[rng()%nGroups] : nullptr);
}
void Mutate(Fake& f,std::mt19937& rng) {
    for(unsigned k=0,n=1+rng()%8;k<n && !f.groups.empty();++k) {
        const unsigned g=rng()%f.groups.size();
        auto& list=f.lists[g];
        switch(rng()%4) {
        case 0: if(!list.empty())list[rng()%list.size()][kNodeOn]^=1;break;
        case 1: if(!list.empty())Wr<void*>(list[rng()%list.size()],kNodeBody,f.bodies[rng()%f.bodies.size()]);break;
        case 2: if(list.size()>1){std::swap(list[rng()%list.size()],list[rng()%list.size()]);f.Link(g);}break;
        default: Wr<void*>(f.bodies[rng()%f.bodies.size()],kBodyGroup,f.groups[rng()%f.groups.size()]);break;
        }
    }
}

// The fake broadphase (the hooked mapping's 0x11A70B0): addBody for each overlapped id; `from` the overlap list.
struct Overlap { const std::vector<unsigned>* ids; Collector* nestedCollector; const std::vector<unsigned>* nestedIds; unsigned nestAt; };
std::uintptr_t __fastcall FakeCast(void* collector,void*,void* from,void*,void*) {
    const auto& o=*static_cast<const Overlap*>(from);
    const auto addBody=reinterpret_cast<void(__fastcall*)(void*,unsigned)>(image+kAddBody);
    for(unsigned i=0;i<o.ids->size();++i) {
        if(o.nestedCollector && i==o.nestAt) {
            Overlap inner{o.nestedIds,nullptr,nullptr,0};
            BulletCast(o.nestedCollector->raw,nullptr,&inner,nullptr,nullptr);
        }
        addBody(collector,(*o.ids)[i]);
    }
    return 0x5A;
}
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
    for(const unsigned id:ids)addBody(c.raw,id);
}
bool Same(const Collector& a,const Collector& b,const char* what,unsigned round) {
    if(a.Count()==b.Count() && std::memcmp(a.buffer.data(),b.buffer.data(),a.Count()*sizeof(unsigned))==0)return true;
    std::printf("FAIL %s round %u: stock %u ids, hooked %u\n",what,round,a.Count(),b.Count());
    return false;
}
std::vector<unsigned> Overlaps(const Fake& f,std::mt19937& rng,unsigned most) {
    std::vector<unsigned> ids;
    const unsigned n=rng()%(most+1);
    // Clumps: a few bodies' neighbourhood (the ids near them) again and again, as a sweep through a swarm.
    for(unsigned i=0;i<n;++i)ids.push_back(1+static_cast<unsigned>((rng()%4==0 ? rng()%f.bodies.size() : (rng()%8)+(i/16)*8%f.bodies.size())%f.bodies.size()));
    return ids;
}

bool RandomWorlds() {
    std::mt19937 rng(1234);
    unsigned casts=0,outsideRuns=0,filteredRuns=0,nested=0;
    for(unsigned w=0;w<60;++w) {
        Fake f;
        Build(f,rng,1+rng()%300,rng()%40,rng()%3==0 ? 0 : 1+rng()%60,rng()%100,rng()%101);
        Collector a(1u<<20),b(1u<<20),na(1u<<20),nb(1u<<20);
        Functor fa{functorVtable,0,0},fb{functorVtable,0,0};
        for(unsigned round=0;round<40;++round) {
            const auto ids=Overlaps(f,rng,200);
            const unsigned kind=rng()%6;
            if(kind==0) {   // outside a cast
                a.Reset(nullptr);b.Reset(nullptr);
                StockRun(a,ids);
                const auto addBody=reinterpret_cast<void(__fastcall*)(void*,unsigned)>(image+kAddBody);
                for(const unsigned id:ids)addBody(b.raw,id);
                if(!Same(a,b,"outside",round))return false;
                ++outsideRuns;
            } else if(kind==1) {   // a filter functor (in a cast)
                fa.calls=fb.calls=0;
                a.Reset(&fa);b.Reset(&fb);
                StockRun(a,ids);
                Overlap o{&ids,nullptr,nullptr,0};
                if(BulletCast(b.raw,nullptr,&o,nullptr,nullptr)!=0x5A){std::puts("FAIL: the cast's return value");return false;}
                if(!Same(a,b,"functor",round))return false;
                if(fa.calls!=fb.calls){std::printf("FAIL functor calls round %u: %u / %u\n",round,fa.calls,fb.calls);return false;}
                ++filteredRuns;
            } else if(kind==2 && !ids.empty()) {   // a cast inside the cast
                const auto inner=Overlaps(f,rng,100);
                const unsigned at=rng()%static_cast<unsigned>(ids.size());
                a.Reset(nullptr);b.Reset(nullptr);na.Reset(nullptr);nb.Reset(nullptr);
                StockRun(a,std::vector<unsigned>(ids.begin(),ids.begin()+at));
                StockRun(na,inner);
                StockRun(a,std::vector<unsigned>(ids.begin()+at,ids.end()));
                Overlap o{&ids,&nb,&inner,at};
                BulletCast(b.raw,nullptr,&o,nullptr,nullptr);
                if(!Same(a,b,"nested outer",round) || !Same(na,nb,"nested inner",round))return false;
                ++nested;
            } else {
                a.Reset(nullptr);b.Reset(nullptr);
                StockRun(a,ids);
                Overlap o{&ids,nullptr,nullptr,0};
                BulletCast(b.raw,nullptr,&o,nullptr,nullptr);
                if(!Same(a,b,"cast",round))return false;
            }
            ++casts;
            if(rng()%2)Mutate(f,rng);
        }
    }
    std::printf("random worlds: %u casts compared (%u outside, %u with a functor, %u nested)\n",casts,outsideRuns,filteredRuns,nested);
    return true;
}

bool Shadow() {
    std::mt19937 rng(77);
    Fake f;
    Build(f,rng,200,20,30,90,80);
    Collector a(1u<<20),b(1u<<20);
    shadowing=true;
    const unsigned long long before=testMismatchLines;
    for(unsigned round=0;round<200;++round) {
        const auto ids=Overlaps(f,rng,200);
        a.Reset(nullptr);b.Reset(nullptr);
        StockRun(a,ids);
        Overlap o{&ids,nullptr,nullptr,0};
        BulletCast(b.raw,nullptr,&o,nullptr,nullptr);
        if(!Same(a,b,"shadow",round)){shadowing=false;return false;}
        Mutate(f,rng);
    }
    shadowing=false;
    const unsigned long long shadowed=self->stats.shadowed+totals.shadowed.load();
    if(testMismatchLines!=before || !shadowed){std::printf("FAIL shadow: %llu checked, %llu mismatch lines\n",shadowed,testMismatchLines-before);return false;}
    std::printf("shadow: %llu copies checked, no mismatch\n",shadowed);
    return true;
}

bool Throws() {
    std::mt19937 rng(5);
    Fake f;
    Build(f,rng,50,5,20,100,100);
    Collector b(1u<<16);
    Functor fb{functorVtable,0,3};
    b.Reset(&fb);
    const std::vector<unsigned> ids{1,2,3,4,5,6,7,8,9,10};
    // Stock's own frames, the cave's (an addBody straight from here and from a frame between), the cast's scope.
    const auto addBody=reinterpret_cast<void(__fastcall*)(void*,unsigned)>(image+kAddBody);
    const auto through=[&](auto&& run) { fb.calls=0;b.Reset(&fb);try{run();}catch(const std::runtime_error&){return true;}return false; };
    Overlap o{&ids,nullptr,nullptr,0};
    const bool stockOk=through([&]{StockRun(b,ids);});
    const bool direct=through([&]{for(const unsigned id:ids)addBody(b.raw,id);});
    const bool framed=through([&]{FakeCast(b.raw,nullptr,&o,nullptr,nullptr);});
    const bool cast=through([&]{BulletCast(b.raw,nullptr,&o,nullptr,nullptr);});
    if(!stockOk || !direct || !framed || !cast || !self || self->depth!=0) {
        std::printf("FAIL: the functor's exception: stock %d, through the cave %d, with a frame between %d, through the cast %d\n",
                    stockOk,direct,framed,cast);
        return false;
    }
    std::puts("exception: through the cave (and a frame above it) and the cast's scope");
    return true;
}

// `enemies` groups of 40 bodies (all on); a cast overlaps 12 enemies, 10 bodies of each. Scattered: the nodes and
// bodies allocated in a shuffled order among padding (a swarm's memory, not in the cache).
void Timing(unsigned enemies,bool scattered,unsigned parts=40) {
    std::mt19937 rng(9);
    Fake f;
    std::vector<unsigned char*> pads;
    bodyById.assign(enemies*parts+1,nullptr);
    std::vector<unsigned> order(enemies*parts);
    for(unsigned i=0;i<order.size();++i)order[i]=i;
    if(scattered)std::shuffle(order.begin(),order.end(),rng);
    std::vector<unsigned char*> nodes(order.size());
    for(const unsigned i:order) {
        const unsigned id=1+i;
        auto* const b=Zeroed(kBodyBytes);
        Wr<unsigned>(b,kBodyId,id);Wr<void*>(b,kBodyWorld,world);Wr<void*>(b,kBodySelf,b);Wr<void*>(b,kBodyObject,f.object);
        f.bodies.push_back(b);bodyById[id]=b;
        nodes[i]=Zeroed(kNodeBytes);Wr<void*>(nodes[i],kNodeBody,b);nodes[i][kNodeOn]=1;
        if(scattered)pads.push_back(Zeroed(0x200+rng()%0x400));
    }
    for(unsigned e=0;e<enemies;++e) {
        auto* const group=Zeroed(kGroupBytes);
        Wr<void*>(group,kGroupList,Zeroed(kNodeBytes));
        f.groups.push_back(group);f.lists.emplace_back();
        for(unsigned p=0;p<parts;++p){f.lists[e].push_back(nodes[e*parts+p]);Wr<void*>(bodyById[1+e*parts+p],kBodyGroup,group);}
        f.Link(e);
    }
    std::vector<std::vector<unsigned>> sweeps;
    for(unsigned s=0;s<1024;++s) {
        std::vector<unsigned> ids;
        for(unsigned k=0;k<12;++k){const unsigned e=rng()%enemies;for(unsigned p=0;p<10;++p)ids.push_back(1+e*parts+rng()%parts);}
        sweeps.push_back(std::move(ids));
    }
    Collector a(1u<<16),b(1u<<16);
    const auto addBody=reinterpret_cast<void(__fastcall*)(void*,unsigned)>(image+kAddBody);
    const bool wasCounting=counting;
    counting=false;
    const auto time=[&](int how) {
        const auto t0=std::chrono::steady_clock::now();
        for(unsigned rep=0;rep<5;++rep)
            for(const auto& ids:sweeps) {
                if(how==2){b.Reset(nullptr);Overlap o{&ids,nullptr,nullptr,0};BulletCast(b.raw,nullptr,&o,nullptr,nullptr);}
                else if(how==1){b.Reset(nullptr);for(const unsigned id:ids)addBody(b.raw,id);}
                else{a.Reset(nullptr);StockRun(a,ids);}
            }
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
    };
    time(0);time(2);   // warm
    const double s=time(0),w=time(1),h=time(2);
    counting=wasCounting;
    std::printf("timing %s, %u enemies: %u casts of 120 overlaps (groups of %u): stock %.2f ms, outside a cast %.2f ms, memo %.2f ms (%.1fx)\n",
                scattered ? "scattered" : "packed",enemies,5*1024,parts,s,w,h,h>0 ? s/h : 0.0);
    for(auto* p:pads)std::free(p);
}

bool LoadStockCopy(const char* dll) {
    char dir[MAX_PATH];
    if(!GetTempPathA(MAX_PATH,dir))return false;
    const std::string path=std::string(dir)+"edf6_group_expand_"+std::to_string(GetCurrentProcessId())+"_EDF.dll";
    if(!CopyFileA(dll,path.c_str(),FALSE))return false;
    stock=reinterpret_cast<unsigned char*>(LoadLibraryExA(path.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES));
    // The mapping holds the file open; deleted when the process ends.
    if(stock)MoveFileExA(path.c_str(),nullptr,MOVEFILE_DELAY_UNTIL_REBOOT);
    return stock && stock!=image;
}
}  // namespace

int main(int argc,char** argv) {
    if(argc<2 || !argv[1][0] || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: group expansion comparison requires EDF.dll");
        return 77;
    }
    std::setvbuf(stdout,nullptr,_IONBF,0);
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    if(!image)return 1;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=0x678CCB46)return 1;
    if(!LoadStockCopy(argv[1])){std::puts("FAIL: a second (stock) mapping");return 1;}
    functorVtable[2]=reinterpret_cast<void*>(&FunctorCall);
    if(!SetUpStore(image) || !SetUpStore(stock)){std::puts("FAIL: the property store");return 1;}
    testConfig.groupExpandMemo=1;
    testConfig.perfProbe=1;
    testConfig.debug=false;   // the shadow path on in Shadow() only
    if(!InstallGroupExpand()){std::puts("FAIL: group expansion hooks not installed");return 1;}
    if(!edf::Matches(stock,kWalk,kWalkStock,sizeof(kWalkStock))){std::puts("FAIL: the stock mapping was patched");return 1;}
    if(edf::Matches(image,kWalk,kWalkStock,sizeof(kWalkStock))){std::puts("FAIL: the walk not redirected");return 1;}
    if(!PatchCast()){std::puts("FAIL: the fake cast");return 1;}
    if(!RandomWorlds() || !Shadow() || !Throws()){std::puts("FAIL: the group expansion differs from stock");return 1;}
    ReportGroupExpand(1);
    Timing(400,false);
    Timing(4000,true);
    std::puts("group expansion: every collector list as stock");
    return 0;
}
