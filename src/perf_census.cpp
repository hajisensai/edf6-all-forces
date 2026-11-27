// The object census (perf_census.h).
#include "perf_census.h"

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "edf_layout.h"
#include "edf/host.h"
#include "config.h"
#include "plugin_log.h"
#include "stores.h"

namespace crew::perfcensus {
namespace {
// EDF.dll (TimeDateStamp 0x678CCB46).
constexpr std::size_t kTeamManager=0x20B2978,kTeamRows=0x38,kTeamRowStride=0x38,kTeamLock=0x8;
constexpr std::size_t kNodeLeft=0x0,kNodeParent=0x8,kNodeRight=0x10,kNodeIsNil=0x19,kNodeObject=0x20;
constexpr std::size_t kDeadNotDeleted=0x2E8,kObjectFlags=0x380;
constexpr std::uint32_t kNotCounted=0x2000;
constexpr int kTeams=7;   // 0x1B5260: a team over 6 has no row
constexpr std::size_t kSystem=0x20B2958,kSystemLock=0x308,kSystemObjects=0x3D0,kListNodeObject=0x10;
// RTTI: the vtable's complete object locator at -8, its type descriptor (+0xC) an image offset, the name at +0x10.
constexpr std::size_t kColType=0xC,kTypeName=0x10;

constexpr ULONGLONG kSampleMs=500;
constexpr unsigned kWalkMost=1u<<20;   // nodes a walk follows at most (a list changed under it would not end)
constexpr unsigned kTeamBuckets=128,kSceneBuckets=160;
constexpr unsigned kTeamLines=12,kSceneLines=24;   // a period's lines: the most numerous kinds, then "(other)"

struct Bucket {
    int team;                 // -1: a scene class
    const void* vtable;
    std::uint32_t hash;       // the SGO name's (0: none, the class names it)
    char name[48];
    unsigned now,max;
    unsigned long long sum;
};
Bucket teamBuckets[kTeamBuckets],sceneBuckets[kSceneBuckets];
unsigned teamUsed=0,sceneUsed=0;
// objects past a full table (reported as "(table full)" / in the scene's "(other)")
unsigned long long teamOverflowSum=0,sceneOverflowSum=0;
unsigned teamOverflowMax=0,sceneOverflowMax=0;
unsigned samples=0,skipped=0;   // skipped: a lock held by another thread, or the walk faulted
double sampleMs=0.0,sampleWorst=0.0;
ULONGLONG lastSample=0;

// `vt`'s class (".?AV<name>@@" from the locator) into `out` without ".?AV" and "@@", or false.
bool ClassName(const unsigned char* vt,char* out,std::size_t size) noexcept {
    if(vt<image+8 || vt>=image+edf::kImageSize)return false;
    const auto col=*reinterpret_cast<const unsigned char* const*>(vt-8);
    if(col<image || col>=image+edf::kImageSize)return false;
    const std::uint32_t td=*reinterpret_cast<const std::uint32_t*>(col+kColType);
    if(!td || td+kTypeName+4>=edf::kImageSize)return false;
    const char* cls=reinterpret_cast<const char*>(image+td+kTypeName);
    if(std::strncmp(cls,".?AV",4)==0 || std::strncmp(cls,".?AU",4)==0)cls+=4;
    std::size_t i=0;
    for(;cls[i] && !(cls[i]=='@' && cls[i+1]=='@') && i+1<size;++i)out[i]=cls[i];
    out[i]=0;
    return i>0;
}

// An object's SGO name's hash (FNV-1a over its file, upper case, without ".SGO"), the file and its length; 0: none.
std::uint32_t SgoHash(const unsigned char* o,const wchar_t** file,std::size_t* length) noexcept {
    std::size_t n=0;
    const wchar_t* f=WeaponFile(o,&n);   // any scene object's resource key at +0x08 (damagestats.cpp KindName)
    if(!f || !n)return 0;
    if(n>4 && _wcsnicmp(f+n-4,L".SGO",4)==0)n-=4;
    std::uint32_t h=2166136261u;
    for(std::size_t i=0;i<n;++i)h=(h^static_cast<std::uint32_t>(towupper(f[i])))*16777619u;
    *file=f;*length=n;
    return h|1u;
}

Bucket* Find(Bucket* table,unsigned& used,unsigned most,int team,const void* vt,std::uint32_t hash,
             const wchar_t* file,std::size_t length) noexcept {
    for(unsigned i=0;i<used;++i) {
        Bucket& b=table[i];
        if(b.team==team && b.vtable==vt && b.hash==hash)return &b;
    }
    if(used==most)return nullptr;
    Bucket& b=table[used++];
    b=Bucket{};
    b.team=team;b.vtable=vt;b.hash=hash;
    if(hash) {
        std::size_t i=0;
        for(;i<length && i+1<sizeof(b.name);++i) {
            const wchar_t c=file[i];
            b.name[i]=c>32 && c<127 ? static_cast<char>(c) : '_';
        }
        b.name[i]=0;
    } else if(!ClassName(static_cast<const unsigned char*>(vt),b.name,sizeof(b.name))) {
        std::snprintf(b.name,sizeof(b.name),"vtable_%llX",static_cast<unsigned long long>(
            static_cast<const unsigned char*>(vt)-image));
    }
    return &b;
}

// One object of team `team`: its bucket's count this sample.
void CountTeam(int team,const unsigned char* o,Bucket*& last,unsigned& dropped) noexcept {
    const void* vt=At<const void*>(o,0);
    const wchar_t* file=nullptr;std::size_t length=0;
    const std::uint32_t hash=SgoHash(o,&file,&length);
    if(last && last->team==team && last->vtable==vt && last->hash==hash){++last->now;return;}
    Bucket* b=Find(teamBuckets,teamUsed,kTeamBuckets,team,vt,hash,file,length);
    if(!b){++dropped;return;}
    ++b->now;last=b;
}

// Team `team`'s set walked in order, as 0x5E13E0 walks it (its lock held by the caller). False: it faulted.
bool WalkTeam(const unsigned char* manager,int team,unsigned& dropped) noexcept {
    __try {
        const auto rows=At<const unsigned char*>(manager,kTeamRows);
        if(!rows)return true;
        const auto head=At<const unsigned char*>(rows,static_cast<std::size_t>(team)*kTeamRowStride);
        if(!head)return true;
        Bucket* last=nullptr;
        const unsigned char* n=At<const unsigned char*>(head,kNodeLeft);
        for(unsigned k=0;n && n!=head && k<kWalkMost;++k) {
            const auto o=At<const unsigned char*>(n,kNodeObject);
            if(o && !(At<std::uint32_t>(o,kObjectFlags)&kNotCounted) && !At<std::uint8_t>(o,kDeadNotDeleted))
                CountTeam(team,o,last,dropped);
            // the in-order successor (0x5E1464..0x5E14AD)
            const auto right=At<const unsigned char*>(n,kNodeRight);
            if(!At<std::uint8_t>(right,kNodeIsNil)) {
                n=right;
                for(auto l=At<const unsigned char*>(n,kNodeLeft);!At<std::uint8_t>(l,kNodeIsNil);l=At<const unsigned char*>(n,kNodeLeft))n=l;
            } else {
                auto p=At<const unsigned char*>(n,kNodeParent);
                while(!At<std::uint8_t>(p,kNodeIsNil) && n==At<const unsigned char*>(p,kNodeRight)){n=p;p=At<const unsigned char*>(p,kNodeParent);}
                n=p;
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// The System's scene list walked (its lock held by the caller), each object by class. False: it faulted.
bool WalkScene(const unsigned char* system,unsigned& dropped) noexcept {
    __try {
        const auto head=At<const unsigned char*>(system,kSystemObjects);
        if(!head)return true;
        Bucket* last=nullptr;
        const unsigned char* n=At<const unsigned char*>(head,0);
        for(unsigned k=0;n && n!=head && k<kWalkMost;++k,n=At<const unsigned char*>(n,0)) {
            const auto o=At<const unsigned char*>(n,kListNodeObject);
            if(!o)continue;
            const void* vt=At<const void*>(o,0);
            if(last && last->vtable==vt){++last->now;continue;}
            Bucket* b=Find(sceneBuckets,sceneUsed,kSceneBuckets,-1,vt,0,nullptr,0);
            if(!b){++dropped;continue;}
            ++b->now;last=b;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void Fold(Bucket* table,unsigned used,unsigned dropped,unsigned long long& overflowSum,unsigned& overflowMax) noexcept {
    for(unsigned i=0;i<used;++i) {
        Bucket& b=table[i];
        b.sum+=b.now;
        if(b.now>b.max)b.max=b.now;
        b.now=0;
    }
    overflowSum+=dropped;
    if(dropped>overflowMax)overflowMax=dropped;
}

void Sample() noexcept {
    LARGE_INTEGER t0,t1,hz;
    QueryPerformanceCounter(&t0);
    auto* const manager=At<unsigned char*>(image,kTeamManager);
    auto* const system=At<unsigned char*>(image,kSystem);
    if(!manager || !system)return;   // off a mission: no sample
    bool ok=true;
    unsigned td=0,sd=0;
    for(unsigned i=0;i<teamUsed;++i)teamBuckets[i].now=0;
    for(unsigned i=0;i<sceneUsed;++i)sceneBuckets[i].now=0;
    auto* const teamLock=reinterpret_cast<CRITICAL_SECTION*>(manager+kTeamLock);
    if(TryEnterCriticalSection(teamLock)) {
        for(int t=0;t<kTeams && ok;++t)ok=WalkTeam(manager,t,td);
        LeaveCriticalSection(teamLock);
    } else {
        ok=false;
    }
    auto* const sceneLock=reinterpret_cast<CRITICAL_SECTION*>(system+kSystemLock);
    if(ok && TryEnterCriticalSection(sceneLock)) {
        ok=WalkScene(system,sd);
        LeaveCriticalSection(sceneLock);
    } else {
        ok=false;
    }
    if(!ok){++skipped;return;}   // a half sample is not folded in (its counts are dropped at the next one)
    Fold(teamBuckets,teamUsed,td,teamOverflowSum,teamOverflowMax);
    Fold(sceneBuckets,sceneUsed,sd,sceneOverflowSum,sceneOverflowMax);
    ++samples;
    QueryPerformanceCounter(&t1);QueryPerformanceFrequency(&hz);
    const double ms=1000.0*static_cast<double>(t1.QuadPart-t0.QuadPart)/static_cast<double>(hz.QuadPart);
    sampleMs+=ms;
    if(ms>sampleWorst)sampleWorst=ms;
}

// The table's buckets by their sum, most first, into `order`; how many.
unsigned Ranked(const Bucket* table,unsigned used,int team,unsigned* order) noexcept {
    unsigned n=0;
    for(unsigned i=0;i<used;++i)if(table[i].team==team && table[i].sum)order[n++]=i;
    for(unsigned i=1;i<n;++i)for(unsigned j=i;j>0 && table[order[j]].sum>table[order[j-1]].sum;--j) {
        const unsigned s=order[j];order[j]=order[j-1];order[j-1]=s;
    }
    return n;
}
}  // namespace

void Tick() noexcept {
    if(!Cfg().debug || Cfg().perfProbe<1)return;
    const ULONGLONG t=GetTickCount64();
    if(t-lastSample<kSampleMs)return;
    lastSample=t;
    Sample();
}

void Report() noexcept {
    if(!Cfg().debug || Cfg().perfProbe<1)return;
    if(samples || skipped) {
        const double f=samples ? samples : 1.0;
        double teamAll=static_cast<double>(teamOverflowSum),sceneAll=static_cast<double>(sceneOverflowSum);
        unsigned teamKinds=0,sceneKinds=0;
        for(unsigned i=0;i<teamUsed;++i)if(teamBuckets[i].sum){teamAll+=static_cast<double>(teamBuckets[i].sum);++teamKinds;}
        for(unsigned i=0;i<sceneUsed;++i)if(sceneBuckets[i].sum){sceneAll+=static_cast<double>(sceneBuckets[i].sum);++sceneKinds;}
        Log("PERF CENSUS %u samples, %u skipped: %.3f ms a sample (worst %.3f); team objects %.1f in %u kinds, scene "
            "objects %.1f in %u classes",samples,skipped,sampleMs/f,sampleWorst,teamAll/f,teamKinds,sceneAll/f,sceneKinds);
        unsigned order[kSceneBuckets>kTeamBuckets ? kSceneBuckets : kTeamBuckets];
        for(int t=0;t<kTeams;++t) {
            const unsigned n=Ranked(teamBuckets,teamUsed,t,order);
            unsigned long long rest=0;unsigned restMax=0;
            for(unsigned k=0;k<n;++k) {
                const Bucket& b=teamBuckets[order[k]];
                if(k<kTeamLines){Log("PERF CENSUS TEAM %d %s: mean %.1f, max %u",t,b.name,static_cast<double>(b.sum)/f,b.max);continue;}
                rest+=b.sum;restMax+=b.max;
            }
            if(rest)Log("PERF CENSUS TEAM %d %s: mean %.1f, max %u",t,"(other)",static_cast<double>(rest)/f,restMax);
        }
        if(teamOverflowSum)Log("PERF CENSUS TEAM %d %s: mean %.1f, max %u",-1,"(table full)",
                               static_cast<double>(teamOverflowSum)/f,teamOverflowMax);
        const unsigned n=Ranked(sceneBuckets,sceneUsed,-1,order);
        unsigned long long rest=sceneOverflowSum;unsigned restMax=sceneOverflowMax;
        for(unsigned k=0;k<n;++k) {
            const Bucket& b=sceneBuckets[order[k]];
            if(k<kSceneLines){Log("PERF CENSUS SCENE %s: mean %.1f, max %u",b.name,static_cast<double>(b.sum)/f,b.max);continue;}
            rest+=b.sum;restMax+=b.max;
        }
        if(rest)Log("PERF CENSUS SCENE %s: mean %.1f, max %u","(other)",static_cast<double>(rest)/f,restMax);
    }
    // the next period's buckets start empty (a kind gone stays out of its lines)
    teamUsed=sceneUsed=0;
    teamOverflowSum=sceneOverflowSum=0;teamOverflowMax=sceneOverflowMax=0;
    samples=skipped=0;sampleMs=sampleWorst=0.0;
}
}  // namespace crew::perfcensus
