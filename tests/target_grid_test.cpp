// The nearest-enemy pick on team grids (src/target_grid.cpp) against the stock walk, on a private EDF.dll mapping:
// the hooks installed into it, a fake TeamManager whose sets the game's own insert (0x5E0B70) and remove (0x5E1C60)
// change (through the hooked calls), fake actors, and the real functor (0x54A3B0) and walk (0x5E0F20).
//  - random worlds: ties (shared positions), weights negative / zero / NaN / inf / light, gone / NoTarget / skipped
//    members, strays and non-finite positions, held targets with divisors 0 / negative / NaN / tiny, relations
//    changed between steps, members in and out (pointers reused) and moving (in their own update) between picks;
//    every pick compared (the pick and the bits of its score) with the stock walk at the same moment;
//  - the slot-5 cave itself (its argument, return value and the `or byte [rdi+18h],8` it stands in for);
//  - timing: 660 pickers, 1500 candidates.
#include "../src/target_grid.cpp"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <random>
#include <vector>

namespace crew {
unsigned char* image=nullptr;
unsigned long long loggedMismatches=0;
void Log(const char* format,...) noexcept {
    char line[512];
    va_list args;
    va_start(args,format);
    vsnprintf(line,sizeof(line),format,args);
    va_end(args);
    if(std::strstr(line,"MISMATCH"))++loggedMismatches;
    if(std::strstr(line,"MISMATCH") || std::strstr(line,"off"))std::printf("log: %s\n",line);
}
Config testConfig;
const Config& Cfg() noexcept { return testConfig; }
}  // namespace crew

namespace {
using namespace crew;
using target_grid::Rd;

constexpr unsigned kFltMax=0x176B03C;
constexpr unsigned kImportMalloc=0x1756300,kImportFree=0x17562D0,kImportEnter=0x1755870,kImportLeave=0x17555D0;
constexpr std::size_t kObjBytes=0x600;
constexpr std::size_t kObjTeam=0x314,kObjSkip=0x39C,kObjWeighs=0x4A0,kObjDivisor=0x538,kObjUpdateBit=0x18;

template<class T> void Wr(void* p,std::size_t off,T v) noexcept { std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof(v)); }

bool SetImport(unsigned slot,void* to) noexcept {
    DWORD old=0;
    if(!VirtualProtect(image+slot,8,PAGE_READWRITE,&old))return false;
    std::memcpy(image+slot,&to,8);
    DWORD ignored=0;
    VirtualProtect(image+slot,8,old,&ignored);
    return true;
}
void* __cdecl TestMalloc(std::size_t n) { return std::malloc(n); }
void __cdecl TestFree(void* p) { std::free(p); }

struct World {
    unsigned char* mgr=nullptr;
    unsigned char* system=nullptr;
    std::vector<std::vector<int>> rel;
    int teams=0;
    std::vector<void*> objects;     // in a set now
    std::vector<void*> holders;     // weak holders (+0x518 object, +0x520 control)
    unsigned char control[16]{};    // uses 2: the weak lock's +1/-1 never frees
};
World world;

using AddFn=void(__fastcall*)(void*,void*);
void Add(void* o) { reinterpret_cast<AddFn>(image+kAdd)(world.mgr,o); }
void Remove(void* o) { reinterpret_cast<AddFn>(image+kRemove)(world.mgr,o); }

