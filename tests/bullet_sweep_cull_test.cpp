// The bullet step-3 cut (src/bullet_sweep_cull.cpp) against the stock command build 0x231720, on a private EDF.dll
// mapping (the hook installed into it: its call site 0x2382B6 goes to the cut, 0x231720 itself stock):
//  - the geometry: SegmentTouchesBox (the segment swept by r + kMargin) never refuses a box the sphere of radius r
//    on the segment reaches (the exact distance by a golden-section search, convex in t), random segments (zero
//    length, axis-aligned, 10 km coordinates), boxes and radii;
//  - Cut keeps the order and at least the first id;
//  - the build: random swarms (bodies, their world boxes through a fake shape type's calcAabb, ids in the store),
//    the stock build on the whole list and the hooked build: the hooked command's bodies are the stock command's in
//    the same order, less only bodies the sphere cannot reach (exact distance > r); every other byte of the command
//    the same; the collector reset once each. Left as stock (the same bodies): the blast flag, 0 / 1 bodies,
//    SweepCull=0 (switched between builds).
// The real narrowphase (0x11A8E40: Havok's TLS allocators, the task queue, real hknp shapes) does not run here: the
// debug shadow check compares its hits in the game.
#include "../src/bullet_sweep_cull.cpp"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <random>
#include <vector>

namespace crew {
unsigned char* image=nullptr;
unsigned long long loggedRefusals=0;
void Log(const char* format,...) noexcept {
    char line[512];
    va_list args;
    va_start(args,format);
    vsnprintf(line,sizeof(line),format,args);
    va_end(args);
    if(std::strstr(line,"REFUSED"))++loggedRefusals;
    std::printf("log: %s\n",line);
}
Config testConfig;
const Config& Cfg() noexcept { return testConfig; }
}  // namespace crew

namespace {
using namespace crew;

template<class T> void Wr(void* p,std::size_t off,T v) noexcept { std::memcpy(static_cast<unsigned char*>(p)+off,&v,sizeof(v)); }

int failures=0;
void Check(bool ok,const char* what) {
    if(!ok){++failures;if(failures<=20)std::printf("FAIL: %s\n",what);}
}

// The exact distance from the segment a -> b to the box [lo, hi].
double PointBox(const double p[3],const float lo[3],const float hi[3]) noexcept {
    double s=0;
    for(int k=0;k<3;++k) {
        const double d=p[k]<lo[k] ? lo[k]-p[k] : p[k]>hi[k] ? p[k]-hi[k] : 0.0;
        s+=d*d;
    }
    return std::sqrt(s);
}
double SegmentBox(const float a[3],const float b[3],const float lo[3],const float hi[3]) noexcept {
    const auto at=[&](double t) noexcept {
        double p[3];
        for(int k=0;k<3;++k)p[k]=a[k]+(static_cast<double>(b[k])-a[k])*t;
        return PointBox(p,lo,hi);
    };
    double l=0,h=1;
    const double g=0.6180339887498949;
    for(int i=0;i<200;++i) {
        const double m1=h-(h-l)*g,m2=l+(h-l)*g;
        if(at(m1)<=at(m2))h=m2; else l=m1;
    }
    double best=at((l+h)/2);
    best=(std::min)(best,(std::min)(at(0),at(1)));
    return best;
}

struct Rng {
    std::mt19937 g;
    explicit Rng(unsigned seed) : g(seed) {}
    float U(float lo,float hi) { return std::uniform_real_distribution<float>(lo,hi)(g); }
    unsigned N(unsigned n) { return static_cast<unsigned>(g()%n); }
};

void RandomSegment(Rng& rng,float a[3],float b[3]) {
    const float base=rng.N(4)==0 ? 0.0f : rng.U(-9000.0f,9000.0f);
    const float len=rng.N(8)==0 ? 0.0f : rng.U(0.0f,60.0f);
    float d[3]={rng.U(-1,1),rng.U(-1,1),rng.U(-1,1)};
    if(rng.N(4)==0){const unsigned k=rng.N(3);for(unsigned j=0;j<3;++j)if(j!=k)d[j]=0;}
    double m=std::sqrt(static_cast<double>(d[0])*d[0]+static_cast<double>(d[1])*d[1]+static_cast<double>(d[2])*d[2]);
    if(m==0){d[0]=1;m=1;}
    for(int k=0;k<3;++k){a[k]=base+rng.U(-50,50);b[k]=a[k]+static_cast<float>(d[k]/m)*len;}
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
        float a[3],b[3],lo[3],hi[3];
        RandomSegment(rng,a,b);
        const float r=rng.N(5)==0 ? 0.0f : rng.U(0.0f,4.0f);
        RandomBoxNear(rng,a,b,rng.N(2) ? 3.0f : 30.0f,lo,hi);
        const double d=SegmentBox(a,b,lo,hi);
        const bool touch=SegmentTouchesBox(a,b,lo,hi,static_cast<double>(r)+kMargin);
        if(d<=r+kMargin-1e-6) {
            ++reached;
            if(!touch){Check(false,"a box the sphere reaches refused");std::printf("  d %.6f r %.3f\n",d,r);}
        } else if(touch) ++looseKept;
        else ++refused;
    }
    std::printf("geometry: 400000 cases, %u reached (all kept), %u refused, %u kept loose (the cube's corners)\n",reached,refused,looseKept);
    Check(refused>100000,"the test refuses boxes far off");
}

