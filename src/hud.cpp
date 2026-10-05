// The vehicle HUD (docs/hud-re.md, ini VehicleHud / VehicleHudCount / VehicleHudRange).
//  - Over each of the Cfg().vehicleHudCount nearest NPC-driven friendly vehicles within Cfg().vehicleHudRange of the
//    player, on screen: one line "<kind> <hp>%  G <rounds>  M <missiles>  D <drones>  F m:ss" and an HP bar under
//    it. The gun / missile counts only for one armed with them (its pilot seat's weapons), the drones only for a
//    drone carrier, the fuel only where the plugin limits it (jets, called helis; "RTB" once it withdraws).
//  - Per submarine carrier a panel at the screen's right in the carrier's own colours (navy, cyan): its name, the
//    hull (a wide bar, HP in figures) and each deck part (a bar each; "DOWN" and the repair time once worn out).
//  - Per submarine carrier its world bars (drawn whatever VehicleHud says): a wide one over its tower for the hull
//    with its name and HP, a short one over each deck part with its name (DOWN and the repair time once worn out).
// Every bar has an edge, tenths marked, and a damage trail: what a hit took stays lighter for a moment and then
// drains to the new HP. A bar over something in the world is drawn larger the nearer it is (kNearScale at the
// camera, kFarScale from its own reading distance on): its size on the screen never grows as it moves away.
// Both are drawn with the game's own HUD primitives from the follower gauge's call (subcarrier.cpp GaugeHook,
// after the stock gauges): 0xC2FB0, the quad the gauge bars are made of, and the text sequence the rescue
// message and the multiplayer name tags use. The data is gathered on the game thread (HudSee, from every
// vehicle's input) into a table of its own, keyed by each vehicle's ObjRef; once a frame (HudPublish) the whole
// of it, with the player's position, is published as one snapshot (a triple buffer: the game thread never waits,
// the draw thread always has a whole frame's copy, never half of one and half of the next). The draw reads only
// the snapshot: it never touches a vehicle, allocates nothing and asks no VirtualQuery. The draw thread's clock
// is the wall clock: a snapshot older than kFreshMs (paused, loading, mission over) is not drawn.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "gear.h"
#include "layout.h"
#include "memory.h"
#include "sight.h"
#include "turretaim.h"
#include "vecmath.h"
#include "warn.h"
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
// The quad (docs/hud-re.md §1): (drawer, ctx, row-major 4x4 transform, RGBA, topology, xyz vertices, count, texture).
constexpr unsigned kQuad=0xC2FB0;
constexpr std::size_t kQuadDrawer=0x2139A78;
constexpr std::int32_t kStrip=5;
// The text (docs/hud-re.md §2): a renderer made from the font manager's factory, begun with a font descriptor,
// measured, drawn at a transform, ended, freed.
constexpr unsigned kTextMake=0x11517A0,kTextBegin=0x113B4A0,kTextMeasure=0x1139790,kTextDraw=0x113C520,kTextEnd=0x113C6F0,
                   kTextFree=0x1138630;
constexpr std::size_t kFontMgr=0x20B29C0,kFontDefaults=0x80,kFontFlags=0xB0,kTextFactory=8;
constexpr std::size_t kRendererSize=0x40,kFontSize=0x40;
using QuadFn=void(__fastcall*)(void*,void*,const float*,const float*,std::int32_t,const float*,std::int32_t,void*);
using TextMakeFn=void*(__fastcall*)(void*,void*);
using TextBeginFn=void(__fastcall*)(void*,void*,void*);
using TextMeasureFn=void(__fastcall*)(void*,float*,const wchar_t*,std::int64_t,bool);
using TextDrawFn=void(__fastcall*)(void*,void*,const float*,const float*,const wchar_t*,std::int64_t);
using TextEndFn=void(__fastcall*)(void*,void*);
using TextFreeFn=void(__fastcall*)(void*);

struct Sig { unsigned rva; unsigned char bytes[12]; std::size_t size; };
const Sig kQuadSigs[]={
    {kQuad,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89},12},
    {0x804582,{0x48,0x8B,0x0D,0xEF,0x54,0x93,0x01},7},            // the gauge's drawer: mov rcx,[EDF+0x2139A78]
    {0x8045B3,{0xC7,0x44,0x24,0x20,0x05,0x00,0x00,0x00},8},       // topology 5
};
const Sig kTextSigs[]={
    {kTextMake,{0x40,0x53,0x48,0x83,0xEC,0x30,0x4C,0x8B,0x41,0x38,0x0F,0x57},12},
    {kTextBegin,{0x48,0x8B,0x49,0x18,0xE9,0xF7,0xFD,0xFF,0xFF},9},
    {kTextMeasure,{0x4C,0x8B,0xDC,0x49,0x89,0x6B,0x10,0x49,0x89,0x73,0x18,0x57},12},
    {kTextDraw,{0x48,0x8B,0x49,0x18,0xE9,0x87,0xEF,0xFF,0xFF},9},
    {kTextEnd,{0x48,0x8B,0x49,0x18,0xE9,0xC7,0xFE,0xFF,0xFF},9},
    {kTextFree,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48},12},
    {0x808568,{0x48,0x8B,0x0D,0x51,0xA4,0x8A,0x01},7},            // the rescue message's font manager: [EDF+0x20B29C0]
};
// The rescue message's draw (0x808410) makes these calls in this order: the sequence this file repeats.
const unsigned kTextCalls[][2]={{0x808611,kTextMake},{0x808622,kTextBegin},{0x80863F,kTextMeasure},{0x8086A9,kTextDraw},
                                {0x8086B5,kTextEnd},{0x8086BF,kTextFree}};

bool quadOk=false,textOk=false;

// --- What the game thread gathers (HudSee) and publishes (HudPublish) ---
constexpr ULONGLONG kFreshMs=500;   // a readout whose vehicle has not been seen this long (game ms) is not drawn,
                                    // nor a snapshot this old (wall ms, the draw thread's clock)
constexpr int kEntries=48,kMaxShown=12;
struct Data {
    const void* key;              // the vehicle (its trail's key; never read through)
    float pos[3],top;             // where it is, and how far over it its readout stands
    float hp,hpMax,fuel;          // fuel: seconds left, <0 none
    std::int32_t guns,missiles,drones;   // <0: it has none of them
    bool leaving;
    char kind[16];
};
// Game thread only.
struct Work { ObjRef ref; ULONGLONG seen; bool logged; Data d; };
Work work[kEntries]{};
// The published frames: `back` is the game thread's to fill, `front` the draw thread's to read, the third
// waits in `middle` (its index, kFresh while the draw has not taken it).
struct Snapshot { ULONGLONG tick; float me[3]; int count; Data d[kEntries]; bool cockpit; PlayerJetReadout jet; bool heli; HeliCue heliCue;
                  bool drill; DrillCue drillCue;
                  bool launcher; LauncherReadout launch; bool heliSight; HeliSightReadout heliAim;
                  bool gunner; GunnerReadout gun; bool highCam,highCamOn,highCamKeys; bool heliFly; PlayerHeliReadout heliHud;
                  bool turret; edf::aimlink::TurretReadoutV1 turretAim;
                  bool stock; StockHudReadout stockHud;
                  bool warned; Warnings warn;
                  bool seats; SeatPrompt seatPrompt; };
constexpr unsigned kFresh=4;
Snapshot snaps[3]{};
std::atomic<unsigned> middle{1};
unsigned back=0;    // game thread
unsigned front=2;   // draw thread

bool CallsTo(unsigned site,unsigned target) noexcept {
    const unsigned char* p=image+site;
    return p[0]==0xE8 && static_cast<std::int64_t>(site)+5+At<std::int32_t>(p,1)==static_cast<std::int64_t>(target);
}

// Its pilot seat's weapons: rounds in the guns, missiles in the homing ones (-1: none of that kind).
void ReadAmmo(unsigned char* v,Data& d) noexcept {
    d.guns=d.missiles=-1;
    const auto seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!count || count>8 || !Readable(holders,count*8))return;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kWeaponAmmo+4))continue;
        const std::int32_t ammo=At<std::int32_t>(w,kWeaponAmmo);
        std::int32_t& sum=At<std::int32_t>(w,kWeaponLockon)==kHoming ? d.missiles : d.guns;
        sum=(sum<0 ? 0 : sum)+(ammo>0 ? ammo : 0);
    }
}

void Kind(Data& d,const char* kind) noexcept { strncpy_s(d.kind,kind,_TRUNCATE); }

// The vehicle's entry, a new one in a free slot, the slot of a gone object at the same address or of one not
// seen for a while; nullptr with every slot a fresh one's (it is not shown).
Work* WorkFor(const void* v,ULONGLONG ms) noexcept {
    Work* slot=nullptr;
    for(auto& w:work) {
        if(w.ref.Is(v))return &w;
        if(!slot && (!w.ref || w.ref.obj==v || ms-w.seen>kFreshMs*4))slot=&w;
    }
    if(slot)*slot=Work{ObjRef::Of(v),ms,false,{}};
    return slot;
}

// The draw thread's snapshot: the newest published one.
const Snapshot& Latest() noexcept {
    if(middle.load(std::memory_order_acquire)&kFresh)front=middle.exchange(front,std::memory_order_acq_rel)&3u;
    return snaps[front];
}

// --- Drawing ---
alignas(16) const float kWhite[4]={1.0f,1.0f,1.0f,1.0f};
alignas(16) const float kWarn[4]={1.0f,0.65f,0.2f,1.0f};
alignas(16) const float kBarBack[4]={0.04f,0.05f,0.06f,0.7f};
alignas(16) const float kBarEdge[4]={0.0f,0.0f,0.0f,0.85f};
alignas(16) const float kBarTick[4]={0.0f,0.0f,0.0f,0.4f};
alignas(16) const float kBarShine[4]={1.0f,1.0f,1.0f,0.18f};
alignas(16) const float kTrail[4]={1.0f,0.95f,0.85f,0.75f};
alignas(16) const float kGreen[4]={0.35f,1.0f,0.35f,0.95f};
alignas(16) const float kYellow[4]={1.0f,0.9f,0.2f,0.95f};
alignas(16) const float kRed[4]={1.0f,0.25f,0.2f,0.95f};
// The carrier's own: a navy panel, a cyan edge and title, the hull cyan / amber / red, the parts teal.
alignas(16) const float kPanel[4]={0.02f,0.07f,0.11f,0.72f};
alignas(16) const float kCyan[4]={0.3f,0.85f,1.0f,1.0f};
alignas(16) const float kTitle[4]={0.55f,0.92f,1.0f,1.0f};
alignas(16) const float kAmber[4]={1.0f,0.7f,0.15f,0.95f};
alignas(16) const float kTeal[4]={0.35f,0.75f,0.85f,0.95f};
alignas(16) const float kDown[4]={1.0f,0.3f,0.25f,1.0f};
constexpr float kLineScale=0.6f,kTitleScale=0.75f;
// The game's glyph cache (docs/hud-re.md §2.1) keys every glyph on its character and its font scale (as a half
// float), so each scale is a set of glyphs of its own. A text scale that follows the view depth would rasterize a
// new set nearly every frame and churn the shared atlas the menus draw from: the scale goes in on this grid.
constexpr float kTextScaleStep=1.0f/32.0f;
// A world bar's size: kNearScale at the camera down to kFarScale at its own reading distance (metres of view depth) and on.
constexpr float kNearScale=1.3f,kFarScale=0.7f;
// The damage trail: held kTrailHoldMs after the last hit, then drains kTrailRate of the bar a second.
constexpr ULONGLONG kTrailHoldMs=450;
constexpr float kTrailRate=0.6f;

void Rect(void* drawer,void* ctx,float x0,float y0,float x1,float y1,const float* rgba) noexcept {
    if(x1<=x0 || y1<=y0)return;
    alignas(16) const float m[16]={1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, x0,y0,0.0f,1.0f};
    const float w=x1-x0,h=y1-y0;
    alignas(16) const float v[12]={0.0f,0.0f,0.0f, w,0.0f,0.0f, 0.0f,h,0.0f, w,h,0.0f};
    reinterpret_cast<QuadFn>(image+kQuad)(drawer,ctx,m,rgba,kStrip,v,4,nullptr);
}
float Unit(float v) noexcept { return v<0.0f ? 0.0f : v>1.0f ? 1.0f : v; }
// A bar: its edge and back, the damage `trail` (a share at least `share`), `share` of it in `fill` with a sheen
// along its top, and its tenths marked once it is wide enough to show them.
void Bar(void* drawer,void* ctx,float x,float y,float w,float h,float share,float trail,const float* fill,float s) noexcept {
    share=Unit(share);trail=Unit(trail>share ? trail : share);
    const float e=s>0.5f ? s : 0.5f;
    Rect(drawer,ctx,x-e,y-e,x+w+e,y+h+e,kBarEdge);
    Rect(drawer,ctx,x,y,x+w,y+h,kBarBack);
    if(trail>share)Rect(drawer,ctx,x+w*share,y,x+w*trail,y+h,kTrail);
    Rect(drawer,ctx,x,y,x+w*share,y+h,fill);
    Rect(drawer,ctx,x,y,x+w*share,y+h*0.35f,kBarShine);
    if(w<50.0f*s)return;
    for(int i=1;i<10;++i) {
        const float tx=x+w*static_cast<float>(i)*0.1f;
        Rect(drawer,ctx,tx-0.5f*e,y,tx+0.5f*e,y+h,kBarTick);
    }
}

// --- Damage trails (draw thread only): per bar the share it showed and its trail ---
constexpr int kTrails=64;
struct Trail { const void* key; float share,trail; ULONGLONG hitAt,at; };
Trail trails[kTrails]{};
// The trail of the bar `key` now showing `share` (wall ms `now`): a hit (a lower share than last time) holds the
// old share for kTrailHoldMs, then it drains; a heal or a new bar has none.
float TrailOf(const void* key,float share,ULONGLONG now) noexcept {
    Trail* t=nullptr;
    Trail* oldest=&trails[0];
    for(auto& c:trails) {
        if(c.key==key){t=&c;break;}
        if(c.at<oldest->at)oldest=&c;
    }
    if(!t || now-t->at>2000) {   // new, or not drawn for a while: no trail to show
        if(!t)t=oldest;
        *t=Trail{key,share,share,0,now};
        return share;
    }
    if(share<t->share)t->hitAt=now;
    const float dt=static_cast<float>(now-t->at)*0.001f;
    if(share>=t->trail)t->trail=share;
    else if(now-t->hitAt>kTrailHoldMs)t->trail=t->trail-kTrailRate*dt>share ? t->trail-kTrailRate*dt : share;
    t->share=share;t->at=now;
    return t->trail;
}

// A world bar's scale at view depth `depth` for a bar meant to read at up to `readTo` metres (see the top).
float DepthScale(float depth,float readTo) noexcept {
    const float f=Unit(depth/readTo);
    return kNearScale+(kFarScale-kNearScale)*f;
}

// A line of text to draw once the quads are down: where, its size (measured), its scale and colour.
struct Line { wchar_t text[128]; float x,y,w,h,scale; const float* rgba; };
constexpr int kMaxLines=96;

void Format(Line& l,const wchar_t* format,...) noexcept {
    va_list args;va_start(args,format);
    _vsnwprintf_s(l.text,_countof(l.text),_TRUNCATE,format,args);
    va_end(args);
}
void Append(Line& l,const wchar_t* format,...) noexcept {
    const std::size_t n=wcsnlen_s(l.text,_countof(l.text));
    va_list args;va_start(args,format);
    _vsnwprintf_s(l.text+n,_countof(l.text)-n,_TRUNCATE,format,args);
    va_end(args);
}

// The text renderer for one draw (the rescue message's sequence, 0x808410).
struct Text {
    void* ctx;
    unsigned char* mgr;
    unsigned char* renderer;      // kRendererSize bytes, 16-aligned (the caller's stack)
    unsigned char* font;          // kFontSize bytes, 16-aligned
    bool made;                    // the renderer made (on the first line measured: none when nothing is shown)
    bool begun;                   // between a Begin and its End: a fault in between still owes the End
};
float TextScale(float scale) noexcept {
    const float snapped=std::round(scale/kTextScaleStep)*kTextScaleStep;
    return snapped>kTextScaleStep ? snapped : kTextScaleStep;
}
void Font(Text& t,float scale) noexcept {
    scale=TextScale(scale);
    std::memset(t.font,0,kFontSize);
    std::memcpy(t.font,t.mgr+kFontDefaults,0x18);
    Put<std::int32_t>(t.font,0x18,1);
    alignas(16) const float outline[4]={0.0f,0.0f,0.0f,1.0f};
    std::memcpy(t.font+0x1C,outline,16);
    Put<float>(t.font,0x2C,1.7f);   // as the name tags (0x804FB3)
    Put<std::int32_t>(t.font,0x30,At<std::int32_t>(t.mgr,kFontFlags));
    Put<std::int32_t>(t.font,0x34,At<std::int32_t>(t.mgr,kFontFlags+4));
    Put<float>(t.font,4,scale);Put<float>(t.font,8,scale);   // 0x113A3F0
}
void Measure(Text& t,Line& l) noexcept {
    Font(t,l.scale);
    reinterpret_cast<TextBeginFn>(image+kTextBegin)(t.renderer,t.ctx,t.font);
    t.begun=true;
    alignas(16) float size[4]{};
    reinterpret_cast<TextMeasureFn>(image+kTextMeasure)(t.renderer,size,l.text,-1,false);
    t.begun=false;
    reinterpret_cast<TextEndFn>(image+kTextEnd)(t.renderer,t.ctx);
    l.w=std::isfinite(size[0]) && size[0]>0.0f ? size[0] : 0.0f;
    l.h=std::isfinite(size[1]) && size[1]>0.0f ? size[1] : 0.0f;
}
void Draw(Text& t,const Line& l) noexcept {
    Font(t,l.scale);
    reinterpret_cast<TextBeginFn>(image+kTextBegin)(t.renderer,t.ctx,t.font);
    t.begun=true;
    alignas(16) const float m[16]={1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, l.x,l.y,0.0f,1.0f};
    reinterpret_cast<TextDrawFn>(image+kTextDraw)(t.renderer,t.ctx,m,l.rgba,l.text,-1);
    t.begun=false;
    reinterpret_cast<TextEndFn>(image+kTextEnd)(t.renderer,t.ctx);
}