void MakeManager(int teams) {
    world.teams=teams;
    world.mgr=static_cast<unsigned char*>(_aligned_malloc(0x100,16));
    std::memset(world.mgr,0,0x100);
    InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(world.mgr+8));
    auto* rows=static_cast<unsigned char*>(std::calloc(static_cast<std::size_t>(teams),0x38));
    world.rel.assign(static_cast<std::size_t>(teams),std::vector<int>(static_cast<std::size_t>(teams),0));
    for(int t=0;t<teams;++t) {
        unsigned char* const row=rows+static_cast<std::size_t>(t)*0x38;
        auto* head=static_cast<unsigned char*>(std::calloc(1,0x28));
        Wr(head,0,head);Wr(head,8,head);Wr(head,0x10,head);head[0x18]=1;head[0x19]=1;
        Wr(row,0,head);Wr<std::uint64_t>(row,8,0);
        Wr(row,0x18,world.rel[static_cast<std::size_t>(t)].data());
        Wr<std::uint64_t>(row,0x28,static_cast<std::uint64_t>(teams));
    }
    Wr(world.mgr,0x38,rows);Wr(world.mgr,0x40,rows+static_cast<std::size_t>(teams)*0x38);Wr<std::uint64_t>(world.mgr,0x48,static_cast<std::uint64_t>(teams));
    std::memcpy(image+kTeams,&world.mgr,8);
    if(!world.system) {
        world.system=static_cast<unsigned char*>(std::calloc(1,0x1000));
        Wr(world.system,kSystemScene,world.system+0x800);
        std::memcpy(image+kSystem,&world.system,8);
    }
    Wr<std::uint32_t>(world.control,8,2);
}
void FreeManager() {
    for(void* o:world.objects){Remove(o);_aligned_free(o);}
    world.objects.clear();
    for(void* h:world.holders)_aligned_free(h);
    world.holders.clear();
    auto* rows=Rd<unsigned char*>(world.mgr,0x38);
    for(int t=0;t<world.teams;++t)std::free(Rd<void*>(rows+static_cast<std::size_t>(t)*0x38,0));
    std::free(rows);
    DeleteCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(world.mgr+8));
    _aligned_free(world.mgr);
    world.mgr=nullptr;
    void* none=nullptr;
    std::memcpy(image+kTeams,&none,8);
}
void NextStep() { Wr(world.system,kSystemStep,Rd<std::uint32_t>(world.system,kSystemStep)+1); }

float Pick(std::mt19937& rng,const std::vector<float>& from) { return from[std::uniform_int_distribution<std::size_t>(0,from.size()-1)(rng)]; }
bool Chance(std::mt19937& rng,double p) { return std::uniform_real_distribution<double>(0.0,1.0)(rng)<p; }

void Place(std::mt19937& rng,void* o,float span) {
    float x,z;
    if(Chance(rng,0.25)){x=static_cast<float>(std::uniform_int_distribution<int>(0,6)(rng))*4.0f;z=static_cast<float>(std::uniform_int_distribution<int>(0,6)(rng))*4.0f;}  // ties
    else{x=std::uniform_real_distribution<float>(-span,span)(rng);z=std::uniform_real_distribution<float>(-span,span)(rng);}
    if(Chance(rng,0.01))x=Pick(rng,{1.0e5f,-3.0e6f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()});
    Wr(o,target_grid::kObjPos,x);Wr(o,target_grid::kObjPos+4,std::uniform_real_distribution<float>(-5.0f,30.0f)(rng));Wr(o,target_grid::kObjPos+8,z);
    Wr(o,target_grid::kObjPos+12,1.0f);
}
void Weigh(std::mt19937& rng,void* o) {
    const float w=Chance(rng,0.85) ? Pick(rng,{1.0f,1.0f,1.0f,0.5f,2.0f,1.25f}) :
        Pick(rng,{0.0f,-1.0f,-0.25f,1e-3f,0.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),1e30f});
    Wr(o,target_grid::kObjWeight,w);
}
void* NewObject(std::mt19937& rng,float span) {
    void* o=_aligned_malloc(kObjBytes,16);
    std::memset(o,0,kObjBytes);
    Wr<int>(o,kObjTeam,std::uniform_int_distribution<int>(0,world.teams-1)(rng));
    Place(rng,o,span);
    Weigh(rng,o);
    if(Chance(rng,0.05))Wr<unsigned char>(o,target_grid::kObjGone,1);
    if(Chance(rng,0.05))Wr<unsigned char>(o,target_grid::kObjFlags,target_grid::kObjNoTarget);
    if(Chance(rng,0.05))Wr<unsigned>(o,kObjSkip,Chance(rng,0.5) ? 2u : 1u);
    Wr<unsigned char>(o,kObjWeighs,Chance(rng,0.3) ? 1 : 0);
    return o;
}
void Hold(std::mt19937& rng,void* self) {
    if(world.objects.empty() || Chance(rng,0.4)){Wr<void*>(self,target_grid::kObjHeld,nullptr);return;}
    while(world.holders.size()<16) {
        auto* h=static_cast<unsigned char*>(_aligned_malloc(0x530,16));
        std::memset(h,0,0x530);
        Wr<void*>(h,0x520,world.control);
        world.holders.push_back(h);
    }
    void* const h=world.holders[std::uniform_int_distribution<std::size_t>(0,world.holders.size()-1)(rng)];
    Wr(h,target_grid::kHeldObject,world.objects[std::uniform_int_distribution<std::size_t>(0,world.objects.size()-1)(rng)]);
    Wr<void*>(self,target_grid::kObjHeld,h);
    Wr(self,kObjDivisor,Pick(rng,{1.0f,2.0f,0.5f,0.0f,-1.0f,1e-30f,3.0f,std::numeric_limits<float>::quiet_NaN()}));
}

