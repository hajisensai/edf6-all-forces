// A sight's reticle by what the weapon is (the user, 2026-10-09: "the sights are rubbish: everything that has a sight,
// a sniper rifle, a tank and so on, should get a sight that plainly fits it"). Until then every direct-fire weapon got
// one generic chevron and stadia. Now a weapon's own numbers pick a style (Classify: the round's class, its fire
// interval, its speed, its blast, the game's own scope flag, the vehicle's role) and each style has one draw function
// built from shared primitives (a cross, a chevron, mil stadia, mil dots, rings, corner brackets, the range ladder):
//  - cannon (a tank's main gun: TPD / GPS style): a chevron, its tip the aim point; lead stadia ticked every 5 mils
//    either side; under it the ballistic range ladder for the loaded round (its real speed and gravity: gunsight.h),
//    a drop line through it; the round named (AP: kinetic, HE: it bursts, BEAM: an energy gun, no drop to show);
//  - precision (a sniper's: the game's own SecondaryFire scope, or a slow-firing fast kinetic gun): a duplex cross,
//    thin in the middle and heavy outside, mil dots every mil (every second when they crowd), the holdover ladder on it;
//  - autocannon (machine guns, gatlings, an IFV's quick-firing cannon): a ring with a centre dot and four posts, a short
//    range ladder under it;
//  - flak (an anti-aircraft gun: the vehicle's role): a computing ring sight, a ring for each kFlakTargets crossing
//    speed at its true lead angle atan(target speed / round speed), spokes between them, each ring's speed in km/h;
//  - energy (a quick-firing laser or beam: no drop, no flight time to lead by): an open cross and a small diamond;
//  - missile (a guided round before it locks): the seeker's gate, four corner brackets and a centre cross (the lock
//    itself is the lock-on code's: hud.cpp LockAt);
//  - none: a weapon with no sight to speak of (a flamethrower, acid, a lobbed grenade: those have their impact marks).
// Pure arithmetic (no EDF.dll): the styles draw into a Sketch of lines, rings, dots and notes in screen pixels that
// hud.cpp renders with its own quads and text; tools/reticle_check.cpp checks every style's geometry offline.
#pragma once
#include "gunsight.h"
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace crew {
namespace reticle {
enum class Style : std::uint8_t { none, cannon, precision, autocannon, flak, energy, missile, rocket };
enum class Shell : std::uint8_t { none, ap, he, beam };
// The posts the magnified scope's field carries (hud.cpp ScopeShade): three (left, right, under: a tank sight's), the
// duplex's four, or none (a ring sight's field is clear).
enum class Posts : std::uint8_t { none, three, duplex };

// What a weapon is, as the game's own data says (vhud.cpp Arm reads it).
struct Traits {
    bool homing=false,rocket=false,lobbed=false,energy=false;
    bool sprayer=false;      // a flamethrower, acid, napalm (rounds.cpp FLAME / ACID / NAPALM)
    bool scoped=false;       // SecondaryFire_Type 1 (weapon+0x690): the game's own scope (docs/zoom-re.md §3)
    bool antiAir=false;      // the vehicle's role is anti-aircraft (AntiAirClass)
    bool shellClass=false;   // a gun shell's factory (SolidBullet01Rail, RocketBullet01, SolidExpBullet01: "CANNON")
    int interval=0;          // FireInterval, frames (weapon+0x36C)
    float speed=0.0f;        // the round's speed, m/s
    float blast=0.0f;        // AmmoExplosion, m (weapon+0x8B0)
};
constexpr int kHeavyFrames=60;          // a shot a second or slower: a gun laid shot by shot
constexpr float kPrecisionSpeed=600.0f; // m/s: a slow-firing plain bullet at least this fast is a marksman's
// The vehicle classes (crew.cpp kClasses) whose guns are anti-aircraft guns.
constexpr const char* kAntiAirClasses[]={"603_Flak"};
inline bool AntiAirClass(const char* cls) noexcept {
    if(!cls)return false;
    for(const char* a:kAntiAirClasses)if(std::strcmp(a,cls)==0)return true;
    return false;
}

inline Style Classify(const Traits& t) noexcept {
    if(t.homing)return Style::missile;
    if(t.rocket)return Style::rocket;
    if(t.lobbed || t.sprayer)return Style::none;
    const bool heavy=t.interval>=kHeavyFrames;
    if(t.scoped)return Style::precision;
    if(t.antiAir && !heavy)return Style::flak;
    if(t.energy)return heavy ? Style::cannon : Style::energy;
    if(!heavy)return Style::autocannon;
    if(!t.shellClass && t.speed>=kPrecisionSpeed)return Style::precision;
    return Style::cannon;
}
inline Shell ShellOf(const Traits& t) noexcept {
    if(t.homing || t.rocket || t.lobbed || t.sprayer)return Shell::none;
    if(t.energy)return Shell::beam;
    return t.blast>0.0f ? Shell::he : Shell::ap;
}
inline const wchar_t* ShellTag(Shell s) noexcept {
    switch(s) {
        case Shell::ap: return L"AP";
        case Shell::he: return L"HE";
        case Shell::beam: return L"BEAM";
        default: return L"";
    }
}

// Each style's table row: its scope posts, whether it carries the range ladder, its mils between lead ticks.
struct Spec { Style style; Posts posts; bool ladder; float leadMils; };
constexpr Spec kSpecs[]={
    {Style::none,Posts::three,false,0.0f},
    {Style::cannon,Posts::three,true,5.0f},
    {Style::precision,Posts::duplex,true,1.0f},
    {Style::autocannon,Posts::none,true,0.0f},
    {Style::flak,Posts::none,false,0.0f},
    {Style::energy,Posts::none,false,0.0f},
    {Style::missile,Posts::none,false,0.0f},
    {Style::rocket,Posts::none,false,0.0f},
};
inline const Spec& SpecOf(Style s) noexcept {
    for(const Spec& k:kSpecs)if(k.style==s)return k;
    return kSpecs[0];
}
// Whether a style is a direct-fire gun's sight (drawn on the seat's sight gun: hud.cpp GunReticle).
constexpr bool GunStyle(Style s) noexcept {
    return s==Style::cannon || s==Style::precision || s==Style::autocannon || s==Style::flak || s==Style::energy;
}

// The anti-aircraft ring sight's crossing speeds, m/s (90 and 180 km/h: the flying enemies' cruise and dash).
constexpr float kFlakTargets[]={25.0f,50.0f};
// The lead angle, rad, for a target crossing at `target` m/s against a round flying `round` m/s (the game's rounds keep
// their speed: no drag): atan(target / round). 0 for a round with no speed.
inline float LeadAngle(float target,float round) noexcept { return round>0.0f ? std::atan(target/round) : 0.0f; }

// The direction `bore` (unit) turned `angle` rad to its right about the world's up: exactly `angle` off it. False when
// the bore is straight up or down (no right to turn to).
inline bool Turned(const float* bore,float angle,float* out) noexcept {
    float side[3]={-bore[2],0.0f,bore[0]};   // bore x up, up = +Y: the bore's level right (docs/player-jet-re.md §2)
    const float l=std::sqrt(side[0]*side[0]+side[2]*side[2]);
    if(!(l>1e-6f))return false;
    side[0]/=l;side[2]/=l;
    const float c=std::cos(angle),s=std::sin(angle);
    for(int i=0;i<3;++i)out[i]=c*bore[i]+s*side[i];
    return true;
}

// --- The sketch: what a style draws, in screen pixels, for hud.cpp to render. ---
enum class Ink : std::uint8_t { main, dim, hot };   // the HUD's green, its dim green, amber
struct Stroke { float x0,y0,x1,y1,w; Ink ink; };
struct Circle { float x,y,r,w; Ink ink; };
struct Dot { float x,y,r; Ink ink; };
struct Note { float x,y; int align; float scale; Ink ink; wchar_t text[16]; };   // align: 0 left, 1 centred, 2 right
constexpr int kMostStrokes=128,kMostCircles=8,kMostDots=64,kMostNotes=16;
struct Sketch {
    Stroke stroke[kMostStrokes]; int strokes=0;
    Circle circle[kMostCircles]; int circles=0;
    Dot dot[kMostDots]; int dots=0;
    Note note[kMostNotes]; int notes=0;
    void Line(float x0,float y0,float x1,float y1,float w,Ink ink=Ink::main) noexcept {
        if(strokes<kMostStrokes)stroke[strokes++]=Stroke{x0,y0,x1,y1,w,ink};
    }
    void Ring(float x,float y,float r,float w,Ink ink=Ink::main) noexcept {
        if(circles<kMostCircles)circle[circles++]=Circle{x,y,r,w,ink};
    }
    void Spot(float x,float y,float r,Ink ink=Ink::main) noexcept { if(dots<kMostDots)dot[dots++]=Dot{x,y,r,ink}; }
    void Text(float x,float y,int align,float scale,Ink ink,const wchar_t* format,...) noexcept;
};
inline void Sketch::Text(float x,float y,int align,float scale,Ink ink,const wchar_t* format,...) noexcept {
    if(notes>=kMostNotes)return;
    Note& n=note[notes++];
    n.x=x;n.y=y;n.align=align;n.scale=scale;n.ink=ink;
    va_list args;
    va_start(args,format);
    std::vswprintf(n.text,sizeof(n.text)/sizeof(n.text[0]),format,args);
    va_end(args);
}

// Where the sight stands on the screen: the bore's point (cx, cy), `focal` the pixels for a unit of tan off the bore
// (0: unknown, no angular marks), `s` the HUD's scale, `note` its text scale.
struct View { float cx,cy,focal,s,note; };
// A ladder tick on the screen: where the round is when it has gone `range` m over the ground.
struct Mark { float x,y,range; };
struct Aim {
    Style style=Style::none;
    Shell shell=Shell::none;
    float speed=0.0f;              // the round's speed, m/s (the flak rings)
    float step=0.0f;               // the ladder's step, m
    Mark tick[gunsight::kMostTicks]{};
    int ticks=0;
};
// Pixels `angle` rad off the bore.
inline float Px(const View& v,float angle) noexcept { return v.focal*std::tan(angle); }

// --- Shared primitives ---
constexpr float kMilApart=8.0f;   // px at 1080: the closest mil marks may stand
constexpr float kLadderTick=9.0f,kLadderApart=4.0f,kLabelApart=16.0f;   // px at 1080
// A cross on (x, y): arms from `gap` out to `len`, `w` thick.
inline void Cross(Sketch& k,float x,float y,float gap,float len,float w,Ink ink=Ink::main) noexcept {
    k.Line(x-len,y,x-gap,y,w,ink);k.Line(x+gap,y,x+len,y,w,ink);
    k.Line(x,y-len,x,y-gap,w,ink);k.Line(x,y+gap,x,y+len,w,ink);
}
// A chevron (an inverted V), its tip on (x, y), `c` its half width.
inline void Chevron(Sketch& k,float x,float y,float c,float w) noexcept {
    k.Line(x-c,y+c*0.85f,x,y,w);k.Line(x,y,x+c,y+c*0.85f,w);
}
// Four corner brackets round (x, y), `half` from the centre, each arm `arm` long.
inline void Brackets(Sketch& k,float x,float y,float half,float arm,float w,Ink ink) noexcept {
    for(int sx=-1;sx<=1;sx+=2)for(int sy=-1;sy<=1;sy+=2) {
        const float cx=x+sx*half,cy=y+sy*half;
        k.Line(cx,cy,cx-sx*arm,cy,w,ink);k.Line(cx,cy,cx,cy-sy*arm,w,ink);
    }
}
// The mils between marks for marks every `mils` mils that stand kMilApart apart at least: `mils`, doubled until so
// (0: none fit within `most` px).
inline float MilStep(const View& v,float mils,float most) noexcept {
    if(!(v.focal>0.0f) || !(mils>0.0f))return 0.0f;
    for(float m=mils;Px(v,m*0.001f)<=most;m*=2.0f)
        if(Px(v,m*0.001f)>=kMilApart*v.s)return m;
    return 0.0f;
}
// Horizontal stadia either side of (cx, cy) from `in` to `out` px with end posts, ticked every `mils` mils (taller every
// second tick) where they stand apart. Returns the ticks drawn a side.
inline int Stadia(Sketch& k,const View& v,float in,float out,float mils,float w) noexcept {
    k.Line(v.cx-out,v.cy,v.cx-in,v.cy,w);k.Line(v.cx+in,v.cy,v.cx+out,v.cy,w);
    k.Line(v.cx-out,v.cy-5.0f*v.s,v.cx-out,v.cy+5.0f*v.s,w);k.Line(v.cx+out,v.cy-5.0f*v.s,v.cx+out,v.cy+5.0f*v.s,w);
    const float m=MilStep(v,mils,out);
    if(!(m>0.0f))return 0;
    int n=0;
    for(int i=1;;++i) {
        const float x=Px(v,i*m*0.001f);
        if(x>out)break;
        if(x<in)continue;
        const float tall=(i%2==0 ? 6.0f : 3.5f)*v.s;
        k.Line(v.cx-x,v.cy,v.cx-x,v.cy+tall,w);k.Line(v.cx+x,v.cy,v.cx+x,v.cy+tall,w);
        ++n;
    }
    return n;
}
// Mil dots along both arms of the cross out to `out` px, every mil (every second when they crowd), from `in` px.
inline int MilDots(Sketch& k,const View& v,float in,float out) noexcept {
    const float m=MilStep(v,1.0f,out);
    if(!(m>0.0f))return 0;
    int n=0;
    for(int i=1;;++i) {
        const float d=Px(v,i*m*0.001f);
        if(d>out)break;
        if(d<in)continue;
        const float r=(i*m==5.0f || i*m==10.0f ? 2.6f : 1.8f)*v.s;
        k.Spot(v.cx-d,v.cy,r);k.Spot(v.cx+d,v.cy,r);k.Spot(v.cx,v.cy-d,r);k.Spot(v.cx,v.cy+d,r);
        n+=4;
    }
    return n;
}
// The range ladder under `top`: a tick where the round is when it has gone that far (kLadderApart from the one above
// at least: closer ones left out), a thin drop line down through them, each numbered (hundreds of metres; metres for a
// short gun's 50 m steps; 2.5 / 7.5 for a 250 m step) where there is room; `side` 2 numbers left of the ticks, 0 right.
// Returns the ticks drawn.
inline int Ladder(Sketch& k,const View& v,const Aim& a,float topX,float top,float tick,int side=2) noexcept {
    float last=top,lastX=topX,labelled=-1e9f;
    bool any=false;
    int n=0;
    for(int i=0;i<a.ticks;++i) {
        const float lx=a.tick[i].x,ly=a.tick[i].y;
        if(ly<last+kLadderApart*v.s)continue;
        const float h=tick*v.s;
        k.Line(lx-h,ly,lx+h,ly,2.0f*v.s);
        ++n;
        const int metres=static_cast<int>(std::lround(a.tick[i].range));
        if(ly>=labelled+kLabelApart*v.s) {
            const float x=side==2 ? lx-h-5.0f*v.s : lx+h+5.0f*v.s;
            if(a.step>=100.0f && metres%100!=0)k.Text(x,ly,side,v.note*0.7f,Ink::main,L"%.1f",metres/100.0);
            else k.Text(x,ly,side,v.note*0.7f,Ink::main,L"%d",a.step>=100.0f ? metres/100 : metres);
            labelled=ly;
        }
        if(any)k.Line(lastX,last,lx,ly,1.0f*v.s,Ink::dim);
        last=ly;lastX=lx;any=true;
    }
    return n;
}

// --- The styles (see the top). Sizes in px at 1080 lines. ---
constexpr float kChevron=14.0f,kStadiaIn=26.0f,kStadiaOut=112.0f;
constexpr float kDuplex=150.0f,kDuplexThin=0.55f;   // the duplex's reach and its thin share
constexpr float kRing=22.0f;                        // the ring sight's ring
constexpr float kGate=36.0f;                        // the seeker gate's half size

inline void Cannon(Sketch& k,const View& v,const Aim& a) noexcept {
    const float t=2.0f*v.s,c=kChevron*v.s;
    Chevron(k,v.cx,v.cy,c,t);
    Stadia(k,v,kStadiaIn*v.s,kStadiaOut*v.s,SpecOf(Style::cannon).leadMils,t);
    if(a.shell!=Shell::beam)Ladder(k,v,a,v.cx,v.cy+c*0.85f,kLadderTick);
    // The loaded round under the left stadia: its ladder is the one drawn.
    k.Text(v.cx-kStadiaOut*v.s,v.cy+14.0f*v.s,0,v.note*0.8f,Ink::hot,L"%ls",ShellTag(a.shell));
}
inline void Precision(Sketch& k,const View& v,const Aim& a) noexcept {
    const float thin=1.2f*v.s,thick=5.0f*v.s,reach=kDuplex*v.s,inner=reach*kDuplexThin,gap=3.0f*v.s;
    Cross(k,v.cx,v.cy,gap,inner,thin);
    // The heavy outer posts.
    k.Line(v.cx-reach,v.cy,v.cx-inner,v.cy,thick);k.Line(v.cx+inner,v.cy,v.cx+reach,v.cy,thick);
    k.Line(v.cx,v.cy-reach,v.cx,v.cy-inner,thick);k.Line(v.cx,v.cy+inner,v.cx,v.cy+reach,thick);
    MilDots(k,v,gap*2.0f,inner);
    if(a.shell!=Shell::beam)Ladder(k,v,a,v.cx,v.cy+gap,kLadderTick*0.6f,0);
}
inline void Autocannon(Sketch& k,const View& v,const Aim& a) noexcept {
    const float t=2.0f*v.s,r=kRing*v.s;
    k.Ring(v.cx,v.cy,r,t);
    k.Spot(v.cx,v.cy,2.0f*v.s);
    k.Line(v.cx-r-12.0f*v.s,v.cy,v.cx-r-3.0f*v.s,v.cy,t);k.Line(v.cx+r+3.0f*v.s,v.cy,v.cx+r+12.0f*v.s,v.cy,t);
    k.Line(v.cx,v.cy-r-12.0f*v.s,v.cx,v.cy-r-3.0f*v.s,t);
    Ladder(k,v,a,v.cx,v.cy+r,kLadderTick*0.7f);
    k.Text(v.cx+r+16.0f*v.s,v.cy+14.0f*v.s,0,v.note*0.8f,Ink::hot,L"%ls",ShellTag(a.shell));
}
// Returns the rings drawn.
inline int Flak(Sketch& k,const View& v,const Aim& a) noexcept {
    const float t=2.0f*v.s;
    k.Spot(v.cx,v.cy,2.5f*v.s);
    Cross(k,v.cx,v.cy,5.0f*v.s,12.0f*v.s,t);
    k.Text(v.cx+16.0f*v.s,v.cy+14.0f*v.s,0,v.note*0.8f,Ink::hot,L"AA");
    if(!(v.focal>0.0f) || !(a.speed>0.0f))return 0;
    int n=0;
    float inner=0.0f;
    for(float target:kFlakTargets) {
        const float r=Px(v,LeadAngle(target,a.speed));
        if(!(r>=14.0f*v.s) || r>4.0f*v.focal)continue;   // too small to see, or past the view
        k.Ring(v.cx,v.cy,r,t);
        k.Text(v.cx+r*0.7071f+4.0f*v.s,v.cy-r*0.7071f-4.0f*v.s,0,v.note*0.7f,Ink::main,L"%d",static_cast<int>(std::lround(target*3.6f)));
        // Spokes between this ring and the one inside it (the centre's cross for the first).
        const float from=inner>0.0f ? inner : 14.0f*v.s;
        for(int q=0;q<4;++q) {
            const float dx=q==0 ? 1.0f : q==1 ? -1.0f : 0.0f,dy=q==2 ? 1.0f : q==3 ? -1.0f : 0.0f;
            k.Line(v.cx+dx*from,v.cy+dy*from,v.cx+dx*r,v.cy+dy*r,1.0f*v.s,Ink::dim);
        }
        inner=r;++n;
    }
    return n;
}
inline void Energy(Sketch& k,const View& v,const Aim&) noexcept {
    const float t=2.0f*v.s,d=4.0f*v.s;
    Cross(k,v.cx,v.cy,9.0f*v.s,30.0f*v.s,t);
    k.Line(v.cx,v.cy-d,v.cx+d,v.cy,t);k.Line(v.cx+d,v.cy,v.cx,v.cy+d,t);
    k.Line(v.cx,v.cy+d,v.cx-d,v.cy,t);k.Line(v.cx-d,v.cy,v.cx,v.cy-d,t);
    k.Text(v.cx+34.0f*v.s,v.cy+14.0f*v.s,0,v.note*0.8f,Ink::hot,L"%ls",ShellTag(Shell::beam));
}
// The seeker gate of a guided round not yet locked (dim: it holds nothing).
inline void Gate(Sketch& k,const View& v) noexcept {
    const float t=2.0f*v.s,h=kGate*v.s;
    Brackets(k,v.cx,v.cy,h,h*0.4f,t,Ink::dim);
    Cross(k,v.cx,v.cy,3.0f*v.s,8.0f*v.s,t,Ink::dim);
}

// The style's reticle (none and rocket draw nothing here: their marks are the impact point's).
inline void Draw(const Aim& a,const View& v,Sketch* k) noexcept {
    switch(a.style) {
        case Style::cannon: Cannon(*k,v,a);break;
        case Style::precision: Precision(*k,v,a);break;
        case Style::autocannon: Autocannon(*k,v,a);break;
        case Style::flak: Flak(*k,v,a);break;
        case Style::energy: Energy(*k,v,a);break;
        case Style::missile: Gate(*k,v);break;
        default: break;
    }
}
}  // namespace reticle
}  // namespace crew
