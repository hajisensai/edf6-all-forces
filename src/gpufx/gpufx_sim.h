// GPU particle effects (src/gpufx): the pure half. Particle simulation with curves, a global budget, back-to-front
// ordering and the procedural smoke flipbook. No Windows, no Direct3D, no game memory: tests/gpufx_sim_test.cpp runs
// all of it offline. The game glue (gpufx.cpp) steps a Pool on the game thread and hands its Instances to the render
// thread (gpufx_gpu.h), which draws them in one instanced call per texture.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::gpufx {

// ---- curves ----
// Piecewise linear over the particle's life fraction t in [0,1]. Keys in increasing t; before the first key the first
// value, after the last the last. Up to kKeys keys (a smoke puff's alpha 0 -> 0.7 -> 0 needs three).
constexpr int kKeys=6;
struct Key { float t=0.0f,v=0.0f; };
struct Curve {
    Key k[kKeys]{};
    int n=0;
    static Curve Constant(float v) noexcept { Curve c;c.k[0]={0.0f,v};c.n=1;return c; }
    Curve& Add(float t,float v) noexcept { if(n<kKeys)k[n++]={t,v};return *this; }
    float Eval(float t) const noexcept {
        if(n<=0)return 0.0f;
        if(!(t>k[0].t))return k[0].v;   // also NaN
        for(int i=1;i<n;++i) {
            if(t<=k[i].t) {
                const float span=k[i].t-k[i-1].t;
                const float f=span>0.0f ? (t-k[i-1].t)/span : 1.0f;
                return k[i-1].v+(k[i].v-k[i-1].v)*f;
            }
        }
        return k[n-1].v;
    }
};

// ---- emitter description ----
// One kind of particle (a layer of a war-thunder-style smoke: the main cloud, the ground dust ring, the flash).
// Distances in metres, times in seconds, angles in radians.
struct Desc {
    float lifeMin=8.0f,lifeMax=12.0f;     // seconds
    float sizeMin=2.0f,sizeMax=3.0f;      // half width at birth
    Curve size=Curve::Constant(1.0f);     // multiplier of the birth size over the life
    Curve alpha=Curve::Constant(1.0f);
    float color0[3]{0.6f,0.6f,0.6f},color1[3]{0.45f,0.45f,0.45f};   // albedo at birth / death (linear, HDR allowed)
    Curve colorMix=Curve::Constant(0.0f);  // 0 = color0, 1 = color1 over the life
    float drag=0.6f;                       // fraction of the velocity lost per second (exponential: v *= (1-drag)^dt)
    float buoyancy=0.3f;                   // m/s^2 up (negative: falls)
    float windFollow=1.0f;                 // how much of the global wind is added to the velocity's target
    float spin=0.4f;                       // the largest angular speed (random sign)
    float frameRate=0.0f;                  // flipbook frames a second; 0: the whole flipbook over the life
    float emissive=0.0f;                   // 0 = lit alpha smoke, 1 = pure additive glow (premultiplied: one draw)
    float soft=1.5f;                       // soft-particle depth fade (metres)
};

// What the render thread draws: 48 bytes, the HLSL StructuredBuffer's element (gpufx_gpu.cpp keeps the same order).
struct Instance {
    float pos[3];
    float size;        // half width (the quad's corner is size*(+-1,+-1) before rotation)
    float color[4];    // linear rgb (HDR allowed), alpha
    float rot;
    float frame;       // flipbook frame, fractional (the two neighbours are blended)
    float soft;        // metres
    float emissive;    // 0..1
};
static_assert(sizeof(Instance)==48,"the HLSL struct is 48 bytes");

struct Particle {
    float pos[3],vel[3];
    float age=0.0f,life=1.0f;
    float size0=1.0f,rot=0.0f,spin=0.0f,frame0=0.0f;
    std::uint16_t desc=0;
    bool live=false;
    std::uint32_t serial=0;   // which spawn (a stable id while it lives: the fill budget's subset)
};

// A tiny deterministic generator (xorshift32): the same seed makes the same smoke on every machine. Effects are
// visual only and made on each machine from its own events (no network message); determinism keeps tests exact.
struct Rng {
    std::uint32_t s=0x9E3779B9u;
    explicit Rng(std::uint32_t seed=0x9E3779B9u) noexcept : s(seed ? seed : 0x9E3779B9u) {}
    std::uint32_t Next() noexcept { s^=s<<13;s^=s>>17;s^=s<<5;return s; }
    float Unit() noexcept { return static_cast<float>(Next()>>8)*(1.0f/16777216.0f); }   // [0,1)
    float Range(float a,float b) noexcept { return a+(b-a)*Unit(); }
};

