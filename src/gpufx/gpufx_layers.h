// GPU particle effects (src/gpufx): the War Thunder style layered emitters, the pure half (no Windows, no game memory:
// tests/gpufx_sim_test.cpp runs it). A gun's shot and a blast are each a few layers of the one Pool, each layer a Desc:
//   - a big gun's shot (a vehicle's, AmmoExplosion >= kCannonBlast): the muzzle's flash, a shock ring of fast puffs in the
//     plane across the bore, a ground dust ring when the muzzle is near the ground, a thick smoke ball pushed out along
//     the bore that swells, rises and drifts with the wind for 8-20 s; a hand-held launcher's: the smoke ball only,
//     smaller.
//   - a blast (EffectGenUtil::Explosion 0x2E5ED0, size = radius / 5; Explosion_Big 0x2E7A30, size = radius / 70; the
//     event carries the radius): a fireball, a dirt column thrown up and falling
//     back, a black smoke column rising 10-18 s, debris, low residual smoke 15-25 s; a radius of kHeavyBlast or more (an
//     artillery shell, a bomb) adds a wide dust cloud for 20-30 s.
// The counts scale with the size and with `lod` (the caller's: distance to the camera and the pool's load): a far or
// crowded blast is fewer, bigger puffs, never a cut. Everything is visual and made on each machine from its own events.
#pragma once
#include "gpufx_sim.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace crew::gpufx {

constexpr float kCannonBlast=2.0f;   // m: a shot of a weapon with at least this AmmoExplosion makes smoke
constexpr float kHeavyBlast=12.0f;   // m: a blast of at least this radius adds the wide dust cloud
// A vehicle gun's shot is as big as its AmmoExplosion or, for a round that bursts not at all (an APFSDS: 0 m, 350 a
// round), its AmmoDamage / kDamagePerBlast: a tank's AP shot as big as its HE (8 m, 350), an autocannon's AP (40) or a
// machine gun's none. A round that leaves slower than kLaunchSpeed (m a frame: a missile off its rail, a rocket out of
// its tube, 0.3..2) is a launch, not a gun's blast: no flash, no shock ring, its puffs blown back out of the tube, and
// off the ground nothing (Ace Combat's rail launch: the round drops, then its own motor lights; docs/feature-projectile-look.md).
constexpr float kDamagePerBlast=40.0f,kLaunchSpeed=2.5f;
inline float ShotPower(float blast,float damage,float speed,bool vehicle,bool& launch) noexcept {
    launch=std::isfinite(speed) && speed<kLaunchSpeed;
    float p=std::isfinite(blast) ? blast : 0.0f;
    if(vehicle && !launch && std::isfinite(damage))p=(std::max)(p,damage/kDamagePerBlast);
    return p;
}

struct LayerDescs {
    int flash=-1,ring=-1,groundDust=-1,muzzleSmoke=-1;
    int fireball=-1,dirt=-1,black=-1,debris=-1,residual=-1,bigDust=-1;
    bool Ready() const noexcept { return flash>=0 && bigDust>=0; }
};

namespace layers_detail {
inline Curve Keys(std::initializer_list<Key> keys) noexcept { Curve c;for(const Key& k:keys)c.Add(k.t,k.v);return c; }
inline void Rgb(float* c,float r,float g,float b) noexcept { c[0]=r;c[1]=g;c[2]=b; }
}

