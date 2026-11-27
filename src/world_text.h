// World text (docs/world-text-re.md §5 / §6): a label of the HUD standing in the world as a hologram plate, drawn by the
// game's own text draw (0x113C520) with a world matrix as its `m` and the camera's viewProj written into the renderer's
// projection (core+0x2F0, the view core+0x330 an identity), so the GPU puts every glyph's corners through the real
// perspective.
//
// The user, 2026-11-21: 「固定朝向的，但是有厚度吧」. So the plate does not turn with the camera (a billboard parallel to the
// screen looks exactly like the scaled 2D text it replaced): it faces the camera square on as the camera was when the
// label first showed, yaw and pitch both (FacingCamera: its up the world's up as far as the view allows, no roll), and
// keeps that (by the label's key in a Facings cache: the map's view and the player's each their own); or it lies on the
// ground north up (Ground: the map's grid labels). An upright plate under the map's look down (30-88 degrees) squashed to
// a line, and growing it for that stretched it sideways: a plate made square on reads wherever it was made. Its thickness is the same text drawn again in layers
// behind its face along its normal (real metres apart: the perspective shows their sides), the ones further back darker
// and fainter, the face last in its own colour with a thin dark edge round it. Seen from behind, the face is the side nearest the
// camera and the text runs mirrored along the plate, so it still reads left to right (the plate itself never moves).
//
// Its size is in metres (the perspective makes it larger near, smaller far): the label's 2D size at `ref` m in the plane
// facing the camera, and past that, for reading, its metres grown with the depth so its height on the screen stays at
// least kFarLeast of the 2D label's (and at most kNearMost up close), as the plate would be seen square on (one seen at a
// slant shows shorter or narrower, never grown for it). The font scale the glyphs are rasterized at stays the
// label's nominal one snapped to the glyph cache's grid (docs/hud-re.md §2.1, kTextScaleStep), whatever the depth: every
// other factor is in `m` (a font scale changing with the depth would move through the cache's sizes as the label moves).
//
// Conventions (docs/world-text-re.md §4): row-major 4x4, row vectors (p' = p x M, the 4th row the translation); the
// camera's viewProj as the plugin has it (docs/hud-re.md §0): clip[k] = x*vp[k] + y*vp[4+k] + z*vp[8+k] + vp[12+k]; the
// screen in viewport pixels, x right, y down; the world's Y up, +Z north. The text's local space: x right, y down, a unit
// a glyph pixel at its font scale, z 0. The game's handedness is read off the viewProj (Frame::hand), never assumed.
//
// Pure (no EDF.dll, no Windows): tools/world_text_check.cpp checks it offline.
#pragma once
#include "screen_view.h"
#include <cmath>
#include <cstdint>

namespace crew::worldtext {
// The label's height on the screen against its 2D height: at least this far off, at most this near.
constexpr float kFarLeast=0.85f,kNearMost=1.8f;
// Seen nearly edge-on (the cosine between its normal and the way to the eye within this), the plate keeps the side it
// showed last (its face does not jump between its two sides as the camera passes its edge).
constexpr float kFlipBand=0.1f;
// The depth (m) a label is its 2D size at in the player's own view; the map's view uses its camera's distance instead.
constexpr float kViewRef=30.0f;
// The edge: a thin dark outline round the face, at this share of its alpha, this wide (the plain HUD text's black outline
// is 1.7, the name tags' 0x804FB3). The user, 2026-11-27: 「文字描边发光有点看不清」: a glow in the label's own colour,
// lightened, blurred the glyphs into the plates behind them; a dark edge keeps them crisp on any ground.
constexpr float kEdgeAlpha=0.9f,kEdgeWidth=1.6f;
alignas(16) constexpr float kEdgeRgb[3]={0.02f,0.03f,0.04f};
// The shimmer: the alpha between 1 - kShimmer and 1 over kShimmerMs.
constexpr float kShimmer=0.12f;
constexpr unsigned long long kShimmerMs=1400;
// The thickness: the layers behind the face span this share of the glyph height; at most / least this many layers.
constexpr float kThickness=0.2f;
constexpr int kLayersMost=8,kLayersLeast=2;
// The layers behind the face: the first one's colour this lit and this opaque, the last one's this share of that (dark:
// they read as the plate's side, a shadow, not a second blurred copy of the face).
constexpr float kBackLit=0.3f,kBackAlpha=0.8f,kBackFall=0.5f;
// The facings kept (a label's key to its plate's orientation): this many at most, one not asked for this long forgotten.
constexpr int kFacingSlots=128;
constexpr unsigned long long kForgetMs=5000;

inline float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline void Cross(const float* a,const float* b,float* o) noexcept {
    o[0]=a[1]*b[2]-a[2]*b[1];o[1]=a[2]*b[0]-a[0]*b[2];o[2]=a[0]*b[1]-a[1]*b[0];
}

// The camera at a point: its right and down axes and its forward (world, unit), the point's depth (the clip w: metres in
// front of the camera), how many screen pixels a metre across (pxX) and down (pxY) is in the plane through the point
// facing the camera, and the screen's handedness (`hand`: +1 when right x down points at the camera, as a D3D view with
// x right, y up, z ahead; -1 the other way). False: the point behind the camera (or on it), or a viewProj with no
// perspective (no depth in its w column) or none at all.
struct Frame { float right[3],down[3],forward[3]; float depth,pxX,pxY,hand; };
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
    float rd[3];Cross(f->right,f->down,rd);
    f->hand=Dot(rd,f->forward)<0.0f ? 1.0f : -1.0f;
    return f->pxX>0.0f && f->pxY>0.0f && std::isfinite(f->pxX) && std::isfinite(f->pxY);
}