// ---- the pool ----
// Fixed capacity: the global budget. A spawn into a full pool takes the slot of the particle nearest its end (largest
// age/life), so a new burst is never lost to an old cloud's tail, and the count never grows past the budget.
constexpr int kDescs=64;
template<int Capacity>
class Pool {
public:
    static_assert(Capacity>0 && Capacity<=65536,"budget");
    int Register(const Desc& d) noexcept {
        if(descCount_>=kDescs)return -1;
        desc_[descCount_]=d;return descCount_++;
    }
    const Desc* DescAt(int i) const noexcept { return i>=0 && i<descCount_ ? &desc_[i] : nullptr; }
    int Live() const noexcept { return live_; }
    static constexpr int Budget() noexcept { return Capacity; }
    unsigned Dropped() const noexcept { return replaced_; }
    void Clear() noexcept { for(auto& p:p_)p.live=false;live_=0;free_=0;replaced_=0; }
    void SetWind(float x,float y,float z) noexcept { wind_[0]=x;wind_[1]=y;wind_[2]=z; }

    // Returns the slot or -1 (an unknown desc).
    int Spawn(int desc,const float pos[3],const float vel[3],Rng& rng,float scale=1.0f) noexcept {
        const Desc* d=DescAt(desc);
        if(!d)return -1;
        int slot=-1;
        if(live_<Capacity) {
            for(int i=0;i<Capacity;++i) {
                const int at=(free_+i)%Capacity;
                if(!p_[at].live){slot=at;break;}
            }
            free_=(slot+1)%Capacity;
            ++live_;
        } else {
            float worst=-1.0f;
            for(int i=0;i<Capacity;++i) {
                const float f=p_[i].age/p_[i].life;
                if(f>worst){worst=f;slot=i;}
            }
            ++replaced_;
        }
        Particle& p=p_[slot];
        std::memcpy(p.pos,pos,sizeof(p.pos));std::memcpy(p.vel,vel,sizeof(p.vel));
        p.age=0.0f;p.life=(std::max)(0.05f,rng.Range(d->lifeMin,d->lifeMax));
        p.size0=rng.Range(d->sizeMin,d->sizeMax)*scale;
        p.rot=rng.Range(0.0f,6.2831853f);
        p.spin=rng.Range(-d->spin,d->spin);
        p.frame0=rng.Range(0.0f,8.0f);   // a frame offset so neighbours do not billow in step
        p.desc=static_cast<std::uint16_t>(desc);p.live=true;p.serial=++serial_;
        return slot;
    }

    void Step(float dt) noexcept {
        if(!(dt>0.0f))return;
        for(auto& p:p_) {
            if(!p.live)continue;
            p.age+=dt;
            if(p.age>=p.life){p.live=false;--live_;continue;}
            const Desc& d=desc_[p.desc];
            const float keep=std::pow((std::max)(0.0f,1.0f-d.drag),dt);
            for(int a=0;a<3;++a) {
                // Drag pulls the velocity towards the wind (smoke drifts with the air, it does not stop dead).
                const float target=wind_[a]*d.windFollow;
                p.vel[a]=target+(p.vel[a]-target)*keep;
            }
            p.vel[1]+=d.buoyancy*dt;
            for(int a=0;a<3;++a)p.pos[a]+=p.vel[a]*dt;
            p.rot+=p.spin*dt;
        }
    }

    // The drawable state of every live particle; returns the count (at most `most`). `frames`: the flipbook's. `ids`
    // (optional, `most` long): each one's spawn serial.
    int Build(Instance* out,int most,int frames,std::uint32_t* ids=nullptr) const noexcept {
        int n=0;
        for(const auto& p:p_) {
            if(!p.live || n>=most)continue;
            if(ids)ids[n]=p.serial;
            const Desc& d=desc_[p.desc];
            const float t=p.age/p.life;
            Instance& o=out[n++];
            std::memcpy(o.pos,p.pos,sizeof(o.pos));
            o.size=p.size0*d.size.Eval(t);
            const float m=std::clamp(d.colorMix.Eval(t),0.0f,1.0f);
            for(int c=0;c<3;++c)o.color[c]=d.color0[c]+(d.color1[c]-d.color0[c])*m;
            o.color[3]=std::clamp(d.alpha.Eval(t),0.0f,1.0f);
            o.rot=p.rot;
            o.frame=FrameAt(p,d,t,frames);
            o.soft=d.soft;o.emissive=d.emissive;
        }
        return n;
    }

