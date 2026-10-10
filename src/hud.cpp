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
#include "formation.h"
#include "map_buttons.h"
#include "scopeview.h"
#include "reticle.h"
#include "boarding_entrance.h"
#include "gear.h"
#include "hudscale.h"
#include "map_cam.h"
#include "map_marks.h"
#include "hud_cue.h"
#include "hudtext.h"
#include "layout.h"
#include "memory.h"
#include "player_view.h"
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
using hudtext::Tr;
using hudtext::Tx;
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
// The screen the game's HUD is laid out on (src/hudscale.h, docs/hud-re.md §0.1): *(*(EDF+0x2137090)+0x10), its width
// and height (int) at +0x20 / +0x24. A HUD text of the game's reads it so (0x94E24F: mov rax,[EDF+0x2137090];
// mov rcx,[rax+0x10]; mov eax,[rcx+0x20]), as does the follower gauge's bar (0x8044B1).
constexpr std::size_t kUiScreen=0x2137090,kUiScreenObj=0x10,kUiW=0x20,kUiH=0x24;
const Sig kUiSigs[]={
    {0x94E24F,{0x48,0x8B,0x05,0x3A,0x8E,0x7E,0x01,0x48,0x8B,0x48,0x10,0x8B},12},
    {0x8044B1,{0x48,0x8B,0x05,0xD8,0x2B,0x93,0x01},7},
};
// The rescue message's draw (0x808410) makes these calls in this order: the sequence this file repeats.
const unsigned kTextCalls[][2]={{0x808611,kTextMake},{0x808622,kTextBegin},{0x80863F,kTextMeasure},{0x8086A9,kTextDraw},
                                {0x8086B5,kTextEnd},{0x8086BF,kTextFree}};

bool quadOk=false,textOk=false,uiOk=false;

// The game's text language (docs/hud-re.md §11): Option_Language's index (0 ja, 1 en, 2 kr, 3 cn: Traditional, 4 sc:
// Simplified), one int EDF.dll reads through its getter 0xE4000 (mov eax,[rip+X]; ret) and sets through 0xE4030
// (mov [rip+X],ecx; ret) from the options (0x9457DE: options+0x310) and from Steam's language at start (0x706EF0 maps
// english / japanese / koreana / tchinese / schinese to 1 / 0 / 2 / 3 / 4). The font manager's loader takes the getter's
// value for its fonts' order (0x963724 call 0xE4000, 0x96372E call 0x963830).
constexpr unsigned kLangGet=0xE4000,kLangSet=0xE4030,kFontLoad=0x963830;
constexpr std::size_t kLangValue=0x20B2B30;
const Sig kLangSigs[]={
    {kLangGet,{0x8B,0x05,0x2A,0xEB,0xFC,0x01,0xC3},7},   // mov eax,[EDF+0x20B2B30]; ret
    {kLangSet,{0x89,0x0D,0xFA,0xEA,0xFC,0x01,0xC3},7},   // mov [EDF+0x20B2B30],ecx; ret
};
bool langOk=false;

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
                  bool mountedOptic,turretBinding; edf::aimlink::ModeBindingV1 binding;
                  bool stock; StockHudReadout stockHud;
                  bool payload; PayloadReadout payloadHud;
                  bool warned; Warnings warn;
                  bool seats; SeatPrompt seatPrompt;
                  bool entrance; BoardingEntrance boardingEntrance;
                  bool turretCamOk; TurretCamReadout turretCam;
                  bool nix; NixTorso nixTorso;
                  bool proteus; ProteusReadout proteusRo;
                  bool sazabi; SazabiCue sazabiCue;
                  bool armor; armorhud::Readout armorRo; };
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
constexpr int kMaxLines=160;   // the map's panels, card, support bar and tooltip on top of its grid labels

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
    float s=1.0f;                 // the HUD's scale (hudscale.h): every line's font scale is times it
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
    Font(t,hudscale::Font(l.scale,t.s));
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
    Font(t,hudscale::Font(l.scale,t.s));
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
        wchar_t kind[24];
        hudtext::WordTo(d.kind,kind,_countof(kind));
        Format(l,L"%ls %d%%",kind,static_cast<int>(std::lround(share*100.0f)));
        if(d.guns>=0)Append(l,Tr(Tx::readoutGuns),d.guns);
        if(d.missiles>=0)Append(l,Tr(Tx::readoutMissiles),d.missiles);
        if(d.drones>=0)Append(l,Tr(Tx::readoutDrones),d.drones);
        if(d.leaving)Append(l,L"%ls",Tr(Tx::readoutRtb));
        else if(d.fuel>=0.0f) {
            const int sec=static_cast<int>(d.fuel);
            Append(l,Tr(Tx::readoutFuel),sec/60,sec%60);
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
    if(count>1)Format(*title,Tr(Tx::carrierTitleN),index+1);
    else Format(*title,L"%ls",Tr(Tx::carrierTitle));
    title->scale=kTitleScale;title->rgba=kTitle;
    const float hullShare=p.hullMax>0.0f ? p.hull/p.hullMax : 0.0f;
    Format(*hull,Tr(Tx::carrierHull),static_cast<int>(std::lround(hullShare*100.0f)),p.hull,p.hullMax);
    hull->scale=kLineScale;hull->rgba=kWhite;
    for(int k=0;k<p.parts;++k) {
        Line& l=lines[first+2+k];
        const auto& part=p.part[k];
        wchar_t name[24];
        hudtext::WordTo(part.name,name,_countof(name));
        if(part.down) {
            const int sec=static_cast<int>(part.repairSec);
            Format(l,Tr(Tx::carrierPartDown),name,sec/60,sec%60);
            l.rgba=kDown;
        } else {
            Format(l,L"%ls   %d%%",name,static_cast<int>(std::lround(part.max>0.0f ? part.hp*100.0f/part.max : 0.0f)));
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

// A key's actual binding on this keyboard, including mouse buttons and extended keys.
void KeyName(int vk,wchar_t* out,int size) noexcept {
    if(vk<=0){wcsncpy_s(out,size,Tr(Tx::controlUnbound),_TRUNCATE);return;}
    const wchar_t* mouse=vk==VK_LBUTTON ? Tr(Tx::mouseLeft) : vk==VK_RBUTTON ? Tr(Tx::mouseRight) : vk==VK_MBUTTON ? Tr(Tx::mouseMiddle) :
                         vk==VK_XBUTTON1 ? Tr(Tx::mouseX1) : vk==VK_XBUTTON2 ? Tr(Tx::mouseX2) : nullptr;
    if(mouse){wcsncpy_s(out,size,mouse,_TRUNCATE);return;}
    const UINT scan=MapVirtualKeyW(static_cast<UINT>(vk),MAPVK_VK_TO_VSC_EX);
    // Some layouts return the keypad scan without E0 even for the navigation VKs (Home otherwise reads "Num 7").
    const bool extended=(scan&0xFF00)!=0 || (vk>=VK_PRIOR && vk<=VK_DOWN) || vk==VK_INSERT || vk==VK_DELETE ||
                        vk==VK_DIVIDE || vk==VK_NUMLOCK || vk==VK_RCONTROL || vk==VK_RMENU;
    const LONG bits=static_cast<LONG>(((scan&0xFF)<<16) | (extended ? 0x01000000 : 0));
    if(!scan || GetKeyNameTextW(bits,out,size)<=0)std::swprintf(out,size,Tr(Tx::virtualKey),vk);
}

// These are EDF seat-button bits, not XInput's differently numbered bits. The caller passes the flight/payload
// readout's mask, so hints follow the same binding that actually switches its store or target.
void SeatButtonName(int mask,wchar_t* out,int size) noexcept {
    static const wchar_t* names[]={L"A",L"B",L"X",L"Y",L"LB",L"RB",L"L3",L"R3"};
    out[0]=0;
    if(mask<=0){wcsncpy_s(out,size,Tr(Tx::controlUnbound),_TRUNCATE);return;}
    if(mask&~0xFF){std::swprintf(out,size,Tr(Tx::padButton),mask);return;}
    for(int i=0;i<8;++i)if(mask&(1<<i)) {
        if(out[0])wcscat_s(out,size,L"/");
        wcscat_s(out,size,names[i]);
    }
}
void AircraftControls(Line& line,bool keys,int choices,int storeButton,int targetButton,bool target,bool flares) noexcept {
    Format(line,L"");
    auto add=[&](Tx text,const wchar_t* key){if(line.text[0])Append(line,L"   ");Append(line,Tr(text),key);};
    wchar_t key[32];
    if(choices>1) {
        if(keys)KeyName(Cfg().playerJetSwitchKey,key,32);else SeatButtonName(storeButton,key,32);
        add(Tx::controlStores,key);
    }
    if(target) {
        if(keys)KeyName(Cfg().playerJetTargetKey,key,32);else SeatButtonName(targetButton,key,32);
        add(Tx::controlTarget,key);
    }
    if(flares) {
        KeyName(Cfg().playerJetFlareKey,key,32);
        add(keys ? Tx::controlFlares : Tx::controlFlaresKeyboard,key); // the flight path currently has no pad flare binding
    }
}
void JetControls(Line& line,const PlayerJetReadout& j) noexcept {
    bool guided=false;
    for(int i=0;i<j.stores && i<kMostStores;++i)guided=guided || j.storeRole[i]==static_cast<int>(StoreRole::air) ||
                                                        j.storeRole[i]==static_cast<int>(StoreRole::ground);
    AircraftControls(line,j.keys,j.stores,j.storeButton,j.targetButton,guided,j.stores>0 && Cfg().playerJetFlares>0);
}
// A persistent compact control row below the stores, without restoring the removed cockpit panel.
void ControlRow(Text* text,float width,float y,float s,Line& line) noexcept {
    line.scale=kLineScale*0.85f;line.rgba=kCyan;line.w=line.h=0;
    if(text) {
        MeasureAll(*text,&line,1);
        const float most=std::fmax(width-32.0f*s,1.0f);
        if(line.w>most){line.scale*=most/line.w;MeasureAll(*text,&line,1);}
    }
    line.x=(width-line.w)*0.5f;line.y=y;
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

// The selected unguided store's map impact (bomb or rocket CCIP; guided stores keep their lock mark).
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
    Format(line,Tr(Tx::launcherLine),static_cast<int>(std::lround(l.range)),l.flight,static_cast<int>(std::lround(l.elevation)));
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
    else if(Cfg().highCamButton!=0x80)std::swprintf(key,32,Tr(Tx::padButton),Cfg().highCamButton);
    if((keys && Cfg().highCamKey<=0) || (!keys && Cfg().highCamButton<=0))return;
    Line& l=lines[(*at)++];
    Format(l,Tr(on ? Tx::highCamOn : Tx::highCamOff),key);
    l.scale=kLineScale*0.85f;l.rgba=on ? kGreen : kWhite;l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
    l.x=(width-l.w)*0.5f;l.y=height*0.86f+2.0f*s;
}

// The seats of the vehicle the player sits in (seatswitch.cpp, the user 2026-10-06 / 2026-10-07): a line low on the
// screen, each seat's number, what it is and who holds it, the player's in brackets. The prompt's moment (p.hints) adds
// the keys that move them; amber a moment after a refused press (the seat named taken, or no free seat), grey online with
// SeatSwitchOnline off. Outside it (SeatList: the whole ride) the bare list, dimmed.
// The seats line's tail in the prompt's moment: the keys that move the player, or why they cannot (online).
void SeatKeys(Line& l,const SeatPrompt& p) noexcept {
    if(p.locked)Append(l,L"%ls",Tr(Tx::seatOnlineLocked));
    else if(p.keys) {
        wchar_t key[32];
        KeyName(Cfg().seatNextKey,key,32);
        if(Cfg().seatNextKey>0)Append(l,Tr(Tx::seatNextKey),key);
        if(Cfg().seatNumberKeys)Append(l,Tr(Tx::seatPick),p.seats<9 ? p.seats : 9);
    } else if(Cfg().seatButton==0x02)Append(l,Tr(Tx::seatNextKey),L"B");
    else if(Cfg().seatButton>0) {
        wchar_t button[32];
        std::swprintf(button,32,Tr(Tx::padButton),Cfg().seatButton);
        Append(l,Tr(Tx::seatNextKey),button);
    }
}

void SeatLine(Text* text,float width,float height,const SeatPrompt& p,Line* lines,int* at) noexcept {
    if(*at>=kMaxLines || p.seats<2)return;
    Line& l=lines[(*at)++];
    Format(l,L"%ls",Tr(Tx::seats));
    const wchar_t* const holders[]={L"-",Tr(Tx::holderYou),Tr(Tx::holderNpc),Tr(Tx::holderTaken)};
    for(int i=0;i<p.seats && i<kMostSeatsShown;++i) {
        const Tx what=i==0 ? (p.aircraft ? Tx::seatPilot : Tx::seatDriver) : p.gun[i] ? Tx::seatGun : Tx::seatOther;
        Append(l,i==p.at ? L"  [%d %ls %ls]" : L"  %d %ls %ls",i+1,Tr(what),holders[static_cast<int>(p.holder[i])&3]);
    }
    if(p.hints)SeatKeys(l,p);
    if(p.refused==-2)Append(l,L"%ls",Tr(Tx::seatNoFree));
    else if(p.refused>=0)Append(l,Tr(Tx::seatTaken),p.refused+1);
    alignas(16) static const float kGrey[4]={0.7f,0.7f,0.7f,0.9f};
    alignas(16) static const float kDim[4]={1.0f,1.0f,1.0f,0.7f};
    l.scale=kLineScale*0.85f;l.rgba=!p.hints ? kDim : p.locked ? kGrey : p.refused!=-1 ? kWarn : kWhite;l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
    l.x=(width-l.w)*0.5f;l.y=height*0.82f;
}

void Arc(void* drawer,void* ctx,float cx,float cy,float r,float from,float span,float t,int sides,const float* rgba) noexcept;

// The controller's validated mouse command is an open circle; free look holds it in cyan.
// The actual gun/impact is a separate mark. Stock HUD already supplies that mark, so
// `square` only requests the legacy barrel-deviation square when that HUD is absent.
void TurretMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const TurretCamReadout& r,bool square) noexcept {
    if(r.physicalOnly || r.high)return;
    float sx,sy;
    // r.aim is the controller's validated mouse command/held point. Never use the
    // physical barrel endpoint r.gun as a surrogate mouse cursor.
    if(r.aimValid && (r.decoupled || r.freeLook) && sight::ToScreen(vp,r.aim,1.0f,width,height,&sx,&sy))
        Arc(drawer,ctx,sx,sy,14.0f*s,0.0f,6.283185307f,2.0f*s,28,r.freeLook ? kCyan : kWhite);
    if(square && r.aimValid && !r.onTarget && sight::ToScreen(vp,r.gun,1.0f,width,height,&sx,&sy)) {
        const float h=9.0f*s,t=2.0f*s;
        Rect(drawer,ctx,sx-h,sy-h,sx+h,sy-h+t,kWhite);Rect(drawer,ctx,sx-h,sy+h-t,sx+h,sy+h,kWhite);
        Rect(drawer,ctx,sx-h,sy-h,sx-h+t,sy+h,kWhite);Rect(drawer,ctx,sx+h-t,sy-h,sx+h,sy+h,kWhite);
    }

}

// The gunship's gun with the player at it (playerjet_crew.inc, README 炮舰机): the cross where a round of the picked gun
// fired now lands (where the screen's centre meets the ground), red out of its reach; under it the gun (SHELLS, CANNON
// or GATLING in brackets, the others there named after it, in the switch's order), the range and READY or the gun's
// wait; a cyan square
// on the pylon turn's centre (the point last fired at). No ground under the centre: the line alone.
// The gunship gunner's sight (the user, 2026-10-08: "the gunship too"), as an AC-130's sensor view frames its guns:
// round the screen's centre (the gunner's line of sight: the camera aims the guns) four corner brackets and a
// crosshair broken at the middle (its lower arm under the gun line), and round the middle a mark for the gun picked, each its own shape so a glance tells
// which is up: the shells' (105 mm) a square, the cannon's (40 mm) a diamond, the gatling's (25 mm) a ring. Drawn dim
// under GunnerMarks' impact cross (the round's point, which the frame is not).
alignas(16) const float kFrameTint[4]={0.85f,0.95f,0.85f,0.55f};   // a sensor view's symbols: pale, under the HUD's marks
constexpr float kFrameW=200.0f,kFrameH=130.0f,kFrameLeg=44.0f,kHairGap=18.0f,kHairOut=110.0f;   // px at 1080 lines
// px under the centre the lower hair starts at: GunnerMarks' gun line is under the centre (22 px), clear of it
constexpr float kHairUnder=44.0f,kGunMark=15.0f;
void Seg(void* drawer,void* ctx,float x0,float y0,float x1,float y1,float t,const float* rgba) noexcept;
void Arc(void* drawer,void* ctx,float cx,float cy,float r,float from,float span,float t,int sides,const float* rgba) noexcept;
void Label(Text* text,Line* lines,int* at,float x,float y,int align,float scale,const float* rgba,const wchar_t* format,...) noexcept;
void GunshipFrame(void* drawer,void* ctx,float width,float height,float s,GunnerGun gun) noexcept {
    const float cx=width*0.5f,cy=height*0.5f,t=2.0f*s,w=kFrameW*s,h=kFrameH*s,leg=kFrameLeg*s;
    for(int sxi=-1;sxi<=1;sxi+=2)
        for(int syi=-1;syi<=1;syi+=2) {
            const float x=cx+sxi*w,y=cy+syi*h;
            Seg(drawer,ctx,x,y,x-sxi*leg,y,t,kFrameTint);Seg(drawer,ctx,x,y,x,y-syi*leg,t,kFrameTint);
        }
    const float gap=kHairGap*s,out=kHairOut*s,thin=1.5f*s;
    Seg(drawer,ctx,cx-out,cy,cx-gap,cy,thin,kFrameTint);Seg(drawer,ctx,cx+gap,cy,cx+out,cy,thin,kFrameTint);
    Seg(drawer,ctx,cx,cy-out,cx,cy-gap,thin,kFrameTint);Seg(drawer,ctx,cx,cy+kHairUnder*s,cx,cy+out,thin,kFrameTint);
    const float m=kGunMark*s;   // round the impact cross (12 px): the cross stays the round's point, the mark the gun's
    switch(gun) {
        case GunnerGun::shells:
            Seg(drawer,ctx,cx-m,cy-m,cx+m,cy-m,t,kFrameTint);Seg(drawer,ctx,cx+m,cy-m,cx+m,cy+m,t,kFrameTint);
            Seg(drawer,ctx,cx+m,cy+m,cx-m,cy+m,t,kFrameTint);Seg(drawer,ctx,cx-m,cy+m,cx-m,cy-m,t,kFrameTint);
            break;
        case GunnerGun::cannon:
            Seg(drawer,ctx,cx,cy-m,cx+m,cy,t,kFrameTint);Seg(drawer,ctx,cx+m,cy,cx,cy+m,t,kFrameTint);
            Seg(drawer,ctx,cx,cy+m,cx-m,cy,t,kFrameTint);Seg(drawer,ctx,cx-m,cy,cx,cy-m,t,kFrameTint);
            break;
        default: Arc(drawer,ctx,cx,cy,m,0.0f,6.2831853f,t,16,kFrameTint);break;
    }
}

void GunnerMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const GunnerReadout& g,
                 Line* lines,int* at) noexcept {
    GunshipFrame(drawer,ctx,width,height,s,g.gun);
    if(g.zoom>1.0f)Label(text,lines,at,width*0.5f-kFrameW*s,height*0.5f-(kFrameH+14.0f)*s,0,kLineScale*0.85f,kFrameTint,L"%.0fx",
                         g.zoom);   // over the top left bracket
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
    wchar_t gun[64];
    const wchar_t* const names[]={Tr(Tx::gunnerShells),Tr(Tx::gunnerCannon),Tr(Tx::gunnerGatling)};
    static_assert(sizeof(names)/sizeof(names[0])==static_cast<std::size_t>(GunnerGun::count),"GunnerGun's names");
    const int picked=static_cast<int>(g.gun),n=static_cast<int>(GunnerGun::count);
    const bool more=(g.guns&~(1u<<picked))!=0;
    int used=std::swprintf(gun,64,more ? L"[%ls]" : L"%ls",names[picked]);
    for(int k=1;k<n && used>0;++k) {   // the others there, from the one the switch goes to next
        const int i=(picked+k)%n;
        if(!(g.guns&(1u<<i)))continue;
        const int w=std::swprintf(gun+used,64-used,L" %ls",names[i]);
        if(w<0)break;   // no room: the names so far
        used+=w;
    }
    if(!g.ground)Format(line,Tr(Tx::gunnerNoGround),gun);
    else if(!g.inReach)Format(line,Tr(Tx::gunnerOutOfRange),gun,static_cast<int>(std::lround(g.range)));
    else if(g.ready)Format(line,Tr(Tx::gunnerReady),gun,static_cast<int>(std::lround(g.range)));
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
        const int n=_snwprintf_s(text,size,_TRUNCATE,Tr(Tx::storesGun),j.gunRounds>0 ? j.gunRounds : 0);
        if(n>0)at=static_cast<std::size_t>(n);
    }
    for(int i=0;names && i<j.stores && i<kMostStores;++i) {
        const int n=_snwprintf_s(text+at,size-at,_TRUNCATE,i==j.store ? L"[%hs %d]  " : L"%hs %d  ",
                                 j.storeName[i] ? j.storeName[i] : "?",j.storeRounds[i]);
        if(n<0)break;
        at+=static_cast<std::size_t>(n);
    }
    if(j.air)_snwprintf_s(text+at,size-at,_TRUNCATE,Tr(Tx::storesFlare),j.flares);
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
        _snwprintf_s(out,size,_TRUNCATE,Tr(Tx::fuelTime),share,sec/60,sec%60);
    } else _snwprintf_s(out,size,_TRUNCATE,Tr(Tx::fuelShare),share);
}