// The camera's eye: the point the viewProj takes to clip x = y = w = 0. False: no such single point.
inline bool EyeOf(const float* vp,float* eye) noexcept {
    // Rows: clip x, y and w of a point less their constants: a[k] . p = -vp[12+k].
    const float a[3][3]={{vp[0],vp[4],vp[8]},{vp[1],vp[5],vp[9]},{vp[3],vp[7],vp[11]}};
    const float b[3]={-vp[12],-vp[13],-vp[15]};
    const float det=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])
                   +a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
    if(!(std::fabs(det)>1e-12f) || !std::isfinite(det))return false;
    for(int c=0;c<3;++c) {   // Cramer: column c replaced by b
        float m[3][3];
        for(int r=0;r<3;++r)for(int k=0;k<3;++k)m[r][k]=k==c ? b[r] : a[r][k];
        eye[c]=(m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
               +m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]))/det;
    }
    return std::isfinite(eye[0]) && std::isfinite(eye[1]) && std::isfinite(eye[2]);
}

// A plate's orientation (world, unit): its text's right and down, its normal out of its face (right x down along the
// normal with the screen's handedness: seen from the normal's side it reads as the screen would).
struct Standee { float right[3],down[3],normal[3]; };
// Square on to the camera of `f`: its normal back along the camera's forward, its right the camera's right made level
// (no roll: the world's up as its up as far as the view's pitch allows), its down to read with the screen's handedness.
// False: no right (a camera rolled onto its side looking straight up or down).
inline bool FacingCamera(const Frame& f,Standee* s) noexcept {
    const float n[3]={-f.forward[0],-f.forward[1],-f.forward[2]};
    float r[3]={f.right[0],0.0f,f.right[2]};
    const float rn=Dot(r,n);
    for(int i=0;i<3;++i)r[i]-=rn*n[i];
    float l=std::sqrt(Dot(r,r));
    if(!(l>1e-3f)){for(int i=0;i<3;++i)r[i]=f.right[i];l=1.0f;}
    for(float& v:r)v/=l;
    // right x down . n = hand: down = hand (n x right) (right x (n x right) = n for unit, square vectors).
    float d[3];Cross(n,r,d);
    for(int i=0;i<3;++i){s->right[i]=r[i];s->down[i]=d[i]*f.hand;s->normal[i]=n[i];}
    return true;
}
// Lying on the ground, north up: its down south (-Z), its normal up, its right east or west as the screen's handedness
// makes it read from above (right x south . up = right.x, so right.x = hand).
inline Standee Ground(float hand) noexcept {
    Standee s{};
    s.right[0]=hand>0.0f ? 1.0f : -1.0f;
    s.down[2]=-1.0f;
    s.normal[1]=1.0f;
    return s;
}
// Whether the camera at `eye` is behind the plate at `at` (on its normal's other side).
inline bool Behind(const Standee& s,const float* at,const float* eye) noexcept {
    const float d[3]={eye[0]-at[0],eye[1]-at[1],eye[2]-at[2]};
    return Dot(s.normal,d)<0.0f;
}
// The same with the side it showed last (`was`) kept while the way to the eye is within kFlipBand of its edge.
inline bool BehindHeld(const Standee& s,const float* at,const float* eye,bool was) noexcept {
    const float d[3]={eye[0]-at[0],eye[1]-at[1],eye[2]-at[2]};
    const float l=std::sqrt(Dot(d,d));
    if(!(l>1e-6f))return was;
    const float c=Dot(s.normal,d)/l;
    return c<-kFlipBand ? true : c>kFlipBand ? false : was;
}