    static float FrameAt(const Particle& p,const Desc& d,float t,int frames) noexcept {
        if(frames<=1)return 0.0f;
        const float last=static_cast<float>(frames-1);
        const float f=d.frameRate>0.0f ? std::fmod(p.frame0+p.age*d.frameRate,last) : t*last;
        return std::clamp(f,0.0f,last);
    }

private:
    Particle p_[Capacity]{};
    Desc desc_[kDescs]{};
    int descCount_=0,live_=0,free_=0;
    unsigned replaced_=0;
    std::uint32_t serial_=0;
    float wind_[3]{};
};

// ---- ordering ----
// View-space depth of a world point under a row-vector world->view matrix (D3D style: p' = p * V). The engine's view
// (Commander view+0x20) looks down -Z, so a larger -z is farther; the sign is taken from the matrix, not assumed.
inline float ViewZ(const float v[16],const float p[3]) noexcept {
    return p[0]*v[2]+p[1]*v[6]+p[2]*v[10]+v[14];
}
// Back to front: `order` gets the indices of `in` farthest first. Stable for equal depths (deterministic). `keys` is
// scratch of n floats. Farther = smaller view z when the view looks down -Z (lookNegZ), larger otherwise.
inline void SortBackToFront(const Instance* in,int n,const float view[16],bool lookNegZ,std::uint32_t* order,
                            float* keys) noexcept {
    for(int i=0;i<n;++i) {
        const float z=ViewZ(view,in[i].pos);
        keys[i]=lookNegZ ? z : -z;   // ascending key = farthest first
        order[i]=static_cast<std::uint32_t>(i);
    }
    std::stable_sort(order,order+n,[keys](std::uint32_t a,std::uint32_t b){return keys[a]<keys[b];});
}

// ---- the fill budget ----
// Ours is drawn lit and soft per pixel, so its cost is the screen it covers: a blast seen from inside its cloud covers
// the screen hundreds of times over (the ultimate strike at 60 m: some 5000 puffs, 0.46 s a frame at 4K). Lowering the
// count does not help by itself (LodGrow keeps the cover), so the cover is budgeted where the camera is known.
//
// One puff's cover in screens: its quad (half width `size`, ours: no corner scale) over the screen at its view depth;
// a whole screen once the camera is about inside it, none where the VS fades it out (its centre nearer than a
// quarter of its size) or behind the camera.
inline float Coverage(const Instance& in,const float view[16],const float proj[16],bool lookNegZ) noexcept {
    float v[3];
    for(int a=0;a<3;++a)v[a]=in.pos[0]*view[a]+in.pos[1]*view[4+a]+in.pos[2]*view[8+a]+view[12+a];
    const float s=in.size;
    if(!(s>0.0f))return 0.0f;
    const float centre=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(centre<=0.25f*s)return 0.0f;
    const float d=lookNegZ ? -v[2] : v[2];
    if(d<=s)return d<=-s ? 0.0f : 1.0f;
    return (std::min)(1.0f,s*s*std::fabs(proj[0]*proj[5])/(d*d));
}
// A stable pseudo-random [0, 1) from a spawn serial (the subset kept must not flicker from frame to frame).
inline float SerialUnit(std::uint32_t id) noexcept {
    id^=id>>16;id*=0x7FEB352Du;id^=id>>15;id*=0x846CA68Bu;id^=id>>16;
    return static_cast<float>(id>>8)*(1.0f/16777216.0f);
}
struct Thinned { int count=0;float covered=0.0f,keep=1.0f; };
// In place: the puffs that cover nothing dropped; past `budget` screens, the stable fraction keep = budget / cover
// (no less than `least`) kept, their alpha raised so a cloud seen through them stays as opaque (k overlapping layers
// of alpha a pass (1 - a)^k; keep * k of them at 1 - (1 - a)^(1 / keep) pass the same).
inline Thinned ThinToBudget(Instance* in,const std::uint32_t* ids,int n,const float view[16],const float proj[16],
                            bool lookNegZ,float budget,float least) noexcept {
    Thinned t{};
    for(int i=0;i<n;++i)t.covered+=Coverage(in[i],view,proj,lookNegZ);
    if(t.covered>budget && budget>0.0f)t.keep=(std::max)(least,budget/t.covered);
    const float power=1.0f/t.keep;
    int out=0;
    for(int i=0;i<n;++i) {
        const float c=Coverage(in[i],view,proj,lookNegZ);
        if(!(c>0.0f))continue;
        if(t.keep<1.0f) {
            if(SerialUnit(ids[i])>=t.keep)continue;
            in[i].color[3]=1.0f-std::pow(1.0f-std::clamp(in[i].color[3],0.0f,1.0f),power);
        }
        in[out++]=in[i];
    }
    t.count=out;
    return t;
}

