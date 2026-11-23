// The 整备 screen painted: armory_draw.h.
#include "armory_draw.h"

namespace crew::armory {
namespace {
uitext::Align ToText(Align a) noexcept {
    return a==Align::centre ? uitext::Align::centre : a==Align::right ? uitext::Align::right : uitext::Align::left;
}
}  // namespace

void Paint(uigpu::Painter& paint,uitext::Labels& labels,const View& view,hudtext::Lang lang,unsigned frame) noexcept {
    if(!view.open)return;
    for(const Layer& layer:view.layer) {
        for(int i=0;i<layer.quads.n;++i) {
            const skin::Quad& q=layer.quads.q[i];
            paint.QuadColours(q.xy,q.argb);
        }
        for(int i=0;i<layer.nt;++i) {
            const Text& t=layer.texts[i];
            if(t.s[0])labels.Draw(paint,t.s,t.x,t.y,t.size,t.argb,ToText(t.align),t.bold,lang,frame,t.maxW);
        }
    }
}
}  // namespace crew::armory
