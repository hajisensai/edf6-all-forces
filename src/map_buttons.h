// The map's command buttons (the user, 2026-10-08: "the M map should do all of it, best by clicking the HUD"): every
// squad command the map's keys give, and the box sweep and its health-box switch, as buttons the mouse clicks. The
// pure part: their layout and which one a click is on. hud.cpp MapCommands lays them out from the drawn labels'
// widths (Flow), draws them and hands the rectangles to mapcmd.cpp (MapCommandButtons), which tests a click against
// exactly what was drawn (Hit) before taking it for a unit. tools/map_buttons_check.cpp runs it offline.
#pragma once

namespace mapbtn {
enum class Id : int {
    guard, follow, release, engage, focus, board, dismount, dismiss, recruit,   // the orders (Order's)
    formation, split, merge,                                                   // the squads' shape and fireteams
    sweep, health,                                                             // the box sweep, its health-box switch
    count
};
constexpr int kCount=static_cast<int>(Id::count);
struct Rect { float x0,y0,x1,y1; };

// Lays `n` buttons of widths `w[i]` (px, the label's own plus its padding) out in rows `rowH` high, `gap` apart, each
// row centred on the screen (`width`) and no wider than it less `margin` either side; the first row's bottom at
// `bottom`, further rows above it. Returns the rows used (0: no room even for one button).
inline int Flow(const float* w,int n,float width,float bottom,float rowH,float gap,float margin,Rect* out) noexcept {
    const float most=width-2.0f*margin;
    int rows=0,i=0;
    while(i<n) {
        // The buttons of this row: as many as fit (at least one, shrunk to the row when it alone is wider).
        int k=i;
        float used=0.0f;
        while(k<n) {
            const float add=(k>i ? gap : 0.0f)+w[k];
            if(k>i && used+add>most)break;
            used+=add;
            ++k;
            if(used>=most)break;
        }
        if(used>most)used=most;
        const float y1=bottom-static_cast<float>(rows)*(rowH+gap),y0=y1-rowH;
        if(y0<0.0f)return rows;
        float x=(width-used)*0.5f;
        for(int j=i;j<k;++j) {
            const float bw=w[j]<most ? w[j] : most;
            out[j]=Rect{x,y0,x+bw,y1};
            x+=bw+gap;
        }
        ++rows;
        i=k;
    }
    return rows;
}

// The button under (x, y) (-1 none) of the `n` laid out.
inline int Hit(const Rect* r,int n,float x,float y) noexcept {
    for(int i=0;i<n;++i)if(x>=r[i].x0 && x<r[i].x1 && y>=r[i].y0 && y<r[i].y1)return i;
    return -1;
}
}  // namespace mapbtn
