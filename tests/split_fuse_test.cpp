// The split missiles' surface distance (src/split_fuse.h) against a simulated Blood Storm flight: no game.
// The stock rule (0x26CF00, docs/split-missile-re.md): split when |lock point - position| < CP[12][1] and the angle
// off the nose < CP[12][2]. M2+ Blood Storm: 70 m, 0.75 rad, top speed CP[6] 1.5 m a frame, AmmoSize 3 m.
#include "../src/split_fuse.h"
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool condition,const char* description) {
    ++checks;
    if(!condition){std::fprintf(stderr,"FAIL: %s\n",description);std::exit(1);}
}

struct Ball { float c[3]; float r; };
struct World { const Ball* balls; int count; };

// split::Cast over spheres, as the plugin's collector: the nearest entry along a->b into a ball that counts
// (split::Counts: the target's), the rest let through. A ray starting inside a ball does not hit it (a ray meets faces
// it enters).
float CastBalls(void* ctx,const float* a,const float* b,const void* target) {
    const auto* w=static_cast<const World*>(ctx);
    const float len=split::Distance(a,b);
    const float dir[3]={(b[0]-a[0])/len,(b[1]-a[1])/len,(b[2]-a[2])/len};
    float best=-1.0f;
    for(int i=0;i<w->count;++i) {
        const Ball& s=w->balls[i];
        if(!split::Counts(&s,target))continue;
        const float m[3]={a[0]-s.c[0],a[1]-s.c[1],a[2]-s.c[2]};
        const float bq=m[0]*dir[0]+m[1]*dir[1]+m[2]*dir[2],cq=m[0]*m[0]+m[1]*m[1]+m[2]*m[2]-s.r*s.r;
        const float disc=bq*bq-cq;
        if(disc<0.0f)continue;
        const float t=-bq-std::sqrt(disc);
        if(t>=0.0f && t<=len && (best<0.0f || t<best))best=t;
    }
    return best;
}

constexpr float kSplitDistance=70.0f,kSpeed=1.5f,kRoundSize=3.0f;

// Fly straight at the target's centre (the angle is 0) from `start` m off. The frame it splits (its surface distance
// then in `*surfaceAt`), or -1 when it meets the surface first. `fixed`: the plugin's proxy shown to the stock rule.
int Fly(const Ball& target,const Ball* other,float start,bool fixed,float* surfaceAt) {
    Ball balls[2]={target,other ? *other : Ball{{0,0,1e6f},0.1f}};
    World w{balls,2};
    for(int frame=0;frame<2000;++frame) {
        const float pos[3]={target.c[0],target.c[1],target.c[2]-start+kSpeed*static_cast<float>(frame)};
        const float centre=split::Distance(pos,target.c);
        if(centre-target.r<=kRoundSize)return -1;   // met the surface: it blasts there
        float seen[3]={pos[0],pos[1],pos[2]};
        if(fixed) {
            const float surface=split::SurfaceAlong(pos,target.c,&balls[0],&CastBalls,&w);
            split::Proxy(pos,target.c,surface,seen);
        }
        if(split::Distance(seen,target.c)<kSplitDistance){*surfaceAt=centre-target.r;return frame;}
    }
    return -1;
}
}  // namespace

int main() {
    // The DLC egg: wider than the split distance.
    const Ball egg{{0.0f,10.0f,0.0f},100.0f};
    float at=0.0f;
    Check(Fly(egg,nullptr,400.0f,false,&at)<0,"stock: the round meets the egg before it splits (the report)");
    const int f=Fly(egg,nullptr,400.0f,true,&at);
    Check(f>=0,"fixed: the round splits before the egg's surface");
    Check(at<=kSplitDistance && at>kSplitDistance-kSpeed-0.01f,"fixed: it splits at the split distance from the surface");
    std::printf("egg r=100: splits on frame %d, %.2f m from its surface\n",f,at);

    // A small target: its surface a radius short of its centre.
    const Ball ant{{0.0f,0.0f,0.0f},1.5f};
    float stockAt=0.0f;
    const int fs=Fly(ant,nullptr,300.0f,false,&stockAt),ff=Fly(ant,nullptr,300.0f,true,&at);
    Check(fs>=0 && ff>=0 && ff<=fs && fs-ff<=2,"small target: the split moves by at most its radius (2 frames)");
    std::printf("ant r=1.5: stock frame %d, fixed frame %d\n",fs,ff);

    // Another unit in the line of sight does not count: the target's own surface does.
    const Ball between{{0.0f,10.0f,-260.0f},4.0f};
    const float pos[3]={0.0f,10.0f,-400.0f};
    Ball both[2]={egg,between};
    World w{both,2};
    const float s=split::SurfaceAlong(pos,egg.c,&both[0],&CastBalls,&w);
    Check(s>299.0f && s<301.0f,"a unit in the way is let through to the target's surface");
    Check(Fly(egg,&between,400.0f,true,&at)>=0 && at<=kSplitDistance && at>kSplitDistance-kSpeed-0.01f,
          "a unit in the way: still splits at the split distance from the egg");
    Check(!split::Counts(nullptr,&both[0]) && !split::Counts(&both[1],&both[0]) && split::Counts(&both[0],&both[0]),
          "only the target's hits count");
    // A lock point outside its target's bodies (no surface before it): the stock test.
    const Ball off{{0.0f,10.0f,200.0f},50.0f};
    Ball offOnly[1]={off};
    World wo{offOnly,1};
    Check(split::SurfaceAlong(pos,egg.c,&offOnly[0],&CastBalls,&wo)<0.0f,"surface beyond the lock point: none");

    // No surface nearer than the lock point: the stock test sees the round where it is.
    float out[3]={7.0f,7.0f,7.0f};
    Check(!split::Proxy(pos,egg.c,-1.0f,out) && out[0]==7.0f,"no surface: no proxy");
    Check(!split::Proxy(pos,egg.c,500.0f,out),"surface beyond the lock point: no proxy");
    // The proxy keeps the line of sight (the stock angle test unchanged) and its distance is the surface's.
    const float aim[3]={30.0f,-20.0f,50.0f},from[3]={-100.0f,40.0f,-200.0f};
    Check(split::Proxy(from,aim,60.0f,out),"proxy made");
    const float d0=split::Distance(from,aim),d1=split::Distance(out,aim);
    Check(std::fabs(d1-60.0f)<1e-3f,"proxy at the surface distance");
    bool along=true;
    for(int i=0;i<3;++i)along=along && std::fabs((aim[i]-out[i])/d1-(aim[i]-from[i])/d0)<1e-5f;
    Check(along,"proxy on the same line of sight");

    std::printf("split_fuse_test: %d checks passed\n",checks);
    return 0;
}
