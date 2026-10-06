// The aircraft's HUD drawn without the game, to look at its layout: src/hud.cpp included whole, its draw run on a
// stand-in "EDF.dll" image whose quad and text functions (the RVAs hud.cpp calls) jump to recorders here, for a few
// scenes of the warnings (warn.h) on a jet (one low on fuel: the fuel readout that replaces the stock FUEL gauge, LOW
// FUEL lit), a rotor craft and a stock heli, and the stock vehicles' HUD (a tank, the drill tank, a Nix, the Proteus
// walking behind its front shield and deployed with its field, barrier and a mark; at 16:9 and 21:9) under threat. Each
// scene's quads (as triangles) and text
// lines go to DIR/<scene>.txt; tools/hud_view.py turns them into PNGs. The text's size is a stand-in (the game's
// glyphs are not here: kGlyphH px a unit of font scale, kGlyphW of that a character), so read the layout, not the
// lettering.
//
// The stock HUD's layout is also checked: the RWR scope's box and the hull / turret block's (StockBlock, with its drill
// line, and with the Proteus's lines and bars) must not overlap at 16:9 or 21:9, nor leave the screen; exit code 1 when
// they do.
//
//   hud_view [--out DIR]      (default %TEMP%\edf6_hud_view), then: python tools/hud_view.py DIR
#include "../src/hud.cpp"
#include "../src/map_cam.h"
#include <cstdio>
#include <string>

namespace crew {
unsigned char* image=nullptr;
PlayerFix player{};
namespace {
Config config{};
// The scene: what the stubs hand the HUD.
bool hasTurret=false;
edf::aimlink::TurretReadoutV1 sceneTurret{};
bool hasJet=false,hasHeli=false,hasWarn=false,hasStock=false,hasDrill=false,hasNix=false,hasMap=false,hasEmc=false,hasProteus=false;
MapReadout sceneMap{};
ProteusReadout sceneProteus{};
PlayerJetReadout sceneJet{};
PlayerHeliReadout sceneHeli{};
Warnings sceneWarn{};
StockHudReadout sceneStock{};
DrillCue sceneDrill{};
EmcCue sceneEmc{};
NixTorso sceneNix{};
// The bounding box of what is drawn while `boxing` (the layout check).
struct Box { float x0,y0,x1,y1; bool any; };
bool boxing=false;
Box box{};
void Grow(float x,float y) {
    if(!box.any){box=Box{x,y,x,y,true};return;}
    box.x0=std::fmin(box.x0,x);box.y0=std::fmin(box.y0,y);box.x1=std::fmax(box.x1,x);box.y1=std::fmax(box.y1,y);
}
std::FILE* out=nullptr;
float fontScale=1.0f;
constexpr float kGlyphH=40.0f,kGlyphW=0.52f;

// The recorders the stand-in image's functions jump to.
void __fastcall QuadRec(void*,void*,const float* m,const float* rgba,std::int32_t,const float* v,std::int32_t n,void*) {
    float p[4][2];
    for(int i=0;i<n && i<4;++i){p[i][0]=v[i*3]*m[0]+v[i*3+1]*m[4]+m[12];p[i][1]=v[i*3]*m[1]+v[i*3+1]*m[5]+m[13];}
    if(boxing)for(int i=0;i<n && i<4;++i)Grow(p[i][0],p[i][1]);
    if(!out)return;
    for(int i=0;i+2<n && i<2;++i)
        std::fprintf(out,"T %.1f %.1f %.1f %.1f %.1f %.1f %.3f %.3f %.3f %.3f\n",p[i][0],p[i][1],p[i+1][0],p[i+1][1],p[i+2][0],p[i+2][1],
                     rgba[0],rgba[1],rgba[2],rgba[3]);
}
void* __fastcall MakeRec(void*,void* r) { return r; }
void __fastcall BeginRec(void*,void*,void* font) { fontScale=*reinterpret_cast<const float*>(static_cast<unsigned char*>(font)+4); }
void __fastcall MeasureRec(void*,float* size,const wchar_t* text,std::int64_t,bool) {
    size[0]=static_cast<float>(wcslen(text))*kGlyphH*kGlyphW*fontScale;size[1]=kGlyphH*fontScale;
}
void __fastcall DrawRec(void*,void*,const float* m,const float* rgba,const wchar_t* text,std::int64_t) {
    if(boxing){Grow(m[12],m[13]);Grow(m[12]+static_cast<float>(wcslen(text))*kGlyphH*kGlyphW*fontScale,m[13]+kGlyphH*fontScale);}
    if(!out)return;
    char narrow[256];
    WideCharToMultiByte(CP_UTF8,0,text,-1,narrow,sizeof(narrow),nullptr,nullptr);
    std::fprintf(out,"S %.1f %.1f %.1f %.3f %.3f %.3f %.3f %s\n",m[12],m[13],kGlyphH*fontScale,rgba[0],rgba[1],rgba[2],rgba[3],narrow);
}
void __fastcall EndRec(void*,void*) {}
void __fastcall FreeRec(void*) {}
void Jump(unsigned rva,const void* to) {   // mov rax, to; jmp rax
    unsigned char* p=image+rva;
    p[0]=0x48;p[1]=0xB8;std::memcpy(p+2,&to,8);p[10]=0xFF;p[11]=0xE0;
}
}  // namespace
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept { return GetTickCount64(); }
bool PlayerJetHud(PlayerJetReadout* o) noexcept { if(hasJet)*o=sceneJet;return hasJet; }
bool PlayerHeliHud(PlayerHeliReadout* o) noexcept { if(hasHeli)*o=sceneHeli;return hasHeli; }
bool WarnLatest(Warnings* o) noexcept { if(hasWarn)*o=sceneWarn;return hasWarn; }
bool PlayerHeliCue(HeliCue*) noexcept { return false; }
bool PlayerDrillCue(DrillCue* o) noexcept { if(hasDrill)*o=sceneDrill;return hasDrill; }
bool PlayerEmcCue(EmcCue* o) noexcept { if(hasEmc)*o=sceneEmc;return hasEmc; }
bool PlayerStockHud(StockHudReadout* o) noexcept { if(hasStock)*o=sceneStock;return hasStock; }
bool PlayerNixTorso(NixTorso* o) noexcept { if(hasNix)*o=sceneNix;return hasNix; }
bool PlayerProteus(ProteusReadout* o) noexcept { if(hasProteus)*o=sceneProteus;return hasProteus; }
bool PlayerTurretCam(TurretCamReadout*) noexcept { return false; }
bool PlayerTurretAim(edf::aimlink::TurretReadoutV1* o) noexcept { if(hasTurret)*o=sceneTurret;return hasTurret; }
bool PlayerSeatPrompt(SeatPrompt*) noexcept { return false; }
bool PlayerLauncher(LauncherReadout*) noexcept { return false; }
bool PlayerHeliSight(HeliSightReadout*) noexcept { return false; }
bool PlayerGunnerHud(GunnerReadout*) noexcept { return false; }
bool PlayerHighCam(bool*,bool*) noexcept { return false; }
bool PlayerMap(MapReadout* o) noexcept { if(hasMap)*o=sceneMap;return hasMap; }
bool MapOwnsView() noexcept { return hasMap; }
bool GearHudLatest(GearHud* g) noexcept {
    if(!hasJet || sceneJet.rotor)return false;
    *g=GearHud{};g->shown=true;g->at[0]=g->at[1]=g->at[2]=1.0f;g->warn=(sceneWarn.on>>kWarnGear&1u)!=0;g->tick=GetTickCount64();
    return true;
}
bool IsSub(const void*) noexcept { return false; }
bool IsJet(const void*) noexcept { return false; }
bool IsHelicopter(const void*) noexcept { return false; }
bool IsGroundRobo(const void*) noexcept { return false; }
bool IsDrillTank(const void*) noexcept { return false; }
bool JetHud(const void*,JetHudInfo*) noexcept { return false; }
bool HeliFuel(const void*,float*) noexcept { return false; }
void SetStockGaugeCover(bool) noexcept {}
}  // namespace crew

