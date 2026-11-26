// The 整备 screen painted: armory_draw.h.
#include "armory_draw.h"

namespace crew::armory {
namespace {
uitext::Align ToText(Align a) noexcept {
    return a==Align::centre ? uitext::Align::centre : a==Align::right ? uitext::Align::right : uitext::Align::left;
}
void PaintQuads(uigpu::Painter& paint,const Layer& layer,int from,int to) noexcept {
    for(int i=from;i<to && i<layer.quads.n;++i) {
        const skin::Quad& q=layer.quads.q[i];
        paint.QuadColours(q.xy,q.argb);
    }
}
}  // namespace

void Paint(uigpu::Painter& paint,uitext::Labels& labels,const View& view,hudtext::Lang lang,unsigned frame) noexcept {
    if(!view.open)return;
    for(const Layer& layer:view.layer) {
        int from=0;
        const skin::Rect& h=layer.hole;
        if(h.W()>0 && h.H()>0 && layer.holeQuads>0) {
            // the background around the hole (the 3D preview, drawn before every UI layer, shows through it): the bands
            // above, below, left and right of it
            const skin::Rect bands[4]={{0,0,skin::kWidth,h.y0},{0,h.y1,skin::kWidth,skin::kHeight},{0,h.y0,h.x0,h.y1},
                                       {h.x1,h.y0,skin::kWidth,h.y1}};
            for(const skin::Rect& b:bands) {
                if(b.W()<=0 || b.H()<=0)continue;
                paint.Clip(b.x0,b.y0,b.x1,b.y1);
                PaintQuads(paint,layer,0,layer.holeQuads);
            }
            paint.Unclip();
            from=layer.holeQuads;
        }
        PaintQuads(paint,layer,from,layer.quads.n);
        // the texts that yield last: what the others drew first (their drawn edges, Text::yieldTo)
        static Edge drawn[Layer::kTextMost];   // render thread only
        for(int pass=0;pass<2;++pass)
            for(int i=0;i<layer.nt;++i) {
                const Text& t=layer.texts[i];
                const bool yields=t.yieldTo>=0 && t.yieldTo<layer.nt && t.yieldTo!=i;
                if(yields!=(pass==1))continue;
                drawn[i]=Edge{};
                if(!t.s[0])continue;
                uitext::Fit fit;
                fit.maxW=yields ? RoomBeside(t,drawn[t.yieldTo]) : t.maxW;
                if(yields && drawn[t.yieldTo].on && !(fit.maxW>0.f))continue;   // no room left beside it
                fit.size2=t.size2;fit.rowY[0]=t.rowY[0];fit.rowY[1]=t.rowY[1];
                const uitext::Label l=labels.DrawFit(paint,t.s,t.x,t.y,t.size,t.argb,ToText(t.align),t.bold,lang,frame,fit);
                drawn[i]=EdgeOf(t,l.w);
            }
    }
}
}  // namespace crew::armory
