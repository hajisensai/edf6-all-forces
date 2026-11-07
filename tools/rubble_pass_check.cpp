// The shared rubble pass (src/rubble_pass.h) offline: one walk marks exactly the materials the per-chassis scans it
// replaced (physics.cpp MarkRubble until 2026-11-07, copied below as the reference) would have marked from the same
// chassis positions, and every chassis that keeps stepping is in a pass once per kScanMs however many step.
//   cmake --build build --target rubble_pass_check && build\rubble_pass_check.exe      (exit code 1 on a failure)
#include "../src/rubble_pass.h"
#include <cstdio>
#include <random>
#include <vector>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,long long a=0,long long b=0) {
    ++cases;
    if(!ok){++failures;std::printf("FAIL: %s (%lld, %lld)\n",what,a,b);}
}
using namespace rubble;

// The per-chassis scan as it was (physics.cpp MarkRubble before the shared pass), on the same layout.
void ReferenceScan(const float* at,std::uint16_t own,const unsigned char* bodies,std::int32_t count,
                   unsigned char* materials,std::int32_t materialCount,int& marks) {
    for(std::int32_t i=0;i<count && i<0x40000;++i) {
        const unsigned char* b=bodies+std::size_t(i)*kBodyStride;
        if((Get<std::uint32_t>(b,0x50)&0xFFFFFF)!=std::uint32_t(i))continue;
        if((Get<std::uint32_t>(b,0x54)&7)!=2)continue;
        if(Get<std::uint8_t>(b,0x89)!=1 || Get<std::uint16_t>(b,0x8A)==own)continue;
        const float p[3]={Get<float>(b,0x30),Get<float>(b,0x34),Get<float>(b,0x38)};
        const float dx=p[0]-at[0],dy=p[1]-at[1],dz=p[2]-at[2];
        if(dx*dx+dy*dy+dz*dz>15.0f*15.0f)continue;
        const std::uint16_t id=Get<std::uint16_t>(b,0x8A);
        unsigned char* const material=id<materialCount ? materials+std::size_t{id}*0x50 : nullptr;
        if(!material || Get<std::uint8_t>(material,0x30)!=0)continue;
        ++marks;
        Set<std::uint32_t>(material,0x14,Get<std::uint32_t>(material,0x14)|0x800000);
        Set<std::uint8_t>(material,0x30,1);
    }
}

struct World {
    std::vector<unsigned char> bodies,materials;
    std::int32_t count,materialCount;
};

// A random world around the origin: `count` bodies, some not live (id not its index), not dynamic, not debris quality;
// `materialCount` materials of category 0 (most), 1 or 2; positions within +-60 m.
World Make(std::mt19937& rng,std::int32_t count,std::int32_t materialCount) {
    World w{std::vector<unsigned char>(std::size_t(count)*kBodyStride),
            std::vector<unsigned char>(std::size_t(materialCount)*kMaterialStride),count,materialCount};
    std::uniform_real_distribution<float> pos(-60.0f,60.0f);
    std::uniform_int_distribution<int> pct(0,99);
    for(std::int32_t i=0;i<count;++i) {
        unsigned char* b=w.bodies.data()+std::size_t(i)*kBodyStride;
        Set<std::uint32_t>(b,kBodyId,(pct(rng)<5 ? std::uint32_t(i+1) : std::uint32_t(i))|0x3000000);
        const int motion=pct(rng);
        Set<std::uint32_t>(b,kBodyFlags,(motion<10 ? 1u : motion<15 ? 4u : 2u)|(pct(rng)<50 ? 0x800000u : 0u));
        Set<std::uint8_t>(b,kBodyQuality,pct(rng)<70 ? 1 : 3);
        // a few ids past the table (no material): skipped by both
        Set<std::uint16_t>(b,kBodyMaterial,static_cast<std::uint16_t>(pct(rng)<3 ? materialCount+1 : rng()%materialCount));
        Set<float>(b,kBodyPos,pos(rng));Set<float>(b,kBodyPos+4,pos(rng)/4);Set<float>(b,kBodyPos+8,pos(rng));
    }
    for(std::int32_t m=0;m<materialCount;++m) {
        unsigned char* mat=w.materials.data()+std::size_t(m)*kMaterialStride;
        const int c=pct(rng);
        Set<std::uint8_t>(mat,kMatCategory,static_cast<std::uint8_t>(c<80 ? 0 : c<90 ? 1 : 2));
        Set<std::uint32_t>(mat,kMatFlags,static_cast<std::uint32_t>(rng()&0xFF));
    }
    return w;
}

