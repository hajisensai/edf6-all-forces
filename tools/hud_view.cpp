// The aircraft's HUD drawn without the game, to look at its layout: src/hud.cpp included whole, its draw run on a
// stand-in "EDF.dll" image whose quad and text functions (the RVAs hud.cpp calls) jump to recorders here, for a few
// scenes of the warnings (warn.h) on a jet, a rotor craft and a stock heli. Each scene's quads (as triangles) and text
// lines go to DIR/<scene>.txt; tools/hud_view.py turns them into PNGs. The text's size is a stand-in (the game's
// glyphs are not here: kGlyphH px a unit of font scale, kGlyphW of that a character), so read the layout, not the
// lettering.
//
//   hud_view [--out DIR]      (default %TEMP%\edf6_hud_view), then: python tools/hud_view.py DIR
#include "../src/hud.cpp"
#include <cstdio>
#include <string>

namespace crew {
unsigned char* image=nullptr;
PlayerFix player{};
namespace {
Config config{};
// The scene: what the stubs hand the HUD.
bool hasJet=false,hasHeli=false,hasWarn=false;
PlayerJetReadout sceneJet{};
PlayerHeliReadout sceneHeli{};
Warnings sceneWarn{};
std::FILE* out=nullptr;
float fontScale=1.0f;
constexpr float kGlyphH=40.0f,kGlyphW=0.52f;

// The recorders the stand-in image's functions jump to.
void __fastcall QuadRec(void*,void*,const float* m,const float* rgba,std::int32_t,const float* v,std::int32_t n,void*) {
    float p[4][2];
    for(int i=0;i<n && i<4;++i){p[i][0]=v[i*3]*m[0]+v[i*3+1]*m[4]+m[12];p[i][1]=v[i*3]*m[1]+v[i*3+1]*m[5]+m[13];}
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
bool PlayerDrillCue(DrillCue*) noexcept { return false; }
bool PlayerLauncher(LauncherReadout*) noexcept { return false; }
bool PlayerHeliSight(HeliSightReadout*) noexcept { return false; }
bool PlayerGunnerHud(GunnerReadout*) noexcept { return false; }
bool PlayerHighCam(bool*,bool*) noexcept { return false; }
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
void Scene(const std::wstring& dir,const wchar_t* name,const float* pos) {
    const std::wstring path=dir+L"\\"+name+L".txt";
    if(_wfopen_s(&out,path.c_str(),L"w") || !out)return;
    std::fprintf(out,"W 1920 1080\n");
    float vp[16];
    const float* fwd=hasJet ? sceneJet.sym.nose : sceneHeli.sym.nose;
    Camera(pos,fwd,1920.0f,1080.0f,vp);
    HudPublish();
    struct { std::int32_t x,y,w,h; } viewport{0,0,1920,1080};
    HudDraw(vp,image,&viewport,nullptr,0);
    std::fclose(out);out=nullptr;
    std::printf("%ls\n",path.c_str());
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
    sceneWarn.on=1u<<kWarnSinkRate;sceneWarn.litAt[kWarnSinkRate]=now-4000;
    Scene(dir,L"heli_sinkrate",pos);
    return 0;
}
