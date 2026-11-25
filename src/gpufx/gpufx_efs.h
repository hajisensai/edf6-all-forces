// The efs renderer's Texture<quad> flush (0x10DD1F0, docs/gpufx-re.md "efs") in the GPU renderer's terms: pure math,
// no engine access (tests/gpufx_sim_test.cpp checks it against the stock VS 0x2130 worked by hand).
#pragma once
#include "gpufx_sim.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crew::gpufx {

// The stock VS's per-quad inputs (Desc +0x38 / +0x48): g_instancing (t1) and g_composite (t2, texcount (0, 1): the
// composite index is the record's).
struct EfsRecord { float translate[4],scale[4],quat[4]; };
struct EfsComposite { float xrot,dist,distR,pad;float position[4];float texcoord[4];float color[4]; };
static_assert(sizeof(EfsRecord)==0x30 && sizeof(EfsComposite)==0x40,"the stock structured buffers' strides");

// The 3x3 the quad's rotated plane goes through into world space (the stock g_view_inverse, cb2[1..3], rows k = 0..2):
// the world matrix's rows (a billboard: the camera's rows, DAT_182139A90 -> +8 -> +0x10, each times the length of the
// world matrix's row k). Row vectors as the engine holds them: world = sum_k v_k * basis[k].
inline void EfsBasis(const float world[16],const float* camera,float basis[9]) noexcept {
    for(int k=0;k<3;++k) {
        if(!camera){for(int c=0;c<3;++c)basis[3*k+c]=world[4*k+c];continue;}
        const float* w=world+4*k;
        const float len=std::sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]);
        for(int c=0;c<3;++c)basis[3*k+c]=camera[4*k+c]*len;
    }
}

// The stock VS's turn: q v q* (its asm: the product with the conjugate, unnormalised), then the basis.
inline void EfsTurn(const float q[4],const float v[3],const float basis[9],float out[3]) noexcept {
    const float mx=q[3]*v[0]+q[1]*v[2]-q[2]*v[1];
    const float my=q[3]*v[1]+q[2]*v[0]-q[0]*v[2];
    const float mz=q[3]*v[2]+q[0]*v[1]-q[1]*v[0];
    const float mw=-(q[0]*v[0]+q[1]*v[1]+q[2]*v[2]);
    const float r[3]{mx*q[3]-mw*q[0]-my*q[2]+mz*q[1],my*q[3]-mw*q[1]-mz*q[0]+mx*q[2],mz*q[3]-mw*q[2]-mx*q[1]+my*q[0]};
    for(int c=0;c<3;++c)out[c]=r[0]*basis[c]+r[1]*basis[3+c]+r[2]*basis[6+c];
}

// One record as two renderer instances (Kind::efs*, read in pairs by the VS). The stock corner for a fetch (fx, fy):
// p = (rot2D(xrot) (fx, fy)) * position.zw + position.xy, times scale.xy, turned, plus the world centre. Linear in
// (fx, fy): centre' + fx * axisX + fy * axisY.
//   a.pos centre', a.size zbias, a.color colour, (a.rot, a.frame, a.soft) axisX, a.emissive the nearest distance
//   b.pos axisY, b.size 1 = depth tested (the stock LESS), b.color the uv rect (offset xy, size zw)
inline void EfsQuad(const EfsRecord& r,const EfsComposite& c,const float world[16],const float basis[9],float zbias,
                    float nearest,bool depthTest,Instance out[2]) noexcept {
    const float co=std::cos(c.xrot),si=std::sin(c.xrot);
    const float sx=r.scale[0],sy=r.scale[1];
    const float ax[3]{co*c.position[2]*sx,si*c.position[3]*sy,0.0f};
    const float ay[3]{-si*c.position[2]*sx,co*c.position[3]*sy,0.0f};
    const float b[3]{c.position[0]*sx,c.position[1]*sy,0.0f};
    float tx[3],ty[3],tb[3];
    EfsTurn(r.quat,ax,basis,tx);EfsTurn(r.quat,ay,basis,ty);EfsTurn(r.quat,b,basis,tb);
    Instance& a=out[0];Instance& o=out[1];
    a=Instance{};o=Instance{};
    for(int k=0;k<3;++k)
        a.pos[k]=r.translate[0]*world[k]+r.translate[1]*world[4+k]+r.translate[2]*world[8+k]+world[12+k]+tb[k];
    a.size=zbias;std::memcpy(a.color,c.color,sizeof(a.color));
    a.rot=tx[0];a.frame=tx[1];a.soft=tx[2];a.emissive=nearest;
    std::memcpy(o.pos,ty,sizeof(o.pos));o.size=depthTest ? 1.0f : 0.0f;std::memcpy(o.color,c.texcoord,sizeof(o.color));
}

// The stock fetch (g_fetch t0, float4: corner xy, uv zw) and the first six indices of its index buffer, read back from
// a stock draw, into the renderer's strip order (vertex v: x sign (v & 1) ? + : -, y sign (v & 2) ? - : +). Only a
// centred rectangle drawn as two triangles meeting on a diagonal is taken; anything else stays stock (false).
inline bool EfsStripFetch(const float (*fetch)[4],int fetchCount,const std::uint32_t idx[6],float out[16]) noexcept {
    int seen[4]{-1,-1,-1,-1};int n=0;
    for(int i=0;i<6;++i) {
        if(idx[i]>=static_cast<std::uint32_t>(fetchCount))return false;
        bool known=false;
        for(int k=0;k<n;++k)known=known || seen[k]==static_cast<int>(idx[i]);
        if(!known){if(n==4)return false;seen[n++]=static_cast<int>(idx[i]);}
    }
    if(n!=4)return false;
    for(int t=0;t<2;++t)
        if(idx[3*t]==idx[3*t+1] || idx[3*t]==idx[3*t+2] || idx[3*t+1]==idx[3*t+2])return false;
    const float hx=std::fabs(fetch[seen[0]][0]),hy=std::fabs(fetch[seen[0]][1]);
    if(!(hx>1e-6f) || !(hy>1e-6f))return false;
    bool filled[4]{};
    for(int k=0;k<4;++k) {
        const float* f=fetch[seen[k]];
        if(std::fabs(std::fabs(f[0])-hx)>1e-5f*hx || std::fabs(std::fabs(f[1])-hy)>1e-5f*hy)return false;
        const int v=(f[0]>0.0f ? 1 : 0)|(f[1]<0.0f ? 2 : 0);
        if(filled[v])return false;
        filled[v]=true;std::memcpy(out+4*v,f,sizeof(float)*4);
    }
    // The two shared vertices are opposite corners (both signs differ): the triangles cover the whole rectangle.
    int shared[2];int s=0;
    for(int i=0;i<3;++i)for(int j=3;j<6;++j)if(idx[i]==idx[j] && s<2)shared[s++]=static_cast<int>(idx[i]);
    if(s!=2)return false;
    const float* p=fetch[shared[0]];const float* q=fetch[shared[1]];
    return (p[0]>0.0f)!=(q[0]>0.0f) && (p[1]>0.0f)!=(q[1]>0.0f);
}

}  // namespace crew::gpufx
