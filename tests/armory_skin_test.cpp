// 整备 screen skin (src/armory_skin.h), offline: the design's colours, every element's quads well formed (a strip's two
// triangles on opposite sides of their shared edge, so no bow-tie), inside the screen, the diamond background tiling a
// 45-degree checker of the two browns at 84 units, the layout's columns / tabs / chips apart and inside their bars.
#include "../src/armory_skin.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
using namespace crew::armory::skin;

double Side(const float* a,const float* b,const float* p) {
    return (static_cast<double>(b[0])-a[0])*(static_cast<double>(p[1])-a[1])-
           (static_cast<double>(b[1])-a[1])*(static_cast<double>(p[0])-a[0]);
}
// Corners 0 and 3 on opposite sides of the diagonal 1-2 (or on it: a triangle), so the strip covers one shape.
bool WellFormed(const Quad& q) {
    const double s0=Side(q.xy[1],q.xy[2],q.xy[0]), s3=Side(q.xy[1],q.xy[2],q.xy[3]);
    return s0*s3<=0.0;
}
bool Inside(const Quad& q,float x0,float y0,float x1,float y1) {
    for(const auto& p:q.xy)
        if(p[0]<x0-0.01f || p[0]>x1+0.01f || p[1]<y0-0.01f || p[1]>y1+0.01f) return false;
    return true;
}
bool Uses(const Quads& qs,std::uint32_t c) {
    for(int i=0;i<qs.n;++i)
        for(std::uint32_t a:qs.q[i].argb)
            if(a==c) return true;
    return false;
}
void AllWellFormed(const Quads& qs,const char* what) {
    Check(qs.dropped==0,what);
    for(int i=0;i<qs.n;++i) Check(WellFormed(qs.q[i]),what);
}
// Point in a quad drawn as a strip (either triangle).
bool InTriangle(const float* a,const float* b,const float* c,float x,float y) {
    const float p[2]{x,y};
    const double d1=Side(a,b,p), d2=Side(b,c,p), d3=Side(c,a,p);
    const bool neg=d1<0 || d2<0 || d3<0, pos=d1>0 || d2>0 || d3>0;
    return !(neg && pos);
}
bool Covers(const Quad& q,float x,float y) {
    return InTriangle(q.xy[0],q.xy[1],q.xy[2],x,y) || InTriangle(q.xy[1],q.xy[2],q.xy[3],x,y);
}
}  // namespace

