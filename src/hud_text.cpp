// The vehicle HUD's text and drawing infrastructure (split from hud.cpp; the HUD's overview is hud.cpp's head):
// the colours, the quad primitives (Emit, Rect, Bar, Seg, Tri, Arc), the damage trails, the Line / Text sequence of
// the game's own text renderer (Font, Measure, Draw, DrawWorld, DrawAll), the key and button names, the world labels.
#include "hud_internal.h"
#include "edf_layout.h"
#include "config.h"
#include "plugin_log.h"

namespace crew {
namespace hudi {
alignas(16) const float kTrail[4]={1.0f,0.95f,0.85f,0.75f};
// The game's glyph cache (docs/hud-re.md §2.1) keys every glyph on its character and its font scale (as a half
// float), so each scale is a set of glyphs of its own. A text scale that follows the view depth would rasterize a
// new set nearly every frame and churn the shared atlas the menus draw from: the scale goes in on this grid.
constexpr float kTextScaleStep=1.0f/32.0f;
// A world bar's size: kNearScale at the camera down to kFarScale at its own reading distance (metres of view depth) and on.
constexpr float kNearScale=1.3f,kFarScale=0.7f;
// The damage trail: held kTrailHoldMs after the last hit, then drains kTrailRate of the bar a second.
constexpr ULONGLONG kTrailHoldMs=450;
constexpr float kTrailRate=0.6f;
// Every quad of the HUD goes down here: as given (`m` its translation), or with a canvas on, each vertex (moved by
// `m`'s translation) mapped onto the screen and the translation dropped.
void Emit(void* drawer,void* ctx,const float* m,const float* rgba,const float* v) noexcept {
    if(!screenCanvas.on){reinterpret_cast<QuadFn>(image+kQuad)(drawer,ctx,m,rgba,kStrip,v,4,nullptr);return;}
    alignas(16) const float id[16]={1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f};
    alignas(16) float p[12];
    for(int i=0;i<4;++i){canvas::Point(screenCanvas,v[3*i]+m[12],v[3*i+1]+m[13],&p[3*i],&p[3*i+1]);p[3*i+2]=0.0f;}
    reinterpret_cast<QuadFn>(image+kQuad)(drawer,ctx,id,rgba,kStrip,p,4,nullptr);
}

void Rect(void* drawer,void* ctx,float x0,float y0,float x1,float y1,const float* rgba) noexcept {
    if(x1<=x0 || y1<=y0)return;
    alignas(16) const float m[16]={1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, x0,y0,0.0f,1.0f};
    const float w=x1-x0,h=y1-y0;
    alignas(16) const float v[12]={0.0f,0.0f,0.0f, w,0.0f,0.0f, 0.0f,h,0.0f, w,h,0.0f};
    Emit(drawer,ctx,m,rgba,v);
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

void Format(Line& l,const wchar_t* format,...) noexcept {
    l.world.on=false;
    va_list args;va_start(args,format);
    TextWritten(_vsnwprintf_s(l.text,_countof(l.text),_TRUNCATE,format,args),l.text);
    va_end(args);
}
void Append(Line& l,const wchar_t* format,...) noexcept {
    const std::size_t n=wcsnlen_s(l.text,_countof(l.text));
    va_list args;va_start(args,format);
    TextWritten(_vsnwprintf_s(l.text+n,_countof(l.text)-n,_TRUNCATE,format,args),l.text);
    va_end(args);
}
void TextWritten(int written,const wchar_t* text) noexcept {
    static bool logged=false;   // the draw thread's
    if(written>=0 || logged)return;
    logged=true;
    Log("HUD a line's text cut at %d characters (later cuts not logged): \"%.48ls...\"",kLineChars-1,text);
}
bool LineSink::Room(int n,const char* who) const noexcept {
    if(*at+n<=kMaxLines)return true;
    static bool logged=false;   // the draw thread's
    if(!logged) {
        logged=true;
        Log("HUD lines full: %s wanted %d more with %d of %d taken, left out (later overflows not logged)",who,n,*at,kMaxLines);
    }
    return false;
}
Line* LineSink::Push(const char* who) const noexcept {
    if(!Room(1,who))return nullptr;
    Line& l=lines[(*at)++];
    l.text[0]=0;l.world.on=false;
    return &l;
}
float TextScale(float scale) noexcept { return worldtext::Snap(scale,kTextScaleStep); }
alignas(16) const float kOutline[4]={0.0f,0.0f,0.0f,1.0f};
// The font descriptor at `scale`: its outline `outline` `wide`; `clear` the bits of the manager's flags (+0x30) dropped
// (1: draw writes the system constants; 2: begin turns the depth test off, docs/world-text-re.md §1.1 / §1.2).
void Font(Text& t,float scale,const float* outline=kOutline,float wide=1.7f,std::int32_t clear=0) noexcept {
    scale=TextScale(scale);
    std::memset(t.font,0,kFontSize);
    std::memcpy(t.font,t.mgr+kFontDefaults,0x18);
    Put<std::int32_t>(t.font,0x18,1);
    std::memcpy(t.font+0x1C,outline,16);
    Put<float>(t.font,0x2C,wide);   // the plain text's 1.7 as the name tags (0x804FB3)
    Put<std::int32_t>(t.font,kFontOwnFlags,At<std::int32_t>(t.mgr,kFontFlags)&~clear);
    Put<std::int32_t>(t.font,0x34,At<std::int32_t>(t.mgr,kFontFlags+4));
    Put<float>(t.font,4,scale);Put<float>(t.font,8,scale);   // 0x113A3F0
}
// The font scale a line is rasterized at: a world line's own (on the grid already), else its design scale times the HUD's.
float FontOf(const Text& t,const Line& l) noexcept { return l.world.on ? l.world.fd : hudscale::Font(l.scale,t.s); }
void Measure(Text& t,Line& l) noexcept {
    Font(t,FontOf(t,l));
    reinterpret_cast<TextBeginFn>(image+kTextBegin)(t.renderer,t.ctx,t.font);
    t.begun=true;
    alignas(16) float size[4]{};
    reinterpret_cast<TextMeasureFn>(image+kTextMeasure)(t.renderer,size,l.text,-1,false);
    t.begun=false;
    reinterpret_cast<TextEndFn>(image+kTextEnd)(t.renderer,t.ctx);
    l.w=std::isfinite(size[0]) && size[0]>0.0f ? size[0] : 0.0f;
    l.h=std::isfinite(size[1]) && size[1]>0.0f ? size[1] : 0.0f;
}
// The outline of the font begun (draw reads it from the font each time: docs/world-text-re.md §1.2, 0x113B7D0).
void Outline(Text& t,const float* outline,float wide) noexcept {
    std::memcpy(t.font+0x1C,outline,16);
    Put<float>(t.font,0x2C,wide);
}
void Draw(Text& t,const Line& l) noexcept {
    Font(t,FontOf(t,l));
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
void FitLine(Text* text,Line& row,float left,float right,bool shrink) noexcept {
    const float room=right-left;
    if(!text || !textOk || !(room>0.0f) || !(row.w>room))return;
    const float mid=row.y+row.h*0.5f;
    if(shrink) {
        const float floor=row.scale*textfit::kMinScale;
        for(int pass=0;pass<4 && row.w>room && row.scale>floor;++pass) {
            row.scale=std::fmax(floor,row.scale*room/row.w*0.96f);MeasureAll(*text,&row,1);
        }
    }
    if(row.w>room) {   // past the floor: the longest start that fits with the dots
        wchar_t whole[kLineChars];
        wcscpy_s(whole,row.text);
        const int n=static_cast<int>(std::wcslen(whole));
        const auto cut=[&](int k) noexcept {
            if(k>0 && k<n && textfit::HighSurrogate(whole[k-1]))--k;
            while(k>0 && textfit::Space(whole[k-1]))--k;
            wmemcpy(row.text,whole,static_cast<std::size_t>(k));
            wcscpy_s(row.text+k,static_cast<std::size_t>(kLineChars-k),L"...");
            MeasureAll(*text,&row,1);
        };
        int lo=0,hi=n-1<kLineChars-4 ? n-1 : kLineChars-4;   // lo: fits (the dots alone kept whatever they measure)
        while(lo<hi) {
            const int k=(lo+hi+1)/2;
            cut(k);
            if(row.w<=room)lo=k; else hi=k-1;
        }
        cut(lo);
    }
    row.x=std::fmax(left,std::fmin(row.x,right-row.w));row.y=mid-row.h*0.5f;
}
alignas(16) const float kClear[4]={0.0f,0.0f,0.0f,0.0f};
std::uint64_t worldDrawingKey=0;   // the key of the world line being drawn (for tools/hud_view's stand-in to tell plates apart)
// A world line (docs/world-text-re.md §5.1 / §6): its plate laid out for the camera (from behind: the face the back side,
// the text mirrored along it so it reads left to right), begun once with its own font (the system constants written, the
// depth test off), the renderer's view an identity and its projection the camera's viewProj, then its layers drawn from
// the furthest to the face (no depth test: the nearer over the further), the ones behind darker and fainter with no
// outline, the face in its own colour with the glow round it. `labels`: the world lines this frame (fewer layers with many).
void DrawWorld(Text& t,const Line& l,ULONGLONG now,int labels) noexcept {
    const WorldAt& w=l.world;
    if(!l.text[0] || !t.vp)return;
    const int layers=worldtext::LayersFor(w.depth,labels);
    worldtext::Plate p;
    worldtext::Lay(w.plate,w.at,w.w,w.h,w.align,w.mpp,layers,w.back,&p);
    alignas(16) float m[16],rgba[4],glow[4],layer[4];
    std::memcpy(rgba,l.rgba,16);
    rgba[3]*=worldtext::Shimmer(now,w.phase);
    worldtext::Glow(rgba,glow);
    Font(t,w.fd,glow,worldtext::kGlowWidth,3);
    worldDrawingKey=w.key;
    reinterpret_cast<TextBeginFn>(image+kTextBegin)(t.renderer,t.ctx,t.font);
    t.begun=true;
    unsigned char* const core=At<unsigned char*>(t.renderer,kRendererCore);
    if(core) {
        alignas(16) const float id[16]={1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f};
        std::memcpy(core+kCoreView,id,64);
        std::memcpy(core+kCoreProjection,t.vp,64);
        for(int k=layers-1;k>=0;--k) {
            worldtext::LayerColour(rgba,k,layers,layer);
            if(k)Outline(t,kClear,0.0f);
            else Outline(t,glow,worldtext::kGlowWidth);
            worldtext::LayerMatrix(p,k,m);
            reinterpret_cast<TextDrawFn>(image+kTextDraw)(t.renderer,t.ctx,m,layer,l.text,-1);
        }
    }
    t.begun=false;
    reinterpret_cast<TextEndFn>(image+kTextEnd)(t.renderer,t.ctx);
}
// After the world lines: the system constants (xgl_system, global, docs/world-text-re.md §1.4) the HUD's orthographic
// again, as every plain text draw leaves them, for whatever the game draws after (§5.1 step 5): a begin of its own and a
// non-empty, invisible string drawn (an empty one returns before it writes anything, 0x113B542): its colour and its
// outline both clear and the outline no width (a black outline round an invisible "." is a dot at the top left). Run
// whatever became of the world lines, a fault among them too.
void RestoreOrtho(Text& t) noexcept {
    __try {
        Font(t,hudscale::Font(kLineScale,t.s),kClear,0.0f,1);   // the most common size: no glyph set of its own
        reinterpret_cast<TextBeginFn>(image+kTextBegin)(t.renderer,t.ctx,t.font);
        t.begun=true;
        alignas(16) const float m[16]={1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f};
        reinterpret_cast<TextDrawFn>(image+kTextDraw)(t.renderer,t.ctx,m,kClear,L".",-1);
        t.begun=false;
        reinterpret_cast<TextEndFn>(image+kTextEnd)(t.renderer,t.ctx);
    } __except(TextFault(GetExceptionInformation())) {}
    EndOwed(t);
}
// The lines: the world ones first (under the HUD's own text; the furthest first, so a nearer plate is drawn over it), the
// system constants put back, then the 2D ones.
void DrawAll(Text& t,const Line* lines,int n) noexcept {
    if(!t.made)return;
    int order[kMaxLines],worlds=0;
    for(int i=0;i<n && i<kMaxLines;++i) {
        if(!lines[i].world.on)continue;
        int k=worlds++;
        for(;k>0 && lines[order[k-1]].world.depth<lines[i].world.depth;--k)order[k]=order[k-1];
        order[k]=i;
    }
    const ULONGLONG now=GetTickCount64();
    __try { for(int i=0;i<worlds && textOk;++i)DrawWorld(t,lines[order[i]],now,worlds); }
    __except(TextFault(GetExceptionInformation())) {}
    EndOwed(t);
    if(worlds)RestoreOrtho(t);
    __try { for(int i=0;i<n && textOk;++i)if(!lines[i].world.on)Draw(t,lines[i]); }
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
// A chord of seat buttons, all held at once (seat_switch_logic.h ChordHeld): "L3+R3".
void SeatChordName(int mask,wchar_t* out,int size) noexcept {
    static const wchar_t* names[]={L"A",L"B",L"X",L"Y",L"LB",L"RB",L"L3",L"R3"};
    out[0]=0;
    if(mask<=0){wcsncpy_s(out,size,Tr(Tx::controlUnbound),_TRUNCATE);return;}
    if(mask&~0xFF){std::swprintf(out,size,Tr(Tx::padButton),mask);return;}
    for(int i=0;i<8;++i)if(mask&(1<<i)) {
        if(out[0])wcscat_s(out,size,L"+");
        wcscat_s(out,size,names[i]);
    }
}
// A pad key's XInput bits (MapButton's: the d-pad 1 / 2 / 4 / 8, others by number) named.
void PadBitsName(int mask,wchar_t* key,int size) noexcept {
    wcsncpy_s(key,static_cast<std::size_t>(size),mask==0x0008 ? Tr(Tx::enemyKeyDpadRight) : mask==0x0004 ? Tr(Tx::enemyKeyDpadLeft) :
                  mask==0x0001 ? Tr(Tx::enemyKeyDpadUp) : mask==0x0002 ? Tr(Tx::enemyKeyDpadDown) :
                  mask<=0 ? Tr(Tx::controlUnbound) : L"?",_TRUNCATE);
    if(mask>0 && (mask&~0x000F))std::swprintf(key,static_cast<std::size_t>(size),Tr(Tx::padButton),mask);
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

// A line `t` pixels thick from (x0, y0) to (x1, y1): Rect's quad, turned (Rect's winding).
void Seg(void* drawer,void* ctx,float x0,float y0,float x1,float y1,float t,const float* rgba) noexcept {
    const float dx=x1-x0,dy=y1-y0,len=std::sqrt(dx*dx+dy*dy);
    if(!std::isfinite(len) || len<0.01f)return;
    const float nx=-dy/len*t*0.5f,ny=dx/len*t*0.5f;
    alignas(16) const float m[16]={1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f};
    alignas(16) const float v[12]={x0-nx,y0-ny,0.0f, x1-nx,y1-ny,0.0f, x0+nx,y0+ny,0.0f, x1+nx,y1+ny,0.0f};
    Emit(drawer,ctx,m,rgba,v);
}
// A triangle, its tip at (tx, ty), its base `half` either side of (bx, by) (Seg's winding; the strip's second
// triangle has the tip twice: nothing).
void Tri(void* drawer,void* ctx,float bx,float by,float tx,float ty,float half,const float* rgba) noexcept {
    const float dx=tx-bx,dy=ty-by,len=std::sqrt(dx*dx+dy*dy);
    if(!std::isfinite(len) || len<0.01f)return;
    const float nx=-dy/len*half,ny=dx/len*half;
    alignas(16) const float m[16]={1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f};
    alignas(16) const float v[12]={bx-nx,by-ny,0.0f, tx,ty,0.0f, bx+nx,by+ny,0.0f, tx,ty,0.0f};
    Emit(drawer,ctx,m,rgba,v);
}
// An arc of radius `r` round (cx, cy) from angle `from` (rad, 0 to the right, growing clockwise on the screen) over
// `span`, in `sides` straight pieces; a whole ring with span kTurn.
void Arc(void* drawer,void* ctx,float cx,float cy,float r,float from,float span,float t,int sides,const float* rgba) noexcept {
    for(int i=0;i<sides;++i) {
        const float a0=from+span*static_cast<float>(i)/static_cast<float>(sides),a1=from+span*static_cast<float>(i+1)/static_cast<float>(sides);
        Seg(drawer,ctx,cx+r*std::cos(a0),cy+r*std::sin(a0),cx+r*std::cos(a1),cy+r*std::sin(a1),t,rgba);
    }
}
// A line of text at (x, y): `align` 0 its left there, 1 its middle, 2 its right; its height centred on y.
void Label(Text* text,Line* lines,int* at,float x,float y,int align,float scale,const float* rgba,const wchar_t* format,...) noexcept {
    Line* const pushed=LineSink{lines,at}.Push(__func__);
    if(!pushed)return;
    Line& l=*pushed;
    va_list args;va_start(args,format);
    TextWritten(_vsnwprintf_s(l.text,_countof(l.text),_TRUNCATE,format,args),l.text);
    va_end(args);
    l.scale=scale;l.rgba=rgba;l.w=l.h=0.0f;l.world.on=false;
    if(text)MeasureAll(*text,&l,1);
    l.x=x-(align==1 ? l.w*0.5f : align==2 ? l.w : 0.0f);l.y=y-l.h*0.5f;
}

// The world text (docs/world-text-re.md §5, src/world_text.h; the user, 2026-11-21: 「贴在3d世界的文字，不管什么视角都能看见，
// 和全息文字类似」): on while InstallHud found the renderer's matrices where they should be and the ini's WorldText3D is on.
bool WorldText() noexcept { return worldTextOk && Cfg().worldText3D; }
// The plates' orientations (world_text.h Facings): a label's key to the way it was made facing; the draw thread's own,
// asked once a frame per key at most kFacingSlots (past that a label is drawn square on to the camera, uncached).
// The keys (worldtext::Key): a kind of label each and the identity of what it is of (WorldKey: the object, the warning,
// the mark's making; never a list index or a moving place), the map's view and the player's apart (a label gets a facing
// of its own in each, made square on to that view's camera when it first shows there: worldMapView). kGroundKey: a plate
// lying on the ground north up (the map's grid), kept by no key.
worldtext::Facings worldFacings{};
std::uint64_t WorldKey(WorldKind kind,std::uint64_t of) noexcept { return worldtext::Key(kind,of); }
// A label's plate (its back side held from the last frame near its edge): on the ground north up, else square on to
// the camera as it was when it first showed in this view (now, from `f`'s, when it is new or there is no slot for it).
bool PlateOf(const worldtext::Frame& f,std::uint64_t key,const float* at,const float* eye,worldtext::Standee* plate,bool* behind) noexcept {
    if(key==kGroundKey){*plate=worldtext::Ground(f.hand);*behind=worldtext::Behind(*plate,at,eye);return true;}
    worldtext::Standee fresh;
    if(!worldtext::FacingCamera(f,&fresh))return false;
    worldtext::FacingSlot* const slot=worldtext::FacingOf(worldFacings,worldMapView ? key^0x4000000000000000ull : key,GetTickCount64(),
                                                          worldFrame,fresh);
    if(!slot){*plate=fresh;*behind=false;return true;}
    *plate=slot->s;
    *behind=slot->back=worldtext::BehindHeld(slot->s,at,eye,slot->back);
    return true;
}
// A world line's face on the screen, its bounding box (what lays out by the lines' boxes and the offline checks read).
bool PlateBounds(const float* vp,float width,float height,const WorldAt& w,Line& l) noexcept {
    worldtext::Plate p;
    worldtext::Lay(w.plate,w.at,w.w,w.h,w.align,w.mpp,1,w.back,&p);
    float m[16];worldtext::LayerMatrix(p,0,m);
    float x0=1e30f,y0=1e30f,x1=-1e30f,y1=-1e30f;
    for(int i=0;i<4;++i) {
        float q[3],sx,sy;
        worldtext::Local(m,(i&1) ? w.w : 0.0f,(i&2) ? w.h : 0.0f,q);
        if(!worldtext::ToScreen(vp,width,height,q,&sx,&sy))return false;
        x0=sx<x0 ? sx : x0;y0=sy<y0 ? sy : y0;x1=sx>x1 ? sx : x1;y1=sy>y1 ? sy : y1;
    }
    l.x=x0;l.y=y0;l.w=x1-x0;l.h=y1-y0;
    return true;
}
// The world point (dx, dy) px from `anchor` on the screen, in the plane through it facing the camera: a label kept on the
// screen by moving its place there (EntranceMark / CommandHintMark clamp it in), its plate then standing there with no
// offset of its own. `anchor` itself when the camera has no frame there.
void ScreenShifted(const float* vp,float width,float height,const float* anchor,float dx,float dy,float* out) noexcept {
    worldtext::Frame f;
    const bool ok=worldtext::FrameAt(vp,width,height,anchor,&f);
    for(int i=0;i<3;++i)out[i]=ok ? anchor[i]+f.right[i]*dx/f.pxX+f.down[i]*dy/f.pxY : anchor[i];
}
// A label of something in the world: its anchor `anchor` (world), the line (dx, dy) px from where that is on the screen,
// `align` as Label's. With the world text on it stands in the world there as a hologram plate (world_text.h: square on to
// the camera as it was when the label of `key` first showed in this view, kept so; or on the ground north up for
// kGroundKey; its metres following the perspective while its height square on stays within world_text.h's bounds,
// layered for its thickness, a glow round its face, drawn first and seen through walls); the offset becomes metres along
// the plate at the anchor's depth (the 2D offset square on, not to the pixel at a slant). Else it is Label's flat line at
// the anchor's projection. Nothing while the anchor is off the screen (as Project says).
void WorldLabel(Text* text,Line* lines,int* at,const float* vp,float width,float height,const float* anchor,std::uint64_t key,
                float dx,float dy,int align,float scale,const float* rgba,const wchar_t* format,...) noexcept {
    float x,y,depth;
    if(!LineSink{lines,at}.Room(1,__func__) || !Project(vp,anchor,width,height,&x,&y,&depth))return;
    Line& l=*LineSink{lines,at}.Push(__func__);   // Room above
    va_list args;va_start(args,format);
    TextWritten(_vsnwprintf_s(l.text,_countof(l.text),_TRUNCATE,format,args),l.text);
    va_end(args);
    l.scale=scale;l.rgba=rgba;l.w=l.h=0.0f;l.world.on=false;
    worldtext::Frame f;
    worldtext::Standee plate;
    float eye[3];
    bool behind=false;
    if(text && WorldText() && worldtext::FrameAt(vp,width,height,anchor,&f) && worldtext::EyeOf(vp,eye) &&
       PlateOf(f,key,anchor,eye,&plate,&behind)) {
        const auto fit=worldtext::FontFor(hudscale::Font(scale,text->s),worldtext::SizeFactor(f.depth,worldRef),kTextScaleStep);
        WorldAt& w=l.world;
        w.on=true;w.back=behind;w.plate=plate;w.fd=fit.fd;w.mpp=fit.unitPx/f.pxY;w.align=align;w.depth=f.depth;
        w.phase=worldtext::PhaseOf(anchor);w.key=key;
        MeasureAll(*text,&l,1);   // glyph pixels at its font scale
        w.w=l.w;w.h=l.h;
        // The offset along the plate as it reads from the camera's side (its right mirrored from behind).
        const float side=behind ? -1.0f : 1.0f;
        for(int i=0;i<3;++i)w.at[i]=anchor[i]+plate.right[i]*side*dx/f.pxX+plate.down[i]*dy/f.pxY;
        if(PlateBounds(vp,width,height,w,l))return;
        l.w*=fit.unitPx;l.h*=fit.unitPx;
        worldtext::Place(x,y,dx,dy,align,l.w,l.h,&l.x,&l.y);
        return;
    }
    if(text)MeasureAll(*text,&l,1);
    worldtext::Place(x,y,dx,dy,align,l.w,l.h,&l.x,&l.y);
}
void ButtonName(int bits,wchar_t* out,int size) noexcept {
    static const wchar_t* const kNames[]={L"A",L"B",L"X",L"Y",L"LB",L"RB",L"L3",L"R3"};
    for(int i=0;i<8;++i)if(bits==(1<<i)){wcscpy_s(out,static_cast<rsize_t>(size),kNames[i]);return;}
    std::swprintf(out,static_cast<std::size_t>(size),L"0x%X",bits);
}
void Binding(bool keys,int key,int button,wchar_t* out,int size) noexcept {
    if(keys)KeyName(key,out,size);
    else ButtonName(button,out,size);
}
// A line through world points, broken where one is off the screen. Returns how many of the points are on it.
int WorldPolyline(void* drawer,void* ctx,const float* vp,float width,float height,const float (*pts)[3],int n,float t,
                  const float* rgba) noexcept {
    float px=0.0f,py=0.0f;
    bool had=false;
    int shown=0;
    for(int i=0;i<n;++i) {
        float x,y,depth;
        const bool on=Project(vp,pts[i],width,height,&x,&y,&depth);
        if(on && had)Seg(drawer,ctx,px,py,x,y,t,rgba);
        shown+=on ? 1 : 0;
        had=on;px=x;py=y;
    }
    return shown;
}
void WorldRing(void* drawer,void* ctx,const float* vp,float width,float height,const float* c,float radius,float t,
               const float* rgba) noexcept {
    constexpr int kSides=24;
    float pts[kSides+1][3];
    for(int i=0;i<=kSides;++i) {
        const float a=kTurn*static_cast<float>(i)/kSides;
        pts[i][0]=c[0]+radius*std::sin(a);pts[i][1]=c[1];pts[i][2]=c[2]+radius*std::cos(a);
    }
    WorldPolyline(drawer,ctx,vp,width,height,pts,kSides+1,t,rgba);
}
// The Balam's impact marker (barga_aim.cpp, docs/feature-barga-aim.md):
//  - each fist: a small reticle (four ticks round a gap, its letter) at the reach of the attack its button would start
//    now (L the LB side cyan, R the RB side green; bright when that button may start one, dim when not), red with
//    corner brackets on the enemy when an enemy's lock point is inside that attack's hit sphere; off the screen or
//    behind the camera, a chevron at the screen's border pointing the way to it. Its size is fixed, not the sphere's:
//    a ring as wide as the sphere took in everything behind it on the screen and read as "all of this gets hit";
//  - a lit sphere: a cross where it touches the ground or meets an enemy (the spheres themselves are left out: the
//    2026-11-27 run had them on the screen hardly ever, "spheres 0/3", at most 1/3);
//  - V605's cannon: a line through where the rounds still to come land, their blast rings every kRingEvery and the
//    last; a round that lands nowhere is left out.
// The clip coordinates of `p` (Project's, before the divide).
void Clip(const float* vp,const float* p,float* c) noexcept {
    for(int k=0;k<4;++k)c[k]=p[0]*vp[k]+p[1]*vp[4+k]+p[2]*vp[8+k]+vp[12+k];
}
}  // namespace hudi
}  // namespace crew