// The functor 0x54C4B0 fills for `self`.
void Functor(void* self,unsigned char* f) {
    std::memset(f,0,target_grid::kFnBytes);
    Wr<const void*>(f,0,image+kPickVtable);
    std::memcpy(f+target_grid::kFnPos,static_cast<unsigned char*>(self)+target_grid::kObjPos,16);
    Wr(f,target_grid::kFnBest,Rd<float>(image,kFltMax));
    Wr(f,target_grid::kFnSelf,self);
    Wr<unsigned char>(f,target_grid::kFnWeighs,Rd<unsigned char>(self,kObjWeighs));
}

unsigned long long compared=0,failures=0,onGrid=0,firstFailures=0;
std::mt19937* updateRng=nullptr;
float updateSpan=200.0f;
bool movesInUpdate=true;

// Inside an update: the pick through the grid (PickWalk) and the stock walk, at the same moment; then the real 0x54C4B0
// (its call patched); then the object moves (and may weigh otherwise).
std::uintptr_t __fastcall TestUpdate(void* self,void* arg) {
    std::mt19937& rng=*updateRng;
    const int team=Rd<int>(self,kObjTeam);
    for(int q=0;q<2;++q) {
        alignas(16) unsigned char a[target_grid::kFnBytes],b[target_grid::kFnBytes];
        Functor(self,a);Functor(self,b);
        const auto answered=index.stats.answered;
        PickWalk(world.mgr,team,a);
        onGrid+=index.stats.answered-answered;
        reinterpret_cast<WalkFn>(image+kWalk)(world.mgr,team,b);
        ++compared;
        if(Rd<void*>(a,target_grid::kFnResult)!=Rd<void*>(b,target_grid::kFnResult) ||
           Rd<std::uint32_t>(a,target_grid::kFnBest)!=Rd<std::uint32_t>(b,target_grid::kFnBest)) {
            if(++failures<=10)std::printf("MISMATCH team %d: grid %p %08X stock %p %08X\n",team,Rd<void*>(a,target_grid::kFnResult),
                Rd<std::uint32_t>(a,target_grid::kFnBest),Rd<void*>(b,target_grid::kFnResult),Rd<std::uint32_t>(b,target_grid::kFnBest));
        }
        using PickFn=void*(__fastcall*)(void*);
        void* const real=reinterpret_cast<PickFn>(image+kPick)(self);
        if(real!=Rd<void*>(b,target_grid::kFnResult) && ++failures<=10)std::printf("MISMATCH through 0x54C4B0\n");
    }
    if(movesInUpdate) {
        if(Chance(rng,0.6)) {
            const float step=Chance(rng,0.2) ? 80.0f : 2.0f;
            Wr(self,target_grid::kObjPos,Rd<float>(self,target_grid::kObjPos)+std::uniform_real_distribution<float>(-step,step)(rng));
            Wr(self,target_grid::kObjPos+8,Rd<float>(self,target_grid::kObjPos+8)+std::uniform_real_distribution<float>(-step,step)(rng));
        }
        if(Chance(rng,0.03))Place(rng,self,updateSpan);
        if(Chance(rng,0.05))Weigh(rng,self);
    }
    return reinterpret_cast<std::uintptr_t>(arg)^0x5A5A;
}

// The cave as the System's loop calls it: rdi and rcx the object, rax its vtable, rdx the argument, the stack as at
// the site (the cave entered at 8 mod 16).
using CaveCallFn=std::uintptr_t(__fastcall*)(void* o,void* arg);
CaveCallFn MakeCaveCaller(const unsigned char* cave) {
    unsigned char code[]={0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x8B,0x01,0x48,0xB8,0,0,0,0,0,0,0,0,
                          0x49,0x89,0xC3,0x48,0x8B,0x07,0x41,0xFF,0xD3,0x48,0x83,0xC4,0x20,0x5F,0xC3};
    // push rdi; sub rsp,20h; mov rdi,rcx; mov rax,[rcx]; mov rax,cave; mov r11,rax; mov rax,[rdi]; call r11; add rsp,20h; pop rdi; ret
    std::memcpy(code+13,&cave,8);
    auto* at=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    std::memcpy(at,code,sizeof(code));
    return reinterpret_cast<CaveCallFn>(at);
}

