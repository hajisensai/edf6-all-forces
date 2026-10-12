// The first person's arithmetic (firstperson.cpp; RE docs/fp-render-re.md §6, docs/fp-soldier-re.md §2.1), no game
// state: tools/fp_view_check.cpp runs it offline.
// Matrices are the game's: 16 floats, rows right, up, forward, position (a point p maps to p·M, row vectors).
//  - The view transform T (the viewmodel): a bone's draw pose P (world) shown in front of the camera is P·T with
//    T = R⁻¹ · Tr(hold − eye) · S(s) · C. R is the soldier's aim frame (+0x60: its pitch and yaw, the same frame the
//    stock third-person camera turns in), eye the camera's eye in R, C the camera's world matrix. Arms and weapon share
//    T, so the hand stays on the grip; hold moves the pair in the aim frame; s shrinks it about the eye (the picture is
//    the same at any s, only nearer: walls in front do not cut it).
//  - The collapse: a matrix that sends every vertex it weighs to one point (rows 0..2 zero): the body's bones that are
//    not the arms', so the body draws nothing but its arms (the triangles of the rest are degenerate, no pixel).
//  - The eye: the head bone's place in the yaw-only body frame, eased (the animation's bob filtered out).
#pragma once
#include <cmath>
#include <cstring>

namespace crew {
namespace fpview {
struct M4 { float m[16]; };

inline M4 Identity() noexcept {
    M4 r{};
    r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.0f;
    return r;
}

// a·b (row vectors: p·(a·b) = (p·a)·b).
inline M4 Mul(const M4& a,const M4& b) noexcept {
    M4 r{};
    for(int i=0;i<4;++i)
        for(int j=0;j<4;++j)
            r.m[i*4+j]=a.m[i*4]*b.m[j]+a.m[i*4+1]*b.m[4+j]+a.m[i*4+2]*b.m[8+j]+a.m[i*4+3]*b.m[12+j];
    return r;
}

inline M4 Translate(float x,float y,float z) noexcept {
    M4 r=Identity();
    r.m[12]=x;r.m[13]=y;r.m[14]=z;
    return r;
}

inline M4 Scale(float s) noexcept {
    M4 r=Identity();
    r.m[0]=r.m[5]=r.m[10]=s;
    return r;
}

// The inverse of an affine matrix (the 3x3 part inverted in full: a scaled frame inverts too). False when singular.
inline bool InverseAffine(const M4& a,M4* out) noexcept {
    const float* m=a.m;
    const float c00=m[5]*m[10]-m[6]*m[9],c01=m[6]*m[8]-m[4]*m[10],c02=m[4]*m[9]-m[5]*m[8];
    const float det=m[0]*c00+m[1]*c01+m[2]*c02;
    if(!(std::fabs(det)>1e-12f) || !std::isfinite(det))return false;
    const float k=1.0f/det;
    M4 r{};
    r.m[0]=c00*k;                  r.m[1]=(m[2]*m[9]-m[1]*m[10])*k;  r.m[2]=(m[1]*m[6]-m[2]*m[5])*k;
    r.m[4]=c01*k;                  r.m[5]=(m[0]*m[10]-m[2]*m[8])*k;  r.m[6]=(m[2]*m[4]-m[0]*m[6])*k;
    r.m[8]=c02*k;                  r.m[9]=(m[1]*m[8]-m[0]*m[9])*k;   r.m[10]=(m[0]*m[5]-m[1]*m[4])*k;
    for(int j=0;j<3;++j)r.m[12+j]=-(m[12]*r.m[j]+m[13]*r.m[4+j]+m[14]*r.m[8+j]);
    r.m[15]=1.0f;
    *out=r;
    return true;
}

inline void Point(const M4& a,const float* p,float* out) noexcept {
    for(int j=0;j<3;++j)out[j]=p[0]*a.m[j]+p[1]*a.m[4+j]+p[2]*a.m[8+j]+a.m[12+j];
}

inline bool Finite(const M4& a) noexcept {
    for(float v:a.m)if(!std::isfinite(v))return false;
    return true;
}

// The procedural motion (fp_motion.h) as a matrix in the camera's frame (x right, y up, z ahead): turned about `pivot`
// by pitch (up), yaw (right) and roll (right), then moved by the offset.
inline M4 MotionMatrix(float x,float y,float z,float pitch,float yaw,float roll,const float* pivot) noexcept {
    const float cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw),cr=std::cos(roll),sr=std::sin(roll);
    M4 rx=Identity(),ry=Identity(),rz=Identity();
    rx.m[5]=cp;rx.m[6]=-sp;rx.m[9]=sp;rx.m[10]=cp;     // ahead tips up
    ry.m[0]=cy;ry.m[2]=-sy;ry.m[8]=sy;ry.m[10]=cy;     // ahead turns right
    rz.m[0]=cr;rz.m[1]=-sr;rz.m[4]=sr;rz.m[5]=cr;      // up leans right
    const M4 turn=Mul(Mul(rz,rx),ry);
    return Mul(Mul(Translate(-pivot[0],-pivot[1],-pivot[2]),turn),Translate(pivot[0]+x,pivot[1]+y,pivot[2]+z));
}

// T (above), the motion applied in the camera's frame before the shrink. False when R is singular or the result is not
// finite (then nothing is moved).
inline bool ViewTransform(const M4& aim,const M4& camera,const float* hold,float scale,M4* out,
                          const M4& motion=Identity()) noexcept {
    M4 inv;
    if(!InverseAffine(aim,&inv))return false;
    float eye[3];
    Point(inv,camera.m+12,eye);
    const M4 t=Mul(Mul(Mul(Mul(inv,Translate(hold[0]-eye[0],hold[1]-eye[1],hold[2]-eye[2])),motion),Scale(scale)),camera);
    if(!Finite(t))return false;
    *out=t;
    return true;
}

// Every vertex weighed by it lands on `at`.
inline M4 Collapse(const float* at) noexcept {
    M4 r{};
    r.m[12]=at[0];r.m[13]=at[1];r.m[14]=at[2];r.m[15]=1.0f;
    return r;
}

// The eye's place in the body frame `body` (rows right, up, forward, position; yaw only) from the head bone's world
// position, pushed `up` and `ahead` metres, eased toward it by `ease` (0..1 a frame) from `local` (in, out). The first
// frame (`fresh`) takes it as it is.
inline void EaseEye(const M4& body,const float* head,float up,float ahead,float ease,bool fresh,float* local) noexcept {
    const float rel[3]={head[0]-body.m[12],head[1]-body.m[13],head[2]-body.m[14]};
    float want[3];
    for(int i=0;i<3;++i)want[i]=rel[0]*body.m[i*4]+rel[1]*body.m[i*4+1]+rel[2]*body.m[i*4+2];
    want[1]+=up;want[2]+=ahead;
    for(int i=0;i<3;++i)local[i]=fresh ? want[i] : local[i]+(want[i]-local[i])*ease;
}

inline void EyeWorld(const M4& body,const float* local,float* out) noexcept {
    for(int j=0;j<3;++j)out[j]=body.m[12+j]+local[0]*body.m[j]+local[1]*body.m[4+j]+local[2]*body.m[8+j];
}
}  // namespace fpview
}  // namespace crew