// The takeoff roll's cue (empty: none; `rotate`: the cue to pull up is showing). On the takeoff roll (not in the air:
// j.ground only says there is ground under it, so the cue stayed on after takeoff, 2026-10-05): the speed it may lift
// off from coming up, then the cue to pull up (the user, 2026-10-05).
void TakeoffCue(const PlayerJetReadout& j,wchar_t* cue,std::size_t size,bool* rotate) noexcept {
    const int rotateKmh=static_cast<int>(std::lround(j.rotate*3.6f));
    const bool rolling=!j.air && j.rotate>0.0f && j.speed>1.0f;
    *rotate=rolling && j.speed>=j.rotate;
    cue[0]=L'\0';
    if(*rotate)_snwprintf_s(cue,size,_TRUNCATE,L"%ls",Tr(Tx::rotateNow));
    else if(rolling && j.speed>=j.rotate*0.7f)_snwprintf_s(cue,size,_TRUNCATE,Tr(Tx::rotateAt),rotateKmh);
}
// The old panel's cue (empty: none): the ground-proximity warning, a missile, a lock, else the takeoff's (TakeoffCue).
void CockpitCue(const PlayerJetReadout& j,wchar_t* cue,std::size_t size,bool* rotate) noexcept {
    TakeoffCue(j,cue,size,rotate);
    if(j.pullUp)_snwprintf_s(cue,size,_TRUNCATE,L"%ls",Tr(Tx::pullUpTerrain));
    else if(j.threat==2)_snwprintf_s(cue,size,_TRUNCATE,L"%ls",Tr(Tx::missileBang));
    else if(j.threat==1)_snwprintf_s(cue,size,_TRUNCATE,L"%ls",Tr(Tx::locked));
    else if(j.area==2)_snwprintf_s(cue,size,_TRUNCATE,L"%ls",Tr(Tx::areaTurning));
    else if(j.area==1)_snwprintf_s(cue,size,_TRUNCATE,L"%ls",Tr(Tx::areaAhead));
    else if(!cue[0] && FuelLow(j.fuel))_snwprintf_s(cue,size,_TRUNCATE,L"%ls",Tr(Tx::lowFuel));
}
// The cue's colour (over `calm` without one): the ground and a missile blink red and white (8 Hz), a lock is yellow,
// a stall red, the area's edge amber (blinking while a wall turns it back), the pull-up cue blinks green (4 Hz).
const float* CueColour(const PlayerJetReadout& j,bool rotate,const float* calm) noexcept {
    const bool blink=(GetTickCount64()/125)%2==0;
    return j.pullUp || j.threat==2 ? (blink ? kRed : kWhite) : j.threat==1 ? kYellow : j.stall ? kRed :
           j.area==2 ? (blink ? kAmber : kWhite) : j.area==1 ? kAmber : rotate && blink ? kGreen : rotate ? kYellow : calm;
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
    Format(info,Tr(Tx::jetInfo),static_cast<int>(std::lround(j.speed*3.6f)),
           j.ground ? L"" : L"*",static_cast<int>(std::lround(alt)),Tr(j.climb>=0.0f ? Tx::climbUp : Tx::climbDown),
           static_cast<int>(std::lround(std::fabs(j.climb))),static_cast<int>(std::lround(j.hpMax>0.0f ? 100.0f*j.hp/j.hpMax : 0.0f)));
    const int rotateKmh=static_cast<int>(std::lround(j.rotate*3.6f));
    wchar_t cue[64];
    bool rotate=false;
    CockpitCue(j,cue,_countof(cue),&rotate);
    wchar_t fuel[32];
    FuelText(fuel,_countof(fuel),j.fuel);
    wchar_t stall[24]=L"";
    if(j.stall)std::swprintf(stall,24,L"    %ls",Tr(Tx::stall));
    Format(thr,Tr(Tx::jetThrottle),static_cast<int>(std::lround(j.throttle*100.0f)),fuel[0] ? L"    " : L"",fuel,j.load,
           stall,cue[0] ? L"    " : L"",cue);
    StoresLine(arms,j);
    if(j.keys) {
        wchar_t boost[32],brake[32],swap[32];
        KeyName(Cfg().playerJetBoostKey,boost,32);KeyName(Cfg().playerJetBrakeKey,brake,32);KeyName(Cfg().playerJetSwitchKey,swap,32);
        if(j.air) {
            Format(keys,L"%ls",Tr(Cfg().playerJetMouseFlight ? Tx::jetKeysMouse : Tx::jetKeysHand));
            if(Cfg().playerJetMouseFlight)Format(keys,L"%ls",Tr(Tx::jetKeysMouseHand));
            wchar_t target[32];KeyName(Cfg().playerJetTargetKey,target,32);
            wchar_t flare[32];KeyName(Cfg().playerJetFlareKey,flare,32);
            Format(keys2,Tr(Tx::jetKeysAir),boost,brake,swap,target,flare);
        } else {
            Format(keys,Tr(Tx::jetKeysGround),boost,brake);
            Format(keys2,Tr(Tx::jetKeysTakeoff),rotateKmh);
        }
    } else if(j.air) {
        Format(keys,L"%ls",Tr(Tx::jetPadAir));
        wchar_t swap[32],target[32],flare[32];
        SeatButtonName(j.storeButton,swap,32);SeatButtonName(j.targetButton,target,32);KeyName(Cfg().playerJetFlareKey,flare,32);
        Format(keys2,Tr(Tx::jetPadAir2),swap,target,flare);
    } else {
        Format(keys,L"%ls",Tr(Tx::jetPadGround));
        Format(keys2,Tr(Tx::jetPadTakeoff),rotateKmh);
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
constexpr float kBoxOff=280.0f,kBoxW=120.0f,kBoxH=34.0f,kBoxRow=30.0f;
// Central instruments end above the lower dock; the remaining 6% separates its
// measured status rows from the projected pitch geometry.
constexpr float kFlightRegionBottom=0.58f,kLoadoutRegionShare=0.36f;
// The world directions `a` and `b` joined on the screen (both in front of the eye); `dashes` > 0: that many dashes.
void DirSeg(void* drawer,void* ctx,const float* vp,float width,float height,const float* a,const float* b,float t,int dashes,
            const float* rgba) noexcept {
    float x0,y0,x1,y1;
    if(!sight::ToScreen(vp,a,0.0f,width,height,&x0,&y0) || !sight::ToScreen(vp,b,0.0f,width,height,&x1,&y1))return;
    // Pitch geometry owns the central flight window. Clip entire segments, not
    // just their degree labels, clear of the instruments and lower dock.
    const float scale=t*0.5f,half=(kBoxOff-kBoxW*0.5f-18.0f)*scale;
    const float dx=x1-x0,dy=y1-y0;
    float enter=0.0f,leave=1.0f;
    auto clip=[&](float p,float q) {
        if(std::fabs(p)<1e-6f)return q>=0.0f;
        const float r=q/p;
        if(p<0.0f){if(r>leave)return false;enter=std::fmax(enter,r);}
        else{if(r<enter)return false;leave=std::fmin(leave,r);}
        return true;
    };
    if(!clip(-dx,x0-(width*0.5f-half)) || !clip(dx,width*0.5f+half-x0) ||
       !clip(-dy,y0) || !clip(dy,height*kFlightRegionBottom-y0))return;
    x1=x0+dx*leave;y1=y0+dy*leave;x0+=dx*enter;y0+=dy*enter;
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

// Mark the real seat locator before the player is within the stock prompt's short reach. Large carriers place
// this beside their outer hull/wing: an origin-centred hint would direct the player to the wrong place.
void EntranceMark(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,
                  const BoardingEntrance& e,Line* lines,int* at) noexcept {
    float x,y,depth;
    if(!Project(vp,e.at,width,height,&x,&y,&depth) || x<0.0f || x>width || y<0.0f || y>height)return;
    const float* colour=e.inReach ? kHud : kCyan;
    Arc(drawer,ctx,x,y,13.0f*s,0.0f,kTurn,2.0f*s,24,colour);
    Seg(drawer,ctx,x-20.0f*s,y,x-14.0f*s,y,2.0f*s,colour);
    Seg(drawer,ctx,x+14.0f*s,y,x+20.0f*s,y,2.0f*s,colour);
    const float tx=vec::Clamp(x,160.0f*s,width-160.0f*s),ty=vec::Clamp(y+30.0f*s,30.0f*s,height-30.0f*s);
    const int metres=static_cast<int>(std::ceil(e.distance));
    if(e.hail && e.coming)Label(text,lines,at,tx,ty,1,kLineScale,colour,Tr(Tx::boardingHailing),metres);
    else if(e.hail){wchar_t key[32];KeyName(Cfg().playerJetHailKey,key,32);Label(text,lines,at,tx,ty,1,kLineScale,colour,Tr(Tx::boardingHail),key,metres);}
    else if(e.inReach)Label(text,lines,at,tx,ty,1,kLineScale,colour,L"%ls",Tr(Tx::boardingReady));
    else Label(text,lines,at,tx,ty,1,kLineScale,colour,Tr(Tx::boardingEntry),metres);
}

// --- The loadout strip (the user, 2026-10-06: "切换挂载应该有图片显示，而非仅文字"): every store a cell with its picture
// (StoreGlyph: a silhouette from quads, hud_cue.h; the HUD has no texture of the game's to draw), its name and rounds
// under it, the picked one on a panel in a cyan frame; on a switch the picked store large over it for kSwitchMs
// (LoadoutBanner). For the plugin's jets and rotor craft (PlayerJetReadout) and a stock heli (StockHudReadout). ---
constexpr unsigned long long kSwitchMs=1500;   // a switch's banner (the store's, EDF6AutoTurret's aim mode's)
constexpr float kCellW=112.0f,kCellH=48.0f;   // px at 1080 lines: a strip's cell
constexpr float kRows3[]={-4.0f,0.0f,4.0f};   // a glyph's three rows (the pod's rockets, the gun's rounds)
struct LoadCell { hudcue::StoreIcon icon; wchar_t text[64]; const float* rgba; bool picked; };

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
    case StoreIcon::drone: // deployed aircraft, not a falling munition
        seg(-14.0f,0.0f,14.0f,0.0f,2.5f);tri(6.0f,0.0f,17.0f,0.0f,2.5f);
        tri(2.0f,0.0f,-8.0f,-8.0f,3.0f);tri(2.0f,0.0f,-8.0f,8.0f,3.0f);
        seg(-12.0f,-4.0f,-12.0f,4.0f,2.0f);break;
    case StoreIcon::energy: // emitter and parallel beam rays
        box(-15.0f,-5.0f,-8.0f,5.0f);
        for(const float r:kRows3)seg(-7.0f,r*3.0f,16.0f,r*3.0f,r==0.0f ? 2.0f : 1.0f);
        break;
    case StoreIcon::charge: // detonation action
        seg(-12.0f,-6.0f,12.0f,6.0f,1.6f);seg(-12.0f,6.0f,12.0f,-6.0f,1.6f);
        seg(0.0f,-9.0f,0.0f,9.0f,1.6f);seg(-16.0f,0.0f,16.0f,0.0f,1.6f);break;
    case StoreIcon::gun:   // the gun's rounds: three bullets
        for(const float r:kRows3){const float v=r*1.125f;box(-10.0f,v-1.4f,2.0f,v+1.4f);tri(2.0f,v,7.0f,v,1.4f);box(-12.0f,v-1.6f,-10.5f,v+1.6f);}
        break;
    }
}

// Every lower HUD group measures with its fitted scale, and records that scale on
// the lines handed back to the final DrawAll. Geometry and text stay in one space.
struct HudRegionText {
    Text copy{}; Text* text; Line* lines; int* at; int first; float ratio;
    HudRegionText(Text* source,float requested,float fitted,Line* l,int* n) noexcept
        : text(source),lines(l),at(n),first(*n),ratio(fitted/requested) {
        if(source) {
            if(!source->made && textOk && source->mgr)MakeText(*source);
            copy=*source;copy.s*=ratio;text=&copy;
        }
    }
    ~HudRegionText(){for(int i=first;i<*at;++i)lines[i].scale*=ratio;}
};
struct LoadoutGrid { int columns,rows; float cell,width,height; };
LoadoutGrid LoadoutGridOf(Text* text,float width,float s,const LoadCell* cells,int n) noexcept {
    n=n<8 ? n : 8;
    if(n<=0)return {};
    const float available=std::fmax(1.0f,width-32.0f*s);
    float cell=kCellW*s;
    for(int i=0;i<n;++i) {
        Line l{};Format(l,L"%ls",cells[i].text);l.scale=kLineScale*0.75f;
        if(text)MeasureAll(*text,&l,1);
        cell=std::fmax(cell,l.w+20.0f*s);
    }
    cell=std::fmin(cell,available);
    int columns=static_cast<int>(available/cell);
    columns=columns<1 ? 1 : columns>4 ? 4 : columns;
    if(columns>n)columns=n;
    const int rows=(n+columns-1)/columns;
    return {columns,rows,cell,cell*columns,(kCellH*rows+8.0f*(rows-1))*s};
}
struct LoadoutDock { float scale,y,footer,top,bannerBottom; };
LoadoutDock LoadoutDockOf(Text* text,float width,float height,float s,const LoadCell* cells,int n,int footers) noexcept {
    if(text && !text->made && textOk && text->mgr)MakeText(*text);
    float fitted=s;
    LoadoutGrid grid{};
    // The lower dock owns at most 36% of the viewport: cells, the switch banner
    // and all control rows. Long translated names determine columns before fit.
    for(int pass=0;pass<5;++pass) {
        Text local{};Text* t=text;
        if(text){local=*text;local.s*=fitted/s;t=&local;}
        grid=LoadoutGridOf(t,width,fitted,cells,n);
        const float need=grid.height+(24.0f+72.0f+24.0f*footers+8.0f)*fitted;
        if(need<=height*kLoadoutRegionShare)break;
        fitted*=height*kLoadoutRegionShare/need;
    }
    Text local{};Text* t=text;
    if(text){local=*text;local.s*=fitted/s;t=&local;}
    grid=LoadoutGridOf(t,width,fitted,cells,n);
    const float footer=height-(24.0f+24.0f*footers)*fitted;
    const float y=footer-8.0f*fitted-grid.height;
    return {fitted,y,footer,y-72.0f*fitted,y-8.0f*fitted};
}

// A measured grid, at most four columns, without a minimum width that can push
// the first/last cell off screen. Every row includes its own text and glyphs.
float LoadoutStrip(void* drawer,void* ctx,Text* text,float width,float y,float s,const LoadCell* cells,int n,Line* lines,int* at) noexcept {
    n=n<8 ? n : 8;
    const LoadoutGrid grid=LoadoutGridOf(text,width,s,cells,n);
    if(!grid.columns)return 0.0f;
    for(int i=0;i<n;++i) {
        const int row=i/grid.columns,col=i%grid.columns;
        const int left=n-row*grid.columns,columns=grid.columns<left ? grid.columns : left;
        const float x=(width-grid.cell*columns)*0.5f+col*grid.cell,cy=y+row*(kCellH+8.0f)*s;
        const float cx=x+grid.cell*0.5f,x1=x+grid.cell,h=kCellH*s,t=2.0f*s;
        const int line=*at;
        Label(text,lines,at,cx,cy+37.0f*s,1,kLineScale*0.75f,cells[i].rgba,L"%ls",cells[i].text);
        if(text && line<*at && lines[line].w>grid.cell-16.0f*s) {
            Line& l=lines[line];l.scale*=(grid.cell-16.0f*s)/l.w*0.97f;MeasureAll(*text,&l,1);
            l.x=cx-l.w*0.5f;l.y=cy+37.0f*s-l.h*0.5f;
        }
        const LoadCell& c=cells[i];
        if(c.picked) {
            Rect(drawer,ctx,x+2.0f*s,cy,x1-2.0f*s,cy+h,kPanel);
            Seg(drawer,ctx,x+2.0f*s,cy,x1-2.0f*s,cy,t,kCyan);Seg(drawer,ctx,x+2.0f*s,cy+h,x1-2.0f*s,cy+h,t,kCyan);
            Seg(drawer,ctx,x+2.0f*s,cy,x+2.0f*s,cy+h,t,kCyan);Seg(drawer,ctx,x1-2.0f*s,cy,x1-2.0f*s,cy+h,t,kCyan);
        }
        StoreGlyph(drawer,ctx,cx,cy+15.0f*s,1.4f*s,c.icon,c.rgba);
    }
    return grid.height;
}

// The picked store large, over the strip, for kSwitchMs after a switch: its picture twice the size, its name and rounds.
// The panel is as wide as its text needs (a weapon's word is up to twice as wide in Chinese or Japanese: the text ran out
// of a fixed 300 px), and it covers the HUD's text drawn under it so far (a pitch ladder's degrees showed through it).
void LoadoutBanner(void* drawer,void* ctx,Text* text,float width,float bottom,float s,const LoadCell& c,Line* lines,int* at) noexcept {
    const int first=*at;
    Label(text,lines,at,0.0f,0.0f,0,kTitleScale,kCyan,L"%ls",c.text);
    const float textX=116.0f*s,right=24.0f*s;
    if(text && *at>first && lines[first].w>width-32.0f*s-textX-right) {
        Line& l=lines[first];l.scale*=std::fmax(1.0f,width-32.0f*s-textX-right)/l.w*0.97f;MeasureAll(*text,&l,1);
    }
    const float textW=*at>first ? lines[first].w : 0.0f;
    const float w=std::fmax(300.0f*s,textX+textW+right),h=64.0f*s,x0=(width-w)*0.5f,y0=bottom-h;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,kCyan);
    StoreGlyph(drawer,ctx,x0+58.0f*s,y0+h*0.5f,2.6f*s,c.icon,kCyan);
    for(int i=0;i<first;++i) {   // what is under the panel
        Line& l=lines[i];
        if(l.x<x0+w && x0<l.x+l.w && l.y<y0+h && y0<l.y+l.h)l.text[0]=L'\0';
    }
    if(*at>first){lines[first].x=x0+textX;lines[first].y=y0+(h-lines[first].h)*0.5f;}
}