int TextFault(const EXCEPTION_POINTERS* e) noexcept {
    const auto r=e->ExceptionRecord;
    Log("HUD the game's text path faulted (%08lX at EDF+%llX): text off until the game restarts, bars only",r->ExceptionCode,
        static_cast<unsigned long long>(static_cast<const unsigned char*>(r->ExceptionAddress)-image));
    textOk=false;
    return EXCEPTION_EXECUTE_HANDLER;
}
bool MakeText(Text& t) noexcept {
    __try {
        reinterpret_cast<TextMakeFn>(image+kTextMake)(t.mgr+kTextFactory,t.renderer);
        t.made=true;
        return true;
    } __except(TextFault(GetExceptionInformation())) { return false; }
}
// A fault between a Begin and its End (the text path off by then): the End it owes, once, guarded on its own
// (a renderer left begun may hold the game's text batch for its own text after).
void EndOwed(Text& t) noexcept {
    if(!t.begun)return;
    t.begun=false;
    __try { reinterpret_cast<TextEndFn>(image+kTextEnd)(t.renderer,t.ctx); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void MeasureAll(Text& t,Line* lines,int n) noexcept {
    if(n<=0 || !textOk || !t.mgr)return;
    if(!t.made && !MakeText(t))return;
    __try { for(int i=0;i<n && textOk;++i)Measure(t,lines[i]); }
    __except(TextFault(GetExceptionInformation())) {}
    EndOwed(t);
}
void DrawAll(Text& t,const Line* lines,int n) noexcept {
    if(!t.made)return;
    __try { for(int i=0;i<n && textOk;++i)Draw(t,lines[i]); }
    __except(TextFault(GetExceptionInformation())) {}
    EndOwed(t);
}
void FreeText(Text& t) noexcept {
    if(!t.made)return;
    __try { reinterpret_cast<TextFreeFn>(image+kTextFree)(t.renderer); }
    __except(TextFault(GetExceptionInformation())) {}
    t.made=false;
}

// The world point `p` on the screen (viewport pixels, y down), as the gauge drawer 0x804300 projects it.
// `depth`: its view depth (the clip w of a perspective projection: metres in front of the camera).
bool Project(const float* vp,const float* p,float width,float height,float* sx,float* sy,float* depth) noexcept {
    float c[4];
    for(int k=0;k<4;++k)c[k]=p[0]*vp[k]+p[1]*vp[4+k]+p[2]*vp[8+k]+vp[12+k];
    if(!(c[3]>1e-6f))return false;
    const float x=c[0]/c[3],y=c[1]/c[3],z=c[2]/c[3];
    if(!(std::fabs(x)<=1.0f && std::fabs(y)<=1.0f && z>=0.0f && z<=1.0f))return false;
    *sx=width*0.5f*x+width*0.5f;*sy=height*0.5f-height*0.5f*y;*depth=c[3];
    return true;
}

// The snapshot's entries nearest the player (as of the snapshot), nearest first.
int Nearest(const Snapshot& s,Data* out,int most) noexcept {
    const float* me=s.me;
    float dist[kMaxShown];
    int n=0;
    for(int i=0;i<s.count && i<kEntries;++i) {
        const Data& d=s.d[i];
        const float dx=d.pos[0]-me[0],dy=d.pos[1]-me[1],dz=d.pos[2]-me[2],r=dx*dx+dy*dy+dz*dz;
        int at=n;
        while(at>0 && dist[at-1]>r)--at;
        if(at>=most)continue;
        for(int k=(n<most ? n : most-1);k>at;--k){out[k]=out[k-1];dist[k]=dist[k-1];}
        out[at]=d;dist[at]=r;
        if(n<most)++n;
    }
    return n;
}

const float* HpColour(float share) noexcept { return share>0.6f ? kGreen : share>0.3f ? kYellow : kRed; }
const float* HullColour(float share) noexcept { return share>0.5f ? kCyan : share>0.25f ? kAmber : kRed; }

// The readouts: a line each over its vehicle, the HP bar under it (bars now, the line later).
constexpr float kReadoutFar=400.0f;   // a readout's bar reads at its smallest from this far on
int Readouts(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int at,
             const Snapshot& snap,int* shown,ULONGLONG now) noexcept {
    Data best[kMaxShown];
    int most=Cfg().vehicleHudCount;
    most=most<0 ? 0 : most>kMaxShown ? kMaxShown : most;
    const int n=Nearest(snap,best,most);
    *shown=0;
    for(int i=0;i<n && at<kMaxLines;++i) {
        const Data& d=best[i];
        const float over[3]={d.pos[0],d.pos[1]+d.top,d.pos[2]};
        float sx,sy,depth;
        if(!Project(vp,over,width,height,&sx,&sy,&depth))continue;
        const float share=d.hpMax>0.0f ? Unit(d.hp/d.hpMax) : 0.0f;
        const float k=DepthScale(depth,kReadoutFar),ks=k*s;
        Line& l=lines[at];
        Format(l,L"%hs %d%%",d.kind,static_cast<int>(std::lround(share*100.0f)));
        if(d.guns>=0)Append(l,L"  G %d",d.guns);
        if(d.missiles>=0)Append(l,L"  M %d",d.missiles);
        if(d.drones>=0)Append(l,L"  D %d",d.drones);
        if(d.leaving)Append(l,L"  RTB");
        else if(d.fuel>=0.0f) {
            const int sec=static_cast<int>(d.fuel);
            Append(l,L"  F %d:%02d",sec/60,sec%60);
        }
        l.scale=kLineScale*k;l.rgba=d.leaving || (d.fuel>=0.0f && d.fuel<20.0f) ? kWarn : kWhite;
        l.w=l.h=0.0f;
        if(text)MeasureAll(*text,&l,1);
        const float bw=80.0f*ks,bh=7.0f*ks;
        Bar(drawer,ctx,sx-bw*0.5f,sy-bh,bw,bh,share,TrailOf(d.key,share,now),HpColour(share),ks);
        l.x=sx-l.w*0.5f;l.y=sy-bh-3.0f*ks-l.h;
        ++at;++*shown;
    }
    return at;
}

// A carrier's panel at the screen's right, from `top` down: its quads now, its lines later. Returns its bottom.
float Panel(void* drawer,void* ctx,Text* text,const CarrierPanel& p,int index,int count,float width,float top,float s,
            Line* lines,int* at) noexcept {
    const int first=*at;
    if(first+2+p.parts>kMaxLines)return top;
    Line* const title=&lines[first];
    Line* const hull=&lines[first+1];
    if(count>1)Format(*title,L"SUBMARINE CARRIER %d",index+1);
    else Format(*title,L"SUBMARINE CARRIER");
    title->scale=kTitleScale;title->rgba=kTitle;
    const float hullShare=p.hullMax>0.0f ? p.hull/p.hullMax : 0.0f;
    Format(*hull,L"HULL %d%%   %.0f / %.0f",static_cast<int>(std::lround(hullShare*100.0f)),p.hull,p.hullMax);
    hull->scale=kLineScale;hull->rgba=kWhite;
    for(int k=0;k<p.parts;++k) {
        Line& l=lines[first+2+k];
        const auto& part=p.part[k];
        if(part.down) {
            const int sec=static_cast<int>(part.repairSec);
            Format(l,L"%hs   DOWN  repair %d:%02d",part.name,sec/60,sec%60);
            l.rgba=kDown;
        } else {
            Format(l,L"%hs   %d%%",part.name,static_cast<int>(std::lround(part.max>0.0f ? part.hp*100.0f/part.max : 0.0f)));
            l.rgba=kWhite;
        }
        l.scale=kLineScale;
    }
    const int n=2+p.parts;
    for(int i=0;i<n;++i)lines[first+i].w=lines[first+i].h=0.0f;
    if(text)MeasureAll(*text,&lines[first],n);
    const float pad=8.0f*s,gap=4.0f*s,hullH=12.0f*s,partH=5.0f*s;
    float textW=0.0f;
    for(int i=0;i<n;++i)textW=lines[first+i].w>textW ? lines[first+i].w : textW;
    const float pw=textW+2.0f*pad>280.0f*s ? textW+2.0f*pad : 280.0f*s;
    const float x0=width-24.0f*s-pw,x=x0+pad,bw=pw-2.0f*pad;
    float y=top+pad;
    // The lines' places and the bars under them.
    title->x=x;title->y=y;y+=title->h+gap;
    hull->x=x;hull->y=y;y+=hull->h+gap*0.5f;
    const float hullY=y;y+=hullH+gap*1.5f;
    float partY[4]{};
    for(int k=0;k<p.parts;++k) {
        Line& l=lines[first+2+k];
        l.x=x;l.y=y;y+=l.h+gap*0.5f;
        partY[k]=y;y+=partH+gap;
    }
    const float bottom=y+pad-gap;
    Rect(drawer,ctx,x0,top,x0+pw,bottom,kPanel);
    Rect(drawer,ctx,x0,top,x0+3.0f*s,bottom,kCyan);           // the edge
    Rect(drawer,ctx,x0,top,x0+pw,top+2.0f*s,kCyan);
    Bar(drawer,ctx,x,hullY,bw,hullH,hullShare,hullShare,HullColour(hullShare),s);
    for(int k=0;k<p.parts;++k) {
        const auto& part=p.part[k];
        const float share=part.down || part.max<=0.0f ? 0.0f : part.hp/part.max;
        Bar(drawer,ctx,x,partY[k],bw,partH,share,share,part.down ? kRed : kTeal,s);
    }
    *at=first+n;
    return bottom;
}

// A key's name on this keyboard (the ini's virtual-key code), "?" for none.
void KeyName(int vk,wchar_t* out,int size) noexcept {
    const UINT scan=MapVirtualKeyW(static_cast<UINT>(vk),MAPVK_VK_TO_VSC);
    if(vk<=0 || !scan || GetKeyNameTextW(static_cast<LONG>(scan<<16),out,size)<=0)wcscpy_s(out,static_cast<rsize_t>(size),L"?");
}

// The mouse's aim: a hollow cyan square at `at` (the jets' and the helis', heliaim.h).
void AimSquare(void* drawer,void* ctx,const float* vp,float width,float height,float s,const float* at) noexcept {
    float sx,sy,depth;
    if(!Project(vp,at,width,height,&sx,&sy,&depth))return;
    const float r=14.0f*s,t=2.0f*s;
    Rect(drawer,ctx,sx-r,sy-r,sx+r,sy-r+t,kCyan);Rect(drawer,ctx,sx-r,sy+r-t,sx+r,sy+r,kCyan);
    Rect(drawer,ctx,sx-r,sy-r,sx-r+t,sy+r,kCyan);Rect(drawer,ctx,sx+r-t,sy-r,sx+r,sy+r,kCyan);
}

// The mouse's aim on the keyboard and mouse: a hollow square where it aims, a small dot where the plane flies (with the
// flight HUD on, its flight path marker shows that: FighterHud).
void AimMarks(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetReadout& j) noexcept {
    AimSquare(drawer,ctx,vp,width,height,s,j.aim);
    float sx,sy,depth;
    if(!Cfg().playerJetFlightHud && Project(vp,j.path,width,height,&sx,&sy,&depth)) {
        const float r=4.0f*s;
        Rect(drawer,ctx,sx-r,sy-r,sx+r,sy+r,kWhite);
    }
}

// An impact point (CCIP) at `at`: a yellow cross with a gap at its centre. False when it is off the screen.
bool ImpactCross(void* drawer,void* ctx,const float* vp,float width,float height,float s,const float* at,float* sx,float* sy) noexcept {
    float depth;
    if(!Project(vp,at,width,height,sx,sy,&depth))return false;
    const float x=*sx,y=*sy,r=16.0f*s,g=5.0f*s,t=2.0f*s;
    Rect(drawer,ctx,x-r,y-t*0.5f,x-g,y+t*0.5f,kYellow);Rect(drawer,ctx,x+g,y-t*0.5f,x+r,y+t*0.5f,kYellow);
    Rect(drawer,ctx,x-t*0.5f,y-r,x+t*0.5f,y-g,kYellow);Rect(drawer,ctx,x-t*0.5f,y+g,x+t*0.5f,y+r,kYellow);
    return true;
}

// A bomb's impact point (CCIP).
void ImpactMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetReadout& j) noexcept {
    float sx,sy;
    if(j.hasImpact)ImpactCross(drawer,ctx,vp,width,height,s,j.impact,&sx,&sy);
}

// The Katyusha's impact point (launcher.cpp): the cross where a rocket fired now lands, a dotted ring round it as far
// as the ripple spreads, and under the cross the range, the flight time and the launcher's elevation (text later).
void LauncherMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const LauncherReadout& l,
                   Line* lines,int* at) noexcept {
    if(!l.reach)return;
    for(int i=0;i<l.rings && i<kLauncherRing;++i) {
        float x,y,depth;
        if(!Project(vp,l.ring[i],width,height,&x,&y,&depth))continue;
        const float r=2.5f*s;
        Rect(drawer,ctx,x-r,y-r,x+r,y+r,kYellow);
    }
    float sx,sy;
    if(!ImpactCross(drawer,ctx,vp,width,height,s,l.impact,&sx,&sy) || *at>=kMaxLines)return;
    Line& line=lines[(*at)++];
    Format(line,L"%d m   %.1f s   ELEV %d",static_cast<int>(std::lround(l.range)),l.flight,static_cast<int>(std::lround(l.elevation)));
    line.scale=kLineScale;line.rgba=kYellow;line.w=line.h=0.0f;
    if(text)MeasureAll(*text,&line,1);
    line.x=sx-line.w*0.5f;line.y=sy+20.0f*s;
}

// The artillery's high camera toggle (highcam.cpp, the user 2026-10-05): a line low on the screen naming its key,
// green while the high view is on.
void HighCamHint(Text* text,float width,float height,float s,bool on,bool keys,Line* lines,int* at) noexcept {
    if(*at>=kMaxLines)return;
    wchar_t key[32]=L"R3";
    if(keys)KeyName(Cfg().highCamKey,key,32);
    else if(Cfg().highCamButton!=0x80)std::swprintf(key,32,L"button 0x%X",Cfg().highCamButton);
    if((keys && Cfg().highCamKey<=0) || (!keys && Cfg().highCamButton<=0))return;
    Line& l=lines[(*at)++];
    Format(l,on ? L"HIGH CAM ON [%ls]" : L"HIGH CAM [%ls]",key);
    l.scale=kLineScale*0.85f;l.rgba=on ? kGreen : kWhite;l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
    l.x=(width-l.w)*0.5f;l.y=height*0.86f+2.0f*s;
}

// The seats of the vehicle the player sits in (seatswitch.cpp, the user 2026-10-06): a line low on the screen, each seat's
// number, what it is and who holds it, the player's in brackets, the keys that move them; amber a moment after a refused
// press (the seat named taken, or no free seat), grey online with SeatSwitchOnline off.
void SeatLine(Text* text,float width,float height,const SeatPrompt& p,Line* lines,int* at) noexcept {
    if(*at>=kMaxLines || p.seats<2)return;
    Line& l=lines[(*at)++];
    Format(l,L"SEATS");
    static const wchar_t* const kHolder[]={L"-",L"YOU",L"NPC",L"TAKEN"};
    for(int i=0;i<p.seats && i<kMostSeatsShown;++i) {
        const wchar_t* const what=i==0 ? (p.aircraft ? L"PILOT" : L"DRIVER") : p.gun[i] ? L"GUN" : L"SEAT";
        Append(l,i==p.at ? L"  [%d %ls %ls]" : L"  %d %ls %ls",i+1,what,kHolder[static_cast<int>(p.holder[i])&3]);
    }
    if(p.locked)Append(l,L"   (online: SeatSwitchOnline=0)");
    else if(p.keys) {
        wchar_t key[32];
        KeyName(Cfg().seatNextKey,key,32);
        if(Cfg().seatNextKey>0)Append(l,L"   [%ls] next",key);
        if(Cfg().seatNumberKeys)Append(l,L"   [1-%d] pick",p.seats<9 ? p.seats : 9);
    } else if(Cfg().seatButton==0x02)Append(l,L"   [B] next");
    else if(Cfg().seatButton>0)Append(l,L"   [button 0x%X] next",Cfg().seatButton);
    if(p.refused==-2)Append(l,L"   NO FREE SEAT");
    else if(p.refused>=0)Append(l,L"   SEAT %d TAKEN",p.refused+1);
    alignas(16) static const float kGrey[4]={0.7f,0.7f,0.7f,0.9f};
    l.scale=kLineScale*0.85f;l.rgba=p.locked ? kGrey : p.refused!=-1 ? kWarn : kWhite;l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
    l.x=(width-l.w)*0.5f;l.y=height*0.82f;
}

