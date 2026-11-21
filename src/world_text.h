// World text (docs/world-text-re.md §5): a label of the HUD standing in the world at its anchor, drawn by the game's own
// text draw (0x113C520) with a billboard world matrix as its `m` and the camera's viewProj written into the renderer's
// projection (core+0x2F0, the view core+0x330 an identity), so the GPU puts every glyph's corners through the real
// perspective. The billboard is the camera's right / down axes at the anchor: its plane is parallel to the screen, so
// from any view (the map's look down too) it faces the camera and reads, and its size on the screen is exactly its local
// size times one scale (the depth is the same over the whole plane). Its depth sets that scale: the label is its 2D size
// (the font scale the caller asked for) at `ref` m and grows / shrinks with the perspective, held to [kFarLeast,
// kNearMost] of that so it never shrinks past reading nor fills the screen up close.
//
// The font scale the glyphs are rasterized at stays on the glyph cache's grid (docs/hud-re.md §2.1, kTextScaleStep): the
// grid point nearest the size wanted, the rest of the size (within half a step) in `m`.
//
// Conventions (docs/world-text-re.md §4): row-major 4x4, row vectors (p' = p x M, the 4th row the translation); the
// camera's viewProj as the plugin has it (docs/hud-re.md §0): clip[k] = x*vp[k] + y*vp[4+k] + z*vp[8+k] + vp[12+k]; the
// screen in viewport pixels, x right, y down. The text's local space: x right, y down, a unit a glyph pixel at its font
// scale, z 0.
//
// Pure (no EDF.dll, no Windows): tools/world_text_check.cpp checks it offline.
#pragma once
#include <cmath>