// The plugin aircraft's stores as cells: each one's picture (hud_cue.h StoreIconOf), its name and rounds; out of rounds
// dim; the picked one cyan.
int JetCells(const PlayerJetReadout& j,LoadCell* cells) noexcept {
    int n=0;
    for(int i=0;i<j.stores && i<kMostStores;++i) {
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
            // The lower fixed readouts and control rows own this space; projected pitch labels must not cover them.
            if(sight::ToScreen(vp,b,0.0f,width,height,&lx,&ly) && lx>0.0f && lx<width && ly>0.0f && ly<height*kFlightRegionBottom)
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
constexpr float kBarHalf=100.0f;   // px: the half length of the bars beside the boxes (a heli's height, a jet's lift)
void Boxes(void* drawer,void* ctx,Text* text,float width,float height,float s,float speed,const wchar_t* under,const float* underRgba,
           float clear,bool ground,float climb,Line* lines,int* at,bool heli=false) noexcept {
    const float cy=height*0.5f,w=kBoxW*s*0.5f,hh=kBoxH*s*0.5f,t=2.0f*s;
    const float left=width*0.5f-kBoxOff*s,right=width*0.5f+kBoxOff*s;
    const float boxes[2]={left,right};
    for(const float x:boxes) {
        Seg(drawer,ctx,x-w,cy-hh,x+w,cy-hh,t,kHud);Seg(drawer,ctx,x-w,cy+hh,x+w,cy+hh,t,kHud);
        Seg(drawer,ctx,x-w,cy-hh,x-w,cy+hh,t,kHud);Seg(drawer,ctx,x+w,cy-hh,x+w,cy+hh,t,kHud);
    }
    const float alt=std::fmax(-9999.0f,std::fmin(clear,99999.0f));
    Label(text,lines,at,left,cy,1,kTitleScale,kHud,L"%d",static_cast<int>(std::lround(speed*3.6f)));
    Label(text,lines,at,left,cy-hh-12.0f*s,1,kLineScale*0.8f,kHud,L"%ls",Tr(heli ? Tx::heliSpeedLabel : Tx::speedLabel));
    if(under && under[0])Label(text,lines,at,left,cy+kBoxRow*s,1,kLineScale,underRgba,L"%ls",under);
    Label(text,lines,at,right,cy,1,kTitleScale,kHud,L"%d",static_cast<int>(std::lround(alt)));
    Label(text,lines,at,right,cy-hh-12.0f*s,1,kLineScale*0.8f,kHud,L"%ls",Tr(ground ? (heli ? Tx::heliHeightLabel : Tx::altLabel) : Tx::altLabelNoGround));
    Label(text,lines,at,right,cy+kBoxRow*s,1,kLineScale,kHud,Tr(Tx::climbRate),static_cast<int>(std::lround(climb)));
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

// The target's lead mark: a cross in a circle at `at` (where the rounds meet it, hud.cpp GunSight / StockMark).
void LeadMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const float* at,const float* rgba) noexcept {
    float x,y;
    if(!sight::ToScreen(vp,at,1.0f,width,height,&x,&y))return;
    const float r=7.0f*s,t=2.0f*s;
    Arc(drawer,ctx,x,y,r,0.0f,kTurn,t,10,rgba);
    Seg(drawer,ctx,x-r,y-r,x+r,y+r,t,rgba);Seg(drawer,ctx,x-r,y+r,x+r,y-r,t,rgba);
}

// The guns' sight: a cross on the boresight (where the nose points: the guns' line), the pipper (a circle, a dot in it)
// where the rounds fired now will be at the target's range (else at the sight's own), the target's lead mark (a cross
// in a circle, dim out of the rounds' reach) and the range as an arc round the pipper (from its top, the share of the
// reach). Pipper on the lead mark: the rounds meet the target.
void GunSight(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetSymbols& y) noexcept {
    if(!y.gun)return;
    float x,yy;
    Boresight(drawer,ctx,vp,width,height,s,y.nose);
    if(Pipper(drawer,ctx,vp,width,height,s,y.pipper,kHud,&x,&yy) && y.lead && y.gunRange>0.0f) {
        const float share=vec::Clamp(y.leadRange/y.gunRange,0.0f,1.0f);
        const int sides=static_cast<int>(std::ceil(share*24.0f));
        if(sides>0)Arc(drawer,ctx,x,yy,(kPipper+5.0f)*s,-0.25f*kTurn,share*kTurn,3.0f*s,sides,y.leadInRange ? kHud : kHudDim);
    }
    if(y.lead)LeadMark(drawer,ctx,vp,width,height,s,y.leadAt,y.leadInRange ? kHud : kHudDim);
}

// A stock helicopter's weapons' sight (helisight.cpp; ini PlayerHeliGunSight). The gun (the primary trigger's): the
// jet sight's boresight and pipper, the pipper where a round fired now first hits the ground (dim: none in its life,
// it is where the round ends), its distance in metres to its right. The other weapon (the secondary button's), each
// in its own shape so neither is taken for the gun's pipper: the missile's lock as the jets' (LockAt: a yellow square
// closing in, a red diamond locked) with MSL and its distance, or with no lock the seeker gate (reticle.h Gate) round its
// boresight with MSL and the range it locks within; the rockets' mark a hollow diamond where their path (flown as the
// game flies them: vhud.h RoundLands) meets the map with its label (RKT, GREN...) and distance (none met: a dim one on
// their boresight).
// A reticle's sketch (reticle.h) in the HUD's quads and text: green, dim green, amber.
const float* InkOf(reticle::Ink ink) noexcept { return ink==reticle::Ink::dim ? kHudDim : ink==reticle::Ink::hot ? kAmber : kHud; }
void Render(void* drawer,void* ctx,Text* text,float s,const reticle::Sketch& k,Line* lines,int* at) noexcept {
    for(int i=0;i<k.strokes;++i){const auto& l=k.stroke[i];Seg(drawer,ctx,l.x0,l.y0,l.x1,l.y1,l.w,InkOf(l.ink));}
    for(int i=0;i<k.circles;++i) {
        const auto& c=k.circle[i];
        const int sides=static_cast<int>(vec::Clamp(c.r/(4.0f*s),16.0f,64.0f));
        Arc(drawer,ctx,c.x,c.y,c.r,0.0f,kTurn,c.w,sides,InkOf(c.ink));
    }
    for(int i=0;i<k.dots;++i){const auto& d=k.dot[i];Rect(drawer,ctx,d.x-d.r,d.y-d.r,d.x+d.r,d.y+d.r,InkOf(d.ink));}
    for(int i=0;i<k.notes;++i){const auto& n=k.note[i];if(n.text[0])Label(text,lines,at,n.x,n.y,n.align,n.scale,InkOf(n.ink),L"%ls",n.text);}
}
// The pixels a unit of tan off `bore` spans on the screen from its point (cx, cy): the bore turned a little to its right,
// projected (the view's own projection: the sight's magnification is in it). 0: not known.
float FocalAt(const float* vp,const float* bore,float width,float height,float cx,float cy) noexcept {
    constexpr float kProbe=0.01f;   // rad
    float turned[3],mx,my;
    if(!reticle::Turned(bore,kProbe,turned) || !sight::ToScreen(vp,turned,0.0f,width,height,&mx,&my))return 0.0f;
    return std::sqrt((mx-cx)*(mx-cx)+(my-cy)*(my-cy))/std::tan(kProbe);
}
// A gun's range ladder (gunsight.h) projected onto the screen as reticle.h's ticks.
void LadderMarks(const float* vp,float width,float height,const gunsight::Ladder& l,reticle::Aim* a) noexcept {
    a->step=l.step;a->ticks=0;
    for(int k=0;k<l.ticks && k<gunsight::kMostTicks;++k) {
        float x,y;
        if(!sight::ToScreen(vp,l.at[k],1.0f,width,height,&x,&y))continue;
        a->tick[a->ticks++]=reticle::Mark{x,y,l.range[k]};
    }
}
// A gun's range ladder under its aim point (cx, top): reticle.h Ladder.
void LadderTicks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const gunsight::Ladder& l,
                 float cx,float top,Line* lines,int* at) noexcept {
    reticle::Aim a{};
    LadderMarks(vp,width,height,l,&a);
    reticle::Sketch k{};
    reticle::Ladder(k,reticle::View{cx,top,0.0f,s,kLineScale},a,cx,top,reticle::kLadderTick);
    Render(drawer,ctx,text,s,k,lines,at);
}
// A guided round's seeker gate at its boresight (reticle.h Gate), `line` under it (its name, the range it locks within).
void SeekerGate(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const float* bore,const wchar_t* line,
                Line* lines,int* at) noexcept {
    float x,y;
    if(!sight::ToScreen(vp,bore,0.0f,width,height,&x,&y))return;
    reticle::Sketch k{};
    reticle::Gate(k,reticle::View{x,y,0.0f,s,kLineScale});
    Render(drawer,ctx,text,s,k,lines,at);
    Label(text,lines,at,x,y+(reticle::kGate+14.0f)*s,1,kLineScale*0.85f,kHudDim,L"%ls",line);
}

constexpr float kRocketMark=11.0f;   // px at 1080 lines
void RocketDiamond(void* drawer,void* ctx,float x,float y,float s,const float* rgba) noexcept {
    const float r=kRocketMark*s,t=2.0f*s;
    Seg(drawer,ctx,x,y-r,x+r,y,t,rgba);Seg(drawer,ctx,x+r,y,x,y+r,t,rgba);
    Seg(drawer,ctx,x,y+r,x-r,y,t,rgba);Seg(drawer,ctx,x-r,y,x,y-r,t,rgba);
}
// Each marker is one actual barrel's predicted terrain intersection or finite path end.
// An open dash is never presented as an enemy hit confirmation.
void PhysicalPaths(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,
                   const roundaim::Impact* paths,int count,bool label,Line* lines,int* at) noexcept {
    for(int i=0;i<count && i<roundaim::kSightPaths;++i) {
        const auto& p=paths[i];float x,y;
        if(!sight::ToScreen(vp,p.at,1.0f,width,height,&x,&y))continue;
        if(p.hit)ImpactCross(drawer,ctx,vp,width,height,s,p.at,&x,&y);
        else {
            Seg(drawer,ctx,x-12*s,y,x-5*s,y,2*s,kHudDim);
            Seg(drawer,ctx,x+5*s,y,x+12*s,y,2*s,kHudDim);
        }
        if(label && i==0)Label(text,lines,at,x,y+26*s,1,kLineScale*0.75f,p.hit ? kYellow : kHudDim,
                              Tr(p.hit ? Tx::predictedGround : Tx::noGroundHit),p.range,p.seconds);
    }
}
void HeliGunSight(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const HeliSightReadout& h,
                  Line* lines,int* at) noexcept {
    float x,y;
    const float note=kLineScale*0.85f;
    if(h.gun) {
        Boresight(drawer,ctx,vp,width,height,s,h.bore);
        float bx,by;
        if(!h.physicalOnly && h.ladder.ticks>0 && sight::ToScreen(vp,h.bore,0.0f,width,height,&bx,&by))
            LadderTicks(drawer,ctx,text,vp,width,height,s,h.ladder,bx,by+12.0f*s,lines,at);
        PhysicalPaths(drawer,ctx,text,vp,width,height,s,h.path,h.paths,true,lines,at);
    }
    if(h.arm==HeliArm::missile) {
        if(LockAt(drawer,ctx,vp,width,height,s,h.lock,h.armAt,h.lockProgress,&x,&y))
            Label(text,lines,at,x,y+44.0f*s,1,note,h.lock==2 ? kRed : kYellow,Tr(Tx::missileRange),static_cast<int>(std::lround(h.armRange)));
        else if(!h.lock) {
            wchar_t line[48];
            _snwprintf_s(line,_countof(line),_TRUNCATE,Tr(Tx::missileRange),static_cast<int>(std::lround(h.lockRange)));
            SeekerGate(drawer,ctx,text,vp,width,height,s,h.armBore,line,lines,at);
        }
    } else if(h.arm==HeliArm::rockets) {
        wchar_t arm[24];
        hudtext::WordTo(h.armLabel ? h.armLabel : "RKT",arm,_countof(arm));
        if(h.armHit && sight::ToScreen(vp,h.armAt,1.0f,width,height,&x,&y)) {
            RocketDiamond(drawer,ctx,x,y,s,kHud);
            Label(text,lines,at,x,y+(kRocketMark+12.0f)*s,1,note,kHud,L"%ls %d m",arm,static_cast<int>(std::lround(h.armRange)));
        } else if(sight::ToScreen(vp,h.armBore,0.0f,width,height,&x,&y)) {
            RocketDiamond(drawer,ctx,x,y,s,kHudDim);
            Label(text,lines,at,x,y+(kRocketMark+12.0f)*s,1,note,kHudDim,L"%ls",arm);
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
    if(Project(vp,p,width,height,&x,&y,&depth))Label(text,lines,at,x+22.0f*s,y,0,kLineScale*0.75f,kCyan,L"%ls",Tr(Tx::fly));
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
              ULONGLONG now,float side,Line* lines,int* at,float bottom=-1.0f) noexcept {
    const float r=kRwrR*s,t=2.0f*s;
    float cx,cy;
    RwrCentre(width,height,s,side,&cx,&cy);
    if(bottom>=0.0f) {
        cx=vec::Clamp(cx,(kRwrR+24.0f)*s,width-(kRwrR+24.0f)*s);
        cy=bottom-(kRwrR+10.0f)*s;
    }
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
    if(launch)Label(text,lines,at,cx,cy-r-22.0f*s,1,kLineScale,blink ? kRed : kWhite,L"%ls",Tr(Tx::launch));
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
    Label(text,lines,at,mx,my+r+22.0f*s,1,kTitleScale,c,L"%ls",Tr(Tx::pullUp));
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
    Label(text,lines,at,x,cy-half-14.0f*s,1,kLineScale*0.7f,kHudDim,L"%ls",Tr(Tx::lift));
    if(!stall)return;
    float fx=width*0.5f,fy=height*0.5f,px,py;
    if(y.moving && sight::ToScreen(vp,y.dir,0.0f,width,height,&px,&py)){fx=px;fy=py;}
    const float* box=blink ? kRed : kWhite;
    const int first=*at;
    Label(text,lines,at,fx+46.0f*s,fy,0,kTitleScale,box,L"%ls",Tr(Tx::stall));
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
const Tx kWarnText[kWarnCount]={Tx::warnPullUp,Tx::warnMissile,Tx::warnStall,Tx::warnGear,Tx::warnTerrain,Tx::warnSinkRate,Tx::warnLock,
                                Tx::warnGearSpeed,Tx::warnWeightOnWheels,Tx::warnLowFuel,Tx::warnArea};
void Annunciator(void* drawer,void* ctx,Text* text,float width,float height,float s,const Warnings& w,Line* lines,int* at) noexcept {
    if(!w.on)return;
    const ULONGLONG now=GetTickCount64();
    const bool blink=(now/125)%2==0;
    int lit[kWarnCount],n=0;
    bool warning=false;
    for(int k=0;k<kWarnCount;++k)if(w.on>>k&1u){lit[n++]=k;warning=warning || IsWarning(k);}
    if(*at+n+1>kMaxLines)return;
    Line* const tile=&lines[*at];
    Format(tile[0],L"%ls",Tr(warning ? Tx::masterWarn : Tx::masterCaution));
    tile[0].rgba=kInk;
    for(int i=0;i<n;++i) {
        const int k=lit[i];
        const bool launch=k==kWarnMissile && w.launchAt && now-w.launchAt<kLaunchMs;
        Format(tile[1+i],L"%ls",Tr(launch ? Tx::missileLaunch : kWarnText[k]));
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
    LoadCell cells[kMostStores];
    const int n=JetCells(j,cells);
    const LoadoutDock dock=LoadoutDockOf(text,width,height,s,cells,n,1);
    HudRegionText region(text,s,dock.scale,lines,at);text=region.text;s=dock.scale;
    Line& warn=lines[(*at)++];
    Line& arms=lines[(*at)++];
    wchar_t cue[64],stores[128],fuel[32];
    bool rotate=false;
    TakeoffCue(j,cue,_countof(cue),&rotate);
    StoresText(stores,_countof(stores),j,false);
    FuelText(fuel,_countof(fuel),j.fuel);
    Format(warn,L"%ls",cue);
    Format(arms,Tr(Tx::strip),static_cast<int>(std::lround(j.throttle*100.0f)),fuel[0] ? L"  " : L"",fuel,stores);
    warn.scale=kTitleScale;warn.rgba=rotate ? ((GetTickCount64()/125)%2==0 ? kGreen : kYellow) : kHud;
    arms.scale=kLineScale;arms.rgba=j.bomb ? kYellow : kHud;
    warn.w=warn.h=arms.w=arms.h=0.0f;
    if(text){MeasureAll(*text,&warn,1);MeasureAll(*text,&arms,1);}
    const float armsH=arms.h>0.0f ? arms.h : 18.0f*s,warnH=warn.h>0.0f ? warn.h : 24.0f*s;
    arms.x=(width-arms.w)*0.5f;arms.y=dock.top-armsH-8.0f*s;
    warn.x=(width-warn.w)*0.5f;warn.y=arms.y-warnH-6.0f*s;
    LoadoutStrip(drawer,ctx,text,width,dock.y,s,cells,n,lines,at);
    if(*at<kMaxLines) {
        Line& keys=lines[(*at)++];JetControls(keys,j);
        ControlRow(text,width,dock.footer,s,keys);
    }
    if(switched && j.store>=0 && j.store<n)LoadoutBanner(drawer,ctx,text,width,dock.bannerBottom,s,cells[j.store],lines,at);
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
    const float fitted=std::fmin(s,width/(2.0f*(kBoxOff+kBoxW*0.5f+60.0f+2.0f*kRwrR+24.0f)));
    HudRegionText region(text,s,fitted,lines,at);text=region.text;s=fitted;
    if(!f.landed)Ladder(drawer,ctx,text,vp,width,height,s,y,lines,at);
    if(y.moving && !f.landed)FlightPath(drawer,ctx,vp,width,height,s,y);
    if(!f.landed && (f.speed<kDriftShown || (f.ground && f.clear<kLowHover)))DriftMark(drawer,ctx,width,height,s,f,y);
    HeadingTape(drawer,ctx,text,width,height,s,y,lines,at);
    wchar_t set[32]=L"";
    if(f.aiming)std::swprintf(set,32,Tr(f.setSpeed==0.0f ? Tx::setHover : Tx::setSpeed),static_cast<int>(std::lround(f.setSpeed*3.6f)));
    Boxes(drawer,ctx,text,width,height,s,f.speed,set,kCyan,f.clear,f.ground,f.climb,lines,at,true);
    HeightBar(drawer,ctx,width,height,s,f);
    GroundCue(drawer,ctx,text,vp,width,height,s,y,f.gpws,f.impactIn,lines,at);
    if(Cfg().playerJetThreatHud) {
        const float bottom=height*kFlightRegionBottom;
        float cx,cy;RwrCentre(width,height,s,-1.0f,&cx,&cy);
        cx=vec::Clamp(cx,(kRwrR+24.0f)*s,width-(kRwrR+24.0f)*s);cy=bottom-(kRwrR+10.0f)*s;
        Label(text,lines,at,cx,cy+kRwrR*s+18.0f*s,1,kLineScale*0.7f,kHudDim,L"%ls",Tr(Tx::heliThreatScope));
        RwrScope(drawer,ctx,text,width,height,s,y,launchAt,GetTickCount64(),-1.0f,lines,at,bottom);
        ThreatMarks(drawer,ctx,vp,width,height,s,y);
    }
}

// The lines in place of the old panel (see above; its warnings are the annunciator's and the HUD's symbols): on top on
// the ground the takeoff cue (the rotor's share of the lift-off speed; LIFT OK blinking from there); under it the speed
// set and the height held (the mouse-aim flight); at the bottom `stores` (a rotor craft's; nullptr: none) on a line of
// their own (with the speed and ALT HOLD on one line, 5-6 stores ran past a line's 128 characters: the last cut off).
// The fuel (`fuel`, the stock gauge's FUEL panel gone: stockgauge.cpp) goes on the speed set's line. The stores' cells
// (`cells`, `n`) are the loadout strip under it (`picked` the picked one's, -1 none; `switched`: it large over the
// lines too, LoadoutBanner).
void HeliStrip(void* drawer,void* ctx,Text* text,float width,float height,float s,const HeliFlight& f,const FuelReading& fuel,
               const wchar_t* stores,const LoadCell* cells,int n,int picked,bool switched,Line* lines,int* at,const Line* controls=nullptr) noexcept {
    if(*at+4>kMaxLines)return;
    const int footers=(f.keys && f.aiming ? 1 : 0)+(controls && controls->text[0] ? 1 : 0);
    const LoadoutDock dock=LoadoutDockOf(text,width,height,s,cells,n,footers);
    HudRegionText region(text,s,dock.scale,lines,at);text=region.text;s=dock.scale;
    Line& warn=lines[(*at)++];
    Line& info=lines[(*at)++];
    Line& arms=lines[(*at)++];
    Line& help=lines[(*at)++];
    // The keys (the user, 2026-10-06: "I do not know how to climb or descend, except Space"): the mouse-aim flight on the
    // keyboard and mouse spells its controls under the strip; the descent is the brake key (ini PlayerJetBrakeKey).
    Format(help,L"");
    if(f.keys && f.aiming) {
        wchar_t down[32];
        KeyName(Cfg().playerJetBrakeKey,down,32);
        if(f.landed)Format(help,L"%ls",Tr(f.collective ? Tx::heliKeysInstructorLanded : Tx::heliKeysLanded));
        else Format(help,Tr(f.collective ? Tx::heliKeysInstructor : Tx::heliKeysAir),down);
    }
    const bool blink=(GetTickCount64()/125)%2==0,lift=f.landed && f.hover>0.0f && f.rotor>0.01f,liftOk=lift && f.rotor>=f.hover;
    if(liftOk){Format(warn,L"%ls",Tr(Tx::liftOk));warn.rgba=blink ? kGreen : kYellow;}
    else if(lift){Format(warn,Tr(Tx::rotorShare),static_cast<int>(std::lround(100.0f*f.rotor/f.hover)));warn.rgba=kCyan;}
    else{Format(warn,L"");warn.rgba=kHud;}
    Format(info,L"");
    if(f.aiming)Append(info,Tr(f.setSpeed==0.0f ? Tx::speedSetHover : Tx::speedSet),static_cast<int>(std::lround(f.setSpeed*3.6f)));
    if(f.power>=0.0f && !f.landed)Append(info,Tr(Tx::heliPower),info.text[0] ? L"    " : L"",static_cast<int>(std::lround(f.power*100.0f)));
    if(f.holding)Append(info,Tr(Tx::altHold),info.text[0] ? L"    " : L"");
    wchar_t tank[32];
    FuelText(tank,_countof(tank),fuel);
    if(tank[0])Append(info,L"%ls%ls",info.text[0] ? L"    " : L"",tank);
    Format(arms,L"%ls",stores ? stores : L"");
    warn.scale=kTitleScale;info.scale=arms.scale=kLineScale;info.rgba=arms.rgba=kHud;
    help.scale=kLineScale*0.85f;help.rgba=kCyan;
    warn.w=warn.h=info.w=info.h=arms.w=arms.h=help.w=help.h=0.0f;
    if(text){MeasureAll(*text,&warn,1);MeasureAll(*text,&info,1);MeasureAll(*text,&arms,1);MeasureAll(*text,&help,1);}
    ControlRow(text,width,0.0f,s,help);
    const float lineH=18.0f*s,armsH=arms.h>0.0f ? arms.h : (arms.text[0] ? lineH : 0.0f);
    const float infoH=info.h>0.0f ? info.h : lineH,warnH=warn.h>0.0f ? warn.h : 24.0f*s;
    arms.x=(width-arms.w)*0.5f;arms.y=dock.top-armsH-8.0f*s;
    info.x=(width-info.w)*0.5f;info.y=arms.y-infoH-(arms.text[0] ? 4.0f*s : 0.0f);
    warn.x=(width-warn.w)*0.5f;warn.y=info.y-warnH-6.0f*s;
    LoadoutStrip(drawer,ctx,text,width,dock.y,s,cells,n,lines,at);
    help.y=dock.footer;   // the keys under the loadout strip, not over it
    if(controls && controls->text[0] && *at<kMaxLines) {
        Line& keys=lines[(*at)++];keys=*controls;
        ControlRow(text,width,help.y+(help.text[0] ? (help.h>0 ? help.h : 18.0f*s)+6.0f*s : 0.0f),s,keys);
    }
    if(switched && picked>=0 && picked<n)LoadoutBanner(drawer,ctx,text,width,dock.bannerBottom,s,cells[picked],lines,at);
}

// The landing gear (gear.cpp GearHudLatest; the jets with gear only), at the screen's right over the cockpit's line: its
// three lights as a real gear panel lays them out (the nose's over the two mains'): green down and locked, amber in
// transit, out (an empty frame) up and locked; under them DOWN / TRANSIT / UP and the key. Warnings over it: GEAR! (red,
// blinking) low and slow with it not down, GEAR SPEED (amber) over the gear's limit with it not up, WEIGHT ON WHEELS
// (amber) an up command refused on the ground; with the annunciator up (`annunciated`) those are its tiles instead.
void GearPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,bool annunciated,Line* lines,int* at,ULONGLONG now) noexcept {
    GearHud g{};
    if(!GearHudLatest(&g) || now-g.tick>kFreshMs || *at+2>kMaxLines)return;
    bool down=true,up=true;
    for(int i=0;i<kGearLegs;++i){down=down && g.at[i]<=0.0f;up=up && g.at[i]>=1.0f;}
    // The lines first: the panel is as wide as they need (WEIGHT ON WHEELS, or a language's longer words, ran past it).
    Line& state=lines[(*at)++];
    wchar_t key[32]=L"L3";
    if(g.keys)KeyName(Cfg().playerJetGearKey,key,32);
    else if(Cfg().playerJetGearButton!=0x40)std::swprintf(key,32,Tr(Tx::padButton),Cfg().playerJetGearButton);
    Format(state,Tr(Tx::gearState),Tr(down ? Tx::gearDown : up ? Tx::gearUp : Tx::gearTransit),key);
    state.scale=kLineScale*0.85f;state.rgba=down ? kGreen : up ? kWhite : kAmber;
    state.w=state.h=0.0f;
    if(text)MeasureAll(*text,&state,1);
    const wchar_t* const what=annunciated ? nullptr : g.warn ? Tr(Tx::gearBang) : g.overspeed ? Tr(Tx::warnGearSpeed) :
                              g.blocked ? Tr(Tx::warnWeightOnWheels) : nullptr;
    Line* warn=nullptr;
    if(what) {
        warn=&lines[(*at)++];
        Format(*warn,L"%ls",what);
        warn->scale=kLineScale;warn->rgba=g.warn ? ((now/125)%2==0 ? kRed : kWhite) : kAmber;
        warn->w=warn->h=0.0f;
        if(text)MeasureAll(*text,warn,1);
    }
    const float box=24.0f*s,gap=10.0f*s,pad=10.0f*s,h=2.0f*box+gap+2.0f*pad+60.0f*s;
    const float w=std::fmax(3.0f*box+4.0f*gap+2.0f*pad,std::fmax(state.w,warn ? warn->w : 0.0f)+2.0f*pad);
    const float x0=width-w-40.0f*s,y0=height*0.80f-h;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,kCyan);
    const float cx=x0+w*0.5f,top=y0+pad;
    const float lx[kGearLegs]={cx-box*0.5f,cx-box*1.5f-gap,cx+box*0.5f+gap};   // nose; left / right main (x>0 is left)
    const float ly[kGearLegs]={top,top+box+gap,top+box+gap};
    for(int i=0;i<kGearLegs;++i) {
        const float t=2.0f*s;
        Rect(drawer,ctx,lx[i],ly[i],lx[i]+box,ly[i]+box,kBarEdge);
        if(g.at[i]<=0.0f)Rect(drawer,ctx,lx[i]+t,ly[i]+t,lx[i]+box-t,ly[i]+box-t,kGreen);
        else if(g.at[i]<1.0f)Rect(drawer,ctx,lx[i]+t,ly[i]+t,lx[i]+box-t,ly[i]+box-t,kAmber);
        else Rect(drawer,ctx,lx[i]+t,ly[i]+t,lx[i]+box-t,ly[i]+box-t,kBarBack);
    }
    state.x=cx-state.w*0.5f;state.y=top+2.0f*box+gap+8.0f*s;
    if(warn){warn->x=cx-warn->w*0.5f;warn->y=state.y+(state.h>0.0f ? state.h : 18.0f*s)+4.0f*s;}
}

// The player's helicopter on the ground (HeliCue, the user 2026-10-05): its rotor spinning up to the speed whose lift
// holds it, then 'LIFT OK: TAKE OFF' blinking: from there the collective lifts it off.
void HeliPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,const HeliCue& c,Line* lines,int* at) noexcept {
    if(*at>=kMaxLines || !(c.rotor>0.01f) || !(c.hover>0.0f))return;
    Line& l=lines[(*at)++];
    const bool ok=c.rotor>=c.hover;
    const int pct=static_cast<int>(std::lround(100.0f*c.rotor/c.hover));
    if(ok)Format(l,Tr(Tx::rotorLiftOk),pct);
    else Format(l,Tr(Tx::rotorShare),pct);
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
// The drill's state word (nullptr: none): overheated, launched (out / on its way back), or biting.
const wchar_t* DrillState(const DrillCue& c) noexcept {
    if(c.overheated)return Tr(Tx::overheat);
    if(c.flying)return Tr(c.returning ? Tx::drillReturning : Tx::drillLaunched);
    return c.touching && c.rpm>0.0f ? Tr(Tx::drilling) : nullptr;
}
void DrillLaunchHint(Line& line,const DrillCue& c) noexcept {
    if(!Cfg().drillLaunch)return;
    wchar_t binding[64];
    if(c.keys)KeyName(Cfg().drillLaunchKey,binding,_countof(binding));
    else SeatButtonName(Cfg().drillLaunchButton,binding,_countof(binding));
    Append(line,L"    ");Append(line,Tr(Tx::drillLaunchHint),binding);
}
void DrillPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,const DrillCue& c,Line* lines,int* at) noexcept {
    if(*at>=kMaxLines || !(c.maxRpm>0.0f))return;
    Line& l=lines[(*at)++];
    const float share=Unit(c.rpm/c.maxRpm),heat=Unit(c.heat);
    const bool top=share>=0.99f;
    wchar_t state[32]=L"";
    if(const wchar_t* const word=DrillState(c))std::swprintf(state,32,L"    %ls",word);
    Format(l,Tr(Tx::drillLine),static_cast<int>(std::lround(c.rpm)),static_cast<int>(std::lround(heat*100.0f)),state);
    DrillLaunchHint(l,c);
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

// --- The player's Sazabi (crew.h SazabiCue, published by sazabi.cpp; README 沙扎比): its own HUD (the user, 2026-10-07:
// "我们自制的hud … 需要做的都要做"), drawn while the player drives it (the stock vehicle weapon gauge given way:
// HudPublish's SetStockGaugeCover). Positions in design px (1080 lines; times the HUD's scale `s`) from the screen's
// centre C:
// - The reticle (SazabiReticle): a pink ring, four ticks and a dot at C when the camera is the Sazabi's own
//   (cue.centred: the centre IS the aim), else where the aim point projects; red, thicker and ringed once the missiles'
//   lock is complete. Under it (+44) the range to what the centre's ray meets ("412 m"; "--- m" when it meets nothing).
// - Within kSazabiKeepOut of C nothing else: the flight's gauges stand round it. Left (SazabiThrusterArc): the
//   thrusters' charge as an arc of radius kSazabiArcR over +-kSazabiArcHalf round the left, filled from its lower end
//   (blue; amber under kSazabiLowThrust; red on a red track overheated; brighter with a glow while boosting), its share
//   and its name (blinking OVERHEAT overheated) right-aligned kSazabiSide left of C. Right (SazabiAltSpeed): a dim
//   bracket arc mirroring it, the altitude and the speed (km/h) left-aligned kSazabiSide right of C, a climb arrow after
//   the altitude past kSazabiClimbShown m/s.
// - Under right (from (C+kSazabiSpecialX, C+kSazabiSpecialY)): the special weapon the secondary fires (SazabiSpecial) on a
//   panel with a pink edge: its name (the missiles' count), its gauge (the next salvo, the lock's progress under it; the
//   funnels' pips, filled docked and hollow out, the next launch under them; the cannon's charge in pink, else its
//   cooldown), and the switch's bindings (ini SazabiSwitchKey / SazabiSwitchButton).
// - Lower left (SazabiPanel; its left kSazabiLeft left of C, its bottom at 0.80 H; held on a narrow viewport, and moved
//   down to the screen's foot when the flight's gauges would meet it): the name, the state tags lit on that row (GUARD,
//   AIR, TOMAHAWK, LOCK, OVERHEAT), then a row per system, its name at the left and its gauge at the right: the
//   thrusters, the beam rifle's next shot, the shield missiles (their count, the next salvo, the lock under it), the mega
//   particle cannon (pink charging, else its cooldown), the funnels' pips; the selected special's row marked (a pink
//   band, a pointer). A gauge full is green: ready.
// - The missiles' lock on its target as the jets' (LockAt: a yellow square closing in while it locks, the red diamond).
alignas(16) const float kSazabiRed[4]={1.0f,0.3f,0.35f,1.0f};
alignas(16) const float kSazabiPink[4]={1.0f,0.45f,0.75f,1.0f};
alignas(16) const float kSazabiLocked[4]={1.0f,0.18f,0.22f,1.0f};
alignas(16) const float kSazabiFrame[4]={0.8f,0.9f,1.0f,0.4f};      // the right bracket, the hints
alignas(16) const float kSazabiTrack[4]={0.0f,0.0f,0.0f,0.5f};      // an arc gauge's empty track
alignas(16) const float kSazabiHotTrack[4]={0.6f,0.05f,0.05f,0.55f};  // the thrusters' track overheated
alignas(16) const float kSazabiThrust[4]={0.3f,0.72f,1.0f,0.85f};
alignas(16) const float kSazabiBoost[4]={0.62f,0.93f,1.0f,1.0f};
alignas(16) const float kSazabiPick[4]={1.0f,0.45f,0.75f,0.16f};    // the selected special's band
alignas(16) const float kSazabiHint[4]={0.85f,0.9f,0.95f,0.8f};
constexpr float kSazabiLowThrust=0.25f,kSazabiLeft=880.0f,kSazabiKeepOut=120.0f,kSazabiArcR=160.0f,kSazabiArcW=8.0f;
constexpr float kSazabiArcHalf=36.0f*kDeg,kSazabiSide=178.0f,kSazabiClimbShown=2.0f;
constexpr float kSazabiSpecialX=140.0f,kSazabiSpecialY=108.0f;
constexpr float kSazabiFlightReach=330.0f;   // how far left of C the flight's gauges may reach (their text, in any language)
constexpr float kSazabiFlightFoot=108.0f;    // and how far under C (the thrusters' arc's lower end, its edge)
constexpr int kSazabiFunnels=6,kSazabiRows=5,kSazabiTags=5,kSazabiSpecials=3;
// A row's gauge: a bar of `share` in `fill` (or the funnels' pips: `pips` of them in the pack), and with `sub` >= 0 a
// thin bar of `sub` in `subFill` under it.
struct SazabiGauge { float share; const float* fill; int pips; float sub; const float* subFill; };
const float* ReadyColour(float share) noexcept { return share>=1.0f ? kGreen : kTeal; }
bool SazabiLockDone(const SazabiCue& c) noexcept { return c.hasLock && c.missileLock>=1.0f; }
int SazabiSpecialOf(const SazabiCue& c) noexcept { return c.special<0 ? 0 : c.special>=kSazabiSpecials ? kSazabiSpecials-1 : c.special; }
int SazabiPack(const SazabiCue& c) noexcept {   // the funnels in the pack
    return kSazabiFunnels-(c.funnelsOut<0 ? 0 : c.funnelsOut>kSazabiFunnels ? kSazabiFunnels : c.funnelsOut);
}
// Where the reticle stands (see the top): the screen's centre on the Sazabi's camera, else the aim point projected.
bool SazabiAimAt(const float* vp,float width,float height,const SazabiCue& c,float* x,float* y) noexcept {
    if(c.centred){*x=width*0.5f;*y=height*0.5f;return true;}
    float depth;
    return c.hasAim && Project(vp,c.aim,width,height,x,y,&depth);
}
void SazabiReticle(void* drawer,void* ctx,const float* vp,float width,float height,float s,const SazabiCue& c) noexcept {
    float x,y;
    if(!SazabiAimAt(vp,width,height,c,&x,&y))return;
    const bool locked=SazabiLockDone(c);
    const float* const ink=locked ? kSazabiLocked : kSazabiPink;
    const float r=18.0f*s,t=(locked ? 3.0f : 2.0f)*s,in=r+3.0f*s,out=in+(locked ? 10.0f : 7.0f)*s,dot=(locked ? 3.0f : 2.0f)*s;
    Arc(drawer,ctx,x,y,r,0.0f,kTurn,t,32,ink);
    Rect(drawer,ctx,x-out,y-t*0.5f,x-in,y+t*0.5f,ink);Rect(drawer,ctx,x+in,y-t*0.5f,x+out,y+t*0.5f,ink);
    Rect(drawer,ctx,x-t*0.5f,y-out,x+t*0.5f,y-in,ink);Rect(drawer,ctx,x-t*0.5f,y+in,x+t*0.5f,y+out,ink);
    Rect(drawer,ctx,x-dot,y-dot,x+dot,y+dot,ink);
    if(locked)Arc(drawer,ctx,x,y,out+4.0f*s,0.0f,kTurn,1.5f*s,40,ink);
}
// The aim assist's enemy (cue.hasAssist, sazabi_assist.h): four pink corner brackets round its lock point, the shots'
// point; held by the lock-on (cue.lockOn) they are red, thicker and tighter, with a dot on the point.
void SazabiAssistMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const SazabiCue& c) noexcept {
    float x,y,depth;
    if(!c.hasAssist || !Project(vp,c.assist,width,height,&x,&y,&depth))return;
    const float* const ink=c.lockOn ? kSazabiLocked : kSazabiPink;
    const float h=(c.lockOn ? 20.0f : 26.0f)*s,arm=(c.lockOn ? 11.0f : 9.0f)*s,t=(c.lockOn ? 3.0f : 2.0f)*s;
    for(int i=0;i<4;++i) {
        const float sx=i&1 ? 1.0f : -1.0f,sy=i&2 ? 1.0f : -1.0f,cx=x+sx*h,cy=y+sy*h;
        Rect(drawer,ctx,std::fmin(cx,cx-sx*arm),cy-t*0.5f,std::fmax(cx,cx-sx*arm),cy+t*0.5f,ink);
        Rect(drawer,ctx,cx-t*0.5f,std::fmin(cy,cy-sy*arm),cx+t*0.5f,std::fmax(cy,cy-sy*arm),ink);
    }
    if(c.lockOn)Rect(drawer,ctx,x-2.5f*s,y-2.5f*s,x+2.5f*s,y+2.5f*s,ink);
}
// The range under the reticle (see the top).
void SazabiRange(Text* text,const float* vp,float width,float height,float s,const SazabiCue& c,Line* lines,int* at) noexcept {
    float x,y;
    if(!SazabiAimAt(vp,width,height,c,&x,&y))return;
    const float* const ink=SazabiLockDone(c) ? kSazabiLocked : kSazabiPink,ly=y+44.0f*s,scale=kLineScale*0.8f;
    if(c.aimHit && std::isfinite(c.aimRange))
        Label(text,lines,at,x,ly,1,scale,ink,L"%d m",static_cast<int>(std::lround(vec::Clamp(c.aimRange,0.0f,99999.0f))));
    else Label(text,lines,at,x,ly,1,scale,ink,L"--- m");
}
// The flight's gauges round C (see the top).
// Left of C (see the top): the thrusters' arc, its share and its name (or OVERHEAT).
void SazabiThrusterArc(void* drawer,void* ctx,Text* text,float width,float height,float s,const SazabiCue& c,Line* lines,int* at) noexcept {
    const float cx=width*0.5f,cy=height*0.5f,r=kSazabiArcR*s,t=kSazabiArcW*s,span=2.0f*kSazabiArcHalf,from=kTurn*0.5f-kSazabiArcHalf;
    const float share=Unit(c.thruster);
    const bool low=share<kSazabiLowThrust,blink=(GetTickCount64()/250)%2==0;
    const float* const fill=c.overheat ? kRed : low ? kAmber : c.boosting ? kSazabiBoost : kSazabiThrust;
    Arc(drawer,ctx,cx,cy,r,from,span,t+2.0f*s,24,kBarEdge);
    Arc(drawer,ctx,cx,cy,r,from,span,t,24,c.overheat ? kSazabiHotTrack : kSazabiTrack);
    if(share>0.0f)Arc(drawer,ctx,cx,cy,r,from,span*share,t,1+static_cast<int>(share*23.0f),fill);
    if(c.boosting && share>0.0f)Arc(drawer,ctx,cx,cy,r+t*0.5f+3.0f*s,from,span*share,1.5f*s,1+static_cast<int>(share*23.0f),kSazabiBoost);
    for(int i=1;i<4;++i) {   // its quarters
        const float a=from+span*static_cast<float>(i)*0.25f,ca=std::cos(a),sa=std::sin(a);
        Seg(drawer,ctx,cx+(r-t*0.5f)*ca,cy+(r-t*0.5f)*sa,cx+(r+t*0.5f)*ca,cy+(r+t*0.5f)*sa,1.5f*s,kBarTick);
    }
    const float lx=cx-kSazabiSide*s,up=cy-13.0f*s,down=cy+15.0f*s;
    Label(text,lines,at,lx,up,2,kLineScale*1.1f,fill,L"%d%%",static_cast<int>(std::lround(share*100.0f)));
    if(c.overheat)Label(text,lines,at,lx,down,2,kLineScale*0.8f,blink ? kRed : kWhite,L"%ls",Tr(Tx::overheat));
    else Label(text,lines,at,lx,down,2,kLineScale*0.7f,kSazabiHint,L"%ls",Tr(Tx::sazabiThruster));
}
// Right of C (see the top): the bracket, the altitude and its climb, the speed.
void SazabiAltSpeed(void* drawer,void* ctx,Text* text,float width,float height,float s,const SazabiCue& c,Line* lines,int* at) noexcept {
    const float cx=width*0.5f,cy=height*0.5f,r=kSazabiArcR*s,span=2.0f*kSazabiArcHalf;
    const float rx=cx+kSazabiSide*s,up=cy-13.0f*s,down=cy+15.0f*s;
    Arc(drawer,ctx,cx,cy,r,-kSazabiArcHalf,span,2.0f*s,24,kSazabiFrame);
    for(int i=0;i<=4;++i) {
        const float a=-kSazabiArcHalf+span*static_cast<float>(i)*0.25f,ca=std::cos(a),sa=std::sin(a);
        Seg(drawer,ctx,cx+r*ca,cy+r*sa,cx+(r+6.0f*s)*ca,cy+(r+6.0f*s)*sa,1.5f*s,kSazabiFrame);
    }
    const int alt=static_cast<int>(std::lround(vec::Clamp(std::isfinite(c.altitude) ? c.altitude : 0.0f,0.0f,99999.0f)));
    const int kmh=static_cast<int>(std::lround(vec::Clamp(std::isfinite(c.speed) ? c.speed*3.6f : 0.0f,0.0f,99999.0f)));
    const int altAt=*at;
    Label(text,lines,at,rx,up,0,kLineScale*0.85f,kWhite,Tr(Tx::sazabiAltitude),alt);
    Label(text,lines,at,rx,down,0,kLineScale*0.85f,c.boosting ? kSazabiBoost : kWhite,Tr(Tx::sazabiSpeed),kmh);
    if(*at>altAt && std::isfinite(c.climb) && std::fabs(c.climb)>kSazabiClimbShown) {   // the climb's arrow after the altitude
        const Line& l=lines[altAt];
        const float ax=l.x+l.w+12.0f*s,ay=up,h=7.0f*s,dir=c.climb>0.0f ? -1.0f : 1.0f;
        Tri(drawer,ctx,ax,ay-dir*h,ax,ay+dir*h,h,c.climb>0.0f ? kGreen : kAmber);
    }
}
// The funnels' pips across (x, y, w, h): the first `filled` in the pack, the rest hollow (flying).
void FunnelPips(void* drawer,void* ctx,float x,float y,float w,float h,int filled,float s) noexcept {
    const float gap=4.0f*s,pw=(w-gap*static_cast<float>(kSazabiFunnels-1))/static_cast<float>(kSazabiFunnels),t=1.5f*s;
    for(int i=0;i<kSazabiFunnels;++i) {
        const float px=x+static_cast<float>(i)*(pw+gap);
        Rect(drawer,ctx,px-t,y-t,px+pw+t,y+h+t,kBarEdge);
        if(i<filled){Rect(drawer,ctx,px,y,px+pw,y+h,kSazabiPink);Rect(drawer,ctx,px,y,px+pw,y+h*0.35f,kBarShine);continue;}
        Rect(drawer,ctx,px,y,px+pw,y+t,kSazabiPink);Rect(drawer,ctx,px,y+h-t,px+pw,y+h,kSazabiPink);
        Rect(drawer,ctx,px,y,px+t,y+h,kSazabiPink);Rect(drawer,ctx,px+pw-t,y,px+pw,y+h,kSazabiPink);
    }
}
// The rows' names and gauges from the cue (see the top): `rows` gets the names, `g` the gauges.
void SazabiRowsOf(const SazabiCue& c,Line* rows,SazabiGauge* g) noexcept {
    const bool low=c.thruster<kSazabiLowThrust,charging=c.cannonCharge>0.0f,empty=c.missiles<=0;
    const float salvo=empty ? 0.0f : Unit(c.missileReady),lock=Unit(c.missileLock);
    const int pack=SazabiPack(c);
    Format(rows[0],L"%ls",Tr(Tx::sazabiThruster));
    rows[0].rgba=c.overheat ? kRed : low ? kAmber : kWhite;
    g[0]=SazabiGauge{Unit(c.thruster),c.overheat ? kRed : low ? kAmber : c.boosting ? kSazabiBoost : kCyan,-1,-1.0f,nullptr};
    Format(rows[1],L"%ls",Tr(Tx::sazabiRifle));
    rows[1].rgba=c.rifleReady>=1.0f ? kGreen : kWhite;
    g[1]=SazabiGauge{Unit(c.rifleReady),ReadyColour(c.rifleReady),-1,-1.0f,nullptr};
    Format(rows[2],Tr(Tx::sazabiMissiles),c.missiles>0 ? c.missiles : 0);
    rows[2].rgba=empty ? kRed : salvo>=1.0f ? kGreen : kWhite;
    g[2]=SazabiGauge{salvo,ReadyColour(salvo),-1,lock,lock>=1.0f ? kRed : kYellow};
    Format(rows[3],L"%ls",Tr(Tx::sazabiCannon));
    rows[3].rgba=charging ? kSazabiPink : c.cannonReady>=1.0f ? kGreen : kWhite;
    g[3]=charging ? SazabiGauge{Unit(c.cannonCharge),kSazabiPink,-1,-1.0f,nullptr}
                  : SazabiGauge{Unit(c.cannonReady),ReadyColour(c.cannonReady),-1,-1.0f,nullptr};
    Format(rows[4],L"%ls",Tr(Tx::sazabiFunnels));
    rows[4].rgba=pack>0 ? kWhite : kAmber;
    g[4]=SazabiGauge{0.0f,nullptr,pack,Unit(c.funnelReady),ReadyColour(c.funnelReady)};
}
// The panel's row of each special (cue.special: 0 the missiles, 1 the funnels, 2 the cannon).
constexpr int kSazabiSpecialRow[kSazabiSpecials]={2,4,3};
// The state tags lit (see the top) into `tags`: how many.
int SazabiTagsOf(const SazabiCue& c,Line* tags) noexcept {
    int n=0;
    auto tag=[&](bool on,Tx text,const float* rgba){if(on){Format(tags[n],L"%ls",Tr(text));tags[n++].rgba=rgba;}};
    tag(c.guard,Tx::sazabiGuard,kCyan);
    tag(c.air,Tx::sazabiAir,kTeal);
    tag(c.swinging,Tx::sazabiTomahawk,kAmber);
    tag(SazabiLockDone(c),Tx::sazabiLock,kRed);
    tag(c.overheat,Tx::overheat,kRed);
    return n;
}
void SazabiPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,const SazabiCue& c,Line* lines,int* at) noexcept {
    if(*at+1+kSazabiRows+kSazabiTags>kMaxLines)return;
    Line* const title=&lines[*at];
    Line* const rows=title+1;
    Line* const tags=rows+kSazabiRows;
    SazabiGauge g[kSazabiRows];
    Format(*title,L"%ls",Tr(Tx::sazabiTitle));
    title->scale=kTitleScale;title->rgba=kSazabiRed;
    SazabiRowsOf(c,rows,g);
    const int nTags=SazabiTagsOf(c,tags),n=1+kSazabiRows+nTags,picked=kSazabiSpecialRow[SazabiSpecialOf(c)];
    for(int i=1;i<n;++i)title[i].scale=i<=kSazabiRows ? kLineScale : kLineScale*0.8f;
    for(int i=0;i<n;++i)title[i].w=title[i].h=0.0f;
    if(text)MeasureAll(*text,title,n);
    const float pad=10.0f*s,gap=7.0f*s,col=18.0f*s,tagGap=12.0f*s,barW=220.0f*s,barH=12.0f*s,subH=5.0f*s,subGap=2.0f*s,mark=16.0f*s;
    const float titleH=title->h>0.0f ? title->h : 30.0f*s;
    float nameW=0.0f,tagsW=0.0f,rowH[kSazabiRows],rowsH=0.0f;
    for(int i=0;i<kSazabiRows;++i) {
        nameW=rows[i].w>nameW ? rows[i].w : nameW;
        const float lineH=rows[i].h>0.0f ? rows[i].h : 24.0f*s,gauge=barH+(g[i].sub>=0.0f ? subGap+subH : 0.0f);
        rowH[i]=lineH>gauge ? lineH : gauge;
        rowsH+=rowH[i]+(i ? gap : 0.0f);
    }
    float tagH=0.0f;
    for(int i=0;i<nTags;++i){tagsW+=tagGap+tags[i].w;tagH=std::fmax(tagH,tags[i].h>0.0f ? tags[i].h : 18.0f*s);}
    // the tags on the title row while they fit beside the name over the rows' width, else a row of their own under it
    const float rowW=mark+nameW+col+barW;
    const bool wrap=nTags>0 && title->w+tagsW>rowW;
    const float titleW=wrap ? std::fmax(title->w,tagsW-tagGap) : title->w+tagsW,headH=titleH+(wrap ? gap+tagH : 0.0f);
    const float w=(rowW>titleW ? rowW : titleW)+2.0f*pad,h=pad+headH+gap+rowsH+pad;
    const float x0=std::fmax(width*0.5f-kSazabiLeft*s,16.0f*s);
    float y0=height*0.80f-h;
    // the flight's gauges left of C reach to kSazabiFlightReach: a panel that would meet them goes to the screen's foot
    if(x0+w>width*0.5f-kSazabiFlightReach*s && y0<height*0.5f+kSazabiFlightFoot*s)y0=std::fmax(height-16.0f*s-h,height*0.5f+kSazabiFlightFoot*s);
    const float bx=x0+w-pad-barW;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,kSazabiRed);
    Rect(drawer,ctx,x0,y0,x0+3.0f*s,y0+h,kSazabiRed);
    title->x=x0+pad;title->y=y0+pad;
    float x=wrap ? x0+pad-tagGap : x0+w-pad-tagsW;   // the tags at the title row's right end, or a row under it
    const float tagsBottom=wrap ? title->y+titleH+gap+tagH : title->y+titleH;
    for(int i=0;i<nTags;++i){x+=tagGap;tags[i].x=x;tags[i].y=tagsBottom-tags[i].h;x+=tags[i].w;}
    float y=y0+pad+headH+gap;
    for(int i=0;i<kSazabiRows;++i) {
        const float lineH=rows[i].h>0.0f ? rows[i].h : 24.0f*s,gauge=barH+(g[i].sub>=0.0f ? subGap+subH : 0.0f);
        const float gy=y+(rowH[i]-gauge)*0.5f;
        if(i==picked) {   // the selected special: its band and pointer
            Rect(drawer,ctx,x0+3.0f*s,y-gap*0.5f,x0+w,y+rowH[i]+gap*0.5f,kSazabiPick);
            Tri(drawer,ctx,x0+pad,y+rowH[i]*0.5f,x0+pad+mark*0.6f,y+rowH[i]*0.5f,5.0f*s,kSazabiPink);
        }
        rows[i].x=x0+pad+mark;rows[i].y=y+(rowH[i]-lineH)*0.5f;
        if(g[i].pips>=0)FunnelPips(drawer,ctx,bx,gy,barW,barH,g[i].pips,s);
        else Bar(drawer,ctx,bx,gy,barW,barH,g[i].share,g[i].share,g[i].fill,s);
        if(g[i].sub>=0.0f)Bar(drawer,ctx,bx,gy+barH+subGap,barW,subH,g[i].sub,g[i].sub,g[i].subFill,s);
        y+=rowH[i]+gap;
    }
    *at+=n;
}
// The special weapon's readout under right of C (see the top).
void SazabiSpecial(void* drawer,void* ctx,Text* text,float width,float height,float s,const SazabiCue& c,Line* lines,int* at) noexcept {
    if(*at+2>kMaxLines)return;
    Line& name=lines[*at];
    Line& hint=lines[*at+1];
    const int special=SazabiSpecialOf(c);
    const bool locked=SazabiLockDone(c);
    if(special==0)Format(name,Tr(Tx::sazabiMissiles),c.missiles>0 ? c.missiles : 0);
    else Format(name,L"%ls",Tr(special==1 ? Tx::sazabiFunnels : Tx::sazabiCannon));
    name.rgba=special==0 && c.missiles<=0 ? kRed : special==0 && locked ? kSazabiLocked : kSazabiPink;
    name.scale=kLineScale*0.9f;
    wchar_t key[32],button[32],both[72];
    KeyName(Cfg().sazabiSwitchKey,key,32);SeatButtonName(Cfg().sazabiSwitchButton,button,32);
    std::swprintf(both,72,L"%ls / %ls",key,button);
    Format(hint,Tr(Tx::sazabiSwitch),both);
    hint.rgba=kSazabiHint;hint.scale=kLineScale*0.65f;
    name.w=name.h=hint.w=hint.h=0.0f;
    if(text)MeasureAll(*text,&name,2);
    const float pad=7.0f*s,gap=5.0f*s,barW=180.0f*s,barH=9.0f*s,subH=4.0f*s,subGap=2.0f*s;
    const float nameH=name.h>0.0f ? name.h : 22.0f*s,hintH=hint.h>0.0f ? hint.h : 16.0f*s;
    const bool sub=special==0 ? c.hasLock : special==1;   // the lock's progress; the next launch
    const float gaugeH=barH+(sub ? subGap+subH : 0.0f);
    const float inner=std::fmax(barW,std::fmax(name.w,hint.w)),w=inner+2.0f*pad+3.0f*s,h=pad+nameH+gap+gaugeH+gap+hintH+pad;
    const float x0=width*0.5f+kSazabiSpecialX*s,y0=height*0.5f+kSazabiSpecialY*s,x=x0+3.0f*s+pad;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+3.0f*s,y0+h,kSazabiPink);
    name.x=x;name.y=y0+pad;
    const float gy=y0+pad+nameH+gap;
    if(special==0) {
        const float salvo=c.missiles>0 ? Unit(c.missileReady) : 0.0f,lock=Unit(c.missileLock);
        Bar(drawer,ctx,x,gy,barW,barH,salvo,salvo,ReadyColour(salvo),s);
        if(sub)Bar(drawer,ctx,x,gy+barH+subGap,barW,subH,lock,lock,lock>=1.0f ? kRed : kYellow,s);
    } else if(special==1) {
        FunnelPips(drawer,ctx,x,gy,barW,barH,SazabiPack(c),s);
        const float ready=Unit(c.funnelReady);
        Bar(drawer,ctx,x,gy+barH+subGap,barW,subH,ready,ready,ReadyColour(ready),s);
    } else {
        const bool charging=c.cannonCharge>0.0f;
        const float share=Unit(charging ? c.cannonCharge : c.cannonReady);
        Bar(drawer,ctx,x,gy,barW,barH,share,share,charging ? kSazabiPink : ReadyColour(share),s);
    }
    hint.x=x;hint.y=gy+gaugeH+gap;
    *at+=2;
}
// The whole of it (see the top), the panel last: its lines after the rest's.
void SazabiHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const SazabiCue& c,
               Line* lines,int* at) noexcept {
    float x,y;
    if(c.hasLock)LockAt(drawer,ctx,vp,width,height,s,c.missileLock>=1.0f ? 2 : 1,c.lock,Unit(c.missileLock),&x,&y);
    SazabiAssistMark(drawer,ctx,vp,width,height,s,c);
    SazabiReticle(drawer,ctx,vp,width,height,s,c);
    SazabiRange(text,vp,width,height,s,c,lines,at);
    SazabiThrusterArc(drawer,ctx,text,width,height,s,c,lines,at);
    SazabiAltSpeed(drawer,ctx,text,width,height,s,c,lines,at);
    SazabiSpecial(drawer,ctx,text,width,height,s,c,lines,at);
    SazabiPanel(drawer,ctx,text,width,height,s,c,lines,at);
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
        Format(l,Tr(Tx::carrierBadge),static_cast<int>(std::lround(share*100.0f)));
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
        wchar_t name[24];
        hudtext::WordTo(part.name,name,_countof(name));
        if(part.down) {
            const int sec=static_cast<int>(part.repairSec);
            Format(l,Tr(Tx::carrierPartDownShort),name,sec/60,sec%60);
            l.rgba=kDown;
        } else {
            Format(l,L"%ls",name);
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
                    const edf::aimlink::TurretReadoutV1& r,bool flipped,Line* lines,int* at,const edf::aimlink::ModeBindingV1* binding=nullptr,float controlsBottom=-1.0f) noexcept {
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
            wchar_t reach[32]=L"";
            if(!r.inReach)std::swprintf(reach,32,L"  %ls",Tr(Tx::outOfRange));
            Label(text,lines,at,x,y+(kLeadCircle+12.0f)*s,1,kLineScale*0.85f,c,L"%d m  %.1f s%ls",static_cast<int>(std::lround(r.range)),
                  r.flight,reach);
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
    const wchar_t* const state=Tr(!r.ownGun ? Tx::gunnersAuto : circle ? Tx::autoAimOffCircle : Tx::autoAimOn);
    const float* const colour=!r.ownGun ? kWhite : circle ? kAmber : kGreen;
    wchar_t keys[128]=L"";
    if(r.ownGun && hasMode)std::swprintf(keys,128,Tr(Tx::autoAimToggle),mode,Tr(circle ? Tx::wordOn : Tx::wordOff));
    else if(r.ownGun)wcscpy_s(keys,Tr(r.keys ? Tx::aimModeKeyUnset : Tx::aimModeButtonUnset));
    if(hasLock) {
        const std::size_t n=wcslen(keys);
        std::swprintf(keys+n,128-n,Tr(Tx::lockKey),n ? L"   " : L"",lock,Tr(r.lock==link::Lock::none ? Tx::lockWord : Tx::nextWord));
    }
    if(binding && binding->conflict) {
        wchar_t requested[32],effective[32];
        Binding(binding->keys,binding->requested,binding->requested,requested,32);
        if(binding->effective>0)Binding(binding->keys,binding->effective,binding->effective,effective,32);
        else wcscpy_s(effective,L"--");
        const std::size_t n=wcslen(keys);
        std::swprintf(keys+n,128-n,Tr(Tx::aimSightBinding),requested,effective);
    }
    const float keysY=controlsBottom>=0.0f ? controlsBottom : height*0.905f;
    const float stateY=controlsBottom>=0.0f ? controlsBottom-32.0f*s : height*0.875f;
    Label(text,lines,at,width*0.5f,stateY,1,kLineScale,colour,L"%ls",state);
    Label(text,lines,at,width*0.5f,keysY,1,kLineScale*0.85f,r.lock==link::Lock::locked ? kRed : kWhite,L"%ls",keys);
    if(!flipped || !r.ownGun)return;
    const float by=height*0.30f;
    Label(text,lines,at,width*0.5f,by,1,kTitleScale,colour,L"%ls",Tr(circle ? Tx::autoAimOffBanner : Tx::autoAimOn));
    const float w=(*at>0 && lines[*at-1].w>0.0f ? lines[*at-1].w : 420.0f*s)+40.0f*s,h=48.0f*s;
    Rect(drawer,ctx,(width-w)*0.5f,by-h*0.5f,(width+w)*0.5f,by+h*0.5f,kPanel);
    Rect(drawer,ctx,(width-w)*0.5f,by-h*0.5f,(width+w)*0.5f,by-h*0.5f+3.0f*s,colour);
}

// --- The stock vehicles' HUD (vhud.cpp gathers it; ini StockVehicleHud; docs/hud-re.md §7): for the stock vehicle the
// player drives or mans, in the helis' green and drawn with the same quads and text (exclusive full screen too):
//  - each weapon's impact point (StockMarks): a gun's or a cannon's the helis' boresight and pipper where its round
//    meets the map, or with an enemy under the view the jets' gun sight on it (the pipper where the round passes it, its
//    lead mark where it is then: pipper on the mark, a hit; dim out of the round's reach); neither (the sky): the
//    boresight alone with its label (roundaim.h GunSight), a grenade's or a mortar's (and any round flying longer than kLobSec) the
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
    Label(text,lines,at,x,base+40.0f*s,1,kLineScale*0.7f,kAmber,L"%ls",Tr(Tx::hullMark));
}