// The label's size against its 2D size at `depth` m, its 2D size at `ref` m (both square on): the perspective's (its
// metres fixed) while its height square on stays within [kFarLeast, kNearMost] of the 2D height, else grown / shrunk to
// the bound. A plate seen at a slant is not grown for it: it shows narrower or shorter, never wider than square on.
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

// The font a label is rasterized at (`fd`: its nominal scale on the grid, never the depth's) and how many screen pixels
// (in the plane facing the camera) one of its glyph pixels covers (`unitPx`: nominal x factor = fd x unitPx).
struct Fit { float fd,unitPx; };
inline Fit FontFor(float nominal,float factor,float step) noexcept {
    const float fd=Snap(nominal,step);
    return Fit{fd,nominal*factor/fd};
}

// Where a label `w` x `h` px (screen) goes: its anchor at (ax, ay) on the screen, (dx, dy) px from it; `align` 0 its left
// there, 1 its middle, 2 its right; its height centred. The top left. (The 2D labels' layout; a plate's bounds.)
inline void Place(float ax,float ay,float dx,float dy,int align,float w,float h,float* left,float* top) noexcept {
    *left=ax+dx-(align==1 ? w*0.5f : align==2 ? w : 0.0f);
    *top=ay+dy-h*0.5f;
}

// A plate laid out: its face's top left (the local (0, 0), world), a glyph pixel along its text's right and down, its
// normal toward the camera's side, one layer's step away from the camera (world), whether the camera is behind it.
struct Plate { float origin[3],rowX[3],rowY[3],normal[3],away[3]; bool back; };
// The plate of a label `w` x `h` glyph px, `mpp` metres a glyph pixel, at `at` (its height's middle; `align` as Place's:
// 0 its text starts there, 1 centred, 2 ends there, along the plate's right), `layers` deep (kThickness of its height
// in all), seen from behind (`back`) or not. From behind the face is the back layer (nearest the camera) and the text
// runs along -right from the other end of the same span: the same plate, read left to right.
inline void Lay(const Standee& s,const float* at,float w,float h,int align,float mpp,int layers,bool back,Plate* p) noexcept {
    const float xl=align==1 ? -w*0.5f : align==2 ? -w : 0.0f,top=-h*0.5f;
    const float thick=kThickness*h*mpp,step=layers>1 ? thick/static_cast<float>(layers-1) : 0.0f;
    const float sx=back ? -1.0f : 1.0f,startX=back ? xl+w : xl;
    for(int i=0;i<3;++i) {
        p->rowX[i]=s.right[i]*mpp*sx;
        p->rowY[i]=s.down[i]*mpp;
        p->normal[i]=s.normal[i]*sx;
        p->away[i]=-p->normal[i]*step;
        p->origin[i]=at[i]+s.right[i]*startX*mpp+s.down[i]*top*mpp-(back ? s.normal[i]*thick : 0.0f);
    }
    p->back=back;
}
// Layer `k` of a plate's `m` (0 the face, nearest the camera; each further one a step away). Draw them from the last to
// the face: with no depth test the nearer drawn over the further.
inline void LayerMatrix(const Plate& p,int k,float* m) noexcept {
    const float kk=static_cast<float>(k);
    for(int i=0;i<3;++i) {
        m[i]=p.rowX[i];
        m[4+i]=p.rowY[i];
        m[8+i]=p.normal[i];
        m[12+i]=p.origin[i]+p.away[i]*kk;
    }
    m[3]=m[7]=m[11]=0.0f;m[15]=1.0f;
}
// Layer `k`'s colour of `layers` for a label of `rgba`: the face its own, the ones behind darker and fainter going back.
inline void LayerColour(const float* rgba,int k,int layers,float* out) noexcept {
    if(k<=0 || layers<2){for(int i=0;i<4;++i)out[i]=rgba[i];return;}
    const float t=layers>2 ? static_cast<float>(k-1)/static_cast<float>(layers-2) : 0.0f;
    const float lit=kBackLit*(1.0f-kBackFall*t),alpha=kBackAlpha*(1.0f-kBackFall*t);
    for(int i=0;i<3;++i)out[i]=rgba[i]*lit;
    out[3]=rgba[3]*alpha;
}
// How many layers a plate at `depth` m gets with `labels` plates in the frame: fewer far off, fewer with many.
inline int LayersFor(float depth,int labels) noexcept {
    int n=depth<80.0f ? kLayersMost : depth<250.0f ? 6 : 4;
    const int most=labels>96 ? kLayersLeast : labels>40 ? 3 : labels>16 ? 5 : kLayersMost;
    n=n<most ? n : most;
    return n<kLayersLeast ? kLayersLeast : n;
}

