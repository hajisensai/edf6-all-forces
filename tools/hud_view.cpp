// The aircraft's HUD drawn without the game, to look at its layout: src/hud.cpp included whole, its draw run on a
// stand-in "EDF.dll" image whose quad and text functions (the RVAs hud.cpp calls) jump to recorders here, for a few
// scenes of the warnings (warn.h) on a jet (one low on fuel: the fuel readout that replaces the stock FUEL gauge, LOW
// FUEL lit), a rotor craft and a stock heli, and the stock vehicles' HUD (a tank, the drill tank, a Nix, the Proteus
// walking behind its front shield and deployed with its field, barrier and a mark; at 16:9 and 21:9) under threat, and
// the Sazabi's own HUD (idle, no hit, not centred, boosting, low thrust, overheated, each special: the missiles locking /
// locked, the funnels out, the cannon charging / cooling; every tag lit; on 1280x720, 3840x2160, split screens and
// HudScale 1.5 too; its layout checked on the scale cases below: SazabiLayoutChecks). Each
// scene's quads (as triangles) and text
// lines go to DIR/<scene>.txt; tools/hud_view.py turns them into PNGs. The text's size is a stand-in (the game's
// glyphs are not here: kGlyphH px a unit of font scale, kGlyphW of that a character), so read the layout, not the
// lettering.
//
// The stock HUD's layout is also checked: the RWR scope's box and the hull / turret block's (StockBlock, with its drill
// line, and with the Proteus's lines and bars) must not overlap at 16:9 or 21:9, nor leave the screen; exit code 1 when
// they do.
//
// The HUD's scale (src/hudscale.h) is checked too: hudscale::Of on the typical screens and split viewports, and the
// stock HUD drawn whole (HudDraw) at 1280x720 .. 3840x2160 and in split viewports, its text and its block / RWR scope
// against the 1080-line design times the scale, in size and in place; exit code 1 when one is off.
//
// Every scene is drawn in each of the HUD's languages (src/hudtext.h): English into DIR, the others into DIR\<language>
// (zh-CN, zh-TW, ja). The stand-in text is as wide as the game's fonts make it (the game's font chain, docs/hud-re.md
// §11: a CJK character a full em; Latin half an em in English and Japanese, whose first font is the monospaced New
// Cezanne, and the Chinese fonts' own advances in Chinese). In each language every scene's text must stay on the
// screen, and no two of its lines may overlap that do not in English; the layout checks above run in each language too.
// Each scene's glyphs (character and font scale: the game's glyph cache keys, §2.1) are counted.
//
//   hud_view [--out DIR] [--lang en|zh-CN|zh-TW|ja]      (default %TEMP%\edf6_hud_view, every language), then:
//   python tools/hud_view.py DIR
#include "../src/turretcam.h"
#include "../src/hud.cpp"
#include "../src/player_name.h"
#include "../src/map_cam.h"
#include <array>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace crew {
bool VehicleLookHint(bool*) noexcept { return false; }
unsigned char* image=nullptr;
PlayerFix player{};
namespace {
Config config{};
// The scene: what the stubs hand the HUD.
bool hasTurret=false;
edf::aimlink::TurretReadoutV1 sceneTurret{};
TurretCamReadout sceneMouseIntent{};bool hasMouseIntent=false,hasMountedOptic=false;
edf::aimlink::ModeBindingV1 sceneModeBinding{};bool hasModeBinding=false;
bool hasJet=false,hasHeli=false,hasWarn=false,hasStock=false,hasDrill=false,hasNix=false,hasMap=false,hasEmc=false,hasProteus=false,
     hasSazabi=false;
bool paused=false;   // the game's pause flag (crew.cpp GamePaused)
// When the scene's readouts were taken (the game thread's tick): before HudDraw takes its own `now`. The gear's read the
// tick in the middle of the draw, a 16 ms tick later at times: `now - tick` wrapped and the gear panel was not drawn.
ULONGLONG sceneTick=0;
bool mapEasing=false;   // the map's camera easing back to the player: its view, no readout (map.cpp Camera)
MapReadout sceneMap{};
bool hasMapPayload=false;PayloadReadout sceneMapPayload{};
ProteusReadout sceneProteus{};
PlayerJetReadout sceneJet{};
PlayerHeliReadout sceneHeli{};
Warnings sceneWarn{};
StockHudReadout sceneStock{};
DrillCue sceneDrill{};
EmcCue sceneEmc{};
NixTorso sceneNix{};
SazabiCue sceneSazabi{};
// The bounding box of what is drawn while `boxing` (the layout check).
struct Box { float x0,y0,x1,y1; bool any; };
bool boxing=false;
Box box{};
int drawn=0;   // the quads and text lines drawn while `boxing`
void Grow(float x,float y) {
    if(!box.any){box=Box{x,y,x,y,true};return;}
    box.x0=std::fmin(box.x0,x);box.y0=std::fmin(box.y0,y);box.x1=std::fmax(box.x1,x);box.y1=std::fmax(box.y1,y);
}
std::FILE* out=nullptr;
int scopePieces=0;
float fontScale=1.0f,tallest=0.0f;   // the font scale begun; the tallest glyph drawn since `tallest` was zeroed
// The game's screen hud.cpp reads (kUiScreen): the global points at a holder, the holder's +0x10 at the screen, its
// width and height at +0x20 / +0x24.
alignas(16) unsigned char uiScreen[0x40]{},uiHolder[0x20]{};
void SetScreen(int w,int h) {
    std::memcpy(uiScreen+kUiW,&w,4);std::memcpy(uiScreen+kUiH,&h,4);
}
constexpr float kGlyphH=40.0f,kGlyphW=0.52f;
// A character's advance in em (see the top): CJK and full-width a whole em; Latin by the language's first font.
// kChineseLatin: the Chinese fonts' (AR UDJingXiHei G30 / B5, Root.cpk UI/) advances of ' ' .. '~'.
constexpr float kChineseLatin[95]={0.283f,0.344f,0.449f,0.706f,0.553f,0.862f,0.728f,0.25f,0.33f,0.33f,0.506f,0.543f,0.295f,0.464f,
    0.268f,0.418f,0.54f,0.54f,0.54f,0.54f,0.54f,0.54f,0.54f,0.54f,0.54f,0.54f,0.302f,0.303f,0.544f,0.544f,0.544f,0.539f,0.714f,
    0.678f,0.699f,0.672f,0.712f,0.632f,0.618f,0.684f,0.737f,0.292f,0.55f,0.694f,0.6f,0.874f,0.762f,0.706f,0.678f,0.709f,0.699f,
    0.635f,0.588f,0.725f,0.623f,0.929f,0.623f,0.592f,0.61f,0.349f,0.418f,0.349f,0.624f,0.585f,0.46f,0.571f,0.582f,0.509f,0.582f,
    0.546f,0.329f,0.587f,0.6f,0.262f,0.279f,0.564f,0.305f,0.945f,0.599f,0.549f,0.582f,0.582f,0.464f,0.535f,0.348f,0.6f,0.531f,
    0.814f,0.508f,0.522f,0.504f,0.371f,0.358f,0.371f,0.495f};
float Advance(wchar_t c) {
    if(c>=0x2E80)return 1.0f;
    const hudtext::Lang l=hudtext::InUse();
    if((l==hudtext::Lang::zhCN || l==hudtext::Lang::zhTW) && c>=0x20 && c<0x7F)return kChineseLatin[c-0x20];
    return kGlyphW;
}
float Width(const wchar_t* text) {
    float w=0.0f;
    for(;*text;++text)w+=Advance(*text);
    return w*kGlyphH*fontScale;
}
// The text lines a scene drew (while `recording`) and its glyphs: (character, font scale in kTextScaleStep units).
struct Drew { float x0,y0,x1,y1; std::wstring text; };
std::vector<Drew> drew;
std::set<std::pair<wchar_t,int>> glyphs;

// The recorders the stand-in image's functions jump to.
struct PanelBox { float x0,y0,x1,y1; };
std::vector<PanelBox> panels;   // the scene's panels (the HUD's kPanel rects) while `recording`
bool recording=false;
bool keepHud=false;std::vector<std::array<float,8>> hudQuads;   // the HUD-green quads drawn while keepHud (the glass scene)
bool probeIntent=false;float intentX=0,intentY=0;int intentQuads=0;
void __fastcall QuadRec(void*,void*,const float* m,const float* rgba,std::int32_t,const float* v,std::int32_t n,void*) {
    if(rgba==kScopeDark)++scopePieces;
    float p[4][2];
    for(int i=0;i<n && i<4;++i){p[i][0]=v[i*3]*m[0]+v[i*3+1]*m[4]+m[12];p[i][1]=v[i*3]*m[1]+v[i*3+1]*m[5]+m[13];}
    if(probeIntent && n==4 && rgba==kWhite) {
        float x=0,y=0;for(int i=0;i<4;++i){x+=p[i][0]*0.25f;y+=p[i][1]*0.25f;}
        if(std::fabs(x-intentX)<17.0f && std::fabs(y-intentY)<17.0f)++intentQuads;
    }
    if(recording && n==4 && std::memcmp(rgba,kPanel,16)==0 && p[0][1]==p[1][1] && p[0][0]==p[2][0])
        panels.push_back(PanelBox{p[0][0],p[0][1],p[3][0],p[3][1]});
    if(boxing){++drawn;for(int i=0;i<n && i<4;++i)Grow(p[i][0],p[i][1]);}
    if(keepHud && n==4 && rgba==kHud)hudQuads.push_back({p[0][0],p[0][1],p[1][0],p[1][1],p[2][0],p[2][1],p[3][0],p[3][1]});
    if(!out)return;
    for(int i=0;i+2<n && i<2;++i)
        std::fprintf(out,"T %.1f %.1f %.1f %.1f %.1f %.1f %.3f %.3f %.3f %.3f\n",p[i][0],p[i][1],p[i+1][0],p[i+1][1],p[i+2][0],p[i+2][1],
                     rgba[0],rgba[1],rgba[2],rgba[3]);
}
int activeTextRenderers=0;
void* __fastcall MakeRec(void*,void* r) { ++activeTextRenderers;return r; }
void __fastcall BeginRec(void*,void*,void* font) { fontScale=*reinterpret_cast<const float*>(static_cast<unsigned char*>(font)+4); }
void __fastcall MeasureRec(void*,float* size,const wchar_t* text,std::int64_t,bool) {
    size[0]=Width(text);size[1]=kGlyphH*fontScale;
}
void __fastcall DrawRec(void*,void*,const float* m,const float* rgba,const wchar_t* text,std::int64_t) {
    tallest=std::fmax(tallest,kGlyphH*fontScale);
    if(boxing){++drawn;Grow(m[12],m[13]);Grow(m[12]+Width(text),m[13]+kGlyphH*fontScale);}
    if(recording) {
        drew.push_back(Drew{m[12],m[13],m[12]+Width(text),m[13]+kGlyphH*fontScale,text});
        const int step=static_cast<int>(std::lround(fontScale/kTextScaleStep));
        for(const wchar_t* c=text;*c;++c)if(*c!=L' ')glyphs.insert({*c,step});
    }
    if(!out)return;
    char narrow[256];
    WideCharToMultiByte(CP_UTF8,0,text,-1,narrow,sizeof(narrow),nullptr,nullptr);
    std::fprintf(out,"S %.1f %.1f %.1f %.3f %.3f %.3f %.3f %s\n",m[12],m[13],kGlyphH*fontScale,rgba[0],rgba[1],rgba[2],rgba[3],narrow);
}
void __fastcall EndRec(void*,void*) {}
void __fastcall FreeRec(void*) { --activeTextRenderers; }
void Jump(unsigned rva,const void* to) {   // mov rax, to; jmp rax
    unsigned char* p=image+rva;
    p[0]=0x48;p[1]=0xB8;std::memcpy(p+2,&to,8);p[10]=0xFF;p[11]=0xE0;
}
}  // namespace
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
ULONGLONG GameMs() noexcept { return GetTickCount64(); }
bool GamePaused() noexcept { return paused; }
bool PlayerJetHud(PlayerJetReadout* o) noexcept { if(hasJet)*o=sceneJet;return hasJet; }
bool PlayerHeliHud(PlayerHeliReadout* o) noexcept { if(hasHeli)*o=sceneHeli;return hasHeli; }
bool WarnLatest(Warnings* o) noexcept { if(hasWarn)*o=sceneWarn;return hasWarn; }
bool PlayerHeliCue(HeliCue*) noexcept { return false; }
bool PlayerDrillCue(DrillCue* o) noexcept { if(hasDrill)*o=sceneDrill;return hasDrill; }
bool PlayerEmcCue(EmcCue* o) noexcept { if(hasEmc)*o=sceneEmc;return hasEmc; }
bool PlayerPayload(PayloadReadout* o) noexcept { if(hasMapPayload){*o=sceneMapPayload;return true;}if(hasHeli || hasStock){*o=PayloadReadout{};o->choices=hasStock ? sceneStock.arms : 3;o->switchButton=0x10;o->keys=hasHeli ? sceneHeli.f.keys : true;}return hasHeli || hasStock; }
bool PlayerSelectablePayload(PayloadReadout* o) noexcept { return PlayerPayload(o); }
bool PlayerStockHud(StockHudReadout* o) noexcept { if(hasStock)*o=sceneStock;return hasStock; }
bool PlayerNixTorso(NixTorso* o) noexcept { if(hasNix)*o=sceneNix;return hasNix; }
bool PlayerProteus(ProteusReadout* o) noexcept { if(hasProteus)*o=sceneProteus;return hasProteus; }
bool PlayerSazabiCue(SazabiCue* o) noexcept { if(hasSazabi)*o=sceneSazabi;return hasSazabi; }
bool hasScreens=false;CockpitScreens sceneScreens{};   // a vehicle's first person: its cockpit's screens
unsigned char* PlayerHuman() noexcept { return nullptr; }
bool FirstPersonScreens(CockpitScreens* o,const unsigned char*) noexcept { if(hasScreens)*o=sceneScreens;return hasScreens; }
instruments::Readings deliveredInstruments{};
void FirstPersonInstrumentReadings(const instruments::Readings& r,const unsigned char*) noexcept { deliveredInstruments=r; }
bool PlayerTurretCam(TurretCamReadout* value) noexcept { if(hasMouseIntent)*value=sceneMouseIntent;return hasMouseIntent; }
bool SightZoomMounted(const void*) noexcept { return hasMountedOptic; }
bool PlayerTurretBinding(edf::aimlink::ModeBindingV1* value) noexcept { if(hasModeBinding)*value=sceneModeBinding;return hasModeBinding; }
bool PlayerTurretAim(edf::aimlink::TurretReadoutV1* o) noexcept { if(hasTurret)*o=sceneTurret;return hasTurret; }
bool PlayerSeatPrompt(SeatPrompt*) noexcept { return false; }
bool PlayerBoardingEntrance(BoardingEntrance*) noexcept { return false; }
bool hasLauncher=false;LauncherReadout sceneLauncher{};
bool PlayerLauncher(LauncherReadout* o) noexcept {if(hasLauncher)*o=sceneLauncher;return hasLauncher;}
// The stock heli gun's sight (helisight.cpp): on in the heli_gun_ladder scenes.
bool hasHeliSight=false;HeliSightReadout sceneHeliSight{};
bool PlayerHeliSight(HeliSightReadout* o) noexcept { if(hasHeliSight)*o=sceneHeliSight;return hasHeliSight; }
// The sight's magnification (sightzoom.cpp): the zoomed scenes set it (and narrow sceneFov to match).
float sceneZoom=1.0f;
float SightZoomNow(const void*) noexcept { return sceneZoom; }
sightzoom::Kind sceneSight=sightzoom::Kind::optical;
sightzoom::Kind SightZoomView(const void*) noexcept { return sceneSight; }
// The soldier looking through the scenes' sight (sightzoom.cpp SightZoomScope): the field as the plugin answers it.
const int sceneScopeSoldier=0;
bool SightZoomScope(SightScope* o) noexcept {
    *o=SightScope{};
    if(!sightzoom::ScopeField(sceneSight,sceneZoom,hasMountedOptic))return false;
    o->human=&sceneScopeSoldier;o->kind=sceneSight;o->zoom=sceneZoom;
    return true;
}
// A soldier's open scope (infantryscope.cpp): on in the infantry_scope scenes.
bool hasInfantry=false;InfantryScopeCue sceneInfantry{};
bool InfantryScopeNow(InfantryScopeCue* o) noexcept { if(hasInfantry && o)*o=sceneInfantry;return hasInfantry; }
bool hasGunner=false;GunnerReadout sceneGunner{};
bool PlayerGunnerHud(GunnerReadout* o) noexcept { if(hasGunner)*o=sceneGunner;return hasGunner; }
bool DreadnoughtGunnerHud(GunnerReadout*) noexcept { return false; }
bool PlayerDreadnoughtCue(DreadnoughtCue*) noexcept { return false; }
bool hasHighView=false;
bool PlayerHighCam(bool* on,bool* keys) noexcept {*on=hasHighView;*keys=true;return hasHighView;}
bool PlayerMap(MapReadout* o) noexcept { if(hasMap)*o=sceneMap;return hasMap; }
bool MapOwnsView() noexcept { return hasMap || mapEasing; }
const void* sceneMapHiddenCamera=nullptr;
bool MapHidesStockHud(const void* camera) noexcept {
    return (hasMap || mapEasing) && (!sceneMapHiddenCamera || camera==sceneMapHiddenCamera);
}
MapCommandReadout sceneCmd{};
bool PlayerMapCommands(MapCommandReadout* o) noexcept { if(hasMap)*o=sceneCmd;return hasMap; }
// The damage statistics (damagestats.cpp): on in every map scene (the map's STATS tab), the page shown in StatsScenes.
dmgstat::Book sceneBook;dmgstat::View sceneStatsView{};std::uint32_t sceneMissionMs=0;int sceneStatsTargets=0;
bool DamageStatsRead(dmgstat::Book* b,bool* fresh,dmgstat::View* v,std::uint32_t* ms) noexcept {
    *fresh=b->serial!=sceneBook.serial;
    if(*fresh)std::memcpy(b,&sceneBook,sizeof(sceneBook));
    *v=sceneStatsView;*ms=sceneMissionMs;
    return true;
}
void DamageStatsUi(const float*,const int*,int n,int,int) noexcept { sceneStatsTargets=n; }
// The NPCs' mark (npcai.cpp): none in these scenes but the ground one (GroundScene sets sceneMark).
bool sceneMarkOn=false;float sceneMark[3]{};
bool NpcMarkReadout(float* at) noexcept { if(sceneMarkOn)std::memcpy(at,sceneMark,12);return sceneMarkOn; }
// The Q marks (qmark.cpp) with it: ours on that enemy, a teammate's (P2) on another one, a teammate's (P3) point.
int QMarkViews(QMarkView* to,int max) noexcept {
    if(!sceneMarkOn || max<3)return 0;
    to[0]=QMarkView{true,true,-1,{sceneMark[0],sceneMark[1],sceneMark[2]}};
    to[1]=QMarkView{false,true,1,{sceneMark[0]-60.0f,sceneMark[1]+4.0f,sceneMark[2]+40.0f}};
    to[2]=QMarkView{false,false,2,{sceneMark[0]+45.0f,sceneMark[1]-6.0f,sceneMark[2]-60.0f}};
    // Names as the game's name tag gives them (player_name.cpp FitName): a Japanese one, and a long mixed one cut.
    const wchar_t* jp=L"さくら_EDF";
    const wchar_t* longName=L"风暴战士 VeryLongCallsign 2026";
    FitName(jp,std::wcslen(jp),to[1].name,kQMarkName);
    FitName(longName,std::wcslen(longName),to[2].name,kQMarkName);
    return 3;
}
// The squads' formation banner (npcai.cpp PlayerFormationCue): on in the npc_formation scene.
int sceneFormation=-1;
bool PlayerFormationCue(FormationCue* o) noexcept { if(sceneFormation<0)return false;o->shape=sceneFormation;o->key=0x54;return true; }
// The box sweep banner (npcai.cpp PlayerSweepCue): on in the npc_sweep scenes.
bool sceneSweepOn=false;SweepCue sceneSweep{};
bool PlayerSweepCue(SweepCue* o) noexcept { if(sceneSweepOn)*o=sceneSweep;return sceneSweepOn; }
// The sea rescue's banner (heli.cpp PlayerRescueCue): on in the rescue_banner scene.
bool sceneRescueOn=false;RescueCue sceneRescue{};
bool PlayerRescueCue(RescueCue* o) noexcept { if(sceneRescueOn)*o=sceneRescue;return sceneRescueOn; }
// The room's builds differ (support_net.cpp SupportVersionCue): on in the version_banner scene, under the rescue's line.
bool sceneVersionOn=false;wchar_t sceneVersion[200]{};
bool SupportVersionCue(wchar_t* line,std::size_t capacity) noexcept {
    if(sceneVersionOn)_snwprintf_s(line,capacity,_TRUNCATE,L"%ls",sceneVersion);
    return sceneVersionOn;
}
// The ultimate strike's caller line (ultimate.cpp PlayerUltimateCue): not drawn in any scene.
bool PlayerUltimateCue(wchar_t*,std::size_t) noexcept { return false; }
bool NpcPingReadout(NpcPing* p) noexcept { *p=NpcPing{};return false; }
// The debug spawn tool's menu (debug_spawn.cpp DebugSpawnReadout): up in the debug_spawn scenes.
bool sceneDebugSpawnOn=false;DebugSpawnCue sceneDebugSpawn{};
bool DebugSpawnReadout(DebugSpawnCue* o) noexcept { if(sceneDebugSpawnOn)*o=sceneDebugSpawn;return sceneDebugSpawnOn; }
unsigned sceneMapViewWrites=0;
void MapCommandView(const float*,float,float) noexcept { ++sceneMapViewWrites; }
// Enemy-control readers and button publication are external seams in the real HUD.
// They return immutable stand-ins here; the native renderer and owner gates below
// are still the production code included above.
MapEnemyControlReadout sceneEnemy{};bool hasEnemyPanel=false,hasEnemyControl=false;
enemy_control::Snapshot sceneEnemyHitSnapshot{};
bool sceneEnemyVisible=false;int sceneEnemyCount=0,sceneEnemyCodes[kMapEnemyItems]{};
float sceneEnemyRects[kMapEnemyItems][4]{};
namespace enemy_control { const wchar_t* ReasonText(Reason) noexcept { return L"Fixture native unavailable"; } }
bool ReadMapEnemyControl(MapEnemyControlReadout* value) noexcept {
    if(!value || !hasEnemyPanel)return false;*value=sceneEnemy;return true;
}
bool EnemyControlReadSnapshotForView(std::uintptr_t key,enemy_control::Snapshot* value) noexcept {
    if(!hasEnemyControl || !value || !key || key!=sceneEnemy.snapshot.ownerHumanKey)return false;
    *value=sceneEnemy.snapshot;return true;
}
// The controlled body's published catalogue: the readout's page stands for its first attacks.
unsigned EnemyControlAbilities(const enemy_control::Snapshot& snapshot,unsigned first,enemy_control::Ability* abilities,unsigned capacity) noexcept {
    if(!abilities || snapshot.revision!=sceneEnemy.snapshot.revision)return 0;
    unsigned n=0;
    for(unsigned i=first;i<static_cast<unsigned>(sceneEnemy.count) && n<capacity;++i)abilities[n++]=sceneEnemy.ability[i];
    return n;
}
void MapCommandEnemyButtons(const float* rects,const int* codes,int n,const enemy_control::Snapshot* snapshot) noexcept {
    sceneEnemyVisible=snapshot!=nullptr;sceneEnemyHitSnapshot=snapshot ? *snapshot : enemy_control::Snapshot{};
    sceneEnemyCount=rects && codes && snapshot ? (std::max)(0,(std::min)(n,kMapEnemyItems)) : 0;
    for(int i=0;i<sceneEnemyCount;++i){std::memcpy(sceneEnemyRects[i],rects+i*4,16);sceneEnemyCodes[i]=codes[i];}
}
// Stand-in PVP state and hit ledger let offline fixtures exercise the production panel.
PvpStatus scenePvp{},scenePvpSnapshot{};bool scenePvpVisible=false;
int scenePvpPadPick=-1,scenePvpCount=0,scenePvpCodes[kMapPvpItems]{};float scenePvpRects[kMapPvpItems][4]{};
bool ReadPvpStatus(PvpStatus* value) noexcept { if(value)*value=scenePvp;return value!=nullptr; }
void MapCommandPvpButtons(const float* rects,const int* codes,int n,const PvpStatus* snapshot) noexcept {
    scenePvpVisible=snapshot!=nullptr;scenePvpSnapshot=snapshot ? *snapshot : PvpStatus{};scenePvpCount=n;
    for(int i=0;i<n;++i){std::memcpy(scenePvpRects[i],rects+i*4,16);scenePvpCodes[i]=codes[i];}
}
int MapPvpPadSelection() noexcept { return scenePvpPadPick; }
bool scenePvpOpen=false;   // the PVP panel unfolded by a click on its title (mapcmd.cpp MapPvpOpen)
bool MapPvpOpen() noexcept { return scenePvpOpen; }
// The legend at the dock's foot (hud.cpp MapLegend): unfolded or not, its title row as drawn.
bool sceneLegendOpen=false,sceneLegendOn=false;float sceneLegendRow[4]{};
bool MapLegendOpen() noexcept { return sceneLegendOpen; }
void MapCommandLegendButton(const float* r) noexcept { sceneLegendOn=r!=nullptr;if(r)std::memcpy(sceneLegendRow,r,16); }
thermal::Status sceneThermal{};
namespace thermal { Status GetStatus(const void*) noexcept { return sceneThermal; } }
VehicleSensorReadout sceneSensor{};bool sceneSensorOn=false;
bool PlayerVehicleContacts(VehicleSensorReadout* value) noexcept { if(value)*value=sceneSensor;return value && sceneSensorOn; }
// The map's buttons as drawn (hud.cpp MapButtons): the scene's check reads them.
int sceneButtons=0;float sceneButton[mapbtn::kCount][4]{};int sceneButtonId[mapbtn::kCount]{};
void MapCommandButtons(const float* r,const int* ids,int n) noexcept {
    sceneButtons=n;
    for(int i=0;i<n;++i){for(int k=0;k<4;++k)sceneButton[i][k]=r[i*4+k];sceneButtonId[i]=ids[i];}
}
float sceneUiPanels[kMapUiPanels][4]{};int sceneUiCount=0;
float sceneSquadButtons[16][4]{};int sceneSquadButtonCount=0;
float sceneSquadFold[4]{};bool sceneSquadFoldOn=false;
float scenePayloadButtons[kMostPayload][4]{};int scenePayloadButtonCount=0;
void MapCommandUiPanels(const float* r,int n) noexcept {
    sceneUiCount=n;for(int i=0;i<n;++i)std::memcpy(sceneUiPanels[i],r+i*4,16);
}
void MapCommandSquadButtons(const float* r,const ObjRef*,int n) noexcept {
    sceneSquadButtonCount=n;for(int i=0;i<n;++i)std::memcpy(sceneSquadButtons[i],r+i*4,16);
}
void MapCommandSquadFold(const float* r) noexcept { sceneSquadFoldOn=r!=nullptr;if(r)std::memcpy(sceneSquadFold,r,16); }
float sceneMenu[kMapFormationEntries][4]{};int sceneMenuEntry[kMapFormationEntries]{},sceneMenuCount=0;
void MapCommandFormationButtons(const float* r,const int* entries,int n) noexcept {
    sceneMenuCount=n;for(int i=0;i<n;++i){std::memcpy(sceneMenu[i],r+i*4,16);sceneMenuEntry[i]=entries[i];}
}
void MapCommandPayloadButtons(const float* r,std::uint64_t,int,const int*,int n) noexcept {
    scenePayloadButtonCount=n;for(int i=0;i<n;++i)std::memcpy(scenePayloadButtons[i],r+i*4,16);
}
// The support bar's rows and chips as drawn (hud.cpp MapSupportBar).
float sceneSupportHits[kMapSupports*2][4]{};int sceneSupportEntry[kMapSupports*2]{},sceneSupportHitCount=0;
void MapCommandSupportButtons(const float* r,const int* entries,int n) noexcept {
    sceneSupportHitCount=n;for(int i=0;i<n;++i){std::memcpy(sceneSupportHits[i],r+i*4,16);sceneSupportEntry[i]=entries[i];}
}
// The support composition panel's seats and kinds as drawn (hud.cpp MapComposePanel).
float sceneComposeHits[mapbtn::kComposeItems][4]{};int sceneComposeCode[mapbtn::kComposeItems]{},sceneComposeHitCount=0;
void MapCommandComposeButtons(const float* r,const int* codes,int n) noexcept {
    sceneComposeHitCount=n;for(int i=0;i<n;++i){std::memcpy(sceneComposeHits[i],r+i*4,16);sceneComposeCode[i]=codes[i];}
}
// The test range's 「摆放」 panel's click targets as drawn (hud.cpp MapPlacePanel).
float scenePlaceHits[kMapPlaceItems][4]{};int scenePlaceCode[kMapPlaceItems]{},scenePlaceHitCount=0,scenePlaceWindow=0;
void MapCommandPlaceButtons(const float* r,const int* codes,int n,int window) noexcept {
    if(!r || !codes)n=0;
    scenePlaceHitCount=n;scenePlaceWindow=window;for(int i=0;i<n;++i){std::memcpy(scenePlaceHits[i],r+i*4,16);scenePlaceCode[i]=codes[i];}
}
bool GearHudLatest(GearHud* g) noexcept {
    if(!hasJet || sceneJet.rotor)return false;
    *g=GearHud{};g->shown=true;g->at[0]=g->at[1]=g->at[2]=1.0f;g->warn=(sceneWarn.on>>kWarnGear&1u)!=0;g->tick=sceneTick;
    return true;
}
bool IsSub(const void*) noexcept { return false; }
bool IsJet(const void*) noexcept { return false; }
bool IsHelicopter(const void*) noexcept { return false; }
bool IsGroundRobo(const void*) noexcept { return false; }
bool IsDrillTank(const void*) noexcept { return false; }
bool JetHud(const void*,JetHudInfo*) noexcept { return false; }
bool HeliFuel(const void*,float*) noexcept { return false; }
bool gaugeCovered=false;
void SetStockGaugeCover(bool covered) noexcept { gaugeCovered=covered; }
// stockgauge.cpp's armor readout: given exactly while the cover HudPublish just set hides the stock armor gauge
// (HideStockGauges, Enabled), the numbers through the production armorhud::Publish (no number when they do not read).
bool armorReads=false;
float sceneArmor=1350.0f,sceneArmorMost=1600.0f,sceneHull=51205.0f,sceneHullMost=81929.0f;
bool PlayerStockArmor(armorhud::Readout* o) noexcept {
    return gaugeCovered && config.hideStockGauges && config.enabled && armorReads &&
           armorhud::Publish(true,sceneArmor,sceneArmorMost,sceneHull,sceneHullMost,o);
}
}  // namespace crew