// The gunship's gun with the player at it (playerjet_crew.inc, README 炮舰机): the cross where a round of the picked gun
// fired now lands (where the screen's centre meets the ground), red out of its reach; under it the gun (SHELLS or
// CANNON, the other one named when the switch has one to go to), the range and READY or the gun's wait; a cyan square
// on the pylon turn's centre (the point last fired at). No ground under the centre: the line alone.
void GunnerMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const GunnerReadout& g,
                 Line* lines,int* at) noexcept {
    float sx=width*0.5f,sy=height*0.5f,depth;
    if(g.centred && Project(vp,g.centre,width,height,&sx,&sy,&depth)) {
        const float r=10.0f*s,t=2.0f*s;
        Rect(drawer,ctx,sx-r,sy-r,sx+r,sy-r+t,kCyan);Rect(drawer,ctx,sx-r,sy+r-t,sx+r,sy+r,kCyan);
        Rect(drawer,ctx,sx-r,sy-r,sx-r+t,sy+r,kCyan);Rect(drawer,ctx,sx+r-t,sy-r,sx+r,sy+r,kCyan);
    }
    sx=width*0.5f;sy=height*0.5f;
    if(g.ground && g.inReach)ImpactCross(drawer,ctx,vp,width,height,s,g.sight,&sx,&sy);
    else if(g.ground && Project(vp,g.sight,width,height,&sx,&sy,&depth)) {
        const float r=12.0f*s,t=2.0f*s;
        Rect(drawer,ctx,sx-r,sy-t*0.5f,sx+r,sy+t*0.5f,kRed);Rect(drawer,ctx,sx-t*0.5f,sy-r,sx+t*0.5f,sy+r,kRed);
    }
    if(*at>=kMaxLines)return;
    Line& line=lines[(*at)++];
    const wchar_t* const gun=g.both ? (g.cannon ? L"[CANNON] SHELLS" : L"[SHELLS] CANNON") : L"SHELLS";
    if(!g.ground)Format(line,L"%ls   NO GROUND IN SIGHT",gun);
    else if(!g.inReach)Format(line,L"%ls   %d m   OUT OF RANGE",gun,static_cast<int>(std::lround(g.range)));
    else if(g.ready)Format(line,L"%ls   %d m   READY",gun,static_cast<int>(std::lround(g.range)));
    else Format(line,L"%ls   %d m   %.1f s",gun,static_cast<int>(std::lround(g.range)),g.wait);
    line.scale=kLineScale;line.rgba=g.ground && g.inReach ? (g.ready ? kGreen : kYellow) : kRed;line.w=line.h=0.0f;
    if(text)MeasureAll(*text,&line,1);
    line.x=sx-line.w*0.5f;line.y=sy+22.0f*s;
}

// A lock (stores.h StoreLock: `lock` 2 locked, 1 locking at `progress`, on the lock point `p`): locking, a yellow
// square closing in as it locks; locked, a red diamond on the target. False off the screen (`sx`, `sy`: its point).
bool LockAt(void* drawer,void* ctx,const float* vp,float width,float height,float s,int lock,const float* p,float progress,float* sx,
            float* sy) noexcept {
    float depth;
    if(!lock || !Project(vp,p,width,height,sx,sy,&depth))return false;
    const float t=2.0f*s;
    if(lock==1) {
        const float r=(40.0f-24.0f*progress)*s;
        const float x=*sx,y=*sy;
        Rect(drawer,ctx,x-r,y-r,x+r,y-r+t,kYellow);Rect(drawer,ctx,x-r,y+r-t,x+r,y+r,kYellow);
        Rect(drawer,ctx,x-r,y-r,x-r+t,y+r,kYellow);Rect(drawer,ctx,x+r-t,y-r,x+r,y+r,kYellow);
        return true;
    }
    const float r=14.0f*s,x=*sx,y=*sy;
    for(int k=0;k<6;++k) {   // a diamond from small squares along its four edges
        const float f=static_cast<float>(k)/6.0f,d=r*f,e=r-d;
        Rect(drawer,ctx,x+d-t,y-e-t,x+d+t,y-e+t,kRed);Rect(drawer,ctx,x-d-t,y-e-t,x-d+t,y-e+t,kRed);
        Rect(drawer,ctx,x+d-t,y+e-t,x+d+t,y+e+t,kRed);Rect(drawer,ctx,x-d-t,y+e-t,x-d+t,y+e+t,kRed);
    }
    return true;
}

// The picked store's lock (LockAt). The missile flies at what this marks (stores.cpp StoreLock reads the same entry the
// round will hold).
void LockMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetReadout& j) noexcept {
    float sx,sy;
    LockAt(drawer,ctx,vp,width,height,s,j.lock,j.lockAt,j.lockProgress,&sx,&sy);
}

// The stores: each store's name and rounds, the picked one in brackets, and the flares in the air.
void StoresText(wchar_t* text,std::size_t size,const PlayerJetReadout& j) noexcept {
    text[0]=L'\0';
    std::size_t at=0;
    for(int i=0;i<j.stores && i<6;++i) {
        const int n=_snwprintf_s(text+at,size-at,_TRUNCATE,i==j.store ? L"[%hs %d]  " : L"%hs %d  ",
                                 j.storeName[i] ? j.storeName[i] : "?",j.storeRounds[i]);
        if(n<0)break;
        at+=static_cast<std::size_t>(n);
    }
    if(j.air)_snwprintf_s(text+at,size-at,_TRUNCATE,L"FLARE %d",j.flares);
}
void StoresLine(Line& l,const PlayerJetReadout& j) noexcept {
    wchar_t text[128];
    StoresText(text,_countof(text),j);
    Format(l,L"%ls",text);
}

// The takeoff roll's cue (empty: none; `rotate`: the cue to pull up is showing). On the takeoff roll (not in the air:
// j.ground only says there is ground under it, so the cue stayed on after takeoff, 2026-10-05): the speed it may lift
// off from coming up, then the cue to pull up (the user, 2026-10-05).
void TakeoffCue(const PlayerJetReadout& j,wchar_t* cue,std::size_t size,bool* rotate) noexcept {
    const int rotateKmh=static_cast<int>(std::lround(j.rotate*3.6f));
    const bool rolling=!j.air && j.rotate>0.0f && j.speed>1.0f;
    *rotate=rolling && j.speed>=j.rotate;
    cue[0]=L'\0';
    if(*rotate)_snwprintf_s(cue,size,_TRUNCATE,L"ROTATE: PULL UP (W / SPACE)");
    else if(rolling && j.speed>=j.rotate*0.7f)_snwprintf_s(cue,size,_TRUNCATE,L"ROTATE AT %d km/h",rotateKmh);
}
// The old panel's cue (empty: none): the ground-proximity warning, a missile, a lock, else the takeoff's (TakeoffCue).
void CockpitCue(const PlayerJetReadout& j,wchar_t* cue,std::size_t size,bool* rotate) noexcept {
    TakeoffCue(j,cue,size,rotate);
    if(j.pullUp)_snwprintf_s(cue,size,_TRUNCATE,L"PULL UP! TERRAIN");
    else if(j.threat==2)_snwprintf_s(cue,size,_TRUNCATE,L"MISSILE!");
    else if(j.threat==1)_snwprintf_s(cue,size,_TRUNCATE,L"LOCKED");
}
// The cue's colour (over `calm` without one): the ground and a missile blink red and white (8 Hz), a lock is yellow,
// a stall red, the pull-up cue blinks green (4 Hz).
const float* CueColour(const PlayerJetReadout& j,bool rotate,const float* calm) noexcept {
    const bool blink=(GetTickCount64()/125)%2==0;
    return j.pullUp || j.threat==2 ? (blink ? kRed : kWhite) : j.threat==1 ? kYellow : j.stall ? kRed :
           rotate && blink ? kGreen : rotate ? kYellow : calm;
}

// The cockpit readout of the jet the player flies (drawn whatever VehicleHud says): at the bottom centre, its
// speed, height over the floor (over the world's zero, ALT*, with no ground under it) and climb, the throttle lever
// as a bar with the g it pulls (and STALL, red, when its wing cannot hold its path), its HP, and the controls (the
// pad's or the keyboard and mouse's, as the seat says it is flown).
void Cockpit(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetReadout& j,Line* lines,
             int* at) noexcept {
    if(*at+5>kMaxLines)return;
    Line& info=lines[(*at)++];
    Line& thr=lines[(*at)++];
    Line& arms=lines[(*at)++];
    Line& keys=lines[(*at)++];
    Line& keys2=lines[(*at)++];
    const float alt=std::fmax(-9999.0f,std::fmin(j.clear,99999.0f));
    Format(info,L"SPD %d km/h    ALT%ls %d m    %ls %d m/s    HP %d%%",static_cast<int>(std::lround(j.speed*3.6f)),
           j.ground ? L"" : L"*",static_cast<int>(std::lround(alt)),j.climb>=0.0f ? L"UP" : L"DOWN",
           static_cast<int>(std::lround(std::fabs(j.climb))),static_cast<int>(std::lround(j.hpMax>0.0f ? 100.0f*j.hp/j.hpMax : 0.0f)));
    const int rotateKmh=static_cast<int>(std::lround(j.rotate*3.6f));
    wchar_t cue[64];
    bool rotate=false;
    CockpitCue(j,cue,_countof(cue),&rotate);
    Format(thr,L"THROTTLE %d%%    G %.1f%ls%ls%ls",static_cast<int>(std::lround(j.throttle*100.0f)),j.load,j.stall ? L"    STALL" : L"",
           cue[0] ? L"    " : L"",cue);
    StoresLine(arms,j);
    if(j.keys) {
        wchar_t boost[32],brake[32],swap[32];
        KeyName(Cfg().playerJetBoostKey,boost,32);KeyName(Cfg().playerJetBrakeKey,brake,32);KeyName(Cfg().playerJetSwitchKey,swap,32);
        if(j.air) {
            Format(keys,Cfg().playerJetMouseFlight ? L"MOUSE: aim (the square)    W / SPACE: pull up    S: push down    A / D: roll"
                                                    : L"W / SPACE: pull up    S: push down    A / D: roll    (let go: wings level)");
            if(Cfg().playerJetMouseFlight)Format(keys,L"MOUSE: aim    W / SPACE / S / A / D: fly by hand (the mouse takes over once it moves)");
            wchar_t target[32];KeyName(Cfg().playerJetTargetKey,target,32);
            wchar_t flare[32];KeyName(Cfg().playerJetFlareKey,flare,32);
            Format(keys2,L"%ls: boost    %ls: brake    %ls: switch weapon    %ls: next target    %ls: flares",boost,brake,swap,target,flare);
        } else {
            Format(keys,L"%ls: throttle up    %ls: throttle down    A / D, MOUSE: steer",boost,brake);
            Format(keys2,L"W / SPACE: pull up to take off (from %d km/h)",rotateKmh);
        }
    } else if(j.air) {
        Format(keys,L"BOOST: forward / ascend    BRAKE: back    ROLL: left stick sideways");
        Format(keys2,L"PITCH, TURN: right stick    LB: switch weapon    X: next target");
    } else {
        Format(keys,L"THROTTLE: forward / ascend = up, back = down    TURN: sticks");
        Format(keys2,L"TAKE OFF: pull the right stick back (from %d km/h)",rotateKmh);
    }
    info.scale=kTitleScale;info.rgba=kWhite;
    thr.scale=kLineScale;
    thr.rgba=CueColour(j,rotate,kCyan);
    arms.scale=kLineScale;arms.rgba=j.bomb ? kYellow : kWhite;
    keys.scale=keys2.scale=kLineScale*0.85f;keys.rgba=keys2.rgba=kWhite;
    info.w=info.h=thr.w=thr.h=arms.w=arms.h=keys.w=keys.h=keys2.w=keys2.h=0.0f;
    if(text){MeasureAll(*text,&info,1);MeasureAll(*text,&thr,1);MeasureAll(*text,&arms,1);MeasureAll(*text,&keys,1);MeasureAll(*text,&keys2,1);}
    const float pad=8.0f*s,gap=5.0f*s,barW=320.0f*s,barH=10.0f*s;
    float w=barW;
    const Line* const parts[]={&info,&thr,&arms,&keys,&keys2};
    for(const Line* l:parts)w=l->w>w ? l->w : w;
    w+=2.0f*pad;
    const float lineH=info.h>0.0f ? info.h : 24.0f*s,smallH=thr.h>0.0f ? thr.h : 18.0f*s,keysH=keys.h>0.0f ? keys.h : 16.0f*s;
    const float h=pad+lineH+gap+smallH+gap*0.5f+barH+gap+smallH+gap+keysH+gap*0.5f+keysH+pad;
    const float x0=(width-w)*0.5f,y0=height*0.80f-h;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,kCyan);
    float y=y0+pad;
    info.x=(width-info.w)*0.5f;info.y=y;y+=lineH+gap;
    thr.x=(width-barW)*0.5f;thr.y=y;y+=smallH+gap*0.5f;
    Bar(drawer,ctx,(width-barW)*0.5f,y,barW,barH,j.throttle,j.throttle,kCyan,s);
    y+=barH+gap;
    arms.x=(width-arms.w)*0.5f;arms.y=y;y+=smallH+gap;
    keys.x=(width-keys.w)*0.5f;keys.y=y;y+=keysH+gap*0.5f;
    keys2.x=(width-keys2.w)*0.5f;keys2.y=y;
}

// --- The fighter HUD (the user, 2026-10-05: "the HUD and the parts a jet should have; locked on, it should show the
// direction"; ini PlayerJetGunSight / PlayerJetFlightHud / PlayerJetThreatHud): for the aircraft the player flies,
// whichever it is (the PlayerJetSymbols playerjet.cpp gathers). Drawn with the quad of the bars (as thin quads at any
// angle and as triangles) and the text of the rest, so it is there in exclusive full screen too. The horizon, the pitch
// ladder, the flight path marker and the boresight are directions (sight::ToScreen with w 0: where they vanish), so
// they lie on the view as the world does wherever the camera stands; the pipper and the lead mark are points. ---
alignas(16) const float kHud[4]={0.35f,1.0f,0.5f,0.95f};
alignas(16) const float kHudDim[4]={0.35f,1.0f,0.5f,0.45f};
constexpr float kDeg=0.0174532925f,kTurn=6.2831853f;

// A line `t` pixels thick from (x0, y0) to (x1, y1): Rect's quad, turned (Rect's winding).
void Seg(void* drawer,void* ctx,float x0,float y0,float x1,float y1,float t,const float* rgba) noexcept {
    const float dx=x1-x0,dy=y1-y0,len=std::sqrt(dx*dx+dy*dy);
    if(!std::isfinite(len) || len<0.01f)return;
    const float nx=-dy/len*t*0.5f,ny=dx/len*t*0.5f;
    alignas(16) const float m[16]={1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f};
    alignas(16) const float v[12]={x0-nx,y0-ny,0.0f, x1-nx,y1-ny,0.0f, x0+nx,y0+ny,0.0f, x1+nx,y1+ny,0.0f};
    reinterpret_cast<QuadFn>(image+kQuad)(drawer,ctx,m,rgba,kStrip,v,4,nullptr);
}
// A triangle, its tip at (tx, ty), its base `half` either side of (bx, by) (Seg's winding; the strip's second
// triangle has the tip twice: nothing).
void Tri(void* drawer,void* ctx,float bx,float by,float tx,float ty,float half,const float* rgba) noexcept {
    const float dx=tx-bx,dy=ty-by,len=std::sqrt(dx*dx+dy*dy);
    if(!std::isfinite(len) || len<0.01f)return;
    const float nx=-dy/len*half,ny=dx/len*half;
    alignas(16) const float m[16]={1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f};
    alignas(16) const float v[12]={bx-nx,by-ny,0.0f, tx,ty,0.0f, bx+nx,by+ny,0.0f, tx,ty,0.0f};
    reinterpret_cast<QuadFn>(image+kQuad)(drawer,ctx,m,rgba,kStrip,v,4,nullptr);
}
// An arc of radius `r` round (cx, cy) from angle `from` (rad, 0 to the right, growing clockwise on the screen) over
// `span`, in `sides` straight pieces; a whole ring with span kTurn.
void Arc(void* drawer,void* ctx,float cx,float cy,float r,float from,float span,float t,int sides,const float* rgba) noexcept {
    for(int i=0;i<sides;++i) {
        const float a0=from+span*static_cast<float>(i)/static_cast<float>(sides),a1=from+span*static_cast<float>(i+1)/static_cast<float>(sides);
        Seg(drawer,ctx,cx+r*std::cos(a0),cy+r*std::sin(a0),cx+r*std::cos(a1),cy+r*std::sin(a1),t,rgba);
    }
}
// The world directions `a` and `b` joined on the screen (both in front of the eye); `dashes` > 0: that many dashes.
void DirSeg(void* drawer,void* ctx,const float* vp,float width,float height,const float* a,const float* b,float t,int dashes,
            const float* rgba) noexcept {
    float x0,y0,x1,y1;
    if(!sight::ToScreen(vp,a,0.0f,width,height,&x0,&y0) || !sight::ToScreen(vp,b,0.0f,width,height,&x1,&y1))return;
    if(dashes<=0){Seg(drawer,ctx,x0,y0,x1,y1,t,rgba);return;}
    const int pieces=2*dashes-1;
    for(int i=0;i<pieces;i+=2) {
        const float f0=static_cast<float>(i)/static_cast<float>(pieces),f1=static_cast<float>(i+1)/static_cast<float>(pieces);
        Seg(drawer,ctx,x0+(x1-x0)*f0,y0+(y1-y0)*f0,x0+(x1-x0)*f1,y0+(y1-y0)*f1,t,rgba);
    }
}
// A line of text at (x, y): `align` 0 its left there, 1 its middle, 2 its right; its height centred on y.
void Label(Text* text,Line* lines,int* at,float x,float y,int align,float scale,const float* rgba,const wchar_t* format,...) noexcept {
    if(*at>=kMaxLines)return;
    Line& l=lines[(*at)++];
    va_list args;va_start(args,format);
    _vsnwprintf_s(l.text,_countof(l.text),_TRUNCATE,format,args);
    va_end(args);
    l.scale=scale;l.rgba=rgba;l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
    l.x=x-(align==1 ? l.w*0.5f : align==2 ? l.w : 0.0f);l.y=y-l.h*0.5f;
}

