// A magnified optic's reticle (the user, 2026-10-11: "这个瞄具做的烂完了，全部给我重做。我说的是z键放大的瞄具。包括玩家的
// 狙击枪之类的，那个瞄具一点不现实。全换成现实的那种，或者稍微未来一点也行"). The 1x HUD reticles (reticle.h) are symbols a
// head-up display paints a fixed size in green; looking through a sight is something else, and these are drawn the way
// real optics are built:
//  - the reticle is etched on a glass in the first focal plane: thin black lines that run out to the edge of the field
//    and grow with the magnification (every angular mark from the live projection's focal length: a mil is a mil at 3x
//    and at 6x), heavy posts on the outer part of the field to lead the eye in, an illuminated red aiming point;
//  - a tank's gunner's primary sight (Leopard 2 EMES / M1 GPS class): a fine cross, three posts (left, right, under;
//    the top of the field is left clear for the target), an illuminated chevron on the aim point, lead ticks every 5
//    mils numbered every 10, the ballistic range marks of the round now loaded under the aim point (the computer's
//    solution: gunsight.h, the same ladder the 1x HUD draws), the computer's data line in LED digits (hud.cpp);
//  - a marksman's scope (mil-dot): a thin cross, four tapered duplex posts, a dot every mil out to the posts (5 and 10
//    bigger, numbered), the holdover marks on the lower stem when the round drops;
//  - a machine gun's / autocannon's sight (Bradley ISU class): the gunner's cross and posts, a 2-mil illuminated ring;
//  - an anti-aircraft gun's: the computing lead rings at the true lead angle for 90 / 180 km/h crossers;
//  - a guided missile's: the gunner's cross and the tracker's gate, red;
//  - a rifle's combat optic (ACOG class, the infantry's quick-firing scoped weapons): an illuminated chevron, a bullet
//    drop stem with range bars;
//  - a rocket launcher's (PGO-7 class): a chevron, lead chevrons either side, and the stadiametric rangefinder: a
//    2.7 m target's height at 200 .. 800 m, measured against the live focal length.
// Strokes are clipped to the field (Clip): nothing of the glass is drawn on the eyepiece's black. Pure arithmetic (no
// EDF.dll); hud.cpp renders the sketch, tools/reticle_check.cpp checks the geometry.
#pragma once
#include "reticle.h"
#include <initializer_list>