using namespace crew;

namespace {
float sceneFov=55.0f;   // degrees, the camera's vertical field of view (a zoomed sight's scene narrows it)
// A level camera kBack m behind and kUp over `pos`, looking along `fwd` (level): the row-vector view-projection.
void Camera(const float* pos,const float* fwdIn,float width,float height,float* vp) {
    float f[3]={fwdIn[0],0.0f,fwdIn[2]};
    vec::Normalize(f);
    const float r[3]={-f[2],0.0f,f[0]},u[3]={0.0f,1.0f,0.0f};
    const float eye[3]={pos[0]-f[0]*24.0f,pos[1]+5.0f,pos[2]-f[2]*24.0f};
    const float fy=1.0f/std::tan(0.5f*sceneFov*kDeg),fx=fy/(width/height),n=1.0f,fa=20000.0f,A=fa/(fa-n),B=-n*fa/(fa-n);
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
// The production overhead placement (hull-centred) with synthetic inputs; not a game screenshot.
void ImpactCamera(const float* origin,const float* impact,float width,float height,float* vp) {
    (void)impact;float eye[3],look[3];tcam::HullPlace(origin,0,90,55,0,0,eye,look);
    float f[3]={look[0]-eye[0],look[1]-eye[1],look[2]-eye[2]};vec::Normalize(f);
    float r[3]={-f[2],0,f[0]};vec::Normalize(r);float u[3];vec::Cross(r,f,u);
    const float fy=1/std::tan(0.5f*sceneFov*kDeg),fx=fy/(width/height),a=20000.0f/19999.0f;
    for(int i=0;i<3;++i){vp[i*4]=r[i]*fx;vp[i*4+1]=u[i]*fy;vp[i*4+2]=f[i]*a;vp[i*4+3]=f[i];}
    vp[12]=-vec::Dot(eye,r)*fx;vp[13]=-vec::Dot(eye,u)*fy;vp[14]=-vec::Dot(eye,f)*a-a;vp[15]=-vec::Dot(eye,f);
}
// What a scene's text did, per language: the pairs of its lines that overlap (by draw order), English's kept to compare.
struct SceneText { std::set<std::pair<int,int>> overlaps; std::vector<std::wstring> texts; };
std::map<std::wstring,SceneText> english;
int textFailed=0;
std::size_t mostGlyphs=0;
std::string mostGlyphsAt;
std::string Narrow(const std::wstring& w) {
    char n[512];
    WideCharToMultiByte(CP_UTF8,0,w.c_str(),-1,n,sizeof(n),nullptr,nullptr);
    return n;
}
// `draw` recorded as scene `name` on a screen `width` x `height`: its text on the screen, its overlaps none English lacks.
template<class F> void Record(const wchar_t* name,F draw,int width,int height=1080) {
    drew.clear();glyphs.clear();panels.clear();recording=true;sceneTick=GetTickCount64();
    draw();
    recording=false;
    SceneText t;
    const float w=static_cast<float>(width),h=static_cast<float>(height);
    for(std::size_t i=0;i<drew.size();++i) {
        const Drew& a=drew[i];
        t.texts.push_back(a.text);
        if(a.text.find_first_not_of(L' ')==std::wstring::npos)continue;   // nothing drawn (a line emptied: under a panel)
        // A line on a panel (its middle in it) stays inside it across (the panels are sized to their text).
        for(const PanelBox& q:panels) {
            const float mx=(a.x0+a.x1)*0.5f,my=(a.y0+a.y1)*0.5f;
            if(mx<q.x0 || mx>q.x1 || my<q.y0 || my>q.y1)continue;
            if(a.x0>=q.x0-0.5f && a.x1<=q.x1+0.5f)continue;
            std::printf("FAIL  %s %ls: out of its panel: \"%s\" (%.0f-%.0f), panel (%.0f-%.0f)\n",hudtext::Name(hudtext::InUse()),name,
                        Narrow(a.text).c_str(),a.x0,a.x1,q.x0,q.x1);
            ++textFailed;
        }
        if(a.x0<-0.5f || a.y0<-0.5f || a.x1>w+0.5f || a.y1>h+0.5f) {
            std::printf("FAIL  %s %ls: off the screen: \"%s\" (%.0f,%.0f)-(%.0f,%.0f)\n",hudtext::Name(hudtext::InUse()),name,
                        Narrow(a.text).c_str(),a.x0,a.y0,a.x1,a.y1);
            ++textFailed;
        }
        for(std::size_t k=0;k<i;++k) {
            const Drew& b=drew[k];
            if(b.text.find_first_not_of(L' ')==std::wstring::npos)continue;
            if(a.x0<b.x1 && b.x0<a.x1 && a.y0<b.y1 && b.y0<a.y1)t.overlaps.insert({static_cast<int>(k),static_cast<int>(i)});
        }
    }
    if(hudtext::InUse()==hudtext::Lang::en)english[name]=t;
    else if(english.count(name)) {
        const SceneText& e=english[name];
        if(e.texts.size()!=t.texts.size())std::printf("note  %s %ls: %zu lines (English %zu)\n",hudtext::Name(hudtext::InUse()),name,
                                                      t.texts.size(),e.texts.size());
        for(const auto& p:t.overlaps) {
            if(e.overlaps.count(p))continue;
            std::printf("FAIL  %s %ls: \"%s\" overlaps \"%s\" (not in English)\n",hudtext::Name(hudtext::InUse()),name,
                        Narrow(t.texts[static_cast<std::size_t>(p.second)]).c_str(),Narrow(t.texts[static_cast<std::size_t>(p.first)]).c_str());
            ++textFailed;
        }
    }
    std::printf("      %s %ls: %zu lines, %zu glyphs, %zu overlapping pairs\n",hudtext::Name(hudtext::InUse()),name,drew.size(),glyphs.size(),
                t.overlaps.size());
    if(glyphs.size()>mostGlyphs){mostGlyphs=glyphs.size();mostGlyphsAt=Narrow(name)+" ("+hudtext::Name(hudtext::InUse())+")";}
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
// `place`: the test range's 「摆放」 panel (mapcmd.h MapCommandReadout place*): 0 none (another mission), 1 shut (its tab),
// 2 open on EDF4.1's enemies with a row armed and rows not installed / not preloaded, 3 the same online (every row dim).
void MapScene(const std::wstring& dir,const wchar_t* name,float height,float pitchDeg,float yawDeg,bool pad,int squadCount=4,int width=1920,float uiScale=1.0f,bool payload=false,bool vehicleOnly=false,bool online=false,int place=0) {
    const float previousScale=config.hudScale;config.hudScale=uiScale;
    hasMapPayload=payload;sceneMapPayload=PayloadReadout{};
    if(payload){sceneMapPayload.count=kMostPayload;sceneMapPayload.selectionToken=111;sceneMapPayload.seat=1;
        for(int i=0;i<kMostPayload;++i){auto& e=sceneMapPayload.entry[i];e.selectable=i>0 && i!=5;e.picked=i==2;e.rounds=12-i;e.capacity=12;
            _snwprintf_s(e.name,_countof(e.name),_TRUNCATE,L"%ls %ls %ls %d",Tr(Tx::wordWeapon),Tr(Tx::wordWeapon),Tr(Tx::wordWeapon),i+1);}}
    sceneMap=MapReadout{};sceneMap.ownerHumanKey=passPlayerRefKey ? passPlayerRefKey : passPlayerSoldierKey;
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
    // The NPC commands (mapcmd.cpp): two crawlers (selected) sent to guard a point, standing in their formation slots
    // 30 m apart; the heli guarding the same point (helis share its orbit); the jet following the player; a box being
    // dragged (Ctrl + left drag) round the crawlers to the pointer; the last command's word.
    sceneCmd=MapCommandReadout{};
    sceneCmd.squadGroups[0]=0x5;sceneCmd.squadGroups[1]=0x100;   // groups 1 and 3, group 9 (the panel's [13], [9])
    sceneCmd.allowed=true;sceneCmd.allowedOrders=(1u<<(static_cast<unsigned>(mapcmd::kLastOrder)+1))-1;sceneCmd.selectedSquads=1;sceneCmd.squadToolsAllowed=!online;sceneCmd.pointOk=true;sceneCmd.pointer=!pad;
    sceneCmd.px=1250.0f;sceneCmd.py=560.0f;sceneCmd.boxing=!pad;sceneCmd.bx=820.0f;sceneCmd.by=360.0f;
    auto cmdUnit=[](const float* pos,bool air,const char* name,Order order,const float* at,bool selected){
        CmdMark& c=sceneCmd.unit[sceneCmd.count++];
        std::memcpy(c.pos,pos,12);c.air=air;c.selected=selected;c.now.order=order;
        if(at)std::memcpy(c.now.at,at,12);
        std::snprintf(c.name,sizeof(c.name),"%s",name);
        if(selected)++sceneCmd.selected;
    };
    const float heli[3]={-150.0f,80.0f,300.0f},jet[3]={400.0f,300.0f,-100.0f},guard[3]={60.0f,0.0f,420.0f};
    const float slot1[3]={60.0f,0.0f,450.0f},robo1[3]={-60.0f,0.0f,90.0f},robo2[3]={80.0f,0.0f,40.0f};
    cmdUnit(heli,true,"506",Order::guard,guard,false);
    sceneCmd.unit[sceneCmd.count-1].owner=kCmdOwnerHeli;
    cmdUnit(jet,true,"fighter",Order::follow,nullptr,false);
    sceneCmd.unit[sceneCmd.count-1].owner=kCmdOwnerJet;
    cmdUnit(robo1,false,"CRAWLER",Order::guard,guard,true);
    cmdUnit(robo2,false,"CRAWLER",Order::guard,slot1,true);
    sceneCmd.unit[sceneCmd.count-1].owner=sceneCmd.unit[sceneCmd.count-2].owner=kCmdOwnerGround;
    // The crawlers kept as group 1 (Ctrl+1), the second also in group 2, with two Shift-queued points after its guard.
    {
        CmdMark& a=sceneCmd.unit[sceneCmd.count-2];CmdMark& b=sceneCmd.unit[sceneCmd.count-1];
        a.groups=0x1;b.groups=0x3;b.ways=2;
        const float w1[3]={200.0f,0.0f,520.0f},w2[3]={320.0f,0.0f,600.0f};std::memcpy(b.way[0],w1,12);std::memcpy(b.way[1],w2,12);
    }
    // The squads (npcai.cpp, docs/npc-ai-design.md §6.1): a recruited one guarding, a free one, a script's (locked, grey),
    // one dismissed waiting out its cooldown; their panel on the right.
    const float sq1[3]={-30.0f,0.0f,150.0f},sq2[3]={200.0f,0.0f,250.0f},sq3[3]={-250.0f,0.0f,500.0f};
    cmdUnit(sq1,false,"RANGER x4",Order::guard,guard,true);
    cmdUnit(sq2,false,"FENCER x2",Order::none,nullptr,false);
    cmdUnit(sq3,false,"RANGER x6",Order::none,nullptr,false);
    for(int i=sceneCmd.count-3;i<sceneCmd.count;++i)sceneCmd.unit[i].owner=kCmdOwnerGround;
    sceneCmd.unit[sceneCmd.count-1].locked=true;
    sceneCmd.pickable=sceneCmd.count-1;   // the script's squad is shown, not picked
    auto row=[](const char* name,int alive,const char* status,Order order,bool locked,bool selected){
        SquadRow& r=sceneCmd.squad[sceneCmd.squads];
        std::snprintf(r.name,sizeof(r.name),"%s",name);std::snprintf(r.status,sizeof(r.status),"%s",status);
        r.alive=alive;r.now.order=order;r.locked=locked;r.rank=mapcmd::SquadRank(std::strcmp(status,"RECRUITED")==0,locked,false);
        sceneCmd.squadSelected[sceneCmd.squads++]=selected;
    };
    row("RANGER",4,"RECRUITED",Order::guard,false,true);
    row("FENCER",2,"FREE",Order::none,false,false);
    row("RANGER",6,"SCRIPT",Order::none,true,false);
    row("AIR RAIDER",3,"WAIT 42s",Order::guard,false,false);
    sceneCmd.squad[sceneCmd.squads-1].cooldown=42;
    // The support catalog as support_dispatch.cpp names it (its air calls in pairs, the infantry, the ground pairs).
    static const struct { const wchar_t* name; SupportIcon icon; } kCatalog[]={
        {L"截击机·守点",SupportIcon::jet},{L"截击机·跟随",SupportIcon::jet},{L"无人机母舰·守点",SupportIcon::carrier},
        {L"无人机母舰·跟随",SupportIcon::carrier},{L"武装直升机·守点",SupportIcon::heli},{L"武装直升机·跟随",SupportIcon::heli},
        {L"潜水母舰支援",SupportIcon::sub},{L"炮舰机·守点",SupportIcon::gunship},{L"炮舰机·跟随",SupportIcon::gunship},
        {L"步兵小队（4人）",SupportIcon::squad},{L"步兵大队（12人）",SupportIcon::platoon},{L"坦克·有人",SupportIcon::tank},
        {L"坦克·空车交付",SupportIcon::tank},{L"装甲运兵车·有人",SupportIcon::apc},{L"装甲运兵车·空车交付",SupportIcon::apc},
        {L"民用轻卡·有人",SupportIcon::truck},{L"民用轻卡·空车交付",SupportIcon::truck}};
    sceneCmd.supports=static_cast<int>(sizeof(kCatalog)/sizeof(kCatalog[0]));
    for(int i=0;i<sceneCmd.supports;++i){wcscpy_s(sceneCmd.support[i].name,kCatalog[i].name);sceneCmd.support[i].icon=kCatalog[i].icon;const wchar_t* v=mapbtn::VariantOf(kCatalog[i].name);sceneCmd.support[i].variant=!v ? SupportVariant::none : std::wcscmp(v,L"守点")==0 ? SupportVariant::guard : std::wcscmp(v,L"跟随")==0 ? SupportVariant::follow : std::wcscmp(v,L"有人")==0 ? SupportVariant::crewed : SupportVariant::empty;}
    sceneCmd.supportArmed=payload ? 3 : -1;sceneCmd.supportPick=3;
    // The payload scenes arm the platoon row and open its composition (map_buttons.h ComposePanel): two squads in, a
    // Wing Diver and a Fencer one, of its twelve seats.
    sceneCmd.composeEntry=-1;sceneCmd.composeSeats=0;sceneCmd.compose=SupportLoadout{};
    if(payload) {
        sceneCmd.supportArmed=sceneCmd.supportPick=sceneCmd.composeEntry=10;sceneCmd.composeSeats=12;sceneCmd.compose.count=8;
        for(int i=0;i<4;++i){sceneCmd.compose.soldier[i]=SupportWeapon::wingLance;sceneCmd.compose.soldier[4+i]=SupportWeapon::fencerCannon;}
    }
    sceneCmd.supportReady=payload ? SupportReadiness{SupportReady::cooldown,17} : SupportReadiness{SupportReady::ready,0};
    if(payload)wcscpy_s(sceneCmd.supportStatus,L"Support route ready - select a visible entry on the map");
    if(squadCount==9) {
        row("WING DIVER",12,"RECRUITED",Order::focus,false,false);
        row("AIR RAIDER",8,"ESCORT",Order::board,true,false);
        row("FENCER",10,"HOLD",Order::dismount,true,false);
        row("WING DIVER",7,"SQUAD",Order::engage,false,false);
        row("RANGER",4,"FREE",Order::recruit,false,false);
        for(int i=0;i<sceneCmd.count;++i)sceneCmd.unit[i].selected=i==sceneCmd.count-3;
        sceneCmd.selected=1;
        sceneCmd.squadOpen=true;   // opened: every row, a script's too, and the row that folds it
        // The formation menu open over its button: the guarding squad's defences (perimeter in use) and the march.
        sceneCmd.formationMenu=sceneCmd.formationGuard=sceneCmd.formationMarch=true;
        sceneCmd.formationGuardShape=static_cast<int>(npc::formation::Shape::perimeter);
        // An armed support's composition and the formation menu are never open together (mapcmd.cpp: one at a time).
        if(payload)sceneCmd.formationMenu=false;
    }
    std::swprintf(sceneCmd.note,_countof(sceneCmd.note),Tr(Tx::cmdGuardResult),Tr(Tx::orderGuard),60.0,420.0,2,L"");
    if(vehicleOnly) {
        for(int i=0;i<sceneCmd.count;++i)sceneCmd.unit[i].selected=i==2;
        for(bool& selected:sceneCmd.squadSelected)selected=false;
        sceneCmd.selected=1;sceneCmd.selectedSquads=0;
        sceneCmd.allowedOrders=(1u<<static_cast<unsigned>(Order::guard))|(1u<<static_cast<unsigned>(Order::none))|(1u<<static_cast<unsigned>(Order::follow))|
                               (1u<<static_cast<unsigned>(Order::move))|(1u<<static_cast<unsigned>(Order::attackMove));
    }
    // The rows as npcai.cpp SquadRows gives them: sorted by rank (stable), a script's last; the tally of them all.
    for(int i=1;i<sceneCmd.squads;++i)for(int j=i;j>0 && sceneCmd.squad[j].rank<sceneCmd.squad[j-1].rank;--j) {
        std::swap(sceneCmd.squad[j],sceneCmd.squad[j-1]);std::swap(sceneCmd.squadSelected[j],sceneCmd.squadSelected[j-1]);
    }
    sceneCmd.squadTally=SquadTally{};sceneCmd.squadTally.total=sceneCmd.squads;
    for(int i=0;i<sceneCmd.squads;++i)sceneCmd.squadTally.scripted+=sceneCmd.squad[i].rank==3;
    sceneCmd.noteFresh=true;
    sceneCmd.placeOn=place>0;sceneCmd.placeOpen=place>=2;sceneCmd.placeOnline=place==3;
    sceneCmd.placeCategory=static_cast<int>(debugspawn::Category::enemy41);sceneCmd.placePick=14;
    sceneCmd.placeArmed=place>=2 && !pad ? debugspawn::RowOf(debugspawn::Category::enemy41,14) : -1;
    for(int i=0;i<debugspawn::kEntryCount;++i)
        sceneCmd.placeState[i]=place==3 ? debugspawn::RowState::online : !debugspawn::kEntries[i].preload ? debugspawn::RowState::notPreloaded :
                               i%7==3 ? debugspawn::RowState::missing : debugspawn::RowState::ready;
    hasMap=true;
    const std::wstring path=dir+L"\\"+name+L".txt";
    if(_wfopen_s(&out,path.c_str(),L"w") || !out){hasMap=false;return;}
    std::fprintf(out,"W %d 1080\n",width);
    SetScreen(width,1080);
    float vp[16];
    MapCamera(sceneMap,static_cast<float>(width),1080.0f,vp);
    struct { std::int32_t x,y,w,h; } viewport{0,0,width,1080};
    Record(name,[&]{HudDraw(vp,image,&viewport,nullptr,0);},width);
    auto shown=[&](const wchar_t* wanted,bool title=false){
        int found=0;
        for(std::size_t i=0;i<drew.size();++i)if(drew[i].text==wanted && (!title || drew[i].y0<100)) {
            ++found;const Drew& a=drew[i];
            for(std::size_t j=0;j<drew.size();++j)if(i!=j && !drew[j].text.empty()) {
                const Drew& b=drew[j];
                if(a.x0<b.x1 && b.x0<a.x1 && a.y0<b.y1 && b.y0<a.y1) {
                    ++textFailed;std::printf("FAIL squad overlap %s: %s / %s\n",hudtext::Name(hudtext::InUse()),Narrow(a.text).c_str(),Narrow(b.text).c_str());
                }
            }
        }
        if(found!=1){++textFailed;std::printf("FAIL squad text missing or repeated: %s\n",Narrow(wanted).c_str());}
    };
    shown(Tr(pad ? Tx::squadTitle : Tx::squadTitleKeys),true);
    // The command buttons (mouse only): the ones the selection takes placed, on the screen, and no text but its own
    // label in it.
    if(pad) {
        if(sceneButtons){++textFailed;std::printf("FAIL map buttons with a pad: %d\n",sceneButtons);}
        if(sceneSupportHitCount){++textFailed;std::printf("FAIL support bar clickable with a pad: %d\n",sceneSupportHitCount);}
    } else {
        int wanted=0;
        for(int i=0;i<mapbtn::kCount;++i)
            wanted+=mapbtn::Shown(static_cast<mapbtn::Id>(i),sceneCmd.selected>0 ? sceneCmd.allowedOrders : 0u,
                                   sceneCmd.squadToolsAllowed && sceneCmd.selectedSquads>0);
        if(sceneButtons!=wanted){++textFailed;std::printf("FAIL map buttons: %d of %d placed\n",sceneButtons,wanted);}
        // The support bar: a row (and its chips) for every kind of support, every entry reachable by a click.
        bool reach[kMapSupports]{};
        for(int i=0;i<sceneSupportHitCount;++i) {
            const float* b=sceneSupportHits[i];
            if(b[0]<0.0f || b[2]>static_cast<float>(width) || b[1]<0.0f || b[3]>1080.0f){++textFailed;std::printf("FAIL support hit %d off the screen\n",i);}
            if(sceneSupportEntry[i]>=0 && sceneSupportEntry[i]<kMapSupports)reach[sceneSupportEntry[i]]=true;
            for(int k=0;k<sceneButtons;++k) {
                const float* q=sceneButton[k];
                if(b[0]<q[2] && q[0]<b[2] && b[1]<q[3] && q[1]<b[3]){++textFailed;std::printf("FAIL support bar under button %d\n",sceneButtonId[k]);}
            }
        }
        // The composition panel: a place for each squad in it and every kind clickable, on the screen, clear of the
        // command card and of the bar.
        if(sceneCmd.composeEntry>=0) {
            if(sceneComposeHitCount!=2+kSupportWeaponCount){++textFailed;std::printf("FAIL composition clicks: %d\n",sceneComposeHitCount);}
            for(int i=0;i<sceneComposeHitCount;++i) {
                const float* b=sceneComposeHits[i];
                if(b[0]<0.0f || b[2]>static_cast<float>(width) || b[1]<0.0f || b[3]>1080.0f){++textFailed;std::printf("FAIL composition hit %d off the screen\n",i);}
                for(int k=0;k<sceneButtons;++k) {
                    const float* q=sceneButton[k];
                    if(b[0]<q[2] && q[0]<b[2] && b[1]<q[3] && q[1]<b[3]){++textFailed;std::printf("FAIL composition under button %d\n",sceneButtonId[k]);}
                }
                for(int k=0;k<sceneSupportHitCount;++k) {
                    const float* q=sceneSupportHits[k];
                    if(b[0]<q[2] && q[0]<b[2] && b[1]<q[3] && q[1]<b[3]){++textFailed;std::printf("FAIL composition over the support bar %d\n",k);}
                }
            }
        } else if(sceneComposeHitCount){++textFailed;std::printf("FAIL composition clickable with none armed: %d\n",sceneComposeHitCount);}
        int reached=0;for(int i=0;i<sceneCmd.supports;++i)reached+=reach[i];
        if(reached!=sceneCmd.supports){++textFailed;std::printf("FAIL support entries clickable: %d of %d\n",reached,sceneCmd.supports);}
        for(int i=0;i<sceneButtons;++i) {
            const float* b=sceneButton[i];
            if(b[0]<0.0f || b[2]>static_cast<float>(width) || b[1]<0.0f || b[3]>1080.0f){++textFailed;std::printf("FAIL map button %d off the screen\n",i);}
            int own=0;
            for(const Drew& d:drew) {
                if(d.text.empty() || !(d.x0<b[2] && b[0]<d.x1 && d.y0<b[3] && b[1]<d.y1))continue;
                const float cx=(d.x0+d.x1)*0.5f,cy=(d.y0+d.y1)*0.5f;
                const bool inside=cx>b[0] && cx<b[2] && cy>b[1] && cy<b[3] && d.x0>=b[0]-1.0f && d.x1<=b[2]+1.0f;
                if(inside)++own;
                else{++textFailed;std::printf("FAIL map button %d covers %s\n",sceneButtonId[i],Narrow(d.text).c_str());}
            }
            if(own!=1){++textFailed;std::printf("FAIL map button %d: %d labels in it\n",sceneButtonId[i],own);}
        }
    }
    // The 「摆放」 panel: clickable with the mouse alone (its tab shut; open, its title, six tabs, the rows of the window
    // and an arrow or two), on the screen, clear of the command card, the support bar and the payload panel's rows.
    if(place>0) {
        // Open: at least three rows fit (a shorter screen, the payload panel above it: fewer rows), the window at most.
        const int least=pad ? 0 : place==1 ? 1 : 1+debugspawn::kCategoryCount+3,want=pad ? 0 : place==1 ? 1 : 1+debugspawn::kCategoryCount+rangeplace::kWindow+2;
        if(pad ? scenePlaceHitCount!=0 : scenePlaceHitCount<least || scenePlaceHitCount>want)
            {++textFailed;std::printf("FAIL place panel clicks: %d (want about %d)\n",scenePlaceHitCount,want);}
        for(int i=0;i<scenePlaceHitCount;++i) {
            const float* b=scenePlaceHits[i];
            if(b[0]<0.0f || b[2]>static_cast<float>(width) || b[1]<0.0f || b[3]>1080.0f){++textFailed;std::printf("FAIL place hit %d off the screen\n",i);}
            auto under=[&](const float* q,const char* what,int k){
                if(b[0]<q[2] && q[0]<b[2] && b[1]<q[3] && q[1]<b[3]){++textFailed;std::printf("FAIL place panel over %s %d\n",what,k);}
            };
            for(int k=0;k<sceneButtons;++k)under(sceneButton[k],"button",k);
            for(int k=0;k<sceneSupportHitCount;++k)under(sceneSupportHits[k],"the support bar",k);
            for(int k=0;k<scenePayloadButtonCount;++k)under(scenePayloadButtons[k],"the payload panel",k);
        }
    } else if(scenePlaceHitCount){++textFailed;std::printf("FAIL place panel in a mission without it: %d\n",scenePlaceHitCount);}
    // The legend (the user, 2026-10-19: 「操作区域和图例冲突了」): its title row at the dock's foot, clickable with the
    // mouse, clear of the command card, the support bar, the payload panel, PVP and every 「摆放」 target; open, none of
    // its words under any of those targets either.
    if(pad ? sceneLegendOn : !sceneLegendOn){++textFailed;std::printf("FAIL legend title clickable %d with pad %d\n",sceneLegendOn,pad);}
    {
        std::vector<std::array<float,4>> targets;
        for(int k=0;k<sceneButtons;++k)targets.push_back({sceneButton[k][0],sceneButton[k][1],sceneButton[k][2],sceneButton[k][3]});
        for(int k=0;k<sceneSupportHitCount;++k)targets.push_back({sceneSupportHits[k][0],sceneSupportHits[k][1],sceneSupportHits[k][2],sceneSupportHits[k][3]});
        for(int k=0;k<scenePayloadButtonCount;++k)targets.push_back({scenePayloadButtons[k][0],scenePayloadButtons[k][1],scenePayloadButtons[k][2],scenePayloadButtons[k][3]});
        for(int k=0;k<scenePlaceHitCount;++k)targets.push_back({scenePlaceHits[k][0],scenePlaceHits[k][1],scenePlaceHits[k][2],scenePlaceHits[k][3]});
        for(int k=0;k<scenePvpCount;++k)targets.push_back({scenePvpRects[k][0],scenePvpRects[k][1],scenePvpRects[k][2],scenePvpRects[k][3]});
        for(int k=0;k<sceneComposeHitCount;++k)targets.push_back({sceneComposeHits[k][0],sceneComposeHits[k][1],sceneComposeHits[k][2],sceneComposeHits[k][3]});
        for(int k=0;k<sceneSquadButtonCount;++k)targets.push_back({sceneSquadButtons[k][0],sceneSquadButtons[k][1],sceneSquadButtons[k][2],sceneSquadButtons[k][3]});
        auto hits=[](const float* a,const std::array<float,4>& q){ return a[0]<q[2] && q[0]<a[2] && a[1]<q[3] && q[1]<a[3]; };
        if(sceneLegendOn) {
            const float* b=sceneLegendRow;
            if(b[0]<0.0f || b[2]>static_cast<float>(width) || b[1]<0.0f || b[3]>1080.0f){++textFailed;std::puts("FAIL legend off the screen");}
            for(const auto& q:targets)if(hits(b,q)){++textFailed;std::puts("FAIL legend title under another target");break;}
        }
        static const Tx kLegendWords[]={Tx::legendYou,Tx::legendSquad,Tx::legendFriendly,Tx::legendVehicle,Tx::legendAircraft,
            Tx::legendHelicopter,Tx::legendEmpty,Tx::legendCarrier,Tx::legendLargeEnemy,Tx::legendLargeEnemyAir,Tx::legendObjective,
            Tx::legendEnemy,Tx::legendEnemyFlying,Tx::legendLock,Tx::legendNearestEnemy};
        int words=0;
        for(const Drew& d:drew)for(const Tx w:kLegendWords)if(d.text==Tr(w)) {
            ++words;
            const float a[4]={d.x0,d.y0,d.x1,d.y1};
            for(const auto& q:targets)if(hits(a,q)){++textFailed;std::printf("FAIL legend word %s under a target\n",Narrow(d.text).c_str());break;}
        }
        if(sceneLegendOpen && !pad && words!=static_cast<int>(sizeof(kLegendWords)/sizeof(kLegendWords[0])))
            {++textFailed;std::printf("FAIL open legend: %d words drawn\n",words);}
    }
    // The squad panel (the user, 2026-10-09: "小队太多了…看不过来"): folded, the rows on foot that take orders, the rest
    // (a script's) in the summary row that opens it; open, every row and the row that folds it.
    int ranks[16];
    for(int i=0;i<sceneCmd.squads;++i)ranks[i]=sceneCmd.squad[i].rank;
    const int listedRows=mapcmd::SquadRowsShown(ranks,sceneCmd.squads,sceneCmd.squadOpen,16);
    const int foldedRows=mapcmd::SquadRowsShown(ranks,sceneCmd.squads,false,16);
    const bool summary=sceneCmd.squadOpen ? foldedRows<sceneCmd.squads : listedRows<sceneCmd.squads;
    for(int i=0;i<sceneCmd.squads;++i) {
        const SquadRow& r=sceneCmd.squad[i];wchar_t kind[32],status[32];
        hudtext::WordTo(r.name,kind,_countof(kind));MapSquadStatus(r,status,_countof(status));
        wchar_t groups[16];MapGroupDigits(sceneCmd.squadGroups[i],groups,_countof(groups));   // its RTS groups ([13]), none: spaces
        Line expected{};Format(expected,L"%ls  %ls x%d   %ls   %ls",groups,kind,r.alive,status,MapOrderWord(r.now.order));
        if(i<listedRows)shown(expected.text);
        else for(const Drew& d:drew)if(d.text==expected.text){++textFailed;std::printf("FAIL folded squad row shown: %s\n",Narrow(d.text).c_str());}
    }
    if(summary) {
        Line more{};
        if(sceneCmd.squadOpen)Format(more,L"%ls",Tr(Tx::squadFold));
        else Format(more,Tr(Tx::squadMore),sceneCmd.squads-listedRows,sceneCmd.squadTally.riding,sceneCmd.squadTally.scripted);
        shown(more.text);
    }
    if(sceneSquadFoldOn!=(summary && !pad)){++textFailed;std::puts("FAIL map squad summary row clickable");}
    if(online && !pad) {
        bool board=false;
        for(int i=0;i<sceneButtons;++i) {
            board=board || sceneButtonId[i]==static_cast<int>(mapbtn::Id::board);
            if(sceneButtonId[i]==static_cast<int>(mapbtn::Id::formation)) {
                ++textFailed;std::puts("FAIL unsupported online squad editor is clickable");
            }
        }
        if(!board){++textFailed;std::puts("FAIL online basic boarding command was hidden");}
    }
    if(vehicleOnly) {
        for(int i=0;i<sceneButtons;++i)if(sceneButtonId[i]==static_cast<int>(mapbtn::Id::board) || sceneButtonId[i]==static_cast<int>(mapbtn::Id::engage)) {
            ++textFailed;std::puts("FAIL unsupported vehicle order is clickable");
        }
    }
    if(sceneSquadButtonCount!=(pad ? 0 : listedRows)) {++textFailed;std::puts("FAIL map squad clickable rows");}
    {   // The formation menu: a row a shape, on the screen, clear of the buttons, its own word in each row.
        bool formationShown=false;
        for(int i=0;i<sceneButtons;++i)formationShown=formationShown || sceneButtonId[i]==static_cast<int>(mapbtn::Id::formation);
        const int wanted=sceneCmd.formationMenu && formationShown ?
            (sceneCmd.formationGuard ? static_cast<int>(std::size(npc::formation::kGuard)) : 0)+
            (sceneCmd.formationMarch ? static_cast<int>(std::size(npc::formation::kMarch)) : 0) : 0;
        if(sceneMenuCount!=wanted){++textFailed;std::printf("FAIL formation menu rows: %d of %d\n",sceneMenuCount,wanted);}
        for(int i=0;i<sceneMenuCount;++i) {
            const float* m=sceneMenu[i];
            if(m[0]<0.0f || m[2]>static_cast<float>(width) || m[1]<0.0f || m[3]>1080.0f){++textFailed;std::printf("FAIL formation menu row %d off the screen\n",i);}
            for(int k=0;k<sceneButtons;++k) {
                const float* q=sceneButton[k];
                if(m[0]<q[2] && q[0]<m[2] && m[1]<q[3] && q[1]<m[3]){++textFailed;std::printf("FAIL formation menu row %d over button %d\n",i,sceneButtonId[k]);}
            }
            int own=0;
            for(const Drew& d:drew) {
                if(d.text.empty() || !(d.x0<m[2] && m[0]<d.x1 && d.y0<m[3] && m[1]<d.y1))continue;
                const float cy=(d.y0+d.y1)*0.5f;
                if(cy>m[1] && cy<m[3] && d.x0>=m[0]-1.0f && d.x1<=m[2]+1.0f)++own;
                else{++textFailed;std::printf("FAIL formation menu row %d covers %s\n",i,Narrow(d.text).c_str());}
            }
            if(own!=1){++textFailed;std::printf("FAIL formation menu row %d: %d labels in it\n",i,own);}
        }
    }
    if(scenePayloadButtonCount!=(payload && !pad ? 6 : 0)){++textFailed;std::puts("FAIL map payload clickable rows");}
    for(std::size_t i=0;i<drew.size();++i) {
        const auto& a=drew[i];if(a.text.empty())continue;bool panelText=false;
        for(int q=0;q<sceneUiCount;++q){const auto* r=sceneUiPanels[q];
            panelText=panelText || (a.x0<r[2] && a.x1>r[0] && a.y0<r[3] && a.y1>r[1]);}
        if(!panelText)continue;
        for(std::size_t j=0;j<i;++j){const auto& b=drew[j];if(b.text.empty())continue;
            if(a.x0<b.x1 && b.x0<a.x1 && a.y0<b.y1 && b.y0<a.y1){++textFailed;
                std::printf("FAIL map UI overlay: %s / %s\n",Narrow(a.text).c_str(),Narrow(b.text).c_str());}}
    }
    std::fclose(out);out=nullptr;
    hasMap=false;hasMapPayload=false;config.hudScale=previousScale;
    std::printf("%ls\n",path.c_str());
}
// The damage statistics' page (hud_stats.inc) on its own scenes: a mission's book as the hook would fill it (my weapons
// and my vehicle's, a squad's, a support heli, another player; friendly fire, heals on friends and on an enemy, the
// enemies' attacks, twelve minutes of it), each tab with the pointer on a bar (its tooltip), a bar's breakdown open, the
// timeline's column picked, at 16:9, 21:9, 4:3 and a narrow split. Each scene: the page's click targets handed over, its
// tooltip drawn whole, and no two of its lines overlapping (in English; the other languages: none English lacks, Record).
void StatsBook() {
    using dmgstat::Group;using dmgstat::Side;
    dmgstat::Clear(sceneBook);
    const wchar_t* me=Tr(Tx::statsGroupMe);
    const wchar_t* allies=Tr(Tx::statsGroupAlly);
    const wchar_t* others=Tr(Tx::statsGroupTeammate);
    struct Shot { Side side; const wchar_t* weapon; Group group; const wchar_t* target; float amount; bool heal; int every; };
    const Shot shots[]={
        {Side::me,L"AF-99ST",Group::enemy,L"E501_ANT_BLACK",180.0f,false,1},{Side::me,L"AF-99ST",Group::enemy,L"E501_ANT_RED",160.0f,false,2},
        {Side::me,L"Lysander Z",Group::enemy,L"E520_SPIDER_GIANT",4200.0f,false,9},{Side::me,L"Goliath D2",Group::enemy,L"E501_ANT_RED",2600.0f,false,7},
        {Side::me,L"Goliath D2",Group::ally,allies,900.0f,false,23},{Side::me,L"Goliath D2",Group::me,me,300.0f,false,31},
        {Side::myVehicle,L"Blacker A1 120mm Cannon With A Very Long Name",Group::enemy,L"E601_FROG_SOLDIER",3100.0f,false,5},
        {Side::squad,L"PA-11",Group::enemy,L"E501_ANT_BLACK",95.0f,false,1},{Side::squad,L"PA-11",Group::teammate,others,60.0f,false,17},
        {Side::support,L"EDF6VC_HELI_GUN",Group::enemy,L"E520_SPIDER_GIANT",520.0f,false,3},
        {Side::teammate,L"Mirage 15WR",Group::enemy,L"E520_SPIDER_GIANT",330.0f,false,2},
        {Side::me,L"Life Vendor Gun",Group::me,me,400.0f,true,6},{Side::me,L"Life Vendor Gun",Group::ally,allies,600.0f,true,8},
        {Side::me,L"Life Vendor Gun",Group::enemy,L"E501_ANT_RED",250.0f,true,13},{Side::enemy,L"E540_QUEEN",Group::enemy,L"E501_ANT_RED",800.0f,true,19},
        {Side::enemy,L"E501_ANT_RED",Group::me,me,45.0f,false,3},{Side::enemy,L"E501_ANT_RED",Group::ally,allies,40.0f,false,2},
        {Side::enemy,L"E520_SPIDER_GIANT",Group::myVehicle,Tr(Tx::statsGroupMyVehicle),210.0f,false,5},
        {Side::enemy,L"E601_FROG_SOLDIER",Group::vehicle,L"APP_VEHICLE_BLACKER",380.0f,false,11},
    };
    for(int tick=0;tick<720;++tick)   // a hit a second for twelve minutes, each kind at its own rate
        for(const Shot& s:shots) {
            if(tick%s.every)continue;
            const float wave=1.0f+0.6f*std::sin(static_cast<float>(tick)*0.02f);
            dmgstat::Add(sceneBook,dmgstat::Hit{s.side,s.weapon,s.group,s.target,s.amount*wave,s.heal,!s.heal && tick%(s.every*4)==0,
                                                static_cast<std::uint32_t>(tick)*1000u},Tr(Tx::statsOther));
        }
    sceneMissionMs=720000u;
    // A new book each scene (and language): never the serial the draw's last copy holds (damagestats.cpp Reset's rule).
    static std::uint32_t generation=0;
    sceneBook.serial+=(++generation)<<20;
}
// `pick`: the row whose breakdown is open (a timeline column; -1 none), or `pickName`'s row (a source; a target on the
// enemies tab) when given.
int StatsScene(const std::wstring& dir,const wchar_t* name,dmgstat::Tab tab,dmgstat::Scope scope,int pick,float px,float py,int width=1920,
               const wchar_t* pickName=nullptr) {
    StatsBook();
    if(pickName && tab==dmgstat::Tab::enemies){for(int i=0;i<sceneBook.targets;++i)if(dmgstat::SameName(sceneBook.target[i].name,pickName))pick=i;}
    else if(pickName){for(int i=0;i<sceneBook.sources;++i)if(dmgstat::SameName(sceneBook.source[i].name,pickName))pick=i;}
    sceneStatsView=dmgstat::View{};
    sceneStatsView.open=true;sceneStatsView.tab=tab;sceneStatsView.scope=scope;sceneStatsView.pick=pick;
    sceneMap=MapReadout{};sceneMap.ownerHumanKey=passPlayerRefKey ? passPlayerRefKey : passPlayerSoldierKey;
    sceneMap.yaw=0.0f;sceneMap.pitch=60.0f*kDeg;sceneMap.height=700.0f;sceneMap.mapKey=0x4D;sceneMap.mapButton=0x20;
    sceneCmd=MapCommandReadout{};
    sceneCmd.pointer=px>=0.0f;sceneCmd.px=px;sceneCmd.py=py;
    hasMap=true;sceneStatsTargets=0;
    const std::wstring path=dir+L"\\"+name+L".txt";
    if(_wfopen_s(&out,path.c_str(),L"w") || !out){hasMap=false;return 1;}
    std::fprintf(out,"W %d 1080\n",width);
    SetScreen(width,1080);
    float vp[16];
    MapCamera(sceneMap,static_cast<float>(width),1080.0f,vp);
    struct { std::int32_t x,y,w,h; } viewport{0,0,width,1080};
    Record(name,[&]{HudDraw(vp,image,&viewport,nullptr,0);},width);
    int failed=0;
    if(sceneStatsTargets<6){++failed;std::printf("FAIL  %ls: %d click targets handed over\n",name,sceneStatsTargets);}
    if(hudtext::InUse()==hudtext::Lang::en)
        for(const auto& p:english[name].overlaps) {
            ++failed;
            std::printf("FAIL  %ls: \"%s\" overlaps \"%s\"\n",name,Narrow(english[name].texts[static_cast<std::size_t>(p.second)]).c_str(),
                        Narrow(english[name].texts[static_cast<std::size_t>(p.first)]).c_str());
        }
    std::fclose(out);out=nullptr;
    hasMap=false;sceneStatsView=dmgstat::View{};
    std::printf("%ls\n",path.c_str());
    textFailed+=failed;
    return failed;
}
void StatsScenes(const std::wstring& dir) {
    using dmgstat::Scope;using dmgstat::Tab;
    // The rows start 160 px down the page (FrameOf at 1080 lines): the pointer on the second row, on a detail row.
    StatsScene(dir,L"stats_weapons_hover",Tab::weapons,Scope::mine,-1,700.0f,250.0f);
    StatsScene(dir,L"stats_weapons_all_detail",Tab::weapons,Scope::everyone,-1,1500.0f,400.0f,1920,L"Goliath D2");
    StatsScene(dir,L"stats_enemies_detail",Tab::enemies,Scope::everyone,0,300.0f,220.0f);
    StatsScene(dir,L"stats_taken_hover",Tab::taken,Scope::mine,-1,900.0f,220.0f);
    StatsScene(dir,L"stats_heals_detail",Tab::heals,Scope::everyone,-1,500.0f,190.0f,1920,L"Life Vendor Gun");
    StatsScene(dir,L"stats_timeline",Tab::timeline,Scope::everyone,40,600.0f,700.0f);
    StatsScene(dir,L"stats_weapons_21x9",Tab::weapons,Scope::everyone,0,1200.0f,300.0f,2560);
    StatsScene(dir,L"stats_weapons_4x3",Tab::weapons,Scope::everyone,0,1100.0f,300.0f,1440);
    StatsScene(dir,L"stats_timeline_narrow",Tab::timeline,Scope::mine,10,400.0f,600.0f,960);
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
    SetScreen(width,1080);
    float vp[16];
    const float* fwd=hasStock ? sceneStock.hull : hasJet ? sceneJet.sym.nose : sceneHeli.sym.nose;
    Camera(pos,fwd,static_cast<float>(width),1080.0f,vp);
    if(hasHighView && hasLauncher)ImpactCamera(pos,sceneLauncher.impact,static_cast<float>(width),1080,vp);
    HudPublish();
    struct { std::int32_t x,y,w,h; } viewport{0,0,width,1080};
    Record(name,[&]{HudDraw(vp,image,&viewport,nullptr,0);},width);
    std::fclose(out);out=nullptr;
    std::printf("%ls\n",path.c_str());
}

// A frame drawn and not recorded: what the HUD sees before a scene (a switch's banner needs the value before it).
void Prime(const float* pos,int width=1920) {
    sceneTick=GetTickCount64();
    float vp[16];
    const float* fwd=hasStock ? sceneStock.hull : hasJet ? sceneJet.sym.nose : sceneHeli.sym.nose;
    Camera(pos,fwd,static_cast<float>(width),1080.0f,vp);
    HudPublish();
    struct { std::int32_t x,y,w,h; } viewport{0,0,width,1080};
    HudDraw(vp,image,&viewport,nullptr,0);
}

// Check the actual draw, including the control row's separation from the loadout and flight hints.
int AircraftControlsDrawn(const Line& expected) {
    int found=0,failed=0;
    for(std::size_t i=0;i<drew.size();++i)if(drew[i].text==expected.text) {
        ++found;const Drew& a=drew[i];
        for(std::size_t k=0;k<drew.size();++k)if(k!=i && !drew[k].text.empty()) {
            const Drew& b=drew[k];
            if(a.x0<b.x1 && b.x0<a.x1 && a.y0<b.y1 && b.y0<a.y1) {
                ++failed;std::printf("FAIL controls overlap: %s at %.1f..%.1f, other %s at %.1f..%.1f\n",
                    Narrow(a.text).c_str(),a.y0,a.y1,Narrow(b.text).c_str(),b.y0,b.y1);
            }
        }
    }
    if(found!=1)++failed;
    std::printf("%s  aircraft controls %s: one complete row, clear of other text\n",failed ? "FAIL" : "ok",hudtext::Name(hudtext::InUse()));
    return failed;
}

int AircraftBindingChecks() {
    int failed=0;
    auto check=[&](bool ok,const char* what){if(!ok){++failed;std::printf("FAIL aircraft binding: %s\n",what);}};
    Line line{};wchar_t key[32],part[80];
    const int swap=config.playerJetSwitchKey,target=config.playerJetTargetKey,flare=config.playerJetFlareKey;
    config.playerJetSwitchKey=VK_F8;config.playerJetTargetKey=VK_XBUTTON1;config.playerJetFlareKey=VK_HOME;
    AircraftControls(line,true,3,0x10,0x04,true,true);
    const int bindings[]={config.playerJetSwitchKey,config.playerJetTargetKey,config.playerJetFlareKey};
    const Tx labels[]={Tx::controlStores,Tx::controlTarget,Tx::controlFlares};
    for(int i=0;i<3;++i){KeyName(bindings[i],key,32);std::swprintf(part,80,Tr(labels[i]),key);check(std::wcsstr(line.text,part)!=nullptr,"remapped ini key is shown");}
    AircraftControls(line,false,3,0x20,0x08,true,true);
    check(std::wcsstr(line.text,L"[RB]") && std::wcsstr(line.text,L"[Y]"),"pad hints follow supplied seat masks");
    KeyName(config.playerJetFlareKey,key,32);std::swprintf(part,80,Tr(Tx::controlFlaresKeyboard),key);
    check(std::wcsstr(line.text,part)!=nullptr,"pad explicitly shows keyboard-only flare binding");
    AircraftControls(line,false,1,0,0,false,false);check(!line.text[0],"unavailable actions are absent");
    KeyName(0,key,32);check(!std::wcscmp(key,Tr(Tx::controlUnbound)),"disabled key is unbound");
    wchar_t home[32];KeyName(VK_HOME,home,32);KeyName(VK_NUMPAD7,key,32);
    check(std::wcscmp(home,key)!=0,"Home is not mislabeled as keypad 7");
    config.playerJetSwitchKey=swap;config.playerJetTargetKey=target;config.playerJetFlareKey=flare;
    return failed;
}

// What `draw` puts on the screen (its quads and its text lines drawn), as a box.
template<class F> Box Measured(F draw,float s=1.0f) {
    alignas(16) unsigned char renderer[kRendererSize]{},font[kFontSize]{};
    static unsigned char ctx[16]{};
    Text text{};
    text.ctx=ctx;text.renderer=renderer;text.font=font;text.mgr=At<unsigned char*>(image,kFontMgr);text.s=s;
    Line lines[kMaxLines];
    int at=0;
    box=Box{};boxing=true;
    draw(&text,lines,&at);
    DrawAll(text,lines,at);
    FreeText(text);
    boxing=false;
    return box;
}

// The map's icons inside their rows (the user, 2026-10-09, a 4K screenshot of the support bar: "左边的重叠了" -- the
// aircraft icons drawn at the HUD scale squared, each over the rows round it): every support icon within its bar row
// (half a row up and down, and clear of the name that starts kSupIcon + 8 px in) and every order icon within its
// button, at the HUD scales of 1080 .. 3240 lines.
int MapIconFitChecks() {
    void* const drawer=At<void*>(image,kQuadDrawer);
    static unsigned char ctx[16]{};
    int failed=0;
    for(const float s:{1.0f,1440.0f/1080.0f,2.0f,3.0f}) {
        const float x=500.0f*s,y=500.0f*s;
        for(int i=0;i<=static_cast<int>(SupportIcon::truck);++i) {
            const Box b=Measured([&](Text*,Line*,int*){MapSupportIcon(drawer,ctx,static_cast<SupportIcon>(i),x,y,kSupIcon*s,s,kWhite);},s);
            const float across=(kSupIcon*0.5f+4.0f)*s,up=kSupRowH*0.5f*s;
            const bool fits=b.any && b.x0>=x-across && b.x1<=x+across && b.y0>=y-up && b.y1<=y+up;
            if(!fits){++failed;std::printf("FAIL support icon %d at scale %.2f: (%.1f,%.1f)-(%.1f,%.1f) round (%.0f,%.0f), +-%.1f x +-%.1f\n",
                                           i,s,b.x0,b.y0,b.x1,b.y1,x,y,across,up);}
        }
        for(int i=0;i<mapbtn::kCount;++i) {
            const Box b=Measured([&](Text*,Line*,int*){MapOrderIcon(drawer,ctx,static_cast<mapbtn::Id>(i),x,y,kBtnIcon*s,s,kWhite);},s);
            const float half=kBtnRowH*0.5f*s;
            if(!b.any || b.x0<x-half || b.x1>x+half || b.y0<y-half || b.y1>y+half){
                ++failed;std::printf("FAIL order icon %d at scale %.2f outside its button\n",i,s);
            }
        }
    }
    std::printf(failed ? "map icons: %d FAILED\n" : "map icons: inside their rows at every scale\n",failed);
    return failed;
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

// The pause menu (docs/hud-re.md §10): the scene now up draws, and the same scene draws nothing while the game is paused.
bool PausedDrawsNothing(const float* pos,const float* fwd) {
    float vp[16];
    Camera(pos,fwd,1920.0f,1080.0f,vp);
    struct { std::int32_t x,y,w,h; } viewport{0,0,1920,1080};
    Box shown[2]{};
    for(int p=0;p<2;++p) {
        paused=p==1;
        HudPublish();
        box=Box{};boxing=true;
        HudDraw(vp,image,&viewport,nullptr,0);
        boxing=false;shown[p]=box;
    }
    paused=false;
    const bool ok=shown[0].any && !shown[1].any;
    std::printf("%s  paused: drawn (%.0f,%.0f)-(%.0f,%.0f) running, %s paused\n",ok ? "ok  " : "FAIL",shown[0].x0,shown[0].y0,shown[0].x1,
                shown[0].y1,shown[1].any ? "something" : "nothing");
    return ok;
}

// The map open over a stock vehicle (docs/hud-re.md §11): the map's layer alone, the very quads and lines the map draws
// with no vehicle; easing back (the map's view, no readout): nothing; the camera back: the vehicle HUD again.
bool MapDrawsMapAlone(const float* pos,const float* fwd) {
    struct { std::int32_t x,y,w,h; } viewport{0,0,1920,1080};
    float mapVp[16],gameVp[16];
    MapCamera(sceneMap,1920.0f,1080.0f,mapVp);
    Camera(pos,fwd,1920.0f,1080.0f,gameVp);
    auto draw=[&](bool stock,bool map,bool easing,const float* vp,int* n){
        hasStock=stock;hasMap=map;mapEasing=easing;
        HudPublish();
        box=Box{};boxing=true;drawn=0;
        HudDraw(vp,image,&viewport,nullptr,0);
        boxing=false;*n=drawn;
        return box;
    };
    int alone=0,over=0,easing=0,closed=0;
    const Box a=draw(false,true,false,mapVp,&alone),o=draw(true,true,false,mapVp,&over),e=draw(true,false,true,mapVp,&easing),
              b=draw(true,false,false,gameVp,&closed);
    hasMap=mapEasing=false;hasStock=true;
    const bool ok=a.any && alone>0 && over==alone && a.x0==o.x0 && a.y0==o.y0 && a.x1==o.x1 && a.y1==o.y1 && !e.any && easing==0 &&
                  b.any && closed>0;
    std::printf("%s  map over a tank: %d drawn (the map alone %d), easing back %d, closed %d\n",ok ? "ok  " : "FAIL",over,alone,easing,
                closed);
    return ok;
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
        for(int i=0;i<*at;++i)off=off || wcsstr(l[i].text,Tr(Tx::autoAimOffCircle))!=nullptr;
    });
    r.mode=edf::aimlink::Mode::autoAim;
    Measured([&](Text* t,Line* l,int* at){
        TurretAimMarks(drawer,ctx,t,vp,w,h,s,r,false,l,at);
        for(int i=0;i<*at;++i)on=on || (wcsstr(l[i].text,Tr(Tx::autoAimOn))!=nullptr) ;
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
    sceneStock.arms=3;sceneStock.selected=0;sceneStock.sight=0;
    for(int i=0;i<3;++i) {
        StockArm& a=sceneStock.arm[i];a.physicalOnly=false;
        strcpy_s(a.label,names[i]);a.ammo=10-i;a.ammoMax=10;a.reload=1.0f;a.reloadSec=-1.0f;a.canReload=true;a.kind=RoundKind::arc;
    }
    // Their sights as vhud.cpp classifies them (reticle.h): the main gun's, the coaxial MG's ring, the missile's gate.
    sceneStock.arm[0].reticle=reticle::Style::cannon;sceneStock.arm[0].shell=reticle::Shell::ap;sceneStock.arm[0].roundSpeed=810.0f;
    sceneStock.arm[1].reticle=reticle::Style::autocannon;sceneStock.arm[1].shell=reticle::Shell::ap;sceneStock.arm[1].roundSpeed=360.0f;
    sceneStock.arm[2].reticle=reticle::Style::missile;
    sceneStock.threats=2;
    sceneStock.threatKind[0]=2;sceneStock.threatAt[0][0]=pos[0]-600.0f;sceneStock.threatAt[0][1]=pos[1]+80.0f;sceneStock.threatAt[0][2]=pos[2]+900.0f;
    sceneStock.threatKind[1]=1;sceneStock.threatAt[1][0]=pos[0]+1500.0f;sceneStock.threatAt[1][1]=pos[1]+300.0f;sceneStock.threatAt[1][2]=pos[2]-800.0f;
}

// The gun reticle (hud.cpp GunReticle, src/gunsight.h): the E551's cannon (Root.cpk V_505TANK_DLC_CANNON04L: 11 m/frame,
// gravity x 0.25, 600 frames; rounds_check kE551Gun) the seat's sight gun, the ground 1300 m off; drawn at the game's
// view and at a 4x narrower one (the ladder's ticks stand apart, the mils show). Checked: the rangefinder line reads
// the gun's range, the ladder's labels go down the screen as their ranges go up, no two texts overlap; zoomed, the
// flat cannon's ladder shows a labelled tick (its round drops 3 m in 1000 m: most of its ticks stand within a label's
// height of each other even 4x zoomed, numbered only where one fits; gunsight_check checks where they are).
int TankSightScenes(const std::wstring& dir,const float* ground) {
    StockTank(ground);
    StockArm& a=sceneStock.arm[0];
    const float muzzle[3]={ground[0],ground[1]+2.6f,ground[2]+4.0f};
    float bore[3]={0.0f,0.004f,1.0f};vec::Normalize(bore);
    const roundaim::Round round{11.0f,{0.0f,-9.8f*0.25f/3600.0f,0.0f},0.0f,600};
    const float still[3]={0.0f,0.0f,0.0f};
    a.aimed=true;a.hit=true;a.kind=RoundKind::arc;
    std::memcpy(a.bore,bore,12);
    a.ladder=gunsight::Of(round,muzzle,bore,still);
    a.at[0]=muzzle[0];a.at[1]=ground[1];a.at[2]=muzzle[2]+1300.0f;
    a.range=1300.0f;a.flight=1300.0f/660.0f;
    int failed=0;
    const auto check=[&](const wchar_t* name,int wantTicks){
        Scene(dir,name,ground);
        bool lrf=false,order=true,apart=true;int ticks=0,lastRange=0;float lastY=-1e9f;
        const bool zoomed=sceneZoom>1.0f;
        for(const Drew& d:drew) {
            // Tx::sightRange in any language; through the optic the computer's LED range (four digits)
            if(d.text.find(zoomed ? L"1300" : L"1300 m")!=std::wstring::npos)lrf=true;
            // through the optic the lead ticks' mils are numbered over the cross: the range marks are the ones under it
            if(zoomed && (d.y0<540.0f || (!d.text.empty() && d.text.back()==L'x')))continue;   // (the data line's power, "3x")
            if(d.text.size()<=2 && !d.text.empty() && d.text[0]>=L'1' && d.text[0]<=L'9') {
                const int r=std::stoi(d.text);
                order=order && r>lastRange && d.y0>lastY;   // farther ranges further down
                lastRange=r;lastY=d.y0;++ticks;
            }
        }
        for(std::size_t i=0;i<drew.size();++i)
            for(std::size_t k=i+1;k<drew.size();++k) {
                const Drew& p=drew[i];const Drew& q=drew[k];
                if(p.text.empty() || q.text.empty())continue;
                if(p.x0<q.x1 && q.x0<p.x1 && p.y0<q.y1 && q.y0<p.y1){apart=false;std::printf("      overlap: %ls / %ls\n",p.text.c_str(),q.text.c_str());}
            }
        const bool ok=lrf && ticks>=wantTicks && order && apart;
        failed+=!ok;
        std::printf("%s  %ls: rangefinder line %d, ladder labels %d (want >= %d) in order %d, no text overlapping %d\n",
                    ok ? "ok  " : "FAIL",name,lrf,ticks,wantTicks,order,apart);
    };
    check(L"stock_tank_sight",0);
    sceneFov=55.0f/3.0f;sceneStock.zoom=3.0f;sceneZoom=3.0f;   // the sight at 3x (sightzoom.cpp: the camera's field of view a third)
    check(L"stock_tank_sight_zoom",1);
    bool named=false;
    for(const Drew& d:drew)named=named || d.text.find(L"3x")!=std::wstring::npos;
    failed+=!named;
    std::printf("%s  stock_tank_sight_zoom: the magnification named (3x) %d\n",named ? "ok  " : "FAIL",named);
    // Through the optic: the eyepiece (its outer black kSides pieces), no green HUD reticle text (the AP/HE tag), the
    // reticle etched black and its aiming point lit red, the LED data line.
    {
        sceneFov=55.0f/6.0f;sceneStock.zoom=6.0f;sceneZoom=6.0f;
        scopePieces=0;Scene(dir,L"stock_tank_sight_zoom_6x",ground);
        bool tag=false,ready=false,boreLine=false;
        for(const Drew& d:drew){tag=tag || d.text==L"AP" || d.text==L"HE";ready=ready || d.text==Tr(Tx::scopeReady);boreLine=boreLine || d.text==Tr(Tx::scopeZeroBore);}
        const bool ok=scopePieces==scopeview::kSides && !tag && ready;
        failed+=!ok;
        std::printf("%s  stock_tank_sight_zoom_6x: eyepiece %d, no HUD tag %d, data line %d\n",ok ? "ok  " : "FAIL",scopePieces,!tag,ready);
        // The range setting (the wheel: sightzoom.h): named on the data line, ---- at the bore line.
        sceneStock.zero=400;Scene(dir,L"stock_tank_sight_zoom_range",ground);sceneStock.zero=0;
        wchar_t zero[32];_snwprintf_s(zero,_countof(zero),_TRUNCATE,Tr(Tx::scopeZero),400);
        bool named400=false;
        for(const Drew& d:drew)named400=named400 || d.text==zero;
        failed+=!(named400 && boreLine);
        std::printf("%s  stock_tank_sight_zoom_range: the setting named %d, the bore line named %d\n",named400 && boreLine ? "ok  " : "FAIL",named400,boreLine);
        sceneFov=55.0f/3.0f;sceneStock.zoom=3.0f;sceneZoom=3.0f;   // the fractional ladder below is read through the 3x optic
    }
    // A 250 m ladder must say 2.5 / 7.5 hundreds, not truncate those ranges to 2 / 7.
    a.ladder={};a.ladder.step=250.0f;a.ladder.ticks=2;
    for(int i=0;i<2;++i) {
        a.ladder.range[i]=i ? 750.0f : 250.0f;
        a.ladder.at[i][0]=ground[0];a.ladder.at[i][1]=ground[1]-(i ? 45.0f : 10.0f);
        a.ladder.at[i][2]=ground[2]+a.ladder.range[i];
    }
    Scene(dir,L"stock_tank_sight_fractional_ranges",ground);
    bool half250=false,half750=false;
    for(const Drew& d:drew){half250=half250 || d.text==L"2.5";half750=half750 || d.text==L"7.5";}
    failed+=!(half250 && half750);
    std::printf("%s  250 m ladder labels preserve half hundreds\n",half250 && half750 ? "ok  " : "FAIL");
    sceneFov=55.0f;sceneStock.zoom=1.0f;sceneZoom=1.0f;
    StockTank(ground);
    return failed;
}

// Each sight style (src/reticle.h) on the seat's sight gun, with the real numbers of a weapon that gets it (Root.cpk
// SGO, tools/reticle_check.cpp kWeapons): AmmoSpeed m/frame, the gravity factor, AmmoAlive. Drawn at the game's view
// and with the scope at 3x (a mock: only the 403/404/505/601 carry a model-authored optic in game, optic_mount.h).
// Checked: the sight's own words (the round, the flak rings' km/h) are drawn, no two texts overlap, SightGun owns it;
// the zoomed scope carries the style's posts (reticle.h Spec: the scope's ring pieces stay kSides).
int SightStyleScenes(const std::wstring& dir,const float* ground) {
    struct Case {
        const wchar_t* name; reticle::Style style; reticle::Shell shell; const char* label;
        float speed,gravity; int alive; const wchar_t* words[3];
    };
    const Case cases[]={
        {L"sight_cannon_ap",reticle::Style::cannon,reticle::Shell::ap,"CANNON",22.5f,0.5f,150,{L"AP",nullptr}},          // V_403TANK_CANNON02
        {L"sight_cannon_he",reticle::Style::cannon,reticle::Shell::he,"CANNON",11.0f,0.25f,600,{L"HE",nullptr}},         // V_505TANK_CANNON04L
        {L"sight_precision",reticle::Style::precision,reticle::Shell::ap,"GUN",20.0f,1.0f,60,{nullptr}},                 // V_502_GROUNDROBO_SNIPE01
        {L"sight_autocannon",reticle::Style::autocannon,reticle::Shell::he,"CANNON",4.0f,0.5f,150,{L"HE",nullptr}},      // V_401STRIKER_CANNON
        {L"sight_machinegun",reticle::Style::autocannon,reticle::Shell::ap,"GUN",6.0f,0.0f,35,{L"AP",nullptr}},          // V_403TANK_MACHINEGUN
        {L"sight_flak",reticle::Style::flak,reticle::Shell::ap,"GUN",6.0f,0.75f,60,{L"AA",L"90",L"180"}},                // V603_FLAK_GUN02
        {L"sight_energy",reticle::Style::energy,reticle::Shell::beam,"LASER",120.0f,0.0f,8,{L"BEAM",nullptr}},           // V_612_A_LASERRIFLE_DLC2
    };
    int failed=0;
    for(const Case& c:cases)for(int zoomed=0;zoomed<2;++zoomed) {
        StockTank(ground);sceneStock.threats=0;
        StockArm& a=sceneStock.arm[0];
        strcpy_s(a.label,c.label);
        a.reticle=c.style;a.shell=c.shell;a.roundSpeed=c.speed*60.0f;
        a.style=c.shell==reticle::Shell::beam ? WeaponStyle::laser : WeaponStyle::projectile;
        const float muzzle[3]={ground[0],ground[1]+2.6f,ground[2]+4.0f};
        float bore[3]={0.0f,0.004f,1.0f};vec::Normalize(bore);
        const roundaim::Round round{c.speed,{0.0f,-9.8f*c.gravity/3600.0f,0.0f},0.0f,c.alive};
        const float still[3]={0.0f,0.0f,0.0f};
        a.aimed=a.hit=true;a.kind=RoundKind::arc;std::memcpy(a.bore,bore,12);
        a.ladder=c.shell==reticle::Shell::beam ? gunsight::Ladder{} : gunsight::Of(round,muzzle,bore,still);
        a.at[0]=muzzle[0];a.at[1]=ground[1];a.at[2]=muzzle[2]+300.0f;a.range=300.0f;a.flight=300.0f/(c.speed*60.0f);
        sceneZoom=zoomed ? 3.0f : 1.0f;sceneFov=55.0f/sceneZoom;sceneStock.zoom=sceneZoom;sceneSight=sightzoom::Kind::optical;
        hasMountedOptic=zoomed!=0;
        const std::wstring name=std::wstring(c.name)+(zoomed ? L"_zoom" : L"");
        scopePieces=0;Scene(dir,name.c_str(),ground);
        bool words=true,apart=true;
        // Through the optic the round is the data line's (the weapon's name) and the power is named; a flak sight's
        // lead rings keep their km/h. At 1x the HUD reticle's own words.
        const wchar_t* zoomWords[3]={L"3x",c.style==reticle::Style::flak ? L"90" : nullptr,c.style==reticle::Style::flak ? L"180" : nullptr};
        const wchar_t* const* list=zoomed ? zoomWords : c.words;
        for(int wi=0;wi<3;++wi) {
            const wchar_t* w=list[wi];
            if(!w)break;
            bool found=false;
            for(const Drew& d:drew)found=found || d.text==w;
            if(!found){words=false;std::printf("      missing: %ls\n",w);}
        }
        for(std::size_t i=0;i<drew.size();++i)
            for(std::size_t k=i+1;k<drew.size();++k) {
                const Drew& p=drew[i];const Drew& q=drew[k];
                if(p.text.empty() || q.text.empty())continue;
                if(p.x0<q.x1 && q.x0<p.x1 && p.y0<q.y1 && q.y0<p.y1){apart=false;std::printf("      overlap: %ls / %ls\n",p.text.c_str(),q.text.c_str());}
            }
        const bool owned=SightGun(sceneStock,false)==0;
        const bool scope=zoomed ? scopePieces==scopeview::kSides : scopePieces==0;
        // The stock crosshair (HudPlayer_CrossHair) gives way to the field drawn, for its soldier only (scope_hold.h; the
        // user, 2026-10-21: "进入瞄具状态的时候没隐藏准星"); at 1x it stays.
        const int otherSoldier=0;
        // Read at the time the scene drew it: the 250 ms freshness is tests/scope_hold_test.cpp's, and a loaded machine
        // (ctest -j) can take longer than that between this scene's draw and here.
        const scopehold::Drawn published=scopeDrawn;
        const auto held=[&](const void* soldier){return scopehold::Holds(published,soldier,published.at);};
        const bool crosshair=held(&sceneScopeSoldier)==(zoomed!=0) && !held(&otherSoldier) && !held(nullptr);
        const bool ok=words && apart && owned && scope && crosshair;
        failed+=!ok;
        std::printf("%s  %ls: its words %d, no text overlapping %d, sight gun %d, scope quads %d, stock crosshair held back %d\n",
                    ok ? "ok  " : "FAIL",name.c_str(),words,apart,owned,scopePieces,held(&sceneScopeSoldier));
    }
    // A flamethrower, a grenade launcher: no gun sight (their impact marks only).
    StockTank(ground);
    sceneStock.arm[0].reticle=reticle::Style::none;sceneStock.arm[0].aimed=true;sceneStock.arm[0].ladder.ticks=1;
    const bool none=SightGun(sceneStock,false)==-1;
    failed+=!none;
    std::printf("%s  a weapon without a sight (flamethrower, grenades) gets no gun reticle\n",none ? "ok  " : "FAIL");
    sceneFov=55.0f;sceneZoom=1.0f;sceneStock.zoom=1.0f;hasMountedOptic=false;
    StockTank(ground);
    return failed;
}

// The gunship's gunner (hud.cpp GunshipFrame, GunnerMarks): the frame round the screen's centre with each gun's centre
// mark, the impact cross 1500 m off on the ground under the centre, the gun line under it. Checked: the gun line is
// drawn, inside the screen, apart from every other text.
int GunshipSightScenes(const std::wstring& dir,const float* ground) {
    sceneSight=sightzoom::Kind::sensor;
    hasStock=false;hasGunner=true;
    int failed=0;
    static const wchar_t* names[]={L"gunship_sight_shells",L"gunship_sight_cannon",L"gunship_sight_gatling"};
    for(int g=0;g<3;++g) {
        sceneGunner=GunnerReadout{};
        sceneGunner.ground=sceneGunner.inReach=sceneGunner.ready=true;
        sceneGunner.range=1500.0f;
        sceneGunner.sight[0]=ground[0];sceneGunner.sight[1]=ground[1]+5.0f;sceneGunner.sight[2]=ground[2]+19.0f;   // the camera's centre ray at the ground
        sceneGunner.gun=static_cast<GunnerGun>(g);sceneGunner.guns=7u;
        sceneGunner.zoom=g==2 ? 6.0f : 1.0f;   // the gatling's sight at 6x: its magnification over the frame
        sceneZoom=sceneGunner.zoom;             // ...and the sensor's dark round its field
        StockTank(ground);   // its camera only (Scene looks along sceneStock.hull when hasStock; with it off, along the jet's nose)
        sceneJet.sym.nose[0]=0.0f;sceneJet.sym.nose[1]=0.0f;sceneJet.sym.nose[2]=1.0f;
        Scene(dir,names[g],ground);
        bool line=false,apart=true;
        for(std::size_t i=0;i<drew.size();++i) {
            const Drew& p=drew[i];
            if(p.text.find(L"1500 m")!=std::wstring::npos)line=line || (p.x0>=0.0f && p.x1<=1920.0f && p.y1<=1080.0f);
            for(std::size_t k=i+1;k<drew.size();++k) {
                const Drew& q=drew[k];
                if(!p.text.empty() && !q.text.empty() && p.x0<q.x1 && q.x0<p.x1 && p.y0<q.y1 && q.y0<p.y1)apart=false;
            }
        }
        bool named=false;
        for(const Drew& d:drew)named=named || d.text==L"6x";
        const bool ok=line && apart && named==(g==2);
        failed+=!ok;
        std::printf("%s  %ls: gun line %d, no text overlapping %d\n",ok ? "ok  " : "FAIL",names[g],line,apart);
    }
    hasGunner=false;sceneZoom=1.0f;sceneSight=sightzoom::Kind::optical;
    return failed;
}

// A magnified sight on the other sighted seats (hud.cpp ScopeShade, LadderTicks): a stock heli's gun at 1x and 3x (its
// range ladder under the boresight; a stand-in round: 10 m/frame, the world's gravity, 2 s) and a player jet at 3x.
// Checked: the magnification named once (the scope's own label), no two texts overlapping, the heli gun's ladder
// numbered at 3x; at 1x no scope.
int ZoomScenes(const std::wstring& dir,const float* ground) {
    int failed=0;
    const auto check=[&](const wchar_t* name,int wantX,int wantLabels){
        Scene(dir,name,ground);
        int named=0,labels=0;bool apart=true;
        for(std::size_t i=0;i<drew.size();++i) {
            const Drew& p=drew[i];
            if(p.text==L"3x")++named;
            if(p.text.size()<=2 && !p.text.empty() && p.text[0]>=L'1' && p.text[0]<=L'9' && p.text.back()!=L'x')++labels;
            for(std::size_t k=i+1;k<drew.size();++k) {
                const Drew& q=drew[k];
                if(!p.text.empty() && !q.text.empty() && p.x0<q.x1 && q.x0<p.x1 && p.y0<q.y1 && q.y0<p.y1)apart=false;
            }
        }
        const bool ok=named==wantX && labels>=wantLabels && apart;
        failed+=!ok;
        std::printf("%s  %ls: magnification named %d (want %d), ladder labels %d (want >= %d), no text overlapping %d\n",
                    ok ? "ok  " : "FAIL",name,named,wantX,labels,wantLabels,apart);
    };
    const bool jetWas=hasJet,heliWas=hasHeli,stockWas=hasStock;
    hasJet=hasHeli=hasStock=false;
    sceneHeli.sym.nose[0]=0.0f;sceneHeli.sym.nose[1]=0.0f;sceneHeli.sym.nose[2]=1.0f;
    hasHeliSight=true;sceneHeliSight=HeliSightReadout{};
    sceneHeliSight.gun=sceneHeliSight.hit=true;sceneHeliSight.physicalOnly=false;
    float bore[3]={0.0f,0.006f,1.0f};vec::Normalize(bore);
    std::memcpy(sceneHeliSight.bore,bore,12);
    const float muzzle[3]={ground[0],ground[1]+4.0f,ground[2]-20.0f};   // by the camera (5 m up, 24 m back), as a heli's gun by its view
    const roundaim::Round round{10.0f,{0.0f,-9.8f/3600.0f,0.0f},1.0f,120};
    const float still[3]={0.0f,0.0f,0.0f};
    sceneHeliSight.ladder=gunsight::Of(round,muzzle,bore,still);
    sceneHeliSight.pipper[0]=ground[0];sceneHeliSight.pipper[1]=ground[1];sceneHeliSight.pipper[2]=ground[2]+700.0f;sceneHeliSight.range=700.0f;
    check(L"heli_gun_ladder",0,0);
    sceneFov=55.0f/3.0f;sceneZoom=3.0f;
    // The eyepiece names no power of its own (2026-10-11: no drawn rim or label; the optic's data line, a stock
    // vehicle's, names it): the heli mock (no stock heli carries an optic) shows the field and its ladder.
    check(L"heli_gun_ladder_zoom",0,1);
    hasHeliSight=false;
    hasJet=true;
    sceneSight=sightzoom::Kind::flight;
    check(L"jet_zoom",0,0);
    sceneFov=55.0f;sceneZoom=1.0f;
    sceneSight=sightzoom::Kind::optical;
    hasJet=jetWas;hasHeli=heliWas;hasStock=stockWas;
    return failed;
}

// hudscale::Of against what it must give: the game's screen (uiW x uiH), the viewport drawn in, the ini's HudScale.
// Mock readouts rendered through the production HUD; not game screenshots.
int FixedFireScenes(const std::wstring& dir,const float* ground) {
    hasJet=hasHeli=hasWarn=hasDrill=hasNix=hasMap=hasEmc=hasProteus=hasSazabi=hasGunner=hasHeliSight=hasTurret=false;
    hasHighView=hasLauncher=false;hasStock=true;sceneSight=sightzoom::Kind::optical;sceneZoom=1;sceneFov=55;
    StockTank(ground);sceneStock.threats=0;sceneStock.arms=2;
    for(int i=0;i<2;++i){auto& a=sceneStock.arm[i];a.aimed=a.physicalOnly=a.coFired=true;a.bore[2]=1;a.paths=1;
        a.path[0]={{ground[0]+(i ? 6.f : -6.f),ground[1],ground[2]+450.f},450,3,true};}
    strcpy_s(sceneStock.kind,"Twin artillery");Scene(dir,L"fixed_artillery_two_impacts_mock",ground);
    sceneStock.arm[1].path[0].hit=false;sceneStock.arm[1].path[0].at[1]+=12;
    Scene(dir,L"fixed_artillery_one_miss_mock",ground);
    sceneStock.arms=1;strcpy_s(sceneStock.kind,"Fixed gun");strcpy_s(sceneStock.arm[0].label,"MG");
    Scene(dir,L"fixed_machinegun_ground_mock",ground);
    sceneStock.arm[0].path[0].hit=false;sceneStock.arm[0].path[0].at[1]+=10;
    Scene(dir,L"fixed_machinegun_no_ground_mock",ground);
    hasStock=false;hasHeliSight=true;sceneHeliSight=HeliSightReadout{};sceneHeliSight.gun=true;sceneHeliSight.bore[2]=1;sceneHeli.sym.nose[2]=1;
    sceneHeliSight.paths=2;sceneHeliSight.path[0]={{ground[0]-5,ground[1],ground[2]+500},500,2,true};
    sceneHeliSight.path[1]={{ground[0]+5,ground[1]+10,ground[2]+550},550,3,false};
    Scene(dir,L"fixed_heli_left_hit_right_end_mock",ground);hasHeliSight=false;hasStock=true;
    // Actual marker primitives: two hits=8 cross arms; hit+no-hit=6, no circular pipper.
    float vp[16];Camera(ground,sceneStock.hull,1920,1080,vp);Line lines[kMaxLines]{};int at=0;
    roundaim::Impact paths[2]={{{ground[0]-6,ground[1],ground[2]+450},450,3,true},{{ground[0]+6,ground[1],ground[2]+450},450,3,true}};
    boxing=true;drawn=0;PhysicalPaths(image,nullptr,nullptr,vp,1920,1080,1,paths,2,false,lines,&at);const int two=drawn;
    drawn=0;paths[1].hit=false;PhysicalPaths(image,nullptr,nullptr,vp,1920,1080,1,paths,2,false,lines,&at);const int one=drawn;boxing=false;
    const bool good=two==8 && one==6;std::printf("%s fixed physical markers: two hits=%d, one no-hit=%d, zero pipper circles\n",good ? "ok" : "FAIL",two,one);
    StockTank(ground);return good ? 0 : 1;
}

int FireControlScenes(const std::wstring& dir,const float* ground) {
    int failed=0;
    hasJet=hasHeli=hasWarn=hasDrill=hasNix=hasMap=hasEmc=hasProteus=hasSazabi=hasGunner=hasHeliSight=hasTurret=false;
    hasStock=true;
    const sightzoom::Kind kinds[]={sightzoom::Kind::optical,sightzoom::Kind::missile,sightzoom::Kind::rocket,
                                  sightzoom::Kind::indirect,sightzoom::Kind::none};
    const wchar_t* names[]={L"firecontrol_cannon",L"firecontrol_missile",L"firecontrol_rocket",L"firecontrol_katyusha_overhead",L"firecontrol_unarmed"};
    for(int i=0;i<5;++i) {
        StockTank(ground);sceneStock.threats=0;
        sceneSight=kinds[i];sceneZoom=sightzoom::Magnifies(sceneSight) ? 3.0f : 1.0f;sceneFov=55.0f/sceneZoom;
        sceneStock.zoom=sceneZoom;
        StockArm& gun=sceneStock.arm[0];gun.aimed=gun.hit=true;gun.bore[2]=1;gun.ladder.ticks=1;
        gun.ladder.at[0][2]=700;gun.ladder.range[0]=700;gun.at[2]=700;gun.range=700;
        sceneStock.selected=(i==1 || i==2) ? 1 : 0;
        sceneStock.sight=sceneStock.selected;
        StockArm& selected=sceneStock.arm[sceneStock.selected];
        selected.aimed=selected.hit=true;selected.bore[2]=1;selected.at[2]=600;selected.range=600;
        selected.kind=i==1 ? RoundKind::homing : i==2 ? RoundKind::rocket : RoundKind::arc;
        if(i==1){strcpy_s(selected.label,"MISSILE");sceneStock.arms=2;}
        if(i==2){strcpy_s(selected.label,"RKT");sceneStock.arms=2;}
        if(i==3){strcpy_s(sceneStock.kind,"Katyusha");strcpy_s(selected.label,"ROCKETS");sceneStock.arms=1;}
        if(i==4)strcpy_s(sceneStock.kind,"Transport");
        selected.lofted=i==3;selected.lock=i==1 ? 2 : 0;selected.lockProgress=1;
        if(i==4)sceneStock.arms=0;
        hasHighView=hasLauncher=i==3;sceneLauncher=LauncherReadout{};
        sceneLauncher.reach=true;sceneLauncher.impact[2]=600;sceneLauncher.range=600;sceneLauncher.flight=4;
        if(i==3) {
            sceneLauncher.elevation=45;sceneLauncher.rings=kLauncherRing;
            for(int k=0;k<kLauncherRing;++k){const float angle=2*tcam::kPi*k/kLauncherRing;sceneLauncher.ring[k][0]=12*std::cos(angle);sceneLauncher.ring[k][2]=600+12*std::sin(angle);}
        }
        scopePieces=0;Scene(dir,names[i],ground);
        // A gun-launched missile / rocket on the seat is looked at through the same optic (2026-10-11): its round field.
        const int want=i<=2 ? scopeview::kSides : 0;
        const bool right=scopePieces==want && (i==0 ? SightGun(sceneStock,false)==0 : SightGun(sceneStock,false)==-1);
        failed+=!right;
        std::printf("%s fire-control owner %ls: scope quads %d/%d, no fallback optical reticle\n",right ? "ok" : "FAIL",names[i],scopePieces,want);
    }
    hasHighView=hasLauncher=false;sceneSight=sightzoom::Kind::optical;sceneZoom=1;sceneFov=55;
    StockTank(ground);
    return failed;
}

// A soldier's open scope (hud.cpp InfantryScope, infantryscope.h; the user, 2026-10-11): a marksman's rifle at 5.5x (the
// KFF 50's SecondaryFire_Parameter), a quick-firing rifle's combat optic at 4x, a rocket launcher's at 3x, on foot (no
// vehicle HUD). Checked: the eyepiece's black all round (kSides pieces), every number etched on the glass inside the
// field, no two texts overlapping; with no scope open nothing of it.
int InfantryScopeScenes(const std::wstring& dir,const float* ground) {
    int failed=0;
    const bool stockWas=hasStock;
    hasStock=false;hasJet=hasHeli=hasGunner=hasHeliSight=hasTurret=false;sceneSight=sightzoom::Kind::none;sceneZoom=1.0f;
    struct Case { const wchar_t* name; scope::Look look; float zoom; int numbers; };
    const Case cases[]={{L"infantry_scope_marksman",scope::Look::marksman,5.5f,2},{L"infantry_scope_combat",scope::Look::combat,4.0f,1},
                        {L"infantry_scope_launcher",scope::Look::launcher,3.0f,3}};
    for(const Case& c:cases) {
        hasInfantry=true;sceneInfantry=InfantryScopeCue{&sceneInfantry,c.zoom,c.look};
        sceneFov=55.0f/c.zoom;
        StockTank(ground);hasStock=false;
        sceneJet.sym.nose[0]=0.0f;sceneJet.sym.nose[1]=0.0f;sceneJet.sym.nose[2]=1.0f;   // the camera along +Z (Scene)
        scopePieces=0;Scene(dir,c.name,ground);
        int numbers=0;bool inside=true,apart=true;
        const float r=scopeview::Radius(1920.0f,1080.0f);
        for(std::size_t i=0;i<drew.size();++i) {
            const Drew& p=drew[i];
            if(!p.text.empty() && p.text[0]>=L'0' && p.text[0]<=L'9') {
                ++numbers;
                const float x=(p.x0+p.x1)*0.5f-960.0f,y=(p.y0+p.y1)*0.5f-540.0f;
                inside=inside && x*x+y*y<=r*r;
            }
            for(std::size_t k=i+1;k<drew.size();++k) {
                const Drew& q=drew[k];
                if(!p.text.empty() && !q.text.empty() && p.x0<q.x1 && q.x0<p.x1 && p.y0<q.y1 && q.y0<p.y1){apart=false;std::printf("      overlap: %ls / %ls\n",p.text.c_str(),q.text.c_str());}
            }
        }
        const bool ok=scopePieces==scopeview::kSides && numbers>=c.numbers && inside && apart;
        failed+=!ok;
        std::printf("%s  %ls: eyepiece %d, etched numbers %d (want >= %d) inside %d, no text overlapping %d\n",ok ? "ok  " : "FAIL",c.name,
                    scopePieces,numbers,c.numbers,inside,apart);
    }
    hasInfantry=false;scopePieces=0;sceneFov=55.0f;
    Scene(dir,L"infantry_scope_closed",ground);
    failed+=scopePieces!=0;
    std::printf("%s  infantry_scope_closed: no eyepiece %d\n",scopePieces==0 ? "ok  " : "FAIL",scopePieces);
    hasStock=stockWas;sceneSight=sightzoom::Kind::optical;
    StockTank(ground);
    return failed;
}

int ScaleOfChecks() {
    struct Case { const char* what; int uiW,uiH,viewW,viewH; float user,want; };
    const Case cases[]={
        {"1280x720",1280,720,1280,720,1.0f,720.0f/1080.0f},
        {"1920x1080",1920,1080,1920,1080,1.0f,1.0f},
        {"2560x1440",2560,1440,2560,1440,1.0f,1440.0f/1080.0f},
        {"3840x2160",3840,2160,3840,2160,1.0f,2.0f},
        {"3440x1440 (21:9: by the height)",3440,1440,3440,1440,1.0f,1440.0f/1080.0f},
        {"3840x2160 split left/right (1920x2160)",3840,2160,1920,2160,1.0f,2.0f},
        {"1920x1080 split top/bottom (1920x540)",1920,1080,1920,540,1.0f,1.0f},
        {"screen unread: the viewport 2560x1440",0,0,2560,1440,1.0f,1440.0f/1080.0f},
        {"screen 1280x720 under a 1920x1080 viewport",1280,720,1920,1080,1.0f,1.0f},
        {"HudScale 1.5 at 1080",1920,1080,1920,1080,1.5f,1.5f},
        {"HudScale 10 held to 3",1920,1080,1920,1080,10.0f,3.0f},
        {"HudScale 0.1 held to 0.5",1920,1080,1920,1080,0.1f,0.5f},
        {"HudScale NaN: none of it",1920,1080,1920,1080,std::nanf(""),1.0f},
    };
    int failed=0;
    for(const Case& c:cases) {
        const float got=hudscale::Of(c.uiW,c.uiH,c.viewW,c.viewH,c.user);
        const bool ok=std::fabs(got-c.want)<1e-4f;
        failed+=!ok;
        std::printf("%s  scale %-46s %.4f (want %.4f)\n",ok ? "ok  " : "FAIL",c.what,got,c.want);
    }
    const float maps[][4]={{1.5f,1920,1080,1},{2,1920,540,0.5f},{3,640,1080,2.0f/3.0f},{0.5f,1920,1080,0.5f},{3,3840,2160,2}};
    for(const auto& m:maps) {
        const float got=hudscale::FitMap(m[0],m[1],m[2]);
        if(std::fabs(got-m[3])>0.001f){++failed;std::printf("FAIL fitted map scale %.3f expected %.3f\n",got,m[3]);}
    }

    return failed;
}

// The stock tank's HUD at a viewport (viewW x viewH) of the game's screen (uiW x uiH): the tallest glyph HudDraw drew,
// and the stock block's and the RWR scope's boxes drawn at the scale `s`, taken from their anchor (W/2, 0.80 H) and
// divided by `s`: in design pixels, the same on every screen when the HUD scales as one.
struct Drawn { float glyph; Box block,rwr; };
Box Design(Box b,float w,float h,float s) {
    const float ax=w*0.5f,ay=h*0.80f;
    return Box{(b.x0-ax)/s,(b.y0-ay)/s,(b.x1-ax)/s,(b.y1-ay)/s,b.any};
}
Drawn DrawnAt(int uiW,int uiH,int viewW,int viewH,float s) {
    SetScreen(uiW,uiH);
    const float w=static_cast<float>(viewW),h=static_cast<float>(viewH);
    float vp[16];
    Camera(sceneStock.pos,sceneStock.hull,w,h,vp);
    HudPublish();
    struct { std::int32_t x,y,w,h; } viewport{0,0,viewW,viewH};
    tallest=0.0f;
    HudDraw(vp,image,&viewport,nullptr,0);
    Drawn d{};
    d.glyph=tallest;
    void* const drawer=At<void*>(image,kQuadDrawer);
    static unsigned char ctx[16]{};
    PlayerJetSymbols y{};
    std::memcpy(y.pos,sceneStock.pos,12);std::memcpy(y.nose,sceneStock.hull,12);
    y.threats=sceneStock.threats;
    for(int i=0;i<y.threats;++i){std::memcpy(y.threatAt[i],sceneStock.threatAt[i],12);y.threatKind[i]=sceneStock.threatKind[i];}
    const StockExtras x{nullptr,nullptr,false,nullptr,nullptr};
    d.block=Design(Measured([&](Text* t,Line* l,int* at){StockBlock(drawer,ctx,t,w,h,s,sceneStock,x,l,at);},s),w,h,s);
    d.rwr=Design(Measured([&](Text* t,Line* l,int* at){RwrScope(drawer,ctx,t,w,h,s,y,0,GetTickCount64(),1.0f,l,at);},s),w,h,s);
    return d;
}
// A design box the same as the design's `b`: within 4 design px and 4% of b's width / height on every side (the text
// in it is sized on 1/32 font steps, hud.cpp kTextScaleStep: up to ~3% apart between two scales).
bool Same(const Box& a,const Box& b) {
    const float tx=4.0f+0.04f*(b.x1-b.x0),ty=4.0f+0.04f*(b.y1-b.y0);
    return a.any && b.any && std::fabs(a.x0-b.x0)<=tx && std::fabs(a.y0-b.y0)<=ty && std::fabs(a.x1-b.x1)<=tx &&
           std::fabs(a.y1-b.y1)<=ty;
}
// The stock HUD on the typical screens and split viewports against the 1920x1080 design: its glyphs (from HudDraw,
// which reads the screen itself) the design's times the scale, its block and RWR scope the same in design pixels.
// The text's scale is held to 1/32 steps (hud.cpp kTextScaleStep): a glyph within 4%, a box as Same.
int ScaleDrawnChecks() {
    const float ground[3]={0.0f,0.0f,0.0f};
    StockTank(ground);
    config.hudScale=1.0f;
    const Drawn ref=DrawnAt(1920,1080,1920,1080,1.0f);
    std::printf("      1920x1080 design: glyph %.1f px, block (%.0f,%.0f)-(%.0f,%.0f), RWR (%.0f,%.0f)-(%.0f,%.0f) from (W/2, 0.80 H)\n",
                ref.glyph,ref.block.x0,ref.block.y0,ref.block.x1,ref.block.y1,ref.rwr.x0,ref.rwr.y0,ref.rwr.x1,ref.rwr.y1);
    struct Case { const char* what; int uiW,uiH,viewW,viewH; float user,s; };
    const Case cases[]={
        {"1280x720",1280,720,1280,720,1.0f,720.0f/1080.0f},
        {"2560x1440",2560,1440,2560,1440,1.0f,1440.0f/1080.0f},
        {"3840x2160",3840,2160,3840,2160,1.0f,2.0f},
        {"3840x2160 split left/right: 1920x2160",3840,2160,1920,2160,1.0f,2.0f},
        {"1920x1080 split top/bottom: 1920x540",1920,1080,1920,540,1.0f,1.0f},
        {"3840x2160, HudScale 1.5",3840,2160,3840,2160,1.5f,3.0f},
    };
    int failed=0;
    for(const Case& c:cases) {
        config.hudScale=c.user;
        const Drawn d=DrawnAt(c.uiW,c.uiH,c.viewW,c.viewH,c.s);
        const float glyph=d.glyph/c.s;
        const bool ok=std::fabs(glyph-ref.glyph)<=0.04f*ref.glyph && Same(d.block,ref.block) && Same(d.rwr,ref.rwr);
        failed+=!ok;
        std::printf("%s  %-40s s=%.3f glyph %.1f px (%.1f design), block (%.0f,%.0f)-(%.0f,%.0f), RWR (%.0f,%.0f)-(%.0f,%.0f)\n",
                    ok ? "ok  " : "FAIL",c.what,c.s,d.glyph,glyph,d.block.x0,d.block.y0,d.block.x1,d.block.y1,d.rwr.x0,d.rwr.y0,
                    d.rwr.x1,d.rwr.y1);
    }
    config.hudScale=1.0f;
    return failed;
}
// The Sazabi (hud.cpp SazabiHud), on its feet at `pos` heading +z, on its own camera (centred): everything ready, the
// shield missiles selected, the centre's ray meeting something 412 m out. Its aim 200 m ahead (where the reticle goes
// when the camera is not its own), its lock on a target ahead and to the right (the camera's right is -x).
void SazabiCueAt(const float* pos) {
    sceneSazabi=SazabiCue{};
    sceneSazabi.thruster=1.0f;sceneSazabi.rifleReady=1.0f;sceneSazabi.missiles=12;sceneSazabi.missileReady=1.0f;
    sceneSazabi.cannonReady=1.0f;sceneSazabi.funnelReady=1.0f;
    sceneSazabi.hasAim=true;sceneSazabi.aim[0]=pos[0]+8.0f;sceneSazabi.aim[1]=pos[1]+20.0f;sceneSazabi.aim[2]=pos[2]+200.0f;
    sceneSazabi.lock[0]=pos[0]-90.0f;sceneSazabi.lock[1]=pos[1]+70.0f;sceneSazabi.lock[2]=pos[2]+320.0f;
    sceneSazabi.centred=true;sceneSazabi.aimHit=true;sceneSazabi.aimRange=412.0f;
}
// The Sazabi's states the layout is checked in: idle; the widest (every tag lit, locked, overheated, the cannon charging);
// the funnels out; low and boosting high in the air with the missiles locking.
void SazabiState(int k,const float* pos) {
    SazabiCueAt(pos);
    SazabiCue& c=sceneSazabi;
    if(k==1){c.guard=c.air=c.swinging=c.overheat=true;c.hasLock=true;c.missileLock=1.0f;c.thruster=0.0f;c.special=2;c.cannonCharge=0.62f;
             c.altitude=9999.0f;c.speed=99.0f;c.climb=-30.0f;c.aimRange=99999.0f;}
    if(k==2){c.special=1;c.funnelsOut=4;c.funnelReady=0.3f;
             c.hasAssist=true;c.assist[0]=pos[0]-6.0f;c.assist[1]=pos[1]+18.0f;c.assist[2]=pos[2]+240.0f;}   // the aim assist's enemy
    if(k==3){c.hasAssist=c.lockOn=true;c.assist[0]=pos[0]+20.0f;c.assist[1]=pos[1]+30.0f;c.assist[2]=pos[2]+260.0f;   // locked on
             c.air=c.boosting=true;c.altitude=312.0f;c.speed=48.0f;c.climb=14.0f;c.thruster=0.2f;c.hasLock=true;c.missileLock=0.5f;}
}
constexpr int kSazabiStates=4;
// A box in design px from the screen's centre (the Sazabi's gauges' anchor).
Box FromCentre(Box b,float w,float h,float s) {
    const float ax=w*0.5f,ay=h*0.5f;
    return Box{(b.x0-ax)/s,(b.y0-ay)/s,(b.x1-ax)/s,(b.y1-ay)/s,b.any};
}
// The nearest a box comes to (x, y).
float NearestTo(const Box& b,float x,float y) {
    const float dx=x<b.x0 ? b.x0-x : x>b.x1 ? x-b.x1 : 0.0f,dy=y<b.y0 ? b.y0-y : y>b.y1 ? y-b.y1 : 0.0f;
    return std::sqrt(dx*dx+dy*dy);
}
bool Apart(const Box& a,const Box& b) { return a.x1<=b.x0 || b.x1<=a.x0 || a.y1<=b.y0 || b.y1<=a.y0; }
// The Sazabi's HUD on the screens and HUD scales hud_view checks the stock HUD on (ScaleDrawnChecks), in each state of
// SazabiState: every piece on the screen; the reticle at the screen's exact centre (centred) and its range within the
// centre's keep-out (hud.cpp kSazabiKeepOut); the thrusters' arc, the altitude and speed, the special's readout and the
// panel all outside it and apart from each other and from the reticle; the centre's pieces the same in design px as at
// 1920x1080, and the panel too wherever the screen has room for it at its place (a narrow viewport holds it at the left
// edge, or at the screen's foot where the gauges would meet it). Its lines inside it: Record's check.
int SazabiLayoutChecks(const float* pos) {
    struct Case { const char* what; int uiW,uiH,viewW,viewH; float user,s; };
    const Case cases[]={
        {"1920x1080",1920,1080,1920,1080,1.0f,1.0f},
        {"2520x1080 (21:9)",2520,1080,2520,1080,1.0f,1.0f},
        {"1280x720",1280,720,1280,720,1.0f,720.0f/1080.0f},
        {"2560x1440",2560,1440,2560,1440,1.0f,1440.0f/1080.0f},
        {"3840x2160",3840,2160,3840,2160,1.0f,2.0f},
        {"3840x2160 split left/right: 1920x2160",3840,2160,1920,2160,1.0f,2.0f},
        {"1920x1080 split top/bottom: 1920x540",1920,1080,1920,540,1.0f,1.0f},
        {"1920x1080, HudScale 1.5",1920,1080,1920,1080,1.5f,1.5f},
        {"3840x2160, HudScale 1.5",3840,2160,3840,2160,1.5f,3.0f},
    };
    void* const drawer=At<void*>(image,kQuadDrawer);
    static unsigned char ctx[16]{};
    const float fwd[3]={0.0f,0.0f,1.0f};
    int failed=0;
    for(int k=0;k<kSazabiStates;++k) {
        SazabiState(k,pos);
        Box ref[5]{};
        for(const Case& c:cases) {
            config.hudScale=c.user;
            SetScreen(c.uiW,c.uiH);
            const float w=static_cast<float>(c.viewW),h=static_cast<float>(c.viewH),s=c.s,cx=w*0.5f,cy=h*0.5f;
            float vp[16];
            Camera(pos,fwd,w,h,vp);
            const SazabiCue& q=sceneSazabi;
            const Box ring=Measured([&](Text*,Line*,int*){SazabiReticle(drawer,ctx,vp,w,h,s,q);},s);
            Box b[5];   // the reticle and its range, the thrusters, the altitude and speed, the special, the panel
            b[0]=Measured([&](Text* t,Line* l,int* at){SazabiReticle(drawer,ctx,vp,w,h,s,q);SazabiRange(t,vp,w,h,s,q,l,at);},s);
            b[1]=Measured([&](Text* t,Line* l,int* at){SazabiThrusterArc(drawer,ctx,t,w,h,s,q,l,at);},s);
            b[2]=Measured([&](Text* t,Line* l,int* at){SazabiAltSpeed(drawer,ctx,t,w,h,s,q,l,at);},s);
            b[3]=Measured([&](Text* t,Line* l,int* at){SazabiSpecial(drawer,ctx,t,w,h,s,q,l,at);},s);
            b[4]=Measured([&](Text* t,Line* l,int* at){SazabiPanel(drawer,ctx,t,w,h,s,q,l,at);},s);
            static const char* const kNames[5]={"reticle","thrusters","alt/speed","special","panel"};
            char why[256]="";
            auto fail=[&](const char* f,int i,int j){
                if(!why[0])std::snprintf(why,sizeof(why),f,kNames[i],j>=0 ? kNames[j] : "");
            };
            const float keep=kSazabiKeepOut*s;
            if(std::fabs((ring.x0+ring.x1)*0.5f-cx)>0.5f || std::fabs((ring.y0+ring.y1)*0.5f-cy)>0.5f)fail("%s not at the centre%s",0,-1);
            for(int i=0;i<5;++i) {
                if(!b[i].any || b[i].x0<-0.5f || b[i].y0<-0.5f || b[i].x1>w+0.5f || b[i].y1>h+0.5f)fail("%s off the screen%s",i,-1);
                if(i>0 && NearestTo(b[i],cx,cy)<keep-0.5f)fail("%s in the centre's keep-out%s",i,-1);
                for(int j=0;j<i;++j)if(!Apart(b[i],b[j]))fail("%s meets the %s",i,j);
            }
            const float corner=std::fmax(std::fmax(std::hypot(b[0].x0-cx,b[0].y0-cy),std::hypot(b[0].x1-cx,b[0].y0-cy)),
                                         std::fmax(std::hypot(b[0].x0-cx,b[0].y1-cy),std::hypot(b[0].x1-cx,b[0].y1-cy)));
            if(corner>keep+0.5f)fail("%s past the centre's keep-out%s",0,-1);
            const bool room=w*0.5f-kSazabiLeft*s>=16.0f*s;
            for(int i=0;i<5;++i) {   // the panel's anchor (W/2, 0.80 H) as the stock block's, the rest's the centre
                const Box d=i==4 ? Design(b[i],w,h,s) : FromCentre(b[i],w,h,s);
                if(&c==&cases[0]){ref[i]=d;continue;}
                const bool held=i==4 && (!room || b[4].y1>=h-16.0f*s-0.5f);   // at the left edge or the screen's foot
                if(!held && !Same(d,ref[i]))fail("%s not as at 1920x1080 in design px%s",i,-1);
            }
            failed+=why[0]!=0;
            const Box p=FromCentre(b[4],w,h,s);
            std::printf("%s  Sazabi %d %-40s s=%.3f panel (%.0f,%.0f)-(%.0f,%.0f) design from C%s%s\n",why[0] ? "FAIL" : "ok  ",k,
                        c.what,s,p.x0,p.y0,p.x1,p.y1,why[0] ? ": " : "",why);
        }
    }
    config.hudScale=1.0f;
    SetScreen(1920,1080);
    return failed;
}
// A Sazabi scene on a screen of its own: the game's uiW x uiH, the viewport viewW x viewH, the ini's HudScale `user`.
void SazabiSceneAt(const std::wstring& dir,const wchar_t* name,const float* pos,int uiW,int uiH,int viewW,int viewH,float user) {
    const std::wstring path=dir+L"\\"+name+L".txt";
    if(_wfopen_s(&out,path.c_str(),L"w") || !out)return;
    std::fprintf(out,"W %d %d\n",viewW,viewH);
    config.hudScale=user;
    SetScreen(uiW,uiH);
    float vp[16];
    const float fwd[3]={0.0f,0.0f,1.0f};
    Camera(pos,fwd,static_cast<float>(viewW),static_cast<float>(viewH),vp);
    HudPublish();
    struct { std::int32_t x,y,w,h; } viewport{0,0,viewW,viewH};
    Record(name,[&]{HudDraw(vp,image,&viewport,nullptr,0);},viewW,viewH);
    std::fclose(out);out=nullptr;
    config.hudScale=1.0f;
    SetScreen(1920,1080);
    std::printf("%ls\n",path.c_str());
}
// The stock armor gauge's numbers in our HUD (the user, 2026-10-09: "让护甲显示在咱们的hud不就行了"; stockgauge.cpp hides
// the gauge, hud.cpp ArmorPanel / StockBlock show its numbers): in each covered vehicle (a stock tank: in its block under
// the hull's bar; a jet, a rotor craft, a stock heli, the Sazabi: the panel at the top left with the hull's line), at
// 16:9, 21:9, 1280 and a split screen's half (960): the armor's line drawn once, on the screen, clear of every other
// line; never drawn on foot, with HideStockGauges=0 or when the numbers do not read (the gauge then shows: not hidden);
// red at the stock gauge's low mark (0.25 of the most, 0x827723) and not above it.
int ArmorDrawn(const wchar_t* scene,bool want,bool hull) {
    wchar_t armor[128],hullText[128];
    std::swprintf(armor,128,Tr(Tx::armorRow),static_cast<int>(std::lround(sceneArmor)),static_cast<int>(std::lround(sceneArmorMost)));
    std::swprintf(hullText,128,Tr(Tx::hullRow),static_cast<int>(std::lround(sceneHull)),static_cast<int>(std::lround(sceneHullMost)));
    int failed=0,found=0,hulls=0;
    for(std::size_t i=0;i<drew.size();++i) {
        const bool isArmor=drew[i].text==armor,isHull=drew[i].text==hullText;
        found+=isArmor;hulls+=isHull;
        if(!isArmor && !isHull)continue;
        const Drew& a=drew[i];
        for(std::size_t k=0;k<drew.size();++k) {
            const Drew& b=drew[k];
            if(k==i || b.text.find_first_not_of(L' ')==std::wstring::npos)continue;
            if(a.x0<b.x1 && b.x0<a.x1 && a.y0<b.y1 && b.y0<a.y1) {
                ++failed;std::printf("FAIL armor %ls: \"%s\" overlaps \"%s\"\n",scene,Narrow(a.text).c_str(),Narrow(b.text).c_str());
            }
        }
    }
    if(found!=(want ? 1 : 0)){++failed;std::printf("FAIL armor %ls: the armor line drawn %d times (want %d)\n",scene,found,want ? 1 : 0);}
    if(hulls!=(want && hull ? 1 : 0)){++failed;std::printf("FAIL armor %ls: the hull line drawn %d times\n",scene,hulls);}
    std::printf("%s  armor %s %ls: %s\n",failed ? "FAIL" : "ok",hudtext::Name(hudtext::InUse()),scene,want ? (hull ? "armor + hull, apart" : "armor in the block, apart") : "not drawn");
    return failed;
}

// The cockpit's screens (cockpit_screens.inc): a tank gunner's status and weapons screens and a heli pilot's flight and
// radar screens, each 0.22 x 0.13 m, 0.6 m ahead of the eye and 0.25 m under it, one either side, facing it. Each must
// get its text, all of it inside the screen's quad on the view; the HUD's own panels (the stock block, the strips) must
// give way.
CockpitScreen ScreenAt(ScreenKind kind,const float* fwd,float side) {
    float f[3]={fwd[0],fwd[1],fwd[2]};
    const float n=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    for(float& v:f)v/=n;
    const float right[3]={-f[2],0.0f,f[0]};   // fwd x up (game +x is left): the viewer's right, level
    const float rn=std::sqrt(right[0]*right[0]+right[2]*right[2]);
    CockpitScreen c{};
    c.kind=kind;c.w=0.22f;c.h=0.13f;
    for(int k=0;k<3;++k)c.right[k]=right[k]/rn;
    c.up[0]=c.right[1]*f[2]-c.right[2]*f[1];c.up[1]=c.right[2]*f[0]-c.right[0]*f[2];c.up[2]=c.right[0]*f[1]-c.right[1]*f[0];
    for(int k=0;k<3;++k)c.off[k]=f[k]*0.6f-c.up[k]*0.25f+c.right[k]*side;
    return c;
}

int ScreensDrawn(const wchar_t* name,const float* pos,const float* fwd,int width) {
    int failed=0,inside=0;
    float vp[16],eye[3],dir[3];
    Camera(pos,fwd,static_cast<float>(width),1080.0f,vp);
    if(!RayOf(vp,eye,dir)){std::printf("FAIL %ls: no eye in the view\n",name);return 1;}   // as the draw finds it
    for(int i=0;i<sceneScreens.n;++i) {
        const CockpitScreen& c=sceneScreens.s[i];
        float lo[2]={1e9f,1e9f},hi[2]={-1e9f,-1e9f};
        for(int k=0;k<4;++k) {
            const float sx=k==1 || k==2 ? 0.5f : -0.5f,sy=k<2 ? 0.5f : -0.5f;
            float p[3],q[4];
            for(int a=0;a<3;++a)p[a]=eye[a]+c.off[a]+c.right[a]*c.w*sx+c.up[a]*c.h*sy;
            for(int a=0;a<4;++a)q[a]=p[0]*vp[a]+p[1]*vp[4+a]+p[2]*vp[8+a]+vp[12+a];
            const float x=width*0.5f*(1.0f+q[0]/q[3]),y=540.0f*(1.0f-q[1]/q[3]);
            lo[0]=std::fmin(lo[0],x);lo[1]=std::fmin(lo[1],y);hi[0]=std::fmax(hi[0],x);hi[1]=std::fmax(hi[1],y);
        }
        int texts=0;
        for(const Drew& d:drew) {
            if(d.text.empty())continue;
            const float cx=(d.x0+d.x1)*0.5f,cy=(d.y0+d.y1)*0.5f;
            if(cx<lo[0] || cx>hi[0] || cy<lo[1] || cy>hi[1])continue;
            ++texts;
            if(d.x0<lo[0]-2.0f || d.x1>hi[0]+2.0f || d.y0<lo[1]-2.0f || d.y1>hi[1]+2.0f) {
                ++failed;std::printf("FAIL %ls: screen %d's text \"%s\" spills out of it\n",name,i,Narrow(d.text).c_str());
            }
        }
        if(texts<2){++failed;std::printf("FAIL %ls: screen %d shows %d lines\n",name,i,texts);}
        inside+=texts;
    }
    std::printf("%s  %ls: %d screens, %d lines on them\n",failed ? "FAIL" : "ok",name,sceneScreens.n,inside);
    return failed;
}

// A physical cockpit's head-up glass (cockpit_screens.inc HeadUpGlass; docs/feedback-2026-10-21-attitude-indicator.md):
// a heli flown nose 3 degrees up and banked 20 degrees right, its glass (an F-16 combiner's size, 0.14 x 0.15 m, 0.8 m
// ahead along the nose) turned with the body; the scene's camera is level. Through the glass the drawn horizon must lie
// on the world's: on the view it is level and on the view's middle row (a level camera's horizon), though the glass is
// turned 20 degrees; the glass's other symbols (the boxes' numbers) are drawn too; a model's own screens (physical)
// get no canvas of their own.
int HeadUpGlassScene(const std::wstring& dir,const float* pos) {
    int failed=0;
    const float p=3.0f*kDeg,b=20.0f*kDeg;
    const float nose[3]={0.0f,std::sin(p),std::cos(p)},level[3]={-1.0f,0.0f,0.0f},u0[3]={0.0f,std::cos(p),-std::sin(p)};
    std::memcpy(sceneHeli.sym.nose,nose,12);
    CockpitScreen g{};
    g.kind=ScreenKind::hud;g.w=0.14f;g.h=0.15f;
    for(int k=0;k<3;++k) {
        g.right[k]=std::cos(b)*level[k]-std::sin(b)*u0[k];g.up[k]=std::sin(b)*level[k]+std::cos(b)*u0[k];g.off[k]=nose[k]*0.8f;
    }
    sceneScreens=CockpitScreens{};
    sceneScreens.s[sceneScreens.n++]=g;
    sceneScreens.s[sceneScreens.n++]=ScreenAt(ScreenKind::flight,nose,-0.13f);
    sceneScreens.physical=true;
    for(int k=0;k<3;++k){sceneScreens.frame[k]=-g.right[k];sceneScreens.frame[4+k]=g.up[k];sceneScreens.frame[8+k]=nose[k];}
    sceneScreens.frame[15]=1.0f;
    hudQuads.clear();keepHud=true;
    Prime(pos);Scene(dir,L"cockpit_hud_glass",pos);
    keepHud=false;
    // The ladder's pieces: the HUD quads at least 30 px long that lie level on the view (a Seg: corners a0 b0 a1 b1,
    // its long edges a0-b0 and a1-b1). The horizon is the one on the view's middle row; the glass-fixed symbols (the
    // tape, the boxes, the bank scale) are turned 20 degrees with the glass and are not among them.
    int pieces=0;float cy=1e9f;
    for(const auto& q:hudQuads) {
        const float len=std::hypot(q[2]-q[0],q[3]-q[1]);
        if(len<30.0f || std::fabs(q[3]-q[1])>0.02f*len)continue;
        ++pieces;
        const float row=(q[1]+q[3]+q[5]+q[7])*0.25f;
        if(std::fabs(row-540.0f)<std::fabs(cy-540.0f))cy=row;
    }
    if(pieces<2 || std::fabs(cy-540.0f)>3.0f) {
        ++failed;
        std::printf("FAIL cockpit_hud_glass: the drawn horizon is not the world's (%d level pieces, the nearest at row %.1f)\n",pieces,cy);
    }
    int numbers=0;
    for(const Drew& d:drew)numbers+=!d.text.empty() && (d.text==L"151" || d.text==L"85");
    if(numbers<2){++failed;std::printf("FAIL cockpit_hud_glass: the speed and height boxes show %d of 2\n",numbers);}
    if(!failed)std::printf("ok  cockpit_hud_glass: the horizon level on the world's (row %.1f, %d level pieces), speed and height on the glass\n",cy,pieces);
    sceneScreens=CockpitScreens{};
    sceneHeli.sym.nose[0]=sceneHeli.sym.nose[1]=0.0f;sceneHeli.sym.nose[2]=1.0f;
    return failed;
}

int CockpitScenes(const std::wstring& dir,const float* ground,const float* pos) {
    int failed=0;
    hasJet=hasHeli=hasWarn=hasSazabi=hasDrill=hasNix=hasProteus=false;hasStock=true;
    StockTank(ground);
    hasScreens=true;
    sceneScreens=CockpitScreens{};
    sceneScreens.s[sceneScreens.n++]=ScreenAt(ScreenKind::weapons,sceneStock.hull,-0.13f);
    sceneScreens.s[sceneScreens.n++]=ScreenAt(ScreenKind::status,sceneStock.hull,0.13f);
    Prime(ground);Scene(dir,L"cockpit_tank_gunner",ground);
    failed+=ScreensDrawn(L"cockpit_tank_gunner",ground,sceneStock.hull,1920);
    hasStock=false;hasHeli=true;hasWarn=false;
    sceneHeli=PlayerHeliReadout{};
    sceneHeli.sym.nose[2]=1.0f;std::memcpy(sceneHeli.sym.pos,pos,12);
    sceneHeli.f.speed=42.0f;sceneHeli.f.clear=85.0f;sceneHeli.f.climb=2.5f;sceneHeli.f.ground=true;
    sceneHeli.f.hp=900.0f;sceneHeli.f.hpMax=1200.0f;
    sceneHeli.fuel.ok=true;sceneHeli.fuel.share=0.6f;sceneHeli.fuel.sec=310.0f;
    sceneScreens=CockpitScreens{};
    sceneScreens.s[sceneScreens.n++]=ScreenAt(ScreenKind::flight,sceneHeli.sym.nose,-0.13f);
    sceneScreens.s[sceneScreens.n++]=ScreenAt(ScreenKind::map,sceneHeli.sym.nose,0.13f);
    Prime(pos);Scene(dir,L"cockpit_heli_pilot",pos);
    failed+=ScreensDrawn(L"cockpit_heli_pilot",pos,sceneHeli.sym.nose,1920);
    failed+=HeadUpGlassScene(dir,pos);
    // An unmanned gun's station (remote_display): its monitor over the whole view, its title, bearing, elevation, weapon.
    hasHeli=false;hasStock=true;
    StockTank(ground);
    sceneScreens=CockpitScreens{};
    sceneScreens.s[sceneScreens.n].kind=ScreenKind::sensor;sceneScreens.s[sceneScreens.n].w=0.4f;sceneScreens.s[sceneScreens.n++].h=0.3f;
    Prime(ground);Scene(dir,L"cockpit_remote_station",ground);
    int rws=0;
    for(const Drew& d:drew)rws+=d.text==Tr(Tx::rwsTitle) || d.text.rfind(L"AZ ",0)==0 || d.text.rfind(L"EL ",0)==0 ||
                                d.text.find(L"CANNON")!=std::wstring::npos;
    if(hudtext::InUse()==hudtext::Lang::en && rws<4){++failed;std::printf("FAIL cockpit_remote_station: %d of its 4 read-outs\n",rws);}
    else std::printf("ok  cockpit_remote_station: the station's monitor and its read-outs\n");
    hasScreens=false;hasHeli=false;hasWarn=false;
    return failed;
}

int ArmorScenes(const std::wstring& dir,const float* ground,const float* pos) {
    int failed=0;
    // The low mark as the stock gauge sets it (+0xB01 at or under 0.25), and the colours that follow it.
    armorhud::Durability d{};
    Line l{};
    failed+=!(armorhud::Read(400.0f,1600.0f,&d) && d.low);
    DurabilityLine(l,Tx::armorRow,d,nullptr);failed+=l.rgba!=kRed || DurabilityFill(d,true)!=kRed;
    failed+=!(armorhud::Read(401.0f,1600.0f,&d) && !d.low);
    DurabilityLine(l,Tx::armorRow,d,nullptr);failed+=l.rgba!=kHud || DurabilityFill(d,true)!=kHud;
    failed+=armorhud::Read(100.0f,0.0f,&d) || armorhud::Read(std::nanf(""),1600.0f,&d);   // no number made up
    armorReads=true;
    // A stock tank: its block (StockVehicleHud), the armor under the hull's bar.
    hasJet=hasHeli=hasWarn=hasSazabi=false;hasStock=true;
    StockTank(ground);
    for(int w:{1920,2520,1280,960}) {
        wchar_t name[64];std::swprintf(name,64,L"armor_stock_tank_%d",w);
        Prime(ground,w);Scene(dir,name,ground,w);failed+=ArmorDrawn(name,true,false);
    }
    sceneArmor=380.0f;Scene(dir,L"armor_stock_tank_low",ground);failed+=ArmorDrawn(L"armor_stock_tank_low",true,false);sceneArmor=1350.0f;
    config.hideStockGauges=false;Scene(dir,L"armor_stock_tank_gauge_kept",ground);failed+=ArmorDrawn(L"armor_stock_tank_gauge_kept",false,false);
    config.hideStockGauges=true;
    armorReads=false;Scene(dir,L"armor_unread",ground);failed+=ArmorDrawn(L"armor_unread",false,false);armorReads=true;
    // On foot: nothing covered, nothing drawn.
    hasStock=false;Scene(dir,L"armor_on_foot",ground);failed+=ArmorDrawn(L"armor_on_foot",false,false);
    // A jet and a rotor craft (no durability of their own on our HUD): the panel, with the hull's line.
    hasJet=true;sceneJet.rotor=false;sceneJet.hasImpact=false;config.playerJetFlightHud=true;
    for(int w:{1920,960}) {
        wchar_t name[64];std::swprintf(name,64,L"armor_jet_%d",w);
        Prime(pos,w);Scene(dir,name,pos,w);failed+=ArmorDrawn(name,true,true);
    }
    sceneJet.rotor=true;config.heliFlightHud=true;
    Scene(dir,L"armor_rotor",pos);failed+=ArmorDrawn(L"armor_rotor",true,true);
    // A stock heli with its stores on the helicopter HUD's strip.
    hasJet=false;hasHeli=true;hasStock=true;
    StockTank(pos);sceneStock.heli=true;sceneStock.arms=1;sceneStock.threats=0;strcpy_s(sceneStock.arm[0].label,"MSL");
    Scene(dir,L"armor_stock_heli",pos);failed+=ArmorDrawn(L"armor_stock_heli",true,true);
    hasHeli=hasStock=false;
    // The Sazabi's own HUD (as SazabiScenes left it).
    hasSazabi=true;
    for(int w:{1920,1280}) {
        wchar_t name[64];std::swprintf(name,64,L"armor_sazabi_%d",w);
        Scene(dir,name,ground,w);failed+=ArmorDrawn(name,true,true);
    }
    hasSazabi=false;armorReads=false;
    return failed;
}

int SazabiScenes(const std::wstring& dir,const float* pos) {
    hasSazabi=true;
    const float noseWas[3]={sceneHeli.sym.nose[0],sceneHeli.sym.nose[1],sceneHeli.sym.nose[2]};
    sceneHeli.sym.nose[0]=0.0f;sceneHeli.sym.nose[1]=0.0f;sceneHeli.sym.nose[2]=1.0f;   // Scene's camera: heading +z
    SazabiCue& c=sceneSazabi;
    SazabiCueAt(pos);                                   // on its feet, everything ready, the missiles selected
    Scene(dir,L"sazabi_idle",pos);
    Scene(dir,L"sazabi_idle_219",pos,2520);
    Scene(dir,L"sazabi_idle_narrow",pos,1280);
    SazabiSceneAt(dir,L"sazabi_idle_1280x720",pos,1280,720,1280,720,1.0f);
    SazabiSceneAt(dir,L"sazabi_idle_3840x2160",pos,3840,2160,3840,2160,1.0f);
    SazabiSceneAt(dir,L"sazabi_idle_split_lr",pos,3840,2160,1920,2160,1.0f);
    SazabiSceneAt(dir,L"sazabi_idle_split_tb",pos,1920,1080,1920,540,1.0f);
    SazabiSceneAt(dir,L"sazabi_idle_hudscale15",pos,1920,1080,1920,1080,1.5f);
    c.aimHit=false;                                     // the centre's ray meets nothing within reach
    Scene(dir,L"sazabi_no_hit",pos);
    SazabiCueAt(pos);
    c.centred=false;                                    // not its own camera: the reticle where the aim projects
    Scene(dir,L"sazabi_not_centred",pos);
    SazabiCueAt(pos);                                   // boosting up through 86 m
    c.air=c.boosting=true;c.altitude=86.0f;c.speed=41.0f;c.climb=12.0f;c.thruster=0.64f;
    Scene(dir,L"sazabi_boost_air",pos);
    SazabiCueAt(pos);                                   // low on thrust, falling
    c.air=true;c.altitude=35.0f;c.speed=18.0f;c.climb=-8.0f;c.thruster=0.18f;c.rifleReady=0.4f;
    Scene(dir,L"sazabi_low_thruster",pos);
    SazabiCueAt(pos);                                   // spent: no boost until it lands
    c.air=c.overheat=true;c.altitude=40.0f;c.speed=22.0f;c.climb=-9.0f;c.thruster=0.0f;
    Scene(dir,L"sazabi_overheat",pos);
    SazabiCueAt(pos);                                   // the missiles locking, then locked
    c.hasLock=true;c.missileLock=0.45f;c.missiles=3;c.missileReady=0.5f;
    Scene(dir,L"sazabi_missiles_locking",pos);
    c.missileLock=1.0f;c.guard=true;
    Scene(dir,L"sazabi_missiles_locked",pos);
    Scene(dir,L"sazabi_missiles_locked_narrow",pos,1280);
    SazabiCueAt(pos);                                   // the funnels: four out
    c.special=1;c.funnelsOut=4;c.funnelReady=0.3f;c.swinging=true;
    Scene(dir,L"sazabi_funnels_out",pos);
    SazabiCueAt(pos);                                   // the cannon charging behind the shield, then cooling
    c.special=2;c.cannonCharge=0.62f;c.guard=true;c.missileReady=0.3f;
    Scene(dir,L"sazabi_cannon_charging",pos);
    c.cannonCharge=0.0f;c.cannonReady=0.35f;c.guard=false;
    Scene(dir,L"sazabi_cannon_cooldown",pos);
    SazabiState(1,pos);                                 // the widest: every tag lit, locked, overheated, the cannon charging
    c.missiles=0;c.missileReady=0.0f;c.funnelsOut=6;
    Scene(dir,L"sazabi_everything",pos);
    SazabiSceneAt(dir,L"sazabi_everything_hudscale15",pos,3840,2160,3840,2160,1.5f);
    SazabiSceneAt(dir,L"sazabi_everything_split_lr",pos,3840,2160,1920,2160,1.0f);
    const int failed=SazabiLayoutChecks(pos);
    std::memcpy(sceneHeli.sym.nose,noseWas,12);
    hasSazabi=false;
    return failed;
}
// The actual complete draw, not isolated panels: long translated stores with
// all controls and an active switch banner in the same frame.
int FullLoadoutScenes(const std::wstring& dir) {
    int failed=0;
    const float pos[3]={0,120,0};
    struct Case { int w,h;float scale; };
    const Case cases[]={{1920,1080,1},{1440,1080,1.5f},{1024,768,1.5f},{960,1080,2.0f},{1920,1080,3.0f}};
    hasJet=hasHeli=hasWarn=hasStock=hasDrill=hasNix=hasMap=hasEmc=hasProteus=hasSazabi=hasTurret=false;
    hasLauncher=hasHeliSight=hasHighView=hasGunner=false;
    sceneFormation=-1;sceneSweepOn=false;sceneMarkOn=false;sceneRescueOn=false;sceneVersionOn=false;
    for(int heli=0;heli<2;++heli)for(const Case& c:cases) {
        hasStock=true;hasHeli=heli!=0;StockTank(pos);sceneStock.heli=hasHeli;
        sceneStock.arms=kStockArms;sceneStock.selected=0;sceneStock.sight=-1;
        sceneStock.aimOk=false;sceneStock.threats=1;sceneStock.threatKind[0]=1;
        sceneStock.threatAt[0][0]=500;sceneStock.threatAt[0][1]=120;sceneStock.threatAt[0][2]=500;
        for(int i=0;i<kStockArms;++i) {
            sceneStock.arm[i]=StockArm{};sceneStock.arm[i].kind=RoundKind::homing;
            std::swprintf(sceneStock.arm[i].name,_countof(sceneStock.arm[i].name),L"反装甲多目标制导导弹超长挂载名称%d",i+1);
            strcpy_s(sceneStock.arm[i].label,"MSL");sceneStock.arm[i].ammo=24+i;sceneStock.arm[i].ammoMax=48;
        }
        sceneHeli=PlayerHeliReadout{};sceneHeli.f.keys=true;sceneHeli.f.aiming=true;sceneHeli.f.holding=true;
        sceneHeli.f.speed=20;sceneHeli.f.setSpeed=20;sceneHeli.f.clear=120;sceneHeli.f.ground=true;
        Symbols(sceneHeli.sym,pos,0,0);sceneHeli.sym.moving=true;
        config.hudScale=c.scale;SetScreen(c.w,c.h);
        float vp[16];Camera(pos,sceneStock.hull,static_cast<float>(c.w),static_cast<float>(c.h),vp);
        struct { std::int32_t x,y,w,h; } viewport{0,0,c.w,c.h};
        HudPublish();HudDraw(vp,image,&viewport,nullptr,0);sceneStock.selected=7;HudPublish();
        wchar_t name[96];std::swprintf(name,_countof(name),L"full_loadout_%ls_%dx%d_scale%.1f",heli ? L"heli" : L"ground",c.w,c.h,c.scale);
        const std::wstring path=dir+L"\\"+name+L".txt";
        _wfopen_s(&out,path.c_str(),L"w");if(!out){++failed;continue;}
        std::fprintf(out,"W %d %d\n",c.w,c.h);
        const int renderers=activeTextRenderers;
        Record(name,[&]{HudDraw(vp,image,&viewport,nullptr,0);},c.w,c.h);
        if(activeTextRenderers!=renderers){++failed;std::printf("FAIL full loadout renderer lifecycle: %ls\n",name);}
        std::fclose(out);out=nullptr;
        int stores=0,overlaps=0;
        for(std::size_t i=0;i<drew.size();++i) {
            const Drew& a=drew[i];if(a.text.empty())continue;
            if(a.text.find(L"反装甲")!=std::wstring::npos)++stores;
            for(std::size_t j=i+1;j<drew.size();++j) {
                const Drew& b=drew[j];if(b.text.empty())continue;
                if(a.x0<b.x1-1 && b.x0<a.x1-1 && a.y0<b.y1-1 && b.y0<a.y1-1) {
                    ++overlaps;std::printf("FAIL full loadout %ls: %s / %s\n",name,Narrow(a.text).c_str(),Narrow(b.text).c_str());
                }
            }
        }
        if(!heli) {
            int status=0;for(const Drew& d:drew)if(d.text==L"403_Tank")++status;
            if(status!=1){++failed;std::printf("FAIL full loadout status hidden by banner: %ls\n",name);}
        }
        Line expected{};AircraftControls(expected,true,kStockArms,0x10,0,false,false);
        int controls=0;for(const Drew& d:drew)if(d.text==expected.text)++controls;
        if(controls!=1 || !gaugeCovered){++failed;std::printf("FAIL full loadout controls or native ammo replacement: %ls %d %d\n",name,controls,gaugeCovered);}
        if(stores!=9){++failed;std::printf("FAIL full loadout %ls: %d store labels, expected 8 cells + banner\n",name,stores);}
        failed+=overlaps;
        std::printf("%s full loadout %ls: %d labels, %d text collisions\n",overlaps ? "FAIL" : "ok",name,stores,overlaps);
    }
    config.hudScale=1;hasStock=hasHeli=false;
    return failed;
}

// Every scene and the layout checks in the language in use (hudtext::InUse), written into `dir`: the failures.
int MouseIntentChecks() {
    const float pos[3]={0,0,0},look[3]={0,0,1};float vp[16];Camera(pos,look,1920,1080,vp);
    TurretCamReadout r{};r.decoupled=r.aimValid=true;r.physicalOnly=false;
    r.aim[0]=80;r.aim[1]=10;r.aim[2]=500;r.gun[0]=-140;r.gun[1]=10;r.gun[2]=500;
    float ax,ay;sight::ToScreen(vp,r.aim,1,1920,1080,&ax,&ay);
    const auto draw=[&]{box=Box{};boxing=true;drawn=0;TurretMark(At<void*>(image,kQuadDrawer),image,vp,1920,1080,1,r,false);boxing=false;return drawn;};
    int failed=0;
    const int visible=draw();failed+=visible!=28 || !box.any || std::fabs((box.x0+box.x1)*0.5f-ax)>0.1f || std::fabs((box.y0+box.y1)*0.5f-ay)>0.1f;
    r.gun[0]=200;draw();failed+=std::fabs((box.x0+box.x1)*0.5f-ax)>0.1f;
    r.aimValid=false;failed+=draw()!=0;r.aimValid=true;
    r.physicalOnly=true;failed+=draw()!=0;r.physicalOnly=false;
    r.high=true;failed+=draw()!=0;r.high=false;
    r.decoupled=false;failed+=draw()!=0;r.freeLook=true;failed+=draw()!=28;
    std::printf("%s mouse intent: verified command point only; independent of barrel; no invalid/fixed/high-view cursor\n",failed ? "FAIL" : "ok");
    return failed;
}

int Scenes(const std::wstring& dir) {
    int failed=AircraftBindingChecks()+MouseIntentChecks();
    hasMouseIntent=hasMountedOptic=hasModeBinding=false;
    failed+=FullLoadoutScenes(dir);
    hasTurret=hasJet=hasHeli=hasWarn=hasStock=hasDrill=hasNix=hasMap=hasEmc=hasProteus=hasSazabi=false;   // as the first run began
    sceneMarkOn=false;
    const ULONGLONG now=GetTickCount64();
    const float pos[3]={0.0f,120.0f,0.0f};

    // A jet diving at the ground with two missiles and a lock on it, just launched, stalled, the gear not down.
    hasJet=true;hasHeli=false;hasWarn=true;
    sceneJet=PlayerJetReadout{};sceneJet.storeButton=0x10;sceneJet.targetButton=0x04;
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
    const PlayerJetReadout beforeDrone=sceneJet;
    sceneJet.storeName[1]="DRONES";sceneJet.storeRole[1]=static_cast<int>(StoreRole::drone);sceneJet.store=1;sceneJet.bomb=false;
    Scene(dir,L"carrier_drone_store",pos);
    sceneJet=beforeDrone;
    Line controls{};JetControls(controls,sceneJet);failed+=AircraftControlsDrawn(controls);
    sceneJet.keys=true;
    Scene(dir,L"jet_controls_keyboard",pos);JetControls(controls,sceneJet);failed+=AircraftControlsDrawn(controls);
    const int swap=config.playerJetSwitchKey,target=config.playerJetTargetKey,flare=config.playerJetFlareKey;
    config.playerJetSwitchKey=VK_F8;config.playerJetTargetKey=VK_XBUTTON1;config.playerJetFlareKey=VK_HOME;
    Scene(dir,L"jet_controls_remapped",pos,1280);JetControls(controls,sceneJet);failed+=AircraftControlsDrawn(controls);
    config.playerJetSwitchKey=swap;config.playerJetTargetKey=target;config.playerJetFlareKey=flare;
    sceneJet.store=4;sceneJet.bomb=false;sceneJet.hasImpact=false;Prime(pos);
    boxing=true;drawn=0;Prime(pos);const int withoutImpact=drawn;
    sceneJet.hasImpact=true;sceneJet.impact[0]=0;sceneJet.impact[1]=0;sceneJet.impact[2]=500;
    drawn=0;Prime(pos);const int withImpact=drawn;boxing=false;
    const bool rocketCross=withImpact==withoutImpact+4;
    std::printf("%s  non-bomb rocket draws four impact-cross arms (%d -> %d)\n",rocketCross ? "ok" : "FAIL",withoutImpact,withImpact);
    failed+=!rocketCross;Scene(dir,L"jet_rocket_impact",pos);sceneJet.hasImpact=false;
    sceneJet.stores=3;sceneJet.store=1;

    // A rotor craft sinking onto the ground, hovering slowly (the helicopter HUD).
    sceneJet.rotor=true;config.heliFlightHud=true;
    sceneJet.heli=HeliFlight{};
    sceneJet.heli.speed=3.0f;sceneJet.heli.clear=12.0f;sceneJet.heli.climb=-9.0f;sceneJet.heli.ground=true;sceneJet.heli.vel[1]=-9.0f;
    sceneJet.heli.gpws=Gpws::pullUp;sceneJet.heli.impactIn=1.3f;sceneJet.heli.power=0.38f;   // sinking: under the hover's
    sceneJet.liftShare=0.0f;sceneJet.gpws=Gpws::pullUp;
    Symbols(sceneJet.sym,pos,0.0f,-70.0f);
    sceneJet.sym.threats=0;
    sceneWarn.on=1u<<kWarnPullUp;sceneWarn.litAt[kWarnPullUp]=now-100;
    Scene(dir,L"rotor_pullup",pos);
    sceneJet.heli.keys=true;sceneJet.heli.aiming=true;
    Scene(dir,L"rotor_controls_keyboard",pos);JetControls(controls,sceneJet);failed+=AircraftControlsDrawn(controls);

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
    AircraftControls(controls,false,3,0x10,0,false,false);failed+=AircraftControlsDrawn(controls);
    sceneHeli.f.keys=true;sceneHeli.f.aiming=true;
    Scene(dir,L"heli_stores_keyboard",pos);
    AircraftControls(controls,true,3,0x10,0,false,false);failed+=AircraftControlsDrawn(controls);
    hasStock=false;

    // The stock vehicles' HUD: a tank under a missile and a lock, at 16:9 and 21:9; the drill tank overheating; a Nix
    // with its torso twisted 40 degrees left of its legs.
    hasJet=hasHeli=hasWarn=false;hasStock=true;
    const float ground[3]={0.0f,0.0f,0.0f};
    StockTank(ground);
    Scene(dir,L"stock_tank",ground);
    Scene(dir,L"stock_tank_219",ground,2520);
    const StockHudReadout savedStock=sceneStock;
    sceneStock.arms=kStockArms;sceneStock.selected=7;
    for(int i=0;i<kStockArms;++i) {
        sceneStock.arm[i]=StockArm{};sceneStock.arm[i].kind=RoundKind::homing;
        wcscpy_s(sceneStock.arm[i].name,L"真实挂载武器名称超长显示测试导弹弹药数量与当前选择");
        strcpy_s(sceneStock.arm[i].label,"MSL");sceneStock.arm[i].ammo=24;sceneStock.arm[i].ammoMax=48;
    }
    Prime(ground);
    Scene(dir,L"ground_stores_eight",ground);
    sceneStock=savedStock;
    sceneStock.arm[0].style=WeaponStyle::beam;strcpy_s(sceneStock.arm[0].label,"BEAM");sceneStock.arm[0].ladder.ticks=0;
    sceneStock.arm[0].reticle=reticle::Style::energy;sceneStock.arm[0].shell=reticle::Shell::beam;
    sceneStock.arm[0].ranged=false;sceneStock.arm[0].physicalOnly=false;sceneStock.sight=0;
    sceneStock.arm[0].aimed=true;sceneStock.arm[0].bore[2]=1.0f;sceneStock.arm[0].hit=true;sceneStock.arm[0].range=90.0f;
    failed+=SightGun(sceneStock,false)!=0; // energy reticle does not need a bogus ballistic ladder to exist
    LoadCell energyCells[kStockArms];StockCells(sceneStock,energyCells);
    failed+=energyCells[0].icon!=hudcue::StoreIcon::energy;
    Scene(dir,L"nix_energy_weapon",ground);
    sceneStock.arm[0].style=WeaponStyle::maser;strcpy_s(sceneStock.arm[0].label,"MASER");
    Scene(dir,L"emc_atomic_ray",ground);
    sceneStock=savedStock;
    // 2026-10-12 feedback: the 601's 140 mm cannon loading its next round (1.8 s: vhud.cpp Cycle) with one shell for its
    // picture, the coaxial MG picked by the right button and the main gun still marked (its own full sight since 2026-10-14);
    // then a Nix whose seat lists its right arm's rifle first: the strip lays it out on the right (hud_cue.h SideOrder).
    {
        StockTank(ground);
        StockArm& gun=sceneStock.arm[0];
        gun.cycle=0.25f;gun.cycleSec=1.8f;gun.aimed=gun.armed=true;gun.bore[0]=-0.08f;gun.bore[2]=1.0f;
        StockArm& mg=sceneStock.arm[1];
        mg.aimed=mg.armed=mg.coFired=true;mg.bore[2]=1.0f;mg.side=-0.3f;
        StockArm& msl=sceneStock.arm[2];   // out of rounds, its magazine 60 % back in: the same bar
        msl.ammo=0;msl.reload=0.6f;msl.reloadSec=2.4f;
        sceneStock.selected=-1;sceneStock.sight=1;
        LoadCell cells[kStockArms];int picked=-1;
        const int n=StockCells(sceneStock,cells,&picked);
        failed+=n!=3 || cells[0].icon!=hudcue::StoreIcon::shell || !std::wcsstr(cells[0].text,L"1.8s");
        failed+=cells[0].load!=0.25f || cells[1].load>=0.0f || cells[2].load!=0.6f;   // loading bars: the gun, none, the magazine
        Prime(ground);
        // 2026-10-14 (「泰坦用机枪的时候，镇魂炮的瞄具就没了」): the main gun on the other trigger keeps its own full sight
        // with the machine gun picked (hud.cpp OtherTriggerSight), the machine gun fired with it none of its own.
        {   // a copy with the cannon's ladder (this scene's has none: no gun sight to keep, picked or not)
            StockHudReadout probe=sceneStock;probe.arm[0].ladder.ticks=3;
            failed+=!OtherTriggerSight(probe,0,false) || OtherTriggerSight(probe,1,false) || OtherTriggerSight(sceneStock,0,false);
            probe.arm[1].kind=RoundKind::homing;   // a missile picked: no gun reticle under its marks
            failed+=OtherTriggerSight(probe,0,false);
        }
        Scene(dir,L"stock_tank_loading_two_triggers",ground);
        sceneStock.arms=2;sceneStock.selected=0;
        for(int i=0;i<2;++i) {
            StockArm& a=sceneStock.arm[i];a=StockArm{};a.kind=RoundKind::arc;a.style=WeaponStyle::beam;a.ammo=9900;a.ammoMax=10000;
            a.reload=1.0f;a.reloadSec=-1.0f;a.canReload=true;strcpy_s(a.label,"BEAM");
        }
        wcscpy_s(sceneStock.arm[0].name,L"RIFLE (R)");sceneStock.arm[0].side=-2.1f;
        wcscpy_s(sceneStock.arm[1].name,L"RIFLE (L)");sceneStock.arm[1].side=2.2f;
        const int m=StockCells(sceneStock,cells,&picked);
        failed+=m!=2 || std::wcsncmp(cells[0].text,L"RIFLE (L)",9)!=0 || picked!=1;
        Prime(ground);
        Scene(dir,L"nix_rifles_left_right",ground);
    }
    sceneStock=savedStock;
    failed+=TankSightScenes(dir,ground);
    failed+=SightStyleScenes(dir,ground);
    failed+=GunshipSightScenes(dir,ground);
    failed+=ZoomScenes(dir,ground);
    failed+=FireControlScenes(dir,ground);
    failed+=InfantryScopeScenes(dir,ground);
    failed+=FixedFireScenes(dir,ground);
    hasDrill=true;
    sceneDrill=DrillCue{1180.0f,1200.0f,0.93f,true,true};
    strcpy_s(sceneStock.kind,"DrillTank");
    Scene(dir,L"stock_drill",ground);
    failed+=!StockLayoutApart(1920);
    failed+=!StockLayoutApart(2520);
    failed+=!PausedDrawsNothing(ground,sceneStock.hull);
    // EDF6AutoTurret's turret (the tank's gun the player is at): auto-aim on, then flipped to the lead circle (its banner).
    hasTurret=true;
    sceneTurret=edf::aimlink::TurretReadoutV1{};
    sceneTurret.mode=edf::aimlink::Mode::autoAim;sceneTurret.keys=true;sceneTurret.ownGun=true;
    sceneTurret.modeKey=0x56;sceneTurret.lockKey=0x51;sceneTurret.modeButton=0;sceneTurret.lockButton=0x04;
    Prime(ground);
    Scene(dir,L"stock_turret_auto",ground);
    sceneTurret.mode=edf::aimlink::Mode::leadCircle;
    Scene(dir,L"stock_turret_lead_switch",ground);
    failed+=!TurretLayoutApart(1920);
    failed+=!TurretLayoutApart(2520);
    hasMouseIntent=hasModeBinding=true;sceneModeBinding={true,true,'Z','V'};
    sceneMouseIntent=TurretCamReadout{};sceneMouseIntent.decoupled=sceneMouseIntent.aimValid=true;sceneMouseIntent.physicalOnly=false;
    sceneMouseIntent.aim[0]=80;sceneMouseIntent.aim[1]=10;sceneMouseIntent.aim[2]=500;
    sceneMouseIntent.gun[0]=-140;sceneMouseIntent.gun[1]=10;sceneMouseIntent.gun[2]=500;
    float cursorVp[16];Camera(ground,sceneStock.hull,1920,1080,cursorVp);
    sight::ToScreen(cursorVp,sceneMouseIntent.aim,1,1920,1080,&intentX,&intentY);
    probeIntent=true;intentQuads=0;Scene(dir,L"mouse_command_circle",ground);failed+=intentQuads!=28;
    hasMountedOptic=true;intentQuads=0;Scene(dir,L"mounted_scope_no_auto_overlay",ground);failed+=intentQuads!=0;
    for(const auto& line:drew)failed+=line.text.find(Tr(Tx::autoAimOffCircle))!=std::wstring::npos;
    probeIntent=false;hasMouseIntent=hasMountedOptic=hasModeBinding=false;
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
    // On foot with an enemy marked for the NPCs (npcai.cpp, the Q mark): the amber diamond and its distance, nothing else.
    hasStock=false;
    const bool heliWas=hasHeli;hasHeli=false;
    const float noseWas[3]={sceneHeli.sym.nose[0],sceneHeli.sym.nose[1],sceneHeli.sym.nose[2]};
    sceneHeli.sym.nose[0]=0.0f;sceneHeli.sym.nose[1]=0.0f;sceneHeli.sym.nose[2]=1.0f;
    sceneMarkOn=true;sceneMark[0]=ground[0]+25.0f;sceneMark[1]=ground[1]+6.0f;sceneMark[2]=ground[2]+180.0f;
    Scene(dir,L"npc_mark",ground);
    // The formation banner just after the key (the longest name: bounding overwatch) with the mark up: on the screen,
    // apart from the mark's text, naming the shape and the key.
    sceneFormation=static_cast<int>(npc::formation::Shape::bounding);
    Scene(dir,L"npc_formation",ground);
    {
        bool named=false,apart=true;
        for(std::size_t i=0;i<drew.size();++i) {
            const Drew& p=drew[i];
            if(p.text.find(FormationText(sceneFormation))!=std::wstring::npos && p.text.find(L"[")!=std::wstring::npos)
                named=p.x0>=0.0f && p.x1<=1920.0f && p.y1<=1080.0f;
            for(std::size_t k=i+1;k<drew.size();++k) {
                const Drew& q=drew[k];
                if(!p.text.empty() && !q.text.empty() && p.x0<q.x1 && q.x0<p.x1 && p.y0<q.y1 && q.y0<p.y1)apart=false;
            }
        }
        failed+=!(named && apart);
        std::printf("%s  npc_formation: the banner named %d, no text overlapping %d\n",named && apart ? "ok  " : "FAIL",named,apart);
    }
    // The box sweep under it: going (boxes left, in, the key to call back) and over (the count), with the formation's banner.
    for(int done=0;done<2;++done) {
        sceneSweepOn=true;sceneSweep=SweepCue{done==0,12,7,0x59};
        const wchar_t* name=done ? L"npc_sweep_done" : L"npc_sweep";
        Scene(dir,name,ground);
        bool line=false,apart=true;
        for(std::size_t i=0;i<drew.size();++i) {
            const Drew& p=drew[i];
            if(p.text.find(L"7")!=std::wstring::npos && (done || p.text.find(L"12")!=std::wstring::npos))
                line=line || (p.x0>=0.0f && p.x1<=1920.0f && p.y1<=1080.0f);
            for(std::size_t k=i+1;k<drew.size();++k) {
                const Drew& q=drew[k];
                if(!p.text.empty() && !q.text.empty() && p.x0<q.x1 && q.x0<p.x1 && p.y0<q.y1 && q.y0<p.y1)apart=false;
            }
        }
        failed+=!(line && apart);
        std::printf("%s  %ls: the sweep line %d, no text overlapping %d\n",line && apart ? "ok  " : "FAIL",name,line,apart);
    }
    // The sea rescue's banner under both (heli.cpp): why no heli came, the longest line it says.
    {
        sceneSweepOn=true;sceneSweep=SweepCue{true,12,7,0x59};sceneRescueOn=true;sceneRescue=RescueCue{};
        _snwprintf_s(sceneRescue.text,_TRUNCATE,hudtext::Tr(hudtext::Tx::rescueFailed),hudtext::Tr(hudtext::Tx::supportRescueNeedsUpdate),10);
        sceneRescue.bad=true;
        Scene(dir,L"rescue_banner",ground);
        bool line=false,apart=true;
        for(std::size_t i=0;i<drew.size();++i) {
            const Drew& p=drew[i];
            if(p.text==sceneRescue.text)line=line || (p.x0>=0.0f && p.x1<=1920.0f && p.y1<=1080.0f);
            for(std::size_t k=i+1;k<drew.size();++k) {
                const Drew& q=drew[k];
                if(!p.text.empty() && !q.text.empty() && p.x0<q.x1 && q.x0<p.x1 && p.y0<q.y1 && q.y0<p.y1)apart=false;
            }
        }
        failed+=!(line && apart);
        std::printf("%s  rescue_banner: the rescue line %d, no text overlapping %d\n",line && apart ? "ok  " : "FAIL",line,apart);
        // The room's builds differ, under it: the longest notice (older guests, every feature named).
        wchar_t features[160];
        _snwprintf_s(features,_TRUNCATE,L"%ls%ls%ls%ls%ls%ls%ls",hudtext::Tr(hudtext::Tx::versionFeatRescue),hudtext::Tr(hudtext::Tx::versionListSep),
                     hudtext::Tr(hudtext::Tx::versionFeatAir),hudtext::Tr(hudtext::Tx::versionListSep),hudtext::Tr(hudtext::Tx::versionFeatLoadout),
                     hudtext::Tr(hudtext::Tx::versionListSep),hudtext::Tr(hudtext::Tx::versionFeatCommand));
        wchar_t who[120],what[160];
        _snwprintf_s(who,_TRUNCATE,hudtext::Tr(hudtext::Tx::versionPeersOlder),2);
        _snwprintf_s(what,_TRUNCATE,hudtext::Tr(hudtext::Tx::versionUnavailable),features);
        _snwprintf_s(sceneVersion,_TRUNCATE,L"%ls\n%ls",who,what);
        sceneVersionOn=true;
        Scene(dir,L"version_banner",ground);
        bool shown=false,clear=true;
        for(std::size_t i=0;i<drew.size();++i) {
            const Drew& p=drew[i];
            const bool mine=p.text==who || p.text==what;
            if(mine && !(p.x0>=0.0f && p.x1<=1920.0f && p.y1<=1080.0f))clear=false;   // every line of it on screen
            if(p.text==what)shown=true;
            for(std::size_t k=i+1;k<drew.size();++k) {
                const Drew& q=drew[k];
                if(!p.text.empty() && !q.text.empty() && p.x0<q.x1 && q.x0<p.x1 && p.y0<q.y1 && q.y0<p.y1)clear=false;
            }
        }
        failed+=!(shown && clear);
        std::printf("%s  version_banner: the build line on screen %d, no text overlapping %d\n",shown && clear ? "ok  " : "FAIL",shown,clear);
        sceneVersionOn=false;sceneRescueOn=false;
    }
    sceneSweepOn=false;
    // The debug spawn tool's menu (DebugSpawn=1): the longest category (vehicles, its window scrolled to the middle) and
    // the enemies with a spawn's result; on the screen and no two texts on each other, the picked row's name drawn.
    for(int pass=0;pass<2;++pass) {
        using namespace debugspawn;
        const Category cat=pass ? Category::enemy : Category::vehicle;
        sceneDebugSpawnOn=true;
        sceneDebugSpawn=DebugSpawnCue{};
        sceneDebugSpawn.open=true;sceneDebugSpawn.category=static_cast<int>(cat);
        sceneDebugSpawn.count=CountIn(cat);sceneDebugSpawn.pick=pass ? 2 : 8;
        sceneDebugSpawn.row=RowOf(cat,sceneDebugSpawn.pick);sceneDebugSpawn.ready=pass==0;
        const int keys[5]={0x77,0x74,0x75,0x76,0x78};
        std::memcpy(sceneDebugSpawn.keys,keys,sizeof(keys));
        if(pass){sceneDebugSpawn.status=kDebugSpawnDone;sceneDebugSpawn.statusRow=sceneDebugSpawn.row;}
        const wchar_t* name=pass ? L"debug_spawn_enemy" : L"debug_spawn_vehicle";
        Scene(dir,name,ground);
        bool picked=false,inside=true,apart=true;
        for(std::size_t i=0;i<drew.size();++i) {
            const Drew& p=drew[i];
            if(p.text.empty())continue;
            if(p.text.find(debugspawn::kEntries[sceneDebugSpawn.row].name)!=std::wstring::npos)picked=true;
            if(p.x0<0.0f || p.x1>1920.0f || p.y0<0.0f || p.y1>1080.0f)inside=false;
            for(std::size_t k=i+1;k<drew.size();++k) {
                const Drew& q=drew[k];
                if(!q.text.empty() && p.x0<q.x1 && q.x0<p.x1 && p.y0<q.y1 && q.y0<p.y1)apart=false;
            }
        }
        failed+=!(picked && inside && apart);
        std::printf("%s  %ls: the picked row drawn %d, on the screen %d, no text overlapping %d\n",
                    picked && inside && apart ? "ok  " : "FAIL",name,picked,inside,apart);
    }
    sceneDebugSpawnOn=false;
    sceneFormation=-1;
    sceneMarkOn=false;hasHeli=heliWas;
    std::memcpy(sceneHeli.sym.nose,noseWas,12);
    // The map view (map.cpp): a medium view on keys, a high steep one on a pad, a low shallow one.
    MapScene(dir,L"map_mid",700.0f,60.0f,20.0f,false);
    MapScene(dir,L"map_high_pad",3000.0f,85.0f,-40.0f,true);
    MapScene(dir,L"map_low",200.0f,32.0f,0.0f,false);
    MapScene(dir,L"map_nine_squads",700.0f,60.0f,20.0f,false,9);
    MapScene(dir,L"map_nine_squads_narrow",700.0f,60.0f,20.0f,false,9,1280);
    MapScene(dir,L"map_nine_squads_pad",700.0f,60.0f,20.0f,true,9);
    MapScene(dir,L"map_clickable_payload",700.0f,60.0f,20.0f,false,9,1920,1.0f,true);
    MapScene(dir,L"map_clickable_payload_narrow",700.0f,60.0f,20.0f,false,9,960,1.75f,true);
    MapScene(dir,L"map_clickable_payload_4x3",700.0f,60.0f,20.0f,false,9,1440,2.0f,true);
    MapScene(dir,L"map_vehicle_only_controls",700.0f,60.0f,20.0f,false,9,1440,1.5f,true,true);
    MapScene(dir,L"map_online_controls",700.0f,60.0f,20.0f,false,9,1920,1.0f,true,false,true);
    // The test range's 「摆放」 panel (range_place.h): shut, open (mouse, a pad, a narrow screen, the payload panel up),
    // online.
    MapScene(dir,L"map_place_shut",700.0f,60.0f,20.0f,false,4,1920,1.0f,false,false,false,1);
    MapScene(dir,L"map_place",700.0f,60.0f,20.0f,false,4,1920,1.0f,false,false,false,2);
    MapScene(dir,L"map_place_pad",700.0f,60.0f,20.0f,true,4,1920,1.0f,false,false,false,2);
    MapScene(dir,L"map_place_narrow",700.0f,60.0f,20.0f,false,9,1280,1.0f,false,false,false,2);
    MapScene(dir,L"map_place_payload",700.0f,60.0f,20.0f,false,9,1920,1.0f,true,false,false,2);
    MapScene(dir,L"map_place_online",700.0f,60.0f,20.0f,false,4,1920,1.0f,false,false,true,3);
    // The legend unfolded at the dock's foot under the open 「摆放」 (the user, 2026-10-19: 「操作区域和图例冲突了」).
    sceneLegendOpen=true;
    MapScene(dir,L"map_place_legend",700.0f,60.0f,20.0f,false,4,1920,1.0f,false,false,false,2);
    MapScene(dir,L"map_place_legend_narrow",700.0f,60.0f,20.0f,false,9,1280,1.0f,true,false,false,2);
    sceneLegendOpen=false;
    StatsScenes(dir);
    StockTank(ground);
    failed+=!MapDrawsMapAlone(ground,sceneStock.hull);
    hasStock=false;
    // The Proteus: walking behind its front shield (the allies' focus up), then deployed: the field's ring, the barrier
    // taken down, the directional shield hot, a target marked, the salvo cooling down; its gunner's cannon in the list.
    StockTank(ground);
    strcpy_s(sceneStock.kind,"BigBegaruta");
    sceneStock.arms=0;sceneStock.aimOk=false;sceneStock.speed=9.0f;sceneStock.hp=9000.0f;sceneStock.hpMax=11000.0f;
    hasStock=true;   // the Proteus's lines are the stock block's (its scenes drew nothing without it)
    hasProteus=true;
    sceneProteus=ProteusReadout{};
    std::memcpy(sceneProteus.pos,ground,12);sceneProteus.hull[2]=1.0f;
    sceneProteus.driver=true;sceneProteus.keys=true;sceneProteus.mode=proteus::Mode::walk;sceneProteus.stagger=1.0f;
    sceneProteus.shieldOn=sceneProteus.shieldUp=true;sceneProteus.priority=true;sceneProteus.shieldHalfArc=60.0f*kDeg;
    sceneProteus.shield=1.0f;sceneProteus.shieldHp=3300.0f;sceneProteus.shieldReady=true;
    sceneProteus.modeKey=0x54;sceneProteus.modeButton=0x20;sceneProteus.shieldKey=0x42;sceneProteus.shieldButton=0x10;
    sceneProteus.launcherKey=0x02;
    Scene(dir,L"stock_proteus_walk",ground);
    sceneProteus.mode=proteus::Mode::deployed;sceneProteus.dirShield=true;sceneProteus.priority=false;sceneProteus.heat=0.74f;
    sceneProteus.shield=0.42f;sceneProteus.launcher=true;sceneProteus.fieldRadius=60.0f;sceneProteus.allies=5;
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
    hasStock=false;
    failed+=SazabiScenes(dir,ground);
    failed+=ArmorScenes(dir,ground,pos);
    failed+=CockpitScenes(dir,ground,pos);
    // The next language's run begins as this one did: EDF6AutoTurret's mode last seen as auto-aim, and seen long enough
    // ago that no switch's banner (hud_cue.h Changed, a wall-clock kSwitchMs) carries over into its turret scenes.
    hasStock=hasTurret=true;
    sceneTurret.mode=edf::aimlink::Mode::autoAim;
    Prime(ground);
    hasStock=hasTurret=false;
    Sleep(static_cast<DWORD>(kSwitchMs)+50);
    return failed;
}
}  // namespace

int PhysicalInstrumentFeedChecks() {
    int failed=0;
    const auto check=[&](bool ok,const char* text){if(!ok){++failed;std::printf("FAIL cockpit feed: %s\n",text);}};
    Snapshot s{};s.stock=true;s.stockHud.hp=900;s.stockHud.hpMax=1000;
    s.armor=true;s.armorRo.armor.hp=10;s.armorRo.armor.most=200;
    s.armorRo.hasHull=true;s.armorRo.hull.hp=900;s.armorRo.hull.most=1000;
    config.hudLanguage=2;PublishInstruments(s);
    check(deliveredInstruments.value[instruments::hp]==900 && std::fabs(deliveredInstruments.share[instruments::hp]-.9f)<1e-6f,
          "vehicle diagram uses vehicle durability, not rider armor");
    check(deliveredInstruments.language==1,"physical language uses the same HUD resolver");
    s.armorRo.armor.hp=1;PublishInstruments(s);
    check(deliveredInstruments.value[instruments::hp]==900,"injuring the rider cannot change vehicle health");
    s.stockHud.hp=100;s.armorRo.armor.hp=200;PublishInstruments(s);
    check(std::fabs(deliveredInstruments.share[instruments::hp]-.1f)<1e-6f,"vehicle damage changes its own diagram even with healthy rider");
    check(FeedOf(s).hp==200,"legacy armor readout retains its rider meaning");
    s.stock=false;s.cockpit=true;s.jet.hp=2000;s.jet.hpMax=10000;PublishInstruments(s);
    check(std::fabs(deliveredInstruments.share[instruments::hp]-.2f)<1e-6f,"aircraft health does not inherit stale rider/vehicle gauge values");
    s.jet.speed=100;s.jet.climb=-5;s.jet.throttle=.65f;
    s.jet.sym.nose[1]=.5f;s.jet.sym.nose[2]=.8660254f;PublishInstruments(s);
    check(deliveredInstruments.flight && deliveredInstruments.value[instruments::speed]==360
          && deliveredInstruments.value[instruments::climb]==-5,"physical gauges receive speed and negative vertical speed");
    check(std::fabs(deliveredInstruments.value[instruments::pitch]-30)<.001f
          && std::fabs(deliveredInstruments.value[instruments::power]-65)<.001f,"physical gauges use degrees and actual throttle percent");
    s.jet.rotor=true;s.jet.heli.power=.42f;PublishInstruments(s);
    check(std::fabs(deliveredInstruments.value[instruments::power]-42)<.001f,"plugin rotor craft use measured rotor power");
    s.cockpit=false;PublishInstruments(s);
    check(deliveredInstruments.value[instruments::hp]<0,"armor-only snapshot cannot invent vehicle durability");
    check(!deliveredInstruments.flight && deliveredInstruments.value[instruments::power]<0,
          "leaving the aircraft invalidates flight-only readings and power");
    s.heliFly=true;s.heliHud.f.climb=-2;s.heliHud.f.power=.35f;s.heliHud.sym.nose[2]=1;PublishInstruments(s);
    check(deliveredInstruments.flight && deliveredInstruments.value[instruments::climb]==-2
          && std::fabs(deliveredInstruments.value[instruments::power]-35)<.001f,"stock helicopters publish their own live climb and power");
    config=Config{};
    std::printf("cockpit feed: %d failures\n",failed);return failed;
}

int wmain(int argc,wchar_t** argv) {
    if(argc==2 && !wcscmp(argv[1],L"--check-feed"))return PhysicalInstrumentFeedChecks();
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH,temp);
    std::wstring dir=std::wstring(temp)+L"edf6_hud_view";
    int only=-1;   // --lang: that language (and English, which the others are compared with) alone
    for(int i=1;i+1<argc;++i) {
        if(!wcscmp(argv[i],L"--out"))dir=argv[i+1];
        if(!wcscmp(argv[i],L"--lang"))only=hudtext::ParseSetting(argv[i+1]);
    }
    CreateDirectoryW(dir.c_str(),nullptr);
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    static unsigned char fontMgr[0x200]{},drawer[16]{};
    *reinterpret_cast<void**>(image+kQuadDrawer)=drawer;
    *reinterpret_cast<void**>(image+kFontMgr)=fontMgr;
    Jump(kQuad,reinterpret_cast<const void*>(&QuadRec));
    Jump(kTextMake,reinterpret_cast<const void*>(&MakeRec));Jump(kTextBegin,reinterpret_cast<const void*>(&BeginRec));
    Jump(kTextMeasure,reinterpret_cast<const void*>(&MeasureRec));Jump(kTextDraw,reinterpret_cast<const void*>(&DrawRec));
    Jump(kTextEnd,reinterpret_cast<const void*>(&EndRec));Jump(kTextFree,reinterpret_cast<const void*>(&FreeRec));
    *reinterpret_cast<void**>(image+kUiScreen)=uiHolder;
    *reinterpret_cast<void**>(uiHolder+kUiScreenObj)=uiScreen;
    SetScreen(1920,1080);
    quadOk=textOk=uiOk=true;
    int failed=PhysicalInstrumentFeedChecks();
    for(int l=0;l<hudtext::kLangs;++l) {   // English first: the others are compared with it
        const hudtext::Lang lang=static_cast<hudtext::Lang>(l);
        if(only>1 && only!=l+1 && lang!=hudtext::Lang::en)continue;
        config.hudLanguage=l+1;   // hudtext::Setting: en, zh-CN, zh-TW, ja
        hudtext::Use(lang);
        std::wstring at=dir;
        if(lang!=hudtext::Lang::en) {
            const char* n=hudtext::Name(lang);
            at+=L"\\"+std::wstring(n,n+std::strlen(n));
            CreateDirectoryW(at.c_str(),nullptr);
        }
        failed+=Scenes(at);
    }
    std::printf(textFailed ? "text: %d FAILED\n" : "text: on the screen, inside its panels, no overlap English lacks, in every language\n",textFailed);
    std::printf("glyphs: at most %zu in one scene (%s)\n",mostGlyphs,mostGlyphsAt.c_str());
    failed+=textFailed;
    config.hudLanguage=1;hudtext::Use(hudtext::Lang::en);
    std::printf(failed ? "layout: %d FAILED\n" : "layout: all apart\n",failed);
    // The HUD's scale (src/hudscale.h).
    hasStock=true;
    const int scaled=ScaleOfChecks()+ScaleDrawnChecks();
    hasStock=false;
    std::printf(scaled ? "scale: %d FAILED\n" : "scale: all as designed\n",scaled);
    failed+=scaled;
    failed+=MapIconFitChecks();
    return failed ? 1 : 0;
}
