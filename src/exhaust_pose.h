// The exhausts' frame as the game draws the body this frame (booster.cpp: the jets' flames and arrival smoke, the
// carrier's nacelle flames). Pure: no game memory; tools/exhaust_pose_check.cpp runs it offline.
//
// Why (the user, 2026-10-06: 「飞机的入场尾焰在飞机的很后面」): the flames are put on the body's bone records (its mesh
// bone, a nacelle) in the input step (slot 55). Those records are the pose the game made the last frame: the body has
// moved one physics step since (the gun muzzles, posed the same way, logged in JetLog over 2026-10-05/06: their offset
// along the nose falls by speed / 52..66 s, one frame of 1/60 s, for every kind: fighter 7317 samples 0..212 m/s, slope
// -0.0192 s; strike, multirole, gunship, carrier, blast alike), and the game poses and draws the body where it is now.
// A flame copied from the record therefore burned one frame of flight behind its nozzle: 3.6..4.1 m at the 215..245 m/s
// a called jet arrives at, most of the interceptor's 6 m flame.
// So the record is carried along by the body's own motion since it was posed: bone x posed^-1 x now, `posed` the body's
// matrix (vehicle +0x60) one frame ago, `now` this frame's (row vectors, affine). That is the record the game will make
// this frame; with no matrix from the frame before (the first frame, a skipped one) the record is used as it is.
#pragma once
#include <cstdint>
#include <cstring>

namespace crew::exhaust {

// a x b (row vectors: p' = p a b), both affine 4x4 (last column 0, 0, 0, 1).
inline void Mul(const float* a,const float* b,float* out) noexcept {
    float r[16];
    for(int i=0;i<4;++i)
        for(int j=0;j<3;++j)
            r[i*4+j]=a[i*4]*b[j]+a[i*4+1]*b[4+j]+a[i*4+2]*b[8+j]+(i==3 ? b[12+j] : 0.0f);
    r[3]=r[7]=r[11]=0.0f;r[15]=1.0f;
    std::memcpy(out,r,sizeof r);
}

// The inverse of an affine row-vector matrix (any invertible 3x3 part); false (out untouched) when singular.
inline bool Inverse(const float* m,float* out) noexcept {
    const float a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],g=m[8],h=m[9],k=m[10];
    const float A=e*k-f*h,B=f*g-d*k,C=d*h-e*g;
    const float det=a*A+b*B+c*C;
    if(!(det>1e-12f || det<-1e-12f))return false;
    const float s=1.0f/det;
    float r[16]={A*s,(c*h-b*k)*s,(b*f-c*e)*s,0.0f, B*s,(a*k-c*g)*s,(c*d-a*f)*s,0.0f, C*s,(b*g-a*h)*s,(a*e-b*d)*s,0.0f,
                 0.0f,0.0f,0.0f,1.0f};
    for(int j=0;j<3;++j)r[12+j]=-(m[12]*r[j]+m[13]*r[4+j]+m[14]*r[8+j]);
    std::memcpy(out,r,sizeof r);
    return true;
}

// One body's matrix over the frames: this frame's and, when it was seen the frame before, that one.
struct BodyTrack {
    std::uint64_t frame=0;   // the frame `now` is of (0: never seen)
    bool posedOk=false;      // `posed` is the frame before's
    float posed[16]{};
    float now[16]{};
};

// The body's matrix in frame `frame` (once a frame; a second call in the same frame changes nothing).
inline void Observe(BodyTrack& t,std::uint64_t frame,const float* body) noexcept {
    if(t.frame==frame)return;
    t.posedOk=t.frame!=0 && t.frame+1==frame;
    if(t.posedOk)std::memcpy(t.posed,t.now,sizeof t.now);
    std::memcpy(t.now,body,sizeof t.now);
    t.frame=frame;
}

// The bone record `bone` (its world matrix as last posed) carried to the body's pose this frame (see the top).
inline void Carry(const float* bone,const BodyTrack& t,float* out) noexcept {
    float inv[16];
    if(!t.posedOk || !Inverse(t.posed,inv)){std::memcpy(out,bone,16*sizeof(float));return;}
    float m[16];
    Mul(bone,inv,m);
    Mul(m,t.now,out);
}

}  // namespace crew::exhaust