// A weapon's label (its round's class: rounds.cpp, vhud.cpp) in the HUD's language, bracketed when selected.
void ArmName(const StockArm& a,bool selected,wchar_t* out,std::size_t size) noexcept {
    wchar_t word[24];
    hudtext::WordTo(a.label,word,_countof(word));
    _snwprintf_s(out,size,_TRUNCATE,selected ? L"[%ls]" : L"%ls",word);
}

// One weapon's mark (see above); `name` its label (bracketed when selected).
// A direct-fire gun's sight: the reticle its style draws (reticle.h: a tank gun's chevron, lead stadia and ballistic
// range ladder; a marksman's duplex and mil dots; a machine gun's ring; an anti-aircraft gun's lead rings; a beam's
// open cross) on the gun's line (the boresight, where the gun points: it follows the camera a moment late), its angles
// from the view's own projection (the magnification is in it); over the left of it the gun, over the right its
// rangefinder (the map hit or the ranged target: the pipper's `range`). The pipper and the lead mark stay StockMark's.
void GunReticle(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockArm& a,
                const wchar_t* name,Line* lines,int* at) noexcept {
    float cx,cy;
    if(!sight::ToScreen(vp,a.bore,0.0f,width,height,&cx,&cy))return;
    const reticle::View v{cx,cy,FocalAt(vp,a.bore,width,height,cx,cy),s,kLineScale};
    reticle::Aim aim{};
    aim.style=a.reticle;aim.shell=a.shell;aim.speed=a.roundSpeed;
    LadderMarks(vp,width,height,a.ladder,&aim);
    reticle::Sketch k{};
    reticle::Draw(aim,v,&k);
    Render(drawer,ctx,text,s,k,lines,at);
    const float in=(a.reticle==reticle::Style::cannon ? reticle::kStadiaIn : 40.0f)*s;
    const float over=cy-16.0f*s,note=kLineScale*0.85f;
    Label(text,lines,at,cx-in,over,2,note,kHud,L"%ls",name);
    const float measured=a.ranged ? a.targetRange : a.range;
    if(measured>0.0f && (a.hit || a.ranged))
        Label(text,lines,at,cx+in,over,0,note,a.ranged && !a.inReach ? kHudDim : kHud,Tr(Tx::sightRange),static_cast<int>(std::lround(measured)));
    else Label(text,lines,at,cx+in,over,0,note,kHudDim,L"%ls",Tr(Tx::sightNoRange));
}

// `reticle`: this gun's sight is GunReticle (the seat's sight gun: StockMarks), its pipper unlabelled (the reticle
// reads its range).
void StockMark(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockArm& a,const wchar_t* name,
               bool reticle,Line* lines,int* at) noexcept {
    float x,y;
    const float note=kLineScale*0.85f;
    const int metres=static_cast<int>(std::lround(a.range));
    if(a.kind==RoundKind::homing) {
        if(LockAt(drawer,ctx,vp,width,height,s,a.lock,a.at,a.lockProgress,&x,&y))
            Label(text,lines,at,x,y+44.0f*s,1,note,a.lock==2 ? kRed : kYellow,L"%ls %d m",name,metres);
        else if(!a.lock) {
            wchar_t line[48];
            _snwprintf_s(line,_countof(line),_TRUNCATE,L"%ls %d m",name,metres);
            SeekerGate(drawer,ctx,text,vp,width,height,s,a.bore,line,lines,at);
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
    if(reticle && !a.physicalOnly)GunReticle(drawer,ctx,text,vp,width,height,s,a,name,lines,at);
    else Boresight(drawer,ctx,vp,width,height,s,a.bore);
    if(a.ranged && !a.physicalOnly)LeadMark(drawer,ctx,vp,width,height,s,a.lead,a.inReach ? kHud : kHudDim);
    PhysicalPaths(drawer,ctx,text,vp,width,height,s,a.path,a.paths,!reticle,lines,at);
}

// What the stock vehicles' HUD takes from the other readouts: the Nix's legs and torso (its ring), the drill tank's drill
// (a line in the block instead of DrillPanel), the EMC's charged beam (a line and its bar: emc.cpp), and whether
// EDF6AutoTurret's lead circle is on the seat's own gun (the
// seat's first weapon, the one its aim turns: then the circle and its bore cross are that gun's marks, not a pipper).
struct StockExtras { const NixTorso* nix; const DrillCue* drill; bool leadGun; const EmcCue* emc; const ProteusReadout* proteus; bool high=false;
                     const armorhud::Readout* armor=nullptr; };   // the hidden stock armor gauge's numbers (ArmorRows)
// --- The Proteus (proteus.cpp; README 普罗透斯): its part of the stock vehicle HUD. In the block (StockBlock) up to four
// lines with a bar under some: the stance (and the stagger's progress), the shield (its state, HP and deployed its heat;
// the HP bar under it), the field (its allies), the driver's missile launcher; the bindings named where the driver has a
// press to make. The cannons and the launcher themselves are listed with the seat's weapons (vhud.cpp, borrowed mounts).
// On the hull ring the standing shield's arc; on the ground the field's edge. ---
constexpr int kProteusLines=4;
const char kBarrierKey=0;            // the shield's HP bar's damage trail's key (an address of our own)
struct ProteusLine { Line* line; bool bar; float share; const float* fill; const void* trailKey; };

// A binding's name: the mouse's buttons by name (GetKeyNameText has none for them), the keys as KeyName, a pad's button.
void ProteusBinding(bool keys,int key,int button,wchar_t* out,int size) noexcept {
    static const Tx kMouse[]={Tx::count,Tx::mouseLeft,Tx::mouseRight,Tx::count,Tx::mouseMiddle,Tx::mouseX1,Tx::mouseX2};
    if(keys && key>0 && key<=6){wcscpy_s(out,static_cast<rsize_t>(size),kMouse[key]==Tx::count ? L"?" : Tr(kMouse[key]));return;}
    Binding(keys,key,button,out,size);
}

// The block's Proteus lines (see above) into `pl` (`n` of them): the lines are `lines`' next ones.
int ProteusLinesOf(const ProteusReadout& p,Line* lines,ProteusLine* pl) noexcept {
    wchar_t mode[24],shield[24],launcher[24];
    ProteusBinding(p.keys,p.modeKey,p.modeButton,mode,24);
    ProteusBinding(p.keys,p.shieldKey,p.shieldButton,shield,24);
    ProteusBinding(p.keys,p.launcherKey,0,launcher,24);
    if(!p.keys)wcscpy_s(launcher,L"LT");
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
            case proteus::Mode::walk: Format(l,L"%ls",Tr(Tx::proteusWalk));if(p.driver)Append(l,Tr(Tx::proteusDeployKey),mode);break;
            case proteus::Mode::deploying: Format(l,Tr(Tx::proteusDeploying),static_cast<int>(std::lround(p.stagger*100.0f)));l.rgba=kAmber;break;
            case proteus::Mode::deployed: Format(l,L"%ls",Tr(Tx::proteusDeployed));if(p.driver)Append(l,Tr(Tx::proteusStowKey),mode);l.rgba=kCyan;break;
            case proteus::Mode::stowing: Format(l,Tr(Tx::proteusStowing),static_cast<int>(std::lround(p.stagger*100.0f)));l.rgba=kAmber;break;
        }
    }
    // The shield: its state and HP (deployed its heat too), the HP bar under it.
    {
        const bool deployed=p.mode==proteus::Mode::deployed;
        Line& l=add(true,p.shield,HullColour(p.shield));
        pl[n-1].trailKey=&kBarrierKey;
        const Tx state=!p.shieldReady ? Tx::shieldOffline : p.broken ? Tx::shieldBroken : deployed && p.overheated ? Tx::overheat :
                       p.shieldUp ? Tx::shieldUp : Tx::shieldOff;
        const int hp=static_cast<int>(std::lround(p.shield*100.0f));
        if(deployed)Format(l,Tr(Tx::shieldHeat),Tr(state),hp,static_cast<int>(std::lround(p.heat*100.0f)));
        else Format(l,Tr(Tx::frontShield),Tr(state),hp);
        if(p.driver && p.shieldReady)Append(l,L"   [%ls]",shield);
        if(p.priority)Append(l,L"%ls",Tr(Tx::alliesFocus));
        l.rgba=!p.shieldReady ? kHudDim : p.broken || (deployed && p.overheated) ? (blink ? kRed : kWhite) : p.shieldUp ? kCyan : kHudDim;
    }
    if(p.mode==proteus::Mode::deployed) {   // the field
        Line& f=add(false,0.0f,nullptr);
        Format(f,Tr(Tx::field),static_cast<int>(std::lround(p.fieldRadius)),p.allies);
        f.rgba=kTeal;
    }
    if(p.driver) {   // the launcher, the driver's while its own seat is empty
        Line& l=add(false,0.0f,nullptr);
        if(p.launcher){Format(l,Tr(Tx::launcherKey),launcher);l.rgba=kYellow;}
        else if(p.mode!=proteus::Mode::deployed){Format(l,L"%ls",Tr(Tx::launcherDeployFirst));l.rgba=kHudDim;}
        else --n;   // a gunner of its own works it
    }
    return n;
}

// The field's edge on the ground (see above).
void ProteusMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const ProteusReadout& p,Line* lines,
                  int* at) noexcept {
    for(int i=0;i<p.ringCount && i<kProteusRing;++i) {
        if(i%2)continue;   // dashed
        float x0,y0,x1,y1;
        const int k=(i+1)%p.ringCount;
        if(sight::ToScreen(vp,p.ring[i],1.0f,width,height,&x0,&y0) && sight::ToScreen(vp,p.ring[k],1.0f,width,height,&x1,&y1))
            Seg(drawer,ctx,x0,y0,x1,y1,2.0f*s,kTeal);
    }
    (void)text;(void)lines;(void)at;
}


// The marks of every aimed weapon but the Katyusha's (launcher.cpp's), those landing on another's drawn once.
// The seat's sight gun (GunReticle's): the picked weapon when its style is a gun sight (reticle.h GunStyle: not a
// flamethrower's, a grenade's, a missile's) with a ladder (a beam needs none); -1 for none. Not the gun EDF6AutoTurret's lead circle is on
// (`leadGun`: arm 0 has its marks).
int SightGun(const StockHudReadout& r,bool leadGun) noexcept {
    const auto sights=[&](int i){
        const StockArm& a=r.arm[i];
        return a.aimed && !a.physicalOnly && !a.lofted && !a.lobbed && a.kind==RoundKind::arc && reticle::GunStyle(a.reticle) &&
               (a.shell==reticle::Shell::beam || a.ladder.ticks>0) && !(i==0 && leadGun);
    };
    return r.sight>=0 && r.sight<r.arms && r.sight<kStockArms && sights(r.sight) ? r.sight : -1;
}

void StockMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockHudReadout& r,
                const StockExtras& x,Line* lines,int* at) noexcept {
    const int sightGun=x.high ? -1 : SightGun(r,x.leadGun);
    // One selected weapon owns the fire-control marks. A missile/rocket selection must not leave the main gun's
    // optical reticle underneath it; lofted launchers have their dedicated impact/spread marks.
    const int selected=r.sight>=0 && r.sight<r.arms && r.sight<kStockArms ? r.sight : -1;
    for(int i=0;i<r.arms && i<kStockArms;++i) {
        const StockArm& a=r.arm[i];
        if(i!=selected && !a.coFired)continue;
        if(!a.aimed || a.lofted)continue;
        if(x.high || i!=selected) {
            PhysicalPaths(drawer,ctx,text,vp,width,height,s,a.path,a.paths,i==selected,lines,at);
            continue;
        }
        if(i==0 && x.leadGun && !a.physicalOnly && a.kind!=RoundKind::homing)continue;
        wchar_t name[32];
        ArmName(a,i==r.selected,name,_countof(name));
        StockMark(drawer,ctx,text,vp,width,height,s,a,name,i==sightGun,lines,at);
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
    wchar_t name[32];
    ArmName(a,selected,name,_countof(name));
    Format(l,L"%ls",name);
    if(a.ammoMax>1)Append(l,L" %d/%d",a.ammo>0 ? a.ammo : 0,a.ammoMax);
    else if(a.ammo>1)Append(l,L" %d",a.ammo);   // no magazine read (WeaponStatusOk false): the rounds alone
    else if(a.ammo>0)Append(l,L" %ls",Tr(Tx::armReady));
    if(a.ammo<=0 && a.reload<1.0f) {
        Append(l,L" ");
        Append(l,Tr(Tx::armReload),static_cast<int>(std::lround(a.reload*100.0f)));
        if(a.reloadSec>=0.0f)Append(l,L" %.1fs",a.reloadSec);
        l.rgba=kAmber;
    } else if(a.ammo<=0) {
        Append(l,L" %ls",Tr(a.canReload ? Tx::armReloadWord : Tx::armEmpty));
        l.rgba=a.canReload ? kAmber : kRed;
    } else l.rgba=selected ? kCyan : kHud;
    l.scale=kLineScale*0.85f;
}

// The EMC's line (EmcCue): charging, its share and an amber bar filling; the beam out, BEAM and the seconds left, its bar
// emptying, white; after it, REARM; ready, the beams its rounds still make, green (a charge let go drains on its bar); no rounds, EMPTY, red.
void EmcLine(Line& l,const EmcCue& c,float* bar,const float** colour) noexcept {
    const float beam=Cfg().emcBeamSec>0.0f ? Cfg().emcBeamSec : 1.0f;
    if(c.firing) {
        Format(l,Tr(Tx::emcBeam),c.beamLeft);
        *bar=Unit(c.beamLeft/beam);*colour=kWhite;
    } else if(c.charging) {
        Format(l,Tr(Tx::emcCharge),static_cast<int>(std::lround(Unit(c.charge)*100.0f)));
        *bar=Unit(c.charge);*colour=kAmber;
    } else if(c.empty) {
        Format(l,L"%ls",Tr(Tx::emcEmpty));
        *bar=0.0f;*colour=kRed;
    } else if(c.rearm>0.0f) {
        Format(l,Tr(Tx::emcRearm),c.rearm);
        *bar=0.0f;*colour=kHudDim;
    } else {
        Format(l,Tr(Tx::emcReady),c.beams);
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

// The stock armor gauge's numbers where stockgauge.cpp hides it (the user, 2026-10-09: "上了载具以后…隐藏", then "让护甲
// 显示在咱们的hud不就行了"): a line and a bar each, as the hull's in StockBlock (its 150 x 6 px bar under its line). The
// player's armor red at the stock gauge's own low mark (armorhud::kLowArmor, 0x827723), else the HUD's colour; the
// vehicle's durability coloured as StockBlock's hull (HpColour). In StockBlock the armor's line goes under the hull's
// bar; a vehicle whose HUD shows no durability (the aircraft, a stock heli's strip, the Sazabi) has both in ArmorPanel.
constexpr float kDurabilityBarW=150.0f,kDurabilityBarH=6.0f;   // px at 1080 lines: StockBlock's barW / barH
void DurabilityLine(Line& l,Tx what,const armorhud::Durability& d,Text* text) noexcept {
    Format(l,Tr(what),static_cast<int>(std::lround(d.hp)),static_cast<int>(std::lround(d.most)));
    l.scale=kLineScale;l.rgba=d.low ? kRed : kHud;l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
}
const float* DurabilityFill(const armorhud::Durability& d,bool armor) noexcept { return armor ? (d.low ? kRed : kHud) : HpColour(d.share); }

// The block of ArmorRows apart (see above): where the stock gauge was, the screen's top left, the vehicle's durability
// over the player's armor.
constexpr float kArmorPanelX=48.0f,kArmorPanelY=96.0f;   // px at 1080 lines: its left and top
void ArmorPanel(void* drawer,void* ctx,Text* text,float s,const armorhud::Readout& r,Line* lines,int* at) noexcept {
    const int rows=r.hasHull ? 2 : 1;
    if(*at+rows>kMaxLines)return;
    const float lineH=18.0f*s,gap=3.0f*s,barW=kDurabilityBarW*s,barH=kDurabilityBarH*s,x=kArmorPanelX*s;
    float y=kArmorPanelY*s;
    const armorhud::Durability* const d[2]={r.hasHull ? &r.hull : &r.armor,&r.armor};
    const Tx what[2]={r.hasHull ? Tx::hullRow : Tx::armorRow,Tx::armorRow};
    for(int i=0;i<rows;++i) {
        const bool armor=!r.hasHull || i==1;
        Line& l=lines[(*at)++];
        DurabilityLine(l,what[i],*d[i],text);
        l.x=x;l.y=y;y+=(l.h>0.0f ? l.h : lineH)+gap;
        Bar(drawer,ctx,x,y,barW,barH,d[i]->share,d[i]->share,DurabilityFill(*d[i],armor),s);
        y+=barH+gap*2.0f;
    }
}

// The block left of the bottom centre (see above). A Nix: the ring's hull is its legs, its gun the torso (nix.cpp), the
// twist's limits ticked and its angle on the info line. The drill tank: its drill's RPM and heat a line under the
// weapons (DrillPanel's colours), OVERHEAT the warning. The EMC (emc.cpp): its charged beam a line under the weapons
// and a bar under it (EmcLine). The readouts of a kind (the Nix's, the EMC's, the Proteus's) are taken only when they are
// this vehicle's (their position on it). Out of lines (a crowded frame): the head always (it carries the warnings), then
// as many weapons as fit, then the extras that fit whole; never the whole block dropped (with the stock gauge hidden,
// stockgauge.cpp, it is the vehicle's only readout).
void StockBlock(void* drawer,void* ctx,Text* text,float width,float height,float s,const StockHudReadout& r,const StockExtras& x,
                Line* lines,int* at,float bottom=-1.0f,bool compact=false) noexcept {
    int room=kMaxLines-*at-3;   // the head's three lines first
    if(room<0)return;
    const bool armorOn=x.armor && room>0;   // the hidden stock armor gauge's numbers: next after the head (ArmorRows)
    room-=armorOn ? 1 : 0;
    const int want=compact ? 0 : r.arms<kStockArms ? r.arms : kStockArms;
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
    Line* const armor=armorOn ? &lines[(*at)++] : nullptr;
    Line* const arm=&lines[*at];
    *at+=arms;
    Line* const drill=drillOn ? &lines[(*at)++] : nullptr;
    Line* const emcLine=emc ? &lines[(*at)++] : nullptr;
    ProteusLine pl[kProteusLines]{};
    const int prots=protLines ? ProteusLinesOf(*prot,&lines[*at],pl) : 0;
    *at+=prots;
    const bool nix=x.nix && vec::Dist(x.nix->at,r.pos)<2.0f;   // the Nix readout is this vehicle's
    bool missile=false,locked=false,dry=r.arms>0;
    for(int i=0;i<r.threats && i<kStockThreats;++i){missile=missile || r.threatKind[i]==2;locked=locked || r.threatKind[i]==1;}
    for(int i=0;i<r.arms && i<kStockArms;++i)dry=dry && r.arm[i].ammo<=0 && !r.arm[i].canReload;
    const float hp=r.hpMax>0.0f ? Unit(r.hp/r.hpMax) : 0.0f;
    const bool blink=(GetTickCount64()/125)%2==0;
    if(missile){Format(warn,L"%ls",Tr(Tx::missileBang));warn.rgba=blink ? kRed : kWhite;}
    else if(locked){Format(warn,L"%ls",Tr(Tx::locked));warn.rgba=kYellow;}
    else if(hp<0.25f && r.hpMax>0.0f){Format(warn,L"%ls",Tr(Tx::hullCritical));warn.rgba=blink ? kRed : kWhite;}
    else if(drill && x.drill->overheated){Format(warn,L"%ls",Tr(Tx::drillOverheat));warn.rgba=blink ? kRed : kWhite;}
    else if(FuelLow(r.fuel)){Format(warn,L"%ls",Tr(Tx::lowFuel));warn.rgba=kAmber;}
    else if(dry){Format(warn,L"%ls",Tr(Tx::noAmmo));warn.rgba=kAmber;}
    else{Format(warn,L"");warn.rgba=kHud;}
    const char* const kind=prot ? "PROTEUS" : r.kind;
    if(r.seat==0)Format(title,L"%hs",kind);
    else Format(title,Tr(Tx::stockGunner),kind,r.seat);
    Format(info,Tr(Tx::stockInfo),static_cast<int>(std::lround(r.speed*3.6f)),static_cast<int>(std::lround(hp*100.0f)));
    if(nix){Append(info,L"    ");Append(info,Tr(Tx::twist),static_cast<int>(std::lround(-x.nix->twist*57.2957795f)));}   // right positive, as headings
    if(r.stab)Append(info,L"    %ls",Tr(r.stab==2 ? Tx::stabLag : Tx::stab));   // the gun stabilizer holds it (LAG: the hull outruns its drive)
    wchar_t fuel[32];
    FuelText(fuel,_countof(fuel),r.fuel);   // a bike's tank (the stock gauge's FUEL panel gone: stockgauge.cpp)
    if(fuel[0])Append(info,L"    %ls",fuel);
    warn.scale=kTitleScale;title.scale=info.scale=kLineScale;title.rgba=info.rgba=kHud;
    for(int i=0;i<arms;++i)ArmLine(arm[i],r.arm[i],i==r.selected);
    if(drill) {
        const DrillCue& c=*x.drill;
        const float share=Unit(c.rpm/c.maxRpm);
        wchar_t state[32]=L"";
        if(const wchar_t* const word=DrillState(c))std::swprintf(state,32,L"  %ls",word);
        Format(*drill,Tr(Tx::drillLineShort),static_cast<int>(std::lround(c.rpm)),static_cast<int>(std::lround(Unit(c.heat)*100.0f)),state);
        DrillLaunchHint(*drill,c);
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
    if(armor)DurabilityLine(*armor,Tx::armorRow,x.armor->armor,text);
    const float lineH=18.0f*s,gap=3.0f*s,barW=kDurabilityBarW*s,barH=kDurabilityBarH*s;
    float h=(title.h>0.0f ? title.h : lineH)+gap+(info.h>0.0f ? info.h : lineH)+gap+barH+gap*2.0f;
    if(armor)h+=(armor->h>0.0f ? armor->h : lineH)+gap+barH+gap*2.0f;
    for(int i=0;i<arms;++i)h+=(arm[i].h>0.0f ? arm[i].h : lineH)+gap;
    if(drill)h+=(drill->h>0.0f ? drill->h : lineH)+gap;
    if(emcLine)h+=(emcLine->h>0.0f ? emcLine->h : lineH)+gap+barH+gap;
    for(int i=0;i<prots;++i)h+=(pl[i].line->h>0.0f ? pl[i].line->h : lineH)+gap+(pl[i].bar ? barH+gap : 0.0f);
    const float cx=compact ? (kIndicatorR+24.0f)*s : width*0.5f-460.0f*s,tx=cx+kIndicatorR*s+18.0f*s;
    float y=(bottom>=0.0f ? bottom : height*0.80f)-h;
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
    if(armor) {
        armor->x=x0;armor->y=y;y+=(armor->h>0.0f ? armor->h : lineH)+gap;
        Bar(drawer,ctx,x0,y,barW,barH,x.armor->armor.share,x.armor->armor.share,DurabilityFill(x.armor->armor,true),s);
        y+=barH+gap*2.0f;
    }
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
                     const StockExtras& x,Line* lines,int* at,const LoadoutDock* dock=nullptr) noexcept {
    StockMarks(drawer,ctx,text,vp,width,height,s,r,x,lines,at);
    if(x.proteus && vec::Dist(x.proteus->pos,r.pos)<2.0f)ProteusMarks(drawer,ctx,text,vp,width,height,s,*x.proteus,lines,at);
    const bool nix=x.nix && vec::Dist(x.nix->at,r.pos)<2.0f;
    const float hull=nix ? HeadingOfYaw(x.nix->legsYaw) : sight::HeadingOf(r.hull);
    const float gun=nix ? sight::HeadingOf(x.nix->dir) : r.aimOk ? sight::HeadingOf(r.aim) : -1.0f;
    if(gun>=0.0f)StockTape(drawer,ctx,text,width,height,s,gun,hull,lines,at);
    else if(hull>=0.0f)StockTape(drawer,ctx,text,width,height,s,hull,-1.0f,lines,at);
    if(dock) {
        HudRegionText region(text,s,dock->scale,lines,at);
        StockBlock(drawer,ctx,region.text,width,height,dock->scale,r,x,lines,at,dock->top-12.0f*dock->scale,true);
    } else StockBlock(drawer,ctx,text,width,height,s,r,x,lines,at);
    if(Cfg().playerJetThreatHud && r.threats>0) {
        PlayerJetSymbols y{};
        std::memcpy(y.pos,r.pos,12);
        y.threats=r.threats<kMostThreats ? r.threats : kMostThreats;
        for(int i=0;i<y.threats && i<kStockThreats;++i){std::memcpy(y.threatAt[i],r.threatAt[i],12);y.threatKind[i]=r.threatKind[i];}
        std::memcpy(y.nose,r.lookOk ? r.look : r.hull,12);   // the scope's up: where the player looks (else the hull)
        // The warnings' RWR scope and the marks (no launch cue: ours); the scope right of the centre, clear of the block.
        const float fitted=std::fmin(dock ? dock->scale : s,width/(2.0f*(kBoxOff+kBoxW*0.5f+60.0f+2.0f*kRwrR+24.0f)));
        HudRegionText region(text,s,fitted,lines,at);
        RwrScope(drawer,ctx,region.text,width,height,fitted,y,0,GetTickCount64(),1.0f,lines,at,
                 dock ? dock->top-12.0f*fitted : -1.0f);
        ThreatMarks(drawer,ctx,vp,width,height,s,y);
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
        c.icon=hudcue::ArmIconOf(static_cast<int>(r.arm[i].kind),r.arm[i].lobbed,static_cast<int>(r.arm[i].style));
        c.picked=i==r.selected;
        if(r.arm[i].name[0]) {
            if(r.arm[i].ammo<=0 && r.arm[i].canReload)_snwprintf_s(c.text,_countof(c.text),_TRUNCATE,L"%ls  %ls",r.arm[i].name,l.text);
            else _snwprintf_s(c.text,_countof(c.text),_TRUNCATE,L"%ls %d/%d",r.arm[i].name,r.arm[i].ammo,r.arm[i].ammoMax);
        }
        else wcsncpy_s(c.text,_countof(c.text),l.text,_TRUNCATE);
        c.rgba=c.picked && l.rgba==kHud ? kCyan : l.rgba;
    }
    return n;
}

void StockDockHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,
                  const StockHudReadout& r,const StockExtras& x,const PayloadReadout* payload,bool switched,Line* lines,int* at) noexcept {
    LoadCell cells[kStockArms];
    const int n=StockCells(r,cells);
    Line controls{};
    if(payload)AircraftControls(controls,payload->keys,payload->choices,payload->switchButton,0,false,false);
    const LoadoutDock dock=LoadoutDockOf(text,width,height,s,cells,n,controls.text[0] ? 1 : 0);
    StockVehicleHud(drawer,ctx,text,vp,width,height,s,r,x,lines,at,&dock);
    HudRegionText region(text,s,dock.scale,lines,at);
    LoadoutStrip(drawer,ctx,region.text,width,dock.y,dock.scale,cells,n,lines,at);
    if(controls.text[0] && *at<kMaxLines) {
        Line& keys=lines[(*at)++];keys=controls;
        ControlRow(region.text,width,dock.footer,dock.scale,keys);
    }
    if(switched && r.selected>=0 && r.selected<n)
        LoadoutBanner(drawer,ctx,region.text,width,dock.bannerBottom,dock.scale,cells[r.selected],lines,at);
}

// The game's screen (kUiScreen): 0 x 0 when it is not there to read (hudscale.h then takes the viewport's).
void UiScreen(int* w,int* h) noexcept {
    *w=*h=0;
    if(!uiOk)return;
    __try {
        const void* const holder=At<const void*>(image,kUiScreen);
        const void* const screen=holder ? At<const void*>(holder,kUiScreenObj) : nullptr;
        if(screen){*w=At<std::int32_t>(screen,kUiW);*h=At<std::int32_t>(screen,kUiH);}
    } __except(EXCEPTION_EXECUTE_HANDLER){*w=*h=0;}
}
// The HUD's scale for a viewport `w` x `h` (hudscale.h: the game's screen, the ini's HudScale).
float HudScaleOf(int w,int h) noexcept {
    int uiW,uiH;
    UiScreen(&uiW,&uiH);
    return hudscale::Of(uiW,uiH,w,h,Cfg().hudScale);
}

// What is drawn, logged when it changes (Debug, once in 10 s at most).
void DrawLog(int shown,int panels,const Line* lines,int count,int width,int height) noexcept {
    static int lastShown=-1,lastPanels=-1;
    static ULONGLONG at=0;
    const ULONGLONG now=GetTickCount64();
    if(!Cfg().debug || (shown==lastShown && panels==lastPanels) || now-at<10000)return;
    at=now;lastShown=shown;lastPanels=panels;
    int uiW,uiH;
    UiScreen(&uiW,&uiH);
    Log("HUD draw %dx%d (screen %dx%d, scale %.3f): %d readout(s), %d carrier panel(s), text=%d",width,height,uiW,uiH,
        HudScaleOf(width,height),shown,panels,textOk);
    for(int i=0;i<count && i<12;++i)Log("HUD   \"%ls\" at (%.0f,%.0f) %.0fx%.0f",lines[i].text,lines[i].x,lines[i].y,lines[i].w,lines[i].h);
}
}  // namespace

bool InstallHud() noexcept {
    __try {
        bool quad=true,text=true;
        for(const auto& q:kQuadSigs)quad=quad && Matches(q.rva,q.bytes,q.size);
        for(const auto& t:kTextSigs)text=text && Matches(t.rva,t.bytes,t.size);
        for(const auto& c:kTextCalls)text=text && CallsTo(c[0],c[1]);
        bool ui=true;
        for(const auto& u:kUiSigs)ui=ui && Matches(u.rva,u.bytes,u.size);
        bool lang=CallsTo(0x963724,kLangGet) && CallsTo(0x96372E,kFontLoad);
        for(const auto& l:kLangSigs)lang=lang && Matches(l.rva,l.bytes,l.size);
        quadOk=quad;textOk=quad && text;uiOk=ui;langOk=lang;
        Log("HOOK hud quad=%d text=%d screen=%d language=%d (drawn from the follower gauge's call: HOOK sub gauge=1 needed; screen=0: "
            "sized on the viewport; language=0: HudLanguage auto is English)",quadOk,textOk,uiOk,langOk);
        Log("HUD language: game %d, HudLanguage %d: %s",GameTextLanguage(),Cfg().hudLanguage,
            hudtext::Name(hudtext::Resolve(Cfg().hudLanguage,GameTextLanguage())));
        return quadOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool HudReady() noexcept { return quadOk; }

int GameTextLanguage() noexcept {
    if(!langOk)return -1;
    __try {
        const std::int32_t v=At<std::int32_t>(image,kLangValue);
        return v>=0 && v<=4 ? v : -1;
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}

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
    s.mountedOptic=SightZoomMounted(nullptr);s.turretBinding=PlayerTurretBinding(&s.binding);
    s.payload=PlayerPayload(&s.payloadHud);
    s.stock=PlayerStockHud(&s.stockHud);   // the stock vehicles' HUD (StockVehicleHud; a heli's stores)
    s.warned=WarnLatest(&s.warn);   // the aircraft's warnings (warn.cpp WarnTick, this frame's: it runs first)
    s.seats=PlayerSeatPrompt(&s.seatPrompt);
    s.entrance=PlayerBoardingEntrance(&s.boardingEntrance);
    s.turretCamOk=PlayerTurretCam(&s.turretCam);   // the turret camera (turretcam.cpp): the gun's mark, free look
    s.nix=PlayerNixTorso(&s.nixTorso);   // the Nix's legs and torso (nix.cpp): the stock HUD's hull / turret ring
    s.sazabi=PlayerSazabiCue(&s.sazabiCue);   // the Sazabi's thrusters, weapons, flight, aim and lock (sazabi.cpp; SazabiHud)
    // The stock weapon gauge gives way (stockgauge.cpp, HideStockGauges) where HudDraw lists the vehicle's weapons and
    // their rounds: a plugin aircraft's stores line (CockpitStrip, the old Cockpit, HeliStrip), StockBlock, a stock heli's
    // HeliStrip (HeliFlightHud), the Sazabi's own HUD (SazabiPanel / SazabiSpecial: its rifle, missiles, cannon, funnels;
    // the stock gauge listed its seat's beam rifles and shield missiles and the 506 body's FUEL). Its text must draw: the
    // lists are text.
    const bool heliLists=s.stock && s.stockHud.heli && s.heliFly && !s.cockpit && Cfg().heliFlightHud;
    SetStockGaugeCover(textOk && (s.cockpit || (s.stock && !s.cockpit && !s.stockHud.heli) || heliLists || (s.sazabi && !s.cockpit)));
    s.armor=PlayerStockArmor(&s.armorRo);   // the stock armor gauge's numbers while that cover hides it (stockgauge.cpp)
    s.proteus=PlayerProteus(&s.proteusRo);   // the Proteus's stance, shield, field, launcher (proteus.cpp)
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
pview::Store playerViews{};   // under viewLock
}  // namespace

void KeepPlayerViewProj(const void* camera,const float* viewProj) noexcept {
    if(!camera || !viewProj || MapOwnsView())return;   // as KeepViewProj: the map's camera is not the aim's view
    const void* ref=nullptr;const void* soldier=nullptr;
    __try {
        if(!Readable(camera,0x368))return;
        ref=At<const void*>(camera,0x350);soldier=At<const void*>(camera,0x360);   // map.cpp kCamTargetRef / kCamTarget
    } __except(EXCEPTION_EXECUTE_HANDLER){return;}
    AcquireSRWLockExclusive(&viewLock);
    playerViews.Keep(ref,soldier,viewProj,GetTickCount64());
    ReleaseSRWLockExclusive(&viewLock);
}

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
namespace {
bool RayOf(const float* vp,float* eye,float* dir) noexcept {
    float inv[16];
    if(!Invert4(vp,inv))return false;
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
}  // namespace
bool CameraRay(float* eye,float* dir) noexcept {
    float vp[16];
    return LastViewProj(vp) && RayOf(vp,eye,dir);
}
bool CameraRayOf(const void* human,float* eye,float* dir) noexcept {
    float vp[16];
    AcquireSRWLockShared(&viewLock);
    const bool ok=playerViews.Find(human,GetTickCount64(),vp);
    ReleaseSRWLockShared(&viewLock);
    return ok && RayOf(vp,eye,dir);
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
            const wchar_t* side=Tr(axis==0 ? (k>0 ? Tx::compassW : Tx::compassE) : (k>0 ? Tx::compassN : Tx::compassS));
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
    Label(text,lines,at,cx+dx*(r+14.0f*s),cy+dy*(r+14.0f*s),1,kLineScale*0.8f,kWhite,L"%ls",Tr(Tx::compassN));
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
    const float drawn=std::fmin(len,width*0.40f-20.0f*s);
    const float x=width*0.55f,y=184.0f*s;
    Rect(drawer,ctx,x,y-2.0f*s,x+drawn,y+2.0f*s,kWhite);
    Rect(drawer,ctx,x-1.0f*s,y-8.0f*s,x+1.0f*s,y+8.0f*s,kWhite);
    Rect(drawer,ctx,x+drawn-1.0f*s,y-8.0f*s,x+drawn+1.0f*s,y+8.0f*s,kWhite);
    wchar_t d[24];MapDistance(d,_countof(d),step*drawn/len);
    Label(text,lines,at,x+drawn*0.5f,y-18.0f*s,1,kLineScale*0.75f,kWhite,L"%ls",d);
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
    // A soldier's mark is on its body, no stem (the user, 2026-10-09: "这个图标为什么要在npc的竖直顶上。直接在npc身上不好吗").
    const bool soldier=u.kind==MapKind::squad || u.kind==MapKind::ally;
    float top[3]={u.pos[0],u.pos[1],u.pos[2]},base[3]={u.pos[0],u.pos[1],u.pos[2]};
    if(air)base[1]=u.ground;
    else if(soldier)mapcmd::BodyPoint(u.pos,false,top);
    else top[1]+=pin;
    float depth;
    if(!Project(vp,top,width,height,&p->ix,&p->iy,&p->depth))return false;
    p->stem=(!air || u.pos[1]-u.ground>1.0f) && !soldier && u.kind!=MapKind::lock && Project(vp,base,width,height,&p->bx,&p->by,&depth);
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

// Map panels share a viewport-fitted scale and publish the exact opaque UI regions.
// Text is drawn in a final batch: remove earlier world labels under each panel before that batch.
float mapUiPanels[16*4]{};int mapUiPanelCount=0;
void MapUiBox(void* drawer,void* ctx,float x0,float y0,float x1,float y1,Line* lines,int prior) noexcept {
    for(int i=0;i<prior;++i) {
        auto& line=lines[i];
        if(line.x<x1 && line.x+line.w>x0 && line.y<y1 && line.y+line.h>y0)line.text[0]=0;
    }
    Rect(drawer,ctx,x0,y0,x1,y1,kMapBand);
    if(mapUiPanelCount<16) {
        auto* r=mapUiPanels+4*mapUiPanelCount++;r[0]=x0;r[1]=y0;r[2]=x1;r[3]=y1;
    }
}
void MapFitLabel(Text* text,Line& row,float left,float right) noexcept {
    if(!text || !(row.w>right-left))return;
    const float mid=row.y+row.h*0.5f;
    // Native fonts quantize pixel sizes; remeasure instead of assuming one proportional shrink fits.
    for(int pass=0;pass<4 && row.w>right-left;++pass) {
        row.scale*=std::fmax(0.01f,(right-left)/row.w)*0.96f;MeasureAll(*text,&row,1);
    }
    row.x=std::fmax(left,std::fmin(row.x,right-row.w));row.y=mid-row.h*0.5f;
}

// The NPC commands (mapcmd.cpp, README 地图 → 指挥 NPC): the mouse pointer (where G sends the selection; with a pad a
// crosshair at the screen's centre) and the box being dragged from it, each commandable unit ringed (white brackets:
// selected), a guard order's line from the unit to its point (its slot of the formation) and a ring there, FOLLOW under a
// unit following the player, and a band over the keys: how many are selected, the keys, the last command's word.
alignas(16) const float kMapOrder[4]={0.3f,0.9f,1.0f,1.0f};
alignas(16) const float kMapOrderDim[4]={0.3f,0.9f,1.0f,0.55f};
alignas(16) const float kMapBoxFill[4]={0.3f,0.9f,1.0f,0.08f};
alignas(16) const float kMapLocked[4]={0.6f,0.6f,0.6f,0.6f};   // a squad a mission script drives: shown, takes no order
constexpr float kMapGuardRing=12.0f;      // m: the ring at a guard order's point (formation slots are 30 m apart)
// The squad panel (docs/npc-ai-design.md §6.1), the map's right edge under the title band: a row a squad (the number key
// that picks it, its class, members alive, what it is doing, its order), selected rows white, a script's grey.
const wchar_t* MapOrderWord(Order o) noexcept {
    switch(o) {
    case Order::guard: return Tr(Tx::orderGuard);
    case Order::follow: return Tr(Tx::orderFollow);
    case Order::engage: return Tr(Tx::orderEngage);
    case Order::focus: return Tr(Tx::orderFocus);
    case Order::board: return Tr(Tx::orderBoard);
    case Order::dismount: return Tr(Tx::orderDismount);
    case Order::dismiss: return Tr(Tx::orderDismiss);
    case Order::recruit: return Tr(Tx::orderRecruit);
    case Order::withdraw: return Tr(Tx::orderWithdraw);
    case Order::move: return Tr(Tx::orderMove);
    case Order::attackMove: return Tr(Tx::orderAttackMove);
    case Order::dismountAll: return Tr(Tx::orderDismountAll);
    case Order::none: break;
    }
    return L"-";
}
// SquadCommandUnits supplies "RANGER x4"; translate the identifier, preserving the live member count.
void MapUnitName(const char* name,wchar_t* out,std::size_t size) noexcept {
    const char* count=name ? std::strstr(name," x") : nullptr;
    if(!count){hudtext::WordTo(name,out,size);return;}
    char kind[24]{};const std::size_t n=static_cast<std::size_t>(count-name);
    if(n>=sizeof(kind)){hudtext::WordTo(name,out,size);return;}
    std::memcpy(kind,name,n);wchar_t translated[32];hudtext::WordTo(kind,translated,_countof(translated));
    _snwprintf_s(out,size,_TRUNCATE,L"%ls%hs",translated,count);
}
void MapSquadStatus(const SquadRow& r,wchar_t* out,std::size_t size) noexcept {
    if(std::strncmp(r.status,"WAIT ",5)==0)_snwprintf_s(out,size,_TRUNCATE,Tr(Tx::squadWait),r.cooldown);
    else hudtext::WordTo(r.status,out,size);
}
// The panel's rows (the user, 2026-10-09: "小队太多了吧，怎么处理合适，感觉看不过来"): sorted by SquadRank (the player's,
// the free ones, riding, a script's), folded to the ones on foot that take orders (at most 9: the number keys), the rest
// summed in a row that opens it (mapcmd_logic.h SquadRowsShown). Its bottom (px), for what goes under it; `top` none.
float MapSquadPanel(void* drawer,void* ctx,Text* text,float width,float s,bool pad,const MapCommandReadout& c,Line* lines,int* at) noexcept {
    const float x0=16.0f*s,rowH=22.0f*s,top=56.0f*s;
    if(c.squads<=0){MapCommandSquadButtons(nullptr,nullptr,0);MapCommandSquadFold(nullptr);return top;}
    // Top left, under the title band and over the legend; the compass has the top right.
    int ranks[16];
    const int listed=c.squads<16 ? c.squads : 16;
    for(int i=0;i<listed;++i)ranks[i]=c.squad[i].rank;
    const int rows=mapcmd::SquadRowsShown(ranks,listed,c.squadOpen,16);
    const int foldedRows=mapcmd::SquadRowsShown(ranks,listed,false,16);
    const int total=c.squadTally.total>listed ? c.squadTally.total : listed;
    const bool fold=c.squadOpen ? foldedRows<total : rows<total;
    if(*at+rows+2>kMaxLines){MapCommandSquadButtons(nullptr,nullptr,0);MapCommandSquadFold(nullptr);return top;}
    const int first=*at;
    Line& title=lines[(*at)++];Format(title,L"%ls",Tr(pad ? Tx::squadTitle : Tx::squadTitleKeys));
    title.rgba=kMapOrder;
    for(int i=0;i<rows;++i) {
        const SquadRow& r=c.squad[i];
        const float* tint=r.locked ? kMapLocked : c.squadSelected[i] ? kWhite : kMapOrder;
        wchar_t key[4]=L" ";
        if(i<mapcmd::kSquadRowsFolded)_snwprintf_s(key,_countof(key),_TRUNCATE,L"%d",i+1);
        wchar_t name[32],status[32];hudtext::WordTo(r.name,name,_countof(name));MapSquadStatus(r,status,_countof(status));
        Line& row=lines[(*at)++];Format(row,L"%ls  %ls x%d   %ls   %ls",key,name,r.alive,status,MapOrderWord(r.now.order));row.rgba=tint;
    }
    if(fold) {
        Line& more=lines[(*at)++];
        if(c.squadOpen)Format(more,L"%ls",Tr(Tx::squadFold));
        else Format(more,Tr(Tx::squadMore),total-rows,c.squadTally.riding,c.squadTally.scripted);
        more.rgba=kMapOrderDim;
    }
    float panelW=0.0f;
    for(int i=first;i<*at;++i) {
        Line& row=lines[i];row.scale=kLineScale*0.7f;row.w=row.h=0;
        if(text)MeasureAll(*text,&row,1);
        const float most=std::fmin(520.0f*s,width*0.46f-32.0f*s);
        if(row.w>most && text){row.scale*=most/row.w;MeasureAll(*text,&row,1);}
        row.x=x0;row.y=top+rowH*static_cast<float>(i-first);
        panelW=std::fmax(panelW,row.w);
    }
    const int drawn=*at-first;   // the title, the rows, the summary
    const float left=x0-8.0f*s,right=x0+panelW+8.0f*s,bottom=top+rowH*static_cast<float>(drawn)+4.0f*s;
    MapUiBox(drawer,ctx,left,top-6.0f*s,right,bottom,lines,first);
    float rects[16*4]{};ObjRef identities[16]{};
    for(int i=0;i<rows;++i) {
        const auto& row=lines[first+i+1];auto* hit=rects+i*4;
        hit[0]=left;hit[1]=row.y;hit[2]=right;hit[3]=row.y+rowH;
        identities[i]=c.squad[i].identity;
        if(c.pointer && c.px>=left && c.px<right && c.py>=hit[1] && c.py<hit[3])
            Rect(drawer,ctx,left,hit[1],right,hit[3],kMapBoxFill);
    }
    MapCommandSquadButtons(pad ? nullptr : rects,pad ? nullptr : identities,pad ? 0 : rows);
    if(fold && !pad) {
        const auto& row=lines[first+rows+1];
        const float hit[4]={left,row.y,right,row.y+rowH};
        if(c.pointer && c.px>=left && c.px<right && c.py>=hit[1] && c.py<hit[3])Rect(drawer,ctx,left,hit[1],right,hit[3],kMapBoxFill);
        MapCommandSquadFold(hit);
    } else MapCommandSquadFold(nullptr);
    return bottom;
}

// A map-only interactive loadout keeps left-click firing and camera aim untouched outside M.
void MapPayloadPanel(void* drawer,void* ctx,Text* text,float width,float s,bool pad,
                     const MapCommandReadout& commands,Line* lines,int* at) noexcept {
    PayloadReadout r{};
    if(!PlayerSelectablePayload(&r) || r.count<=0){MapCommandPayloadButtons(nullptr,0,0,nullptr,0);return;}
    const int count=r.count<kMostPayload ? r.count : kMostPayload;
    const float right=width-16.0f*s,left=right-std::fmin(460.0f*s,width*0.46f-32.0f*s),top=216.0f*s,rowH=30.0f*s;
    MapUiBox(drawer,ctx,left-8.0f*s,top-14.0f*s,right+8.0f*s,top+rowH*static_cast<float>(count+1),lines,*at);
    Label(text,lines,at,left,top,0,kLineScale*0.75f,kWhite,L"%ls",Tr(Tx::mapPayloadTitle));
    if(*at)MapFitLabel(text,lines[*at-1],left,right);
    float rects[kMostPayload*4]{};int entries[kMostPayload]{};int hits=0;
    for(int i=0;i<count;++i) {
        const auto& e=r.entry[i];const float y=top+rowH*static_cast<float>(i+1);
        const bool hover=!pad && commands.pointer && commands.px>=left && commands.px<right && commands.py>=y-12*s && commands.py<y+12*s;
        if(e.picked || (e.selectable && hover))Rect(drawer,ctx,left-3*s,y-12*s,right+3*s,y+12*s,kMapBoxFill);
        Label(text,lines,at,left+10*s,y,0,kLineScale*0.70f,e.selectable ? kWhite : kMapLocked,L"%ls  %d/%d",e.name,e.rounds,e.capacity);
        if(*at)MapFitLabel(text,lines[*at-1],left+10*s,right);
        if(e.picked)Rect(drawer,ctx,left,y-7*s,left+3*s,y+7*s,kCyan);
        if(!pad && e.selectable && r.selectionToken) {
            auto* hit=rects+4*hits;hit[0]=left;hit[1]=y-12*s;hit[2]=right;hit[3]=y+12*s;entries[hits++]=i;
        }
    }
    MapCommandPayloadButtons(rects,r.selectionToken,r.seat,entries,hits);
}

// The map's tooltip (the user, 2026-10-09: "m里面显示太复杂了"): a button's full word and its key are not on the
// button but in a box by the pointer while it is over it, drawn over everything else (MapScreen, last).
struct MapTip { bool on; float x,y; wchar_t text[160]; };
MapTip mapTip{};
void MapTipSet(const MapCommandReadout& c,float x0,float y0,float x1,float y1,const wchar_t* format,...) noexcept {
    if(!c.pointer || c.px<x0 || c.px>=x1 || c.py<y0 || c.py>=y1)return;
    va_list args;va_start(args,format);
    _vsnwprintf_s(mapTip.text,_countof(mapTip.text),_TRUNCATE,format,args);
    va_end(args);
    mapTip.on=true;mapTip.x=c.px;mapTip.y=c.py;
}
void MapTipDraw(void* drawer,void* ctx,Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    if(!mapTip.on || *at>=kMaxLines)return;
    Line& l=lines[*at];
    Format(l,L"%ls",mapTip.text);l.scale=kLineScale*0.7f;l.rgba=kWhite;l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
    if(!(l.w>0.0f)){l.w=static_cast<float>(wcslen(l.text))*9.0f*s;l.h=18.0f*s;}
    const float pad=6.0f*s;
    float x=mapTip.x+18.0f*s,y=mapTip.y-l.h-22.0f*s;   // over the pointer and to its right
    if(x+l.w+2.0f*pad>width-4.0f*s)x=width-4.0f*s-l.w-2.0f*pad;
    if(x<4.0f*s)x=4.0f*s;
    if(y<50.0f*s)y=mapTip.y+24.0f*s;
    if(y+l.h+2.0f*pad>height)y=height-l.h-2.0f*pad;
    MapUiBox(drawer,ctx,x,y,x+l.w+2.0f*pad,y+l.h+2.0f*pad,lines,*at);
    l.x=x+pad;l.y=y+pad;
    ++*at;
}

// The command card's and the support bar's icons: small line drawings in a box `h` px across round (x, y), from the
// HUD's own primitives (Seg, Tri, Arc, Rect).
constexpr float kIconChevrons[]={-0.75f,-0.2f},kIconWheels3[]={-0.55f,0.0f,0.55f},kIconWheels2[]={-0.55f,0.55f};
void MapOrderIcon(void* d,void* c,mapbtn::Id id,float x,float y,float h,float s,const float* rgba) noexcept {
    using mapbtn::Id;
    const float t=1.8f*s,r=h*0.5f;
    switch(id) {
    case Id::move:          // an arrow up and right
        Seg(d,c,x-r*0.8f,y+r*0.8f,x+r*0.3f,y-r*0.3f,t,rgba);Tri(d,c,x+r*0.1f,y-r*0.1f,x+r*0.85f,y-r*0.85f,r*0.45f,rgba);break;
    case Id::attackMove:    // the arrow over a cross
        Seg(d,c,x-r*0.8f,y+r*0.8f,x+r*0.3f,y-r*0.3f,t,rgba);Tri(d,c,x+r*0.1f,y-r*0.1f,x+r*0.85f,y-r*0.85f,r*0.45f,rgba);
        Seg(d,c,x-r*0.9f,y-r*0.1f,x-r*0.1f,y+r*0.9f,t,kAmber);Seg(d,c,x-r*0.9f,y+r*0.9f,x-r*0.1f,y+r*0.1f,t,kAmber);break;
    case Id::guard:         // a shield
        Seg(d,c,x-r*0.75f,y-r*0.8f,x+r*0.75f,y-r*0.8f,t,rgba);Seg(d,c,x-r*0.75f,y-r*0.8f,x-r*0.75f,y,t,rgba);
        Seg(d,c,x+r*0.75f,y-r*0.8f,x+r*0.75f,y,t,rgba);Seg(d,c,x-r*0.75f,y,x,y+r*0.9f,t,rgba);Seg(d,c,x+r*0.75f,y,x,y+r*0.9f,t,rgba);break;
    case Id::follow:        // two chevrons towards a dot (the player)
        for(const float o:kIconChevrons){Seg(d,c,x+o*r,y-r*0.6f,x+(o+0.4f)*r,y,t,rgba);Seg(d,c,x+(o+0.4f)*r,y,x+o*r,y+r*0.6f,t,rgba);}
        Rect(d,c,x+r*0.45f,y-r*0.25f,x+r*0.95f,y+r*0.25f,rgba);break;
    case Id::release:       // an open ring
        Arc(d,c,x,y,r*0.75f,-0.9f,kTurn-1.0f,t,14,rgba);break;
    case Id::engage:        // a crosshair
        Arc(d,c,x,y,r*0.6f,0.0f,kTurn,t,14,rgba);
        Seg(d,c,x-r,y,x-r*0.3f,y,t,rgba);Seg(d,c,x+r*0.3f,y,x+r,y,t,rgba);Seg(d,c,x,y-r,x,y-r*0.3f,t,rgba);Seg(d,c,x,y+r*0.3f,x,y+r,t,rgba);break;
    case Id::focus:         // a bullseye
        Arc(d,c,x,y,r*0.85f,0.0f,kTurn,t,16,kAmber);Arc(d,c,x,y,r*0.45f,0.0f,kTurn,t,12,kAmber);
        Rect(d,c,x-r*0.15f,y-r*0.15f,x+r*0.15f,y+r*0.15f,kAmber);break;
    case Id::board:         // into a box
    case Id::dismount:      // out of it
    case Id::dismountAll:   // out of it, two of them
        Seg(d,c,x-r*0.2f,y-r*0.8f,x+r*0.9f,y-r*0.8f,t,rgba);Seg(d,c,x+r*0.9f,y-r*0.8f,x+r*0.9f,y+r*0.8f,t,rgba);
        Seg(d,c,x+r*0.9f,y+r*0.8f,x-r*0.2f,y+r*0.8f,t,rgba);
        if(id==Id::board){Seg(d,c,x-r,y,x+r*0.1f,y,t,rgba);Tri(d,c,x-r*0.1f,y,x+r*0.5f,y,r*0.4f,rgba);}
        else{Seg(d,c,x+r*0.5f,y,x-r*0.4f,y,t,rgba);Tri(d,c,x-r*0.3f,y,x-r,y,r*0.4f,rgba);}
        if(id==Id::dismountAll){Seg(d,c,x+r*0.5f,y-r*0.45f,x-r*0.4f,y-r*0.45f,t,kAmber);Tri(d,c,x-r*0.3f,y-r*0.45f,x-r,y-r*0.45f,r*0.3f,kAmber);}
        break;
    case Id::withdraw:      // out of the box and away (the transport leaves the field)
        Seg(d,c,x-r*0.9f,y-r*0.8f,x+r*0.1f,y-r*0.8f,t,rgba);Seg(d,c,x-r*0.9f,y-r*0.8f,x-r*0.9f,y+r*0.8f,t,rgba);
        Seg(d,c,x-r*0.9f,y+r*0.8f,x+r*0.1f,y+r*0.8f,t,rgba);
        Seg(d,c,x-r*0.4f,y,x+r*0.5f,y,t,kAmber);Tri(d,c,x+r*0.3f,y,x+r,y,r*0.4f,kAmber);
        break;
    case Id::dismiss:       // a head and a minus
    case Id::recruit:       // a head and a plus
        Arc(d,c,x-r*0.35f,y-r*0.35f,r*0.35f,0.0f,kTurn,t,10,rgba);Seg(d,c,x-r*0.95f,y+r*0.8f,x+r*0.25f,y+r*0.8f,t,rgba);
        Seg(d,c,x-r*0.95f,y+r*0.8f,x-r*0.35f,y+r*0.15f,t,rgba);Seg(d,c,x+r*0.25f,y+r*0.8f,x-r*0.35f,y+r*0.15f,t,rgba);
        Seg(d,c,x+r*0.35f,y-r*0.4f,x+r,y-r*0.4f,t,id==Id::recruit ? kGreen : kAmber);
        if(id==Id::recruit)Seg(d,c,x+r*0.675f,y-r*0.75f,x+r*0.675f,y-r*0.05f,t,kGreen);
        break;
    case Id::formation:     // a wedge of dots
    {
        static const float kDots[5][2]={{0.0f,-0.6f},{-0.6f,0.1f},{0.6f,0.1f},{-1.0f,0.75f},{1.0f,0.75f}};
        for(const auto& p:kDots) {
            const float px=x+p[0]*r*0.85f,py=y+p[1]*r;Rect(d,c,px-r*0.18f,py-r*0.18f,px+r*0.18f,py+r*0.18f,rgba);
        }
        break;
    }
    case Id::split:         // a fork
        Seg(d,c,x,y+r*0.9f,x,y,t,rgba);Seg(d,c,x,y,x-r*0.7f,y-r*0.8f,t,rgba);Seg(d,c,x,y,x+r*0.7f,y-r*0.8f,t,rgba);break;
    case Id::merge:         // two lines joining
        Seg(d,c,x-r*0.7f,y+r*0.8f,x,y,t,rgba);Seg(d,c,x+r*0.7f,y+r*0.8f,x,y,t,rgba);Seg(d,c,x,y,x,y-r*0.9f,t,rgba);break;
    case Id::sweep:         // a crate
        Seg(d,c,x-r*0.8f,y-r*0.6f,x+r*0.8f,y-r*0.6f,t,rgba);Seg(d,c,x+r*0.8f,y-r*0.6f,x+r*0.8f,y+r*0.7f,t,rgba);
        Seg(d,c,x+r*0.8f,y+r*0.7f,x-r*0.8f,y+r*0.7f,t,rgba);Seg(d,c,x-r*0.8f,y+r*0.7f,x-r*0.8f,y-r*0.6f,t,rgba);
        Seg(d,c,x-r*0.8f,y-r*0.6f,x+r*0.8f,y+r*0.7f,t*0.7f,rgba);Seg(d,c,x+r*0.8f,y-r*0.6f,x-r*0.8f,y+r*0.7f,t*0.7f,rgba);break;
    case Id::health:        // a cross
        Rect(d,c,x-r*0.25f,y-r*0.8f,x+r*0.25f,y+r*0.8f,kGreen);Rect(d,c,x-r*0.8f,y-r*0.25f,x+r*0.8f,y+r*0.25f,kGreen);break;
    case Id::count: break;
    }
}
// MapAircraft's drawing reaches kAircraftReach of its own units from its centre (the heli's tail boom, the plane's nose):
// an icon `h` px across draws it at h / 2 / kAircraftReach. `h` is already in screen px (the caller's kSupIcon * s): the
// HUD scale must not go in a second time (the user, 2026-10-09, a 4K screenshot: "左边的重叠了" -- at s 2 the aircraft
// were drawn at s * s, twice their row, over the rows round them).
constexpr float kAircraftReach=13.0f;
void MapSupportIcon(void* d,void* c,SupportIcon icon,float x,float y,float h,float s,const float* rgba) noexcept {
    const float t=1.8f*s,r=h*0.5f,plane=r/kAircraftReach;
    auto heads=[&](int n){
        for(int i=0;i<n;++i) {
            const float px=x+(static_cast<float>(i%3)-1.0f)*r*0.65f,py=y+(n>3 ? (i<3 ? -0.4f : 0.45f) : 0.0f)*r;
            Arc(d,c,px,py,r*0.22f,0.0f,kTurn,t*0.8f,8,rgba);
        }
    };
    switch(icon) {
    case SupportIcon::jet: MapAircraft(d,c,x,y,plane,false,0.0f,-1.0f,rgba);break;
    case SupportIcon::heli: MapAircraft(d,c,x,y,plane,true,0.0f,-1.0f,rgba);break;
    case SupportIcon::carrier: MapBox(d,c,x,y,r*0.9f,t,rgba);MapBox(d,c,x,y,r*0.45f,t,rgba);break;
    case SupportIcon::gunship:
        MapAircraft(d,c,x,y,plane,false,0.0f,-1.0f,rgba);Arc(d,c,x,y,r*0.95f,0.0f,kTurn,t*0.7f,16,kAmber);break;
    case SupportIcon::sub:
        Arc(d,c,x-r*0.45f,y+r*0.2f,r*0.4f,kTurn*0.25f,kTurn*0.5f,t,8,rgba);Arc(d,c,x+r*0.45f,y+r*0.2f,r*0.4f,-kTurn*0.25f,kTurn*0.5f,t,8,rgba);
        Seg(d,c,x-r*0.45f,y-r*0.2f,x+r*0.45f,y-r*0.2f,t,rgba);Seg(d,c,x-r*0.45f,y+r*0.6f,x+r*0.45f,y+r*0.6f,t,rgba);
        Rect(d,c,x-r*0.15f,y-r*0.65f,x+r*0.2f,y-r*0.2f,rgba);break;
    case SupportIcon::squad: heads(3);break;
    case SupportIcon::platoon: heads(6);break;
    case SupportIcon::tank:
        Seg(d,c,x-r*0.9f,y+r*0.5f,x+r*0.9f,y+r*0.5f,t,rgba);Seg(d,c,x-r*0.9f,y+r*0.5f,x-r*0.9f,y,t,rgba);
        Seg(d,c,x+r*0.9f,y+r*0.5f,x+r*0.9f,y,t,rgba);Seg(d,c,x-r*0.9f,y,x+r*0.9f,y,t,rgba);
        Rect(d,c,x-r*0.4f,y-r*0.4f,x+r*0.3f,y,rgba);Seg(d,c,x+r*0.2f,y-r*0.25f,x+r,y-r*0.45f,t,rgba);break;
    case SupportIcon::apc:
        Seg(d,c,x-r*0.9f,y+r*0.3f,x+r*0.9f,y+r*0.3f,t,rgba);Seg(d,c,x-r*0.9f,y+r*0.3f,x-r*0.9f,y-r*0.5f,t,rgba);
        Seg(d,c,x+r*0.9f,y+r*0.3f,x+r*0.9f,y-r*0.2f,t,rgba);Seg(d,c,x-r*0.9f,y-r*0.5f,x+r*0.5f,y-r*0.5f,t,rgba);
        Seg(d,c,x+r*0.5f,y-r*0.5f,x+r*0.9f,y-r*0.2f,t,rgba);
        for(const float o:kIconWheels3)Arc(d,c,x+o*r,y+r*0.55f,r*0.2f,0.0f,kTurn,t*0.8f,8,rgba);
        break;
    case SupportIcon::truck:
        Rect(d,c,x+r*0.25f,y-r*0.45f,x+r*0.9f,y+r*0.3f,rgba);
        Seg(d,c,x-r*0.9f,y+r*0.3f,x+r*0.2f,y+r*0.3f,t,rgba);Seg(d,c,x-r*0.9f,y+r*0.3f,x-r*0.9f,y-r*0.15f,t,rgba);
        for(const float o:kIconWheels2)Arc(d,c,x+o*r,y+r*0.55f,r*0.22f,0.0f,kTurn,t*0.8f,8,rgba);
        break;
    }
}
// A variant chip's icon (support_call.h SupportCallVariant: the catalog's own data, never its words): guarding a point,
// following the player, a crew aboard, an empty vehicle delivered, the vehicle a plane drops.
void MapVariantIcon(void* d,void* c,SupportVariant variant,float x,float y,float h,float s,const float* rgba) noexcept {
    switch(variant) {
    case SupportVariant::guard: MapOrderIcon(d,c,mapbtn::Id::guard,x,y,h,s,rgba);break;
    case SupportVariant::follow: MapOrderIcon(d,c,mapbtn::Id::follow,x,y,h,s,rgba);break;
    case SupportVariant::empty: MapBox(d,c,x,y,h*0.4f,1.6f*s,rgba);break;
    case SupportVariant::crewed:
        Arc(d,c,x,y-h*0.15f,h*0.18f,0.0f,kTurn,1.6f*s,8,rgba);Seg(d,c,x-h*0.3f,y+h*0.35f,x+h*0.3f,y+h*0.35f,1.6f*s,rgba);break;
    case SupportVariant::squad: MapSupportIcon(d,c,SupportIcon::squad,x,y,h,s,rgba);break;
    case SupportVariant::platoon: MapSupportIcon(d,c,SupportIcon::platoon,x,y,h,s,rgba);break;
    case SupportVariant::tank: MapSupportIcon(d,c,SupportIcon::tank,x,y,h,s,rgba);break;
    case SupportVariant::apc: MapSupportIcon(d,c,SupportIcon::apc,x,y,h,s,rgba);break;
    case SupportVariant::truck: MapSupportIcon(d,c,SupportIcon::truck,x,y,h,s,rgba);break;
    case SupportVariant::none: Rect(d,c,x-h*0.12f,y-h*0.12f,x+h*0.12f,y+h*0.12f,rgba);break;
    }
}

// The command card (map_buttons.h; the user, 2026-10-09: "可以参考各大rts游戏"): an icon and a short word for each order
// the selection takes, the squads' tools with squads selected, the sweep and its switch; lit: the armed point order,
// the sweep going, health boxes for the hurt. The key and the full word in the button's tooltip. The rectangles drawn go
// to mapcmd.cpp (MapCommandButtons), which takes a click on one for the button. With a pad: none (its keys). The rows
// it took (Flow's), for what is laid out over it.
alignas(16) const float kBtnFill[4]={0.03f,0.05f,0.06f,0.78f};
alignas(16) const float kBtnLit[4]={0.10f,0.42f,0.50f,0.90f};
constexpr float kBtnRowH=30.0f,kBtnGap=6.0f,kBtnPad=8.0f,kBtnIcon=18.0f,kBtnMargin=16.0f,kBtnBottom=142.0f;   // px at 1080 lines
// The formation button's menu (map_buttons.h MenuColumn; the user, 2026-10-09: "这个编队应该点击以后展开选择里面的东西"):
// over the button, a row a shape -- a selected guarding squad's defences, the march of the squads following the player --
// the shape in use lit; a click on a row is that shape (mapcmd.cpp PickFormation).
void MapFormationMenu(void* drawer,void* ctx,Text* text,float width,float height,float s,const MapCommandReadout& c,
                      const float* buttons,const int* ids,int n,Line* lines,int* at) noexcept {
    int button=-1;
    for(int i=0;i<n;++i)if(ids[i]==static_cast<int>(mapbtn::Id::formation))button=i;
    if(!c.formationMenu || button<0){MapCommandFormationButtons(nullptr,nullptr,0);return;}
    int entries[kMapFormationEntries];int count=0;
    if(c.formationGuard)for(const auto shape:npc::formation::kGuard)if(count<kMapFormationEntries)entries[count++]=mapbtn::MenuEntry(true,static_cast<int>(shape));
    if(c.formationMarch)for(const auto shape:npc::formation::kMarch)if(count<kMapFormationEntries)entries[count++]=mapbtn::MenuEntry(false,static_cast<int>(shape));
    const float scale=kLineScale*0.7f,pad=kBtnPad*s,rowH=26.0f*s,gap=2.0f*s;
    wchar_t word[kMapFormationEntries][64];float rowW=120.0f*s;
    for(int i=0;i<count;++i) {
        _snwprintf_s(word[i],_countof(word[i]),_TRUNCATE,L"%ls  %ls",Tr(mapbtn::MenuGuard(entries[i]) ? Tx::menuDefence : Tx::menuMarch),
                     FormationText(mapbtn::MenuShape(entries[i])));
        Line probe{};Format(probe,L"%ls",word[i]);probe.scale=scale;
        if(text)MeasureAll(*text,&probe,1);
        const float w=(text ? probe.w : static_cast<float>(wcslen(probe.text))*9.0f*s)+2.0f*pad;
        rowW=std::fmax(rowW,w);
    }
    // Over the whole card (its top row's top), at the formation button's left: a card of more rows than one (a narrow
    // screen, the transport's WITHDRAW making one more) has buttons over the formation button the menu would cover.
    float top=buttons[button*4+1];
    for(int i=0;i<n;++i)top=std::fmin(top,buttons[i*4+1]);
    const mapbtn::Rect at0{buttons[button*4],top,buttons[button*4+2],top+(buttons[button*4+3]-buttons[button*4+1])};
    mapbtn::Rect row[kMapFormationEntries]{};
    const int placed=mapbtn::MenuColumn(at0,count,rowW,rowH,gap,width,height,row);
    if(placed<=0){MapCommandFormationButtons(nullptr,nullptr,0);return;}
    MapUiBox(drawer,ctx,row[0].x0-4.0f*s,row[0].y0-4.0f*s,row[0].x1+4.0f*s,row[placed-1].y1+4.0f*s,lines,*at);
    float rects[kMapFormationEntries*4];
    for(int i=0;i<placed;++i) {
        const mapbtn::Rect& q=row[i];
        const int shape=mapbtn::MenuShape(entries[i]);
        const bool lit=mapbtn::MenuGuard(entries[i]) ? shape==c.formationGuardShape : shape==c.march;
        const bool hover=c.pointer && c.px>=q.x0 && c.px<q.x1 && c.py>=q.y0 && c.py<q.y1;
        Rect(drawer,ctx,q.x0,q.y0,q.x1,q.y1,lit ? kBtnLit : hover ? kMapBoxFill : kBtnFill);
        Label(text,lines,at,q.x0+pad,(q.y0+q.y1)*0.5f,0,scale,lit ? kWhite : kMapOrder,L"%ls",word[i]);
        if(*at>0)MapFitLabel(text,lines[*at-1],q.x0+pad,q.x1-pad);
        rects[i*4]=q.x0;rects[i*4+1]=q.y0;rects[i*4+2]=q.x1;rects[i*4+3]=q.y1;
    }
    MapCommandFormationButtons(rects,entries,placed);
}

int MapButtons(void* drawer,void* ctx,Text* text,float width,float height,float s,bool pad,const MapCommandReadout& c,Line* lines,
               int* at) noexcept {
    using mapbtn::Id;
    if(pad){MapCommandButtons(nullptr,nullptr,0);MapCommandFormationButtons(nullptr,nullptr,0);return 0;}
    constexpr int n=mapbtn::kCount;
    static const Tx kWord[n]={Tx::orderMove,Tx::orderAttackMove,Tx::orderGuard,Tx::orderFollow,Tx::orderRelease,Tx::orderEngage,
                              Tx::orderFocus,Tx::orderBoard,Tx::orderDismount,Tx::orderDismiss,Tx::orderRecruit,Tx::orderWithdraw,
                              Tx::orderDismountAll,Tx::btnFormationShort,Tx::btnSplit,Tx::btnMerge,Tx::btnSweep,Tx::btnHealth};
    const wchar_t* const kKey[n]={L"",L"Z / G",L"G",L"V",L"X",L"J",L"H",L"B",L"N",L"K",L"U",L"",Tr(Tx::keyDismountAll),L"T",L"P",L"L",L"Y",
                                  L"O"};
    const std::uint32_t orders=c.allowed && c.selected>0 ? c.allowedOrders : 0u;
    const bool tools=c.allowed && c.squadToolsAllowed && c.selectedSquads>0;
    const float scale=kLineScale*0.7f;
    int shownIds[n];float w[n];int shown=0;
    const wchar_t* word[n]{};
    for(const Id b:mapbtn::kCardOrder) {
        const int i=static_cast<int>(b);
        if(!mapbtn::Shown(b,orders,tools))continue;
        word[shown]=b==Id::sweep && c.sweepOn ? Tr(Tx::btnSweepStop) : Tr(kWord[i]);
        Line probe{};Format(probe,L"%ls",word[shown]);probe.scale=scale;
        if(text)MeasureAll(*text,&probe,1);
        const float textW=text ? probe.w : static_cast<float>(wcslen(probe.text))*9.0f*s;
        w[shown]=(2.0f*kBtnPad+kBtnIcon+6.0f)*s+textW;shownIds[shown++]=i;
    }
    mapbtn::Rect r[n]{};
    const int rows=mapbtn::Flow(w,shown,width,height-kBtnBottom*s,kBtnRowH*s,kBtnGap*s,kBtnMargin*s,r);
    if(rows>0)MapUiBox(drawer,ctx,0,height-(kBtnBottom+static_cast<float>(rows)*(kBtnRowH+kBtnGap)+8.0f)*s,width,height-104.0f*s,lines,*at);
    float rects[n*4];int placed=0,ids[n];
    for(int k=0;k<shown;++k) {
        const mapbtn::Rect& q=r[k];
        if(!(q.x1>q.x0))continue;   // no room for its row
        const Id b=static_cast<Id>(shownIds[k]);
        const bool lit=(c.armedOrder && mapbtn::IsOrder(b) && mapbtn::Arms(b) && c.armed==mapbtn::OrderOf(b)) ||
                       (b==Id::sweep && c.sweepOn) || (b==Id::health && c.healthOn);
        const bool hover=c.pointer && c.px>=q.x0 && c.px<q.x1 && c.py>=q.y0 && c.py<q.y1;
        Rect(drawer,ctx,q.x0,q.y0,q.x1,q.y1,lit ? kBtnLit : hover ? kMapBoxFill : kBtnFill);
        const float t=1.5f*s;
        Seg(drawer,ctx,q.x0,q.y0,q.x1,q.y0,t,kMapOrder);Seg(drawer,ctx,q.x1,q.y0,q.x1,q.y1,t,kMapOrder);
        Seg(drawer,ctx,q.x1,q.y1,q.x0,q.y1,t,kMapOrder);Seg(drawer,ctx,q.x0,q.y1,q.x0,q.y0,t,kMapOrder);
        const float iconX=q.x0+(kBtnPad+kBtnIcon*0.5f)*s,mid=(q.y0+q.y1)*0.5f;
        MapOrderIcon(drawer,ctx,b,iconX,mid,kBtnIcon*s,s,kWhite);
        Label(text,lines,at,q.x0+(kBtnPad+kBtnIcon+6.0f)*s,mid,0,scale,kWhite,L"%ls",word[k]);
        if(*at>0)MapFitLabel(text,lines[*at-1],q.x0+(kBtnPad+kBtnIcon+6.0f)*s,q.x1-kBtnPad*s);
        // The tooltip: the full word (the formation's name, the health boxes' state) and how to give it.
        wchar_t full[96];
        if(b==Id::formation)_snwprintf_s(full,_countof(full),_TRUNCATE,Tr(Tx::btnFormation),FormationText(c.march));
        else if(b==Id::health)_snwprintf_s(full,_countof(full),_TRUNCATE,L"%ls",Tr(c.healthOn ? Tx::btnHealthOn : Tx::btnHealthOff));
        else _snwprintf_s(full,_countof(full),_TRUNCATE,L"%ls",word[k]);
        if(b==Id::move)MapTipSet(c,q.x0,q.y0,q.x1,q.y1,Tr(Tx::tipRightClick),full);
        else if(mapbtn::Arms(b))MapTipSet(c,q.x0,q.y0,q.x1,q.y1,Tr(Tx::tipArms),full,kKey[shownIds[k]]);
        else MapTipSet(c,q.x0,q.y0,q.x1,q.y1,Tr(Tx::tipKey),full,kKey[shownIds[k]]);
        rects[placed*4]=q.x0;rects[placed*4+1]=q.y0;rects[placed*4+2]=q.x1;rects[placed*4+3]=q.y1;ids[placed++]=shownIds[k];
    }
    MapCommandButtons(rects,ids,placed);
    MapFormationMenu(drawer,ctx,text,width,height,s,c,rects,ids,placed,lines,at);
    return rows;
}

// The support bar (map_buttons.h GroupSupport / Column; the user, 2026-10-09: "m里面的支援招募弄成一列带图标的hud并且可以
// 点击操作吧"): the map's left edge under the squad panel, a row a kind of support (its icon and name), its variants as
// chips at the row's right; over them its title and whether the dispatcher takes a call now (dim when it does not). A
// click on a row or chip arms that call (lit), the next left click on the map is its point; its tooltip the full name.
constexpr float kSupRowH=26.0f,kSupGap=3.0f,kSupW=250.0f,kSupIcon=18.0f;   // px at 1080 lines
// The composition panel's words (map_buttons.h ComposePanel): a soldier kind's short name (SupportWeapon order), a class's.
constexpr Tx kKindText[kSupportWeaponCount]={Tx::kindRifle,Tx::kindFlame,Tx::kindRocket,Tx::kindShotgun,Tx::kindSniper,
    Tx::kindLance,Tx::kindLaser,Tx::kindMonster,Tx::kindIzuna,Tx::kindThunderBow,
    Tx::kindCannon,Tx::kindMiddleCannon,Tx::kindPileBanker,Tx::kindFencerShotgun};
constexpr Tx kClassText[mapbtn::kComposeClasses]={Tx::classRanger,Tx::classWingDiver,Tx::classFencer};
const wchar_t* KindText(SupportWeapon w) noexcept {
    const int i=static_cast<int>(w);
    return Tr(i>=0 && i<kSupportWeaponCount ? kKindText[i] : Tx::kindRifle);
}
// The armed support's composition beside the bar (the user, 2026-10-09: "支援栏是断剑那种，先点载具，然后选里面的人并且可以
// 点多次，直到座位满"; a click a whole squad): the seats used over the seats in its title, its squads in their places (each
// its kind; a click takes it out), the squad kinds a class a row (a click puts one more in; dim once no squad fits).
void MapComposePanel(void* drawer,void* ctx,Text* text,float s,const MapCommandReadout& c,const wchar_t* name,float x0,float top,
                     float ceiling,float bottom,Line* lines,int* at) noexcept {
    if(c.composeEntry<0 || c.composeSeats<=0){MapCommandComposeButtons(nullptr,nullptr,0);return;}
    const auto L=mapbtn::ComposePanel(x0,top,ceiling,bottom,c.composeSeats,s);
    MapUiBox(drawer,ctx,L.box.x0,L.box.y0,L.box.x1,L.box.y1,lines,*at);
    const auto& load=c.compose;
    const bool room=mapbtn::ComposeRoom(load,c.composeSeats);
    Label(text,lines,at,L.title.x0,(L.title.y0+L.title.y1)*0.5f,0,kLineScale*0.7f,kWhite,Tr(Tx::composeSeats),name,load.count,
          c.composeSeats<kSupportLoadoutMost ? c.composeSeats : kSupportLoadoutMost);
    if(*at>0)MapFitLabel(text,lines[*at-1],L.title.x0,L.title.x1);
    float rects[mapbtn::kComposeItems*4];int codes[mapbtn::kComposeItems];int hits=0;
    auto hit=[&](const mapbtn::Rect& q,int code){
        if(hits>=mapbtn::kComposeItems)return;
        rects[hits*4]=q.x0;rects[hits*4+1]=q.y0;rects[hits*4+2]=q.x1;rects[hits*4+3]=q.y1;codes[hits++]=code;
    };
    const float t=1.2f*s;
    auto frame=[&](const mapbtn::Rect& q,const float* edge){
        Seg(drawer,ctx,q.x0,q.y0,q.x1,q.y0,t,edge);Seg(drawer,ctx,q.x1,q.y0,q.x1,q.y1,t,edge);
        Seg(drawer,ctx,q.x1,q.y1,q.x0,q.y1,t,edge);Seg(drawer,ctx,q.x0,q.y1,q.x0,q.y0,t,edge);
    };
    const int squads=mapbtn::ComposeSquads(load);
    for(int j=0;j<L.slots;++j) {
        const mapbtn::Rect& q=L.slot[j];
        const bool taken=j<squads;
        const bool hover=taken && c.pointer && c.px>=q.x0 && c.px<q.x1 && c.py>=q.y0 && c.py<q.y1;
        Rect(drawer,ctx,q.x0,q.y0,q.x1,q.y1,hover ? kBtnLit : taken ? kBtnFill : kMapBoxFill);
        frame(q,taken ? kMapOrder : kMapOrderDim);
        if(!taken)continue;
        const int members=load.count-j*mapbtn::kSquad<mapbtn::kSquad ? load.count-j*mapbtn::kSquad : mapbtn::kSquad;
        const SupportWeapon kind=mapbtn::ComposeSquadKind(load,j);
        Label(text,lines,at,(q.x0+q.x1)*0.5f,(q.y0+q.y1)*0.5f,1,kLineScale*0.6f,kWhite,Tr(Tx::composeSquad),KindText(kind),members);
        if(*at>0)MapFitLabel(text,lines[*at-1],q.x0+2.0f*s,q.x1-2.0f*s);
        MapTipSet(c,q.x0,q.y0,q.x1,q.y1,Tr(Tx::composeSquadTip),j+1,KindText(load.soldier[j*mapbtn::kSquad]),KindText(kind),members);
        hit(q,mapbtn::ComposeSquadCode(j));
    }
    for(int k=0;k<mapbtn::kComposeClasses;++k) {
        Label(text,lines,at,L.classLabel[k].x0,(L.classLabel[k].y0+L.classLabel[k].y1)*0.5f,0,kLineScale*0.6f,kMapOrder,L"%ls",Tr(kClassText[k]));
        if(*at>0)MapFitLabel(text,lines[*at-1],L.classLabel[k].x0,L.classLabel[k].x1-2.0f*s);
    }
    for(int k=0;k<L.kinds;++k) {
        const mapbtn::Rect& q=L.kind[k];
        const auto kind=static_cast<SupportWeapon>(L.kindCode[k]);
        const bool hover=room && c.pointer && c.px>=q.x0 && c.px<q.x1 && c.py>=q.y0 && c.py<q.y1;
        Rect(drawer,ctx,q.x0,q.y0,q.x1,q.y1,hover ? kBtnLit : kBtnFill);
        frame(q,room ? kMapOrder : kMapOrderDim);
        Label(text,lines,at,(q.x0+q.x1)*0.5f,(q.y0+q.y1)*0.5f,1,kLineScale*0.6f,room ? kWhite : kMapOrderDim,L"%ls",KindText(kind));
        if(*at>0)MapFitLabel(text,lines[*at-1],q.x0+2.0f*s,q.x1-2.0f*s);
        if(room)MapTipSet(c,q.x0,q.y0,q.x1,q.y1,Tr(Tx::composeAddTip),KindText(kind),mapbtn::kSquad,c.composeSeats-load.count);
        else MapTipSet(c,q.x0,q.y0,q.x1,q.y1,Tr(Tx::composeFullTip),KindText(kind),mapbtn::kSquad);
        hit(q,L.kindCode[k]);
    }
    Label(text,lines,at,L.hint.x0,(L.hint.y0+L.hint.y1)*0.5f,0,kLineScale*0.55f,kMapOrderDim,L"%ls",Tr(Tx::composeHint));
    if(*at>0)MapFitLabel(text,lines[*at-1],L.hint.x0,L.hint.x1);
    MapCommandComposeButtons(rects,codes,hits);
}
void MapSupportBar(void* drawer,void* ctx,Text* text,float width,float s,float top,float bottom,bool pad,const MapCommandReadout& c,
                   Line* lines,int* at) noexcept {
    if(pad || c.supports<=0){MapCommandSupportButtons(nullptr,nullptr,0);return;}
    const wchar_t* names[kMapSupports];
    for(int i=0;i<c.supports;++i)names[i]=c.support[i].name;
    mapbtn::Group groups[kMapSupports];
    const int rows=mapbtn::GroupSupport(names,c.supports,groups,kMapSupports);
    const float x0=8.0f*s,x1=std::fmin(kSupW*s,width*0.3f),titleH=24.0f*s;
    mapbtn::Rect row[kMapSupports]{},chip[kMapSupports]{};
    const int placed=mapbtn::Column(groups,rows,x0+4.0f*s,x1-4.0f*s,top+titleH,bottom,kSupRowH*s,kSupGap*s,4.0f*s,row,chip);
    if(placed<=0){MapCommandSupportButtons(nullptr,nullptr,0);return;}
    const float last=row[placed-1].y1;
    MapUiBox(drawer,ctx,x0,top,x1,last+4.0f*s,lines,*at);
    const bool ready=c.supportReady.state==SupportReady::ready;
    const float* tint=ready ? kWhite : kMapOrderDim;
    Label(text,lines,at,x0+8.0f*s,top+titleH*0.5f,0,kLineScale*0.75f,kMapOrder,L"%ls",Tr(Tx::supportTitle));
    if(!ready) {
        const SupportReadiness& r=c.supportReady;
        if(r.state==SupportReady::cooldown)Label(text,lines,at,x1-8.0f*s,top+titleH*0.5f,2,kLineScale*0.65f,kAmber,Tr(Tx::supportCooldown),r.seconds);
        else Label(text,lines,at,x1-8.0f*s,top+titleH*0.5f,2,kLineScale*0.65f,kAmber,L"%ls",
                   Tr(r.state==SupportReady::planning ? Tx::supportPlanning : Tx::supportOff));
    }
    float rects[kMapSupports*2*4];int entries[kMapSupports*2];int hits=0;
    auto hit=[&](const mapbtn::Rect& q,int entry){
        if(hits>=kMapSupports*2)return;
        rects[hits*4]=q.x0;rects[hits*4+1]=q.y0;rects[hits*4+2]=q.x1;rects[hits*4+3]=q.y1;entries[hits++]=entry;
    };
    // The rows under their chips (drawn first), the chips over them; the hit list the chips first: a click on a chip is
    // that variant, not the row's.
    mapbtn::Rect body[kMapSupports]{};int pickOf[kMapSupports]{};
    for(int k=0;k<placed;++k) {
        const mapbtn::Group& g=groups[k];
        const mapbtn::Rect& q=row[k];
        bool lit=false;
        for(int e=g.first;e<g.first+g.count;++e)lit=lit || c.supportArmed==e;
        const bool hover=c.pointer && c.px>=q.x0 && c.px<q.x1 && c.py>=q.y0 && c.py<q.y1;
        if(lit || hover)Rect(drawer,ctx,q.x0,q.y0,q.x1,q.y1,lit ? kBtnLit : kMapBoxFill);
        const float mid=(q.y0+q.y1)*0.5f;
        MapSupportIcon(drawer,ctx,c.support[g.first].icon,q.x0+(4.0f+kSupIcon*0.5f)*s,mid,kSupIcon*s,s,tint);
        wchar_t base[40]{};   // the name before its "·" (map_buttons.h BaseLength)
        const int most=static_cast<int>(_countof(base))-1,len=mapbtn::BaseLength(names[g.first])<most ? mapbtn::BaseLength(names[g.first]) : most;
        std::wmemcpy(base,names[g.first],static_cast<std::size_t>(len));
        const float textRight=g.count>1 ? chip[g.first].x0-4.0f*s : q.x1-4.0f*s;
        Label(text,lines,at,q.x0+(8.0f+kSupIcon)*s,mid,0,kLineScale*0.7f,tint,L"%ls",base);
        if(*at>0)MapFitLabel(text,lines[*at-1],q.x0+(8.0f+kSupIcon)*s,textRight);
        // The row itself (left of its chips): its first variant, or the one [ ] picked last within it.
        pickOf[k]=c.supportPick>=g.first && c.supportPick<g.first+g.count ? c.supportPick : g.first;
        body[k]=mapbtn::Rect{q.x0,q.y0,textRight,q.y1};
        MapTipSet(c,body[k].x0,body[k].y0,body[k].x1,body[k].y1,Tr(Tx::supportTip),names[pickOf[k]]);
    }
    for(int k=0;k<placed;++k) {
        const mapbtn::Group& g=groups[k];
        if(g.count<=1)continue;
        for(int e=g.first;e<g.first+g.count;++e) {
            const mapbtn::Rect& q=chip[e];
            const bool lit=c.supportArmed==e;
            Rect(drawer,ctx,q.x0,q.y0,q.x1,q.y1,lit ? kBtnLit : kBtnFill);
            const float t=1.2f*s;const float* edge=lit ? kWhite : kMapOrderDim;
            Seg(drawer,ctx,q.x0,q.y0,q.x1,q.y0,t,edge);Seg(drawer,ctx,q.x1,q.y0,q.x1,q.y1,t,edge);
            Seg(drawer,ctx,q.x1,q.y1,q.x0,q.y1,t,edge);Seg(drawer,ctx,q.x0,q.y1,q.x0,q.y0,t,edge);
            MapVariantIcon(drawer,ctx,c.support[e].variant,(q.x0+q.x1)*0.5f,(q.y0+q.y1)*0.5f,(q.y1-q.y0)*0.8f,s,tint);
            MapTipSet(c,q.x0,q.y0,q.x1,q.y1,Tr(Tx::supportTip),names[e]);
            hit(q,e);
        }
    }
    for(int k=0;k<placed;++k)hit(body[k],pickOf[k]);
    MapCommandSupportButtons(rects,entries,hits);
    // The armed support's composition beside its row (its name before the "·").
    int composeRow=-1;
    for(int k=0;k<placed && composeRow<0;++k)
        if(c.composeEntry>=groups[k].first && c.composeEntry<groups[k].first+groups[k].count)composeRow=k;
    if(composeRow<0){MapCommandComposeButtons(nullptr,nullptr,0);return;}
    wchar_t base[40]{};
    const int most=static_cast<int>(_countof(base))-1,blen=mapbtn::BaseLength(names[c.composeEntry]);
    std::wmemcpy(base,names[c.composeEntry],static_cast<std::size_t>(blen<most ? blen : most));
    MapComposePanel(drawer,ctx,text,s,c,base,x1+6.0f*s,row[composeRow].y0,top,bottom,lines,at);
}

void MapCommands(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const MapReadout& m,Line* lines,int* at) noexcept {
    static MapCommandReadout c;   // the draw thread's (too big for its stack)
    if(!PlayerMapCommands(&c)){
        MapCommandButtons(nullptr,nullptr,0);MapCommandSquadButtons(nullptr,nullptr,0);MapCommandSquadFold(nullptr);
        MapCommandFormationButtons(nullptr,nullptr,0);
        MapCommandPayloadButtons(nullptr,0,0,nullptr,0);MapCommandSupportButtons(nullptr,nullptr,0);return;
    }
    const float* tint=c.allowed && c.pickable ? kMapOrder : kMapOrderDim;
    // Where a point order goes: the pointer (a ring and a cross), or with a pad the crosshair at the centre; an armed
    // point order or support call in its colour.
    const float* aim=c.armedOrder ? (c.armed==Order::attackMove ? kAmber : kWhite) : c.supportArmed>=0 ? kGreen : tint;
    const float cx=c.pointer ? c.px : width*0.5f,cy=c.pointer ? c.py : height*0.5f;
    Seg(drawer,ctx,cx-16.0f*s,cy,cx-5.0f*s,cy,2.0f*s,aim);Seg(drawer,ctx,cx+5.0f*s,cy,cx+16.0f*s,cy,2.0f*s,aim);
    Seg(drawer,ctx,cx,cy-16.0f*s,cx,cy-5.0f*s,2.0f*s,aim);Seg(drawer,ctx,cx,cy+5.0f*s,cx,cy+16.0f*s,2.0f*s,aim);
    if(c.pointer)Arc(drawer,ctx,cx,cy,9.0f*s,0.0f,kTurn,1.5f*s,16,c.armedOrder || c.supportArmed>=0 ? aim : kWhite);
    // The box being dragged.
    if(c.boxing) {
        const float x0=c.bx<c.px ? c.bx : c.px,x1=c.bx<c.px ? c.px : c.bx,y0=c.by<c.py ? c.by : c.py,y1=c.by<c.py ? c.py : c.by;
        Rect(drawer,ctx,x0,y0,x1,y1,kMapBoxFill);
        Seg(drawer,ctx,x0,y0,x1,y0,1.5f*s,kMapOrder);Seg(drawer,ctx,x1,y0,x1,y1,1.5f*s,kMapOrder);
        Seg(drawer,ctx,x1,y1,x0,y1,1.5f*s,kMapOrder);Seg(drawer,ctx,x0,y1,x0,y0,1.5f*s,kMapOrder);
    }
    // The enemy under the pointer: amber brackets on it, what the right button (or H) and Q do with it.
    float hx,hy,hd;
    if(c.hover && Project(vp,c.hoverAt,width,height,&hx,&hy,&hd)) {
        MapBrackets(drawer,ctx,hx,hy,20.0f*s,2.0f*s,kAmber);
        Label(text,lines,at,hx,hy-30.0f*s,1,kLineScale*0.6f,kAmber,L"%ls",Tr(Tx::cmdHoverEnemy));
    }
    // The transports' pairs (transport.cpp): a thin line from a squad on foot to its vehicle (bright on a trip).
    for(int i=0;i<c.links && i<kMapTransportLinks;++i) {
        float sx,sy,sd,vx,vy,vd;
        float sp[3],vp3[3];mapcmd::BodyPoint(c.link[i].squad,false,sp);mapcmd::BodyPoint(c.link[i].vehicle,false,vp3);
        if(Project(vp,sp,width,height,&sx,&sy,&sd) && Project(vp,vp3,width,height,&vx,&vy,&vd))
            Seg(drawer,ctx,sx,sy,vx,vy,1.0f*s,c.link[i].trip ? kMapOrder : kMapOrderDim);
    }
    wchar_t one[64]{};
    for(int i=0;i<c.count && i<kCmdUnits;++i) {
        const CmdMark& u=c.unit[i];
        if(u.riding && !u.selected)continue;   // its vehicle's mark stands for it (picked only from its panel row)
        // On the unit's body (mapcmd_logic.h BodyPoint; the user, 2026-10-09: "直接在npc身上不好吗"), where a click takes it.
        float body[3],ux=0.0f,uy=0.0f,ud=0.0f;
        mapcmd::BodyPoint(u.pos,u.air,body);
        const bool shown=Project(vp,body,width,height,&ux,&uy,&ud);
        if(mapcmd::PointOrder(u.now.order)) {
            // The ring round its point (white: a move, amber: an attack-move, dim: a guard), its line from the unit.
            const float* order=u.now.order==Order::move ? kWhite : u.now.order==Order::attackMove ? kAmber : kMapOrder;
            float last[3];
            for(int k=0;k<=16;++k) {
                const float a=kTurn*static_cast<float>(k)/16.0f;
                const float q[3]={u.now.at[0]+std::sin(a)*kMapGuardRing,u.now.at[1]+1.0f,u.now.at[2]+std::cos(a)*kMapGuardRing};
                if(k)MapLine(drawer,ctx,vp,width,height,last,q,2.0f*s,order);
                std::memcpy(last,q,12);
            }
            float gx,gy,depth;
            if(shown && Project(vp,u.now.at,width,height,&gx,&gy,&depth))Seg(drawer,ctx,ux,uy,gx,gy,1.5f*s,u.now.order==Order::guard ? kMapOrderDim : order);
        }
        if(!shown)continue;
        Arc(drawer,ctx,ux,uy,13.0f*s,0.0f,kTurn,1.5f*s,20,u.selected ? kWhite : u.locked ? kMapLocked : kMapOrderDim);
        if(u.selected)MapBrackets(drawer,ctx,ux,uy,20.0f*s,2.5f*s,kWhite);
        if(u.now.order==Order::follow)Label(text,lines,at,ux,uy+22.0f*s,1,kLineScale*0.6f,kMapOrder,L"%ls",Tr(Tx::orderFollow));
        if(u.selected && c.selected==1) {
            wchar_t kind[48];
            MapUnitName(u.name,kind,_countof(kind));
            if(u.owner==kCmdOwnerHeli || u.owner==kCmdOwnerJet)
                _snwprintf_s(one,_countof(one),_TRUNCATE,Tr(u.owner==kCmdOwnerHeli ? Tx::unitHeli : Tx::unitJet),kind);
            else _snwprintf_s(one,_countof(one),_TRUNCATE,L"%ls",kind);
            Label(text,lines,at,ux,uy-28.0f*s,1,kLineScale*0.7f,kWhite,L"%ls",one);
        }
    }
    const float panelBottom=MapSquadPanel(drawer,ctx,text,width,s,m.pad,c,lines,at);
    MapPayloadPanel(drawer,ctx,text,width,s,m.pad,c,lines,at);
    const int buttonRows=MapButtons(drawer,ctx,text,width,height,s,m.pad,c,lines,at);
    // The support bar under the squad panel (its rows and the gap under them), down to the command card.
    const float barTop=c.squads>0 ? panelBottom+10.0f*s : panelBottom;
    const float barBottom=height-(kBtnBottom+static_cast<float>(buttonRows)*(kBtnRowH+kBtnGap)+40.0f)*s;
    MapSupportBar(drawer,ctx,text,width,s,barTop,barBottom,m.pad,c,lines,at);
    // The band over the map's keys: what is selected and what to do with it (one line; the keys are in the tooltips).
    Rect(drawer,ctx,0.0f,height-104.0f*s,width,height-46.0f*s,kMapBand);
    const float y=height-75.0f*s;
    const int footerFirst=*at;
    wchar_t sel[64];
    if(c.all)_snwprintf_s(sel,_countof(sel),_TRUNCATE,Tr(Tx::selectedAll),c.selected);
    else if(c.selected==1 && one[0])_snwprintf_s(sel,_countof(sel),_TRUNCATE,Tr(Tx::selectedOne),c.pickable,one);
    else _snwprintf_s(sel,_countof(sel),_TRUNCATE,Tr(Tx::selectedSome),c.selected,c.pickable);
    if(!c.allowed)Label(text,lines,at,width*0.5f,y,1,kLineScale*0.75f,kAmber,L"%ls",Tr(Tx::npcOfflineOnly));
    else if(!c.pickable)Label(text,lines,at,width*0.5f,y,1,kLineScale*0.75f,kWhite,L"%ls",Tr(Tx::npcNone));
    else if(!c.selected && !m.pad)Label(text,lines,at,width*0.5f,y,1,kLineScale*0.75f,kWhite,L"%ls",Tr(Tx::cmdHintSelect));
    else Label(text,lines,at,width*0.5f,y,1,kLineScale*0.75f,kWhite,Tr(m.pad ? Tx::npcPadKeys : Tx::npcMouseKeys),sel);
    if(c.noteFresh)Label(text,lines,at,width*0.5f,height-124.0f*s,1,kLineScale*0.8f,kAmber,L"%ls",c.note);
    for(int i=footerFirst;i<*at;++i) {
        Line& row=lines[i];MapFitLabel(text,row,16.0f*s,width-16.0f*s);row.x=(width-row.w)*0.5f;
    }
}

// The legend: a small tab over the right end of the command card ("图例"), the whole legend over it while the pointer
// is on the tab or on the legend (the user, 2026-10-09: "m里面显示太复杂了": it no longer takes the left edge for good).
void MapLegend(void* drawer,void* ctx,Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    MapCommandReadout c{};
    const bool pointer=PlayerMapCommands(&c) && c.pointer;
    struct Entry { MapKind kind; std::uint8_t flags; Tx name; };
    static const Entry kLegend[]={{MapKind::squad,0,Tx::legendSquad},{MapKind::ally,0,Tx::legendFriendly},{MapKind::vehicle,0,Tx::legendVehicle},
                                  {MapKind::air,0,Tx::legendAircraft},{MapKind::air,kMapRotor,Tx::legendHelicopter},
                                  {MapKind::vehicle,kMapEmpty,Tx::legendEmpty},{MapKind::carrier,0,Tx::legendCarrier},
                                  {MapKind::enemy,kMapLarge,Tx::legendLargeEnemy},{MapKind::enemyAir,kMapLarge,Tx::legendLargeEnemyAir},
                                  {MapKind::marker,0,Tx::legendObjective}};
    constexpr int kRows=static_cast<int>(sizeof(kLegend)/sizeof(kLegend[0]))+5;   // the player, the dots, the brackets
    const float tabW=110.0f*s,tabH=24.0f*s,x1=width-12.0f*s,x0=x1-tabW,tabY1=height-110.0f*s,tabY0=tabY1-tabH;
    const float listW=std::fmin(300.0f*s,width*0.4f),lx0=x1-listW,ly1=tabY0-2.0f*s,ly0=ly1-(static_cast<float>(kRows)*22.0f+12.0f)*s;
    const bool open=pointer && ((c.px>=x0 && c.px<x1 && c.py>=tabY0 && c.py<tabY1) || (c.px>=lx0 && c.px<x1 && c.py>=ly0 && c.py<ly1));
    MapUiBox(drawer,ctx,x0,tabY0,x1,tabY1,lines,*at);
    const float mid=(tabY0+tabY1)*0.5f,arrow=x1-12.0f*s;
    Label(text,lines,at,(x0+arrow)*0.5f-4.0f*s,mid,1,kLineScale*0.7f,open ? kWhite : kMapOrder,L"%ls",Tr(Tx::legendTitle));
    Tri(drawer,ctx,arrow,mid+(open ? 3.0f : -3.0f)*s,arrow,mid+(open ? -4.0f : 4.0f)*s,4.0f*s,open ? kWhite : kMapOrder);
    if(!open)return;
    MapUiBox(drawer,ctx,lx0,ly0,x1,ly1,lines,*at);
    const int first=*at;
    const float ix=lx0+24.0f*s,tx=lx0+44.0f*s;
    float y=ly0+17.0f*s;
    Arc(drawer,ctx,ix,y,8.0f*s,0.0f,kTurn,2.0f*s,16,kWhite);
    Label(text,lines,at,tx,y,0,kLineScale*0.75f,kWhite,L"%ls",Tr(Tx::legendYou));
    for(const Entry& e:kLegend) {
        y+=22.0f*s;
        MapIcon(drawer,ctx,ix,y,s,e.kind,e.flags,0.0f,0.0f,-1.0f,1.0f);
        Label(text,lines,at,tx,y,0,kLineScale*0.75f,kWhite,L"%ls",Tr(e.name));
    }
    y+=22.0f*s;MapDot1(drawer,ctx,ix,y,s,0,kMapEnemy);
    Label(text,lines,at,tx,y,0,kLineScale*0.75f,kWhite,L"%ls",Tr(Tx::legendEnemy));
    y+=22.0f*s;MapDot1(drawer,ctx,ix,y,s,kMapFlying,kMapEnemy);
    Label(text,lines,at,tx,y,0,kLineScale*0.75f,kWhite,L"%ls",Tr(Tx::legendEnemyFlying));
    y+=22.0f*s;MapBrackets(drawer,ctx,ix,y,9.0f*s,2.0f*s,kMapEnemy);
    Label(text,lines,at,tx,y,0,kLineScale*0.75f,kWhite,L"%ls",Tr(Tx::legendLock));
    y+=22.0f*s;MapBrackets(drawer,ctx,ix,y,9.0f*s,2.0f*s,kAmber);
    Label(text,lines,at,tx,y,0,kLineScale*0.75f,kWhite,L"%ls",Tr(Tx::legendNearestEnemy));
    for(int i=first;i<*at;++i)MapFitLabel(text,lines[i],tx,x1-8.0f*s);
}

// The title and the keys (top and bottom bands).
void MapText(void* drawer,void* ctx,Text* text,float width,float height,float s,const MapReadout& m,Line* lines,int* at) noexcept {
    MapUiBox(drawer,ctx,0,0,width,46.0f*s,lines,*at);
    MapUiBox(drawer,ctx,0,height-46.0f*s,width,height,lines,*at);
    const int bandsFirst=*at;
    wchar_t h[24],g[24];
    MapDistance(h,_countof(h),m.height);MapDistance(g,_countof(g),mapcam::GridStep(m.height));
    wchar_t follow[32]=L"";
    if(m.follow)std::swprintf(follow,32,L"   %ls",Tr(Tx::orderFollow));
    Label(text,lines,at,width*0.5f,23.0f*s,1,kTitleScale,kWhite,Tr(Tx::mapTitle),h,g,follow);
    if(m.pad)Label(text,lines,at,width*0.5f,height-23.0f*s,1,kLineScale*0.8f,kWhite,L"%ls",Tr(Tx::mapPadKeys));
    else {
        wchar_t key[32];KeyName(m.mapKey,key,32);
        Label(text,lines,at,width*0.5f,height-23.0f*s,1,kLineScale*0.8f,kWhite,Tr(Tx::mapMouseKeys),key);
    }
    for(int i=bandsFirst;i<*at;++i)MapFitLabel(text,lines[i],16.0f*s,width-16.0f*s);
    MapLegend(drawer,ctx,text,width,height,s,lines,at);
}

void NpcMarkHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept;

// The map view open: its marks drawn (true), nothing else of the HUD.
bool MapScreen(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept {
    static MapReadout m;   // the draw thread's (too big for its stack)
    if(!PlayerMap(&m)){
        MapCommandButtons(nullptr,nullptr,0);MapCommandSquadButtons(nullptr,nullptr,0);MapCommandSquadFold(nullptr);
        MapCommandFormationButtons(nullptr,nullptr,0);
        MapCommandPayloadButtons(nullptr,0,0,nullptr,0);MapCommandSupportButtons(nullptr,nullptr,0);MapCommandUiPanels(nullptr,0);return false;
    }
    s=hudscale::FitMap(s,width,height);
    if(text)text->s=s;
    mapUiPanelCount=0;mapTip.on=false;
    MapGrid(drawer,ctx,text,vp,width,height,s,m,lines,at);
    MapUnits(drawer,ctx,text,vp,width,height,s,m,lines,at);
    MapCommandView(vp,width,height);   // the commands' box, clicks and pointer are found on this view
    NpcMarkHud(drawer,ctx,text,vp,width,height,s,lines,at);   // which enemy the NPCs are set on, on the map too
    MapCommands(drawer,ctx,text,vp,width,height,s,m,lines,at);
    MapUiBox(drawer,ctx,width*0.55f-8*s,146*s,width-12*s,198*s,lines,*at);
    MapScale(drawer,ctx,text,vp,width,height,s,m,lines,at);
    MapUiBox(drawer,ctx,width-134*s,54*s,width-6*s,174*s,lines,*at);
    MapCompass(drawer,ctx,text,vp,width,height,s,m,lines,at);
    MapText(drawer,ctx,text,width,height,s,m,lines,at);
    if(!m.pad)MapTipDraw(drawer,ctx,text,width,height,s,lines,at);   // over everything: drawn last
    MapCommandUiPanels(mapUiPanels,mapUiPanelCount);
    MapCommandReadout pointer{};
    if(PlayerMapCommands(&pointer) && pointer.pointer && !m.pad) {
        for(int i=0;i<mapUiPanelCount;++i) {
            const auto* r=mapUiPanels+4*i;
            if(pointer.px<r[0] || pointer.px>=r[2] || pointer.py<r[1] || pointer.py>=r[3])continue;
            // Keep the pointer above opaque controls, rather than hiding the map crosshair behind them.
            Tri(drawer,ctx,pointer.px+9*s,pointer.py+13*s,pointer.px,pointer.py,5*s,kWhite);break;
        }
    }
    return true;
}
// The NPCs' mark (npcai.cpp, the user's Q on foot; docs/npc-ai-design.md §6.3): an amber diamond round it, MARK and its
// distance under it.
void NpcMarkHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept {
    float m[3],x,y,depth;
    if(!NpcMarkReadout(m) || !Project(vp,m,width,height,&x,&y,&depth))return;
    const float r=16.0f*s,t=2.0f*s;
    Seg(drawer,ctx,x,y-r,x+r,y,t,kAmber);Seg(drawer,ctx,x+r,y,x,y+r,t,kAmber);
    Seg(drawer,ctx,x,y+r,x-r,y,t,kAmber);Seg(drawer,ctx,x-r,y,x,y-r,t,kAmber);
    float eye[3],dir[3];
    if(CameraRay(eye,dir))Label(text,lines,at,x,y+r+12.0f*s,1,kLineScale*0.7f,kAmber,Tr(Tx::npcMarkRange),vec::Dist(eye,m));
    else Label(text,lines,at,x,y+r+12.0f*s,1,kLineScale*0.7f,kAmber,L"%ls",Tr(Tx::npcMark));
}

// The march formation just cycled (npcai.cpp; the player's key on foot or the map's T): its name and the key, a moment
// under the screen's centre.
// The formation's name (formation.h Shape) as the HUD says it (mapcmd.cpp's notes too).
}  // namespace

const wchar_t* FormationText(int shape) noexcept {
    using hudtext::Tx;
    static const Tx kNames[]={Tx::formStock,Tx::formColumn,Tx::formStaggered,Tx::formWedge,Tx::formVee,Tx::formLine,
                              Tx::formEchelonLeft,Tx::formEchelonRight,Tx::formDiamond,Tx::formBounding,Tx::formPerimeter};
    static_assert(sizeof(kNames)/sizeof(kNames[0])==static_cast<std::size_t>(npc::formation::kShapes),"a name a shape");
    return hudtext::Tr(shape>=0 && shape<npc::formation::kShapes ? kNames[shape] : Tx::formStock);
}
namespace {
void FormationBanner(Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    FormationCue c{};
    if(!PlayerFormationCue(&c))return;
    wchar_t key[24];
    KeyName(c.key,key,_countof(key));
    Label(text,lines,at,width*0.5f,height*0.5f+190.0f*s,1,kLineScale,kCyan,Tr(Tx::formationCue),FormationText(c.shape),key);
}

// The box sweep (npcai.cpp, the player's NpcPickupKey): the boxes still to fetch and the ones in while it goes, the
// count a moment after it is over; under the formation's banner.
void SweepBanner(Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    SweepCue c{};
    if(!PlayerSweepCue(&c))return;
    wchar_t key[24];
    KeyName(c.key,key,_countof(key));
    if(c.on)Label(text,lines,at,width*0.5f,height*0.5f+222.0f*s,1,kLineScale,kAmber,Tr(Tx::sweepOn),c.left,c.taken,key);
    else Label(text,lines,at,width*0.5f,height*0.5f+222.0f*s,1,kLineScale,kGreen,Tr(Tx::sweepDone),c.taken);
}
// The mark key on foot with no enemy near the centre (npcai.cpp SendToPoint): a ring where it points, for a moment, and
// what came of it (the selected units sent there, how many; none selected; online).
void NpcPingHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept {
    NpcPing p{};
    float x,y,depth;
    if(!NpcPingReadout(&p) || !Project(vp,p.at,width,height,&x,&y,&depth))return;
    const float* tint=p.given>0 ? kMapOrder : kAmber;
    Arc(drawer,ctx,x,y,14.0f*s,0.0f,kTurn,2.0f*s,20,tint);
    Seg(drawer,ctx,x,y-6.0f*s,x,y+6.0f*s,2.0f*s,tint);Seg(drawer,ctx,x-6.0f*s,y,x+6.0f*s,y,2.0f*s,tint);
    if(p.given>=0)Label(text,lines,at,x,y+26.0f*s,1,kLineScale*0.7f,tint,Tr(Tx::npcPingSent),p.given);
    else Label(text,lines,at,x,y+26.0f*s,1,kLineScale*0.7f,tint,L"%ls",
               Tr(p.given==-2 ? Tx::cmdOfflineOnly : p.given==kPingNearEnemy ? Tx::npcPingNearEnemy : Tx::npcPingNoUnit));
}

}  // namespace

// A magnified sight's picture (scopeview.h; sightzoom.cpp): while the sight is magnified the screen outside its field
// dark; a round field for an optical sight with its rim and three posts (left, right, under: the aim stays clear), the
// magnification inside its left edge; the gunship gunner's a 4:3 sensor with its edge (GunnerMarks names its zoom).
alignas(16) const float kScopeDark[4]={0.0f,0.0f,0.0f,0.86f};
alignas(16) const float kScopeRim[4]={0.55f,0.62f,0.55f,0.75f};
void Quad4(void* drawer,void* ctx,const scopeview::Quad& q,const float* rgba) noexcept {
    alignas(16) const float m[16]={1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f};
    alignas(16) const float v[12]={q.x[0],q.y[0],0.0f, q.x[1],q.y[1],0.0f, q.x[2],q.y[2],0.0f, q.x[3],q.y[3],0.0f};
    reinterpret_cast<QuadFn>(image+kQuad)(drawer,ctx,m,rgba,kStrip,v,4,nullptr);
}
void ScopeShade(void* drawer,void* ctx,Text* text,float width,float height,float s,float zoom,bool sensor,reticle::Posts posts,Line* lines,
                int* at) noexcept {
    const float cx=width*0.5f,cy=height*0.5f;
    if(sensor) {
        scopeview::Quad b[4];
        scopeview::RectBands(width,height,b);
        for(const auto& q:b)Quad4(drawer,ctx,q,kScopeDark);
        float hx,hy;
        scopeview::RectHalf(width,height,&hx,&hy);
        const float t=2.0f*s;
        Seg(drawer,ctx,cx-hx,cy-hy,cx+hx,cy-hy,t,kScopeRim);Seg(drawer,ctx,cx-hx,cy+hy,cx+hx,cy+hy,t,kScopeRim);
        Seg(drawer,ctx,cx-hx,cy-hy,cx-hx,cy+hy,t,kScopeRim);Seg(drawer,ctx,cx+hx,cy-hy,cx+hx,cy+hy,t,kScopeRim);
        return;
    }
    const float r=scopeview::Radius(width,height),out=scopeview::Reach(width,height,cx,cy);
    for(int i=0;i<scopeview::kSides;++i)Quad4(drawer,ctx,scopeview::RingPiece(i,cx,cy,r,out),kScopeDark);
    Arc(drawer,ctx,cx,cy,r,0.0f,kTurn,3.0f*s,scopeview::kSides,kScopeRim);
    // The posts the reticle's style carries (reticle.h Spec): a tank sight's three, the duplex's four, a ring sight's none.
    const float post=4.0f*s,inner=r*0.62f;
    if(posts!=reticle::Posts::none) {
        Seg(drawer,ctx,cx-r,cy,cx-inner,cy,post,kScopeRim);Seg(drawer,ctx,cx+inner,cy,cx+r,cy,post,kScopeRim);
        Seg(drawer,ctx,cx,cy+inner,cx,cy+r,post,kScopeRim);
    }
    if(posts==reticle::Posts::duplex)Seg(drawer,ctx,cx,cy-r,cx,cy-inner,post,kScopeRim);
    Label(text,lines,at,cx-r+18.0f*s,cy-22.0f*s,0,kLineScale,kScopeRim,L"%.0fx",zoom);
}

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
        const float width=static_cast<float>(w),height=static_cast<float>(h),s=HudScaleOf(w,h);
        // The words in the language the game's text is in now (it may change in the options mid-game), or the ini's.
        hudtext::Use(hudtext::Resolve(Cfg().hudLanguage,GameTextLanguage()));
        alignas(16) unsigned char renderer[kRendererSize]{},font[kFontSize]{};
        Text text{};
        text.ctx=ctx;text.renderer=renderer;text.font=font;text.mgr=At<unsigned char*>(image,kFontMgr);text.s=s;
        Text* const t=textOk && text.mgr ? &text : nullptr;
        Line lines[kMaxLines];
        int at=0,shown=0;
        if(MapScreen(drawer,ctx,t,viewProj,width,height,s,lines,&at)) {   // the map view: its marks alone
            if(t && textOk)DrawAll(*t,lines,at);
            FreeText(text);
            return;
        }
        // The map's camera easing back to the player (map.cpp Camera, kEaseOut frames): its readout is gone and the view is
        // still the map's, so the world marks (the impact rings, the turret's box, the vehicle HUD) would land on a
        // view they were not made for. Nothing, like the stock HUD (its switch held until the camera is back).
        if(MapOwnsView()){FreeText(text);return;}
        const ULONGLONG now=GetTickCount64();
        const sightzoom::Kind sightKind=SightZoomView();
        const float sightMagnification=SightZoomNow(nullptr);
        const sightzoom::Mask sightMask=sightzoom::MaskOf(sightKind);
        const bool scopeOverlay=sightMagnification>1.0f && sightMask!=sightzoom::Mask::none;
        {   // A validated seat/weapon chooses the mask. Aircraft/mechs keep their own HUD; overhead has no optic.
            const Snapshot& z=Latest();
            reticle::Posts posts=reticle::Posts::three;
            const int pick=z.stock ? z.stockHud.sight : -1;
            if(pick>=0 && pick<z.stockHud.arms && pick<kStockArms)posts=reticle::SpecOf(z.stockHud.arm[pick].reticle).posts;
            if(scopeOverlay && now-z.tick<=kFreshMs)
                ScopeShade(drawer,ctx,t,width,height,s,sightMagnification,sightMask==sightzoom::Mask::sensor,posts,lines,&at);
        }
        for(int i=0;i<count && i<3;++i)CarrierBars(drawer,ctx,t,viewProj,width,height,s,panels[i],lines,&at,now);
        const Snapshot& snap=Latest();
        const bool mountedOptic=snap.mountedOptic || scopeOverlay; // also cover the first paint before next HUD publication
        // A switch the player makes shows for a moment (hud_cue.h Change, kSwitchMs): the picked store (forgotten while no
        // aircraft's stores show, so boarding shows none) and EDF6AutoTurret's aim mode.
        static hudcue::Change storePick{},aimMode{};
        const bool fresh=now-snap.tick<=kFreshMs,storesShown=fresh && (snap.cockpit || snap.heliFly || snap.stock);
        if(!storesShown)storePick.seen=false;
        const int picked=snap.cockpit ? snap.jet.store : snap.stock ? snap.stockHud.selected : -1;
        const bool storeSwitched=storesShown && hudcue::Changed(storePick,picked,now,kSwitchMs);
        const bool aimFlipped=fresh && snap.turret && hudcue::Changed(aimMode,static_cast<int>(snap.turretAim.mode),now,kSwitchMs);
        const bool rotorHud=snap.cockpit && snap.jet.rotor && Cfg().heliFlightHud;   // a rotor craft: the helicopter HUD
        const ULONGLONG launchAt=snap.warned ? snap.warn.launchAt : 0;
        if(now-snap.tick<=kFreshMs && snap.cockpit) {
            if(snap.jet.aiming)AimMarks(drawer,ctx,viewProj,width,height,s,snap.jet);
            if(snap.jet.hasImpact)ImpactMark(drawer,ctx,viewProj,width,height,s,snap.jet);
            if(!snap.jet.bomb)LockMark(drawer,ctx,viewProj,width,height,s,snap.jet);
            if(snap.jet.rotor && snap.jet.heli.aiming)FlightAim(drawer,ctx,t,viewProj,width,height,s,snap.jet.heli.aim,lines,&at);   // HUD or not
            if(rotorHud) {
                HeliHud(drawer,ctx,t,viewProj,width,height,s,snap.jet.heli,snap.jet.sym,launchAt,lines,&at);
                if(Cfg().playerJetGunSight)GunSight(drawer,ctx,viewProj,width,height,s,snap.jet.sym);
                wchar_t stores[128];
                StoresText(stores,_countof(stores),snap.jet,false);
                LoadCell cells[kMostStores];
                const int n=JetCells(snap.jet,cells);
                Line controls{};JetControls(controls,snap.jet);
                HeliStrip(drawer,ctx,t,width,height,s,snap.jet.heli,snap.jet.fuel,stores,cells,n,snap.jet.store,storeSwitched,lines,&at,&controls);
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
            Line controls{};
            if(snap.payload)AircraftControls(controls,snap.payloadHud.keys,snap.payloadHud.choices,snap.payloadHud.switchButton,0,false,false);
            HeliStrip(drawer,ctx,t,width,height,s,snap.heliHud.f,snap.heliHud.fuel,nullptr,cells,n,own ? snap.stockHud.selected : -1,
                      storeSwitched,lines,&at,&controls);
            if(snap.warned)Annunciator(drawer,ctx,t,width,height,s,snap.warn,lines,&at);
        }
        if(now-snap.tick<=kFreshMs && snap.heli && !snap.cockpit && !heliHud)HeliPanel(drawer,ctx,t,width,height,s,snap.heliCue,lines,&at);
        // The stock vehicles' HUD up (StockVehicleHud): it carries the drill's line and the gun's marks, so DrillPanel and
        // TurretMark's square give way to it.
        const bool stockHud=now-snap.tick<=kFreshMs && snap.stock && !snap.cockpit && !snap.stockHud.heli;
        if(now-snap.tick<=kFreshMs && snap.drill && !snap.cockpit && !snap.heli && !stockHud)DrillPanel(drawer,ctx,t,width,height,s,snap.drillCue,lines,&at);
        if(now-snap.tick<=kSazabiCueMs && snap.sazabi && !snap.cockpit) {   // the Sazabi's own HUD (it is no stock vehicle)
            SazabiHud(drawer,ctx,t,viewProj,width,height,s,snap.sazabiCue,lines,&at);
        }
        const bool gunnerSight=fresh && snap.gunner && !snap.cockpit;
        const bool mechSight=fresh && snap.sazabi && !snap.cockpit;
        const bool overhead=fresh && ((snap.highCam && snap.highCamOn) || sightKind==sightzoom::Kind::indirect);
        const int stockPick=stockHud ? snap.stockHud.sight : -1;
        const bool physicalSight=(stockHud && stockPick>=0 && stockPick<snap.stockHud.arms && stockPick<kStockArms && snap.stockHud.arm[stockPick].physicalOnly) ||
                                 (fresh && snap.heliSight && snap.heliAim.gun && snap.heliAim.physicalOnly);
        const bool launcherSight=stockHud && stockPick>=0 && stockPick<snap.stockHud.arms && snap.stockHud.arm[stockPick].lofted;
        if(fresh && snap.heliSight && !snap.cockpit && !gunnerSight && !mechSight)
            HeliGunSight(drawer,ctx,t,viewProj,width,height,s,snap.heliAim,lines,&at);
        if(fresh && snap.launcher && launcherSight)LauncherMarks(drawer,ctx,t,viewProj,width,height,s,snap.launch,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.highCam && !snap.cockpit)HighCamHint(t,width,height,s,snap.highCamOn,snap.highCamKeys,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.seats)SeatLine(t,width,height,snap.seatPrompt,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.entrance)EntranceMark(drawer,ctx,t,viewProj,width,height,s,snap.boardingEntrance,lines,&at);
        if(fresh && snap.turretCamOk && !mountedOptic && !snap.cockpit && !launcherSight && !overhead && !gunnerSight && !mechSight)
            TurretMark(drawer,ctx,viewProj,width,height,s,snap.turretCam,!stockHud);
        if(gunnerSight)GunnerMarks(drawer,ctx,t,viewProj,width,height,s,snap.gun,lines,&at);
        const bool showTurretOverlay=fresh && snap.turret && !mountedOptic && !snap.cockpit && !(physicalSight && snap.turretAim.ownGun) && (!stockHud || stockPick==0) && !launcherSight && !overhead && !gunnerSight && !mechSight;
        if(showTurretOverlay) {
            float controlsBottom=-1.0f;
            if(stockHud) {
                LoadCell cells[kStockArms];const int n=StockCells(snap.stockHud,cells);Line keys{};
                if(snap.payload)AircraftControls(keys,snap.payloadHud.keys,snap.payloadHud.choices,snap.payloadHud.switchButton,0,false,false);
                const auto dock=LoadoutDockOf(t,width,height,s,cells,n,keys.text[0] ? 1 : 0);
                controlsBottom=dock.top-20.0f*s;
            }
            TurretAimMarks(drawer,ctx,t,viewProj,width,height,s,snap.turretAim,aimFlipped,lines,&at,
                           snap.turretBinding ? &snap.binding : nullptr,controlsBottom);
        }
        if(stockHud) {
            const StockExtras x{fresh && snap.nix ? &snap.nixTorso : nullptr,fresh && snap.drill ? &snap.drillCue : nullptr,
                                showTurretOverlay && !physicalSight && stockPick==0 && snap.turretAim.ownGun && snap.turretAim.mode==edf::aimlink::Mode::leadCircle && snap.turretAim.lead,
                                fresh && snap.emc ? &snap.emcCue : nullptr,
                                fresh && snap.proteus ? &snap.proteusRo : nullptr,overhead,snap.armor ? &snap.armorRo : nullptr};
            StockDockHud(drawer,ctx,t,viewProj,width,height,s,snap.stockHud,x,snap.payload ? &snap.payloadHud : nullptr,
                         storeSwitched,lines,&at);
        }
        // The hidden stock armor gauge's numbers for a vehicle whose HUD is no StockBlock (that one has them in it).
        if(now-snap.tick<=kFreshMs && snap.armor && !stockHud)ArmorPanel(drawer,ctx,t,s,snap.armorRo,lines,&at);
        NpcMarkHud(drawer,ctx,t,viewProj,width,height,s,lines,&at);
        FormationBanner(t,width,height,s,lines,&at);
        SweepBanner(t,width,height,s,lines,&at);
        NpcPingHud(drawer,ctx,t,viewProj,width,height,s,lines,&at);
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