// The facings: a label's key to its plate's orientation, made the first time it is asked for (the caller's `fresh`: the
// camera square on then) and kept while it is asked for again within kForgetMs, with the side it showed last (`back`,
// BehindHeld). At most kFacingSlots: a new key takes a free or forgotten slot, else the one asked for in the oldest
// frame; when every slot was asked for in this frame already (more labels than slots), none: the caller draws that
// one square on to the camera now, uncached (no slot given up and made again every frame). The draw thread's own.
struct FacingSlot { std::uint64_t key; unsigned long long seen,frame; Standee s; bool used,back; };
struct Facings { FacingSlot slot[kFacingSlots]; };
inline FacingSlot* FacingOf(Facings& c,std::uint64_t key,unsigned long long now,unsigned long long frame,const Standee& fresh) noexcept {
    FacingSlot* pick=nullptr;
    const auto stale=[now](const FacingSlot& f){ return !f.used || now<f.seen || now-f.seen>kForgetMs; };
    for(FacingSlot& f:c.slot)if(f.used && f.key==key){pick=&f;break;}
    if(pick && stale(*pick)){pick->s=fresh;pick->back=false;}   // back after long away: a new label there
    if(!pick) {
        for(FacingSlot& f:c.slot)if(stale(f)){pick=&f;break;}
        if(!pick) {
            pick=&c.slot[0];
            for(FacingSlot& f:c.slot)if(f.frame<pick->frame)pick=&f;
            if(pick->frame==frame)return nullptr;
        }
        pick->key=key;pick->used=true;pick->s=fresh;pick->back=false;
    }
    pick->seen=now;pick->frame=frame;
    return pick;
}
// How many facings are kept now (not forgotten).
inline int FacingsKept(const Facings& c,unsigned long long now) noexcept {
    int n=0;
    for(const FacingSlot& f:c.slot)if(f.used && now>=f.seen && now-f.seen<=kForgetMs)++n;
    return n;
}
// A label's key: its kind (1..255) and what it is of (an object's identity, or Cell's of its place). Never 0.
inline std::uint64_t Key(unsigned kind,std::uint64_t of) noexcept {
    std::uint64_t h=of*0x9E3779B97F4A7C15ull;
    h^=h>>29;
    return (static_cast<std::uint64_t>(kind&0xFFu)<<56) ^ (h&0x00FFFFFFFFFFFFFFull) ^ 0x8000000000000000ull;
}
// A place quantized to `cell` m (a key's for a label of a place that does not move).
inline std::uint64_t Cell(const float* p,float cell) noexcept {
    std::uint64_t v=0;
    for(int i=0;i<3;++i) {
        const long long q=static_cast<long long>(std::floor(p[i]/cell));
        v=(v<<21)^(static_cast<std::uint64_t>(q)&0x1FFFFFull);
    }
    return v;
}

// A world point on the screen through `vp` (viewport pixels, y down); false behind the camera.
inline bool ToScreen(const float* vp,float width,float height,const float* p,float* sx,float* sy) noexcept {
    float c[4];
    screenview::Clip(vp,p,1.0f,c);
    if(!(c[3]>1e-6f))return false;
    screenview::Pixel(c[0]/c[3],c[1]/c[3],width,height,sx,sy);
    return true;
}
// A world point's clip w (its depth) through `vp`.
inline float DepthOf(const float* vp,const float* p) noexcept { return p[0]*vp[3]+p[1]*vp[7]+p[2]*vp[11]+vp[15]; }
// The local point (x, y) of a text drawn at `m` (the draw's world), in the world.
inline void Local(const float* m,float x,float y,float* out) noexcept {
    for(int i=0;i<3;++i)out[i]=x*m[i]+y*m[4+i]+m[12+i];
}

// The edge's colour (the face's outline): near black, at a share of the label's alpha.
inline void Edge(const float* rgba,float* out) noexcept {
    for(int i=0;i<3;++i)out[i]=kEdgeRgb[i];
    out[3]=rgba[3]*kEdgeAlpha;
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