// The level heading the ladder and the tape go by: the flight path's, else (hovering, straight up or down) the nose's.
bool LevelHeading(const PlayerJetSymbols& y,float* h) noexcept {
    const float* ref=y.moving ? y.dir : y.nose;
    h[0]=ref[0];h[1]=0.0f;h[2]=ref[2];
    if(vec::Normalize(h))return true;
    h[0]=y.nose[0];h[2]=y.nose[2];
    return vec::Normalize(h);
}

// The horizon and the pitch ladder: a rung every kLadderStep degrees within kLadderShown of the path's climb, its two
// halves from kLadderGap to kLadderHalf off the path's heading (tangents), a tab at each outer end pointing to the
// horizon, the dives' rungs dashed, the climb's degrees at their ends; the horizon out to kHorizonHalf (60 degrees).
constexpr int kLadderStep=10;
constexpr float kLadderShown=25.0f,kLadderGap=0.035f,kLadderHalf=0.13f,kHorizonHalf=1.7f,kLadderTab=0.012f;
void Ladder(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const PlayerJetSymbols& y,
            Line* lines,int* at) noexcept {
    float h[3];
    if(!LevelHeading(y,h))return;
    const float r[3]={-h[2],0.0f,h[0]};   // its level right (playerjet.cpp RightOf)
    const float* ref=y.moving ? y.dir : y.nose;
    const float climb=std::asin(vec::Clamp(ref[1],-1.0f,1.0f))/kDeg,t=2.0f*s;
    for(int e=-90+kLadderStep;e<90;e+=kLadderStep) {
        if(std::fabs(static_cast<float>(e)-climb)>kLadderShown)continue;
        const float co=std::cos(static_cast<float>(e)*kDeg),si=std::sin(static_cast<float>(e)*kDeg);
        const float c[3]={h[0]*co,si,h[2]*co};
        const float inner=e==0 ? 2.0f*kLadderGap : kLadderGap,outer=e==0 ? kHorizonHalf : kLadderHalf;
        for(int side=-1;side<=1;side+=2) {
            const float k=static_cast<float>(side);
            const float a[3]={c[0]+r[0]*inner*k,c[1],c[2]+r[2]*inner*k},b[3]={c[0]+r[0]*outer*k,c[1],c[2]+r[2]*outer*k};
            DirSeg(drawer,ctx,vp,width,height,a,b,t,e<0 ? 3 : 0,kHud);
            if(e==0)continue;
            const float tab[3]={b[0],b[1]-(e>0 ? kLadderTab : -kLadderTab),b[2]};
            DirSeg(drawer,ctx,vp,width,height,b,tab,t,0,kHud);
            float lx,ly;
            if(sight::ToScreen(vp,b,0.0f,width,height,&lx,&ly) && lx>0.0f && lx<width && ly>0.0f && ly<height)
                Label(text,lines,at,lx+k*8.0f*s,ly,side>0 ? 0 : 2,kLineScale*0.85f,kHud,L"%d",e);
        }
    }
}

// The flight path marker (velocity vector): a circle with wings and a fin where the aircraft goes; none hovering.
void FlightPath(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetSymbols& y) noexcept {
    float x,yy;
    if(!y.moving || !sight::ToScreen(vp,y.dir,0.0f,width,height,&x,&yy))return;
    const float r=8.0f*s,t=2.0f*s;
    Arc(drawer,ctx,x,yy,r,0.0f,kTurn,t,12,kHud);
    Seg(drawer,ctx,x-r-12.0f*s,yy,x-r,yy,t,kHud);Seg(drawer,ctx,x+r,yy,x+r+12.0f*s,yy,t,kHud);
    Seg(drawer,ctx,x,yy-r,x,yy-r-8.0f*s,t,kHud);
}

// The heading tape at the top: a tick every 5 degrees within kTapeHalf of the heading (sight::HeadingOf: a compass's,
// 0 along the world's +Z), the tens numbered, a caret under the middle and the heading under it.
constexpr float kTapeHalf=30.0f,kTapePx=6.0f;   // degrees either side; pixels a degree (at 1080 lines)
void HeadingTape(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetSymbols& y,Line* lines,
                 int* at) noexcept {
    float h[3];
    if(!LevelHeading(y,h))return;
    const float hdg=sight::HeadingOf(h);
    const float cx=width*0.5f,base=height*0.11f,k=kTapePx*s,t=2.0f*s;
    Seg(drawer,ctx,cx-kTapeHalf*k,base,cx+kTapeHalf*k,base,t,kHudDim);
    for(int d=static_cast<int>(std::ceil((hdg-kTapeHalf)/5.0f))*5;static_cast<float>(d)<=hdg+kTapeHalf;d+=5) {
        const float x=cx+(static_cast<float>(d)-hdg)*k;
        const bool ten=d%10==0;
        Seg(drawer,ctx,x,base,x,base-(ten ? 10.0f : 5.0f)*s,t,kHud);
        if(ten)Label(text,lines,at,x,base-22.0f*s,1,kLineScale*0.8f,kHud,L"%03d",((d%360)+360)%360);
    }
    Tri(drawer,ctx,cx,base+12.0f*s,cx,base+2.0f*s,6.0f*s,kHud);
    Label(text,lines,at,cx,base+26.0f*s,1,kLineScale,kHud,L"%03d",static_cast<int>(std::lround(hdg))%360);
}

// The boxes left and right of the middle: the speed (km/h) with `under` under it (the jets' g, the helis' speed set), the
// height over the floor (ALT*: over the world's zero, nothing under it) with the climb under it. The landing gear's
// indicator (branch feat/jet-gear) has the row under the left one: (width / 2 - kBoxOff * s, height / 2 + 2 * kBoxRow * s).
constexpr float kBoxOff=280.0f,kBoxW=120.0f,kBoxH=34.0f,kBoxRow=30.0f;
constexpr float kBarHalf=100.0f;   // px: the half length of the bars beside the boxes (a heli's height, a jet's lift)
void Boxes(void* drawer,void* ctx,Text* text,float width,float height,float s,float speed,const wchar_t* under,const float* underRgba,
           float clear,bool ground,float climb,Line* lines,int* at) noexcept {
    const float cy=height*0.5f,w=kBoxW*s*0.5f,hh=kBoxH*s*0.5f,t=2.0f*s;
    const float left=width*0.5f-kBoxOff*s,right=width*0.5f+kBoxOff*s;
    const float boxes[2]={left,right};
    for(const float x:boxes) {
        Seg(drawer,ctx,x-w,cy-hh,x+w,cy-hh,t,kHud);Seg(drawer,ctx,x-w,cy+hh,x+w,cy+hh,t,kHud);
        Seg(drawer,ctx,x-w,cy-hh,x-w,cy+hh,t,kHud);Seg(drawer,ctx,x+w,cy-hh,x+w,cy+hh,t,kHud);
    }
    const float alt=std::fmax(-9999.0f,std::fmin(clear,99999.0f));
    Label(text,lines,at,left,cy,1,kTitleScale,kHud,L"%d",static_cast<int>(std::lround(speed*3.6f)));
    Label(text,lines,at,left,cy-hh-12.0f*s,1,kLineScale*0.8f,kHud,L"KM/H");
    if(under && under[0])Label(text,lines,at,left,cy+kBoxRow*s,1,kLineScale,underRgba,L"%ls",under);
    Label(text,lines,at,right,cy,1,kTitleScale,kHud,L"%d",static_cast<int>(std::lround(alt)));
    Label(text,lines,at,right,cy-hh-12.0f*s,1,kLineScale*0.8f,kHud,ground ? L"ALT M" : L"ALT* M");
    Label(text,lines,at,right,cy+kBoxRow*s,1,kLineScale,kHud,L"VS %+d",static_cast<int>(std::lround(climb)));
}
void SpeedAltBoxes(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetReadout& j,Line* lines,
                   int* at) noexcept {
    wchar_t g[16];
    std::swprintf(g,16,L"G %.1f",j.load);
    Boxes(drawer,ctx,text,width,height,s,j.speed,g,j.load>5.0f ? kWarn : kHud,j.clear,j.ground,j.climb,lines,at);
}

// The boresight: a cross with a gap at its centre on the direction `dir` (where it vanishes: the guns' line, far).
void Boresight(void* drawer,void* ctx,const float* vp,float width,float height,float s,const float* dir) noexcept {
    const float t=2.0f*s,r=10.0f*s,g=3.0f*s;
    float x,y;
    if(!sight::ToScreen(vp,dir,0.0f,width,height,&x,&y))return;
    Seg(drawer,ctx,x-r,y,x-g,y,t,kHud);Seg(drawer,ctx,x+g,y,x+r,y,t,kHud);
    Seg(drawer,ctx,x,y-r,x,y-g,t,kHud);Seg(drawer,ctx,x,y+g,x,y+r,t,kHud);
}
// The pipper: a circle of radius kPipper with a dot in it at the point `at`, its screen point in (x, y); false off
// the screen.
constexpr float kPipper=18.0f;
bool Pipper(void* drawer,void* ctx,const float* vp,float width,float height,float s,const float* at,const float* rgba,float* x,
            float* y) noexcept {
    if(!sight::ToScreen(vp,at,1.0f,width,height,x,y))return false;
    Arc(drawer,ctx,*x,*y,kPipper*s,0.0f,kTurn,2.0f*s,16,rgba);
    Rect(drawer,ctx,*x-1.5f*s,*y-1.5f*s,*x+1.5f*s,*y+1.5f*s,rgba);
    return true;
}

// The guns' sight: a cross on the boresight (where the nose points: the guns' line), the pipper (a circle, a dot in it)
// where the rounds fired now will be at the target's range (else at the sight's own), the target's lead mark (a cross
// in a circle, dim out of the rounds' reach) and the range as an arc round the pipper (from its top, the share of the
// reach). Pipper on the lead mark: the rounds meet the target.
void GunSight(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetSymbols& y) noexcept {
    if(!y.gun)return;
    const float t=2.0f*s;
    float x,yy;
    Boresight(drawer,ctx,vp,width,height,s,y.nose);
    if(Pipper(drawer,ctx,vp,width,height,s,y.pipper,kHud,&x,&yy) && y.lead && y.gunRange>0.0f) {
        const float share=vec::Clamp(y.leadRange/y.gunRange,0.0f,1.0f);
        const int sides=static_cast<int>(std::ceil(share*24.0f));
        if(sides>0)Arc(drawer,ctx,x,yy,(kPipper+5.0f)*s,-0.25f*kTurn,share*kTurn,3.0f*s,sides,y.leadInRange ? kHud : kHudDim);
    }
    if(y.lead && sight::ToScreen(vp,y.leadAt,1.0f,width,height,&x,&yy)) {
        const float r=7.0f*s;
        const float* c=y.leadInRange ? kHud : kHudDim;
        Arc(drawer,ctx,x,yy,r,0.0f,kTurn,t,10,c);
        Seg(drawer,ctx,x-r,yy-r,x+r,yy+r,t,c);Seg(drawer,ctx,x-r,yy+r,x+r,yy-r,t,c);
    }
}

// A stock helicopter's weapons' sight (helisight.cpp; ini PlayerHeliGunSight). The gun (the primary trigger's): the
// jet sight's boresight and pipper, the pipper where a round fired now first hits the ground (dim: none in its life,
// it is where the round ends), its distance in metres to its right. The other weapon (the secondary button's), each
// in its own shape so neither is taken for the gun's pipper: the missile's lock as the jets' (LockAt: a yellow square
// closing in, a red diamond locked) with MSL and its distance, or with no lock a dim ring of kMissileRing round its
// boresight with MSL and the range it locks within; the rockets' mark a hollow diamond where their path (flown as the
// game flies them: vhud.h RoundLands) meets the map with its label (RKT, GREN...) and distance (none met: a dim one on
// their boresight).
constexpr float kMissileRing=34.0f,kRocketMark=11.0f;   // px at 1080 lines
void RocketDiamond(void* drawer,void* ctx,float x,float y,float s,const float* rgba) noexcept {
    const float r=kRocketMark*s,t=2.0f*s;
    Seg(drawer,ctx,x,y-r,x+r,y,t,rgba);Seg(drawer,ctx,x+r,y,x,y+r,t,rgba);
    Seg(drawer,ctx,x,y+r,x-r,y,t,rgba);Seg(drawer,ctx,x-r,y,x,y-r,t,rgba);
}
void HeliGunSight(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const HeliSightReadout& h,
                  Line* lines,int* at) noexcept {
    float x,y;
    const float note=kLineScale*0.85f;
    if(h.gun) {
        const float* c=h.hit ? kHud : kHudDim;
        Boresight(drawer,ctx,vp,width,height,s,h.bore);
        if(Pipper(drawer,ctx,vp,width,height,s,h.pipper,c,&x,&y))
            Label(text,lines,at,x+(kPipper+8.0f)*s,y,0,note,c,L"%d m",static_cast<int>(std::lround(h.range)));
    }
    if(h.arm==HeliArm::missile) {
        if(LockAt(drawer,ctx,vp,width,height,s,h.lock,h.armAt,h.lockProgress,&x,&y))
            Label(text,lines,at,x,y+44.0f*s,1,note,h.lock==2 ? kRed : kYellow,L"MSL %d m",static_cast<int>(std::lround(h.armRange)));
        else if(!h.lock && sight::ToScreen(vp,h.armBore,0.0f,width,height,&x,&y)) {
            Arc(drawer,ctx,x,y,kMissileRing*s,0.0f,kTurn,2.0f*s,24,kHudDim);
            Label(text,lines,at,x,y+(kMissileRing+12.0f)*s,1,note,kHudDim,L"MSL %d m",static_cast<int>(std::lround(h.lockRange)));
        }
    } else if(h.arm==HeliArm::rockets) {
        if(h.armHit && sight::ToScreen(vp,h.armAt,1.0f,width,height,&x,&y)) {
            RocketDiamond(drawer,ctx,x,y,s,kHud);
            Label(text,lines,at,x,y+(kRocketMark+12.0f)*s,1,note,kHud,L"%hs %d m",h.armLabel ? h.armLabel : "RKT",
                  static_cast<int>(std::lround(h.armRange)));
        } else if(sight::ToScreen(vp,h.armBore,0.0f,width,height,&x,&y)) {
            RocketDiamond(drawer,ctx,x,y,s,kHudDim);
            Label(text,lines,at,x,y+(kRocketMark+12.0f)*s,1,note,kHudDim,L"%hs",h.armLabel ? h.armLabel : "RKT");
        }
    }
}

// The mouse-aim flight's aim of a heli or rotor craft (heliaim.h): the jets' cyan square, FLY beside it, so it is not
// taken for a weapon's mark (the user, 2026-10-05: the flight's aim and the gun's pipper were confused). The jets keep
// the bare square: theirs is where both the flight and the nose's guns go.
void FlightAim(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const float* p,Line* lines,
               int* at) noexcept {
    float x,y,depth;
    AimSquare(drawer,ctx,vp,width,height,s,p);
    if(Project(vp,p,width,height,&x,&y,&depth))Label(text,lines,at,x+22.0f*s,y,0,kLineScale*0.75f,kCyan,L"FLY");
}

// --- The warnings (the user, 2026-10-06: "告警换成更现实一点的如何。比如圆圈告警这种。还有拉起、失速提示音和拉起提示"):
// what warn.cpp decides, drawn as a real cockpit shows it instead of a line of text: the threats on a radar warning
// receiver's scope (RwrScope) and marked in view (ThreatMarks), the ground's chevrons, break X and pull-up arrow
// (GroundCue), the stall's lift tape and box (StallCue), and every warning lit on a fixed annunciator (Annunciator). ---
alignas(16) const float kInk[4]={0.03f,0.03f,0.03f,1.0f};

