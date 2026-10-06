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
// is the wall clock: a snapshot older than kFreshMs (loading, mission over) is not drawn; nothing is while the game is paused.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "gear.h"
#include "map_cam.h"
#include "map_marks.h"
#include "hud_cue.h"
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
                  bool drill; DrillCue drillCue; bool emc; EmcCue emcCue;
                  bool launcher; LauncherReadout launch; bool heliSight; HeliSightReadout heliAim;
                  bool gunner; GunnerReadout gun; bool highCam,highCamOn,highCamKeys; bool heliFly; PlayerHeliReadout heliHud;
                  bool turret; edf::aimlink::TurretReadoutV1 turretAim;
                  bool stock; StockHudReadout stockHud;
                  bool warned; Warnings warn;
                  bool seats; SeatPrompt seatPrompt;
                  bool turretCamOk; TurretCamReadout turretCam;
                  bool nix; NixTorso nixTorso;
                  bool proteus; ProteusReadout proteusRo; };
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

// The turret camera (turretcam.cpp): while the turret has not come onto the point the view sends it to, a hollow
// square where its gun points (where its round would be at that point's range); looking round (free look), a cyan
// cross on the point it holds. The rest is a vehicle HUD's to draw (TurretCamReadout). `square` false: the stock
// vehicles' HUD is up and its gun's boresight and pipper already show where the gun is against the screen's centre
// (one gun, one mark).
void TurretMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const TurretCamReadout& r,bool square) noexcept {
    float sx,sy;
    if(square && !r.onTarget && sight::ToScreen(vp,r.gun,1.0f,width,height,&sx,&sy)) {
        const float h=9.0f*s,t=2.0f*s;
        Rect(drawer,ctx,sx-h,sy-h,sx+h,sy-h+t,kWhite);Rect(drawer,ctx,sx-h,sy+h-t,sx+h,sy+h,kWhite);
        Rect(drawer,ctx,sx-h,sy-h,sx-h+t,sy+h,kWhite);Rect(drawer,ctx,sx+h-t,sy-h,sx+h,sy+h,kWhite);
    }
    if(r.freeLook && sight::ToScreen(vp,r.aim,1.0f,width,height,&sx,&sy)) {
        const float h=7.0f*s,t=2.0f*s;
        Rect(drawer,ctx,sx-h,sy-t*0.5f,sx+h,sy+t*0.5f,kCyan);Rect(drawer,ctx,sx-t*0.5f,sy-h,sx+t*0.5f,sy+h,kCyan);
    }
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

// The stores: the guns' rounds (the fewest in one), each store's name and rounds (`names`; the picked one in brackets),
// and the flares in the air. Without `names` the stores are the loadout strip's (LoadoutStrip) and the line keeps the
// guns and the flares.
void StoresText(wchar_t* text,std::size_t size,const PlayerJetReadout& j,bool names=true) noexcept {
    text[0]=L'\0';
    std::size_t at=0;
    if(j.guns>0) {
        const int n=_snwprintf_s(text,size,_TRUNCATE,L"GUN %d  ",j.gunRounds>0 ? j.gunRounds : 0);
        if(n>0)at=static_cast<std::size_t>(n);
    }
    for(int i=0;names && i<j.stores && i<6;++i) {
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

// The fuel tank's readout (empty with none): FUEL and the share left, and the time left at its burn under 100 minutes (a
// jet's 506 body idles its rotor: a full tank lasts some 21 hours, no time worth showing). It replaces the stock gauge's
// FUEL panel (stockgauge.cpp).
void FuelText(wchar_t* out,std::size_t size,const FuelReading& f) noexcept {
    out[0]=L'\0';
    if(!f.ok)return;
    const int share=static_cast<int>(std::lround(f.share*100.0f));
    if(f.sec>=0.0f && f.sec<5999.5f) {
        const int sec=static_cast<int>(std::lround(f.sec));
        _snwprintf_s(out,size,_TRUNCATE,L"FUEL %d%% %d:%02d",share,sec/60,sec%60);
    } else _snwprintf_s(out,size,_TRUNCATE,L"FUEL %d%%",share);
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
    else if(!cue[0] && FuelLow(j.fuel))_snwprintf_s(cue,size,_TRUNCATE,L"LOW FUEL");
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
    wchar_t fuel[32];
    FuelText(fuel,_countof(fuel),j.fuel);
    Format(thr,L"THROTTLE %d%%%ls%ls    G %.1f%ls%ls%ls",static_cast<int>(std::lround(j.throttle*100.0f)),fuel[0] ? L"    " : L"",fuel,j.load,
           j.stall ? L"    STALL" : L"",cue[0] ? L"    " : L"",cue);
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

// --- The loadout strip (the user, 2026-10-06: "切换挂载应该有图片显示，而非仅文字"): every store a cell with its picture
// (StoreGlyph: a silhouette from quads, hud_cue.h; the HUD has no texture of the game's to draw), its name and rounds
// under it, the picked one on a panel in a cyan frame; on a switch the picked store large over it for kSwitchMs
// (LoadoutBanner). For the plugin's jets and rotor craft (PlayerJetReadout) and a stock heli (StockHudReadout). ---
constexpr unsigned long long kSwitchMs=1500;   // a switch's banner (the store's, EDF6AutoTurret's aim mode's)
constexpr float kCellW=112.0f,kCellH=48.0f;   // px at 1080 lines: a strip's cell
constexpr float kRows3[]={-4.0f,0.0f,4.0f};   // a glyph's three rows (the pod's rockets, the gun's rounds)
struct LoadCell { hudcue::StoreIcon icon; wchar_t text[40]; const float* rgba; bool picked; };

// A store's silhouette round (cx, cy), the nose to the right, `k` px a unit (some 36 x 14 units).
void StoreGlyph(void* drawer,void* ctx,float cx,float cy,float k,hudcue::StoreIcon icon,const float* rgba) noexcept {
    using hudcue::StoreIcon;
    auto box=[&](float u0,float v0,float u1,float v1){Rect(drawer,ctx,cx+u0*k,cy+v0*k,cx+u1*k,cy+v1*k,rgba);};
    auto tri=[&](float bu,float bv,float tu,float tv,float half){Tri(drawer,ctx,cx+bu*k,cy+bv*k,cx+tu*k,cy+tv*k,half*k,rgba);};
    auto seg=[&](float u0,float v0,float u1,float v1,float t){Seg(drawer,ctx,cx+u0*k,cy+v0*k,cx+u1*k,cy+v1*k,t*k,rgba);};
    // A missile: a body of length `len` and radius `r`, its nose cone, tail fins of `fin`, canards (front fins) or mid
    // wings (`mid`, an air-to-ground's).
    auto missile=[&](float len,float r,float fin,bool canards,bool mid){
        const float tail=-len*0.5f,cone=len*0.5f-r*2.0f;
        box(tail,-r,cone,r);tri(cone,0.0f,len*0.5f,0.0f,r);
        tri(tail+3.0f,-r,tail,-r-fin,1.6f);tri(tail+3.0f,r,tail,r+fin,1.6f);
        if(canards){tri(cone-3.0f,-r,cone-4.5f,-r-fin*0.5f,1.0f);tri(cone-3.0f,r,cone-4.5f,r+fin*0.5f,1.0f);}
        if(mid){tri(1.0f,-r,-4.0f,-r-fin*1.2f,3.5f);tri(1.0f,r,-4.0f,r+fin*1.2f,3.5f);}
    };
    switch(icon) {
    case StoreIcon::aamShort: missile(24.0f,1.4f,3.5f,true,false);break;
    case StoreIcon::aam: missile(30.0f,1.6f,4.0f,false,true);break;
    case StoreIcon::aamLong: missile(34.0f,2.3f,4.5f,false,true);break;
    case StoreIcon::agm: missile(26.0f,2.6f,3.0f,false,true);break;
    case StoreIcon::agmLight: missile(20.0f,1.6f,2.5f,true,false);break;
    case StoreIcon::bomb:   // Mk 82: a fat body, its ogive nose, the tail cone and its box fins
        box(-7.0f,-3.8f,8.0f,3.8f);tri(8.0f,0.0f,15.0f,0.0f,3.8f);tri(-7.0f,0.0f,-12.0f,0.0f,3.8f);
        seg(-15.0f,-5.0f,-15.0f,5.0f,1.6f);seg(-15.0f,-5.0f,-10.0f,-3.0f,1.4f);seg(-15.0f,5.0f,-10.0f,3.0f,1.4f);
        break;
    case StoreIcon::rocket:   // Hydra 70: the pod's outline, its tubes, the rockets' tips out of its front
        seg(-14.0f,-6.0f,12.0f,-6.0f,1.6f);seg(-14.0f,6.0f,12.0f,6.0f,1.6f);seg(-14.0f,-6.0f,-14.0f,6.0f,1.6f);
        seg(12.0f,-6.0f,12.0f,6.0f,1.6f);seg(-14.0f,-2.0f,12.0f,-2.0f,0.8f);seg(-14.0f,2.0f,12.0f,2.0f,0.8f);
        for(const float v:kRows3)tri(12.0f,v*1.0f,16.0f,v*1.0f,1.4f);
        break;
    case StoreIcon::gun:   // the gun's rounds: three bullets
        for(const float r:kRows3){const float v=r*1.125f;box(-10.0f,v-1.4f,2.0f,v+1.4f);tri(2.0f,v,7.0f,v,1.4f);box(-12.0f,v-1.6f,-10.5f,v+1.6f);}
        break;
    }
}

// The strip: `n` cells side by side, centred on the screen's middle, from `y` down; a cell kCellW wide, wider for a
// longer text (a stock weapon's reload). Its height.
float LoadoutStrip(void* drawer,void* ctx,Text* text,float width,float y,float s,const LoadCell* cells,int n,Line* lines,int* at) noexcept {
    if(n<=0)return 0.0f;
    constexpr int kMost=8;
    float w[kMost],total=0.0f;
    int line[kMost];
    if(n>kMost)n=kMost;
    for(int i=0;i<n;++i) {   // the texts first: their widths size the cells
        line[i]=*at;
        Label(text,lines,at,0.0f,y+37.0f*s,1,kLineScale*0.75f,cells[i].rgba,L"%ls",cells[i].text);
        const float tw=line[i]<*at ? lines[line[i]].w : 0.0f;
        w[i]=tw+16.0f*s>kCellW*s ? tw+16.0f*s : kCellW*s;
        total+=w[i];
    }
    const float h=kCellH*s,t=2.0f*s;
    float x=(width-total)*0.5f;
    for(int i=0;i<n;++i) {
        const LoadCell& c=cells[i];
        const float cx=x+w[i]*0.5f,x1=x+w[i];
        if(line[i]<*at)lines[line[i]].x=cx-lines[line[i]].w*0.5f;
        if(c.picked) {
            Rect(drawer,ctx,x+2.0f*s,y,x1-2.0f*s,y+h,kPanel);
            Seg(drawer,ctx,x+2.0f*s,y,x1-2.0f*s,y,t,kCyan);Seg(drawer,ctx,x+2.0f*s,y+h,x1-2.0f*s,y+h,t,kCyan);
            Seg(drawer,ctx,x+2.0f*s,y,x+2.0f*s,y+h,t,kCyan);Seg(drawer,ctx,x1-2.0f*s,y,x1-2.0f*s,y+h,t,kCyan);
        }
        StoreGlyph(drawer,ctx,cx,y+15.0f*s,1.4f*s,c.icon,c.rgba);
        x=x1;
    }
    return h;
}

// The picked store large, over the strip, for kSwitchMs after a switch: its picture twice the size, its name and rounds.
void LoadoutBanner(void* drawer,void* ctx,Text* text,float width,float bottom,float s,const LoadCell& c,Line* lines,int* at) noexcept {
    const float w=300.0f*s,h=64.0f*s,x0=(width-w)*0.5f,y0=bottom-h;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,kCyan);
    StoreGlyph(drawer,ctx,x0+58.0f*s,y0+h*0.5f,2.6f*s,c.icon,kCyan);
    Label(text,lines,at,x0+116.0f*s,y0+h*0.5f,0,kTitleScale,kCyan,L"%ls",c.text);
}

// The plugin aircraft's stores as cells: each one's picture (hud_cue.h StoreIconOf), its name and rounds; out of rounds
// dim; the picked one cyan.
int JetCells(const PlayerJetReadout& j,LoadCell* cells) noexcept {
    int n=0;
    for(int i=0;i<j.stores && i<6;++i) {
        LoadCell& c=cells[n++];
        c.icon=hudcue::StoreIconOf(j.storeName[i],j.storeRole[i]);
        c.picked=i==j.store;
        _snwprintf_s(c.text,_countof(c.text),_TRUNCATE,L"%hs %d",j.storeName[i] ? j.storeName[i] : "?",j.storeRounds[i]);
        c.rgba=j.storeRounds[i]<=0 ? kHudDim : c.picked ? kCyan : kHud;
    }
    return n;
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
// scope stays, dim: a real one is always there. `side` -1: left of the HUD (the aircraft's, beside their left box), +1:
// mirrored to the right (the stock vehicles': their hull / turret block is on the left, StockBlock).
constexpr float kRwrR=86.0f,kRwrRange=5000.0f;   // px at 1080 lines; m at the outer ring
void RwrCentre(float width,float height,float s,float side,float* cx,float* cy) noexcept {
    *cx=width*0.5f+side*((kBoxOff+kBoxW*0.5f+60.0f+kRwrR)*s);
    *cy=height*0.80f-(kRwrR+10.0f)*s;
}
void RwrScope(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetSymbols& y,ULONGLONG launchAt,
              ULONGLONG now,float side,Line* lines,int* at) noexcept {
    const float r=kRwrR*s,t=2.0f*s;
    float cx,cy;
    RwrCentre(width,height,s,side,&cx,&cy);
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
             ULONGLONG launchAt,Line* lines,int* at,float side=-1.0f) noexcept {
    RwrScope(drawer,ctx,text,width,height,s,y,launchAt,GetTickCount64(),side,lines,at);
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
                                            L"WEIGHT ON WHEELS",L"LOW FUEL"};
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
// What only it showed stays, as HUD text without a panel under the HUD's centre: one line of the throttle, the fuel (the
// stock gauge's FUEL panel gone, stockgauge.cpp), the stores (the picked one in brackets) and the flares, and over it on
// the takeoff roll its cue (TakeoffCue). The stores are the loadout strip under it (their pictures, names and rounds;
// `switched`: the picked one large over the line too, LoadoutBanner).
void CockpitStrip(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetReadout& j,bool switched,Line* lines,
                  int* at) noexcept {
    if(*at+2>kMaxLines)return;
    Line& warn=lines[(*at)++];
    Line& arms=lines[(*at)++];
    wchar_t cue[64],stores[128],fuel[32];
    bool rotate=false;
    TakeoffCue(j,cue,_countof(cue),&rotate);
    StoresText(stores,_countof(stores),j,false);
    FuelText(fuel,_countof(fuel),j.fuel);
    Format(warn,L"%ls",cue);
    Format(arms,L"THR %d%%%ls%ls    %ls",static_cast<int>(std::lround(j.throttle*100.0f)),fuel[0] ? L"  " : L"",fuel,stores);
    warn.scale=kTitleScale;warn.rgba=rotate ? ((GetTickCount64()/125)%2==0 ? kGreen : kYellow) : kHud;
    arms.scale=kLineScale;arms.rgba=j.bomb ? kYellow : kHud;
    warn.w=warn.h=arms.w=arms.h=0.0f;
    if(text){MeasureAll(*text,&warn,1);MeasureAll(*text,&arms,1);}
    const float armsH=arms.h>0.0f ? arms.h : 18.0f*s,warnH=warn.h>0.0f ? warn.h : 24.0f*s;
    arms.x=(width-arms.w)*0.5f;arms.y=height*0.80f-armsH;
    warn.x=(width-warn.w)*0.5f;warn.y=arms.y-warnH-6.0f*s;
    LoadCell cells[6];
    const int n=JetCells(j,cells);
    LoadoutStrip(drawer,ctx,text,width,height*0.80f+4.0f*s,s,cells,n,lines,at);
    if(switched && j.store>=0 && j.store<n)LoadoutBanner(drawer,ctx,text,width,warn.y-8.0f*s,s,cells[j.store],lines,at);
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
// The fuel (`fuel`, the stock gauge's FUEL panel gone: stockgauge.cpp) goes on the speed set's line. The stores' cells
// (`cells`, `n`) are the loadout strip under it (`picked` the picked one's, -1 none; `switched`: it large over the
// lines too, LoadoutBanner).
void HeliStrip(void* drawer,void* ctx,Text* text,float width,float height,float s,const HeliFlight& f,const FuelReading& fuel,
               const wchar_t* stores,const LoadCell* cells,int n,int picked,bool switched,Line* lines,int* at) noexcept {
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
    wchar_t tank[32];
    FuelText(tank,_countof(tank),fuel);
    if(tank[0])Append(info,L"%ls%ls",info.text[0] ? L"    " : L"",tank);
    Format(arms,L"%ls",stores ? stores : L"");
    warn.scale=kTitleScale;info.scale=arms.scale=kLineScale;info.rgba=arms.rgba=kHud;
    warn.w=warn.h=info.w=info.h=arms.w=arms.h=0.0f;
    if(text){MeasureAll(*text,&warn,1);MeasureAll(*text,&info,1);MeasureAll(*text,&arms,1);}
    const float lineH=18.0f*s,armsH=arms.h>0.0f ? arms.h : (arms.text[0] ? lineH : 0.0f);
    const float infoH=info.h>0.0f ? info.h : lineH,warnH=warn.h>0.0f ? warn.h : 24.0f*s;
    arms.x=(width-arms.w)*0.5f;arms.y=height*0.80f-armsH;
    info.x=(width-info.w)*0.5f;info.y=arms.y-infoH-(arms.text[0] ? 4.0f*s : 0.0f);
    warn.x=(width-warn.w)*0.5f;warn.y=info.y-warnH-6.0f*s;
    LoadoutStrip(drawer,ctx,text,width,height*0.80f+4.0f*s,s,cells,n,lines,at);
    if(switched && picked>=0 && picked<n)LoadoutBanner(drawer,ctx,text,width,warn.y-8.0f*s,s,cells[picked],lines,at);
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
                    const edf::aimlink::TurretReadoutV1& r,bool flipped,Line* lines,int* at) noexcept {
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
    // The mode as plainly on or off (the user, 2026-10-06: "怎么切换自动瞄准和关闭，看不出来"): AUTO-AIM ON (green, the
    // turret aims itself) or AUTO-AIM OFF (amber, the lead circle: the turret is the player's), the key that flips it on
    // the line under it (a pad with no AimModeButton: where to set one); for kSwitchMs after a flip (`flipped`) the new
    // state large over the screen's middle on a panel.
    wchar_t mode[32],lock[32];
    Binding(r.keys,r.modeKey,r.modeButton,mode,32);
    Binding(r.keys,r.lockKey,r.lockButton,lock,32);
    const bool hasMode=r.keys ? r.modeKey>0 : r.modeButton>0,hasLock=r.keys ? r.lockKey>0 : r.lockButton>0;
    const wchar_t* const state=!r.ownGun ? L"GUNNERS: AUTO-AIM" : circle ? L"AUTO-AIM OFF  (LEAD CIRCLE)" : L"AUTO-AIM ON";
    const float* const colour=!r.ownGun ? kWhite : circle ? kAmber : kGreen;
    wchar_t keys[128]=L"";
    if(r.ownGun && hasMode)std::swprintf(keys,128,L"[%ls] auto-aim %ls",mode,circle ? L"on" : L"off");
    else if(r.ownGun)wcscpy_s(keys,r.keys ? L"(no AimModeKey set)" : L"(pad: set AimModeButton in EDF6AutoTurret.ini)");
    if(hasLock) {
        const std::size_t n=wcslen(keys);
        std::swprintf(keys+n,128-n,L"%ls[%ls] %ls (hold: clear)",n ? L"   " : L"",lock,r.lock==link::Lock::none ? L"lock" : L"next");
    }
    Label(text,lines,at,width*0.5f,height*0.875f,1,kLineScale,colour,L"%ls",state);
    Label(text,lines,at,width*0.5f,height*0.905f,1,kLineScale*0.85f,r.lock==link::Lock::locked ? kRed : kWhite,L"%ls",keys);
    if(!flipped || !r.ownGun)return;
    const float by=height*0.30f;
    Label(text,lines,at,width*0.5f,by,1,kTitleScale,colour,L"%ls",circle ? L"AUTO-AIM OFF  -  LEAD CIRCLE" : L"AUTO-AIM ON");
    const float w=(*at>0 && lines[*at-1].w>0.0f ? lines[*at-1].w : 420.0f*s)+40.0f*s,h=48.0f*s;
    Rect(drawer,ctx,(width-w)*0.5f,by-h*0.5f,(width+w)*0.5f,by+h*0.5f,kPanel);
    Rect(drawer,ctx,(width-w)*0.5f,by-h*0.5f,(width+w)*0.5f,by-h*0.5f+3.0f*s,colour);
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
//    its speed and HP with the HP bar (STAB on that line while the gun stabilizer holds the seat's gun), a line per
//    weapon (rounds of the magazine; RELOAD and its share and seconds; EMPTY when it never reloads), and over it the warning (a missile, a lock, the hull critical, out of ammo);
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

// What the stock vehicles' HUD takes from the other readouts: the Nix's legs and torso (its ring), the drill tank's drill
// (a line in the block instead of DrillPanel), the EMC's charged beam (a line and its bar: emc.cpp), and whether
// EDF6AutoTurret's lead circle is on the seat's own gun (the
// seat's first weapon, the one its aim turns: then the circle and its bore cross are that gun's marks, not a pipper).
struct StockExtras { const NixTorso* nix; const DrillCue* drill; bool leadGun; const EmcCue* emc; const ProteusReadout* proteus; };
// --- The Proteus (proteus.cpp; README 普罗透斯): its part of the stock vehicle HUD. In the block (StockBlock) up to five
// lines with a bar under some: the stance (and the stagger's progress), the shield (deployed its heat), the barrier, the
// salvo (its cooldown, the mark's range), the field (its allies) and the driver's gun; the bindings named where the
// driver has a press to make. On the hull ring the standing shield's arc; on the ground the field's edge; on the marked
// target a red diamond with its range. ---
constexpr int kProteusLines=5;
const char kBarrierKey=0;            // the barrier bar's damage trail's key (an address of our own)
struct ProteusLine { Line* line; bool bar; float share; const float* fill; const void* trailKey; };

// A binding's name: the mouse's buttons by name (GetKeyNameText has none for them), the keys as KeyName, a pad's button.
void ProteusBinding(bool keys,int key,int button,wchar_t* out,int size) noexcept {
    static const wchar_t* const kMouse[]={L"?",L"LMB",L"RMB",L"?",L"MMB",L"MB4",L"MB5"};
    if(keys && key>0 && key<=6){wcscpy_s(out,static_cast<rsize_t>(size),kMouse[key]);return;}
    Binding(keys,key,button,out,size);
}

// The block's Proteus lines (see above) into `pl` (`n` of them): the lines are `lines`' next ones.
int ProteusLinesOf(const ProteusReadout& p,Line* lines,ProteusLine* pl) noexcept {
    wchar_t mode[16],shield[16],mark[16],salvo[16];
    ProteusBinding(p.keys,p.modeKey,p.modeButton,mode,16);
    ProteusBinding(p.keys,p.shieldKey,p.shieldButton,shield,16);
    ProteusBinding(p.keys,p.markKey,p.markButton,mark,16);
    ProteusBinding(p.keys,p.salvoKey,0,salvo,16);
    if(!p.keys)wcscpy_s(salvo,L"LT");
    const bool blink=(GetTickCount64()/125)%2==0;
    int n=0;
    const auto add=[&](bool bar,float share,const float* fill)->Line& {
        Line& l=lines[n];
        l.scale=kLineScale*0.85f;l.rgba=kHud;l.text[0]=L'\0';
        pl[n]=ProteusLine{&l,bar,share,fill,nullptr};
        ++n;
        return l;
    };
    // The stance.
    {
        const bool stagger=p.mode==proteus::Mode::deploying || p.mode==proteus::Mode::stowing;
        Line& l=add(stagger,p.stagger,kAmber);
        switch(p.mode) {
            case proteus::Mode::walk: Format(l,L"WALK");if(p.driver)Append(l,L"   [%ls] DEPLOY",mode);break;
            case proteus::Mode::deploying: Format(l,L"DEPLOYING %d%%",static_cast<int>(std::lround(p.stagger*100.0f)));l.rgba=kAmber;break;
            case proteus::Mode::deployed: Format(l,L"DEPLOYED");if(p.driver)Append(l,L"   [%ls] STOW",mode);l.rgba=kCyan;break;
            case proteus::Mode::stowing: Format(l,L"STOWING %d%%",static_cast<int>(std::lround(p.stagger*100.0f)));l.rgba=kAmber;break;
        }
    }
    // The shield.
    {
        const bool deployed=p.mode==proteus::Mode::deployed;
        Line& l=add(deployed,p.heat,p.overheated ? kRed : p.heat>=0.7f ? kAmber : kYellow);
        Format(l,deployed ? L"SHIELD %ls  HEAT %d%%" : L"FRONT SHIELD %ls",p.overheated ? L"OVERHEAT" : p.shieldUp ? L"UP" : L"OFF",
               static_cast<int>(std::lround(p.heat*100.0f)));
        if(p.driver)Append(l,L"   [%ls]",shield);
        if(p.priority)Append(l,L"   ALLIES FOCUS");
        l.rgba=p.overheated ? (blink ? kRed : kWhite) : p.shieldUp ? kCyan : kHudDim;
    }
    if(p.mode==proteus::Mode::deployed) {
        // The barrier.
        Line& b=add(true,p.barrier,HullColour(p.barrier));
        pl[n-1].trailKey=&kBarrierKey;
        Format(b,L"BARRIER %d%%",static_cast<int>(std::lround(p.barrier*100.0f)));
        b.rgba=p.barrier>0.25f ? kHud : kAmber;
        // The field and the gun.
        Line& f=add(false,0.0f,nullptr);
        Format(f,L"FIELD %d m  ALLIES %d",static_cast<int>(std::lround(p.fieldRadius)),p.allies);
        if(p.gun)Append(f,p.keys ? L"   GUN [LMB]" : L"   GUN [RT]");
        f.rgba=kTeal;
    }
    // The salvo.
    {
        Line& l=add(false,0.0f,nullptr);
        if(!p.salvoArmed){Format(l,L"SALVO OFFLINE");l.rgba=kHudDim;}
        else if(p.mode!=proteus::Mode::deployed){Format(l,L"SALVO  DEPLOY FIRST");l.rgba=kHudDim;}
        else if(p.salvoLeft>0){Format(l,L"SALVO  %d IN THE AIR",p.salvoLeft);l.rgba=kRed;}
        else if(p.salvoWait>0.0f){Format(l,L"SALVO %.1fs",p.salvoWait);l.rgba=kAmber;}
        else if(!p.marked){Format(l,L"SALVO READY  MARK A TARGET");if(p.driver)Append(l,L" [%ls]",mark);l.rgba=kYellow;}
        else{Format(l,L"SALVO READY");if(p.driver)Append(l,L" [%ls]",salvo);l.rgba=blink ? kRed : kYellow;}
        if(p.marked)Append(l,L"   MARK %d m",static_cast<int>(std::lround(p.markRange)));
    }
    return n;
}

// The field's edge on the ground and the marked target (see above).
void ProteusMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const ProteusReadout& p,Line* lines,
                  int* at) noexcept {
    for(int i=0;i<p.ringCount && i<kProteusRing;++i) {
        if(i%2)continue;   // dashed
        float x0,y0,x1,y1;
        const int k=(i+1)%p.ringCount;
        if(sight::ToScreen(vp,p.ring[i],1.0f,width,height,&x0,&y0) && sight::ToScreen(vp,p.ring[k],1.0f,width,height,&x1,&y1))
            Seg(drawer,ctx,x0,y0,x1,y1,2.0f*s,kTeal);
    }
    float x,y;
    if(p.marked && sight::ToScreen(vp,p.markAt,1.0f,width,height,&x,&y)) {
        const float r=16.0f*s,t=2.5f*s;
        Seg(drawer,ctx,x,y-r,x+r,y,t,kRed);Seg(drawer,ctx,x+r,y,x,y+r,t,kRed);
        Seg(drawer,ctx,x,y+r,x-r,y,t,kRed);Seg(drawer,ctx,x-r,y,x,y-r,t,kRed);
        Label(text,lines,at,x,y+r+12.0f*s,1,kLineScale*0.85f,kRed,L"MARK %d m",static_cast<int>(std::lround(p.markRange)));
    }
}


// The marks of every aimed weapon but the Katyusha's (launcher.cpp's), those landing on another's drawn once.
void StockMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockHudReadout& r,
                const StockExtras& x,Line* lines,int* at) noexcept {
    for(int i=0;i<r.arms && i<kStockArms;++i) {
        const StockArm& a=r.arm[i];
        if(!a.aimed || a.lofted)continue;
        if(i==0 && x.leadGun && a.kind!=RoundKind::homing)continue;   // the lead circle's gun
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
// by its heading off it, the gun a line from the middle by its heading off it (headings < 0: none); `stops` (a Nix:
// the torso's twist limits as headings, each < 0: none) two amber ticks outside the ring.
void HullTurret(void* drawer,void* ctx,float cx,float cy,float s,float up,float hull,float gun,const float* stops=nullptr) noexcept {
    const float r=kIndicatorR*s,t=2.0f*s;
    Arc(drawer,ctx,cx,cy,r,0.0f,kTurn,t,24,kHudDim);
    Tri(drawer,ctx,cx,cy-r+6.0f*s,cx,cy-r-2.0f*s,4.0f*s,kHudDim);   // the look's mark at the top
    for(int k=0;stops && k<2;++k) {
        if(stops[k]<0.0f)continue;
        const float a=HdgDiff(stops[k],up)*kDeg,fx=std::sin(a),fy=-std::cos(a);
        Seg(drawer,ctx,cx+fx*(r+2.0f*s),cy+fy*(r+2.0f*s),cx+fx*(r+9.0f*s),cy+fy*(r+9.0f*s),t,kAmber);
    }
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

// The EMC's line (EmcCue): charging, its share and an amber bar filling; the beam out, BEAM and the seconds left, its bar
// emptying, white; after it, REARM; ready, the beams its rounds still make, green (a charge let go drains on its bar); no rounds, EMPTY, red.
void EmcLine(Line& l,const EmcCue& c,float* bar,const float** colour) noexcept {
    const float beam=Cfg().emcBeamSec>0.0f ? Cfg().emcBeamSec : 1.0f;
    if(c.firing) {
        Format(l,L"EMC BEAM %.1fs",c.beamLeft);
        *bar=Unit(c.beamLeft/beam);*colour=kWhite;
    } else if(c.charging) {
        Format(l,L"EMC CHARGE %d%%",static_cast<int>(std::lround(Unit(c.charge)*100.0f)));
        *bar=Unit(c.charge);*colour=kAmber;
    } else if(c.empty) {
        Format(l,L"EMC EMPTY");
        *bar=0.0f;*colour=kRed;
    } else if(c.rearm>0.0f) {
        Format(l,L"EMC REARM %.1fs",c.rearm);
        *bar=0.0f;*colour=kHudDim;
    } else {
        Format(l,L"EMC READY x%d  HOLD FIRE",c.beams);
        *bar=Unit(c.charge);*colour=kGreen;
    }
    l.rgba=*colour;
    l.scale=kLineScale*0.85f;
}

// A heading (sight::HeadingOf's degrees) of world yaw `yaw` (nix.h's: rad, atan2(x, z)).
float HeadingOfYaw(float yaw) noexcept {
    const float d[3]={std::sin(yaw),0.0f,std::cos(yaw)};
    return sight::HeadingOf(d);
}

// The block left of the bottom centre (see above). A Nix: the ring's hull is its legs, its gun the torso (nix.cpp), the
// twist's limits ticked and its angle on the info line. The drill tank: its drill's RPM and heat a line under the
// weapons (DrillPanel's colours), OVERHEAT the warning. The EMC (emc.cpp): its charged beam a line under the weapons
// and a bar under it (EmcLine). The readouts of a kind (the Nix's, the EMC's, the Proteus's) are taken only when they are
// this vehicle's (their position on it). Out of lines (a crowded frame): the head always (it carries the warnings), then
// as many weapons as fit, then the extras that fit whole; never the whole block dropped (with the stock gauge hidden,
// stockgauge.cpp, it is the vehicle's only readout).
void StockBlock(void* drawer,void* ctx,Text* text,float width,float height,float s,const StockHudReadout& r,const StockExtras& x,
                Line* lines,int* at) noexcept {
    int room=kMaxLines-*at-3;   // the head's three lines first
    if(room<0)return;
    const int want=r.arms<kStockArms ? r.arms : kStockArms;
    const int arms=want<room ? want : room;
    room-=arms;
    const bool drillOn=x.drill && x.drill->maxRpm>0.0f && room>0;
    room-=drillOn ? 1 : 0;
    const EmcCue* const emc=x.emc && vec::Dist(x.emc->pos,r.pos)<2.0f && room>0 ? x.emc : nullptr;   // this vehicle's
    room-=emc ? 1 : 0;
    const ProteusReadout* const prot=x.proteus && vec::Dist(x.proteus->pos,r.pos)<2.0f ? x.proteus : nullptr;   // this vehicle's
    const bool protLines=prot && room>=kProteusLines;
    Line& warn=lines[(*at)++];
    Line& title=lines[(*at)++];
    Line& info=lines[(*at)++];
    Line* const arm=&lines[*at];
    *at+=arms;
    Line* const drill=drillOn ? &lines[(*at)++] : nullptr;
    Line* const emcLine=emc ? &lines[(*at)++] : nullptr;
    ProteusLine pl[kProteusLines]{};
    const int prots=protLines ? ProteusLinesOf(*prot,&lines[*at],pl) : 0;
    *at+=prots;
    const bool nix=x.nix && vec::Dist(x.nix->at,r.pos)<2.0f;   // the Nix readout is this vehicle's
    bool missile=false,locked=false,dry=arms>0;
    for(int i=0;i<r.threats && i<kStockThreats;++i){missile=missile || r.threatKind[i]==2;locked=locked || r.threatKind[i]==1;}
    for(int i=0;i<arms;++i)dry=dry && r.arm[i].ammo<=0 && !r.arm[i].canReload;
    const float hp=r.hpMax>0.0f ? Unit(r.hp/r.hpMax) : 0.0f;
    const bool blink=(GetTickCount64()/125)%2==0;
    if(missile){Format(warn,L"MISSILE!");warn.rgba=blink ? kRed : kWhite;}
    else if(locked){Format(warn,L"LOCKED");warn.rgba=kYellow;}
    else if(hp<0.25f && r.hpMax>0.0f){Format(warn,L"HULL CRITICAL");warn.rgba=blink ? kRed : kWhite;}
    else if(drill && x.drill->overheated){Format(warn,L"DRILL OVERHEAT");warn.rgba=blink ? kRed : kWhite;}
    else if(FuelLow(r.fuel)){Format(warn,L"LOW FUEL");warn.rgba=kAmber;}
    else if(dry){Format(warn,L"NO AMMO");warn.rgba=kAmber;}
    else{Format(warn,L"");warn.rgba=kHud;}
    const char* const kind=prot ? "PROTEUS" : r.kind;
    if(r.seat==0)Format(title,L"%hs",kind);
    else Format(title,L"%hs  GUNNER %u",kind,r.seat);
    Format(info,L"SPD %d km/h    HP %d%%",static_cast<int>(std::lround(r.speed*3.6f)),static_cast<int>(std::lround(hp*100.0f)));
    if(nix)Append(info,L"    TWIST %+d",static_cast<int>(std::lround(-x.nix->twist*57.2957795f)));   // right positive, as headings
    if(r.stab)Append(info,r.stab==2 ? L"    STAB LAG" : L"    STAB");   // the gun stabilizer holds it (LAG: the hull outruns its drive)
    wchar_t fuel[32];
    FuelText(fuel,_countof(fuel),r.fuel);   // a bike's tank (the stock gauge's FUEL panel gone: stockgauge.cpp)
    if(fuel[0])Append(info,L"    %ls",fuel);
    warn.scale=kTitleScale;title.scale=info.scale=kLineScale;title.rgba=info.rgba=kHud;
    for(int i=0;i<arms;++i)ArmLine(arm[i],r.arm[i],i==r.selected);
    if(drill) {
        const DrillCue& c=*x.drill;
        const float share=Unit(c.rpm/c.maxRpm);
        Format(*drill,L"DRILL %d RPM  HEAT %d%%%ls",static_cast<int>(std::lround(c.rpm)),static_cast<int>(std::lround(Unit(c.heat)*100.0f)),
               c.overheated ? L"  OVERHEAT" : c.touching && c.rpm>0.0f ? L"  DRILLING" : L"");
        drill->scale=kLineScale*0.85f;
        drill->rgba=c.overheated ? kRed : c.heat>=0.7f ? HeatColour(Unit(c.heat),false) : share>=0.99f ? kGreen : share>0.0f ? kAmber : kCyan;
    }
    Line* const head[]={&warn,&title,&info};
    for(Line* l:head){l->w=l->h=0.0f;if(text)MeasureAll(*text,l,1);}
    for(int i=0;i<arms;++i){arm[i].w=arm[i].h=0.0f;if(text)MeasureAll(*text,&arm[i],1);}
    if(drill){drill->w=drill->h=0.0f;if(text)MeasureAll(*text,drill,1);}
    float emcBar=0.0f;
    const float* emcColour=kHud;
    if(emcLine) {
        EmcLine(*emcLine,*emc,&emcBar,&emcColour);
        emcLine->w=emcLine->h=0.0f;
        if(text)MeasureAll(*text,emcLine,1);
    }
    for(int i=0;i<prots;++i){pl[i].line->w=pl[i].line->h=0.0f;if(text)MeasureAll(*text,pl[i].line,1);}
    const float lineH=18.0f*s,gap=3.0f*s,barW=150.0f*s,barH=6.0f*s;
    float h=(title.h>0.0f ? title.h : lineH)+gap+(info.h>0.0f ? info.h : lineH)+gap+barH+gap*2.0f;
    for(int i=0;i<arms;++i)h+=(arm[i].h>0.0f ? arm[i].h : lineH)+gap;
    if(drill)h+=(drill->h>0.0f ? drill->h : lineH)+gap;
    if(emcLine)h+=(emcLine->h>0.0f ? emcLine->h : lineH)+gap+barH+gap;
    for(int i=0;i<prots;++i)h+=(pl[i].line->h>0.0f ? pl[i].line->h : lineH)+gap+(pl[i].bar ? barH+gap : 0.0f);
    const float cx=width*0.5f-460.0f*s,tx=cx+kIndicatorR*s+18.0f*s;
    float y=height*0.80f-h;
    float hull=sight::HeadingOf(r.hull),gun=r.aimOk ? sight::HeadingOf(r.aim) : -1.0f;
    const float look=r.lookOk ? sight::HeadingOf(r.look) : -1.0f;
    float stops[2]={-1.0f,-1.0f};
    if(nix) {
        hull=HeadingOfYaw(x.nix->legsYaw);gun=sight::HeadingOf(x.nix->dir);
        stops[0]=HeadingOfYaw(x.nix->legsYaw+x.nix->twistMin);stops[1]=HeadingOfYaw(x.nix->legsYaw+x.nix->twistMax);
    }
    const float up=look>=0.0f ? look : gun>=0.0f ? gun : hull;
    HullTurret(drawer,ctx,cx,y+kIndicatorR*s+4.0f*s,s,up<0.0f ? 0.0f : up,hull,gun,nix ? stops : nullptr);
    if(prot && prot->shieldUp) {   // the standing shield round the hull's nose, on the ring
        const float half=prot->shieldHalfArc,a=HdgDiff(hull,up<0.0f ? 0.0f : up)*kDeg-1.5707963f;
        Arc(drawer,ctx,cx,y+kIndicatorR*s+4.0f*s,(kIndicatorR+6.0f)*s,a-half,2.0f*half,3.0f*s,12,prot->overheated ? kRed : kCyan);
    }
    const float x0=tx;
    warn.x=x0;warn.y=y-(warn.h>0.0f ? warn.h : 24.0f*s)-4.0f*s;
    title.x=x0;title.y=y;y+=(title.h>0.0f ? title.h : lineH)+gap;
    info.x=x0;info.y=y;y+=(info.h>0.0f ? info.h : lineH)+gap;
    Bar(drawer,ctx,x0,y,barW,barH,hp,TrailOf(&kStockHpKey,hp,GetTickCount64()),HpColour(hp),s);
    y+=barH+gap*2.0f;
    for(int i=0;i<arms;++i){arm[i].x=x0;arm[i].y=y;y+=(arm[i].h>0.0f ? arm[i].h : lineH)+gap;}
    if(drill){drill->x=x0;drill->y=y;y+=(drill->h>0.0f ? drill->h : lineH)+gap;}
    if(emcLine) {
        emcLine->x=x0;emcLine->y=y;y+=(emcLine->h>0.0f ? emcLine->h : lineH)+gap;
        Bar(drawer,ctx,x0,y,barW,barH,emcBar,emcBar,emcColour,s);
        y+=barH+gap;   // the Proteus's lines (a vehicle has one or the other) under it
    }
    for(int i=0;i<prots;++i) {
        pl[i].line->x=x0;pl[i].line->y=y;
        y+=(pl[i].line->h>0.0f ? pl[i].line->h : lineH)+gap;
        if(!pl[i].bar)continue;
        const float trail=pl[i].trailKey ? TrailOf(pl[i].trailKey,pl[i].share,GetTickCount64()) : pl[i].share;
        Bar(drawer,ctx,x0,y,barW,barH,pl[i].share,trail,pl[i].fill,s);
        y+=barH+gap;
    }
}

// The stock vehicle HUD, part by part (see above). A stock heli's are elsewhere (HeliHud, HeliGunSight, StockCells).
void StockVehicleHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockHudReadout& r,
                     const StockExtras& x,Line* lines,int* at) noexcept {
    StockMarks(drawer,ctx,text,vp,width,height,s,r,x,lines,at);
    if(x.proteus && vec::Dist(x.proteus->pos,r.pos)<2.0f)ProteusMarks(drawer,ctx,text,vp,width,height,s,*x.proteus,lines,at);
    const bool nix=x.nix && vec::Dist(x.nix->at,r.pos)<2.0f;
    const float hull=nix ? HeadingOfYaw(x.nix->legsYaw) : sight::HeadingOf(r.hull);
    const float gun=nix ? sight::HeadingOf(x.nix->dir) : r.aimOk ? sight::HeadingOf(r.aim) : -1.0f;
    if(gun>=0.0f)StockTape(drawer,ctx,text,width,height,s,gun,hull,lines,at);
    else if(hull>=0.0f)StockTape(drawer,ctx,text,width,height,s,hull,-1.0f,lines,at);
    StockBlock(drawer,ctx,text,width,height,s,r,x,lines,at);
    if(Cfg().playerJetThreatHud && r.threats>0) {
        PlayerJetSymbols y{};
        std::memcpy(y.pos,r.pos,12);
        y.threats=r.threats<kMostThreats ? r.threats : kMostThreats;
        for(int i=0;i<y.threats && i<kStockThreats;++i){std::memcpy(y.threatAt[i],r.threatAt[i],12);y.threatKind[i]=r.threatKind[i];}
        std::memcpy(y.nose,r.lookOk ? r.look : r.hull,12);   // the scope's up: where the player looks (else the hull)
        // The warnings' RWR scope and the marks (no launch cue: ours); the scope right of the centre, clear of the block.
        Threats(drawer,ctx,text,vp,width,height,s,y,0,lines,at,1.0f);
    }
}

// A stock heli's stores for the helicopter HUD's loadout strip (HeliStrip): each weapon's picture by its round
// (hud_cue.h ArmIconOf), its rounds and its reload (ArmLine's text and colour); the picked one cyan.
int StockCells(const StockHudReadout& r,LoadCell* cells) noexcept {
    int n=0;
    for(int i=0;i<r.arms && i<kStockArms;++i) {
        Line l{};
        ArmLine(l,r.arm[i],false);
        LoadCell& c=cells[n++];
        c.icon=hudcue::ArmIconOf(static_cast<int>(r.arm[i].kind),r.arm[i].lobbed);
        c.picked=i==r.selected;
        wcsncpy_s(c.text,_countof(c.text),l.text,_TRUNCATE);
        c.rgba=c.picked && l.rgba==kHud ? kCyan : l.rgba;
    }
    return n;
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
    s.emc=PlayerEmcCue(&s.emcCue);
    s.launcher=PlayerLauncher(&s.launch);
    s.heliSight=PlayerHeliSight(&s.heliAim);
    s.gunner=PlayerGunnerHud(&s.gun);
    s.highCam=PlayerHighCam(&s.highCamOn,&s.highCamKeys);
    s.turret=PlayerTurretAim(&s.turretAim);
    s.stock=PlayerStockHud(&s.stockHud);   // the stock vehicles' HUD (StockVehicleHud; a heli's stores)
    s.warned=WarnLatest(&s.warn);   // the aircraft's warnings (warn.cpp WarnTick, this frame's: it runs first)
    s.seats=PlayerSeatPrompt(&s.seatPrompt);
    s.turretCamOk=PlayerTurretCam(&s.turretCam);   // the turret camera (turretcam.cpp): the gun's mark, free look
    s.nix=PlayerNixTorso(&s.nixTorso);   // the Nix's legs and torso (nix.cpp): the stock HUD's hull / turret ring
    // The stock weapon gauge gives way (stockgauge.cpp, HideStockGauges) where HudDraw lists the vehicle's weapons and
    // their rounds: a plugin aircraft's stores line (CockpitStrip, the old Cockpit, HeliStrip), StockBlock, a stock heli's
    // HeliStrip (HeliFlightHud). Its text must draw: the lists are text.
    const bool heliLists=s.stock && s.stockHud.heli && s.heliFly && !s.cockpit && Cfg().heliFlightHud;
    SetStockGaugeCover(textOk && (s.cockpit || (s.stock && !s.cockpit && !s.stockHud.heli) || heliLists));
    s.proteus=PlayerProteus(&s.proteusRo);   // the Proteus's stance, shields, salvo, field (proteus.cpp)
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
bool Invert4(const float* m,float* out) noexcept { return mapcmd::Invert4(m,out); }

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

// --- The map view's marks (map.cpp; README 地图, docs/camera-re.md §8): drawn over the real world the map's camera
// shows, in place of every other HUD element while it is open: a ground grid (its lines a round step apart, through the
// player, labelled with their distance from the player and the side: north is the world's +z, east its -x, the game's
// level right of +z), the north arrow, a scale bar, the player, the squad, friendly vehicles, aircraft and carriers, the
// enemies the lock-on registry knows, the objective markers, a legend and the keys. ---
namespace {
alignas(16) const float kMapGrid[4]={0.85f,0.95f,1.0f,0.28f};
alignas(16) const float kMapAxis[4]={0.85f,0.95f,1.0f,0.6f};
alignas(16) const float kMapBand[4]={0.0f,0.0f,0.0f,0.45f};
alignas(16) const float kMapSquad[4]={0.35f,1.0f,0.45f,1.0f};
alignas(16) const float kMapAlly[4]={0.45f,0.85f,0.5f,0.8f};
alignas(16) const float kMapEnemy[4]={1.0f,0.22f,0.18f,1.0f};
alignas(16) const float kMapMarker[4]={1.0f,0.85f,0.2f,1.0f};
constexpr int kMapLabels=12;             // at most this many labels an axis (every other line past that)
constexpr float kMapLabelGap=34.0f;      // px (at 1080 lines) between two labels of an axis
constexpr float kMapFarFade=0.45f;       // a pin three times the focus's distance away (and on) drawn this opaque
constexpr float kMapLockNear=40.0f;      // m: a lock point this near an enemy's pin brackets that pin
constexpr float kMapEmptyFade=0.55f;     // nobody in it (kMapEmpty): its pin this much of a crewed one's
constexpr int kMapLegendRows=15;
constexpr float kMapGuard=1.25f;         // a grid line is cut to this much of the screen's half size round it
constexpr float kMapNearW=1.0f;          // ...and to this clip w (m in front of the eye)

// The world segment a..b, cut in clip space to the part in front of the eye and on the screen (with a guard band:
// Liang-Barsky on the planes w >= kMapNearW, |x|, |y| <= kMapGuard w), drawn as one line: a straight line stays
// straight in a perspective view, and a line whose ends are both off the screen may still cross it.
void MapLine(void* drawer,void* ctx,const float* vp,float width,float height,const float* a,const float* b,float t,const float* rgba) noexcept {
    float ca[4],cb[4];
    for(int k=0;k<4;++k) {
        ca[k]=a[0]*vp[k]+a[1]*vp[4+k]+a[2]*vp[8+k]+vp[12+k];
        cb[k]=b[0]*vp[k]+b[1]*vp[4+k]+b[2]*vp[8+k]+vp[12+k];
    }
    // Each plane as d(c) >= 0, linear in the clip coordinates.
    const float da[5]={ca[3]-kMapNearW,kMapGuard*ca[3]+ca[0],kMapGuard*ca[3]-ca[0],kMapGuard*ca[3]+ca[1],kMapGuard*ca[3]-ca[1]};
    const float db[5]={cb[3]-kMapNearW,kMapGuard*cb[3]+cb[0],kMapGuard*cb[3]-cb[0],kMapGuard*cb[3]+cb[1],kMapGuard*cb[3]-cb[1]};
    float t0=0.0f,t1=1.0f;
    for(int i=0;i<5;++i) {
        if(da[i]<0.0f && db[i]<0.0f)return;
        if(da[i]<0.0f)t0=std::fmax(t0,da[i]/(da[i]-db[i]));
        else if(db[i]<0.0f)t1=std::fmin(t1,da[i]/(da[i]-db[i]));
    }
    if(!(t0<t1))return;
    float p[2][2];
    for(int e=0;e<2;++e) {
        const float f=e ? t1 : t0;
        float c[4];
        for(int k=0;k<4;++k)c[k]=ca[k]+(cb[k]-ca[k])*f;
        p[e][0]=width*0.5f*(c[0]/c[3])+width*0.5f;p[e][1]=height*0.5f-height*0.5f*(c[1]/c[3]);
    }
    Seg(drawer,ctx,p[0][0],p[0][1],p[1][0],p[1][1],t,rgba);
}

// A distance on the map: "850 m" / "1.5 km".
void MapDistance(wchar_t* out,std::size_t size,float m) noexcept {
    if(m<1000.0f)_snwprintf_s(out,size,_TRUNCATE,L"%.0f m",m);
    else _snwprintf_s(out,size,_TRUNCATE,L"%.1f km",m*0.001f);
}

// Whether a grid label at (x, y) keeps clear of the bands (title, keys), the legend and the scale bar.
bool MapLabelFree(float x,float y,float width,float height,float s) noexcept {
    if(y<60.0f*s || y>height-90.0f*s || x>width-140.0f*s)return false;   // the bands (and MapCommands' over the keys)
    if(x<240.0f*s && y>height*0.28f && y<height*0.30f+static_cast<float>(kMapLegendRows)*28.0f*s+20.0f*s)return false;   // the legend
    return !(x<420.0f*s && y>height-140.0f*s);                                             // the scale bar (MapScale)
}

// The grid, its axes through the player, its labels (where each line meets the other axis through the focus; one at
// least kMapLabelGap px from the last one drawn of its axis: they crowd toward the horizon).
void MapGrid(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const MapReadout& m,Line* lines,int* at) noexcept {
    const float step=mapcam::GridStep(m.height);
    const int n=mapcam::GridLines(m.height,m.pitch,step);
    const float y=m.focus[1],reach=static_cast<float>(n)*step;
    for(int axis=0;axis<2;++axis) {   // 0: the lines of constant x (east / west of the player), 1: of constant z
        const int c=axis*2,o=2-c;
        const int k0=static_cast<int>(std::floor((m.focus[c]-reach-m.me[c])/step)),k1=static_cast<int>(std::ceil((m.focus[c]+reach-m.me[c])/step));
        const int every=(k1-k0+1)>kMapLabels*2 ? 2 : 1;
        float lastX=-1e9f,lastY=-1e9f;
        for(int k=k0;k<=k1;++k) {
            float a[3],b[3];
            a[1]=b[1]=y;a[c]=b[c]=m.me[c]+static_cast<float>(k)*step;
            a[o]=m.focus[o]-reach;b[o]=m.focus[o]+reach;
            MapLine(drawer,ctx,vp,width,height,a,b,(k==0 ? 2.0f : 1.0f)*s,k==0 ? kMapAxis : kMapGrid);
            if(k==0 || k%every)continue;
            float p[3];p[1]=y;p[c]=a[c];p[o]=m.focus[o];
            float x,yy,depth;
            if(!Project(vp,p,width,height,&x,&yy,&depth) || !MapLabelFree(x,yy,width,height,s))continue;
            if(std::fabs(x-lastX)<kMapLabelGap*s && std::fabs(yy-lastY)<kMapLabelGap*s)continue;
            lastX=x;lastY=yy;
            // +x is west of +z (north), -x east; +z north, -z south.
            const wchar_t* side=axis==0 ? (k>0 ? L"W" : L"E") : (k>0 ? L"N" : L"S");
            wchar_t d[24];MapDistance(d,_countof(d),std::fabs(static_cast<float>(k))*step);
            Label(text,lines,at,x+4.0f*s,yy-10.0f*s,0,kLineScale*0.7f,kMapAxis,L"%ls %ls",side,d);
        }
    }
}

// The north arrow (top right): north's way on the screen from the focus.
void MapCompass(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const MapReadout& m,Line* lines,int* at) noexcept {
    const float north[3]={m.focus[0],m.focus[1],m.focus[2]+mapcam::GridStep(m.height)};
    float x0,y0,x1,y1,depth;
    if(!Project(vp,m.focus,width,height,&x0,&y0,&depth) || !Project(vp,north,width,height,&x1,&y1,&depth))return;
    float dx=x1-x0,dy=y1-y0;
    const float len=std::sqrt(dx*dx+dy*dy);
    if(!(len>0.5f))return;
    dx/=len;dy/=len;
    const float r=34.0f*s,cx=width-70.0f*s,cy=110.0f*s;
    Arc(drawer,ctx,cx,cy,r,0.0f,kTurn,2.0f*s,32,kMapAxis);
    Tri(drawer,ctx,cx,cy,cx+dx*r,cy+dy*r,8.0f*s,kMapEnemy);     // north red
    Tri(drawer,ctx,cx,cy,cx-dx*r,cy-dy*r,8.0f*s,kWhite);        // south white
    Label(text,lines,at,cx+dx*(r+14.0f*s),cy+dy*(r+14.0f*s),1,kLineScale*0.8f,kWhite,L"N");
}

// The scale bar (bottom left): one grid step across the screen at the focus.
void MapScale(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const MapReadout& m,Line* lines,int* at) noexcept {
    const float step=mapcam::GridStep(m.height);
    float r[3];mapcam::Right(m.yaw,r);
    const float b[3]={m.focus[0]+r[0]*step,m.focus[1],m.focus[2]+r[2]*step};
    float x0,y0,x1,y1,depth;
    if(!Project(vp,m.focus,width,height,&x0,&y0,&depth) || !Project(vp,b,width,height,&x1,&y1,&depth))return;
    const float len=std::sqrt((x1-x0)*(x1-x0)+(y1-y0)*(y1-y0));
    if(!(len>4.0f) || len>width*0.5f)return;
    const float x=40.0f*s,y=height-110.0f*s;
    Rect(drawer,ctx,x,y-2.0f*s,x+len,y+2.0f*s,kWhite);
    Rect(drawer,ctx,x-1.0f*s,y-8.0f*s,x+1.0f*s,y+8.0f*s,kWhite);
    Rect(drawer,ctx,x+len-1.0f*s,y-8.0f*s,x+len+1.0f*s,y+8.0f*s,kWhite);
    wchar_t d[24];MapDistance(d,_countof(d),step);
    Label(text,lines,at,x+len*0.5f,y-18.0f*s,1,kLineScale*0.75f,kWhite,L"%ls",d);
}

// A hollow square / diamond of half size `h` round (x, y).
void MapBox(void* drawer,void* ctx,float x,float y,float h,float t,const float* rgba) noexcept {
    Seg(drawer,ctx,x-h,y-h,x+h,y-h,t,rgba);Seg(drawer,ctx,x+h,y-h,x+h,y+h,t,rgba);
    Seg(drawer,ctx,x+h,y+h,x-h,y+h,t,rgba);Seg(drawer,ctx,x-h,y+h,x-h,y-h,t,rgba);
}
void MapDiamond(void* drawer,void* ctx,float x,float y,float h,float t,const float* rgba) noexcept {
    Seg(drawer,ctx,x,y-h,x+h,y,t,rgba);Seg(drawer,ctx,x+h,y,x,y+h,t,rgba);
    Seg(drawer,ctx,x,y+h,x-h,y,t,rgba);Seg(drawer,ctx,x-h,y,x,y-h,t,rgba);
}
// Corner brackets round (x, y), half size `h` (a lock, the nearest enemy).
void MapBrackets(void* drawer,void* ctx,float x,float y,float h,float t,const float* rgba) noexcept {
    const float k=h*0.4f;
    for(int sx=-1;sx<=1;sx+=2)for(int sy=-1;sy<=1;sy+=2) {
        const float cx=x+static_cast<float>(sx)*h,cy=y+static_cast<float>(sy)*h;
        Seg(drawer,ctx,cx,cy,cx-static_cast<float>(sx)*k,cy,t,rgba);Seg(drawer,ctx,cx,cy,cx,cy-static_cast<float>(sy)*k,t,rgba);
    }
}

// The pins' colours by side, and the icon's shape by kind (the user, 2026-10-06: "3D 显示各个友方微缩模型或者标记，
// 敌方也显示"): a constant size on the screen, at the top of the pin's stem (or, an aircraft, at the unit itself).
const float* MapColour(MapKind kind) noexcept {
    switch(kind) {
    case MapKind::squad: case MapKind::vehicle: return kMapSquad;
    case MapKind::ally: return kMapAlly;
    case MapKind::air: case MapKind::carrier: return kCyan;
    case MapKind::marker: return kMapMarker;
    default: return kMapEnemy;
    }
}
// `rgba` with its alpha times `fade` (into `out`, 16-aligned as the quad reads it).
const float* MapFade(const float* rgba,float fade,float* out) noexcept {
    out[0]=rgba[0];out[1]=rgba[1];out[2]=rgba[2];out[3]=rgba[3]*fade;
    return out;
}

// A friendly aircraft (kind air; the user, 2026-10-06: aircraft as plainly on the map as the vehicles, an icon of
// their own): a fixed-wing one a plane seen from above, its nose along its heading on the screen (`dx`, `dy`; none: up);
// a helicopter (kMapRotor) its rotor's ring round a short fuselage and tail boom.
void MapAircraft(void* drawer,void* ctx,float x,float y,float s,bool rotor,float dx,float dy,const float* rgba) noexcept {
    if(dx==0.0f && dy==0.0f)dy=-1.0f;
    const float rx=-dy,ry=dx,t=2.0f*s;
    auto seg=[&](float f0,float r0,float f1,float r1,float w){
        Seg(drawer,ctx,x+(dx*f0+rx*r0)*s,y+(dy*f0+ry*r0)*s,x+(dx*f1+rx*r1)*s,y+(dy*f1+ry*r1)*s,w,rgba);
    };
    if(rotor) {
        Arc(drawer,ctx,x,y,9.0f*s,0.0f,kTurn,1.5f*s,18,rgba);
        seg(5.0f,0.0f,-13.0f,0.0f,3.0f*s);seg(-12.0f,-3.0f,-12.0f,3.0f,t);
        return;
    }
    seg(12.0f,0.0f,-10.0f,0.0f,3.0f*s);                        // the fuselage
    seg(3.0f,0.0f,-4.0f,11.0f,t);seg(3.0f,0.0f,-4.0f,-11.0f,t);   // the swept wings
    seg(-4.0f,11.0f,-1.0f,0.0f,t);seg(-4.0f,-11.0f,-1.0f,0.0f,t);
    seg(-7.0f,0.0f,-11.0f,5.0f,t);seg(-7.0f,0.0f,-11.0f,-5.0f,t);   // the tail
}

// One icon at (x, y): `dx, dy` its heading on the screen (0, 0: none), `hp` its HP bar (<0: none). Nobody in it
// (kMapEmpty: a vehicle or aircraft of team 5) at kMapEmptyFade of it.
void MapIcon(void* drawer,void* ctx,float x,float y,float s,MapKind kind,std::uint8_t flags,float dx,float dy,float hp,float fade) noexcept {
    alignas(16) float c[4],b[4];
    if(flags&kMapEmpty)fade*=kMapEmptyFade;
    const float* rgba=MapFade(MapColour(kind),fade,c);
    const float t=2.0f*s;
    float tick=0.0f;   // the heading tick's start, from the centre
    switch(kind) {
    case MapKind::squad: Rect(drawer,ctx,x-4.5f*s,y-4.5f*s,x+4.5f*s,y+4.5f*s,rgba);break;
    case MapKind::ally: Rect(drawer,ctx,x-3.5f*s,y-3.5f*s,x+3.5f*s,y+3.5f*s,rgba);break;
    case MapKind::vehicle: MapBox(drawer,ctx,x,y,8.0f*s,t,rgba);Rect(drawer,ctx,x-3.0f*s,y-3.0f*s,x+3.0f*s,y+3.0f*s,rgba);tick=8.0f*s;break;
    case MapKind::air: MapAircraft(drawer,ctx,x,y,s,(flags&kMapRotor)!=0,dx,dy,rgba);tick=flags&kMapRotor ? 9.0f*s : 0.0f;break;
    case MapKind::carrier: MapBox(drawer,ctx,x,y,13.0f*s,t,rgba);MapBox(drawer,ctx,x,y,7.0f*s,t,rgba);tick=13.0f*s;break;
    case MapKind::enemy:
        if(flags&kMapLarge){MapBox(drawer,ctx,x,y,11.0f*s,t,rgba);Rect(drawer,ctx,x-6.0f*s,y-6.0f*s,x+6.0f*s,y+6.0f*s,rgba);tick=11.0f*s;}
        else Tri(drawer,ctx,x,y-5.0f*s,x,y+6.0f*s,6.0f*s,rgba);   // a small one: a red triangle, its point down onto it
        break;
    case MapKind::enemyAir:
        MapDiamond(drawer,ctx,x,y,(flags&kMapLarge ? 12.0f : 8.0f)*s,t,rgba);Rect(drawer,ctx,x-2.5f*s,y-2.5f*s,x+2.5f*s,y+2.5f*s,rgba);
        tick=(flags&kMapLarge ? 12.0f : 8.0f)*s;
        break;
    case MapKind::marker: Arc(drawer,ctx,x,y,13.0f*s,0.0f,kTurn,t,20,rgba);Rect(drawer,ctx,x-2.5f*s,y-2.5f*s,x+2.5f*s,y+2.5f*s,rgba);break;
    case MapKind::lock: break;
    }
    if(tick>0.0f && (dx!=0.0f || dy!=0.0f))Seg(drawer,ctx,x+dx*tick,y+dy*tick,x+dx*(tick+8.0f*s),y+dy*(tick+8.0f*s),t,rgba);
    if(hp>=0.0f)Bar(drawer,ctx,x-17.0f*s,y+15.0f*s,34.0f*s,4.0f*s,hp,hp,MapFade(HpColour(hp),fade,b),s);
}

// A small enemy's dot at (x, y): filled on the ground, hollow flying (kMapFlying).
void MapDot1(void* drawer,void* ctx,float x,float y,float s,std::uint8_t flags,const float* rgba) noexcept {
    if(flags&kMapFlying)MapBox(drawer,ctx,x,y,3.5f*s,1.5f*s,rgba);
    else Rect(drawer,ctx,x-3.0f*s,y-3.0f*s,x+3.0f*s,y+3.0f*s,rgba);
}

// Where a unit's pin is on the screen: its icon (`ix`, `iy`), its stem's other end (`bx`, `by`; false: none shown), its
// heading on the screen, its depth. A ground unit's stem stands `pin` m up from it to the icon; an aircraft's icon is
// on the aircraft, its stem down to the ground under it (its height read off the stem).
struct Pin { float ix,iy,bx,by,dx,dy,depth; bool stem; };
bool MapPin(const float* vp,float width,float height,const MapUnit& u,float pin,Pin* p) noexcept {
    // An aircraft standing on the ground (parked, landed: mapmarks::Landed) gets a ground unit's pin: its icon up on a
    // stem as a vehicle's, not lost in the ground's clutter at its wheels.
    const bool air=u.kind==MapKind::lock ||
                   ((u.kind==MapKind::air || u.kind==MapKind::carrier || u.kind==MapKind::enemyAir) && !mapmarks::Landed(u.pos[1],u.ground));
    float top[3]={u.pos[0],u.pos[1],u.pos[2]},base[3]={u.pos[0],u.pos[1],u.pos[2]};
    if(air)base[1]=u.ground;
    else top[1]+=pin;
    float depth;
    if(!Project(vp,top,width,height,&p->ix,&p->iy,&p->depth))return false;
    p->stem=(!air || u.pos[1]-u.ground>1.0f) && u.kind!=MapKind::lock && Project(vp,base,width,height,&p->bx,&p->by,&depth);
    p->dx=p->dy=0.0f;
    if(u.dir[0]!=0.0f || u.dir[1]!=0.0f) {
        const float ahead[3]={top[0]+u.dir[0]*pin*0.5f,top[1],top[2]+u.dir[1]*pin*0.5f};
        float x,y;
        if(Project(vp,ahead,width,height,&x,&y,&depth)) {
            const float dx=x-p->ix,dy=y-p->iy,len=std::sqrt(dx*dx+dy*dy);
            if(len>0.5f){p->dx=dx/len;p->dy=dy/len;}
        }
    }
    return true;
}

void MapUnits(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const MapReadout& m,Line* lines,int* at) noexcept {
    const mapcam::View view{{m.focus[0],m.focus[1],m.focus[2]},m.yaw,m.pitch,m.height};
    const float dist=mapcam::Distance(view),pin=mapcam::PinHeight(dist,m.pitch);
    // Depth fade: full at the focus's distance and nearer, kMapFarFade past three times it.
    auto fade=[&](float depth){ return 1.0f-(1.0f-kMapFarFade)*Unit((depth-dist)/(2.0f*dist)); };
    alignas(16) float c[4];
    // The small enemies first: flat dots (no pin), filled on the ground, hollow flying.
    for(int i=0;i<m.dots && i<kMapDots;++i) {
        const MapDot& d=m.dot[i];
        float x,y,depth;
        if(!Project(vp,d.pos,width,height,&x,&y,&depth))continue;
        MapDot1(drawer,ctx,x,y,s,d.flags,MapFade(kMapEnemy,fade(depth),c));
        if(d.flags&kMapNearest)MapBrackets(drawer,ctx,x,y,10.0f*s,2.0f*s,MapFade(kAmber,fade(depth),c));
    }
    // The large enemies' pins, the friendly side over them, the markers and the locks on top.
    static const MapKind kOrder[]={MapKind::enemy,MapKind::enemyAir,MapKind::ally,MapKind::squad,MapKind::vehicle,MapKind::air,
                                   MapKind::carrier,MapKind::marker};
    for(MapKind kind:kOrder) {
        for(int i=0;i<m.count && i<kMapUnits;++i) {
            const MapUnit& u=m.unit[i];
            Pin p;
            if(u.kind!=kind || !MapPin(vp,width,height,u,pin,&p))continue;
            const float f=fade(p.depth);
            if(p.stem) {
                Seg(drawer,ctx,p.bx,p.by,p.ix,p.iy,1.5f*s,MapFade(MapColour(kind),f*0.7f,c));
                if((kind==MapKind::air || kind==MapKind::carrier || kind==MapKind::enemyAir) && !mapmarks::Landed(u.pos[1],u.ground))
                    Arc(drawer,ctx,p.bx,p.by,4.0f*s,0.0f,kTurn,1.5f*s,10,c);   // a flying one's ground point
            }
            MapIcon(drawer,ctx,p.ix,p.iy,s,kind,u.flags,p.dx,p.dy,u.flags&kMapLarge ? u.hp : -1.0f,f);
            if(u.flags&kMapNearest)MapBrackets(drawer,ctx,p.ix,p.iy,16.0f*s,2.0f*s,MapFade(kAmber,f,c));
            if(kind==MapKind::marker && MapLabelFree(p.ix,p.iy-24.0f*s,width,height,s)) {
                wchar_t d[24];MapDistance(d,_countof(d),vec::Flat(u.pos,m.me));
                Label(text,lines,at,p.ix,p.iy-24.0f*s,1,kLineScale*0.7f,kMapMarker,L"%ls",d);
            }
        }
    }
    // The locks: brackets on the enemy (a large one's pin, a small one's dot) nearest the lock point within kMapLockNear m,
    // else on the point itself.
    auto d2=[](const float* a,const float* b){ const float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};return d[0]*d[0]+d[1]*d[1]+d[2]*d[2]; };
    for(int i=0;i<m.count && i<kMapUnits;++i) {
        const MapUnit& l=m.unit[i];
        if(l.kind!=MapKind::lock)continue;
        const float* rgba=l.flags&kMapAcquiring ? kAmber : kMapEnemy;
        const MapUnit* on=&l;
        const MapDot* dot=nullptr;
        float best=kMapLockNear*kMapLockNear;
        for(int k=0;k<m.count && k<kMapUnits;++k) {
            const MapUnit& u=m.unit[k];
            if((u.kind==MapKind::enemy || u.kind==MapKind::enemyAir) && d2(u.pos,l.pos)<best){best=d2(u.pos,l.pos);on=&u;}
        }
        for(int k=0;k<m.dots && k<kMapDots;++k)if(d2(m.dot[k].pos,l.pos)<best){best=d2(m.dot[k].pos,l.pos);dot=&m.dot[k];}
        float x,y,depth;
        Pin p;
        if(dot){if(Project(vp,dot->pos,width,height,&x,&y,&depth))MapBrackets(drawer,ctx,x,y,13.0f*s,2.5f*s,rgba);}
        else if(MapPin(vp,width,height,*on,pin,&p))MapBrackets(drawer,ctx,p.ix,p.iy,20.0f*s,2.5f*s,rgba);
    }
    // The player: a white pin, a ring and an arrow along their heading.
    MapUnit me{};
    std::memcpy(me.pos,m.me,12);me.ground=m.me[1];me.dir[0]=m.meDir[0];me.dir[1]=m.meDir[2];
    Pin p;
    if(!MapPin(vp,width,height,me,pin,&p))return;
    if(p.stem)Seg(drawer,ctx,p.bx,p.by,p.ix,p.iy,2.0f*s,kWhite);
    Arc(drawer,ctx,p.ix,p.iy,11.0f*s,0.0f,kTurn,2.5f*s,24,kWhite);
    if(p.dx!=0.0f || p.dy!=0.0f)Tri(drawer,ctx,p.ix-p.dx*5.0f*s,p.iy-p.dy*5.0f*s,p.ix+p.dx*20.0f*s,p.iy+p.dy*20.0f*s,7.0f*s,kWhite);
}

// The NPC commands (mapcmd.cpp, README 地图 → 指挥 NPC): the mouse pointer (where G sends the selection; with a pad a
// crosshair at the screen's centre) and the box being dragged from it, each commandable unit ringed (white brackets:
// selected), a guard order's line from the unit to its point (its slot of the formation) and a ring there, FOLLOW under a
// unit following the player, and a band over the keys: how many are selected, the keys, the last command's word.
alignas(16) const float kMapOrder[4]={0.3f,0.9f,1.0f,1.0f};
alignas(16) const float kMapOrderDim[4]={0.3f,0.9f,1.0f,0.55f};
alignas(16) const float kMapBoxFill[4]={0.3f,0.9f,1.0f,0.08f};
constexpr float kMapGuardRing=12.0f;      // m: the ring at a guard order's point (formation slots are 30 m apart)
void MapCommands(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const MapReadout& m,Line* lines,int* at) noexcept {
    static MapCommandReadout c;   // the draw thread's (too big for its stack)
    if(!PlayerMapCommands(&c))return;
    const mapcam::View view{{m.focus[0],m.focus[1],m.focus[2]},m.yaw,m.pitch,m.height};
    const float pin=mapcam::PinHeight(mapcam::Distance(view),m.pitch);
    const float* tint=c.allowed && c.count ? kMapOrder : kMapOrderDim;
    // Where G sends them: the pointer (a ring and a cross), or with a pad the crosshair at the centre.
    const float cx=c.pointer ? c.px : width*0.5f,cy=c.pointer ? c.py : height*0.5f;
    Seg(drawer,ctx,cx-16.0f*s,cy,cx-5.0f*s,cy,2.0f*s,tint);Seg(drawer,ctx,cx+5.0f*s,cy,cx+16.0f*s,cy,2.0f*s,tint);
    Seg(drawer,ctx,cx,cy-16.0f*s,cx,cy-5.0f*s,2.0f*s,tint);Seg(drawer,ctx,cx,cy+5.0f*s,cx,cy+16.0f*s,2.0f*s,tint);
    if(c.pointer)Arc(drawer,ctx,cx,cy,9.0f*s,0.0f,kTurn,1.5f*s,16,kWhite);
    // The box being dragged.
    if(c.boxing) {
        const float x0=c.bx<c.px ? c.bx : c.px,x1=c.bx<c.px ? c.px : c.bx,y0=c.by<c.py ? c.by : c.py,y1=c.by<c.py ? c.py : c.by;
        Rect(drawer,ctx,x0,y0,x1,y1,kMapBoxFill);
        Seg(drawer,ctx,x0,y0,x1,y0,1.5f*s,kMapOrder);Seg(drawer,ctx,x1,y0,x1,y1,1.5f*s,kMapOrder);
        Seg(drawer,ctx,x1,y1,x0,y1,1.5f*s,kMapOrder);Seg(drawer,ctx,x0,y1,x0,y0,1.5f*s,kMapOrder);
    }
    wchar_t one[24]{};
    for(int i=0;i<c.count && i<kCmdUnits;++i) {
        const CmdMark& u=c.unit[i];
        MapUnit mu{};
        std::memcpy(mu.pos,u.pos,12);mu.ground=u.pos[1];mu.kind=u.air ? MapKind::air : MapKind::vehicle;
        Pin p;
        const bool shown=MapPin(vp,width,height,mu,pin,&p);
        if(u.now.order==Order::guard) {
            // The ring round its point, its line from the unit.
            float last[3];
            for(int k=0;k<=16;++k) {
                const float a=kTurn*static_cast<float>(k)/16.0f;
                const float q[3]={u.now.at[0]+std::sin(a)*kMapGuardRing,u.now.at[1]+1.0f,u.now.at[2]+std::cos(a)*kMapGuardRing};
                if(k)MapLine(drawer,ctx,vp,width,height,last,q,2.0f*s,kMapOrder);
                std::memcpy(last,q,12);
            }
            float gx,gy,depth;
            if(shown && Project(vp,u.now.at,width,height,&gx,&gy,&depth))Seg(drawer,ctx,p.ix,p.iy,gx,gy,1.5f*s,kMapOrderDim);
        }
        if(!shown)continue;
        Arc(drawer,ctx,p.ix,p.iy,15.0f*s,0.0f,kTurn,1.5f*s,20,u.selected ? kWhite : kMapOrderDim);
        if(u.selected)MapBrackets(drawer,ctx,p.ix,p.iy,24.0f*s,2.5f*s,kWhite);
        if(u.now.order==Order::follow)Label(text,lines,at,p.ix,p.iy+24.0f*s,1,kLineScale*0.6f,kMapOrder,L"FOLLOW");
        if(u.selected && c.selected==1) {
            _snwprintf_s(one,_countof(one),_TRUNCATE,L"%hs",u.name);
            Label(text,lines,at,p.ix,p.iy-30.0f*s,1,kLineScale*0.7f,kWhite,L"%ls",one);
        }
    }
    // The band over the keys: how many are selected, the keys.
    Rect(drawer,ctx,0.0f,height-80.0f*s,width,height-46.0f*s,kMapBand);
    const float y=height-63.0f*s;
    wchar_t sel[48];
    if(c.all)_snwprintf_s(sel,_countof(sel),_TRUNCATE,L"SELECTED ALL %d",c.selected);
    else if(c.selected==1 && one[0])_snwprintf_s(sel,_countof(sel),_TRUNCATE,L"SELECTED 1 / %d: %ls",c.count,one);
    else _snwprintf_s(sel,_countof(sel),_TRUNCATE,L"SELECTED %d / %d",c.selected,c.count);
    if(!c.allowed)Label(text,lines,at,width*0.5f,y,1,kLineScale*0.75f,kAmber,L"NPC COMMANDS: OFFLINE ONLY");
    else if(!c.count)Label(text,lines,at,width*0.5f,y,1,kLineScale*0.75f,kWhite,L"NPC COMMANDS: no plugin NPC unit (helis, jets, crawlers)");
    else if(m.pad)Label(text,lines,at,width*0.5f,y,1,kLineScale*0.75f,kWhite,
                        L"%ls   X select (ALL last)   Y guard crosshair   RB follow me   LB release",sel);
    else Label(text,lines,at,width*0.5f,y,1,kLineScale*0.75f,kWhite,
               L"%ls   Ctrl+drag box (Shift add)   click pick / clear   Tab cycle   G guard pointer   V follow me   X release",sel);
    if(c.noteFresh)Label(text,lines,at,width*0.5f,height-100.0f*s,1,kLineScale*0.8f,kAmber,L"%ls",c.note);
}

// The legend (left), the title and the keys (top and bottom bands).
void MapText(void* drawer,void* ctx,Text* text,float width,float height,float s,const MapReadout& m,Line* lines,int* at) noexcept {
    Rect(drawer,ctx,0.0f,0.0f,width,46.0f*s,kMapBand);
    Rect(drawer,ctx,0.0f,height-46.0f*s,width,height,kMapBand);
    wchar_t h[24],g[24];
    MapDistance(h,_countof(h),m.height);MapDistance(g,_countof(g),mapcam::GridStep(m.height));
    Label(text,lines,at,width*0.5f,23.0f*s,1,kTitleScale,kWhite,L"MAP   height %ls   grid %ls%ls",h,g,m.follow ? L"   FOLLOW" : L"");
    if(m.pad)Label(text,lines,at,width*0.5f,height-23.0f*s,1,kLineScale*0.8f,kWhite,
                   L"L stick pan   R stick turn / tilt   RT / LT zoom   A centre   B / map button close");
    else {
        wchar_t key[32];KeyName(m.mapKey,key,32);
        Label(text,lines,at,width*0.5f,height-23.0f*s,1,kLineScale*0.8f,kWhite,
              L"LMB drag / WASD pan   RMB drag / Q E turn   R F tilt   wheel / + - zoom   Space centre   %ls / Esc close",key);
    }
    struct Entry { MapKind kind; std::uint8_t flags; const wchar_t* name; };
    static const Entry kLegend[]={{MapKind::squad,0,L"SQUAD"},{MapKind::ally,0,L"FRIENDLY"},{MapKind::vehicle,0,L"VEHICLE"},
                                  {MapKind::air,0,L"AIRCRAFT"},{MapKind::air,kMapRotor,L"HELICOPTER"},
                                  {MapKind::vehicle,kMapEmpty,L"EMPTY (NO CREW)"},{MapKind::carrier,0,L"CARRIER"},
                                  {MapKind::enemy,kMapLarge,L"LARGE ENEMY"},{MapKind::enemyAir,kMapLarge,L"LARGE ENEMY AIR"},
                                  {MapKind::marker,0,L"OBJECTIVE"}};
    float y=height*0.30f;
    Arc(drawer,ctx,40.0f*s,y,8.0f*s,0.0f,kTurn,2.0f*s,16,kWhite);
    Label(text,lines,at,60.0f*s,y,0,kLineScale*0.75f,kWhite,L"YOU");
    for(const Entry& e:kLegend) {
        y+=28.0f*s;
        MapIcon(drawer,ctx,40.0f*s,y,s,e.kind,e.flags,0.0f,0.0f,-1.0f,1.0f);
        Label(text,lines,at,60.0f*s,y,0,kLineScale*0.75f,kWhite,L"%ls",e.name);
    }
    y+=28.0f*s;
    MapDot1(drawer,ctx,40.0f*s,y,s,0,kMapEnemy);
    Label(text,lines,at,60.0f*s,y,0,kLineScale*0.75f,kWhite,L"ENEMY");
    y+=28.0f*s;
    MapDot1(drawer,ctx,40.0f*s,y,s,kMapFlying,kMapEnemy);
    Label(text,lines,at,60.0f*s,y,0,kLineScale*0.75f,kWhite,L"ENEMY FLYING");
    y+=28.0f*s;
    MapBrackets(drawer,ctx,40.0f*s,y,9.0f*s,2.0f*s,kMapEnemy);
    Label(text,lines,at,60.0f*s,y,0,kLineScale*0.75f,kWhite,L"LOCK");
    y+=28.0f*s;
    MapBrackets(drawer,ctx,40.0f*s,y,9.0f*s,2.0f*s,kAmber);
    Label(text,lines,at,60.0f*s,y,0,kLineScale*0.75f,kWhite,L"NEAREST ENEMY");
}

// The map view open: its marks drawn (true), nothing else of the HUD.
bool MapScreen(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept {
    static MapReadout m;   // the draw thread's (too big for its stack)
    if(!PlayerMap(&m))return false;
    MapGrid(drawer,ctx,text,vp,width,height,s,m,lines,at);
    MapUnits(drawer,ctx,text,vp,width,height,s,m,lines,at);
    MapCommandView(vp,width,height);   // the commands' box, clicks and pointer are found on this view
    MapCommands(drawer,ctx,text,vp,width,height,s,m,lines,at);
    MapScale(drawer,ctx,text,vp,width,height,s,m,lines,at);
    MapCompass(drawer,ctx,text,vp,width,height,s,m,lines,at);
    MapText(drawer,ctx,text,width,height,s,m,lines,at);
    return true;
}
}  // namespace

void HudDraw(const float* viewProj,void* ctx,const void* viewport,const CarrierPanel* panels,int count) noexcept {
    // The aim's view (CameraRay) stays the game's while the map's camera shows: the turret, the launcher and the sights
    // hold where the player left them.
    if(viewProj && !MapOwnsView())KeepViewProj(viewProj);
    if(!quadOk || !viewProj || !ctx || !viewport)return;   // the carriers' bars are drawn whatever VehicleHud says
    // The pause menu: nothing of the plugin's (docs/hud-re.md §10). The game still draws the scene and this HUD pass every
    // frame, and steps the cameras; the stock HUD slides out (its layouts' transition_slide). The snapshot's own age
    // (kFreshMs, wall) only caught the pause half a second in, and the carriers' bars and the map's marks not at all.
    if(GamePaused())return;
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
        if(MapScreen(drawer,ctx,t,viewProj,width,height,s,lines,&at)) {   // the map view: its marks alone
            if(t && textOk)DrawAll(*t,lines,at);
            FreeText(text);
            return;
        }
        const ULONGLONG now=GetTickCount64();
        for(int i=0;i<count && i<3;++i)CarrierBars(drawer,ctx,t,viewProj,width,height,s,panels[i],lines,&at,now);
        const Snapshot& snap=Latest();
        // A switch the player makes shows for a moment (hud_cue.h Change, kSwitchMs): the picked store (forgotten while no
        // aircraft's stores show, so boarding shows none) and EDF6AutoTurret's aim mode.
        static hudcue::Change storePick{},aimMode{};
        const bool fresh=now-snap.tick<=kFreshMs,storesShown=fresh && (snap.cockpit || snap.heliFly);
        if(!storesShown)storePick.seen=false;
        const int picked=snap.cockpit ? snap.jet.store : snap.stock && snap.stockHud.heli ? snap.stockHud.selected : -1;
        const bool storeSwitched=storesShown && hudcue::Changed(storePick,picked,now,kSwitchMs);
        const bool aimFlipped=fresh && snap.turret && hudcue::Changed(aimMode,static_cast<int>(snap.turretAim.mode),now,kSwitchMs);
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
                StoresText(stores,_countof(stores),snap.jet,false);
                LoadCell cells[6];
                const int n=JetCells(snap.jet,cells);
                HeliStrip(drawer,ctx,t,width,height,s,snap.jet.heli,snap.jet.fuel,stores,cells,n,snap.jet.store,storeSwitched,lines,&at);
            } else {
                FighterHud(drawer,ctx,t,viewProj,width,height,s,snap.jet,launchAt,lines,&at);
                if(Cfg().playerJetFlightHud)CockpitStrip(drawer,ctx,t,width,height,s,snap.jet,storeSwitched,lines,&at);
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
            const bool own=snap.stock && snap.stockHud.heli;   // its weapons, rounds and reloads (vhud.cpp)
            LoadCell cells[kStockArms];
            const int n=own ? StockCells(snap.stockHud,cells) : 0;
            HeliStrip(drawer,ctx,t,width,height,s,snap.heliHud.f,snap.heliHud.fuel,nullptr,cells,n,own ? snap.stockHud.selected : -1,
                      storeSwitched,lines,&at);
            if(snap.warned)Annunciator(drawer,ctx,t,width,height,s,snap.warn,lines,&at);
        }
        if(now-snap.tick<=kFreshMs && snap.heli && !snap.cockpit && !heliHud)HeliPanel(drawer,ctx,t,width,height,s,snap.heliCue,lines,&at);
        // The stock vehicles' HUD up (StockVehicleHud): it carries the drill's line and the gun's marks, so DrillPanel and
        // TurretMark's square give way to it.
        const bool stockHud=now-snap.tick<=kFreshMs && snap.stock && !snap.cockpit && !snap.stockHud.heli;
        if(now-snap.tick<=kFreshMs && snap.drill && !snap.cockpit && !snap.heli && !stockHud)DrillPanel(drawer,ctx,t,width,height,s,snap.drillCue,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.heliSight && !snap.cockpit)HeliGunSight(drawer,ctx,t,viewProj,width,height,s,snap.heliAim,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.launcher)LauncherMarks(drawer,ctx,t,viewProj,width,height,s,snap.launch,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.highCam && !snap.cockpit)HighCamHint(t,width,height,s,snap.highCamOn,snap.highCamKeys,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.seats)SeatLine(t,width,height,snap.seatPrompt,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.turretCamOk && !snap.cockpit)TurretMark(drawer,ctx,viewProj,width,height,s,snap.turretCam,!stockHud);
        if(now-snap.tick<=kFreshMs && snap.gunner && !snap.cockpit)GunnerMarks(drawer,ctx,t,viewProj,width,height,s,snap.gun,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.turret && !snap.cockpit)TurretAimMarks(drawer,ctx,t,viewProj,width,height,s,snap.turretAim,aimFlipped,lines,&at);
        if(stockHud) {
            const StockExtras x{fresh && snap.nix ? &snap.nixTorso : nullptr,fresh && snap.drill ? &snap.drillCue : nullptr,
                                snap.turret && snap.turretAim.ownGun && snap.turretAim.mode==edf::aimlink::Mode::leadCircle && snap.turretAim.lead,
                                fresh && snap.emc ? &snap.emcCue : nullptr,
                                fresh && snap.proteus ? &snap.proteusRo : nullptr};
            StockVehicleHud(drawer,ctx,t,viewProj,width,height,s,snap.stockHud,x,lines,&at);
        }
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