void CutOrder() {
    unsigned ids[8]={5,6,7,8,9,10,11,12};
    unsigned n=Cut(ids,8,[](unsigned id){ return id%2==0; });
    Check(n==4 && ids[0]==6 && ids[1]==8 && ids[2]==10 && ids[3]==12,"Cut keeps the order");
    unsigned one[3]={3,4,5};
    n=Cut(one,3,[](unsigned){ return false; });
    Check(n==1 && one[0]==3,"Cut keeps the first when it refuses all");
    unsigned all[3]={3,4,5};
    n=Cut(all,3,[](unsigned){ return true; });
    Check(n==3 && all[2]==5,"Cut keeps all");
}

// ---- the fake world on the mapping ----
constexpr unsigned kFakeType=5;
constexpr std::size_t kBodyBytes=0x100,kBodyBox=0xC0;
std::vector<unsigned char*> bodyById;
void* storeVtable[0x20];
struct StoreBlock { unsigned char pad[0x20]; void* vtable; unsigned char rest[0x40]; } storeBlock;
struct alignas(16) SystemBlock { unsigned char bytes[0x100]; } systemBlock;
alignas(16) unsigned char aabbTable[0x100*8];
alignas(16) unsigned char fakeShape[0x40];
unsigned resets=0;
void* collectorVtable[8];
struct CollectorBlock { void* vtable; unsigned char rest[0x80]; } collector;

const void* __fastcall StoreBody(void*,unsigned id) { return id<bodyById.size() ? bodyById[id] : nullptr; }
void __fastcall FakeAabb(const void*,const void* body,float* out) {
    float box[6];
    std::memcpy(box,static_cast<const unsigned char*>(body)+kBodyBox,sizeof(box));
    out[0]=box[0];out[1]=box[1];out[2]=box[2];out[3]=0;out[4]=box[3];out[5]=box[4];out[6]=box[5];out[7]=0;
}
void __fastcall CollectorReset(void*) { ++resets; }

void SetUpWorld() {
    storeVtable[kStoreBodySlot]=reinterpret_cast<void*>(&StoreBody);
    storeBlock.vtable=storeVtable;
    Wr<void*>(systemBlock.bytes+kSystemWorld,kWorldIface,&storeBlock);   // the interface base: +0x20 the store
    Wr<void*>(image,kSystem,&systemBlock);
    fakeShape[kShapeType]=static_cast<unsigned char>(kFakeType);
    Wr<void*>(aabbTable,kFakeType*kAabbStride+kAabbEntry,reinterpret_cast<void*>(&FakeAabb));
    Wr<void*>(image,kAabbTable,aabbTable);
    collectorVtable[2]=reinterpret_cast<void*>(&CollectorReset);
    collector.vtable=collectorVtable;
}

struct Bullet {
    alignas(16) unsigned char core[0xC00]{};
    alignas(16) unsigned char cmd[kCommandBytes]{};
    std::vector<unsigned> ids;
    std::vector<void*> bodies;
    Bullet() : ids(4096), bodies(4096) {
        Wr<void*>(core,kCoreIds,ids.data());
        Wr<void*>(core,kCoreHits,&collector);
        Wr<void*>(cmd,kCommandBodies,bodies.data());
        Wr<unsigned>(cmd,kCommandBodies+0xC,0x10000u);   // capacity: no growth
    }
};

// The build through `fn` on `list`; the command's body pointers.
std::vector<void*> RunBuild(Bullet& u,BuildFn fn,const std::vector<unsigned>& list,std::uint64_t& ret,std::vector<unsigned char>& cmdBytes) {
    std::copy(list.begin(),list.end(),u.ids.begin());
    Wr<unsigned>(u.core,kCoreCount,static_cast<unsigned>(list.size()));
    Wr<unsigned>(u.cmd,kCommandBodies+8,0u);
    std::memset(u.cmd,0xCD,kCommandBodies);
    std::memset(u.cmd+kCommandBodiesEnd,0xCD,kCommandBytes-kCommandBodiesEnd);
    const unsigned before=resets;
    ret=fn(u.core,u.cmd);
    Check(resets==before+1,"the collector reset once");
    cmdBytes.assign(u.cmd,u.cmd+kCommandBytes);
    std::memset(cmdBytes.data()+kCommandBodies,0,kCommandBodiesEnd-kCommandBodies);
    const unsigned n=ret ? Rd<unsigned>(u.cmd,kCommandBodies+8) : 0;
    return {u.bodies.begin(),u.bodies.begin()+n};
}

