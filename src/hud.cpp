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
#include "layout.h"
#include "memory.h"
#include "sight.h"
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
                  bool launcher; LauncherReadout launch; };
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
void Font(Text& t,float scale) noexcept {
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

// The mouse's aim on the keyboard and mouse: a hollow square where it aims, a small dot where the plane flies (with the
// flight HUD on, its flight path marker shows that: FighterHud).
void AimMarks(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetReadout& j) noexcept {
    float sx,sy,depth;
    if(Project(vp,j.aim,width,height,&sx,&sy,&depth)) {
        const float r=14.0f*s,t=2.0f*s;
        Rect(drawer,ctx,sx-r,sy-r,sx+r,sy-r+t,kCyan);Rect(drawer,ctx,sx-r,sy+r-t,sx+r,sy+r,kCyan);
        Rect(drawer,ctx,sx-r,sy-r,sx-r+t,sy+r,kCyan);Rect(drawer,ctx,sx+r-t,sy-r,sx+r,sy+r,kCyan);
    }
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

// The picked store's lock: locking, a yellow square closing in as it locks; locked, a red diamond on the target. The
// missile flies at what this marks (stores.cpp StoreLock reads the same entry the round will hold).
void LockMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetReadout& j) noexcept {
    float sx,sy,depth;
    if(!j.lock || !Project(vp,j.lockAt,width,height,&sx,&sy,&depth))return;
    const float t=2.0f*s;
    if(j.lock==1) {
        const float r=(40.0f-24.0f*j.lockProgress)*s;
        Rect(drawer,ctx,sx-r,sy-r,sx+r,sy-r+t,kYellow);Rect(drawer,ctx,sx-r,sy+r-t,sx+r,sy+r,kYellow);
        Rect(drawer,ctx,sx-r,sy-r,sx-r+t,sy+r,kYellow);Rect(drawer,ctx,sx+r-t,sy-r,sx+r,sy+r,kYellow);
        return;
    }
    const float r=14.0f*s;
    for(int k=0;k<6;++k) {   // a diamond from small squares along its four edges
        const float f=static_cast<float>(k)/6.0f,d=r*f,e=r-d;
        Rect(drawer,ctx,sx+d-t,sy-e-t,sx+d+t,sy-e+t,kRed);Rect(drawer,ctx,sx-d-t,sy-e-t,sx-d+t,sy-e+t,kRed);
        Rect(drawer,ctx,sx+d-t,sy+e-t,sx+d+t,sy+e+t,kRed);Rect(drawer,ctx,sx-d-t,sy+e-t,sx-d+t,sy+e+t,kRed);
    }
}

// The stores line: each store's name and rounds, the picked one in brackets.
void StoresLine(Line& l,const PlayerJetReadout& j) noexcept {
    wchar_t text[128]=L"";
    std::size_t at=0;
    for(int i=0;i<j.stores && i<6;++i) {
        const int n=_snwprintf_s(text+at,_countof(text)-at,_TRUNCATE,i==j.store ? L"[%hs %d]  " : L"%hs %d  ",
                                 j.storeName[i] ? j.storeName[i] : "?",j.storeRounds[i]);
        if(n<0)break;
        at+=static_cast<std::size_t>(n);
    }
    if(j.air)_snwprintf_s(text+at,_countof(text)-at,_TRUNCATE,L"FLARE %d",j.flares);
    Format(l,L"%ls",text);
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
    // On the takeoff roll (not in the air: j.ground only says there is ground under it, so the cue stayed on after
    // takeoff, 2026-10-05): the speed it may lift off from coming up, then the cue to pull up (the user, 2026-10-05).
    // In the air, the ground-proximity warning (PlayerJetReadout::pullUp) in its place.
    const int rotateKmh=static_cast<int>(std::lround(j.rotate*3.6f));
    const bool rolling=!j.air && j.rotate>0.0f && j.speed>1.0f,rotate=rolling && j.speed>=j.rotate;
    wchar_t cue[64]=L"";
    if(j.pullUp)std::swprintf(cue,64,L"    PULL UP! TERRAIN");
    else if(j.threat==2)std::swprintf(cue,64,L"    MISSILE!");
    else if(j.threat==1)std::swprintf(cue,64,L"    LOCKED");
    else if(rotate)std::swprintf(cue,64,L"    ROTATE: PULL UP (W / SPACE)");
    else if(rolling && j.speed>=j.rotate*0.7f)std::swprintf(cue,64,L"    ROTATE AT %d km/h",rotateKmh);
    Format(thr,L"THROTTLE %d%%    G %.1f%ls%ls",static_cast<int>(std::lround(j.throttle*100.0f)),j.load,j.stall ? L"    STALL" : L"",cue);
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
    // The rotate cue blinks green (4 Hz) over the throttle line's cyan; the pull-up warning red and white (8 Hz).
    const bool blink=(GetTickCount64()/125)%2==0;
    thr.scale=kLineScale;
    thr.rgba=j.pullUp || j.threat==2 ? (blink ? kRed : kWhite) : j.threat==1 ? kYellow : j.stall ? kRed :
             rotate && blink ? kGreen : rotate ? kYellow : kCyan;
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

// The boxes left and right of the middle: the speed (km/h) with the g under it, the height over the floor (ALT*: over
// the world's zero, nothing under it) with the climb under it. The landing gear's indicator (branch feat/jet-gear) has
// the row under the g: (width / 2 - kBoxOff * s, height / 2 + 2 * kBoxRow * s).
constexpr float kBoxOff=280.0f,kBoxW=120.0f,kBoxH=34.0f,kBoxRow=30.0f;
void SpeedAltBoxes(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetReadout& j,Line* lines,
                   int* at) noexcept {
    const float cy=height*0.5f,w=kBoxW*s*0.5f,hh=kBoxH*s*0.5f,t=2.0f*s;
    const float left=width*0.5f-kBoxOff*s,right=width*0.5f+kBoxOff*s;
    const float boxes[2]={left,right};
    for(const float x:boxes) {
        Seg(drawer,ctx,x-w,cy-hh,x+w,cy-hh,t,kHud);Seg(drawer,ctx,x-w,cy+hh,x+w,cy+hh,t,kHud);
        Seg(drawer,ctx,x-w,cy-hh,x-w,cy+hh,t,kHud);Seg(drawer,ctx,x+w,cy-hh,x+w,cy+hh,t,kHud);
    }
    const float alt=std::fmax(-9999.0f,std::fmin(j.clear,99999.0f));
    Label(text,lines,at,left,cy,1,kTitleScale,kHud,L"%d",static_cast<int>(std::lround(j.speed*3.6f)));
    Label(text,lines,at,left,cy-hh-12.0f*s,1,kLineScale*0.8f,kHud,L"KM/H");
    Label(text,lines,at,left,cy+kBoxRow*s,1,kLineScale,j.load>5.0f ? kWarn : kHud,L"G %.1f",j.load);
    Label(text,lines,at,right,cy,1,kTitleScale,kHud,L"%d",static_cast<int>(std::lround(alt)));
    Label(text,lines,at,right,cy-hh-12.0f*s,1,kLineScale*0.8f,kHud,j.ground ? L"ALT M" : L"ALT* M");
    Label(text,lines,at,right,cy+kBoxRow*s,1,kLineScale,kHud,L"VS %+d",static_cast<int>(std::lround(j.climb)));
}

// The guns' sight: a cross on the boresight (where the nose points: the guns' line), the pipper (a circle, a dot in it)
// where the rounds fired now will be at the target's range (else at the sight's own), the target's lead mark (a cross
// in a circle, dim out of the rounds' reach) and the range as an arc round the pipper (from its top, the share of the
// reach). Pipper on the lead mark: the rounds meet the target.
void GunSight(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetSymbols& y) noexcept {
    if(!y.gun)return;
    const float t=2.0f*s;
    float x,yy;
    if(sight::ToScreen(vp,y.nose,0.0f,width,height,&x,&yy)) {
        const float r=10.0f*s,g=3.0f*s;
        Seg(drawer,ctx,x-r,yy,x-g,yy,t,kHud);Seg(drawer,ctx,x+g,yy,x+r,yy,t,kHud);
        Seg(drawer,ctx,x,yy-r,x,yy-g,t,kHud);Seg(drawer,ctx,x,yy+g,x,yy+r,t,kHud);
    }
    if(sight::ToScreen(vp,y.pipper,1.0f,width,height,&x,&yy)) {
        const float r=18.0f*s;
        Arc(drawer,ctx,x,yy,r,0.0f,kTurn,t,16,kHud);
        Rect(drawer,ctx,x-1.5f*s,yy-1.5f*s,x+1.5f*s,yy+1.5f*s,kHud);
        if(y.lead && y.gunRange>0.0f) {
            const float share=vec::Clamp(y.leadRange/y.gunRange,0.0f,1.0f);
            const int sides=static_cast<int>(std::ceil(share*24.0f));
            if(sides>0)Arc(drawer,ctx,x,yy,r+5.0f*s,-0.25f*kTurn,share*kTurn,3.0f*s,sides,y.leadInRange ? kHud : kHudDim);
        }
    }
    if(y.lead && sight::ToScreen(vp,y.leadAt,1.0f,width,height,&x,&yy)) {
        const float r=7.0f*s;
        const float* c=y.leadInRange ? kHud : kHudDim;
        Arc(drawer,ctx,x,yy,r,0.0f,kTurn,t,10,c);
        Seg(drawer,ctx,x-r,yy-r,x+r,yy+r,t,c);Seg(drawer,ctx,x-r,yy+r,x+r,yy-r,t,c);
    }
}

// The threats (the user, 2026-10-05: "locked on, it should show the direction"): round the screen's middle a faint
// ring (red with a missile coming, amber with locks only), on it an arrow toward each threat wherever it is (behind
// too: sight::Toward): a missile's red (blinking) with its distance, an enemy jet's lock amber; one in view also
// marked where it is (a missile a red circle with a cross, a jet amber brackets).
constexpr float kThreatRing=0.30f;   // of the screen's height
void ThreatRing(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const PlayerJetSymbols& y,
                Line* lines,int* at) noexcept {
    if(y.threats<=0)return;
    const float cx=width*0.5f,cy=height*0.5f,r=kThreatRing*height,t=2.0f*s;
    bool missile=false;
    for(int i=0;i<y.threats && i<kMostThreats;++i)missile=missile || y.threatKind[i]==2;
    alignas(16) const float faint[4]={1.0f,missile ? 0.25f : 0.7f,missile ? 0.2f : 0.15f,0.35f};
    Arc(drawer,ctx,cx,cy,r,0.0f,kTurn,t,48,faint);
    const bool blink=(GetTickCount64()/125)%2==0;
    for(int i=0;i<y.threats && i<kMostThreats;++i) {
        const float* p=y.threatAt[i];
        const bool m=y.threatKind[i]==2;
        float dx,dy;
        sight::Toward(vp,p,width,height,&dx,&dy);
        Tri(drawer,ctx,cx+dx*r,cy+dy*r,cx+dx*(r+22.0f*s),cy+dy*(r+22.0f*s),11.0f*s,m ? (blink ? kRed : kWhite) : kAmber);
        if(m)Label(text,lines,at,cx+dx*(r+42.0f*s),cy+dy*(r+42.0f*s),1,kLineScale*0.85f,kRed,L"%.1f km",vec::Dist(p,y.pos)*0.001f);
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

// The fighter HUD of the aircraft the player flies, part by part as the ini switches them.
void FighterHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const PlayerJetReadout& j,
                Line* lines,int* at) noexcept {
    const PlayerJetSymbols& y=j.sym;
    if(Cfg().playerJetFlightHud) {
        if(j.air){Ladder(drawer,ctx,text,vp,width,height,s,y,lines,at);FlightPath(drawer,ctx,vp,width,height,s,y);}
        HeadingTape(drawer,ctx,text,width,height,s,y,lines,at);
        SpeedAltBoxes(drawer,ctx,text,width,height,s,j,lines,at);
    }
    if(Cfg().playerJetGunSight)GunSight(drawer,ctx,vp,width,height,s,y);
    if(Cfg().playerJetThreatHud)ThreatRing(drawer,ctx,text,vp,width,height,s,y,lines,at);
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

// The player's drill tank (DrillCue, the user 2026-10-05: the RPM on the HUD): its RPM and a bar of it, amber while
// it spins up or down, green at the top; "DRILLING" while it touches something.
void DrillPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,const DrillCue& c,Line* lines,int* at) noexcept {
    if(*at>=kMaxLines || !(c.maxRpm>0.0f))return;
    Line& l=lines[(*at)++];
    const float share=Unit(c.rpm/c.maxRpm);
    const bool top=share>=0.99f;
    Format(l,L"DRILL %d RPM%ls",static_cast<int>(std::lround(c.rpm)),c.touching && c.rpm>0.0f ? L"    DRILLING" : L"");
    l.scale=kTitleScale;
    l.rgba=top ? kGreen : share>0.0f ? kAmber : kCyan;
    l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
    const float pad=8.0f*s,gap=5.0f*s,barW=320.0f*s,barH=10.0f*s;
    const float lineH=l.h>0.0f ? l.h : 24.0f*s,w=(l.w>barW ? l.w : barW)+2.0f*pad,h=pad+lineH+gap+barH+pad;
    const float x0=(width-w)*0.5f,y0=height*0.80f-h;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,top ? kGreen : kCyan);
    l.x=(width-l.w)*0.5f;l.y=y0+pad;
    Bar(drawer,ctx,(width-barW)*0.5f,y0+pad+lineH+gap,barW,barH,share,share,top ? kGreen : kAmber,s);
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

void HudSee(unsigned char* v) noexcept {
    if(!Cfg().vehicleHud || !quadOk || v[kDead] || IsSub(v))return;
    if(SeatCount(v)==0 || SeatRider(SeatAt(v,0))!=Rider::dummy)return;   // NPC-driven only
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
    s.drill=PlayerDrillCue(&s.drillCue);
    s.launcher=PlayerLauncher(&s.launch);
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
        if(now-snap.tick<=kFreshMs && snap.cockpit) {
            if(snap.jet.aiming)AimMarks(drawer,ctx,viewProj,width,height,s,snap.jet);
            if(snap.jet.bomb)ImpactMark(drawer,ctx,viewProj,width,height,s,snap.jet);
            else LockMark(drawer,ctx,viewProj,width,height,s,snap.jet);
            FighterHud(drawer,ctx,t,viewProj,width,height,s,snap.jet,lines,&at);
            Cockpit(drawer,ctx,t,width,height,s,snap.jet,lines,&at);
        }
        if(now-snap.tick<=kFreshMs && snap.heli && !snap.cockpit)HeliPanel(drawer,ctx,t,width,height,s,snap.heliCue,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.drill && !snap.cockpit && !snap.heli)DrillPanel(drawer,ctx,t,width,height,s,snap.drillCue,lines,&at);
        if(now-snap.tick<=kFreshMs && snap.launcher)LauncherMarks(drawer,ctx,t,viewProj,width,height,s,snap.launch,lines,&at);
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