namespace crew {
namespace scope {
using reticle::Aim;
using reticle::Ink;
using reticle::Sketch;
using reticle::View;
using reticle::Px;

enum class Look : std::uint8_t { gunner, marksman, autocannon, flak, seeker, rocket, beam, combat, launcher };
// The optic a weapon's sight style is seen through (the gun's own data picked the style: reticle.h Classify).
constexpr Look LookOf(reticle::Style s) noexcept {
    switch(s) {
        case reticle::Style::cannon: return Look::gunner;
        case reticle::Style::precision: return Look::marksman;
        case reticle::Style::autocannon: return Look::autocannon;
        case reticle::Style::flak: return Look::flak;
        case reticle::Style::energy: return Look::beam;
        case reticle::Style::missile: return Look::seeker;
        case reticle::Style::rocket: return Look::rocket;
        default: return Look::gunner;
    }
}

// The field on the screen: its centre and radius (scopeview.h Radius), px.
struct Field { float cx,cy,r; };

// Sizes, px at 1080 lines (times the HUD's scale), or shares of the field's radius.
constexpr float kFine=1.3f;          // an etched line
constexpr float kPost=5.5f;          // a post
constexpr float kPostIn=0.70f;       // the gunner's posts start this share of the field out
constexpr float kDuplexIn=0.55f;     // the marksman's duplex posts: at most this share of the field out
constexpr float kMarksmanMils=10.0f; // ...where they start: ten mils out (a mil-dot reticle's thin centre)
constexpr float kDotApart=5.0f;      // px at 1080: the closest a mil-dot's dots stand (else every second mil)
constexpr float kLadderEnd=0.50f;    // range marks stop here (the data line is under them)
constexpr float kDataLine=0.58f;     // the fire-control data line (hud.cpp ScopeData), clear of the HUD's lower dock
constexpr float kClipShare=0.985f;   // strokes stop just inside the field's edge
constexpr float kChevronHalf=11.0f;
constexpr float kLeadMils=5.0f;      // the gunner's lead ticks
constexpr float kRingMils=2.0f;      // the machine gun's aiming ring's radius
constexpr float kStadiaHeight=2.7f;  // m: the launcher's rangefinder target (a tank's height, PGO-7)
constexpr float kStadiaRanges[]={200.0f,300.0f,400.0f,500.0f,600.0f,800.0f};

// Segment (x0, y0)-(x1, y1) clipped to the disc of radius `r` round (cx, cy): false when none of it is inside.
inline bool Clip(float cx,float cy,float r,float* x0,float* y0,float* x1,float* y1) noexcept {
    const float dx=*x1-*x0,dy=*y1-*y0,fx=*x0-cx,fy=*y0-cy;
    const float a=dx*dx+dy*dy,b=2.0f*(fx*dx+fy*dy),c=fx*fx+fy*fy-r*r;
    if(!(a>1e-9f))return c<=0.0f;
    const float disc=b*b-4.0f*a*c;
    if(disc<=0.0f)return false;
    const float q=std::sqrt(disc);
    float t0=(-b-q)/(2.0f*a),t1=(-b+q)/(2.0f*a);
    if(t0<0.0f)t0=0.0f;
    if(t1>1.0f)t1=1.0f;
    if(t0>=t1)return false;
    const float ox=*x0,oy=*y0;
    *x0=ox+dx*t0;*y0=oy+dy*t0;*x1=ox+dx*t1;*y1=oy+dy*t1;
    return true;
}
// A stroke on the glass (clipped to the field).
inline void Etch(Sketch& k,const Field& f,float x0,float y0,float x1,float y1,float w,Ink ink=Ink::etch) noexcept {
    if(Clip(f.cx,f.cy,f.r*kClipShare,&x0,&y0,&x1,&y1))k.Line(x0,y0,x1,y1,w,ink);
}
inline bool InField(const Field& f,float x,float y,float share=kClipShare) noexcept {
    return (x-f.cx)*(x-f.cx)+(y-f.cy)*(y-f.cy)<=(f.r*share)*(f.r*share);
}
// The fine cross through the aim point, out to the field's edge, open `gap` px round the centre.
inline void FineCross(Sketch& k,const View& v,const Field& f,float gap,bool top=true) noexcept {
    const float t=kFine*v.s,R=2.0f*f.r;
    Etch(k,f,v.cx-R,v.cy,v.cx-gap,v.cy,t);Etch(k,f,v.cx+gap,v.cy,v.cx+R,v.cy,t);
    if(top)Etch(k,f,v.cx,v.cy-R,v.cx,v.cy-gap,t);
    Etch(k,f,v.cx,v.cy+gap,v.cx,v.cy+R,t);
}
// Posts from `in` (a share of the field) to its edge: left, right, under, and over when `four`; tapered (thinner
// inner end) as a real duplex's are.
inline void Posts(Sketch& k,const View& v,const Field& f,float in,bool four) noexcept {
    // On the same glass as the cross: round the reticle's centre (the bore's point; the field's centre in game).
    const float a=f.r*in,b=f.r*(in+0.07f),e=2.0f*f.r,thin=kPost*0.5f*v.s,thick=kPost*v.s;
    const auto post=[&](float dx,float dy){
        Etch(k,f,v.cx+dx*a,v.cy+dy*a,v.cx+dx*b,v.cy+dy*b,thin);
        Etch(k,f,v.cx+dx*b,v.cy+dy*b,v.cx+dx*e,v.cy+dy*e,thick);
    };
    post(-1.0f,0.0f);post(1.0f,0.0f);post(0.0f,1.0f);
    if(four)post(0.0f,-1.0f);
}
// The illuminated aiming point: a red dot (and a red chevron, its tip on the aim, for the gunner's sight).
inline void LitPoint(Sketch& k,const View& v,bool chevron) noexcept {
    if(chevron) {
        const float c=kChevronHalf*v.s,t=1.6f*v.s;
        k.Line(v.cx-c,v.cy+c*0.8f,v.cx,v.cy,t,Ink::lit);k.Line(v.cx,v.cy,v.cx+c,v.cy+c*0.8f,t,Ink::lit);
    }
    k.Spot(v.cx,v.cy,1.5f*v.s,Ink::lit);
}
// Ticks every `mils` mils on the horizontal line from `in` to `out` px either side (taller every second), numbered
// every `numbered` mils where the numbers stand apart. Returns the ticks a side.
inline int LeadTicks(Sketch& k,const View& v,const Field& f,float in,float out,float mils,float numbered) noexcept {
    const float m=reticle::MilStep(v,mils,out);
    if(!(m>0.0f))return 0;
    int n=0;
    float lastLabel=-1e9f;
    for(int i=1;;++i) {
        const float d=Px(v,i*m*0.001f);
        if(d>out)break;
        if(d<in)continue;
        const float mil=i*m;
        const bool big=numbered>0.0f && std::fmod(mil+0.01f,numbered)<0.02f;
        const float tall=(big ? 7.0f : i%2==0 ? 5.0f : 3.0f)*v.s;
        Etch(k,f,v.cx-d,v.cy-tall,v.cx-d,v.cy,kFine*v.s);Etch(k,f,v.cx+d,v.cy-tall,v.cx+d,v.cy,kFine*v.s);
        if(big && d-lastLabel>=28.0f*v.s && InField(f,v.cx+d,v.cy-tall-10.0f*v.s,0.93f)) {
            k.Text(v.cx-d,v.cy-tall-10.0f*v.s,1,v.note*0.62f,Ink::etch,L"%d",static_cast<int>(std::lround(mil)));
            k.Text(v.cx+d,v.cy-tall-10.0f*v.s,1,v.note*0.62f,Ink::etch,L"%d",static_cast<int>(std::lround(mil)));
            lastLabel=d;
        }
        ++n;
    }
    return n;
}
// The range marks of the round now loaded (gunsight.h Ladder, projected: Aim.tick) under the aim point down to
// kLadderEnd of the field: a short bar at each (both sides of the stem), numbered on `side` (2 left, 0 right) in
// hundreds of metres (metres for a short gun's 50 m steps). Returns the marks drawn.
inline int RangeMarks(Sketch& k,const View& v,const Field& f,const Aim& a,float top,float half,int side) noexcept {
    float last=top,labelled=-1e9f;
    int n=0;
    for(int i=0;i<a.ticks;++i) {
        const float lx=a.tick[i].x,ly=a.tick[i].y;
        if(ly<last+reticle::kLadderApart*v.s)continue;
        if(ly>f.cy+f.r*kLadderEnd)break;
        const float h=half*v.s;
        Etch(k,f,lx-h,ly,lx+h,ly,1.6f*v.s);
        ++n;
        if(ly>=labelled+14.0f*v.s) {
            const int metres=static_cast<int>(std::lround(a.tick[i].range));
            const float x=side==2 ? lx-h-4.0f*v.s : lx+h+4.0f*v.s;
            if(a.step>=100.0f && metres%100!=0)k.Text(x,ly,side,v.note*0.62f,Ink::etch,L"%.1f",metres/100.0);
            else k.Text(x,ly,side,v.note*0.62f,Ink::etch,L"%d",a.step>=100.0f ? metres/100 : metres);
            labelled=ly;
        }
        last=ly;
    }
    return n;
}

// --- The optics (see the top) ---
inline void Gunner(Sketch& k,const View& v,const Field& f,const Aim& a) noexcept {
    const float gap=6.0f*v.s;
    FineCross(k,v,f,gap);
    Posts(k,v,f,kPostIn,false);
    LeadTicks(k,v,f,gap*2.0f,f.r*(kPostIn-0.03f),kLeadMils,10.0f);
    if(a.shell!=reticle::Shell::beam)RangeMarks(k,v,f,a,v.cy+gap,6.0f,0);
    LitPoint(k,v,true);
}
inline void Marksman(Sketch& k,const View& v,const Field& f,const Aim& a) noexcept {
    // The thin centre spans kMarksmanMils either side (a mil-dot's ten), held between a quarter of the field and
    // kDuplexIn of it; the duplex posts from there.
    float in=Px(v,kMarksmanMils*0.001f);
    if(!(in>=f.r*0.25f))in=f.r*0.25f;
    if(in>f.r*kDuplexIn)in=f.r*kDuplexIn;
    const float t=1.1f*v.s;
    k.Line(v.cx-in,v.cy,v.cx+in,v.cy,t,Ink::etch);k.Line(v.cx,v.cy-in,v.cx,v.cy+in,t,Ink::etch);
    Posts(k,v,f,in/f.r,true);
    // A dot every mil (every second when they stand closer than kDotApart), 5 and 10 bigger and numbered right and under.
    const float m=v.focal>0.0f ? (Px(v,0.001f)>=kDotApart*v.s ? 1.0f : 2.0f) : 0.0f;
    if(m>0.0f)for(int i=1;;++i) {
        const float d=Px(v,i*m*0.001f),mil=i*m;
        if(d>in+0.5f)break;
        const bool big=std::fmod(mil+0.01f,5.0f)<0.02f;
        const float r=(big ? 2.4f : 1.7f)*v.s;
        k.Spot(v.cx-d,v.cy,r,Ink::etch);k.Spot(v.cx+d,v.cy,r,Ink::etch);
        k.Spot(v.cx,v.cy-d,r,Ink::etch);k.Spot(v.cx,v.cy+d,r,Ink::etch);
        if(big && d>=24.0f*v.s) {
            k.Text(v.cx+d,v.cy+12.0f*v.s,1,v.note*0.58f,Ink::etch,L"%d",static_cast<int>(std::lround(mil)));
            if(!a.ticks)k.Text(v.cx-12.0f*v.s,v.cy+d,2,v.note*0.58f,Ink::etch,L"%d",static_cast<int>(std::lround(mil)));   // the holdover marks' numbers stand there
        }
        // half-mil hashes between the dots when there is room
        const float h=Px(v,(i-0.5f)*m*0.001f);
        if(m<=1.0f && h-Px(v,(i-1)*m*0.001f)>=6.0f*v.s) {
            k.Line(v.cx+h,v.cy-2.5f*v.s,v.cx+h,v.cy+2.5f*v.s,t,Ink::etchDim);
            k.Line(v.cx-h,v.cy-2.5f*v.s,v.cx-h,v.cy+2.5f*v.s,t,Ink::etchDim);
        }
    }
    if(a.shell!=reticle::Shell::beam && a.ticks>0)RangeMarks(k,v,f,a,v.cy+3.0f*v.s,5.0f,2);
    k.Spot(v.cx,v.cy,1.4f*v.s,Ink::lit);
}
inline void Autocannon(Sketch& k,const View& v,const Field& f,const Aim& a) noexcept {
    float ring=Px(v,kRingMils*0.001f);
    if(!(ring>=7.0f*v.s))ring=7.0f*v.s;
    if(ring>40.0f*v.s)ring=40.0f*v.s;
    FineCross(k,v,f,ring+4.0f*v.s);
    Posts(k,v,f,kPostIn,false);
    LeadTicks(k,v,f,ring+8.0f*v.s,f.r*0.45f,kLeadMils,10.0f);   // a machine gun's lead: the near crossing targets
    k.Ring(v.cx,v.cy,ring,1.4f*v.s,Ink::lit);
    if(a.ticks>0)RangeMarks(k,v,f,a,v.cy+ring,6.0f,0);
    k.Spot(v.cx,v.cy,1.3f*v.s,Ink::lit);
}
// Returns the lead rings drawn.
inline int Flak(Sketch& k,const View& v,const Field& f,const Aim& a) noexcept {
    FineCross(k,v,f,5.0f*v.s);
    k.Spot(v.cx,v.cy,1.6f*v.s,Ink::lit);
    if(!(v.focal>0.0f) || !(a.speed>0.0f))return 0;
    int n=0;
    for(float target:reticle::kFlakTargets) {
        const float r=Px(v,reticle::LeadAngle(target,a.speed));
        if(!(r>=12.0f*v.s) || r>f.r*0.95f)continue;
        k.Ring(v.cx,v.cy,r,1.3f*v.s,Ink::etch);
        k.Text(v.cx+r*0.7071f+3.0f*v.s,v.cy-r*0.7071f-3.0f*v.s,0,v.note*0.6f,Ink::etch,L"%d",static_cast<int>(std::lround(target*3.6f)));
        ++n;
    }
    return n;
}
inline void Seeker(Sketch& k,const View& v,const Field& f) noexcept {
    const float gap=10.0f*v.s;
    FineCross(k,v,f,gap);
    Posts(k,v,f,kPostIn,false);
    const float h=reticle::kGate*v.s;
    reticle::Brackets(k,v.cx,v.cy,h,h*0.35f,1.6f*v.s,Ink::lit);
    k.Spot(v.cx,v.cy,1.3f*v.s,Ink::lit);
}
inline void Rocket(Sketch& k,const View& v,const Field& f) noexcept {
    FineCross(k,v,f,8.0f*v.s);
    Posts(k,v,f,kPostIn,false);
    k.Spot(v.cx,v.cy,1.3f*v.s,Ink::lit);
}
inline void Beam(Sketch& k,const View& v,const Field& f) noexcept {
    FineCross(k,v,f,10.0f*v.s);
    Posts(k,v,f,kPostIn,false);
    const float d=4.0f*v.s,t=1.5f*v.s;
    k.Line(v.cx,v.cy-d,v.cx+d,v.cy,t,Ink::lit);k.Line(v.cx+d,v.cy,v.cx,v.cy+d,t,Ink::lit);
    k.Line(v.cx,v.cy+d,v.cx-d,v.cy,t,Ink::lit);k.Line(v.cx-d,v.cy,v.cx,v.cy-d,t,Ink::lit);
}
// The infantry's combat optic: an illuminated chevron, the stem under it with the drop bars at 2..8 mils, a fine
// horizontal line out from the bars' reach.
inline void Combat(Sketch& k,const View& v,const Field& f) noexcept {
    const float c=14.0f*v.s,t=2.4f*v.s;
    k.Line(v.cx-c,v.cy+c*0.9f,v.cx,v.cy,t,Ink::lit);k.Line(v.cx,v.cy,v.cx+c,v.cy+c*0.9f,t,Ink::lit);
    const float stemTop=v.cy+c*0.9f+3.0f*v.s;
    float bottom=v.cy+Px(v,0.0085f);
    if(!(bottom>stemTop+12.0f*v.s))bottom=stemTop+60.0f*v.s;
    Etch(k,f,v.cx,stemTop,v.cx,bottom,kFine*v.s);
    float labelled=-1e9f;
    for(int i=2;i<=8;i+=2) {
        const float y=v.cy+Px(v,i*0.001f);
        if(!(y>stemTop) || y>bottom)continue;
        const float half=(14.0f-i)*v.s;   // the bars narrow with range: a man's width at the range they mark
        Etch(k,f,v.cx-half,y,v.cx+half,y,1.6f*v.s);
        if(y-labelled<18.0f*v.s)continue;  // numbered where the numbers stand apart
        k.Text(v.cx+half+5.0f*v.s,y,0,v.note*0.55f,Ink::etch,L"%d",i/2+2);
        labelled=y;
    }
    const float side=Px(v,0.006f)>30.0f*v.s ? Px(v,0.006f) : 30.0f*v.s;
    Etch(k,f,v.cx-2.0f*f.r,v.cy,v.cx-side,v.cy,kFine*v.s);Etch(k,f,v.cx+side,v.cy,v.cx+2.0f*f.r,v.cy,kFine*v.s);
}
// The rocket launcher's (see the top). Returns the rangefinder's marks drawn.
inline int Launcher(Sketch& k,const View& v,const Field& f) noexcept {
    const float c=10.0f*v.s,t=1.8f*v.s;
    k.Line(v.cx-c,v.cy+c,v.cx,v.cy,t,Ink::lit);k.Line(v.cx,v.cy,v.cx+c,v.cy+c,t,Ink::lit);
    // Lead chevrons at 5 and 10 mils either side (a target crossing at 15 / 30 km/h at 300 m, a PGO-7's).
    for(float mils:{5.0f,10.0f}) {
        const float d=Px(v,mils*0.001f);
        if(!(d>c+6.0f*v.s))continue;
        for(int sx=-1;sx<=1;sx+=2) {
            const float x=v.cx+sx*d,h=6.0f*v.s;
            Etch(k,f,x-h,v.cy+h,x,v.cy,1.4f*v.s);Etch(k,f,x,v.cy,x+h,v.cy+h,1.4f*v.s);
        }
    }
    // A fine vertical stem and horizontal line, open round the chevron.
    Etch(k,f,v.cx,v.cy+c+4.0f*v.s,v.cx,v.cy+2.0f*f.r,kFine*v.s);
    // The rangefinder (lower left): a base line, and at each range a mark as tall as the target there looks.
    if(!(v.focal>0.0f))return 0;
    const float baseY=v.cy+f.r*0.42f,x0=v.cx-f.r*0.62f,step=f.r*0.075f;
    int n=0;
    float lastX=x0,lastTop=baseY;
    for(int i=0;i<static_cast<int>(sizeof(kStadiaRanges)/sizeof(kStadiaRanges[0]));++i) {
        const float x=x0+step*i,top=baseY-v.focal*kStadiaHeight/kStadiaRanges[i];
        if(!InField(f,x,top,0.95f) || !InField(f,x,baseY,0.95f))continue;
        Etch(k,f,x,baseY,x,baseY+3.0f*v.s,kFine*v.s);
        if(n)Etch(k,f,lastX,lastTop,x,top,kFine*v.s);
        k.Text(x,baseY+12.0f*v.s,1,v.note*0.52f,Ink::etch,L"%d",static_cast<int>(kStadiaRanges[i]/100.0f));
        lastX=x;lastTop=top;++n;
    }
    if(n>1)Etch(k,f,x0,baseY,lastX,baseY,kFine*v.s);
    return n;
}

// The optic `look` for aim `a` on view `v` (its centre the bore's point) in field `f`.
inline void Draw(Look look,const Aim& a,const View& v,const Field& f,Sketch* k) noexcept {
    switch(look) {
        case Look::gunner: Gunner(*k,v,f,a);break;
        case Look::marksman: Marksman(*k,v,f,a);break;
        case Look::autocannon: Autocannon(*k,v,f,a);break;
        case Look::flak: Flak(*k,v,f,a);break;
        case Look::seeker: Seeker(*k,v,f);break;
        case Look::rocket: Rocket(*k,v,f);break;
        case Look::beam: Beam(*k,v,f);break;
        case Look::combat: Combat(*k,v,f);break;
        case Look::launcher: Launcher(*k,v,f);break;
    }
}
}  // namespace scope
}  // namespace crew