void Builds(BuildFn hooked) {
    const auto stock=reinterpret_cast<BuildFn>(image+kBuild);
    Rng rng(4000);
    Bullet u;
    unsigned long long cases=0,candidates=0,culled=0,asStock=0;
    for(unsigned c=0;c<3000;++c) {
        float a[3],b[3];
        RandomSegment(rng,a,b);
        const float r=rng.N(5)==0 ? 0.0f : rng.U(0.0f,3.0f);
        float from[4]={a[0],a[1],a[2],1.0f},to[4]={b[0],b[1],b[2],1.0f};
        std::memcpy(u.core+kCoreFrom,from,sizeof(from));
        std::memcpy(u.core+kCoreTo,to,sizeof(to));
        Wr<float>(u.core,0,r);Wr<float>(u.core,kCoreRadius,r);   // the shape's radius current: no rebuild
        const unsigned mode=rng.N(10);
        unsigned flags=rng.N(0x800)&~kFlagBlast;
        if(mode==0)flags|=kFlagBlast;
        Wr<unsigned>(u.core,kCoreFlags,flags);
        testConfig.sweepCull=mode!=1;
        // A swarm: enemies of 1..40 parts around the segment; their ids shuffled into the list.
        bodyById.assign(1,nullptr);
        std::vector<unsigned> list;
        const unsigned n=mode==2 ? 1 : 2+rng.N(300);
        for(unsigned i=0;i<n;++i) {
            auto* const body=static_cast<unsigned char*>(std::calloc(1,kBodyBytes));
            Wr<void*>(body,kBodyShape,fakeShape);
            float lo[3],hi[3];
            RandomBoxNear(rng,a,b,rng.N(3) ? 4.0f : 40.0f,lo,hi);
            const float box[6]={lo[0],lo[1],lo[2],hi[0],hi[1],hi[2]};
            std::memcpy(body+kBodyBox,box,sizeof(box));
            bodyById.push_back(body);
            list.push_back(static_cast<unsigned>(bodyById.size()-1));
        }
        std::shuffle(list.begin(),list.end(),rng.g);
        std::uint64_t rs=0,rh=0;
        std::vector<unsigned char> cs,ch;
        const auto whole=RunBuild(u,stock,list,rs,cs);
        const auto cut=RunBuild(u,hooked,list,rh,ch);
        Check(rs==rh,"the build's return as stock");
        Check(cs==ch,"the command's other bytes as stock");
        // The cut's bodies: the stock ones in order, less unreachable ones only; at least one.
        std::size_t j=0;
        bool ordered=true,onlyUnreachable=true;
        for(void* const body:whole) {
            if(j<cut.size() && cut[j]==body){++j;continue;}
            float box[6];
            std::memcpy(box,static_cast<unsigned char*>(body)+kBodyBox,sizeof(box));
            if(SegmentBox(a,b,box,box+3)<=r)onlyUnreachable=false;
        }
        if(j!=cut.size())ordered=false;
        Check(ordered,"the cut list is the stock list's subsequence");
        Check(onlyUnreachable,"only bodies the sphere cannot reach taken out");
        Check(!rs || !cut.empty(),"at least one body");
        const bool stockPath=mode<=2;
        if(stockPath){Check(cut==whole,"left as stock (blast / off / one body)");++asStock;}
        // Each id's list order back in the core as the build read it (the cut ids), count the cut's.
        Check(Rd<unsigned>(u.core,kCoreCount)==(stockPath ? list.size() : cut.size()),"the core's count is the cut's");
        ++cases;candidates+=whole.size();culled+=whole.size()-cut.size();
        for(std::size_t i=1;i<bodyById.size();++i)std::free(bodyById[i]);
    }
    std::printf("builds: %llu, %llu bodies, %llu taken out (%.1f%%), %llu left as stock\n",cases,candidates,culled,
                candidates ? 100.0*static_cast<double>(culled)/static_cast<double>(candidates) : 0.0,asStock);
}
}  // namespace

int main(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    Geometry();
    CutOrder();
    if(argc<2 || !argv[1][0] || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts(failures ? "FAIL: the geometry" : "SKIP: the build comparison requires EDF.dll (the geometry passed)");
        return failures ? 1 : 77;
    }
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    if(!image)return 1;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=0x678CCB46)return 1;
    testConfig.sweepCull=true;
    testConfig.perfProbe=1;
    testConfig.debug=false;   // the shadow check runs the real narrowphase: in the game only
    if(!InstallBulletSweepCull() || loggedRefusals){std::puts("FAIL: not installed");return 1;}
    if(edf::Matches(image,kBuildCall,kBuildCallStock,sizeof(kBuildCallStock))){std::puts("FAIL: the call not redirected");return 1;}
    const auto* const site=image+kBuildCall;
    const auto hooked=reinterpret_cast<BuildFn>(site+5+Rd<std::int32_t>(site,1));
    SetUpWorld();
    Builds(hooked);
    ReportBulletSweepCull(1);
    if(failures){std::printf("FAIL: %d checks\n",failures);return 1;}
    std::puts("bullet sweep cull: every cut command is the stock one less unreachable bodies");
    return 0;
}