void Update(void* o,void* arg) {
    static CaveCallFn call=nullptr;
    static void* vtable[8]{};
    if(!call) {
        const auto* site=image+kUpdateCalls[0];
        const unsigned char* cave=site+5+Rd<std::int32_t>(site,1);
        call=MakeCaveCaller(cave);
        vtable[5]=reinterpret_cast<void*>(&TestUpdate);
    }
    void* const saved=Rd<void*>(o,0);
    Wr<void*>(o,0,vtable);
    Wr<unsigned char>(o,kObjUpdateBit,0);
    const auto r=call(o,arg);
    Wr<void*>(o,0,saved);
    if(r!=(reinterpret_cast<std::uintptr_t>(arg)^0x5A5A) || !(Rd<unsigned char>(o,kObjUpdateBit)&8)) {
        if(++failures<=10)std::printf("FAIL: the update cave's return value or flag\n");
    }
}

bool RandomWorlds() {
    std::mt19937 rng(20261127u);
    updateRng=&rng;
    for(int world_=0;world_<160;++world_) {
        const int teams=std::uniform_int_distribution<int>(2,7)(rng);
        MakeManager(teams);
        const float span=Pick(rng,{30.0f,200.0f,1500.0f});
        updateSpan=span;
        for(int i=0;i<teams;++i)for(int j=0;j<teams;++j)world.rel[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)]=Chance(rng,0.5) ? 2 : Chance(rng,0.5) ? 1 : 0;
        const int members=std::uniform_int_distribution<int>(0,Chance(rng,0.2) ? 900 : 160)(rng);
        for(int i=0;i<members;++i){void* o=NewObject(rng,span);Add(o);world.objects.push_back(o);}
        for(int step=0;step<6;++step) {
            NextStep();
            if(Chance(rng,0.3))  // relations changed between steps
                for(auto& r:world.rel)for(int& v:r)if(Chance(rng,0.2))v=v==2 ? 0 : 2;
            if(Chance(rng,0.1)) {   // a shorter relation table than the rows
                const auto row=Rd<unsigned char*>(world.mgr,0x38)+static_cast<std::size_t>(std::uniform_int_distribution<int>(0,teams-1)(rng))*0x38;
                Wr<std::uint64_t>(row,0x28,static_cast<std::uint64_t>(std::uniform_int_distribution<int>(0,teams)(rng)));
            }
            const int updates=world.objects.empty() ? 0 : std::uniform_int_distribution<int>(1,60)(rng);
            for(int u=0;u<updates && !world.objects.empty();++u) {
                void* const self=world.objects[std::uniform_int_distribution<std::size_t>(0,world.objects.size()-1)(rng)];
                Hold(rng,self);
                Update(self,reinterpret_cast<void*>(static_cast<std::uintptr_t>(u)));
                // members in and out between picks (a freed one's memory may come back as the next one)
                if(Chance(rng,0.25) && !world.objects.empty()) {
                    const std::size_t k=std::uniform_int_distribution<std::size_t>(0,world.objects.size()-1)(rng);
                    void* const o=world.objects[k];
                    Remove(o);
                    world.objects.erase(world.objects.begin()+static_cast<std::ptrdiff_t>(k));
                    for(void* h:world.holders)if(Rd<void*>(h,target_grid::kHeldObject)==o && Chance(rng,0.5))Wr<void*>(h,target_grid::kHeldObject,nullptr);
                    if(Chance(rng,0.3)) {   // SetTeam: out, another team, in again
                        Wr<int>(o,kObjTeam,std::uniform_int_distribution<int>(0,teams-1)(rng));
                        Add(o);world.objects.push_back(o);
                    } else {
                        // a reused pointer still held is only compared, never read (as the functor does)
                        _aligned_free(o);
                        for(void* h:world.holders)if(Rd<void*>(h,target_grid::kHeldObject)==o)Wr<void*>(h,target_grid::kHeldObject,nullptr);
                    }
                }
                if(Chance(rng,0.25)){void* o=NewObject(rng,span);Add(o);world.objects.push_back(o);}
            }
        }
        FreeManager();
    }
    std::printf("random worlds: %llu picks compared (%llu on the grid), %llu failures; shadow %llu, mismatches %llu (logged %llu)\n",
                compared,onGrid,failures,shadowRuns,mismatches,loggedMismatches);
    std::printf("index: %llu builds, %llu untracked, %llu kept apart; scored %llu+%llu of %llu\n",index.stats.builds,index.stats.untracked,
                index.stats.apartMoves,index.stats.first,index.stats.second,index.stats.members);
    return failures==0 && mismatches==0 && onGrid*2>compared;
}