// The layers' descriptions into `pool` (once; sizes are at a unit scale, Spawn's scale sets each event's).
template<int N>
LayerDescs RegisterLayers(Pool<N>& pool) noexcept {
    using namespace layers_detail;
    LayerDescs l{};
    {   // the muzzle flash: a short additive burst
        Desc d{};d.lifeMin=0.07f;d.lifeMax=0.12f;d.sizeMin=1.0f;d.sizeMax=1.3f;
        d.size=Keys({{0.0f,0.6f},{0.3f,1.0f},{1.0f,1.2f}});d.alpha=Keys({{0.0f,1.0f},{1.0f,0.0f}});
        Rgb(d.color0,9.0f,5.0f,1.8f);Rgb(d.color1,4.0f,1.4f,0.3f);d.colorMix=Keys({{0.0f,0.0f},{1.0f,1.0f}});
        d.drag=0.0f;d.buoyancy=0.0f;d.windFollow=0.0f;d.spin=0.0f;d.emissive=1.0f;d.soft=0.5f;d.frameRate=30.0f;
        l.flash=pool.Register(d);
    }
    {   // the shock ring: pale, fast, gone in under a second
        Desc d{};d.lifeMin=0.45f;d.lifeMax=0.8f;d.sizeMin=0.6f;d.sizeMax=0.9f;
        d.size=Keys({{0.0f,0.7f},{1.0f,3.0f}});d.alpha=Keys({{0.0f,0.0f},{0.08f,0.45f},{1.0f,0.0f}});
        Rgb(d.color0,0.78f,0.76f,0.72f);Rgb(d.color1,0.8f,0.78f,0.75f);
        d.drag=0.97f;d.buoyancy=0.0f;d.windFollow=0.2f;d.spin=1.0f;d.soft=1.0f;
        l.ring=pool.Register(d);
    }
    {   // the ground dust ring: brown, rolling out along the ground, settling
        Desc d{};d.lifeMin=3.0f;d.lifeMax=6.0f;d.sizeMin=1.0f;d.sizeMax=1.5f;
        d.size=Keys({{0.0f,0.8f},{0.3f,2.2f},{1.0f,3.6f}});d.alpha=Keys({{0.0f,0.0f},{0.05f,0.7f},{0.5f,0.45f},{1.0f,0.0f}});
        Rgb(d.color0,0.42f,0.35f,0.26f);Rgb(d.color1,0.56f,0.5f,0.41f);d.colorMix=Keys({{0.0f,0.0f},{1.0f,1.0f}});
        d.drag=0.85f;d.buoyancy=0.15f;d.windFollow=1.0f;d.spin=0.4f;d.soft=1.5f;
        l.groundDust=pool.Register(d);
    }
    {   // the muzzle smoke ball: thick, swelling, rising, drifting 8-20 s
        Desc d{};d.lifeMin=8.0f;d.lifeMax=20.0f;d.sizeMin=0.9f;d.sizeMax=1.3f;
        d.size=Keys({{0.0f,0.7f},{0.1f,2.0f},{0.5f,4.0f},{1.0f,6.0f}});
        d.alpha=Keys({{0.0f,0.0f},{0.02f,0.85f},{0.3f,0.6f},{1.0f,0.0f}});
        Rgb(d.color0,0.48f,0.47f,0.45f);Rgb(d.color1,0.66f,0.66f,0.65f);d.colorMix=Keys({{0.0f,0.0f},{0.6f,1.0f}});
        d.drag=0.9f;d.buoyancy=0.35f;d.windFollow=1.0f;d.spin=0.3f;d.soft=2.0f;
        l.muzzleSmoke=pool.Register(d);
    }
    {   // the fireball: additive, a third of a second
        Desc d{};d.lifeMin=0.25f;d.lifeMax=0.5f;d.sizeMin=0.5f;d.sizeMax=0.75f;
        d.size=Keys({{0.0f,0.5f},{0.4f,1.0f},{1.0f,1.25f}});d.alpha=Keys({{0.0f,1.0f},{0.5f,0.8f},{1.0f,0.0f}});
        Rgb(d.color0,10.0f,5.0f,1.6f);Rgb(d.color1,3.0f,0.9f,0.2f);d.colorMix=Keys({{0.0f,0.0f},{1.0f,1.0f}});
        d.drag=0.9f;d.buoyancy=2.0f;d.windFollow=0.0f;d.spin=0.8f;d.emissive=1.0f;d.soft=1.0f;d.frameRate=20.0f;
        l.fireball=pool.Register(d);
    }
    {   // the dirt column: dark earth thrown up, falling back
        Desc d{};d.lifeMin=1.4f;d.lifeMax=2.8f;d.sizeMin=0.25f;d.sizeMax=0.4f;
        d.size=Keys({{0.0f,0.6f},{0.4f,1.3f},{1.0f,1.8f}});d.alpha=Keys({{0.0f,0.95f},{0.6f,0.8f},{1.0f,0.0f}});
        Rgb(d.color0,0.2f,0.16f,0.12f);Rgb(d.color1,0.36f,0.31f,0.25f);d.colorMix=Keys({{0.0f,0.0f},{1.0f,1.0f}});
        d.drag=0.35f;d.buoyancy=-9.0f;d.windFollow=0.3f;d.spin=0.6f;d.soft=1.0f;
        l.dirt=pool.Register(d);
    }
    {   // the black smoke column: rising, spreading 10-18 s
        Desc d{};d.lifeMin=10.0f;d.lifeMax=18.0f;d.sizeMin=0.35f;d.sizeMax=0.5f;
        d.size=Keys({{0.0f,0.6f},{0.15f,1.6f},{1.0f,4.5f}});d.alpha=Keys({{0.0f,0.0f},{0.03f,0.9f},{0.4f,0.65f},{1.0f,0.0f}});
        Rgb(d.color0,0.07f,0.066f,0.062f);Rgb(d.color1,0.3f,0.29f,0.28f);d.colorMix=Keys({{0.0f,0.0f},{1.0f,1.0f}});
        d.drag=0.75f;d.buoyancy=1.2f;d.windFollow=1.0f;d.spin=0.35f;d.soft=2.0f;
        l.black=pool.Register(d);
    }
    {   // debris: small dark bits on ballistic arcs
        Desc d{};d.lifeMin=1.0f;d.lifeMax=2.2f;d.sizeMin=0.12f;d.sizeMax=0.25f;
        d.alpha=Keys({{0.0f,1.0f},{0.8f,1.0f},{1.0f,0.0f}});
        Rgb(d.color0,0.05f,0.045f,0.04f);Rgb(d.color1,0.05f,0.045f,0.04f);
        d.drag=0.08f;d.buoyancy=-9.8f;d.windFollow=0.0f;d.spin=6.0f;d.soft=0.2f;
        l.debris=pool.Register(d);
    }
    {   // residual smoke: low, grey-brown, lingering 15-25 s
        Desc d{};d.lifeMin=15.0f;d.lifeMax=25.0f;d.sizeMin=0.5f;d.sizeMax=0.7f;
        d.size=Keys({{0.0f,0.8f},{0.2f,1.8f},{1.0f,3.5f}});d.alpha=Keys({{0.0f,0.0f},{0.1f,0.45f},{0.6f,0.28f},{1.0f,0.0f}});
        Rgb(d.color0,0.4f,0.37f,0.33f);Rgb(d.color1,0.58f,0.56f,0.53f);d.colorMix=Keys({{0.0f,0.0f},{1.0f,1.0f}});
        d.drag=0.9f;d.buoyancy=0.12f;d.windFollow=1.0f;d.spin=0.2f;d.soft=2.5f;
        l.residual=pool.Register(d);
    }
    {   // the heavy blast's wide dust cloud: 20-30 s
        Desc d{};d.lifeMin=20.0f;d.lifeMax=30.0f;d.sizeMin=0.8f;d.sizeMax=1.1f;
        d.size=Keys({{0.0f,0.8f},{0.1f,2.0f},{1.0f,4.0f}});d.alpha=Keys({{0.0f,0.0f},{0.04f,0.5f},{0.6f,0.28f},{1.0f,0.0f}});
        Rgb(d.color0,0.45f,0.39f,0.31f);Rgb(d.color1,0.6f,0.56f,0.5f);d.colorMix=Keys({{0.0f,0.0f},{1.0f,1.0f}});
        d.drag=0.8f;d.buoyancy=0.2f;d.windFollow=1.0f;d.spin=0.15f;d.soft=3.0f;
        l.bigDust=pool.Register(d);
    }
    return l;
}

