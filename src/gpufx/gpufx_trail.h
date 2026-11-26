// The efs RenderInfo trails (ExtLine / LocusExtendLine / EmittingExtendLine, docs/gpufx-re.md "trails") in the GPU
// renderer's terms: pure math, no engine access (tests/gpufx_sim_test.cpp checks it against the stock vertex and
// index streams built the way 0x10C2240 / 0x10C1E20 build them).
//
// The stock draws one strip per call: ExtLine (0x10C2240) 4 vertices a point pair, the pairs stitched by two
// degenerate indices; Locus (0x10C1E20, LocusExtendLine and EmittingExtendLine) 2 vertices a point, 512 points a draw,
// the next draw restarting at the last point. Every non-degenerate triangle of both belongs to one quad (a, b, c, d) in
// strip order with the triangles (a, b, c) and (c, b, d) (the strip's even and odd triangles, winding included), so
// ours keeps the quads and draws them as a triangle list (TrailIndex), many calls in one draw.
#pragma once
#include <cstdint>

namespace crew::gpufx {

// One point as the slot functions build them (0x30 bytes): position, colour, the strip's v, the half width.
struct TrailPoint { float pos[3];float pad0;float color[4];float v;float halfWidth;float pad1[2]; };
// One vertex as the stock VS reads it (stride 0x34): POSITION, COLOR (+0x0C), UV_SIZE (+0x1C: u, v, the signed half
// width), FRONT (+0x28: the strip's direction there).
struct TrailVertex { float pos[3];float color[4];float uvSize[3];float front[3]; };
struct TrailQuad { TrailVertex v[4]; };   // a, b, c, d in strip order
static_assert(sizeof(TrailPoint)==0x30 && sizeof(TrailVertex)==0x34 && sizeof(TrailQuad)==4*0x34,"the stock strides");

// The stock's transform goes into the VS's xgl_transform as rows (m[r], m[4+r], m[8+r], m[12+r]) (0x0FDDD0): a point
// p lands at p.x m[0..2] + p.y m[4..6] + p.z m[8..10] + m[12..14], FRONT through the same 3x3. Ours applies it here
// (its xgl_transform is the identity) so calls with different transforms share one draw.
inline void TrailTransform(const float p[3],const float m[16],float out[3]) noexcept {
    for(int r=0;r<3;++r)out[r]=p[0]*m[r]+p[1]*m[4+r]+p[2]*m[8+r]+m[12+r];
}
inline void TrailTurn(const float d[3],const float m[16],float out[3]) noexcept {
    for(int r=0;r<3;++r)out[r]=d[0]*m[r]+d[1]*m[4+r]+d[2]*m[8+r];
}

// A point's two vertices: A (u 0, +half width) and B (u 1, -half width), the front given (in the point's space).
inline void TrailPair(const TrailPoint& p,const float front[3],const float m[16],TrailVertex& a,TrailVertex& b) noexcept {
    TrailTransform(p.pos,m,a.pos);
    for(int k=0;k<4;++k)a.color[k]=p.color[k];
    a.uvSize[0]=0.0f;a.uvSize[1]=p.v;a.uvSize[2]=p.halfWidth;
    TrailTurn(front,m,a.front);
    b=a;
    b.uvSize[0]=1.0f;b.uvSize[2]=-p.halfWidth;
}

// ExtLine (0x10C2240): the points in pairs (an odd last point dropped), each pair one quad p0A p0B p1A p1B, front
// p1 - p0 for all four. Returns the quads written (count / 2).
inline int ExtLineQuads(const TrailPoint* p,unsigned count,const float m[16],TrailQuad* out) noexcept {
    const unsigned pairs=count/2u;
    for(unsigned i=0;i<pairs;++i) {
        const TrailPoint& p0=p[2u*i];const TrailPoint& p1=p[2u*i+1u];
        const float f[3]{p1.pos[0]-p0.pos[0],p1.pos[1]-p0.pos[1],p1.pos[2]-p0.pos[2]};
        TrailPair(p0,f,m,out[i].v[0],out[i].v[1]);
        TrailPair(p1,f,m,out[i].v[2],out[i].v[3]);
    }
    return static_cast<int>(pairs);
}

// Locus (0x10C1E20): one strip through all the points, front next - current at the first point, current - previous
// at the last, next - previous between (the call's first and last, not a 512-point draw's: a restarted draw's first
// point is a middle one). Returns the quads written (count - 1; none under two points).
inline int LocusQuads(const TrailPoint* p,unsigned count,const float m[16],TrailQuad* out) noexcept {
    if(count<2u)return 0;
    auto front=[&](unsigned i,float f[3]) noexcept {
        const TrailPoint& lo=i==0u ? p[0] : p[i-1u];
        const TrailPoint& hi=i+1u==count ? p[i] : p[i+1u];
        for(int k=0;k<3;++k)f[k]=hi.pos[k]-lo.pos[k];
    };
    TrailVertex a,b;
    float f[3];
    front(0u,f);TrailPair(p[0],f,m,a,b);
    for(unsigned i=1;i<count;++i) {
        TrailQuad& q=out[i-1u];
        q.v[0]=a;q.v[1]=b;
        front(i,f);TrailPair(p[i],f,m,q.v[2],q.v[3]);
        a=q.v[2];b=q.v[3];
    }
    return static_cast<int>(count-1u);
}

// The quads' triangle list: quad q's six indices (its corners a b c, c b d), relative to the draw's base vertex.
constexpr int kTrailIndices=6;
inline std::uint16_t TrailIndex(int i) noexcept {
    constexpr std::uint16_t kCorner[kTrailIndices]{0,1,2,2,1,3};
    return static_cast<std::uint16_t>(4*(i/kTrailIndices)+kCorner[i%kTrailIndices]);
}

}  // namespace crew::gpufx