// The threats (the user, 2026-10-05: "locked on, it should show the direction"; 2026-10-06: "圆圈告警"): a radar warning
// receiver's scope low at the left of the HUD, heading up (the aircraft's level nose at its top, its right at its
// right), two rings, a tick every 30 degrees and the aircraft at its centre. Each threat at its bearing off the nose,
// nearer the centre the nearer it is (kRwrRange at the outer ring, spaced by the square root: the near ones apart, as
// a real scope puts the most lethal inside): an enemy's lock an amber J under an airborne threat's hat, a missile a red
// M with its distance (km), the nearest one in a diamond (the priority threat). A launch (warn.cpp: more missiles
// coming than a moment before) flashes the scope and its missiles red, LAUNCH over it, for kLaunchMs. With none the
// scope stays, dim: a real one is always there.
constexpr float kRwrR=86.0f,kRwrRange=5000.0f;   // px at 1080 lines; m at the outer ring
void RwrScope(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetSymbols& y,ULONGLONG launchAt,
              ULONGLONG now,Line* lines,int* at) noexcept {
    const float r=kRwrR*s,cx=width*0.5f-(kBoxOff+kBoxW*0.5f+60.0f)*s-r,cy=height*0.80f-r-10.0f*s,t=2.0f*s;
    const bool launch=launchAt && now-launchAt<kLaunchMs,blink=(now/125)%2==0;
    Arc(drawer,ctx,cx,cy,r,0.0f,kTurn,launch ? 3.0f*s : t,48,launch && blink ? kRed : y.threats>0 ? kHud : kHudDim);
    Arc(drawer,ctx,cx,cy,r*0.5f,0.0f,kTurn,t,32,kHudDim);
    for(int i=0;i<12;++i) {
        const float a=static_cast<float>(i)*kTurn/12.0f,dx=std::sin(a),dy=-std::cos(a),tick=(i==0 ? 10.0f : 5.0f)*s;
        Seg(drawer,ctx,cx+dx*r,cy+dy*r,cx+dx*(r+tick),cy+dy*(r+tick),t,y.threats>0 ? kHud : kHudDim);
    }
    Seg(drawer,ctx,cx-9.0f*s,cy+1.0f*s,cx+9.0f*s,cy+1.0f*s,t,kHud);   // the aircraft: wings, fuselage, tail
    Seg(drawer,ctx,cx,cy-7.0f*s,cx,cy+8.0f*s,t,kHud);
    Seg(drawer,ctx,cx-4.0f*s,cy+7.0f*s,cx+4.0f*s,cy+7.0f*s,t,kHud);
    if(launch)Label(text,lines,at,cx,cy-r-22.0f*s,1,kLineScale,blink ? kRed : kWhite,L"LAUNCH");
    float fwd[3]={y.nose[0],0.0f,y.nose[2]};
    if(y.threats<=0 || !vec::Normalize(fwd))return;
    const float right[3]={-fwd[2],0.0f,fwd[0]};   // its level right (playerjet.cpp RightOf)
    int nearest=-1;
    for(int i=0;i<y.threats && i<kMostThreats;++i)
        if(y.threatKind[i]==2 && (nearest<0 || vec::Dist(y.threatAt[i],y.pos)<vec::Dist(y.threatAt[nearest],y.pos)))nearest=i;
    for(int i=0;i<y.threats && i<kMostThreats;++i) {
        const float* p=y.threatAt[i];
        const float d[3]={p[0]-y.pos[0],p[1]-y.pos[1],p[2]-y.pos[2]},dist=vec::Dist(p,y.pos);
        const float across=d[0]*right[0]+d[2]*right[2],along=d[0]*fwd[0]+d[2]*fwd[2];
        const float b=across*across+along*along>1e-6f ? std::atan2(across,along) : 0.0f;
        const float rr=r*(0.15f+0.85f*std::sqrt(Unit(dist/kRwrRange)));
        const float x=cx+std::sin(b)*rr,yy=cy-std::cos(b)*rr;
        if(y.threatKind[i]!=2) {
            Label(text,lines,at,x,yy+2.0f*s,1,kLineScale*0.8f,kAmber,L"J");
            Seg(drawer,ctx,x-8.0f*s,yy-8.0f*s,x,yy-14.0f*s,t,kAmber);Seg(drawer,ctx,x,yy-14.0f*s,x+8.0f*s,yy-8.0f*s,t,kAmber);
            continue;
        }
        const float* c=launch && !blink ? kWhite : kRed;
        Label(text,lines,at,x,yy,1,kLineScale*0.8f,c,L"M");
        Label(text,lines,at,x+12.0f*s,yy+12.0f*s,0,kLineScale*0.6f,kRed,L"%.1f",dist*0.001f);
        if(i!=nearest)continue;
        const float q=13.0f*s;
        Seg(drawer,ctx,x,yy-q,x+q,yy,t,c);Seg(drawer,ctx,x+q,yy,x,yy+q,t,c);
        Seg(drawer,ctx,x,yy+q,x-q,yy,t,c);Seg(drawer,ctx,x-q,yy,x,yy-q,t,c);
    }
}

// A threat in view, marked where it is: a missile a red circle with a cross, an enemy jet amber brackets.
void ThreatMarks(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetSymbols& y) noexcept {
    const float t=2.0f*s;
    for(int i=0;i<y.threats && i<kMostThreats;++i) {
        const float* p=y.threatAt[i];
        const bool m=y.threatKind[i]==2;
        float x,yy;
        if(!sight::ToScreen(vp,p,1.0f,width,height,&x,&yy) || x<0.0f || x>width || yy<0.0f || yy>height)continue;
        const float q=14.0f*s;
        if(m) {
            Arc(drawer,ctx,x,yy,q,0.0f,kTurn,t,12,kRed);
            Seg(drawer,ctx,x-q*0.6f,yy,x+q*0.6f,yy,t,kRed);Seg(drawer,ctx,x,yy-q*0.6f,x,yy+q*0.6f,t,kRed);
            continue;
        }
        for(int side=-1;side<=1;side+=2) {   // brackets either side
            const float k=static_cast<float>(side);
            Seg(drawer,ctx,x+k*q,yy-q,x+k*q*0.4f,yy-q,t,kAmber);Seg(drawer,ctx,x+k*q,yy-q,x+k*q,yy+q,t,kAmber);
            Seg(drawer,ctx,x+k*q,yy+q,x+k*q*0.4f,yy+q,t,kAmber);
        }
    }
}
void Threats(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const PlayerJetSymbols& y,
             ULONGLONG launchAt,Line* lines,int* at) noexcept {
    RwrScope(drawer,ctx,text,width,height,s,y,launchAt,GetTickCount64(),lines,at);
    ThreatMarks(drawer,ctx,vp,width,height,s,y);
}

// The ground (warn.cpp's GPWS; the F-16's and its Auto-GCAS's symbols): two chevrons either side of the flight path marker
// (the nose's point hovering) closing in on it as the impact nears, amber from kTerrainSeconds out, red at
// kPullUpSeconds, the seconds to the impact under it; with PULL UP a large blinking break X over the HUD's centre, PULL
// UP under it, and an arrow off the marker the way the path must go to climb away (toward the sky: rolled over, that
// is down the screen).
void GroundCue(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const PlayerJetSymbols& y,Gpws g,
               float impactIn,Line* lines,int* at) noexcept {
    if(g==Gpws::none)return;
    const ULONGLONG now=GetTickCount64();
    const bool pull=g==Gpws::pullUp,blink=(now/125)%2==0;
    const float* c=pull ? (blink ? kRed : kWhite) : kAmber;
    const float* base=y.moving ? y.dir : y.nose;
    float cx=width*0.5f,cy=height*0.5f,x,yy;
    if(sight::ToScreen(vp,base,0.0f,width,height,&x,&yy)){cx=x;cy=yy;}
    const float t=3.0f*s,k=12.0f*s;
    const float off=(30.0f+110.0f*Unit((impactIn-kPullUpSeconds)/(kTerrainSeconds-kPullUpSeconds)))*s;
    for(int side=-1;side<=1;side+=2) {   // pointing in at the marker
        const float e=static_cast<float>(side),bx=cx+e*off;
        Seg(drawer,ctx,bx,cy-k,bx-e*k,cy,t,c);Seg(drawer,ctx,bx-e*k,cy,bx,cy+k,t,c);
    }
    if(impactIn>=0.0f)Label(text,lines,at,cx,cy+28.0f*s,1,kLineScale*0.8f,c,L"%.1f s",impactIn);
    // The way the path's point moves on the screen as the path turns toward the world's up across it: the projection's
    // derivative there ((u w_b - b w_u) / w_b^2 of the clip coordinates, the square dropped: only the way counts), so it
    // holds with the marker off the screen too (a steep sink: the escape's point on and the path's off it pointed the
    // arrow down, found offline with tools/hud_view.cpp). Behind the eye or straight down: up the screen.
    float up[3]={-base[0]*base[1],1.0f-base[1]*base[1],-base[2]*base[1]};   // the world's up across the path
    float dx=0.0f,dy=-1.0f;
    if(vec::Normalize(up)) {
        float b[4],u[4];
        for(int i=0;i<4;++i){b[i]=base[0]*vp[i]+base[1]*vp[4+i]+base[2]*vp[8+i];u[i]=up[0]*vp[i]+up[1]*vp[4+i]+up[2]*vp[8+i];}
        const float mx=(u[0]*b[3]-b[0]*u[3])*width*0.5f,my=-(u[1]*b[3]-b[1]*u[3])*height*0.5f,len=std::sqrt(mx*mx+my*my);
        if(b[3]>1e-6f && std::isfinite(len) && len>1e-6f){dx=mx/len;dy=my/len;}
    }
    const float from=22.0f*s,to=(pull ? 95.0f : 70.0f)*s,head=14.0f*s;
    Seg(drawer,ctx,cx+dx*from,cy+dy*from,cx+dx*(to-head),cy+dy*(to-head),4.0f*s,c);
    Tri(drawer,ctx,cx+dx*(to-head),cy+dy*(to-head),cx+dx*to,cy+dy*to,9.0f*s,c);
    if(!pull)return;
    const float mx=width*0.5f,my=height*0.5f,r=110.0f*s;
    Seg(drawer,ctx,mx-r,my-r,mx+r,my+r,5.0f*s,c);Seg(drawer,ctx,mx-r,my+r,mx+r,my-r,5.0f*s,c);
    Label(text,lines,at,mx,my+r+22.0f*s,1,kTitleScale,c,L"PULL UP");
}

// The stall (the user: "失速提示"): a lift tape left of the speed box, the share of all the wing gives that its path needs
// (an AoA indexer's job: this flight model has the wing's lift, not an angle of attack, playerjet.cpp Air) from 0 at
// the bottom to the stall at the top, red over kStallNear, a caret at the share; stalled, a blinking STALL box beside
// the flight path marker. `share` 0: on the ground or a rotor craft, none.
constexpr float kStallNear=0.85f;
void StallCue(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const PlayerJetSymbols& y,float share,
              bool stall,Line* lines,int* at) noexcept {
    if(!(share>0.0f))return;
    const bool blink=(GetTickCount64()/125)%2==0;
    const float x=width*0.5f-(kBoxOff+kBoxW*0.5f+18.0f)*s,cy=height*0.5f,half=kBarHalf*s,t=2.0f*s;
    const float bottom=cy+half,per=2.0f*half;
    Seg(drawer,ctx,x,cy-half,x,bottom,t,kHudDim);
    Seg(drawer,ctx,x,cy-half,x,bottom-per*kStallNear,4.0f*s,kRed);
    for(int q=0;q<=4;++q)Seg(drawer,ctx,x,bottom-per*0.25f*static_cast<float>(q),x+(q%2==0 ? 10.0f : 6.0f)*s,bottom-per*0.25f*static_cast<float>(q),t,kHud);
    const float yy=bottom-per*Unit(share);
    const float* c=stall ? kRed : share>kStallNear ? kAmber : kHud;
    Tri(drawer,ctx,x-14.0f*s,yy,x-2.0f*s,yy,6.0f*s,c);
    Label(text,lines,at,x,cy-half-14.0f*s,1,kLineScale*0.7f,kHudDim,L"LIFT");
    if(!stall)return;
    float fx=width*0.5f,fy=height*0.5f,px,py;
    if(y.moving && sight::ToScreen(vp,y.dir,0.0f,width,height,&px,&py)){fx=px;fy=py;}
    const float* box=blink ? kRed : kWhite;
    const int first=*at;
    Label(text,lines,at,fx+46.0f*s,fy,0,kTitleScale,box,L"STALL");
    if(*at==first)return;
    const Line& l=lines[first];
    const float w=(l.w>0.0f ? l.w : 70.0f*s)+12.0f*s,h=(l.h>0.0f ? l.h : 24.0f*s)+8.0f*s,x0=l.x-6.0f*s,y0=fy-h*0.5f;
    Seg(drawer,ctx,x0,y0,x0+w,y0,t,box);Seg(drawer,ctx,x0,y0+h,x0+w,y0+h,t,box);
    Seg(drawer,ctx,x0,y0,x0,y0+h,t,box);Seg(drawer,ctx,x0+w,y0,x0+w,y0+h,t,box);
}

// The annunciator (a real cockpit's master caution and its lights: one fixed place to look): a row of lit tiles under
// the heading tape, MASTER WARN (red) or MASTER CAUTION (amber) filled first, then each warning lit in warn.h's order,
// the warnings red and the cautions amber in a frame; the warnings blink, and so does a caution its first kNewMs.
constexpr ULONGLONG kNewMs=3000;
const wchar_t* const kWarnText[kWarnCount]={L"PULL UP",L"MISSILE",L"STALL",L"GEAR",L"TERRAIN",L"SINK RATE",L"LOCK",L"GEAR SPEED",
                                            L"WEIGHT ON WHEELS"};
void Annunciator(void* drawer,void* ctx,Text* text,float width,float height,float s,const Warnings& w,Line* lines,int* at) noexcept {
    if(!w.on)return;
    const ULONGLONG now=GetTickCount64();
    const bool blink=(now/125)%2==0;
    int lit[kWarnCount],n=0;
    bool warning=false;
    for(int k=0;k<kWarnCount;++k)if(w.on>>k&1u){lit[n++]=k;warning=warning || IsWarning(k);}
    if(*at+n+1>kMaxLines)return;
    Line* const tile=&lines[*at];
    Format(tile[0],warning ? L"MASTER WARN" : L"MASTER CAUTION");
    tile[0].rgba=kInk;
    for(int i=0;i<n;++i) {
        const int k=lit[i];
        const bool launch=k==kWarnMissile && w.launchAt && now-w.launchAt<kLaunchMs;
        Format(tile[1+i],L"%ls",launch ? L"MSL LAUNCH" : kWarnText[k]);
        const bool flash=IsWarning(k) || now-w.litAt[k]<kNewMs;
        tile[1+i].rgba=flash && !blink ? kWhite : IsWarning(k) ? kRed : kAmber;
    }
    for(int i=0;i<=n;++i){tile[i].scale=kLineScale*0.85f;tile[i].w=tile[i].h=0.0f;}
    if(text)MeasureAll(*text,tile,n+1);
    const float pad=8.0f*s,gap=6.0f*s,t=2.0f*s;
    float textH=0.0f,total=-gap;
    for(int i=0;i<=n;++i){textH=tile[i].h>textH ? tile[i].h : textH;total+=(tile[i].w>0.0f ? tile[i].w : 80.0f*s)+2.0f*pad+gap;}
    const float h=(textH>0.0f ? textH : 18.0f*s)+2.0f*4.0f*s,y0=height*0.11f+44.0f*s;
    float x=(width-total)*0.5f;
    for(int i=0;i<=n;++i) {
        Line& l=tile[i];
        const float tw=(l.w>0.0f ? l.w : 80.0f*s)+2.0f*pad;
        const float* c=i==0 ? (warning ? kRed : kAmber) : l.rgba;
        if(i==0)Rect(drawer,ctx,x,y0,x+tw,y0+h,c);
        else {
            Rect(drawer,ctx,x,y0,x+tw,y0+h,kBarBack);
            Rect(drawer,ctx,x,y0,x+tw,y0+t,c);Rect(drawer,ctx,x,y0+h-t,x+tw,y0+h,c);
            Rect(drawer,ctx,x,y0,x+t,y0+h,c);Rect(drawer,ctx,x+tw-t,y0,x+tw,y0+h,c);
        }
        l.x=x+pad;l.y=y0+(h-l.h)*0.5f;
        x+=tw+gap;
    }
    *at+=n+1;
}

// The fighter HUD of the aircraft the player flies, part by part as the ini switches them (`launchAt`: warn.cpp's).
void FighterHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const PlayerJetReadout& j,
                ULONGLONG launchAt,Line* lines,int* at) noexcept {
    const PlayerJetSymbols& y=j.sym;
    if(Cfg().playerJetFlightHud) {
        if(j.air){Ladder(drawer,ctx,text,vp,width,height,s,y,lines,at);FlightPath(drawer,ctx,vp,width,height,s,y);}
        HeadingTape(drawer,ctx,text,width,height,s,y,lines,at);
        SpeedAltBoxes(drawer,ctx,text,width,height,s,j,lines,at);
        GroundCue(drawer,ctx,text,vp,width,height,s,y,j.gpws,j.impactIn,lines,at);
        StallCue(drawer,ctx,text,vp,width,height,s,y,j.liftShare,j.stall,lines,at);
    }
    if(Cfg().playerJetGunSight)GunSight(drawer,ctx,vp,width,height,s,y);
    if(Cfg().playerJetThreatHud)Threats(drawer,ctx,text,vp,width,height,s,y,launchAt,lines,at);
}

// With the flight HUD on (ini PlayerJetFlightHud; the user, 2026-10-05: "飞机底部的旧hud可以删了吧") the old cockpit
// panel goes: its speed, height, climb and g are the HUD's boxes (SpeedAltBoxes), its HP the game's own vehicle bar,
// its key list the README's, its warnings the annunciator's and the HUD's symbols (2026-10-06: "只留挂载和告警").
// What only it showed stays, as HUD text without a panel under the HUD's centre: one line of the throttle, the stores
// (the picked one in brackets) and the flares, and over it on the takeoff roll its cue (TakeoffCue).
void CockpitStrip(Text* text,float width,float height,float s,const PlayerJetReadout& j,Line* lines,int* at) noexcept {
    if(*at+2>kMaxLines)return;
    Line& warn=lines[(*at)++];
    Line& arms=lines[(*at)++];
    wchar_t cue[64],stores[128];
    bool rotate=false;
    TakeoffCue(j,cue,_countof(cue),&rotate);
    StoresText(stores,_countof(stores),j);
    Format(warn,L"%ls",cue);
    Format(arms,L"THR %d%%    %ls",static_cast<int>(std::lround(j.throttle*100.0f)),stores);
    warn.scale=kTitleScale;warn.rgba=rotate ? ((GetTickCount64()/125)%2==0 ? kGreen : kYellow) : kHud;
    arms.scale=kLineScale;arms.rgba=j.bomb ? kYellow : kHud;
    warn.w=warn.h=arms.w=arms.h=0.0f;
    if(text){MeasureAll(*text,&warn,1);MeasureAll(*text,&arms,1);}
    const float armsH=arms.h>0.0f ? arms.h : 18.0f*s,warnH=warn.h>0.0f ? warn.h : 24.0f*s;
    arms.x=(width-arms.w)*0.5f;arms.y=height*0.80f-armsH;
    warn.x=(width-warn.w)*0.5f;warn.y=arms.y-warnH-6.0f*s;
}