struct ShotEvent {
    float pos[3];        // the muzzle
    float dir[3];        // the bore (unit)
    float blast=0.0f;    // the weapon's AmmoExplosion, m
    float ground=0.0f;   // the ground's height under the muzzle (the shooter's feet / hull)
    bool vehicle=false;  // a vehicle's gun (else a hand-held launcher)
    bool launch=false;   // a missile's or a rocket's launch (ShotPower)
};
struct BlastEvent {
    float pos[3];
    float radius=5.0f;   // m (ExplosionRadius / ExplosionBigRadius from the stock call's size)
};

// The two stock blast calls' size units differ, so a hook turns its size back into the radius first:
//   - Explosion 0x2E5ED0: the callers pass radius / 5 (0x264B03 divss [0x1765A38] = 5.0; 0x254EE0 the same).
//   - Explosion_Big 0x2E7A30: the callers pass radius / 70 (0x255192 and 0x287E89 divss [0x179F74C] = 70.0; the
//     constant callers 0.5-2.5); inside, the stock scatter is size*100 and the point light size*600.
// Read as radius / 5, a big blast's layers were 14x too small.
constexpr float kExplosionSizeUnit=5.0f,kExplosionBigSizeUnit=70.0f;
constexpr float ExplosionRadius(float size) noexcept { return size*kExplosionSizeUnit; }
constexpr float ExplosionBigRadius(float size) noexcept { return size*kExplosionBigSizeUnit; }

