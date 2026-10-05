// A vehicle weapon's muzzles and the world gravity (common/edf/weapon.h).
#include "edf/weapon.h"
#include "edf/memory.h"
#include <cmath>

namespace edf {
namespace {
constexpr std::size_t kWorld=0x20B2958,kWorldPhysics=0x68,kPhysicsGravity=0x20;

bool AllFinite(const float* v,int n) noexcept {
    for(int i=0;i<n;++i)if(!std::isfinite(v[i]))return false;
    return true;
}
float Dot3(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
}  // namespace

bool MuzzleFrame(const unsigned char* weapon,const unsigned char* muzzle,float* pos,float* dir) noexcept {
    const auto bone=At<const unsigned char*>(muzzle,0);
    if(!Readable(bone,kBoneRows+0x40))return false;
    float b[4][4],l[4][4];
    std::memcpy(b,bone+kBoneRows,sizeof(b));std::memcpy(l,muzzle+kMuzzleLocal,sizeof(l));
    for(int c=0;c<3;++c) {
        dir[c]=l[2][0]*b[0][c]+l[2][1]*b[1][c]+l[2][2]*b[2][c];
        pos[c]=l[3][0]*b[0][c]+l[3][1]*b[1][c]+l[3][2]*b[2][c]+l[3][3]*b[3][c];
    }
    if(At<std::int32_t>(muzzle,kMuzzleMode)==kModeWeaponRows)std::memcpy(dir,weapon+kWeaponMatrix+0x20,12);
    const float length=std::sqrt(Dot3(dir,dir));
    return AllFinite(pos,3) && std::isfinite(length) && length>0.5f && length<2.0f;
}

bool MeanMuzzle(const unsigned char* weapon,std::uint64_t most,float* pos,float* dir) noexcept {
    const auto muzzles=At<const unsigned char*>(weapon,kMuzzles);
    const auto count=At<std::uint64_t>(weapon,kMuzzleCount);
    if(count==0 || count>most || !Readable(muzzles,count*kMuzzleStride))return false;
    std::memset(pos,0,12);std::memset(dir,0,12);
    for(std::uint64_t i=0;i<count;++i) {
        float p[3],f[3];
        if(!MuzzleFrame(weapon,muzzles+i*kMuzzleStride,p,f))return false;
        for(int c=0;c<3;++c){pos[c]+=p[c];dir[c]+=f[c];}
    }
    const float length=std::sqrt(Dot3(dir,dir));
    if(!(length>0.1f))return false;
    for(int c=0;c<3;++c){pos[c]/=static_cast<float>(count);dir[c]/=length;}
    return true;
}

bool WorldGravity(const unsigned char* image,float* g) noexcept {
    const auto world=At<const unsigned char*>(image,kWorld);
    if(!Readable(world,kWorldPhysics+8))return false;
    const auto physics=At<unsigned char*>(world,kWorldPhysics);
    if(!Readable(physics,kPhysicsGravity+8))return false;
    void* object=physics+kPhysicsGravity;
    const auto vtable=At<void* const*>(object,0);
    if(!Readable(vtable,8) || !Readable(vtable[0],1))return false;
    using GravityFn=const float*(__fastcall*)(void*);
    const float* v=reinterpret_cast<GravityFn>(vtable[0])(object);
    if(!Readable(v,12))return false;
    std::memcpy(g,v,12);
    return AllFinite(g,3);
}

namespace {
// The parabola's launch elevation through (x, y): speed v m/frame, drop a m/frame^2, the high root or the low.
bool Root(double x,double y,double v,double a,bool high,double& e) noexcept {
    const double disc=v*v*v*v-a*(a*x*x+2.0*y*v*v);
    if(disc<0.0)return false;
    e=std::atan((v*v+(high ? std::sqrt(disc) : -std::sqrt(disc)))/(a*x));
    return true;
}
}  // namespace

bool BallisticArc(double x,double y,double speed,double drop,bool high,float& elevation,float& frames) noexcept {
    if(!(speed>0.01))return false;
    if(!(drop>0.0) || x<0.01) {
        elevation=static_cast<float>(std::atan2(y,x));
        frames=static_cast<float>(std::sqrt(x*x+y*y)/speed);
        return true;
    }
    double e=0.0,n=0.0;
    for(int pass=0;pass<3;++pass) {
        if(!Root(x,y+drop*n*0.5,speed,drop,high,e))return false;
        n=x/(speed*std::cos(e));
    }
    elevation=static_cast<float>(e);
    frames=static_cast<float>(n);
    return true;
}
}  // namespace edf