// ---- the stock particle card (docs/gpufx-re.md: VS 0x1968790, grid table 0x1AE0BA0) ----
// The stock VS puts corner k (uv (0,0) (1,0) (0,1) (1,1)) at view-space offset size * (sin, cos)(rot + phi_k) with
// phi = 0, pi/2, 3pi/2, pi. Ours puts corner c = (-1,1) (1,1) (-1,-1) (1,-1) at size * 0.7071 * c turned anticlockwise
// by its own rot: the same corners when ours = -stock - pi/4.
inline float StockRotToOurs(float stockRot) noexcept { return -stockRot-0.78539816f; }
// The stock atlas grid (tiles a side) by frame count: 1 and 2 one tile, 4 a 2x2, 9 a 3x3, 16 a 4x4; 0 for any other
// count (the table holds -1 there; no stock emitter has one, and ours refuses it).
inline int StockGrid(int frames) noexcept {
    switch(frames){case 1:case 2:return 1;case 4:return 2;case 9:return 3;case 16:return 4;default:return 0;}
}

// ---- depth ----
// The view-space z a hardware depth value d came from, for a row-vector projection P (clip = v * P): solved from
// d = (z*P22+P32)/(z*P23+P33). Works for standard and reversed depth alike (the shader uses the same formula).
inline float ViewZFromDepth(const float proj[16],float d) noexcept {
    const float p22=proj[10],p32=proj[14],p23=proj[11],p33=proj[15];
    const float den=d*p23-p22;
    return den!=0.0f ? (p32-d*p33)/den : 0.0f;
}
inline float DepthFromViewZ(const float proj[16],float z) noexcept {
    const float w=z*proj[11]+proj[15];
    return w!=0.0f ? (z*proj[10]+proj[14])/w : 0.0f;
}
// Whether nearer points get larger depth values (reversed depth): decided from the projection itself.
inline bool ReversedDepth(const float proj[16],bool lookNegZ) noexcept {
    const float s=lookNegZ ? -1.0f : 1.0f;
    return DepthFromViewZ(proj,s*1.0f)>DepthFromViewZ(proj,s*100.0f);
}

// ---- the procedural smoke flipbook ----
// grid x grid frames of tile x tile pixels, RGBA8: rg = the puff's normal (x right, y up, 0.5 = 0), b = thickness,
// a = density. Each frame is one slice of 3D fbm value noise moving through z (billowing), inside a soft round
// falloff that reaches 0 two pixels inside the tile's edge (no bleeding between tiles down to the fourth mip).
// Deterministic: the same bytes on every machine and run (tests/gpufx_sim_test.cpp pins a hash).
namespace noise {
inline std::uint32_t Hash(int x,int y,int z) noexcept {
    std::uint32_t h=static_cast<std::uint32_t>(x)*0x8DA6B343u ^ static_cast<std::uint32_t>(y)*0xD8163841u ^
        static_cast<std::uint32_t>(z)*0xCB1AB31Fu;
    h^=h>>13;h*=0x5BD1E995u;h^=h>>15;return h;
}
inline float Lattice(int x,int y,int z) noexcept { return static_cast<float>(Hash(x,y,z)&0xFFFFu)*(1.0f/65535.0f); }
inline float Smooth(float t) noexcept { return t*t*(3.0f-2.0f*t); }
inline float Value(float x,float y,float z) noexcept {
    const int xi=static_cast<int>(std::floor(x)),yi=static_cast<int>(std::floor(y)),zi=static_cast<int>(std::floor(z));
    const float fx=Smooth(x-static_cast<float>(xi)),fy=Smooth(y-static_cast<float>(yi)),fz=Smooth(z-static_cast<float>(zi));
    auto lerp=[](float a,float b,float t){return a+(b-a)*t;};
    const float x00=lerp(Lattice(xi,yi,zi),Lattice(xi+1,yi,zi),fx),x10=lerp(Lattice(xi,yi+1,zi),Lattice(xi+1,yi+1,zi),fx);
    const float x01=lerp(Lattice(xi,yi,zi+1),Lattice(xi+1,yi,zi+1),fx),x11=lerp(Lattice(xi,yi+1,zi+1),Lattice(xi+1,yi+1,zi+1),fx);
    return lerp(lerp(x00,x10,fy),lerp(x01,x11,fy),fz);
}
inline float Fbm(float x,float y,float z) noexcept {
    float sum=0.0f,amp=0.5f,norm=0.0f;
    for(int o=0;o<5;++o){sum+=amp*Value(x,y,z);norm+=amp;x*=2.03f;y*=2.03f;z*=2.03f;amp*=0.5f;}
    return sum/norm;
}
}  // namespace noise