int main() {
    // design colours
    Check(kCursor==0xFFF07A10u,"cursor orange #F07A10");
    Check(kDiamondLight==0xFF423C29u && kDiamondDark==0xFF332E1Eu,"diamond browns #423C29 / #332E1E");
    Check(kDiamondPeriod==84.0f,"diamonds every 84 units");

    // bars
    {
        Quads q;
        Bar(&q,layout::kTitle);
        Bar(&q,layout::kHint);
        AllWellFormed(q,"bar quads well formed");
        Check(Uses(q,kBarTop) && Uses(q,kBarBottom) && Uses(q,kBarCap),"bar: tan gradient and dark caps");
        for(int i=0;i<q.n;++i) Check(Inside(q.q[i],0,0,kWidth,kHeight),"bar inside the screen");
    }
    // diamond background: the quads draw exactly DiamondAt's checker
    {
        Quads q;
        const Rect all{0,0,kWidth,kHeight};
        Diamonds(&q,all);
        AllWellFormed(q,"diamond quads well formed");
        Check(q.n>1+23*13,"a diamond per 84-unit cell over the screen");
        int light=0,dark=0;
        for(float y=3.71f;y<kHeight;y+=17.29f)
            for(float x=2.37f;x<kWidth;x+=13.13f){
                bool lit=false;
                for(int i=1;i<q.n && !lit;++i) lit=Covers(q.q[i],x,y);
                const std::uint32_t want=DiamondAt(x,y);
                Check((lit ? kDiamondLight : kDiamondDark)==want,"diamond quads match the checker model");
                (lit ? light : dark)++;
            }
        Check(std::abs(light-dark)<(light+dark)/10,"the two browns cover about half each");
        // the 45-degree checker: light diamonds on (84 i, 84 j), dark ones on (84 i + 42, 84 j + 42)
        Check(DiamondAt(84,84)==kDiamondLight && DiamondAt(168,84)==kDiamondLight && DiamondAt(100,84)==kDiamondLight,
              "light diamonds a period apart");
        Check(DiamondAt(126,126)==kDiamondDark && DiamondAt(114,114)==kDiamondDark && DiamondAt(42,42)==kDiamondDark,
              "dark diamonds half a period off diagonally");
    }
    // panel brackets
    {
        Quads q;
        const Rect r{100,100,500,400};
        Panel(&q,r);
        AllWellFormed(q,"panel quads well formed");
        Check(q.n==9 && q.q[0].argb[0]==kPanel,"panel: fill and eight bracket arms");
        for(int i=0;i<q.n;++i) Check(Inside(q.q[i],r.x0,r.y0,r.x1,r.y1),"brackets inside the panel");
        for(int i=1;i<q.n;++i) Check(q.q[i].argb[0]==kBracket,"brackets silver");
    }
    // buttons, tabs, chips: selected beige, otherwise metal; slanted inside their rectangle
    {
        const Rect r{200,200,400,252};
        for(int sel=0;sel<2;++sel){
            Quads q;
            Button(&q,r,sel!=0);
            AllWellFormed(q,"button quads well formed");
            Check(q.q[0].argb[0]==(sel ? kBeigeTop : kMetalTop),"button selected beige");
            Check(q.q[0].xy[0][0]==r.x0+kButtonSlant && q.q[0].xy[2][0]==r.x0,"button slanted");
            for(int i=0;i<q.n;++i) Check(Inside(q.q[i],r.x0-1,r.y0,r.x1,r.y1),"button inside");
            Quads t;
            Tab(&t,r,sel!=0);
            AllWellFormed(t,"tab well formed");
            Check(t.q[0].argb[3]==(sel ? kBeigeBottom : kMetalBottom),"tab selected beige");
            Quads c;
            Chip(&c,r,sel!=0,sel!=0);
            AllWellFormed(c,"chip well formed");
            Check(Uses(c,kCursor)==(sel!=0),"chip focus outline orange");
        }
        Check(TextOn(true)!=TextOn(false),"text readable on beige and metal");
    }
    // cursor round a row, cell with a cut corner
    {
        const Rect r{300,300,600,352};
        Quads q;
        Cursor(&q,r);
        AllWellFormed(q,"cursor well formed");
        for(int i=0;i<q.n;++i){
            Check(q.q[i].argb[0]==kCursor,"cursor orange");
            Check(!Covers(q.q[i],450,326),"cursor leaves the row's inside clear");
        }
        Quads c;
        Cell(&c,r);
        AllWellFormed(c,"cell well formed");
        bool cut=false,body=false;
        for(int i=0;i<c.n;++i){
            cut=cut || Covers(c.q[i],r.x1-1,r.y0+1);
            body=body || Covers(c.q[i],r.x1-1,r.y1-6);
        }
        Check(!cut && body,"cell's top-right corner cut");
    }
    // layout
    {
        using namespace layout;
        Check(kList.x1<kCentre.x0 && kCentre.x1<kDetails.x0,"columns apart left to right");
        Check(kTitle.y1<kList.y0 && kList.y1<kHint.y0,"columns between the bars");
        Check(kTitle.x0>=0 && kTitle.x1<=kWidth && kHint.y1<=kHeight,"bars inside the screen");
        for(int c=0;c<3;++c){
            Check(RowsIn(c)>=12,"a dozen rows per column");
            const Rect last=Row(c,RowsIn(c)-1);
            Check(last.y1<=Column(c).y1-kPad+0.01f,"last row inside its column");
            Check(Row(c,1).y0>=Row(c,0).y1,"rows apart");
        }
        for(int i=0;i+1<kTabMost;++i) Check(Tab(i).x1<Tab(i+1).x0,"tabs apart");
        for(int n=1;n<=kChipMost;++n){
            Check(Chip(n-1,n).x1==kChipRight,"last chip at the right end");
            Check(Chip(0,n).x0>Tab(kTabMost-1).x1,"chips clear of the tabs");
            for(int i=0;i+1<n;++i) Check(Chip(i,n).x1<Chip(i+1,n).x0,"chips apart");
        }
    }
    std::printf("armory_skin: %d checks passed\n",checks);
    return 0;
}