// How many particles of each layer (before the caller's lod), for the budget and the tests.
struct ShotCounts { int flash,ring,dust,smoke; };
inline ShotCounts CountShot(const ShotEvent& e,float lod) noexcept {
    ShotCounts c{0,0,0,0};
    if(!(e.blast>=kCannonBlast))return c;
    const float k=std::clamp(e.blast/10.0f,0.4f,1.6f)*std::clamp(lod,0.0f,1.0f);
    const bool nearGround=e.pos[1]-e.ground<5.0f;
    auto n=[](float v){return static_cast<int>(v+0.5f);};
    if(e.launch) {   // the tube's backblast on the ground; a rail launch in the air leaves the motor's own trail alone
        if(!nearGround)return c;
        c.dust=n(10.0f*k);
        c.smoke=(std::max)(k>0.0f ? 2 : 0,n(6.0f*k));
        return c;
    }
    if(e.vehicle) {
        c.flash=k>0.0f ? 2 : 0;
        c.ring=n(14.0f*k);
        c.dust=nearGround ? n(18.0f*k) : 0;
        c.smoke=(std::max)(k>0.0f ? 3 : 0,n(16.0f*k));
    } else {
        c.smoke=(std::max)(k>0.0f ? 2 : 0,n(6.0f*k));
    }
    return c;
}
struct BlastCounts { int fireball,dirt,black,debris,residual,bigDust; };
inline BlastCounts CountBlast(const BlastEvent& e,float lod) noexcept {
    BlastCounts c{0,0,0,0,0,0};
    const float r=e.radius;
    if(!(r>0.2f) || !(lod>0.0f))return c;
    const float k=std::clamp(std::sqrt(r/6.0f),0.35f,2.0f)*std::clamp(lod,0.0f,1.0f);
    auto n=[](float v,int least){return (std::max)(least,static_cast<int>(v+0.5f));};
    c.fireball=n(4.0f*k,1);c.dirt=n(14.0f*k,2);c.black=n(10.0f*k,2);c.debris=n(16.0f*k,0);c.residual=n(6.0f*k,1);
    c.bigDust=r>=kHeavyBlast ? n(12.0f*k,3) : 0;
    return c;
}

// The blast layers' size units. The short-lived ones near the burst (fireball, dirt, black column, debris) are linear
// in the radius like the stock effect (1-40 m). Only the long-lived low ones (residual smoke, the heavy blast's dust
// cloud: 15-30 s, growing 3.5-4x) use BlastScale: the radius up to 6 m, then its square root's growth (a 15 m rocket's
// puffs 9.5, an artillery shell's 40 m 15.5); linear, a heavy blast's 4x-grown dust puffs were 60 m walls the camera
// stood inside of for half a minute (the range A/B, 2026-11-26).
inline float BlastNearScale(float r) noexcept { return std::clamp(r,1.0f,40.0f); }
inline float BlastScale(float r) noexcept { return r<=6.0f ? (std::max)(r,1.0f) : 6.0f*std::sqrt(r/6.0f); }

// A far or crowded event's fewer puffs are bigger: the cover stays about the same (area ~ count * size^2).
inline float LodGrow(float lod) noexcept { return 1.0f/std::sqrt(std::clamp(lod,0.2f,1.0f)); }