namespace crew::worldtext {
// The label's size on the screen against its 2D size: at least this far off, at most this near.
constexpr float kFarLeast=0.6f,kNearMost=1.5f;
// The depth (m) a label is its 2D size at in the player's own view; the map's view uses its camera's distance instead.
constexpr float kViewRef=30.0f;
// The glow: the outline in the label's own colour, lightened toward white this much, at this share of its alpha, this wide
// (the plain HUD text's black outline is 1.7, the name tags' 0x804FB3).
constexpr float kGlowLighten=0.35f,kGlowAlpha=0.5f,kGlowWidth=2.4f;
// The shimmer: the alpha between 1 - kShimmer and 1 over kShimmerMs.
constexpr float kShimmer=0.12f;
constexpr unsigned long long kShimmerMs=1400;

inline float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

// The camera at a point: its right and down axes and its forward (world, unit), the point's depth (the clip w: metres in
// front of the camera) and how many screen pixels a metre across (pxX) and down (pxY) is in the plane through the point
// facing the camera. False: the point behind the camera (or on it), or a viewProj with no perspective (no depth in its
// w column) or none at all.
struct Frame { float right[3],down[3],forward[3]; float depth,pxX,pxY; };
inline bool FrameAt(const float* vp,float width,float height,const float* p,Frame* f) noexcept {
    float c0[3],c1[3],c3[3];
    for(int i=0;i<3;++i){c0[i]=vp[i*4];c1[i]=vp[i*4+1];c3[i]=vp[i*4+3];}
    const float l3=std::sqrt(Dot(c3,c3));
    if(!(l3>1e-6f) || !(width>0.0f) || !(height>0.0f))return false;
    // The forward: the w column (the view's z, times the projection's w scale). The right: the x column less any of the
    // forward (an off-centre or jittered projection's), the up: the y column less both.
    float fw[3],r[3],u[3];
    for(int i=0;i<3;++i)fw[i]=c3[i]/l3;
    const float r0=Dot(c0,fw);
    for(int i=0;i<3;++i)r[i]=c0[i]-r0*fw[i];
    const float lr=std::sqrt(Dot(r,r));
    if(!(lr>1e-6f))return false;
    for(float& v:r)v/=lr;
    const float u0=Dot(c1,fw),u1=Dot(c1,r);
    for(int i=0;i<3;++i)u[i]=c1[i]-u0*fw[i]-u1*r[i];
    const float lu=std::sqrt(Dot(u,u));
    if(!(lu>1e-6f))return false;
    for(float& v:u)v/=lu;
    const float w=Dot(p,c3)+vp[15];
    if(!(w>1e-4f) || !std::isfinite(w))return false;
    for(int i=0;i<3;++i){f->right[i]=r[i];f->down[i]=-u[i];f->forward[i]=fw[i];}
    f->depth=w;
    // A metre along the right moves clip x by c0.r and leaves clip w (c3 is along the forward): sx = W/2 (1 + x/w).
    f->pxX=0.5f*width*Dot(c0,r)/w;
    f->pxY=0.5f*height*Dot(c1,u)/w;
    return f->pxX>0.0f && f->pxY>0.0f && std::isfinite(f->pxX) && std::isfinite(f->pxY);
}

// The label's screen size against its 2D size at `depth` m, its 2D size at `ref` m: the perspective's, held to
// [kFarLeast, kNearMost].
inline float SizeFactor(float depth,float ref) noexcept {
    if(!(depth>0.0f) || !(ref>0.0f))return kNearMost;
    const float k=ref/depth;
    return k<kFarLeast ? kFarLeast : k>kNearMost ? kNearMost : k;
}

// A font scale on the glyph cache's grid (docs/hud-re.md §2.1): the nearest grid point, one step at least.
inline float Snap(float scale,float step) noexcept {
    const float snapped=std::round(scale/step)*step;
    return snapped>step ? snapped : step;
}

// The font a label is rasterized at (`fd`, on the grid) and how many screen pixels one of its glyph pixels covers
// (`unitPx`: the rest of the size wanted, nominal x factor = fd x unitPx).
struct Fit { float fd,unitPx; };
inline Fit FontFor(float nominal,float factor,float step) noexcept {
    const float want=nominal*factor;
    const float fd=Snap(want,step);
    return Fit{fd,want>0.0f ? want/fd : 1.0f};
}

// Where a label `w` x `h` px (screen) goes: its anchor at (ax, ay) on the screen, (dx, dy) px from it; `align` 0 its left
// there, 1 its middle, 2 its right; its height centred. The top left.
inline void Place(float ax,float ay,float dx,float dy,int align,float w,float h,float* left,float* top) noexcept {
    *left=ax+dx-(align==1 ? w*0.5f : align==2 ? w : 0.0f);
    *top=ay+dy-h*0.5f;
}

// The billboard: the text's local space to the world, for a label whose top left is at (left, top) on the screen, its
// anchor (world `anchor`) at (ax, ay), a glyph pixel `unitPx` screen pixels. Row 0 the camera's right, row 1 its down
// (the local y is down), each a glyph pixel's metres in the plane through the anchor; row 2 its forward; row 3 the top
// left's world point (the anchor moved by the screen offset, in that plane).
inline void Billboard(const Frame& f,const float* anchor,float ax,float ay,float left,float top,float unitPx,float* m) noexcept {
    const float mx=unitPx/f.pxX,my=unitPx/f.pxY,ox=(left-ax)/f.pxX,oy=(top-ay)/f.pxY;
    for(int i=0;i<3;++i) {
        m[i]=f.right[i]*mx;
        m[4+i]=f.down[i]*my;
        m[8+i]=f.forward[i];
        m[12+i]=anchor[i]+f.right[i]*ox+f.down[i]*oy;
    }
    m[3]=m[7]=m[11]=0.0f;m[15]=1.0f;
}

// A world point on the screen through `vp` (viewport pixels, y down); false behind the camera.
inline bool ToScreen(const float* vp,float width,float height,const float* p,float* sx,float* sy) noexcept {
    float c[4];
    for(int k=0;k<4;++k)c[k]=p[0]*vp[k]+p[1]*vp[4+k]+p[2]*vp[8+k]+vp[12+k];
    if(!(c[3]>1e-6f))return false;
    *sx=0.5f*width*(1.0f+c[0]/c[3]);*sy=0.5f*height*(1.0f-c[1]/c[3]);
    return true;
}
// The local point (x, y) of a text drawn at `m` (the draw's world), in the world.
inline void Local(const float* m,float x,float y,float* out) noexcept {
    for(int i=0;i<3;++i)out[i]=x*m[i]+y*m[4+i]+m[12+i];
}

// The glow's colour (the outline): the label's colour lightened, at a share of its alpha.
inline void Glow(const float* rgba,float* out) noexcept {
    for(int i=0;i<3;++i)out[i]=rgba[i]+(1.0f-rgba[i])*kGlowLighten;
    out[3]=rgba[3]*kGlowAlpha;
}
// The shimmer's alpha factor at wall ms `ms` for a label of phase `phase` (0..1: labels apart do not pulse together).
inline float Shimmer(unsigned long long ms,float phase) noexcept {
    const float t=static_cast<float>(ms%kShimmerMs)/static_cast<float>(kShimmerMs)+phase;
    return 1.0f-kShimmer*0.5f*(1.0f+std::sin(t*6.28318531f));
}
// A label's phase from its anchor (a stable 0..1 a place).
inline float PhaseOf(const float* anchor) noexcept {
    const float v=std::fabs(anchor[0]*0.0137f+anchor[2]*0.0071f+anchor[1]*0.031f);
    return v-std::floor(v);
}
}  // namespace crew::worldtext
