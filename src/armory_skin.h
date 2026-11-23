#pragma once
// 整备 screen skin (docs/plan-2026-11-24-ui-rewrite.md stage 2): every element's geometry as quads and corner colours,
// pure (no D3D) so the offline test (tests/armory_skin_test.cpp) checks shapes, colours and the layout, and
// src/armory_draw.cpp only hands the quads to uigpu::Painter::QuadColours. Design space 1920 x 1080 (the stock UI's).
// Corner order is the Painter's: top-left, top-right, bottom-left, bottom-right (a triangle strip: TL TR BL / TR BL BR).
#include <cmath>
#include <cstdint>

namespace crew::armory::skin {

constexpr float kWidth=1920.0f, kHeight=1080.0f;

// Palette (0xAARRGGBB). The diamond pair and the cursor orange are the design's (plan §2).
constexpr std::uint32_t kDiamondLight=0xFF423C29u, kDiamondDark=0xFF332E1Eu;
constexpr std::uint32_t kCursor=0xFFF07A10u;
constexpr std::uint32_t kBarTop=0xFFD8C69Cu, kBarBottom=0xFF9C8658u, kBarCap=0xFF4A4232u;
constexpr std::uint32_t kPanel=0xE0141310u, kBracket=0xFFC4C8CCu;
constexpr std::uint32_t kMetalTop=0xFF6E7378u, kMetalBottom=0xFF34383Cu, kMetalEdge=0xFF9AA0A6u;
constexpr std::uint32_t kBeigeTop=0xFFEEE0B8u, kBeigeBottom=0xFFC8B68Au;
constexpr std::uint32_t kCell=0xF0222019u, kCellEdge=0xFF5A5440u;
constexpr std::uint32_t kText=0xFFEDE6D2u, kTextDark=0xFF26221Au, kTextDim=0xFF9A927Cu;
constexpr float kDiamondPeriod=84.0f;      // one light diamond every 84 units each way, centred on (84 i, 84 j)
constexpr float kBracketLength=18.0f, kBracketWidth=3.0f;
constexpr float kButtonSlant=12.0f;        // a button's top edge shifted right of its bottom edge
constexpr float kCellCut=14.0f;            // the label cell's cut top-right corner
constexpr float kCursorWidth=3.0f;

struct Rect {
    float x0=0,y0=0,x1=0,y1=0;
    constexpr bool Contains(float x,float y) const noexcept { return x>=x0 && x<x1 && y>=y0 && y<y1; }
    constexpr float W() const noexcept { return x1-x0; }
    constexpr float H() const noexcept { return y1-y0; }
};

struct Quad {
    float xy[4][2];
    std::uint32_t argb[4];
};

// A fixed list of quads (no allocation: built on the UI thread or the render thread alike). Add past kMost is dropped
// and counted (the test asserts no element overflows).
struct Quads {
    static constexpr int kMost=1024;
    Quad q[kMost];
    int n=0;
    int dropped=0;
    void Add(float x0,float y0,float x1,float y1,float x2,float y2,float x3,float y3,std::uint32_t c0,std::uint32_t c1,
             std::uint32_t c2,std::uint32_t c3) noexcept {
        if(n>=kMost){++dropped;return;}
        Quad& o=q[n++];
        o.xy[0][0]=x0;o.xy[0][1]=y0;o.xy[1][0]=x1;o.xy[1][1]=y1;o.xy[2][0]=x2;o.xy[2][1]=y2;o.xy[3][0]=x3;o.xy[3][1]=y3;
        o.argb[0]=c0;o.argb[1]=c1;o.argb[2]=c2;o.argb[3]=c3;
    }
    void Box(const Rect& r,std::uint32_t c) noexcept { Add(r.x0,r.y0,r.x1,r.y0,r.x0,r.y1,r.x1,r.y1,c,c,c,c); }
    void Vertical(const Rect& r,std::uint32_t top,std::uint32_t bottom) noexcept {
        Add(r.x0,r.y0,r.x1,r.y0,r.x0,r.y1,r.x1,r.y1,top,top,bottom,bottom);
    }
};

// ---- layout (design units) -------------------------------------------------------------------------------------------
namespace layout {
constexpr Rect kTitle{60,24,1860,92};          // title bar: screen name, tabs, preset chips
constexpr Rect kHint{60,992,1860,1044};        // hint bar: the keys of the current focus
constexpr Rect kList{110,112,600,972};         // left column: the page's list
constexpr Rect kCentre{620,112,1290,972};      // centre column: the selected item
constexpr Rect kDetails{1310,112,1810,972};    // right column: its fields
constexpr float kRowHeight=52.0f, kRowGap=6.0f, kPad=18.0f;
constexpr float kTabX=330.0f, kTabWidth=168.0f, kTabGap=8.0f;
constexpr float kChipWidth=44.0f, kChipGap=4.0f, kChipRight=1840.0f;
constexpr int kTabMost=6, kChipMost=9;        // chips: at most 8 presets and the ＋

constexpr Rect Column(int c) noexcept { return c<=0 ? kList : c==1 ? kCentre : kDetails; }
// Row `i` of column `c` (0 at the top under the panel's padding).
constexpr Rect Row(int c,int i) noexcept {
    const Rect p=Column(c);
    const float y=p.y0+kPad+static_cast<float>(i)*(kRowHeight+kRowGap);
    return Rect{p.x0+kPad,y,p.x1-kPad,y+kRowHeight};
}
// Rows that fit in a column.
constexpr int RowsIn(int c) noexcept {
    const Rect p=Column(c);
    return static_cast<int>((p.H()-2*kPad+kRowGap)/(kRowHeight+kRowGap));
}
constexpr Rect Tab(int i) noexcept {
    const float x=kTabX+static_cast<float>(i)*(kTabWidth+kTabGap);
    return Rect{x,kTitle.y0+10,x+kTabWidth,kTitle.y1-8};
}
// Chip `i` of `count`, right-aligned in the title bar (the last one nearest the right end).
constexpr Rect Chip(int i,int count) noexcept {
    const float x1=kChipRight-static_cast<float>(count-1-i)*(kChipWidth+kChipGap);
    return Rect{x1-kChipWidth,kTitle.y0+14,x1,kTitle.y1-12};
}
}  // namespace layout

// ---- elements --------------------------------------------------------------------------------------------------------

// Title / hint bar: tan, lighter at the top, with a dark wedge cap at each end (the cap's slanted inner edge).
inline void Bar(Quads* out,const Rect& r) {
    const float cap=r.H()*0.6f;
    out->Vertical(Rect{r.x0+cap,r.y0,r.x1-cap,r.y1},kBarTop,kBarBottom);
    // left cap: a wedge from the bar's corner in to `cap` at the top, filled tan, edged dark
    out->Add(r.x0+cap,r.y0,r.x0+cap,r.y0,r.x0,r.y1,r.x0+cap,r.y1,kBarTop,kBarTop,kBarBottom,kBarBottom);
    out->Add(r.x1-cap,r.y0,r.x1-cap,r.y0,r.x1-cap,r.y1,r.x1,r.y1,kBarTop,kBarTop,kBarBottom,kBarBottom);
    // dark end marks
    out->Add(r.x0-8,r.y1,r.x0-8+6,r.y1,r.x0-8+cap,r.y0,r.x0-8+cap+6,r.y0,kBarCap,kBarCap,kBarCap,kBarCap);
    out->Add(r.x1+8-cap-6,r.y0,r.x1+8-cap,r.y0,r.x1+8-6,r.y1,r.x1+8,r.y1,kBarCap,kBarCap,kBarCap,kBarCap);
    // a thin dark line under the bar
    out->Box(Rect{r.x0+cap,r.y1-2,r.x1-cap,r.y1},kBarCap);
}

// The brown diamond background over `r`: dark base, a light diamond (half-diagonal 42) centred on every (84 i, 84 j),
// so the two tile a checker rotated 45 degrees, 84 units a period. Diamonds reaching past `r` are listed whole (the
// painter clips to `r`).
inline void Diamonds(Quads* out,const Rect& r) {
    out->Box(r,kDiamondDark);
    const float h=kDiamondPeriod/2;
    const int i0=static_cast<int>(r.x0/kDiamondPeriod), i1=static_cast<int>(r.x1/kDiamondPeriod)+1;
    const int j0=static_cast<int>(r.y0/kDiamondPeriod), j1=static_cast<int>(r.y1/kDiamondPeriod)+1;
    for(int j=j0;j<=j1;++j)
        for(int i=i0;i<=i1;++i){
            const float cx=static_cast<float>(i)*kDiamondPeriod, cy=static_cast<float>(j)*kDiamondPeriod;
            // top, right, left, bottom: TL TR BL BR order of a diamond
            out->Add(cx,cy-h,cx+h,cy,cx-h,cy,cx,cy+h,kDiamondLight,kDiamondLight,kDiamondLight,kDiamondLight);
        }
}

// ---- a hole for the 3D view (room_preview.h SetViewPlacement): the model is drawn before every UI layer, so nothing of
// the screen may cover its rect. Shapes are cut to the area round the hole, not drawn over it.

// Rect `a` and `b` share some area.
constexpr bool Overlap(const Rect& a,const Rect& b) noexcept { return a.x0<b.x1 && b.x0<a.x1 && a.y0<b.y1 && b.y0<a.y1; }

// The convex polygon `in` (n corners in order round it, n <= 8) cut to `r` (Sutherland-Hodgman); its corners into `out`
// (room for n + 4), how many (0: nothing of it in `r`).
inline int ClipToRect(const float (*in)[2],int n,const Rect& r,float (*out)[2]) noexcept {
    float a[12][2], b[12][2];
    int m=n<8 ? n : 8;
    for(int i=0;i<m;++i){a[i][0]=in[i][0];a[i][1]=in[i][1];}
    for(int edge=0;edge<4 && m>0;++edge){
        // keep: x >= x0, x <= x1, y >= y0, y <= y1
        const int axis=edge/2;
        const bool low=edge%2==0;
        const float lim=axis==0 ? (low ? r.x0 : r.x1) : (low ? r.y0 : r.y1);
        auto in_=[&](const float* p){ return low ? p[axis]>=lim : p[axis]<=lim; };
        int k=0;
        for(int i=0;i<m;++i){
            const float* p=a[i];
            const float* q=a[(i+1)%m];
            const bool pin=in_(p), qin=in_(q);
            if(pin){b[k][0]=p[0];b[k][1]=p[1];++k;}
            if(pin!=qin){
                const float t=(lim-p[axis])/(q[axis]-p[axis]);
                b[k][0]=p[0]+(q[0]-p[0])*t;b[k][1]=p[1]+(q[1]-p[1])*t;
                b[k][axis]=lim;
                ++k;
            }
        }
        m=k;
        for(int i=0;i<m;++i){a[i][0]=b[i][0];a[i][1]=b[i][1];}
    }
    // a corner on the cut line comes out twice: keep it once
    int k=0;
    for(int i=0;i<m;++i){
        const float* p=a[i];
        const float* last=k>0 ? out[k-1] : a[m-1];
        if(std::fabs(p[0]-last[0])<1e-3f && std::fabs(p[1]-last[1])<1e-3f)continue;
        out[k][0]=p[0];out[k][1]=p[1];++k;
    }
    return k<3 ? 0 : k;
}

// A convex polygon (corners in order round it) in one colour: 4 corners one quad, more a fan of triangles (each a quad
// with its last corner repeated).
inline void Polygon(Quads* out,const float (*p)[2],int n,std::uint32_t c) {
    if(n==4){
        out->Add(p[0][0],p[0][1],p[1][0],p[1][1],p[3][0],p[3][1],p[2][0],p[2][1],c,c,c,c);
        return;
    }
    for(int i=1;i+1<n;++i)
        out->Add(p[0][0],p[0][1],p[i][0],p[i][1],p[i+1][0],p[i+1][1],p[i+1][0],p[i+1][1],c,c,c,c);
}

// The diamond background inside `r` only (each diamond cut to it).
inline void DiamondsIn(Quads* out,const Rect& r) {
    if(r.x1<=r.x0 || r.y1<=r.y0)return;
    out->Box(r,kDiamondDark);
    const float h=kDiamondPeriod/2;
    const int i0=static_cast<int>(r.x0/kDiamondPeriod), i1=static_cast<int>(r.x1/kDiamondPeriod)+1;
    const int j0=static_cast<int>(r.y0/kDiamondPeriod), j1=static_cast<int>(r.y1/kDiamondPeriod)+1;
    for(int j=j0;j<=j1;++j)
        for(int i=i0;i<=i1;++i){
            const float cx=static_cast<float>(i)*kDiamondPeriod, cy=static_cast<float>(j)*kDiamondPeriod;
            const float d[4][2]={{cx,cy-h},{cx+h,cy},{cx,cy+h},{cx-h,cy}};   // round it: top, right, bottom, left
            float cut[12][2];
            const int n=ClipToRect(d,4,r,cut);
            if(n)Polygon(out,cut,n,kDiamondLight);
        }
}
// The four bands of `outer` round `hole` (top, bottom, left, right; empty ones are empty rects).
inline void Around(const Rect& outer,const Rect& hole,Rect band[4]) noexcept {
    const float hx0=hole.x0>outer.x0 ? hole.x0 : outer.x0, hx1=hole.x1<outer.x1 ? hole.x1 : outer.x1;
    const float hy0=hole.y0>outer.y0 ? hole.y0 : outer.y0, hy1=hole.y1<outer.y1 ? hole.y1 : outer.y1;
    band[0]=Rect{outer.x0,outer.y0,outer.x1,hy0};
    band[1]=Rect{outer.x0,hy1,outer.x1,outer.y1};
    band[2]=Rect{outer.x0,hy0,hx0,hy1};
    band[3]=Rect{hx1,hy0,outer.x1,hy1};
}
// The diamond background over `screen` but `hole`.
inline void DiamondsAround(Quads* out,const Rect& screen,const Rect& hole) {
    if(!Overlap(screen,hole)){DiamondsIn(out,screen);return;}
    Rect band[4];
    Around(screen,hole,band);
    for(const Rect& b:band)DiamondsIn(out,b);
}
inline std::uint32_t DiamondAt(float x,float y) noexcept {
    const float h=kDiamondPeriod/2;
    float fx=x-static_cast<float>(static_cast<int>(x/kDiamondPeriod+0.5f))*kDiamondPeriod;
    float fy=y-static_cast<float>(static_cast<int>(y/kDiamondPeriod+0.5f))*kDiamondPeriod;
    if(fx<0) fx=-fx;
    if(fy<0) fy=-fy;
    return fx+fy<h ? kDiamondLight : kDiamondDark;
}

// Dark panel with a silver L bracket at each corner.
inline void Panel(Quads* out,const Rect& r) {
    out->Box(r,kPanel);
    const float l=kBracketLength, w=kBracketWidth;
    out->Box(Rect{r.x0,r.y0,r.x0+l,r.y0+w},kBracket);
    out->Box(Rect{r.x0,r.y0,r.x0+w,r.y0+l},kBracket);
    out->Box(Rect{r.x1-l,r.y0,r.x1,r.y0+w},kBracket);
    out->Box(Rect{r.x1-w,r.y0,r.x1,r.y0+l},kBracket);
    out->Box(Rect{r.x0,r.y1-w,r.x0+l,r.y1},kBracket);
    out->Box(Rect{r.x0,r.y1-l,r.x0+w,r.y1},kBracket);
    out->Box(Rect{r.x1-l,r.y1-w,r.x1,r.y1},kBracket);
    out->Box(Rect{r.x1-w,r.y1-l,r.x1,r.y1},kBracket);
}

// A panel with the 3D view's hole in it: the panel round the hole, its corner brackets, and silver brackets at the
// hole's corners (the view framed like a panel of its own).
inline void PanelAround(Quads* out,const Rect& r,const Rect& hole) {
    if(!Overlap(r,hole)){Panel(out,r);return;}
    Rect band[4];
    Around(r,hole,band);
    for(const Rect& b:band)
        if(b.x1>b.x0 && b.y1>b.y0)out->Box(b,kPanel);
    const float l=kBracketLength, w=kBracketWidth;
    // the hole's brackets sit just outside it (grown by the bracket width), never on the view
    const Rect corners[2]={r,Rect{hole.x0-w,hole.y0-w,hole.x1+w,hole.y1+w}};
    for(const Rect& c:corners){
        out->Box(Rect{c.x0,c.y0,c.x0+l,c.y0+w},kBracket);
        out->Box(Rect{c.x0,c.y0,c.x0+w,c.y0+l},kBracket);
        out->Box(Rect{c.x1-l,c.y0,c.x1,c.y0+w},kBracket);
        out->Box(Rect{c.x1-w,c.y0,c.x1,c.y0+l},kBracket);
        out->Box(Rect{c.x0,c.y1-w,c.x0+l,c.y1},kBracket);
        out->Box(Rect{c.x0,c.y1-l,c.x0+w,c.y1},kBracket);
        out->Box(Rect{c.x1-l,c.y1-w,c.x1,c.y1},kBracket);
        out->Box(Rect{c.x1-w,c.y1-l,c.x1,c.y1},kBracket);
    }
}

// Slanted metal button: a parallelogram, top edge kButtonSlant right of the bottom, within `r`. Selected: beige.
inline void Button(Quads* out,const Rect& r,bool selected) {
    const std::uint32_t top=selected ? kBeigeTop : kMetalTop, bottom=selected ? kBeigeBottom : kMetalBottom;
    const float s=kButtonSlant;
    out->Add(r.x0+s,r.y0,r.x1,r.y0,r.x0,r.y1,r.x1-s,r.y1,top,top,bottom,bottom);
    // the light top edge
    out->Add(r.x0+s,r.y0,r.x1,r.y0,r.x0+s-0.5f,r.y0+2,r.x1-0.5f,r.y0+2,kMetalEdge,kMetalEdge,kMetalEdge,kMetalEdge);
}

// The orange cursor round `r`: an outline kCursorWidth thick just outside it and a pointer wedge at its left.
inline void Cursor(Quads* out,const Rect& r) {
    const float w=kCursorWidth;
    out->Box(Rect{r.x0-w,r.y0-w,r.x1+w,r.y0},kCursor);
    out->Box(Rect{r.x0-w,r.y1,r.x1+w,r.y1+w},kCursor);
    out->Box(Rect{r.x0-w,r.y0,r.x0,r.y1},kCursor);
    out->Box(Rect{r.x1,r.y0,r.x1+w,r.y1},kCursor);
    const float my=(r.y0+r.y1)/2, a=r.H()*0.25f;
    out->Add(r.x0-w-a,my-a,r.x0-w,my,r.x0-w-a,my+a,r.x0-w,my,kCursor,kCursor,kCursor,kCursor);
}

// Dark label cell with its top-right corner cut diagonally (kCellCut), a lighter edge along the bottom.
inline void Cell(Quads* out,const Rect& r) {
    const float c=kCellCut;
    out->Box(Rect{r.x0,r.y0,r.x1-c,r.y1},kCell);
    out->Add(r.x1-c,r.y0,r.x1-c,r.y0,r.x1-c,r.y1,r.x1,r.y0+c,kCell,kCell,kCell,kCell);
    out->Add(r.x1-c,r.y0+c,r.x1,r.y0+c,r.x1-c,r.y1,r.x1,r.y1,kCell,kCell,kCell,kCell);
    out->Box(Rect{r.x0,r.y1-2,r.x1,r.y1},kCellEdge);
}

// Tab: a trapezoid narrowing upward; selected beige, otherwise dark metal.
inline void Tab(Quads* out,const Rect& r,bool selected) {
    const std::uint32_t top=selected ? kBeigeTop : kMetalTop, bottom=selected ? kBeigeBottom : kMetalBottom;
    const float s=r.H()*0.25f;
    out->Add(r.x0+s,r.y0,r.x1-s,r.y0,r.x0,r.y1,r.x1,r.y1,top,top,bottom,bottom);
}

// Preset chip: a small slanted plate; the current preset beige, the chip under the pointer / cursor outlined orange.
inline void Chip(Quads* out,const Rect& r,bool current,bool focused) {
    const std::uint32_t top=current ? kBeigeTop : kMetalTop, bottom=current ? kBeigeBottom : kMetalBottom;
    const float s=6.0f;
    out->Add(r.x0+s,r.y0,r.x1,r.y0,r.x0,r.y1,r.x1-s,r.y1,top,top,bottom,bottom);
    if(focused){
        out->Box(Rect{r.x0,r.y1,r.x1,r.y1+kCursorWidth},kCursor);
    }
}

// The text colour on an element: dark on beige (selected / current), light otherwise.
constexpr std::uint32_t TextOn(bool beige) noexcept { return beige ? kTextDark : kText; }

}  // namespace crew::armory::skin