using namespace crew;

namespace {
// A level camera kBack m behind and kUp over `pos`, looking along `fwd` (level): the row-vector view-projection.
void Camera(const float* pos,const float* fwdIn,float width,float height,float* vp) {
    float f[3]={fwdIn[0],0.0f,fwdIn[2]};
    vec::Normalize(f);
    const float r[3]={-f[2],0.0f,f[0]},u[3]={0.0f,1.0f,0.0f};
    const float eye[3]={pos[0]-f[0]*24.0f,pos[1]+5.0f,pos[2]-f[2]*24.0f};
    const float fy=1.0f/std::tan(0.5f*55.0f*kDeg),fx=fy/(width/height),n=1.0f,fa=20000.0f,A=fa/(fa-n),B=-n*fa/(fa-n);
    auto dot=[](const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    for(int i=0;i<3;++i){vp[i*4]=r[i]*fx;vp[i*4+1]=u[i]*fy;vp[i*4+2]=f[i]*A;vp[i*4+3]=f[i];}
    vp[12]=-dot(eye,r)*fx;vp[13]=-dot(eye,u)*fy;vp[14]=-dot(eye,f)*A+B;vp[15]=-dot(eye,f);
}
// The map view's camera (map.cpp: mapcam::Place, its eye looking at the focus): the row-vector view-projection, its
// level right the game's (-f.z, 0, f.x) as Camera's, its up right x forward.
void MapCamera(const MapReadout& m,float width,float height,float* vp) {
    mapcam::View v{{m.focus[0],m.focus[1],m.focus[2]},m.yaw,m.pitch,m.height};
    float eye[3],look[3];
    mapcam::Place(v,eye,look);
    float f[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};
    vec::Normalize(f);
    float r[3]={-f[2],0.0f,f[0]};
    vec::Normalize(r);
    float u[3];vec::Cross(r,f,u);
    const float fy=1.0f/std::tan(0.5f*55.0f*kDeg),fx=fy/(width/height),n=1.0f,fa=20000.0f,A=fa/(fa-n),B=-n*fa/(fa-n);
    auto dot=[](const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    for(int i=0;i<3;++i){vp[i*4]=r[i]*fx;vp[i*4+1]=u[i]*fy;vp[i*4+2]=f[i]*A;vp[i*4+3]=f[i];}
    vp[12]=-dot(eye,r)*fx;vp[13]=-dot(eye,u)*fy;vp[14]=-dot(eye,f)*A+B;vp[15]=-dot(eye,f);
}
// A unit at (x, y, z) heading `headingDeg` (compass: 0 +z, growing to its right, -x), on flat ground at 0.
MapUnit& Unit(MapKind kind,float x,float y,float z,float headingDeg=-999.0f) {
    static MapUnit spare{};
    if(sceneMap.count>=kMapUnits)return spare;
    MapUnit& u=sceneMap.unit[sceneMap.count++];
    u=MapUnit{};
    u.pos[0]=x;u.pos[1]=y;u.pos[2]=z;u.ground=0.0f;u.hp=-1.0f;u.kind=kind;
    if(headingDeg>-900.0f){u.dir[0]=-std::sin(headingDeg*kDeg);u.dir[1]=std::cos(headingDeg*kDeg);}
    return u;
}
// A small enemy's dot at (x, y, z).
void Dot(float x,float y,float z,std::uint8_t flags) {
    if(sceneMap.dots>=kMapDots)return;
    MapDot& d=sceneMap.dot[sceneMap.dots++];
    d.pos[0]=x;d.pos[1]=y;d.pos[2]=z;d.flags=flags;
}
// The map at `height` m, looking `pitchDeg` down along heading `yawDeg`, round a player at the origin with the squad,
// two tanks, a heli, a jet, a carrier, a spread of enemies (some airborne) and two objective markers.
void MapScene(const std::wstring& dir,const wchar_t* name,float height,float pitchDeg,float yawDeg,bool pad) {
    sceneMap=MapReadout{};
    sceneMap.pad=pad;sceneMap.follow=true;
    sceneMap.yaw=yawDeg*kDeg;sceneMap.pitch=pitchDeg*kDeg;sceneMap.height=height;
    sceneMap.meDir[0]=std::sin(0.5f);sceneMap.meDir[2]=std::cos(0.5f);
    sceneMap.mapKey=0x4D;sceneMap.mapButton=0x20;
    for(int i=0;i<4;++i)Unit(MapKind::squad,std::cos(static_cast<float>(i)*1.6f)*12.0f,0.0f,std::sin(static_cast<float>(i)*1.6f)*12.0f-8.0f);
    for(int i=0;i<6;++i)Unit(MapKind::ally,140.0f+static_cast<float>(i)*9.0f,0.0f,-220.0f+static_cast<float>(i)*5.0f);
    Unit(MapKind::vehicle,-60.0f,0.0f,90.0f,30.0f);Unit(MapKind::vehicle,80.0f,0.0f,40.0f,-90.0f);
    Unit(MapKind::air,-150.0f,80.0f,300.0f,10.0f);Unit(MapKind::air,400.0f,300.0f,-100.0f,200.0f);
    Unit(MapKind::air,180.0f,60.0f,180.0f,-40.0f).flags=kMapRotor;   // a helicopter
    // Nobody's (team 5, map_marks.h): two parked jets on the ground and an empty tank.
    Unit(MapKind::air,-260.0f,0.0f,40.0f,90.0f).flags=kMapEmpty;Unit(MapKind::air,-260.0f,0.0f,-10.0f,90.0f).flags=kMapEmpty;
    Unit(MapKind::vehicle,30.0f,0.0f,-140.0f,0.0f).flags=kMapEmpty;
    Unit(MapKind::carrier,-500.0f,240.0f,-350.0f,45.0f).kind=MapKind::carrier;
    // The enemies nearest first (map.cpp Enemies): the nearest bracketed, two large ones with HP bars, a large flyer.
    // The small enemies (map.cpp Enemies): dots, a swarm of 300 ants and 40 flyers, the nearest one bracketed; the large
    // ones pins with HP bars, a large flyer among them.
    Dot(40.0f,0.0f,330.0f,kMapNearest);
    for(int i=0;i<300;++i)Dot(-400.0f+std::fmod(static_cast<float>(i)*137.0f,900.0f),0.0f,500.0f+std::fmod(static_cast<float>(i)*91.0f,700.0f),0);
    for(int i=0;i<40;++i)Dot(150.0f+std::fmod(static_cast<float>(i)*53.0f,300.0f),120.0f+static_cast<float>(i%5)*20.0f,
                             750.0f+std::fmod(static_cast<float>(i)*29.0f,250.0f),kMapFlying);
    MapUnit& big=Unit(MapKind::enemy,-120.0f,0.0f,520.0f,180.0f);big.flags=kMapLarge;big.hp=0.62f;
    MapUnit& big2=Unit(MapKind::enemy,320.0f,0.0f,700.0f,150.0f);big2.flags=kMapLarge;big2.hp=0.18f;
    MapUnit& ship=Unit(MapKind::enemyAir,-420.0f,400.0f,820.0f,90.0f);ship.flags=kMapLarge;ship.hp=0.9f;
    Unit(MapKind::lock,-120.0f,6.0f,522.0f);
    Unit(MapKind::marker,250.0f,0.0f,450.0f);Unit(MapKind::marker,-700.0f,0.0f,1200.0f);
    hasMap=true;
    const std::wstring path=dir+L"\\"+name+L".txt";
    if(_wfopen_s(&out,path.c_str(),L"w") || !out){hasMap=false;return;}
    std::fprintf(out,"W 1920 1080\n");
    float vp[16];
    MapCamera(sceneMap,1920.0f,1080.0f,vp);
    struct { std::int32_t x,y,w,h; } viewport{0,0,1920,1080};
    HudDraw(vp,image,&viewport,nullptr,0);
    std::fclose(out);out=nullptr;
    hasMap=false;
    std::printf("%ls\n",path.c_str());
}
void Symbols(PlayerJetSymbols& y,const float* pos,float pitchDeg,float pathDeg) {
    std::memcpy(y.pos,pos,12);
    y.nose[0]=0.0f;y.nose[1]=std::sin(pitchDeg*kDeg);y.nose[2]=std::cos(pitchDeg*kDeg);
    y.dir[0]=0.0f;y.dir[1]=std::sin(pathDeg*kDeg);y.dir[2]=std::cos(pathDeg*kDeg);
    y.moving=true;
}
void Threat(PlayerJetSymbols& y,int kind,float bearingDeg,float dist,float up) {
    const int i=y.threats++;
    y.threatKind[i]=kind;
    y.threatAt[i][0]=y.pos[0]-std::sin(bearingDeg*kDeg)*dist;   // its right is -x (heading +z)
    y.threatAt[i][1]=y.pos[1]+up;
    y.threatAt[i][2]=y.pos[2]+std::cos(bearingDeg*kDeg)*dist;
}
void Scene(const std::wstring& dir,const wchar_t* name,const float* pos,int width=1920) {
    const std::wstring path=dir+L"\\"+name+L".txt";
    if(_wfopen_s(&out,path.c_str(),L"w") || !out)return;
    std::fprintf(out,"W %d 1080\n",width);
    float vp[16];
    const float* fwd=hasStock ? sceneStock.hull : hasJet ? sceneJet.sym.nose : sceneHeli.sym.nose;
    Camera(pos,fwd,static_cast<float>(width),1080.0f,vp);
    HudPublish();
    struct { std::int32_t x,y,w,h; } viewport{0,0,width,1080};
    HudDraw(vp,image,&viewport,nullptr,0);
    std::fclose(out);out=nullptr;
    std::printf("%ls\n",path.c_str());
}

// A frame drawn and not recorded: what the HUD sees before a scene (a switch's banner needs the value before it).
void Prime(const float* pos,int width=1920) {
    float vp[16];
    const float* fwd=hasStock ? sceneStock.hull : hasJet ? sceneJet.sym.nose : sceneHeli.sym.nose;
    Camera(pos,fwd,static_cast<float>(width),1080.0f,vp);
    HudPublish();
    struct { std::int32_t x,y,w,h; } viewport{0,0,width,1080};
    HudDraw(vp,image,&viewport,nullptr,0);
}

// What `draw` puts on the screen (its quads and its text lines drawn), as a box.
template<class F> Box Measured(F draw) {
    alignas(16) unsigned char renderer[kRendererSize]{},font[kFontSize]{};
    static unsigned char ctx[16]{};
    Text text{};
    text.ctx=ctx;text.renderer=renderer;text.font=font;text.mgr=At<unsigned char*>(image,kFontMgr);
    Line lines[kMaxLines];
    int at=0;
    box=Box{};boxing=true;
    draw(&text,lines,&at);
    DrawAll(text,lines,at);
    FreeText(text);
    boxing=false;
    return box;
}

// The stock HUD's RWR scope (Threats' side for it) and its block, at `width` x 1080: apart.
bool StockLayoutApart(int width,const ProteusReadout* proteus=nullptr) {
    const float w=static_cast<float>(width),h=1080.0f,s=1.0f;
    void* const drawer=At<void*>(image,kQuadDrawer);
    static unsigned char ctx[16]{};
    PlayerJetSymbols y{};
    std::memcpy(y.pos,sceneStock.pos,12);std::memcpy(y.nose,sceneStock.hull,12);
    y.threats=sceneStock.threats;
    for(int i=0;i<y.threats;++i){std::memcpy(y.threatAt[i],sceneStock.threatAt[i],12);y.threatKind[i]=sceneStock.threatKind[i];}
    const Box rwr=Measured([&](Text* t,Line* l,int* at){RwrScope(drawer,ctx,t,w,h,s,y,0,GetTickCount64(),1.0f,l,at);});
    const StockExtras x{nullptr,proteus ? nullptr : &sceneDrill,false,proteus ? nullptr : &sceneEmc,proteus};   // the drill's
    // line and the EMC's (the tallest block), or the Proteus's
    const Box block=Measured([&](Text* t,Line* l,int* at){StockBlock(drawer,ctx,t,w,h,s,sceneStock,x,l,at);});
    const bool apart=rwr.x1<block.x0 || block.x1<rwr.x0 || rwr.y1<block.y0 || block.y1<rwr.y0;
    const bool inside=rwr.x1<=w && block.x0>=0.0f && block.y0>=0.0f && block.y1<=h;
    std::printf("%s  %dx1080%s: RWR scope (%.0f,%.0f)-(%.0f,%.0f), stock block (%.0f,%.0f)-(%.0f,%.0f)\n",apart && inside ? "ok  " : "FAIL",width,
                proteus ? " (Proteus)" : "",rwr.x0,rwr.y0,rwr.x1,rwr.y1,block.x0,block.y0,block.x1,block.y1);
    return apart && rwr.any && block.any && inside;
}

// EDF6AutoTurret's mode lines (TurretAimMarks: the state, the keys; the longest: the lead circle, a lock) at `width` x
// 1080: apart from the stock HUD's block and its RWR scope, on the screen, and saying plainly on or off.
bool TurretLayoutApart(int width) {
    const float w=static_cast<float>(width),h=1080.0f,s=1.0f;
    void* const drawer=At<void*>(image,kQuadDrawer);
    static unsigned char ctx[16]{};
    float vp[16];
    Camera(sceneStock.pos,sceneStock.hull,w,h,vp);
    edf::aimlink::TurretReadoutV1 r=sceneTurret;
    r.lock=edf::aimlink::Lock::locked;r.mode=edf::aimlink::Mode::leadCircle;r.lead=false;
    std::memset(r.at,0,12);r.at[2]=-1000.0f;   // behind the camera: no lock box, the lines alone
    bool off=false,on=false;
    const Box lines=Measured([&](Text* t,Line* l,int* at){
        TurretAimMarks(drawer,ctx,t,vp,w,h,s,r,false,l,at);
        for(int i=0;i<*at;++i)off=off || wcsstr(l[i].text,L"AUTO-AIM OFF")!=nullptr;
    });
    r.mode=edf::aimlink::Mode::autoAim;
    Measured([&](Text* t,Line* l,int* at){
        TurretAimMarks(drawer,ctx,t,vp,w,h,s,r,false,l,at);
        for(int i=0;i<*at;++i)on=on || (wcsstr(l[i].text,L"AUTO-AIM ON")!=nullptr) ;
    });
    PlayerJetSymbols y{};
    std::memcpy(y.pos,sceneStock.pos,12);std::memcpy(y.nose,sceneStock.hull,12);
    y.threats=sceneStock.threats;
    for(int i=0;i<y.threats;++i){std::memcpy(y.threatAt[i],sceneStock.threatAt[i],12);y.threatKind[i]=sceneStock.threatKind[i];}
    const Box rwr=Measured([&](Text* t,Line* l,int* at){RwrScope(drawer,ctx,t,w,h,s,y,0,GetTickCount64(),1.0f,l,at);});
    const StockExtras x{nullptr,&sceneDrill,false,&sceneEmc,nullptr};
    const Box block=Measured([&](Text* t,Line* l,int* at){StockBlock(drawer,ctx,t,w,h,s,sceneStock,x,l,at);});
    auto apart=[](const Box& a,const Box& b){return a.x1<b.x0 || b.x1<a.x0 || a.y1<b.y0 || b.y1<a.y0;};
    const bool ok=lines.any && apart(lines,rwr) && apart(lines,block) && lines.x0>=0.0f && lines.x1<=w && lines.y1<=h && on && off;
    std::printf("%s  %dx1080: aim-mode lines (%.0f,%.0f)-(%.0f,%.0f), ON %d OFF %d; block (%.0f,%.0f)-(%.0f,%.0f), RWR (%.0f,%.0f)-(%.0f,%.0f)\n",
                ok ? "ok  " : "FAIL",width,lines.x0,lines.y0,lines.x1,lines.y1,on,off,block.x0,block.y0,block.x1,block.y1,rwr.x0,rwr.y0,rwr.x1,rwr.y1);
    return ok;
}

// A stock tank (three weapons, a missile and a lock on it) at `pos`, heading +z, the turret 30 degrees right.
void StockTank(const float* pos) {
    sceneStock=StockHudReadout{};
    strcpy_s(sceneStock.kind,"403_Tank");
    std::memcpy(sceneStock.pos,pos,12);
    sceneStock.hull[2]=1.0f;
    sceneStock.aim[0]=-0.5f;sceneStock.aim[2]=0.866f;sceneStock.aimOk=true;
    std::memcpy(sceneStock.look,sceneStock.hull,12);sceneStock.lookOk=true;
    sceneStock.speed=12.0f;sceneStock.hp=3000.0f;sceneStock.hpMax=4000.0f;
    static const char* names[]={"CANNON","MG","MISSILE"};
    sceneStock.arms=3;sceneStock.selected=0;
    for(int i=0;i<3;++i) {
        StockArm& a=sceneStock.arm[i];
        strcpy_s(a.label,names[i]);a.ammo=10-i;a.ammoMax=10;a.reload=1.0f;a.reloadSec=-1.0f;a.canReload=true;a.kind=RoundKind::arc;
    }
    sceneStock.threats=2;
    sceneStock.threatKind[0]=2;sceneStock.threatAt[0][0]=pos[0]-600.0f;sceneStock.threatAt[0][1]=pos[1]+80.0f;sceneStock.threatAt[0][2]=pos[2]+900.0f;
    sceneStock.threatKind[1]=1;sceneStock.threatAt[1][0]=pos[0]+1500.0f;sceneStock.threatAt[1][1]=pos[1]+300.0f;sceneStock.threatAt[1][2]=pos[2]-800.0f;
}
}  // namespace

int wmain(int argc,wchar_t** argv) {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH,temp);
    std::wstring dir=std::wstring(temp)+L"edf6_hud_view";
    for(int i=1;i+1<argc;++i)if(!wcscmp(argv[i],L"--out"))dir=argv[i+1];
    CreateDirectoryW(dir.c_str(),nullptr);
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    static unsigned char fontMgr[0x200]{},drawer[16]{};
    *reinterpret_cast<void**>(image+kQuadDrawer)=drawer;
    *reinterpret_cast<void**>(image+kFontMgr)=fontMgr;
    Jump(kQuad,reinterpret_cast<const void*>(&QuadRec));
    Jump(kTextMake,reinterpret_cast<const void*>(&MakeRec));Jump(kTextBegin,reinterpret_cast<const void*>(&BeginRec));
    Jump(kTextMeasure,reinterpret_cast<const void*>(&MeasureRec));Jump(kTextDraw,reinterpret_cast<const void*>(&DrawRec));
    Jump(kTextEnd,reinterpret_cast<const void*>(&EndRec));Jump(kTextFree,reinterpret_cast<const void*>(&FreeRec));
    quadOk=textOk=true;
    const ULONGLONG now=GetTickCount64();
    const float pos[3]={0.0f,120.0f,0.0f};

    // A jet diving at the ground with two missiles and a lock on it, just launched, stalled, the gear not down.
    hasJet=true;hasHeli=false;hasWarn=true;
    sceneJet=PlayerJetReadout{};
    sceneJet.speed=95.0f;sceneJet.throttle=0.8f;sceneJet.clear=120.0f;sceneJet.climb=-45.0f;sceneJet.load=2.1f;sceneJet.air=true;
    sceneJet.ground=true;sceneJet.stall=true;sceneJet.liftShare=1.04f;sceneJet.pullUp=true;sceneJet.gpws=Gpws::pullUp;
    sceneJet.impactIn=2.4f;sceneJet.stores=3;sceneJet.store=1;
    static const char* names[]={"AIM-120","AGM-65","Mk 82","AIM-9X","Hydra 70","AGM-114"};
    static const int roles[]={0,1,2,0,3,1};
    for(int i=0;i<3;++i){sceneJet.storeName[i]=names[i];sceneJet.storeRounds[i]=4-i;sceneJet.storeRole[i]=roles[i];}
    sceneJet.fuel=FuelReading{true,0.62f,-1.0f};   // the 506 body's idle burn: no time worth showing
    sceneJet.guns=2;sceneJet.gunRounds=1786;
    Symbols(sceneJet.sym,pos,-10.0f,-25.0f);
    Threat(sceneJet.sym,2,150.0f,900.0f,30.0f);Threat(sceneJet.sym,2,-40.0f,2600.0f,200.0f);Threat(sceneJet.sym,1,20.0f,3800.0f,400.0f);
    sceneWarn=Warnings{};
    sceneWarn.on=1u<<kWarnPullUp|1u<<kWarnMissile|1u<<kWarnStall|1u<<kWarnGear|1u<<kWarnLock;
    for(auto& t:sceneWarn.litAt)t=now-5000;
    sceneWarn.launchAt=now-200;sceneWarn.tick=now;
    Scene(dir,L"jet_pullup",pos);

    // The same jet a moment earlier: TERRAIN 5 s out, a lift share near the stall, a lock only.
    sceneJet.gpws=Gpws::terrain;sceneJet.impactIn=5.0f;sceneJet.pullUp=false;sceneJet.stall=false;sceneJet.liftShare=0.9f;
    Symbols(sceneJet.sym,pos,2.0f,-5.0f);
    sceneJet.sym.threats=0;
    Threat(sceneJet.sym,1,-120.0f,1500.0f,0.0f);
    sceneWarn.on=1u<<kWarnTerrain|1u<<kWarnLock;sceneWarn.litAt[kWarnTerrain]=now-500;sceneWarn.launchAt=0;
    Scene(dir,L"jet_terrain",pos);

    // The same jet cruising low on fuel, 5:12 left at its burn: FUEL on the stores line, LOW FUEL lit.
    sceneJet.gpws=Gpws::none;sceneJet.impactIn=-1.0f;sceneJet.climb=0.0f;sceneJet.liftShare=0.5f;
    Symbols(sceneJet.sym,pos,3.0f,0.0f);
    sceneJet.sym.threats=0;
    sceneJet.fuel=FuelReading{true,0.08f,312.0f};
    sceneWarn.on=1u<<kWarnFuel;sceneWarn.litAt[kWarnFuel]=now-500;
    Scene(dir,L"jet_lowfuel",pos);
    sceneJet.fuel=FuelReading{true,0.62f,-1.0f};
    // All six stores, the switch from the AIM-120 to the AIM-9X just made: the loadout strip with the banner over it.
    sceneWarn.on=0;
    sceneJet.stores=6;sceneJet.store=0;
    for(int i=0;i<6;++i){sceneJet.storeName[i]=names[i];sceneJet.storeRounds[i]=i==2 ? 0 : 6-i;sceneJet.storeRole[i]=roles[i];}
    Prime(pos);
    sceneJet.store=3;
    Scene(dir,L"jet_switch",pos);
    sceneJet.stores=3;sceneJet.store=1;

    // A rotor craft sinking onto the ground, hovering slowly (the helicopter HUD).
    sceneJet.rotor=true;config.heliFlightHud=true;
    sceneJet.heli=HeliFlight{};
    sceneJet.heli.speed=3.0f;sceneJet.heli.clear=12.0f;sceneJet.heli.climb=-9.0f;sceneJet.heli.ground=true;sceneJet.heli.vel[1]=-9.0f;
    sceneJet.heli.gpws=Gpws::pullUp;sceneJet.heli.impactIn=1.3f;
    sceneJet.liftShare=0.0f;sceneJet.gpws=Gpws::pullUp;
    Symbols(sceneJet.sym,pos,0.0f,-70.0f);
    sceneJet.sym.threats=0;
    sceneWarn.on=1u<<kWarnPullUp;sceneWarn.litAt[kWarnPullUp]=now-100;
    Scene(dir,L"rotor_pullup",pos);

    // A stock heli, SINK RATE near the ground, an empty scope.
    hasJet=false;hasHeli=true;
    sceneHeli=PlayerHeliReadout{};
    sceneHeli.f.speed=12.0f;sceneHeli.f.clear=8.0f;sceneHeli.f.climb=-5.0f;sceneHeli.f.ground=true;sceneHeli.f.vel[2]=12.0f;sceneHeli.f.vel[1]=-5.0f;
    Symbols(sceneHeli.sym,pos,0.0f,-22.0f);
    sceneHeli.fuel=FuelReading{true,0.41f,1830.0f};
    sceneWarn.on=1u<<kWarnSinkRate;sceneWarn.litAt[kWarnSinkRate]=now-4000;
    Scene(dir,L"heli_sinkrate",pos);
    // The same stock heli with its weapons (StockHeliStores: the stock missile, the Hydra pod, the Hellfires; vhud.cpp
    // labels by the round's class), the rockets just picked.
    hasStock=true;
    StockTank(pos);
    sceneStock.heli=true;sceneStock.arms=3;sceneStock.threats=0;
    {
        static const char* labels[]={"MSL","RKT","MSL"};
        static const RoundKind kinds[]={RoundKind::homing,RoundKind::rocket,RoundKind::homing};
        for(int i=0;i<3;++i){StockArm& a=sceneStock.arm[i];strcpy_s(a.label,labels[i]);a.kind=kinds[i];a.ammo=i==1 ? 19 : 4;a.ammoMax=a.ammo;}
        sceneStock.arm[2].ammo=0;sceneStock.arm[2].reload=0.4f;sceneStock.arm[2].reloadSec=6.0f;
    }
    sceneStock.selected=0;
    Prime(pos);
    sceneStock.selected=1;
    Scene(dir,L"heli_stores",pos);
    hasStock=false;

    // The stock vehicles' HUD: a tank under a missile and a lock, at 16:9 and 21:9; the drill tank overheating; a Nix
    // with its torso twisted 40 degrees left of its legs.
    hasJet=hasHeli=hasWarn=false;hasStock=true;
    const float ground[3]={0.0f,0.0f,0.0f};
    StockTank(ground);
    Scene(dir,L"stock_tank",ground);
    Scene(dir,L"stock_tank_219",ground,2520);
    hasDrill=true;
    sceneDrill=DrillCue{1180.0f,1200.0f,0.93f,true,true};
    strcpy_s(sceneStock.kind,"DrillTank");
    Scene(dir,L"stock_drill",ground);
    int failed=0;
    failed+=!StockLayoutApart(1920);
    failed+=!StockLayoutApart(2520);
    // EDF6AutoTurret's turret (the tank's gun the player is at): auto-aim on, then flipped to the lead circle (its banner).
    hasTurret=true;
    sceneTurret=edf::aimlink::TurretReadoutV1{};
    sceneTurret.mode=edf::aimlink::Mode::autoAim;sceneTurret.keys=true;sceneTurret.ownGun=true;
    sceneTurret.modeKey=0x5A;sceneTurret.lockKey=0x51;sceneTurret.modeButton=0;sceneTurret.lockButton=0x04;
    Prime(ground);
    Scene(dir,L"stock_turret_auto",ground);
    sceneTurret.mode=edf::aimlink::Mode::leadCircle;
    Scene(dir,L"stock_turret_lead_switch",ground);
    failed+=!TurretLayoutApart(1920);
    failed+=!TurretLayoutApart(2520);
    hasTurret=false;
    hasDrill=false;
    // The EMC charging (62%) and firing (1.4 s of its beam left).
    hasEmc=true;
    strcpy_s(sceneStock.kind,"510_Maser");
    sceneStock.arms=1;sceneStock.selected=-1;sceneStock.threats=0;
    strcpy_s(sceneStock.arm[0].label,"MASER");sceneStock.arm[0].ammo=5000;sceneStock.arm[0].ammoMax=7000;sceneStock.arm[0].canReload=false;
    sceneEmc=EmcCue{0.62f,0.0f,0.0f,5,true,false,false};
    Scene(dir,L"stock_emc_charge",ground);
    sceneEmc=EmcCue{0.0f,1.4f,0.0f,4,false,true,false};
    Scene(dir,L"stock_emc_beam",ground);
    hasEmc=false;
    hasNix=true;
    strcpy_s(sceneStock.kind,"612_nix");
    sceneNix=NixTorso{};
    std::memcpy(sceneNix.at,ground,12);
    sceneNix.legsYaw=0.0f;sceneNix.twist=0.7f;sceneNix.twistMin=-1.4f;sceneNix.twistMax=1.4f;sceneNix.torsoYaw=0.7f;
    sceneNix.dir[0]=std::sin(0.7f);sceneNix.dir[2]=std::cos(0.7f);sceneNix.held=true;
    Scene(dir,L"stock_nix",ground);
    hasNix=false;
    // The map view (map.cpp): a medium view on keys, a high steep one on a pad, a low shallow one.
    hasStock=false;
    MapScene(dir,L"map_mid",700.0f,60.0f,20.0f,false);
    MapScene(dir,L"map_high_pad",3000.0f,85.0f,-40.0f,true);
    MapScene(dir,L"map_low",200.0f,32.0f,0.0f,false);
    // The Proteus: walking behind its front shield (the allies' focus up), then deployed: the field's ring, the barrier
    // taken down, the directional shield hot, a target marked, the salvo cooling down; its gunner's cannon in the list.
    StockTank(ground);
    strcpy_s(sceneStock.kind,"BigBegaruta");
    sceneStock.arms=0;sceneStock.aimOk=false;sceneStock.speed=9.0f;sceneStock.hp=9000.0f;sceneStock.hpMax=11000.0f;
    hasProteus=true;
    sceneProteus=ProteusReadout{};
    std::memcpy(sceneProteus.pos,ground,12);sceneProteus.hull[2]=1.0f;
    sceneProteus.driver=true;sceneProteus.keys=true;sceneProteus.mode=proteus::Mode::walk;sceneProteus.stagger=1.0f;
    sceneProteus.shieldOn=sceneProteus.shieldUp=true;sceneProteus.priority=true;sceneProteus.shieldHalfArc=60.0f*kDeg;
    sceneProteus.barrier=1.0f;sceneProteus.barrierHp=3300.0f;sceneProteus.salvoArmed=true;sceneProteus.salvoCooldown=30.0f;
    sceneProteus.modeKey=0x54;sceneProteus.modeButton=0x20;sceneProteus.shieldKey=0x42;sceneProteus.shieldButton=0x10;
    sceneProteus.markKey=0x51;sceneProteus.markButton=0x04;sceneProteus.salvoKey=0x02;
    Scene(dir,L"stock_proteus_walk",ground);
    sceneProteus.mode=proteus::Mode::deployed;sceneProteus.dirShield=true;sceneProteus.priority=false;sceneProteus.heat=0.74f;
    sceneProteus.barrier=0.42f;sceneProteus.marked=true;sceneProteus.markAt[0]=-120.0f;sceneProteus.markAt[1]=8.0f;sceneProteus.markAt[2]=420.0f;
    sceneProteus.markRange=437.0f;sceneProteus.salvoWait=12.4f;sceneProteus.gun=true;sceneProteus.fieldRadius=60.0f;sceneProteus.allies=5;
    sceneProteus.ringCount=kProteusRing;
    for(int i=0;i<kProteusRing;++i) {
        const float a=2.0f*3.14159265f*static_cast<float>(i)/static_cast<float>(kProteusRing);
        sceneProteus.ring[i][0]=60.0f*std::cos(a);sceneProteus.ring[i][1]=0.5f;sceneProteus.ring[i][2]=60.0f*std::sin(a);
    }
    sceneStock.arms=1;strcpy_s(sceneStock.arm[0].label,"CANNON");sceneStock.arm[0].ammo=140;sceneStock.arm[0].ammoMax=200;
    Scene(dir,L"stock_proteus_deployed",ground);
    Scene(dir,L"stock_proteus_deployed_219",ground,2520);
    failed+=!StockLayoutApart(1920,&sceneProteus);
    failed+=!StockLayoutApart(2520,&sceneProteus);
    hasProteus=false;
    std::printf(failed ? "layout: %d FAILED\n" : "layout: all apart\n",failed);
    return failed ? 1 : 0;
}
