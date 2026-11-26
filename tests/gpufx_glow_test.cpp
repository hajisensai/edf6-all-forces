// Offline check of the rounds' halos (src/gpufx/gpufx_glow.h): which classes and list lengths can carry a declaration,
// the declaration's parse (mark, ranges, colour normalised times the intensity), the per-variant cache (hit, recheck,
// replacement), the per-frame cap's stable share and its brightness compensation, and the card a round gets.
#include "gpufx/gpufx_glow.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace crew::gpufx;

static int failures=0;
#define CHECK(c) do { if(!(c)) { std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } } while(0)
static bool Near(float a,float b,float e=1e-4f) { return std::fabs(a-b)<=e; }

static void Classes() {
    CHECK(glow::FirstFree(0x17A1C10u)==13);   // MissileBullet01
    CHECK(glow::FirstFree(0x17A1E28u)==16);   // MissileBullet02
    CHECK(glow::FirstFree(0x17A3F78u)==-1);   // SolidBullet01: not looked at
    // A stock list (MB01's 12 or 13 entries, MB02's 16) never carries one; one entry past the last read does.
    CHECK(!glow::CanDeclare(12,13) && !glow::CanDeclare(13,13) && glow::CanDeclare(14,13));
    CHECK(!glow::CanDeclare(16,16) && glow::CanDeclare(17,16));
    CHECK(!glow::CanDeclare(40,-1));
}

static void Parse() {
    glow::Glow g{};
    const double mother[]={glow::kMark,1.0,0.95,0.9,6.0,1.6};
    CHECK(glow::Parse(mother,6,&g));
    CHECK(Near(g.color[0],6.0f) && Near(g.color[1],5.7f) && Near(g.color[2],5.4f) && Near(g.size,1.6f));
    // The colour normalised by its largest channel (0.5 0.5 0.25 is the same hue as 1 1 0.5).
    const double half[]={glow::kMark,0.5,0.5,0.25,2.0,1.0};
    CHECK(glow::Parse(half,6,&g) && Near(g.color[0],2.0f) && Near(g.color[2],1.0f));
    // Extra trailing numbers are allowed (room for later fields), up to kMostFields.
    const double more[]={glow::kMark,1.0,1.0,1.0,1.0,1.0,0.0,0.0};
    CHECK(glow::Parse(more,8,&g));
    double v[6];
    auto refused=[&](int at,double value,int n=6) {
        std::memcpy(v,mother,sizeof(v));v[at]=value;glow::Glow x{};
        return !glow::Parse(v,n,&x);
    };
    CHECK(refused(0,4242.0));               // not the mark (the plugin's MISSILE_MARK, for one)
    CHECK(refused(1,NAN) && refused(4,INFINITY));
    CHECK(refused(1,-0.1));                 // a negative channel
    CHECK(refused(1,1.5));                  // a channel past 1 (the HDR scale is the intensity)
    CHECK(refused(4,0.0) && refused(4,glow::kMostIntensity+1.0));
    CHECK(refused(5,0.0) && refused(5,glow::kMostSize+1.0));
    std::memcpy(v,mother,sizeof(v));v[1]=v[2]=v[3]=0.0;
    CHECK(!glow::Parse(v,6,&g));            // black
    CHECK(!glow::Parse(mother,5,&g));       // too short
    CHECK(!glow::Parse(mother,glow::kMostFields+1,&g));
}

static void CacheWorks() {
    static glow::Cache c;   // static: the test's stack stays small
    c.Clear();
    unsigned char a[16]{},b[16]{};a[0]=1;b[0]=2;
    const glow::Glow g{{1.0f,2.0f,3.0f},0.5f};
    CHECK(c.Find(a,100)==nullptr);
    const auto* ea=c.Put(a,100,&g);
    CHECK(ea && ea->glows && Near(ea->glow.color[2],3.0f));
    CHECK(c.Find(a,100+glow::kRecheckMs-1)==ea);
    CHECK(c.Find(a,100+glow::kRecheckMs)==nullptr);   // stale: read again
    const auto* eb=c.Put(b,100,nullptr);
    CHECK(eb && !eb->glows && c.Find(b,150)==eb);
    // A stale key read again takes its own entry back, not a new one.
    const auto* again=c.Put(a,100+glow::kRecheckMs,nullptr);
    CHECK(again==ea && !again->glows);
    // Past the capacity the oldest written goes.
    for(int i=0;i<glow::kCacheSize;++i){unsigned char k[16]{};k[1]=static_cast<unsigned char>(i+1);c.Put(k,200,&g);}
    CHECK(c.Find(a,210)==nullptr && c.Find(b,210)==nullptr);
}

static void Cap() {
    CHECK(Near(glow::KeepShare(10),1.0f) && Near(glow::KeepShare(glow::kMostGlows),1.0f));
    CHECK(Near(glow::KeepShare(glow::kMostGlows*2),0.5f));
    // A salvo in flight: 256 carriers and 1024 children stay under the cap, all drawn.
    CHECK(Near(glow::KeepShare(256+1024),1.0f));
    // The share kept is stable (by id) and close to the share asked.
    int kept=0;
    const int n=8192;
    for(int i=0;i<n;++i)kept+=glow::Kept(glow::IdOf(0x10000000ull+static_cast<std::uintptr_t>(i)*0x1A0u),0.25f);
    CHECK(kept>n/4-n/20 && kept<n/4+n/20);
    CHECK(glow::Kept(12345u,0.25f)==glow::Kept(12345u,0.25f));
    CHECK(glow::Kept(1u,1.0f));
    // Neighbouring cores (nodes a round's size apart) get distinct ids.
    CHECK(glow::IdOf(0x200000000ull)!=glow::IdOf(0x200000010ull));
}

static void Card() {
    const glow::Glow g{{4.0f,3.0f,2.0f},1.2f};
    const float pos[3]{10.0f,20.0f,30.0f},vel[3]{600.0f,0.0f,-60.0f};
    const Instance in=glow::Make(g,pos,vel,1.0f);
    CHECK(Near(in.pos[0],10.0f+600.0f*glow::kLeadSteps/60.0f) && Near(in.pos[1],20.0f) && Near(in.pos[2],30.0f-glow::kLeadSteps));
    CHECK(Near(in.size,1.2f) && Near(in.color[0],4.0f) && Near(in.color[3],1.0f) && Near(in.emissive,1.0f));
    CHECK(Near(in.soft,0.3f));
    const Instance half=glow::Make(g,pos,vel,0.5f);   // kept one in two: twice as bright
    CHECK(Near(half.color[0],8.0f) && Near(half.color[2],4.0f));
    const float bad[3]{NAN,0.0f,0.0f};
    CHECK(Near(glow::Make(g,pos,bad,1.0f).pos[0],10.0f));   // a velocity not finite: no lead
}

int main() {
    Classes();Parse();CacheWorks();Cap();Card();
    if(failures){std::printf("gpufx_glow: %d failures\n",failures);return 1;}
    std::printf("gpufx_glow: ok\n");
    return 0;
}