namespace layers_detail {
// Two unit vectors across `d` (unit).
inline void Across(const float* d,float* a,float* b) noexcept {
    const float up[3]{0.0f,1.0f,0.0f},side[3]{1.0f,0.0f,0.0f};
    const float* h=std::fabs(d[1])<0.9f ? up : side;
    a[0]=d[1]*h[2]-d[2]*h[1];a[1]=d[2]*h[0]-d[0]*h[2];a[2]=d[0]*h[1]-d[1]*h[0];
    const float l=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
    for(int i=0;i<3;++i)a[i]/=l;
    b[0]=d[1]*a[2]-d[2]*a[1];b[1]=d[2]*a[0]-d[0]*a[2];b[2]=d[0]*a[1]-d[1]*a[0];
}
}

// A shot's layers into the pool; returns the particles spawned.
template<int N>
int SpawnShot(Pool<N>& pool,const LayerDescs& l,Rng& rng,const ShotEvent& e,float lod) noexcept {
    using namespace layers_detail;
    const ShotCounts c=CountShot(e,lod);
    const float g=LodGrow(lod);
    const float s=std::clamp(e.blast/10.0f,0.4f,1.6f)*(e.vehicle ? 1.0f : 0.5f);
    float a[3],b[3];Across(e.dir,a,b);
    int made=0;
    for(int i=0;i<c.flash;++i) {
        const float at[3]{e.pos[0]+e.dir[0]*0.8f*s*static_cast<float>(i),e.pos[1]+e.dir[1]*0.8f*s*static_cast<float>(i),e.pos[2]+e.dir[2]*0.8f*s*static_cast<float>(i)};
        const float v[3]{e.dir[0]*4.0f,e.dir[1]*4.0f,e.dir[2]*4.0f};
        made+=pool.Spawn(l.flash,at,v,rng,g*1.6f*s)>=0;
    }
    for(int i=0;i<c.ring;++i) {
        const float t=6.2831853f*(static_cast<float>(i)+rng.Unit()*0.5f)/static_cast<float>(c.ring);
        const float ca=std::cos(t),sa=std::sin(t),speed=rng.Range(22.0f,32.0f)*s;
        float v[3];for(int k=0;k<3;++k)v[k]=(a[k]*ca+b[k]*sa)*speed+e.dir[k]*6.0f*s;
        const float at[3]{e.pos[0]+e.dir[0]*0.5f,e.pos[1]+e.dir[1]*0.5f,e.pos[2]+e.dir[2]*0.5f};
        made+=pool.Spawn(l.ring,at,v,rng,g*s)>=0;
    }
    for(int i=0;i<c.dust;++i) {
        const float t=6.2831853f*(static_cast<float>(i)+rng.Unit())/static_cast<float>(c.dust);
        const float speed=rng.Range(9.0f,16.0f)*s;
        // The blast pushes the dust away from the muzzle's foot, more along the bore than behind it.
        const float fwd=0.5f+0.5f*(std::cos(t)*e.dir[0]+std::sin(t)*e.dir[2]);
        const float v[3]{std::cos(t)*speed*(0.6f+0.8f*fwd),rng.Range(0.5f,2.0f),std::sin(t)*speed*(0.6f+0.8f*fwd)};
        const float at[3]{e.pos[0]+e.dir[0]*2.0f,e.ground+0.6f*s,e.pos[2]+e.dir[2]*2.0f};
        made+=pool.Spawn(l.groundDust,at,v,rng,g*1.6f*s)>=0;
    }
    for(int i=0;i<c.smoke;++i) {
        // Along the bore, the first puffs fastest (they travel furthest before the drag stops them).
        const float f=c.smoke>1 ? static_cast<float>(i)/static_cast<float>(c.smoke-1) : 0.0f;
        const float speed=((28.0f-20.0f*f)*s+rng.Range(-2.0f,2.0f))*(e.launch ? -0.4f : 1.0f);   // a launch's out back
        float v[3];for(int k=0;k<3;++k)v[k]=e.dir[k]*speed+(a[k]*rng.Range(-1.0f,1.0f)+b[k]*rng.Range(-1.0f,1.0f))*2.5f*s;
        made+=pool.Spawn(l.muzzleSmoke,e.pos,v,rng,g*1.5f*s)>=0;
    }
    return made;
}

