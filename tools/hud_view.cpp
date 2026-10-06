// The aircraft's HUD drawn without the game, to look at its layout: src/hud.cpp included whole, its draw run on a
// stand-in "EDF.dll" image whose quad and text functions (the RVAs hud.cpp calls) jump to recorders here, for a few
// scenes of the warnings (warn.h) on a jet (one low on fuel: the fuel readout that replaces the stock FUEL gauge, LOW
// FUEL lit), a rotor craft and a stock heli, and the stock vehicles' HUD (a tank, the
// drill tank, a Nix; at 16:9 and 21:9) under threat. Each scene's quads (as triangles) and text
// lines go to DIR/<scene>.txt; tools/hud_view.py turns them into PNGs. The text's size is a stand-in (the game's
// glyphs are not here: kGlyphH px a unit of font scale, kGlyphW of that a character), so read the layout, not the
// lettering.
//
// The stock HUD's layout is also checked: the RWR scope's box and the hull / turret block's (StockBlock, with its drill
// line) must not overlap at 16:9 or 21:9; exit code 1 when they do.
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
bool hasJet=false,hasHeli=false,hasWarn=false,hasStock=false,hasDrill=false,hasNix=false,hasMap=false,hasEmc=false;
MapReadout sceneMap{};
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
bool PlayerTurretCam(TurretCamReadout*) noexcept { return false; }
bool PlayerTurretAim(edf::aimlink::TurretReadoutV1*) noexcept { return false; }
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
void Unit(MapKind kind,float x,float y,float z) {
    if(sceneMap.count>=kMapUnits)return;
    MapUnit& u=sceneMap.unit[sceneMap.count++];
    u.pos[0]=x;u.pos[1]=y;u.pos[2]=z;u.kind=kind;
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
    Unit(MapKind::vehicle,-60.0f,0.0f,90.0f);Unit(MapKind::vehicle,80.0f,0.0f,40.0f);
    Unit(MapKind::air,-150.0f,80.0f,300.0f);Unit(MapKind::air,400.0f,300.0f,-100.0f);
    Unit(MapKind::carrier,-500.0f,0.0f,-350.0f);
    for(int i=0;i<40;++i)Unit(MapKind::enemy,-300.0f+std::fmod(static_cast<float>(i)*137.0f,700.0f),0.0f,600.0f+std::fmod(static_cast<float>(i)*91.0f,500.0f));
    for(int i=0;i<3;++i)Unit(MapKind::enemyAir,200.0f+static_cast<float>(i)*60.0f,250.0f,900.0f);
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
bool StockLayoutApart(int width) {
    const float w=static_cast<float>(width),h=1080.0f,s=1.0f;
    void* const drawer=At<void*>(image,kQuadDrawer);
    static unsigned char ctx[16]{};
    PlayerJetSymbols y{};
    std::memcpy(y.pos,sceneStock.pos,12);std::memcpy(y.nose,sceneStock.hull,12);
    y.threats=sceneStock.threats;
    for(int i=0;i<y.threats;++i){std::memcpy(y.threatAt[i],sceneStock.threatAt[i],12);y.threatKind[i]=sceneStock.threatKind[i];}
    const Box rwr=Measured([&](Text* t,Line* l,int* at){RwrScope(drawer,ctx,t,w,h,s,y,0,GetTickCount64(),1.0f,l,at);});
    const StockExtras x{nullptr,&sceneDrill,false,&sceneEmc};   // the drill's line and the EMC's: the tallest block
    const Box block=Measured([&](Text* t,Line* l,int* at){StockBlock(drawer,ctx,t,w,h,s,sceneStock,x,l,at);});
    const bool apart=rwr.x1<block.x0 || block.x1<rwr.x0 || rwr.y1<block.y0 || block.y1<rwr.y0;
    std::printf("%s  %dx1080: RWR scope (%.0f,%.0f)-(%.0f,%.0f), stock block (%.0f,%.0f)-(%.0f,%.0f)\n",apart ? "ok  " : "FAIL",width,
                rwr.x0,rwr.y0,rwr.x1,rwr.y1,block.x0,block.y0,block.x1,block.y1);
    return apart && rwr.any && block.any && rwr.x1<=w && block.x0>=0.0f;
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
    static const char* names[]={"AAM","AGM","BOMB"};
    for(int i=0;i<3;++i){sceneJet.storeName[i]=names[i];sceneJet.storeRounds[i]=4-i;}
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
    std::printf(failed ? "layout: %d FAILED\n" : "layout: all apart\n",failed);
    return failed ? 1 : 0;
}