// --- The helicopter HUD (the user, 2026-10-05: "a helicopter HUD": the flight instruments, the weapons' aim, the hover's
// aids, the old panel gone; ini HeliFlightHud): for a stock helicopter the player flies (heli.cpp PlayerHeliHud) and a
// rotor craft of the plugin (PlayerJetReadout::rotor), from their HeliFlight and the fighter HUD's symbols. The fighter
// HUD's horizon, ladder, heading tape and boxes (the left one's under-line the speed W / S set), the flight path marker
// once it flies (kMovingSpeed), else and low over the ground the hover's: a drift circle (its level velocity on the
// nose's frame, kDriftFull at the ring) and a bar of the height over the ground up to kLowHover. The mouse's aim is the
// jets' cyan square. The weapons' aim is the stock heli's gun sight (HeliGunSight, drawn apart) or the rotor craft's the
// jets' (GunSight, LockMark, ImpactMark); the threat ring the fighter's. One line under the HUD's centre in place of the
// old panel (HeliStrip): the warning or the takeoff cue over the speed set, the height held and the stores. ---
constexpr float kDriftShown=8.0f;    // m/s: slower than this (level) the drift circle shows
constexpr float kDriftFull=5.0f;     // m/s: the drift at the circle's ring
constexpr float kDriftR=40.0f,kDriftDown=150.0f;   // px (at 1080 lines): its radius, its centre under the screen's
constexpr float kLowHover=30.0f;     // m over the ground: the height bar's top (shown under it)

// The hover's drift: a ring round a centre cross under the screen's middle, an arrow from the centre the way it drifts
// (up: along the nose, right: to its right), kDriftFull m/s at the ring; faster, the arrow stops at the ring, amber.
void DriftMark(void* drawer,void* ctx,float width,float height,float s,const HeliFlight& f,const PlayerJetSymbols& y) noexcept {
    float nose[3]={y.nose[0],0.0f,y.nose[2]};
    if(!vec::Normalize(nose))return;
    const float right[3]={-nose[2],0.0f,nose[0]};   // its level right (playerjet.cpp RightOf)
    const float fore=f.vel[0]*nose[0]+f.vel[2]*nose[2],side=f.vel[0]*right[0]+f.vel[2]*right[2];
    const float cx=width*0.5f,cy=height*0.5f+kDriftDown*s,r=kDriftR*s,t=2.0f*s;
    Arc(drawer,ctx,cx,cy,r,0.0f,kTurn,t,24,kHudDim);
    Seg(drawer,ctx,cx-6.0f*s,cy,cx+6.0f*s,cy,t,kHud);Seg(drawer,ctx,cx,cy-6.0f*s,cx,cy+6.0f*s,t,kHud);
    const float drift=std::sqrt(fore*fore+side*side);
    if(drift<0.2f)return;
    const float len=r*vec::Clamp(drift/kDriftFull,0.0f,1.0f),dx=side/drift,dy=-fore/drift;
    const float* c=drift>kDriftFull ? kAmber : kHud;
    const float tipX=cx+dx*len,tipY=cy+dy*len,head=8.0f*s;
    if(len>head)Seg(drawer,ctx,cx,cy,tipX-dx*head,tipY-dy*head,3.0f*s,c);
    Tri(drawer,ctx,tipX-dx*head,tipY-dy*head,tipX,tipY,5.0f*s,c);
}

// The height over the ground for the landing and the low hover: a bar right of the height box from 0 to kLowHover m, a
// tick every 5 m, a caret at the height, amber sinking faster than kSinkWarn under kSinkLow m.
void HeightBar(void* drawer,void* ctx,float width,float height,float s,const HeliFlight& f) noexcept {
    if(!f.ground || !(f.clear<kLowHover))return;
    const float x=width*0.5f+(kBoxOff+kBoxW*0.5f+18.0f)*s,cy=height*0.5f,half=kBarHalf*s,t=2.0f*s;
    const float bottom=cy+half,per=2.0f*half/kLowHover;
    Seg(drawer,ctx,x,cy-half,x,bottom,t,kHudDim);
    for(int m=0;m<=static_cast<int>(kLowHover);m+=5)Seg(drawer,ctx,x,bottom-per*static_cast<float>(m),x+(m%10==0 ? 10.0f : 6.0f)*s,
                                                        bottom-per*static_cast<float>(m),t,kHud);
    const float at=bottom-per*vec::Clamp(f.clear,0.0f,kLowHover);
    const bool sink=f.clear<kSinkLow && f.climb<-kSinkWarn;
    Tri(drawer,ctx,x-14.0f*s,at,x-2.0f*s,at,6.0f*s,sink ? kAmber : kHud);
}

// The helicopter HUD's instruments (see above); the weapons' aim and the mouse's aim (drawn with the HUD off too) are the
// caller's.
void HeliHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const HeliFlight& f,
             const PlayerJetSymbols& y,ULONGLONG launchAt,Line* lines,int* at) noexcept {
    if(!f.landed)Ladder(drawer,ctx,text,vp,width,height,s,y,lines,at);
    if(y.moving && !f.landed)FlightPath(drawer,ctx,vp,width,height,s,y);
    if(!f.landed && (f.speed<kDriftShown || (f.ground && f.clear<kLowHover)))DriftMark(drawer,ctx,width,height,s,f,y);
    HeadingTape(drawer,ctx,text,width,height,s,y,lines,at);
    wchar_t set[32]=L"";
    if(f.aiming)std::swprintf(set,32,f.setSpeed==0.0f ? L"SET HOVER" : L"SET %d",static_cast<int>(std::lround(f.setSpeed*3.6f)));
    Boxes(drawer,ctx,text,width,height,s,f.speed,set,kCyan,f.clear,f.ground,f.climb,lines,at);
    HeightBar(drawer,ctx,width,height,s,f);
    GroundCue(drawer,ctx,text,vp,width,height,s,y,f.gpws,f.impactIn,lines,at);
    if(Cfg().playerJetThreatHud)Threats(drawer,ctx,text,vp,width,height,s,y,launchAt,lines,at);
}

// The lines in place of the old panel (see above; its warnings are the annunciator's and the HUD's symbols): on top on
// the ground the takeoff cue (the rotor's share of the lift-off speed; LIFT OK blinking from there); under it the speed
// set and the height held (the mouse-aim flight); at the bottom `stores` (a rotor craft's; nullptr: none) on a line of
// their own (with the speed and ALT HOLD on one line, 5-6 stores ran past a line's 128 characters: the last cut off).
void HeliStrip(Text* text,float width,float height,float s,const HeliFlight& f,const wchar_t* stores,Line* lines,int* at) noexcept {
    if(*at+3>kMaxLines)return;
    Line& warn=lines[(*at)++];
    Line& info=lines[(*at)++];
    Line& arms=lines[(*at)++];
    const bool blink=(GetTickCount64()/125)%2==0,lift=f.landed && f.hover>0.0f && f.rotor>0.01f,liftOk=lift && f.rotor>=f.hover;
    if(liftOk){Format(warn,L"LIFT OK: TAKE OFF (ascend)");warn.rgba=blink ? kGreen : kYellow;}
    else if(lift){Format(warn,L"ROTOR %d%% of lift-off",static_cast<int>(std::lround(100.0f*f.rotor/f.hover)));warn.rgba=kCyan;}
    else{Format(warn,L"");warn.rgba=kHud;}
    Format(info,L"");
    if(f.aiming)Append(info,f.setSpeed==0.0f ? L"SPEED SET: HOVER" : L"SPEED SET %d km/h",static_cast<int>(std::lround(f.setSpeed*3.6f)));
    if(f.holding)Append(info,L"%lsALT HOLD",info.text[0] ? L"    " : L"");
    Format(arms,L"%ls",stores ? stores : L"");
    warn.scale=kTitleScale;info.scale=arms.scale=kLineScale;info.rgba=arms.rgba=kHud;
    warn.w=warn.h=info.w=info.h=arms.w=arms.h=0.0f;
    if(text){MeasureAll(*text,&warn,1);MeasureAll(*text,&info,1);MeasureAll(*text,&arms,1);}
    const float lineH=18.0f*s,armsH=arms.h>0.0f ? arms.h : (arms.text[0] ? lineH : 0.0f);
    const float infoH=info.h>0.0f ? info.h : lineH,warnH=warn.h>0.0f ? warn.h : 24.0f*s;
    arms.x=(width-arms.w)*0.5f;arms.y=height*0.80f-armsH;
    info.x=(width-info.w)*0.5f;info.y=arms.y-infoH-(arms.text[0] ? 4.0f*s : 0.0f);
    warn.x=(width-warn.w)*0.5f;warn.y=info.y-warnH-6.0f*s;
}

// The landing gear (gear.cpp GearHudLatest; the jets with gear only), at the screen's right over the cockpit's line: its
// three lights as a real gear panel lays them out (the nose's over the two mains'): green down and locked, amber in
// transit, out (an empty frame) up and locked; under them DOWN / TRANSIT / UP and the key. Warnings over it: GEAR! (red,
// blinking) low and slow with it not down, GEAR SPEED (amber) over the gear's limit with it not up, WEIGHT ON WHEELS
// (amber) an up command refused on the ground; with the annunciator up (`annunciated`) those are its tiles instead.
void GearPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,bool annunciated,Line* lines,int* at,ULONGLONG now) noexcept {
    GearHud g{};
    if(!GearHudLatest(&g) || now-g.tick>kFreshMs || *at+2>kMaxLines)return;
    const float box=24.0f*s,gap=10.0f*s,pad=10.0f*s,w=3.0f*box+4.0f*gap+2.0f*pad,h=2.0f*box+gap+2.0f*pad+60.0f*s;
    const float x0=width-w-40.0f*s,y0=height*0.80f-h;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,kCyan);
    const float cx=x0+w*0.5f,top=y0+pad;
    const float lx[kGearLegs]={cx-box*0.5f,cx-box*1.5f-gap,cx+box*0.5f+gap};   // nose; left / right main (x>0 is left)
    const float ly[kGearLegs]={top,top+box+gap,top+box+gap};
    bool down=true,up=true;
    for(int i=0;i<kGearLegs;++i) {
        down=down && g.at[i]<=0.0f;up=up && g.at[i]>=1.0f;
        const float t=2.0f*s;
        Rect(drawer,ctx,lx[i],ly[i],lx[i]+box,ly[i]+box,kBarEdge);
        if(g.at[i]<=0.0f)Rect(drawer,ctx,lx[i]+t,ly[i]+t,lx[i]+box-t,ly[i]+box-t,kGreen);
        else if(g.at[i]<1.0f)Rect(drawer,ctx,lx[i]+t,ly[i]+t,lx[i]+box-t,ly[i]+box-t,kAmber);
        else Rect(drawer,ctx,lx[i]+t,ly[i]+t,lx[i]+box-t,ly[i]+box-t,kBarBack);
    }
    Line& state=lines[(*at)++];
    wchar_t key[32]=L"L3";
    if(g.keys)KeyName(Cfg().playerJetGearKey,key,32);
    else if(Cfg().playerJetGearButton!=0x40)std::swprintf(key,32,L"button 0x%X",Cfg().playerJetGearButton);
    Format(state,L"GEAR %ls  [%ls]",down ? L"DOWN" : up ? L"UP" : L"TRANSIT",key);
    state.scale=kLineScale*0.85f;state.rgba=down ? kGreen : up ? kWhite : kAmber;
    state.w=state.h=0.0f;
    if(text)MeasureAll(*text,&state,1);
    state.x=cx-state.w*0.5f;state.y=top+2.0f*box+gap+8.0f*s;
    const wchar_t* const what=g.warn ? L"GEAR!" : g.overspeed ? L"GEAR SPEED" : g.blocked ? L"WEIGHT ON WHEELS" : nullptr;
    if(!what || annunciated)return;
    Line& warn=lines[(*at)++];
    Format(warn,L"%ls",what);
    warn.scale=kLineScale;warn.rgba=g.warn ? ((now/125)%2==0 ? kRed : kWhite) : kAmber;
    warn.w=warn.h=0.0f;
    if(text)MeasureAll(*text,&warn,1);
    warn.x=cx-warn.w*0.5f;warn.y=state.y+(state.h>0.0f ? state.h : 18.0f*s)+4.0f*s;
}

// The player's helicopter on the ground (HeliCue, the user 2026-10-05): its rotor spinning up to the speed whose lift
// holds it, then 'LIFT OK: TAKE OFF' blinking: from there the collective lifts it off.
void HeliPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,const HeliCue& c,Line* lines,int* at) noexcept {
    if(*at>=kMaxLines || !(c.rotor>0.01f) || !(c.hover>0.0f))return;
    Line& l=lines[(*at)++];
    const bool ok=c.rotor>=c.hover;
    const int pct=static_cast<int>(std::lround(100.0f*c.rotor/c.hover));
    if(ok)Format(l,L"ROTOR %d%%    LIFT OK: TAKE OFF (ascend)",pct);
    else Format(l,L"ROTOR %d%% of lift-off",pct);
    l.scale=kTitleScale;
    l.rgba=ok ? ((GetTickCount64()/125)%2==0 ? kGreen : kYellow) : kCyan;
    l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
    const float pad=8.0f*s,h=(l.h>0.0f ? l.h : 24.0f*s)+2.0f*pad,w=(l.w>0.0f ? l.w : 360.0f*s)+2.0f*pad;
    const float x0=(width-w)*0.5f,y0=height*0.80f-h;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,ok ? kGreen : kCyan);
    l.x=(width-l.w)*0.5f;l.y=y0+pad;
}

// The player's drill tank (DrillCue, the user 2026-10-05: the RPM on the HUD; its heat next to it): the RPM and its
// bar, amber while it spins up or down, green at the top, "DRILLING" while it touches something; the heat's bar beside
// it, yellow, amber past 70%, red past 90%; overheated, all red and "OVERHEAT" (it turns again once cooled).
const float* HeatColour(float heat,bool over) noexcept { return over || heat>=0.9f ? kRed : heat>=0.7f ? kAmber : kYellow; }
void DrillPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,const DrillCue& c,Line* lines,int* at) noexcept {
    if(*at>=kMaxLines || !(c.maxRpm>0.0f))return;
    Line& l=lines[(*at)++];
    const float share=Unit(c.rpm/c.maxRpm),heat=Unit(c.heat);
    const bool top=share>=0.99f;
    Format(l,L"DRILL %d RPM    HEAT %d%%%ls",static_cast<int>(std::lround(c.rpm)),static_cast<int>(std::lround(heat*100.0f)),
           c.overheated ? L"    OVERHEAT" : c.touching && c.rpm>0.0f ? L"    DRILLING" : L"");
    l.scale=kTitleScale;
    l.rgba=c.overheated ? kRed : top ? kGreen : share>0.0f ? kAmber : kCyan;
    l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
    const float pad=8.0f*s,gap=5.0f*s,rpmW=220.0f*s,heatW=120.0f*s,between=16.0f*s,barH=10.0f*s,barsW=rpmW+between+heatW;
    const float lineH=l.h>0.0f ? l.h : 24.0f*s,w=(l.w>barsW ? l.w : barsW)+2.0f*pad,h=pad+lineH+gap+barH+pad;
    const float x0=(width-w)*0.5f,y0=height*0.80f-h,bx=(width-barsW)*0.5f,by=y0+pad+lineH+gap;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,c.overheated ? kRed : top ? kGreen : kCyan);
    l.x=(width-l.w)*0.5f;l.y=y0+pad;
    Bar(drawer,ctx,bx,by,rpmW,barH,share,share,top ? kGreen : kAmber,s);
    Bar(drawer,ctx,bx+rpmW+between,by,heatW,barH,heat,heat,HeatColour(heat,c.overheated),s);
}

// A carrier's world bars (see the top): the hull's over its tower, each deck part's over its place.
constexpr float kCarrierFar=1500.0f,kPartFar=600.0f;
void CarrierBars(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const CarrierPanel& p,
                 Line* lines,int* at,ULONGLONG now) noexcept {
    float sx,sy,depth;
    if(*at<kMaxLines && Project(vp,p.at,width,height,&sx,&sy,&depth)) {
        const float k=DepthScale(depth,kCarrierFar),ks=k*s;
        const float share=p.hullMax>0.0f ? Unit(p.hull/p.hullMax) : 0.0f;
        Line& l=lines[(*at)++];
        Format(l,L"SUBMARINE CARRIER  %d%%",static_cast<int>(std::lround(share*100.0f)));
        l.scale=kTitleScale*k;l.rgba=kTitle;l.w=l.h=0.0f;
        if(text)MeasureAll(*text,&l,1);
        const float bw=240.0f*ks,bh=12.0f*ks;
        Bar(drawer,ctx,sx-bw*0.5f,sy-bh,bw,bh,share,TrailOf(p.key,share,now),HullColour(share),ks);
        l.x=sx-l.w*0.5f;l.y=sy-bh-4.0f*ks-l.h;
    }
    for(int k=0;k<p.parts && k<4 && *at<kMaxLines;++k) {
        const auto& part=p.part[k];
        if(!Project(vp,part.at,width,height,&sx,&sy,&depth))continue;
        const float f=DepthScale(depth,kPartFar),fs=f*s;
        const float share=part.down || part.max<=0.0f ? 0.0f : Unit(part.hp/part.max);
        Line& l=lines[(*at)++];
        if(part.down) {
            const int sec=static_cast<int>(part.repairSec);
            Format(l,L"%hs  DOWN %d:%02d",part.name,sec/60,sec%60);
            l.rgba=kDown;
        } else {
            Format(l,L"%hs",part.name);
            l.rgba=kWhite;
        }
        l.scale=kLineScale*f;l.w=l.h=0.0f;
        if(text)MeasureAll(*text,&l,1);
        const float bw=90.0f*fs,bh=6.0f*fs;
        // Each part's trail its own: the carrier's key and the part's index (a value, never read through).
        const void* const key=static_cast<const unsigned char*>(p.key)+1+k;
        Bar(drawer,ctx,sx-bw*0.5f,sy-bh,bw,bh,share,TrailOf(key,share,now),part.down ? kRed : kTeal,fs);
        l.x=sx-l.w*0.5f;l.y=sy-bh-3.0f*fs-l.h;
    }
}