constexpr int kFlipTile=64,kFlipGrid=8,kFlipFrames=kFlipGrid*kFlipGrid,kFlipSide=kFlipTile*kFlipGrid;

// Density of frame f at tile-local (u,v) in [-1,1]^2.
inline float SmokeDensity(int frame,float u,float v) noexcept {
    const float z=static_cast<float>(frame)*0.11f;
    const float r=std::sqrt(u*u+v*v);
    // The falloff edge wobbles with low-frequency noise so the silhouette is not a circle.
    const float edge=0.78f+0.18f*(noise::Fbm(u*1.3f+7.1f,v*1.3f-3.3f,z*0.5f)-0.5f)*2.0f;
    const float limit=1.0f-4.0f/static_cast<float>(kFlipTile);   // 2 px inside the edge
    float fall=std::clamp((edge-r)/0.45f,0.0f,1.0f);
    fall=fall*fall*(3.0f-2.0f*fall);
    if(r>=limit)fall=0.0f;
    const float n=noise::Fbm(u*2.2f+11.0f,v*2.2f+5.0f,z);
    return std::clamp((n-0.28f)*1.9f,0.0f,1.0f)*fall;
}

// Fills rgba (kFlipSide*kFlipSide*4 bytes).
inline void GenerateSmokeFlipbook(std::uint8_t* rgba) noexcept {
    constexpr int T=kFlipTile;
    static float d[T+2][T+2];
    for(int f=0;f<kFlipFrames;++f) {
        const int ox=(f%kFlipGrid)*T,oy=(f/kFlipGrid)*T;
        for(int y=-1;y<=T;++y)for(int x=-1;x<=T;++x) {
            const float u=(static_cast<float>(x)+0.5f)/T*2.0f-1.0f,v=(static_cast<float>(y)+0.5f)/T*2.0f-1.0f;
            d[y+1][x+1]=SmokeDensity(f,u,v);
        }
        for(int y=0;y<T;++y)for(int x=0;x<T;++x) {
            const float u=(static_cast<float>(x)+0.5f)/T*2.0f-1.0f,v=(static_cast<float>(y)+0.5f)/T*2.0f-1.0f;
            const float a=d[y+1][x+1];
            // Normal: the density's slope (texture y grows downwards; normal y is up) bent towards the puff's sphere.
            const float gx=(d[y+1][x+2]-d[y+1][x])*0.5f*static_cast<float>(T)*0.08f;
            const float gy=(d[y+2][x+1]-d[y][x+1])*0.5f*static_cast<float>(T)*0.08f;
            float nx=-gx+u*0.6f,ny=gy-v*0.6f;
            const float len=std::sqrt(nx*nx+ny*ny);
            if(len>0.95f){nx*=0.95f/len;ny*=0.95f/len;}
            const float thick=std::clamp(a*1.2f,0.0f,1.0f);
            std::uint8_t* px=rgba+(static_cast<std::size_t>(oy+y)*kFlipSide+static_cast<std::size_t>(ox+x))*4;
            auto b=[](float v01){return static_cast<std::uint8_t>(std::clamp(v01,0.0f,1.0f)*255.0f+0.5f);};
            px[0]=b(nx*0.5f+0.5f);px[1]=b(ny*0.5f+0.5f);px[2]=b(thick);px[3]=b(a);
        }
    }
}

// FNV-1a of a byte range (the offline test's fingerprint of the flipbook).
inline std::uint32_t Fnv(const std::uint8_t* p,std::size_t n) noexcept {
    std::uint32_t h=2166136261u;
    for(std::size_t i=0;i<n;++i){h^=p[i];h*=16777619u;}
    return h;
}

}  // namespace crew::gpufx