// 660 pickers (team 0) and 1500 candidates (team 1, hostile), over 1.5 km: a step's picks through the grid (the build
// included) against the stock walks.
bool Timing() {
    std::mt19937 rng(7u);
    MakeManager(2);
    world.rel[0][1]=2;world.rel[1][0]=2;
    std::vector<void*> pickers;
    for(int i=0;i<2160;++i) {
        void* o=_aligned_malloc(kObjBytes,16);
        std::memset(o,0,kObjBytes);
        const int team=i<660 ? 0 : 1;
        Wr<int>(o,kObjTeam,team);
        Wr(o,target_grid::kObjPos,std::uniform_real_distribution<float>(-750.0f,750.0f)(rng));
        Wr(o,target_grid::kObjPos+8,std::uniform_real_distribution<float>(-750.0f,750.0f)(rng));
        Wr(o,target_grid::kObjWeight,1.0f);
        Add(o);world.objects.push_back(o);
        if(team==0)pickers.push_back(o);
    }
    using Clock=std::chrono::steady_clock;
    double stock=0.0,grid=0.0;
    constexpr int kSteps=20;
    movesInUpdate=false;
    testConfig.debug=false;   // no shadow walks in the timing
    void* sink=nullptr;
    for(int step=0;step<kSteps;++step) {
        NextStep();
        auto t0=Clock::now();
        for(void* self:pickers) {
            alignas(16) unsigned char f[target_grid::kFnBytes];
            Functor(self,f);
            reinterpret_cast<WalkFn>(image+kWalk)(world.mgr,0,f);
            sink=Rd<void*>(f,target_grid::kFnResult);
        }
        auto t1=Clock::now();
        ++updateDepth;
        for(void* self:pickers) {
            alignas(16) unsigned char f[target_grid::kFnBytes];
            Functor(self,f);
            PickWalk(world.mgr,0,f);
            sink=Rd<void*>(f,target_grid::kFnResult);
        }
        --updateDepth;
        auto t2=Clock::now();
        stock+=std::chrono::duration<double,std::milli>(t1-t0).count();
        grid+=std::chrono::duration<double,std::milli>(t2-t1).count();
    }
    std::printf("timing (660 x 1500, %d steps): stock %.3f ms/step, grid %.3f ms/step (x%.1f)%s\n",kSteps,stock/kSteps,grid/kSteps,
                stock/(grid>0 ? grid : 1e-9),sink ? "" : " (no pick)");
    FreeManager();
    return true;
}
}  // namespace

int main(int argc,char** argv) {
    if(argc<2 || !argv[1][0] || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: target grid comparison requires EDF.dll");
        return 77;
    }
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    if(!image)return 1;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=0x678CCB46)return 1;
    if(Rd<float>(image,kFltMax)!=(std::numeric_limits<float>::max)()){std::puts("FAIL: the pick's first best is not FLT_MAX");return 1;}
    if(!SetImport(kImportMalloc,reinterpret_cast<void*>(&TestMalloc)) || !SetImport(kImportFree,reinterpret_cast<void*>(&TestFree)) ||
       !SetImport(kImportEnter,reinterpret_cast<void*>(&EnterCriticalSection)) ||
       !SetImport(kImportLeave,reinterpret_cast<void*>(&LeaveCriticalSection))) {
        std::puts("FAIL: imports");return 1;
    }
    if(!InstallTargetGrid()){std::puts("FAIL: target grid hooks not installed");return 1;}
    if(!RandomWorlds()){std::puts("FAIL: target grid picks differ from the stock walk");return 1;}
    Timing();
    std::puts("target grid: every pick as the stock walk");
    return 0;
}
