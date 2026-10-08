// The map's command buttons (src/map_buttons.h) checked offline: laid out at 16:9, 21:9, 4:3 and a narrow split
// screen, with labels of every width the languages give: none overlapping, all on the screen (within the margin),
// in their order (left to right, row by row upwards), each row centred; a button wider than the row shrunk to it;
// a click on a button finds it and only it, a click in the gap between two finds none; no room for a row: fewer rows.
//   cmake --build build --target map_buttons_check && build\map_buttons_check.exe      (exit code 1 on a failure)
#include "../src/map_buttons.h"
#include <cmath>
#include <cstdio>

namespace {
using namespace mapbtn;
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}

void Layouts() {
    const float screens[][2]={{1920.0f,1080.0f},{2520.0f,1080.0f},{1440.0f,1080.0f},{960.0f,1080.0f},{640.0f,1080.0f}};
    const float widths[][kCount]={
        {80,90,90,90,110,90,100,90,90,120,80,80,150,190,120,120,320},     // English-ish
        {70,70,70,70,70,70,70,70,70,90,70,70,120,170,100,100,280},        // Chinese-ish
        {300,40,40,40,40,40,40,40,40,40,40,40,40,700,150,150,600},        // odd ones: one very long
    };
    for(const auto& sc:screens)for(const auto& w:widths) {
        Rect r[kCount];
        const float margin=16.0f,gap=6.0f,rowH=30.0f,bottom=sc[1]-150.0f;
        const int rows=Flow(w,kCount,sc[0],bottom,rowH,gap,margin,r);
        Check(rows>=1,"at least a row",sc[0]);
        bool on=true,apart=true,order=true,shrunk=true;
        for(int i=0;i<kCount;++i) {
            on=on && r[i].x0>=margin-0.01f && r[i].x1<=sc[0]-margin+0.01f && r[i].y0>=0.0f && r[i].y1<=bottom+0.01f;
            shrunk=shrunk && r[i].x1-r[i].x0<=sc[0]-2.0f*margin+0.01f;
            for(int j=0;j<i;++j) {
                apart=apart && !(r[i].x0<r[j].x1 && r[j].x0<r[i].x1 && r[i].y0<r[j].y1 && r[j].y0<r[i].y1);
                // Order: a later button is on the same row to the right, or on a row above.
                order=order && ((r[i].y0==r[j].y0 && r[i].x0>r[j].x0) || r[i].y0<r[j].y0);
            }
        }
        Check(on,"every button on the screen",sc[0],rows);
        Check(apart,"no two overlapping",sc[0],rows);
        Check(order,"in their order, rows upwards",sc[0],rows);
        Check(shrunk,"none wider than the row",sc[0]);
        // Each row centred: its left and right margins equal.
        for(int i=0;i<kCount;) {
            int k=i;
            while(k+1<kCount && r[k+1].y0==r[i].y0)++k;
            Check(std::fabs(r[i].x0-(sc[0]-r[k].x1))<0.5f,"each row centred",r[i].x0,sc[0]-r[k].x1);
            i=k+1;
        }
        // Hits: the centre of each finds it; the gap after it (same row) finds none.
        for(int i=0;i<kCount;++i) {
            Check(Hit(r,kCount,(r[i].x0+r[i].x1)*0.5f,(r[i].y0+r[i].y1)*0.5f)==i,"a click on a button finds it",i);
            if(i+1<kCount && r[i+1].y0==r[i].y0)
                Check(Hit(r,kCount,r[i].x1+gap*0.5f,(r[i].y0+r[i].y1)*0.5f)==-1,"a click in the gap finds none",i);
        }
        Check(Hit(r,kCount,sc[0]*0.5f,bottom+5.0f)==-1,"under the bar: none");
    }
    // No room: the rows that fit, the rest unplaced.
    float w[kCount];
    for(float& x:w)x=500.0f;
    Rect r[kCount];
    Check(Flow(w,kCount,1920.0f,100.0f,30.0f,6.0f,16.0f,r)==2,"a short screen: as many rows as fit",Flow(w,kCount,1920.0f,100.0f,30.0f,6.0f,16.0f,r));
}
}  // namespace

int main() {
    Layouts();
    std::printf(failures ? "map_buttons_check: %d of %d FAILED\n" : "map_buttons_check: all %d ok\n",failures ? failures : cases,cases);
    return failures ? 1 : 0;
}