// A blast's layers into the pool; returns the particles spawned.
template<int N>
int SpawnBlast(Pool<N>& pool,const LayerDescs& l,Rng& rng,const BlastEvent& e,float lod) noexcept {
    const BlastCounts c=CountBlast(e,lod);
    const float g=LodGrow(lod);
    const float r=e.radius;
    const float s=BlastNearScale(r);              // the short-lived layers' unit sizes are per metre of this
    const float sLow=BlastScale(r);               // the long-lived low layers' (residual, bigDust)
    const float up=std::clamp(std::sqrt(r/6.0f),0.5f,2.2f);   // throw speeds grow slower than the radius
    int made=0;
    auto at=[&](float spread,float lift,float* p){
        p[0]=e.pos[0]+rng.Range(-spread,spread);p[1]=e.pos[1]+lift;p[2]=e.pos[2]+rng.Range(-spread,spread);
    };
    float p[3];
    for(int i=0;i<c.fireball;++i) {
        at(0.25f*s,0.2f*s,p);
        const float v[3]{rng.Range(-3.0f,3.0f),rng.Range(2.0f,6.0f),rng.Range(-3.0f,3.0f)};
        made+=pool.Spawn(l.fireball,p,v,rng,g*s)>=0;
    }
    for(int i=0;i<c.dirt;++i) {
        at(0.15f*s,0.1f*s,p);
        const float t=rng.Range(0.0f,6.2831853f),out=rng.Range(0.0f,0.35f);
        const float speed=rng.Range(14.0f,26.0f)*up;
        const float v[3]{std::cos(t)*out*speed,speed,std::sin(t)*out*speed};
        made+=pool.Spawn(l.dirt,p,v,rng,g*s)>=0;
    }
    for(int i=0;i<c.black;++i) {
        at(0.2f*s,0.3f*s*static_cast<float>(i)/static_cast<float>(c.black),p);
        const float v[3]{rng.Range(-1.5f,1.5f),rng.Range(4.0f,10.0f)*up,rng.Range(-1.5f,1.5f)};
        made+=pool.Spawn(l.black,p,v,rng,g*s)>=0;
    }
    for(int i=0;i<c.debris;++i) {
        at(0.1f*s,0.1f*s,p);
        const float t=rng.Range(0.0f,6.2831853f),out=rng.Range(0.2f,0.8f);
        const float speed=rng.Range(14.0f,30.0f)*up;
        const float v[3]{std::cos(t)*out*speed,speed*std::sqrt(1.0f-out*out),std::sin(t)*out*speed};
        made+=pool.Spawn(l.debris,p,v,rng,g*1.0f)>=0;
    }
    for(int i=0;i<c.residual;++i) {
        at(0.5f*sLow,0.1f*sLow,p);
        const float t=rng.Range(0.0f,6.2831853f),speed=rng.Range(1.0f,3.0f)*up;
        const float v[3]{std::cos(t)*speed,rng.Range(0.2f,1.0f),std::sin(t)*speed};
        made+=pool.Spawn(l.residual,p,v,rng,g*sLow)>=0;
    }
    for(int i=0;i<c.bigDust;++i) {
        const float t=6.2831853f*(static_cast<float>(i)+rng.Unit())/static_cast<float>(c.bigDust);
        const float speed=rng.Range(6.0f,12.0f)*up;
        p[0]=e.pos[0]+std::cos(t)*0.3f*sLow;p[1]=e.pos[1]+0.2f*sLow;p[2]=e.pos[2]+std::sin(t)*0.3f*sLow;
        const float v[3]{std::cos(t)*speed,rng.Range(1.0f,3.0f),std::sin(t)*speed};
        made+=pool.Spawn(l.bigDust,p,v,rng,g*sLow)>=0;
    }
    return made;
}

// The caller's lod: full within 60 m, falling with the distance (a puff far away covers as many pixels as several
// near ones) down to a fifth; halved again while the pool is more than three quarters full.
inline float LayerLod(float distance,int live,int budget) noexcept {
    float lod=distance>60.0f ? std::clamp(60.0f/distance,0.2f,1.0f) : 1.0f;
    if(budget>0 && live*4>budget*3)lod*=0.5f;
    return lod;
}

} // namespace crew::gpufx