// EDF6AutoTurret's turret the player is at (turretaim.cpp, common/edf/aimlink.h; the user, 2026-10-06: "auto-aim
// switchable to a lead circle, a lock box on the target I pick"). The lock as the jets' (LockAt: a yellow square closing
// in while its track settles, then the red diamond). In the lead-circle mode the lead circle (预瞄圈: a ring with a dot
// where the gun's line must pass for the round to meet the target on its arc) and the gun's bore cross (where its line
// passes now, at the same range): the cross in the ring, the round meets the target; dim when the round's life does
// not reach. Under the ring the range and the flight time. Low on the screen the mode and its bindings.
constexpr float kLeadCircle=22.0f;   // px at 1080 lines
void ButtonName(int bits,wchar_t* out,int size) noexcept {
    static const wchar_t* const kNames[]={L"A",L"B",L"X",L"Y",L"LB",L"RB",L"L3",L"R3"};
    for(int i=0;i<8;++i)if(bits==(1<<i)){wcscpy_s(out,static_cast<rsize_t>(size),kNames[i]);return;}
    std::swprintf(out,static_cast<std::size_t>(size),L"0x%X",bits);
}
void Binding(bool keys,int key,int button,wchar_t* out,int size) noexcept {
    if(keys)KeyName(key,out,size);
    else ButtonName(button,out,size);
}
void TurretAimMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,
                    const edf::aimlink::TurretReadoutV1& r,Line* lines,int* at) noexcept {
    namespace link=edf::aimlink;
    float x,y;
    if(r.lock!=link::Lock::none)LockAt(drawer,ctx,vp,width,height,s,r.lock==link::Lock::locked ? 2 : 1,r.at,r.lockProgress,&x,&y);
    const bool circle=r.ownGun && r.mode==link::Mode::leadCircle;
    if(circle && r.lead) {
        const float t=2.0f*s,* c=r.inReach ? kHud : kHudDim;
        float bx,by;
        if(sight::ToScreen(vp,r.boreAt,1.0f,width,height,&bx,&by)) {   // the bore cross, its centre open
            const float b=9.0f*s,g=3.0f*s;
            Seg(drawer,ctx,bx-b,by,bx-g,by,t,kWhite);Seg(drawer,ctx,bx+g,by,bx+b,by,t,kWhite);
            Seg(drawer,ctx,bx,by-b,bx,by-g,t,kWhite);Seg(drawer,ctx,bx,by+g,bx,by+b,t,kWhite);
        }
        if(sight::ToScreen(vp,r.leadAt,1.0f,width,height,&x,&y)) {
            Arc(drawer,ctx,x,y,kLeadCircle*s,0.0f,kTurn,t,20,c);
            Rect(drawer,ctx,x-1.5f*s,y-1.5f*s,x+1.5f*s,y+1.5f*s,c);
            Label(text,lines,at,x,y+(kLeadCircle+12.0f)*s,1,kLineScale*0.85f,c,L"%d m  %.1f s%ls",static_cast<int>(std::lround(r.range)),
                  r.flight,r.inReach ? L"" : L"  OUT OF RANGE");
        }
    }
    wchar_t mode[32],lock[32];
    Binding(r.keys,r.modeKey,r.modeButton,mode,32);
    Binding(r.keys,r.lockKey,r.lockButton,lock,32);
    const bool hasMode=r.keys ? r.modeKey>0 : r.modeButton>0,hasLock=r.keys ? r.lockKey>0 : r.lockButton>0;
    const wchar_t* const state=!r.ownGun ? L"GUNNERS" : circle ? L"LEAD CIRCLE" : L"AUTO-AIM";
    wchar_t keys[96]=L"";
    if(r.ownGun && hasMode)std::swprintf(keys,96,L"   [%ls] %ls",mode,circle ? L"auto-aim" : L"lead circle");
    if(hasLock) {
        const std::size_t n=wcslen(keys);
        std::swprintf(keys+n,96-n,L"   [%ls] %ls (hold: clear)",lock,r.lock==link::Lock::none ? L"lock" : L"next");
    }
    Label(text,lines,at,width*0.5f,height*0.90f,1,kLineScale*0.85f,r.lock==link::Lock::locked ? kRed : kWhite,L"%ls%ls",state,keys);
}

// --- The stock vehicles' HUD (vhud.cpp gathers it; ini StockVehicleHud; docs/hud-re.md §7): for the stock vehicle the
// player drives or mans, in the helis' green and drawn with the same quads and text (exclusive full screen too):
//  - each weapon's impact point (StockMarks): a gun's or a cannon's the helis' boresight and pipper (dim: no ground
//    within its reach, where its round ends), a grenade's or a mortar's (and any round flying longer than kLobSec) the
//    artillery's yellow cross as the Katyusha's (LauncherMarks) with its range and flight time, the rockets' the helis'
//    diamond, a missile's lock the helis' (LockAt; no lock: a dim ring round its boresight and its LockonRange); its
//    label and range beside it; the selected store's label in brackets. Weapons that land together (a pair of guns)
//    are drawn once;
//  - the heading tape at the top (the gun's heading, a caret under it for the hull's: StockTape);
//  - left of the bottom centre (StockBlock): the hull / turret indicator (the hull's outline and the gun's line, up the
//    camera's look: which way the hull points against where the player looks and aims), the vehicle's kind and seat,
//    its speed and HP with the HP bar, a line per weapon (rounds of the magazine; RELOAD and its share and seconds;
//    EMPTY when it never reloads), and over it the warning (a missile, a lock, the hull critical, out of ammo);
//  - the threat ring of the aircraft (ThreatRing) round the screen's middle. ---
constexpr float kLobSec=2.5f;        // s: a round in the air longer than this is lobbed (the cross, with its flight time)
constexpr float kSamePoint=2.0f;     // m: two weapons' points this near and of one label are drawn once
constexpr float kIndicatorR=34.0f;   // px (1080 lines): the hull / turret indicator's ring
const char kStockHpKey=0;            // the HP bar's damage trail's key (an address of our own: never a vehicle's)

// `a` - `b` in degrees, in (-180, 180].
float HdgDiff(float a,float b) noexcept {
    float d=std::fmod(a-b,360.0f);
    if(d>180.0f)d-=360.0f;
    if(d<=-180.0f)d+=360.0f;
    return d;
}

// The heading tape for `hdg` (the gun's) with a caret under it at `hull`'s heading when within the tape (HeadingTape's
// look: a tick every 5 degrees, the tens numbered).
void StockTape(void* drawer,void* ctx,Text* text,float width,float height,float s,float hdg,float hull,Line* lines,int* at) noexcept {
    const float cx=width*0.5f,base=height*0.11f,k=kTapePx*s,t=2.0f*s;
    Seg(drawer,ctx,cx-kTapeHalf*k,base,cx+kTapeHalf*k,base,t,kHudDim);
    for(int d=static_cast<int>(std::ceil((hdg-kTapeHalf)/5.0f))*5;static_cast<float>(d)<=hdg+kTapeHalf;d+=5) {
        const float x=cx+(static_cast<float>(d)-hdg)*k;
        const bool ten=d%10==0;
        Seg(drawer,ctx,x,base,x,base-(ten ? 10.0f : 5.0f)*s,t,kHud);
        if(ten)Label(text,lines,at,x,base-22.0f*s,1,kLineScale*0.8f,kHud,L"%03d",((d%360)+360)%360);
    }
    Tri(drawer,ctx,cx,base+12.0f*s,cx,base+2.0f*s,6.0f*s,kHud);
    Label(text,lines,at,cx,base+26.0f*s,1,kLineScale,kHud,L"%03d",static_cast<int>(std::lround(hdg))%360);
    if(hull<0.0f)return;
    const float off=HdgDiff(hull,hdg);
    if(std::fabs(off)>kTapeHalf)return;
    const float x=cx+off*k;
    Tri(drawer,ctx,x,base+14.0f*s,x,base+4.0f*s,5.0f*s,kAmber);
    Label(text,lines,at,x,base+40.0f*s,1,kLineScale*0.7f,kAmber,L"HULL");
}

// One weapon's mark (see above); `name` its label (bracketed when selected).
void StockMark(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockArm& a,const wchar_t* name,
               Line* lines,int* at) noexcept {
    float x,y;
    const float note=kLineScale*0.85f;
    const int metres=static_cast<int>(std::lround(a.range));
    if(a.kind==RoundKind::homing) {
        if(LockAt(drawer,ctx,vp,width,height,s,a.lock,a.at,a.lockProgress,&x,&y))
            Label(text,lines,at,x,y+44.0f*s,1,note,a.lock==2 ? kRed : kYellow,L"%ls %d m",name,metres);
        else if(!a.lock && sight::ToScreen(vp,a.bore,0.0f,width,height,&x,&y)) {
            Arc(drawer,ctx,x,y,kMissileRing*s,0.0f,kTurn,2.0f*s,24,kHudDim);
            Label(text,lines,at,x,y+(kMissileRing+12.0f)*s,1,note,kHudDim,L"%ls %d m",name,metres);
        }
        return;
    }
    if(a.kind==RoundKind::rocket) {
        if(sight::ToScreen(vp,a.at,1.0f,width,height,&x,&y) || sight::ToScreen(vp,a.bore,0.0f,width,height,&x,&y)) {
            const float* c=a.hit ? kHud : kHudDim;
            RocketDiamond(drawer,ctx,x,y,s,c);
            if(a.hit)Label(text,lines,at,x,y+(kRocketMark+12.0f)*s,1,note,c,L"%ls %d m",name,metres);
            else Label(text,lines,at,x,y+(kRocketMark+12.0f)*s,1,note,c,L"%ls",name);
        }
        return;
    }
    if((a.lobbed || a.flight>kLobSec) && a.hit) {
        if(ImpactCross(drawer,ctx,vp,width,height,s,a.at,&x,&y))
            Label(text,lines,at,x,y+26.0f*s,1,note,kYellow,L"%ls %d m   %.1f s",name,metres,a.flight);
        return;
    }
    const float* c=a.hit ? kHud : kHudDim;
    Boresight(drawer,ctx,vp,width,height,s,a.bore);
    if(Pipper(drawer,ctx,vp,width,height,s,a.at,c,&x,&y))
        Label(text,lines,at,x+(kPipper+8.0f)*s,y,0,note,c,L"%ls %d m",name,metres);
}

// The marks of every aimed weapon but the Katyusha's (launcher.cpp's), those landing on another's drawn once.
void StockMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockHudReadout& r,Line* lines,
                int* at) noexcept {
    for(int i=0;i<r.arms && i<kStockArms;++i) {
        const StockArm& a=r.arm[i];
        if(!a.aimed || a.lofted)continue;
        bool twin=false;
        for(int k=0;k<i && !twin && i!=r.selected;++k) {
            const StockArm& b=r.arm[k];
            twin=b.aimed && !b.lofted && b.kind==a.kind && std::strcmp(a.label,b.label)==0 && vec::Dist(a.at,b.at)<kSamePoint;
        }
        if(twin)continue;
        wchar_t name[24];
        std::swprintf(name,24,i==r.selected ? L"[%hs]" : L"%hs",a.label);
        StockMark(drawer,ctx,text,vp,width,height,s,a,name,lines,at);
    }
}

// The hull / turret indicator at (cx, cy): up is the way the camera looks (`up` its heading); the hull an outline turned
// by its heading off it, the gun a line from the middle by its heading off it (headings < 0: none).
void HullTurret(void* drawer,void* ctx,float cx,float cy,float s,float up,float hull,float gun) noexcept {
    const float r=kIndicatorR*s,t=2.0f*s;
    Arc(drawer,ctx,cx,cy,r,0.0f,kTurn,t,24,kHudDim);
    Tri(drawer,ctx,cx,cy-r+6.0f*s,cx,cy-r-2.0f*s,4.0f*s,kHudDim);   // the look's mark at the top
    if(hull>=0.0f) {
        const float a=HdgDiff(hull,up)*kDeg,fx=std::sin(a),fy=-std::cos(a),rx=-fy,ry=fx;   // its nose and right on the screen
        const float l=0.62f*r,w=0.38f*r;
        const float corner[4][2]={{l,w},{l,-w},{-l,-w},{-l,w}};
        float px[4],py[4];
        for(int k=0;k<4;++k){px[k]=cx+fx*corner[k][0]+rx*corner[k][1];py[k]=cy+fy*corner[k][0]+ry*corner[k][1];}
        for(int k=0;k<4;++k)Seg(drawer,ctx,px[k],py[k],px[(k+1)%4],py[(k+1)%4],t,kHud);
        Tri(drawer,ctx,cx+fx*l*0.55f,cy+fy*l*0.55f,cx+fx*l*0.95f,cy+fy*l*0.95f,w*0.5f,kHud);   // its front
    }
    if(gun>=0.0f) {
        const float a=HdgDiff(gun,up)*kDeg,fx=std::sin(a),fy=-std::cos(a);
        Arc(drawer,ctx,cx,cy,0.2f*r,0.0f,kTurn,t,10,kCyan);
        Seg(drawer,ctx,cx+fx*0.2f*r,cy+fy*0.2f*r,cx+fx*1.05f*r,cy+fy*1.05f*r,3.0f*s,kCyan);
    }
}

// A weapon's line: its label (bracketed when selected), the rounds of the magazine, its reload; its colour by its state.
void ArmLine(Line& l,const StockArm& a,bool selected) noexcept {
    Format(l,selected ? L"[%hs]" : L"%hs",a.label);
    if(a.ammoMax>1)Append(l,L" %d/%d",a.ammo>0 ? a.ammo : 0,a.ammoMax);
    else if(a.ammo>1)Append(l,L" %d",a.ammo);   // no magazine read (WeaponStatusOk false): the rounds alone
    else if(a.ammo>0)Append(l,L" READY");
    if(a.ammo<=0 && a.reload<1.0f) {
        Append(l,L" RELOAD %d%%",static_cast<int>(std::lround(a.reload*100.0f)));
        if(a.reloadSec>=0.0f)Append(l,L" %.1fs",a.reloadSec);
        l.rgba=kAmber;
    } else if(a.ammo<=0) {
        Append(l,a.canReload ? L" RELOAD" : L" EMPTY");
        l.rgba=a.canReload ? kAmber : kRed;
    } else l.rgba=selected ? kCyan : kHud;
    l.scale=kLineScale*0.85f;
}

// The block left of the bottom centre (see above).
void StockBlock(void* drawer,void* ctx,Text* text,float width,float height,float s,const StockHudReadout& r,Line* lines,int* at) noexcept {
    const int arms=r.arms<kStockArms ? r.arms : kStockArms;
    if(*at+3+arms>kMaxLines)return;
    Line& warn=lines[(*at)++];
    Line& title=lines[(*at)++];
    Line& info=lines[(*at)++];
    Line* const arm=&lines[*at];
    *at+=arms;
    bool missile=false,locked=false,dry=arms>0;
    for(int i=0;i<r.threats && i<kStockThreats;++i){missile=missile || r.threatKind[i]==2;locked=locked || r.threatKind[i]==1;}
    for(int i=0;i<arms;++i)dry=dry && r.arm[i].ammo<=0 && !r.arm[i].canReload;
    const float hp=r.hpMax>0.0f ? Unit(r.hp/r.hpMax) : 0.0f;
    const bool blink=(GetTickCount64()/125)%2==0;
    if(missile){Format(warn,L"MISSILE!");warn.rgba=blink ? kRed : kWhite;}
    else if(locked){Format(warn,L"LOCKED");warn.rgba=kYellow;}
    else if(hp<0.25f && r.hpMax>0.0f){Format(warn,L"HULL CRITICAL");warn.rgba=blink ? kRed : kWhite;}
    else if(dry){Format(warn,L"NO AMMO");warn.rgba=kAmber;}
    else{Format(warn,L"");warn.rgba=kHud;}
    if(r.seat==0)Format(title,L"%hs",r.kind);
    else Format(title,L"%hs  GUNNER %u",r.kind,r.seat);
    Format(info,L"SPD %d km/h    HP %d%%",static_cast<int>(std::lround(r.speed*3.6f)),static_cast<int>(std::lround(hp*100.0f)));
    warn.scale=kTitleScale;title.scale=info.scale=kLineScale;title.rgba=info.rgba=kHud;
    for(int i=0;i<arms;++i)ArmLine(arm[i],r.arm[i],i==r.selected);
    Line* const head[]={&warn,&title,&info};
    for(Line* l:head){l->w=l->h=0.0f;if(text)MeasureAll(*text,l,1);}
    for(int i=0;i<arms;++i){arm[i].w=arm[i].h=0.0f;if(text)MeasureAll(*text,&arm[i],1);}
    const float lineH=18.0f*s,gap=3.0f*s,barW=150.0f*s,barH=6.0f*s;
    float h=(title.h>0.0f ? title.h : lineH)+gap+(info.h>0.0f ? info.h : lineH)+gap+barH+gap*2.0f;
    for(int i=0;i<arms;++i)h+=(arm[i].h>0.0f ? arm[i].h : lineH)+gap;
    const float cx=width*0.5f-460.0f*s,x=cx+kIndicatorR*s+18.0f*s;
    float y=height*0.80f-h;
    const float hull=sight::HeadingOf(r.hull),gun=r.aimOk ? sight::HeadingOf(r.aim) : -1.0f,look=r.lookOk ? sight::HeadingOf(r.look) : -1.0f;
    const float up=look>=0.0f ? look : gun>=0.0f ? gun : hull;
    HullTurret(drawer,ctx,cx,y+kIndicatorR*s+4.0f*s,s,up<0.0f ? 0.0f : up,hull,gun);
    warn.x=x;warn.y=y-(warn.h>0.0f ? warn.h : 24.0f*s)-4.0f*s;
    title.x=x;title.y=y;y+=(title.h>0.0f ? title.h : lineH)+gap;
    info.x=x;info.y=y;y+=(info.h>0.0f ? info.h : lineH)+gap;
    Bar(drawer,ctx,x,y,barW,barH,hp,TrailOf(&kStockHpKey,hp,GetTickCount64()),HpColour(hp),s);
    y+=barH+gap*2.0f;
    for(int i=0;i<arms;++i){arm[i].x=x;arm[i].y=y;y+=(arm[i].h>0.0f ? arm[i].h : lineH)+gap;}
}