Window window{};   // 40 KB: static storage, as in physics.cpp
}  // namespace

int main() {
    // 1. The union: random worlds and 1..40 chassis (their own materials drawn from the debris' too, so a body is
    // spared by its own chassis and marked by another's), the one pass against each chassis' scan in turn.
    {
        std::mt19937 rng(1107);
        int bad=0,marksSeen=0,sparedByOwn=0,runs=0;
        for(int run=0;run<400;++run) {
            const std::int32_t count=1+static_cast<std::int32_t>(rng()%3000),materials=1+static_cast<std::int32_t>(rng()%40);
            World ref=Make(rng,count,materials);
            World got=ref;
            const int chassis=1+static_cast<int>(rng()%40);
            std::uniform_real_distribution<float> pos(-60.0f,60.0f);
            std::vector<float> at(std::size_t(chassis)*3);
            std::vector<std::uint16_t> owned(static_cast<std::size_t>(chassis));
            std::vector<unsigned char> wrappers(std::size_t(chassis)*0x40);
            for(int k=0;k<chassis;++k) {
                at[k*3]=pos(rng);at[k*3+1]=pos(rng)/4;at[k*3+2]=pos(rng);
                owned[k]=static_cast<std::uint16_t>(rng()%materials);
            }
            int refMarks=0;
            for(int k=0;k<chassis;++k)ReferenceScan(&at[k*3],owned[k],ref.bodies.data(),count,ref.materials.data(),materials,refMarks);
            // Did some chassis' owned test spare a body another chassis' test marked? (the case the per-chassis `owned` covers)
            {
                for(std::int32_t b=0;b<count;++b) {
                    const unsigned char* body=got.bodies.data()+std::size_t(b)*kBodyStride;
                    const std::uint16_t id=Get<std::uint16_t>(body,kBodyMaterial);
                    for(int k=0;k<chassis;++k)if(id==owned[k]){++sparedByOwn;break;}
                }
            }
            Clear(window);window.world=nullptr;window.passed=false;
            const unsigned char iface=0;
            for(int k=0;k<chassis;++k)
                Check(Note(window,&iface,wrappers.data()+k*0x40,&at[k*3],owned[k],5000),"noted");
            int gotMarks=0;
            Pass(window,got.bodies.data(),count,got.materials.data(),materials,5000,
                 [&](const unsigned char*,const unsigned char*){++gotMarks;});
            bad+=got.materials!=ref.materials || got.bodies!=ref.bodies || gotMarks!=refMarks;
            marksSeen+=refMarks;++runs;
        }
        std::printf("  union: %d runs, %d materials marked, %d bodies sharing a chassis' own material\n",runs,marksSeen,
                    sparedByOwn);
        Check(bad==0,"one pass marks what the per-chassis scans marked, byte for byte",bad);
        Check(marksSeen>100 && sparedByOwn>100,"the random worlds mark materials and exercise the own-material rule",
              marksSeen,sparedByOwn);
    }
    // 2. A body sharing chassis A's own material, within reach of A alone: unmarked; within reach of B too: marked.
    {
        World w{std::vector<unsigned char>(kBodyStride),std::vector<unsigned char>(2*kMaterialStride),1,2};
        Set<std::uint32_t>(w.bodies.data(),kBodyId,0);Set<std::uint32_t>(w.bodies.data(),kBodyFlags,2);
        Set<std::uint8_t>(w.bodies.data(),kBodyQuality,1);Set<std::uint16_t>(w.bodies.data(),kBodyMaterial,1);
        const float a[3]={5,0,0},b[3]={0,0,10};
        const unsigned char iface=0,wa=0,wb=0;
        Clear(window);window.world=nullptr;
        Note(window,&iface,&wa,a,1,100);
        Pass(window,w.bodies.data(),1,w.materials.data(),2,100,[](const unsigned char*,const unsigned char*){});
        Check(Get<std::uint8_t>(w.materials.data()+kMaterialStride,kMatCategory)==0,"its own chassis spares it");
        Note(window,&iface,&wa,a,1,400);Note(window,&iface,&wb,b,0,400);
        Pass(window,w.bodies.data(),1,w.materials.data(),2,400,[](const unsigned char*,const unsigned char*){});
        Check(Get<std::uint8_t>(w.materials.data()+kMaterialStride,kMatCategory)==1 &&
              Get<std::uint32_t>(w.materials.data()+kMaterialStride,kMatFlags)==0x800000,"another chassis marks it");
        // Stale: a chassis last seen kScanMs ago does not count.
        Set<std::uint8_t>(w.materials.data()+kMaterialStride,kMatCategory,0);
        Note(window,&iface,&wb,b,0,1000);
        Pass(window,w.bodies.data(),1,w.materials.data(),2,1000+kScanMs,[](const unsigned char*,const unsigned char*){});
        Check(Get<std::uint8_t>(w.materials.data()+kMaterialStride,kMatCategory)==0,"a chassis not stepped for kScanMs: not in the pass");
        // Another world drops the noted chassis.
        const unsigned char other=0;
        Note(window,&iface,&wb,b,0,2000);
        Note(window,&other,&wa,a,1,2000);
        Check(window.n==1 && window.c[0].wrapper==&wa,"another world: the chassis of the old one dropped",window.n);
        Check(Due(window,2000),"another world: the first pass due at once");
    }
    // 3. The throttle: 1..1000 chassis each stepped every 16 ms frame for 10 s, a pass whenever Due: about one pass per
    // kScanMs in all (the per-chassis scans walked the world once per chassis per kScanMs), each pass holding every
    // chassis.
    for(int n:{1,8,20,100,1000}) {
        Clear(window);window.world=nullptr;window.passed=false;
        const unsigned char iface=0;
        std::vector<unsigned char> objs(std::size_t(n)*0x40);
        const float at[3]{};
        int passes=0,short_=0;
        for(std::uint64_t t=3600000;t<3600000+10000;t+=16)
            for(int i=0;i<n;++i) {
                if(!Note(window,&iface,objs.data()+std::size_t(i)*0x40,at,0,t)){++short_;continue;}
                if(Due(window,t)) {
                    // The pass after the first holds every chassis (all stepped since the last one).
                    if(passes>0 && window.n!=n)++short_;
                    ++passes;
                    Pass(window,nullptr,0,nullptr,0,t,[](const unsigned char*,const unsigned char*){});
                }
            }
        Check(passes>=38 && passes<=41 && short_==0,"one pass per kScanMs with every chassis in it",passes,short_);
    }
    // 4. Over kMost chassis between two passes: the window passes early when full (the caller's Pass and Note again);
    // no chassis is left out of every pass.
    {
        Clear(window);window.world=nullptr;window.passed=false;
        const unsigned char iface=0;
        constexpr int n=1500;
        std::vector<unsigned char> objs(std::size_t(n)*0x40);
        std::vector<int> in(n,0);
        const float at[3]{};
        int passes=0;
        auto pass=[&](std::uint64_t t) {
            for(int i=0;i<window.n;++i)
                ++in[static_cast<std::size_t>((static_cast<const unsigned char*>(window.c[i].wrapper)-objs.data())/0x40)];
            ++passes;
            Pass(window,nullptr,0,nullptr,0,t,[](const unsigned char*,const unsigned char*){});
        };
        for(std::uint64_t t=3600000;t<3600000+2000;t+=16)
            for(int i=0;i<n;++i) {
                const void* w=objs.data()+std::size_t(i)*0x40;
                if(!Note(window,&iface,w,at,0,t)){pass(t);Note(window,&iface,w,at,0,t);}
                if(Due(window,t))pass(t);
            }
        int never=0;
        for(int c:in)never+=c==0;
        std::printf("  over kMost: %d passes in 2 s for %d chassis\n",passes,n);
        Check(never==0,"over kMost chassis: every one in some pass",never);
    }
    std::printf("rubble_pass_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