// The stock vehicle HUD, part by part (see above). A stock heli's are elsewhere (HeliHud, HeliGunSight, StockStores).
void StockVehicleHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockHudReadout& r,Line* lines,
                     int* at) noexcept {
    StockMarks(drawer,ctx,text,vp,width,height,s,r,lines,at);
    const float hull=sight::HeadingOf(r.hull),gun=r.aimOk ? sight::HeadingOf(r.aim) : -1.0f;
    if(gun>=0.0f)StockTape(drawer,ctx,text,width,height,s,gun,hull,lines,at);
    else if(hull>=0.0f)StockTape(drawer,ctx,text,width,height,s,hull,-1.0f,lines,at);
    StockBlock(drawer,ctx,text,width,height,s,r,lines,at);
    if(Cfg().playerJetThreatHud && r.threats>0) {
        PlayerJetSymbols y{};
        std::memcpy(y.pos,r.pos,12);
        y.threats=r.threats<kMostThreats ? r.threats : kMostThreats;
        for(int i=0;i<y.threats && i<kStockThreats;++i){std::memcpy(y.threatAt[i],r.threatAt[i],12);y.threatKind[i]=r.threatKind[i];}
        std::memcpy(y.nose,r.lookOk ? r.look : r.hull,12);   // the scope's up: where the player looks (else the hull)
        Threats(drawer,ctx,text,vp,width,height,s,y,0,lines,at);   // the warnings' RWR scope and the marks (no launch cue: ours)
    }
}

// A stock heli's stores for the helicopter HUD's bottom line (HeliStrip): each weapon, its rounds, its reload.
void StockStores(const StockHudReadout& r,wchar_t* out,std::size_t size) noexcept {
    out[0]=L'\0';
    std::size_t n=0;
    for(int i=0;i<r.arms && i<kStockArms;++i) {
        Line l{};
        ArmLine(l,r.arm[i],i==r.selected);
        const int k=_snwprintf_s(out+n,size-n,_TRUNCATE,L"%ls%ls",n ? L"   " : L"",l.text);
        if(k<0)break;
        n+=static_cast<std::size_t>(k);
    }
}

// What is drawn, logged when it changes (Debug, once in 10 s at most).
void DrawLog(int shown,int panels,const Line* lines,int count,int width,int height) noexcept {
    static int lastShown=-1,lastPanels=-1;
    static ULONGLONG at=0;
    const ULONGLONG now=GetTickCount64();
    if(!Cfg().debug || (shown==lastShown && panels==lastPanels) || now-at<10000)return;
    at=now;lastShown=shown;lastPanels=panels;
    Log("HUD draw %dx%d: %d readout(s), %d carrier panel(s), text=%d",width,height,shown,panels,textOk);
    for(int i=0;i<count && i<12;++i)Log("HUD   \"%ls\" at (%.0f,%.0f) %.0fx%.0f",lines[i].text,lines[i].x,lines[i].y,lines[i].w,lines[i].h);
}
}  // namespace

bool InstallHud() noexcept {
    __try {
        bool quad=true,text=true;
        for(const auto& q:kQuadSigs)quad=quad && Matches(q.rva,q.bytes,q.size);
        for(const auto& t:kTextSigs)text=text && Matches(t.rva,t.bytes,t.size);
        for(const auto& c:kTextCalls)text=text && CallsTo(c[0],c[1]);
        quadOk=quad;textOk=quad && text;
        Log("HOOK hud quad=%d text=%d (drawn from the follower gauge's call: HOOK sub gauge=1 needed)",quadOk,textOk);
        return quadOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool HudReady() noexcept { return quadOk; }

void HudSee(unsigned char* v) noexcept {
    if(!Cfg().vehicleHud || !quadOk || v[kDead] || IsSub(v))return;
    if(SeatCount(v)==0 || SeatRider(SeatAt(v,0))!=Rider::dummy)return;   // NPC-driven only
    for(unsigned i=1;i<SeatCount(v);++i)if(SeatRider(SeatAt(v,i))==Rider::player)return;   // not the one the player is in (a gunner)
    const std::int32_t team=At<std::int32_t>(v,kTeam);
    if(team!=player.team && team!=kTeamFriend && team!=kTeamVehicle)return;
    const ULONGLONG ms=GameMs();   // player.at's clock (a wall tick here never matched it: no readout was ever shown)
    if(!player.at || ms-player.at>2000)return;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float dx=pos[0]-player.pos[0],dy=pos[1]-player.pos[1],dz=pos[2]-player.pos[2];
    if(Cfg().vehicleHudRange>0.0f && dx*dx+dy*dy+dz*dz>Cfg().vehicleHudRange*Cfg().vehicleHudRange)return;
    Work* const w=WorkFor(v,ms);
    if(!w)return;
    Data d{};
    d.key=v;
    std::memcpy(d.pos,pos,12);
    d.hp=At<float>(v,kHp);d.hpMax=At<float>(v,kHpMax);
    d.fuel=-1.0f;d.drones=-1;
    ReadAmmo(v,d);
    JetHudInfo jet{};
    if(IsJet(v)) {
        d.top=4.0f;
        if(JetHud(v,&jet)){Kind(d,jet.role);d.fuel=jet.fuelSec;d.drones=jet.drones;d.leaving=jet.leaving;}
        else Kind(d,"jet");
    } else if(IsHelicopter(v)) {
        d.top=6.0f;Kind(d,"heli");
        float fuel=0.0f;
        if(HeliFuel(v,&fuel)){d.fuel=fuel;d.leaving=fuel<=0.0f;}
    } else if(IsGroundRobo(v)) {
        d.top=6.0f;Kind(d,"crawler");
    } else if(IsDrillTank(v)) {
        d.top=5.0f;Kind(d,"drill");
    } else {
        d.top=4.0f;Kind(d,"npc");
    }
    w->d=d;w->seen=ms;
    if(Cfg().debug && !w->logged) {
        w->logged=true;
        Log("HUD v=%p %s: hp %.0f/%.0f guns %d missiles %d drones %d fuel %.0fs%s",v,d.kind,d.hp,d.hpMax,d.guns,d.missiles,d.drones,
            d.fuel,d.leaving ? " (leaving)" : "");
    }
}

void HudPublish() noexcept {
    if(!quadOk)return;
    const ULONGLONG ms=GameMs();
    Snapshot& s=snaps[back];
    s.tick=GetTickCount64();
    std::memcpy(s.me,player.pos,sizeof(s.me));
    s.count=0;
    s.cockpit=PlayerJetHud(&s.jet);
    s.heli=PlayerHeliCue(&s.heliCue);
    s.heliFly=PlayerHeliHud(&s.heliHud);   // the HUD (HeliFlightHud), or only the mouse-aim flight's square
    s.drill=PlayerDrillCue(&s.drillCue);
    s.launcher=PlayerLauncher(&s.launch);
    s.heliSight=PlayerHeliSight(&s.heliAim);
    s.gunner=PlayerGunnerHud(&s.gun);
    s.highCam=PlayerHighCam(&s.highCamOn,&s.highCamKeys);
    s.turret=PlayerTurretAim(&s.turretAim);
    s.stock=PlayerStockHud(&s.stockHud);   // the stock vehicles' HUD (StockVehicleHud; a heli's stores)
    s.warned=WarnLatest(&s.warn);   // the aircraft's warnings (warn.cpp WarnTick, this frame's: it runs first)
    s.seats=PlayerSeatPrompt(&s.seatPrompt);
    if(Cfg().vehicleHud)
        for(const auto& w:work)if(w.ref && ms-w.seen<=kFreshMs)s.d[s.count++]=w.d;
    back=middle.exchange(back|kFresh,std::memory_order_acq_rel)&3u;
}

// The last frame's view-projection, for the game thread (the player jet keeps its mouse aim on the screen with it).
namespace {
SRWLOCK viewLock=SRWLOCK_INIT;
float lastViewProj[16];
bool hasViewProj=false;

void KeepViewProj(const float* viewProj) noexcept {
    AcquireSRWLockExclusive(&viewLock);
    std::memcpy(lastViewProj,viewProj,sizeof(lastViewProj));hasViewProj=true;
    ReleaseSRWLockExclusive(&viewLock);
}
}  // namespace

bool LastViewProj(float* out) noexcept {
    AcquireSRWLockShared(&viewLock);
    const bool ok=hasViewProj;
    if(ok)std::memcpy(out,lastViewProj,sizeof(lastViewProj));
    ReleaseSRWLockShared(&viewLock);
    return ok;
}

namespace {
// `m` inverted (4x4, Gauss-Jordan with partial pivoting); false when it is singular.
bool Invert4(const float* m,float* out) noexcept {
    float a[4][8];
    for(int r=0;r<4;++r)for(int c=0;c<8;++c)a[r][c]=c<4 ? m[r*4+c] : (c-4==r ? 1.0f : 0.0f);
    for(int c=0;c<4;++c) {
        int p=c;
        for(int r=c+1;r<4;++r)if(std::fabs(a[r][c])>std::fabs(a[p][c]))p=r;
        if(std::fabs(a[p][c])<1e-12f)return false;
        if(p!=c)for(int k=0;k<8;++k){const float t=a[c][k];a[c][k]=a[p][k];a[p][k]=t;}
        const float d=a[c][c];
        for(int k=0;k<8;++k)a[c][k]/=d;
        for(int r=0;r<4;++r) {
            if(r==c)continue;
            const float f=a[r][c];
            for(int k=0;k<8;++k)a[r][k]-=f*a[c][k];
        }
    }
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)out[r*4+c]=a[r][c+4];
    return true;
}

// `h` (row vector) times `m`.
void RowTimes(const float* h,const float* m,float* out) noexcept {
    for(int k=0;k<4;++k)out[k]=h[0]*m[k]+h[1]*m[4+k]+h[2]*m[8+k]+h[3]*m[12+k];
}
}  // namespace

// The camera's eye and its look through the screen's centre, from the last frame's view-projection (row vectors,
// hud.cpp): the eye is where clip w is 0 with x and y (0, 0, 1, 0) x VP^-1), a point ahead the centre at mid depth.
bool CameraRay(float* eye,float* dir) noexcept {
    float vp[16],inv[16];
    if(!LastViewProj(vp) || !Invert4(vp,inv))return false;
    const float atEye[4]={0.0f,0.0f,1.0f,0.0f},ahead[4]={0.0f,0.0f,0.5f,1.0f};
    float e[4],a[4];
    RowTimes(atEye,inv,e);RowTimes(ahead,inv,a);
    if(std::fabs(e[3])<1e-9f || std::fabs(a[3])<1e-9f)return false;
    for(int i=0;i<3;++i){eye[i]=e[i]/e[3];dir[i]=a[i]/a[3]-eye[i];}
    if(!vec::Normalize(dir))return false;
    const float probe[4]={eye[0]+dir[0]*100.0f,eye[1]+dir[1]*100.0f,eye[2]+dir[2]*100.0f,1.0f};
    float c[4];RowTimes(probe,vp,c);
    return c[3]>0.0f && std::isfinite(eye[0]+eye[1]+eye[2]);
}

void HudDraw(const float* viewProj,void* ctx,const void* viewport,const CarrierPanel* panels,int count) noexcept {
    if(viewProj)KeepViewProj(viewProj);
    if(!quadOk || !viewProj || !ctx || !viewport)return;   // the carriers' bars are drawn whatever VehicleHud says
    __try {
        void* const drawer=At<void*>(image,kQuadDrawer);
        const int w=At<std::int32_t>(viewport,8),h=At<std::int32_t>(viewport,0xC);
        if(!drawer || w<=0 || h<=0)return;
        const float width=static_cast<float>(w),height=static_cast<float>(h),s=height/1080.0f;
        alignas(16) unsigned char renderer[kRendererSize]{},font[kFontSize]{};
        Text text{};
        text.ctx=ctx;text.renderer=renderer;text.font=font;text.mgr=At<unsigned char*>(image,kFontMgr);
        Text* const t=textOk && text.mgr ? &text : nullptr;
        Line lines[kMaxLines];
        int at=0,shown=0;
        const ULONGLONG now=GetTickCount64();
        for(int i=0;i<count && i<3;++i)CarrierBars(drawer,ctx,t,viewProj,width,height,s,panels[i],lines,&at,now);
        const Snapshot& snap=Latest();
        const bool rotorHud=snap.cockpit && snap.jet.rotor && Cfg().heliFlightHud;   // a rotor craft: the helicopter HUD
        const ULONGLONG launchAt=snap.warned ? snap.warn.launchAt : 0;
        if(now-snap.tick<=kFreshMs && snap.cockpit) {
            if(snap.jet.aiming)AimMarks(drawer,ctx,viewProj,width,height,s,snap.jet);
            if(snap.jet.bomb)ImpactMark(drawer,ctx,viewProj,width,height,s,snap.jet);
            else LockMark(drawer,ctx,viewProj,width,height,s,snap.jet);
            if(snap.jet.rotor && snap.jet.heli.aiming)FlightAim(drawer,ctx,t,viewProj,width,height,s,snap.jet.heli.aim,lines,&at);   // HUD or not
            if(rotorHud) {
                HeliHud(drawer,ctx,t,viewProj,width,height,s,snap.jet.heli,snap.jet.sym,launchAt,lines,&at);
                if(Cfg().playerJetGunSight)GunSight(drawer,ctx,viewProj,width,height,s,snap.jet.sym);
                wchar_t stores[128];
                StoresText(stores,_countof(stores),snap.jet);
                HeliStrip(t,width,height,s,snap.jet.heli,stores,lines,&at);
            } else {
                FighterHud(drawer,ctx,t,viewProj,width,height,s,snap.jet,launchAt,lines,&at);
                if(Cfg().playerJetFlightHud)CockpitStrip(t,width,height,s,snap.jet,lines,&at);
                else Cockpit(drawer,ctx,t,width,height,s,snap.jet,lines,&at);
            }
            const bool annunciated=snap.warned && (rotorHud || Cfg().playerJetFlightHud);   // else the old panel's text
            if(annunciated)Annunciator(drawer,ctx,t,width,height,s,snap.warn,lines,&at);
            GearPanel(drawer,ctx,t,width,height,s,annunciated,lines,&at,now);
        }
        // A stock heli: the mouse-aim flight's square whenever the mouse flies it (HeliMouseAim), the helicopter HUD round it
        // with HeliFlightHud (off: the takeoff panel, as before).
        const bool heliFresh=now-snap.tick<=kFreshMs && snap.heliFly && !snap.cockpit,heliHud=heliFresh && Cfg().heliFlightHud;
        if(heliFresh && snap.heliHud.f.aiming)FlightAim(drawer,ctx,t,viewProj,width,height,s,snap.heliHud.f.aim,lines,&at);
        if(heliHud) {
            HeliHud(drawer,ctx,t,viewProj,width,height,s,snap.heliHud.f,snap.heliHud.sym,launchAt,lines,&at);
            wchar_t stores[192];
            const bool own=snap.stock && snap.stockHud.heli;   // its weapons, rounds and reloads (vhud.cpp)
            if(own)StockStores(snap.stockHud,stores,_countof(stores));
            HeliStrip(t,width,height,s,snap.heliHud.f,own ? stores : nullptr,lines,&at);
            if(snap.warned)Annunciator(drawer,ctx,t,width,height,s,snap.warn,lines,&at);
        }
        if(now-snap.tick<=kFreshMs && snap.heli && !snap.cockpit && !heliHud)HeliPanel(drawer,ctx,t,width,height,s,snap.heliCue,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.drill && !snap.cockpit && !snap.heli)DrillPanel(drawer,ctx,t,width,height,s,snap.drillCue,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.heliSight && !snap.cockpit)HeliGunSight(drawer,ctx,t,viewProj,width,height,s,snap.heliAim,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.launcher)LauncherMarks(drawer,ctx,t,viewProj,width,height,s,snap.launch,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.highCam && !snap.cockpit)HighCamHint(t,width,height,s,snap.highCamOn,snap.highCamKeys,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.seats)SeatLine(t,width,height,snap.seatPrompt,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.gunner && !snap.cockpit)GunnerMarks(drawer,ctx,t,viewProj,width,height,s,snap.gun,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.turret && !snap.cockpit)TurretAimMarks(drawer,ctx,t,viewProj,width,height,s,snap.turretAim,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.stock && !snap.cockpit && !snap.stockHud.heli)
            StockVehicleHud(drawer,ctx,t,viewProj,width,height,s,snap.stockHud,lines,&at);
        if(Cfg().vehicleHud) {
            if(now-snap.tick<=kFreshMs)at=Readouts(drawer,ctx,t,viewProj,width,height,s,lines,at,snap,&shown,now);
            float top=height*0.28f;
            for(int i=0;i<count && i<3;++i)top=Panel(drawer,ctx,t,panels[i],i,count,width,top,s,lines,&at)+10.0f*s;
        }
        if(t && textOk)DrawAll(*t,lines,at);
        FreeText(text);
        DrawLog(shown,count,lines,at,w,h);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
// A new mission (mission.cpp MissionStart): the last mission's vehicles are gone; the next publish is empty.
void ResetHud() noexcept {
    for(auto& w:work)w=Work{};
}
}  // namespace crew
