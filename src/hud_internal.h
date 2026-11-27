// The vehicle HUD's internal header (hud.cpp and its parts hud_text.cpp, hud_vehicle.cpp, hud_air.cpp, hud_map.cpp,
// hud_scope.cpp; tools/hud_view.cpp includes them all): what more than one of them uses. Not for any other file: the
// HUD's API is hud.h's (HudDraw, HudSee, HudPublish, ...). Everything here is in crew::hudi, which every part uses.
#pragma once
#include "sightzoom.h"
#include "dreadnought.h"
#include "drill.h"
#include "emc.h"
#include "heli.h"
#include "helisight.h"
#include "hud.h"
#include "jet.h"
#include "launchers.h"
#include "sazabi.h"
#include "turretcam_api.h"
#include "nix.h"
#include "vhud.h"
#include "payload.h"
#include "proteus.h"
#include "npcai.h"
#include "barga_aim.h"
#include "cockpit_canvas.h"
#include "firstperson.h"
#include "vehicle_look.h"
#include "formation.h"
#include "map_buttons.h"
#include "scopeview.h"
#include "scope_pip.h"
#include "reticle.h"
#include "scopereticle.h"
#include "boarding_entrance.h"
#include "debug_spawn.h"
#include "gear.h"
#include "hudscale.h"
#include "map_cam.h"
#include "map_marks.h"
#include "hud_cue.h"
#include "hudtext.h"
#include "text_fit.h"
#include "module.h"   // ModuleNotice
#include "layout.h"
#include "memory.h"
#include "player_view.h"
#include "scope_hold.h"
#include "sight.h"
#include "turretaim.h"
#include "vecmath.h"
#include "warn.h"
#include "world_text.h"
#include "drivegear.h"
#include "drive_gear.h"
#include "support_net.h"
#include "support_warning.h"
#include "pvp.h"
#include "map_pvp_input.h"
#include "thermal.h"
#include "map_enemy_ui.h"
#include "enemy_control_bindings.h"
#include "aura.h"
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace crew {
namespace hudi {}
using namespace hudi;
namespace hudi {
using hudtext::Tr;
using hudtext::Tx;
// The quad (docs/hud-re.md §1): (drawer, ctx, row-major 4x4 transform, RGBA, topology, xyz vertices, count, texture).
constexpr unsigned kQuad=0xC2FB0;
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

// The world text (docs/world-text-re.md §5, src/world_text.h) leans on the text renderer's Core (*(renderer+0x18)) keeping
// its projection at +0x2F0 and its view at +0x330, begin writing both and draw reading them, and draw writing the system
// constants unless the font's +0x30 has bit 0 (§1.1, §1.2, §5.2). Any of these off: 2D labels only.
constexpr std::size_t kRendererCore=0x18,kCoreProjection=0x2F0,kCoreView=0x330,kFontOwnFlags=0x30;

inline bool quadOk=false,textOk=false,uiOk=false,worldTextOk=false;

// --- What the game thread gathers (HudSee) and publishes (HudPublish) ---
// A vehicle's readout is listed while HudSee saw it in the last steps (step_fresh.h: its input runs every step; one it
// skipped is no longer NPC-driven, in range or there), and a snapshot is drawn while HudPublish published it in the last
// steps: neither is a wall-clock window any more, so a slow frame (stress: 204 ms) never drops them.
constexpr int kEntries=48,kMaxShown=12;
struct Data {
    const void* key;              // the vehicle (its trail's key; never read through)
    float pos[3],top;             // where it is, and how far over it its readout stands
    float hp,hpMax,fuel;          // fuel: seconds left, <0 none
    std::int32_t guns,missiles,drones;   // <0: it has none of them
    bool leaving;
    char kind[16];
};
// The published frames: `back` is the game thread's to fill, `front` the draw thread's to read, the third
// waits in `middle` (its index, kFresh while the draw has not taken it).
struct Snapshot { stepfresh::Step step; float me[3]; int count; Data d[kEntries]; bool cockpit; PlayerJetReadout jet; bool heli; HeliCue heliCue;
                  bool drill; DrillCue drillCue; bool emc; EmcCue emcCue;
                  bool launcher; LauncherReadout launch; bool barga; bargaaim::Readout bargaAim; bool heliSight; HeliSightReadout heliAim;
                  bool gunner; GunnerReadout gun; bool highCam,highCamOn,highCamKeys; bool heliFly; PlayerHeliReadout heliHud;
                  bool turret; edf::aimlink::TurretReadoutV1 turretAim;
                  bool mountedOptic;
                  bool stock; StockHudReadout stockHud;
                  bool payload; PayloadReadout payloadHud;
                  bool sensor; VehicleSensorReadout sensorHud;
                  bool warned; Warnings warn;
                  bool seats; SeatPrompt seatPrompt;
                    bool lookHint,lookKeys;
                  bool entrance; BoardingEntrance boardingEntrance;
                  bool cmdHint; CommandHint commandHint;   // the first-time "press M to command" (npcai.cpp, ride_along.h)
                  bool turretCamOk; TurretCamReadout turretCam;
                  bool nix; NixTorso nixTorso;
                  bool proteus; ProteusReadout proteusRo;
                  bool sazabi; SazabiCue sazabiCue;
                  bool dreadnought; DreadnoughtCue dnCue;
                  bool armor; armorhud::Readout armorRo;
                  bool screens; CockpitScreens scr; };   // a vehicle's first person: its cockpit's screens (cockpit_screens.inc)
inline unsigned back=0;    // game thread

// --- Drawing ---
alignas(16) const float kWhite[4]={1.0f,1.0f,1.0f,1.0f};
alignas(16) const float kWarn[4]={1.0f,0.65f,0.2f,1.0f};
alignas(16) const float kBarBack[4]={0.04f,0.05f,0.06f,0.7f};
alignas(16) const float kBarEdge[4]={0.0f,0.0f,0.0f,0.85f};
alignas(16) const float kBarTick[4]={0.0f,0.0f,0.0f,0.4f};
alignas(16) const float kBarShine[4]={1.0f,1.0f,1.0f,0.18f};
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

// A cockpit screen being drawn (cockpit_screens.inc): every quad's vertices go through the homography from its canvas
// onto the screen's place on the view (cockpit_canvas.h; draw thread only, on while CockpitScreens lays a screen out).
inline canvas::Map screenCanvas{};
void Emit(void* drawer,void* ctx,const float* m,const float* rgba,const float* v) noexcept;
void Rect(void* drawer,void* ctx,float x0,float y0,float x1,float y1,const float* rgba) noexcept;
float Unit(float v) noexcept;
void Bar(void* drawer,void* ctx,float x,float y,float w,float h,float share,float trail,const float* fill,float s) noexcept;
float TrailOf(const void* key,float share,ULONGLONG now) noexcept;
float DepthScale(float depth,float readTo) noexcept;

// A line standing in the world (WorldLabel, src/world_text.h): its plate's orientation, the point its text is laid out
// from (its height's middle, `align` as Label's along the plate), its size in glyph pixels at its font scale (`fd`, on the
// glyph grid), the metres a glyph pixel, its shimmer's phase and its anchor's depth (the far ones drawn first, with fewer
// layers), whether the camera is behind it (held near its edge: world_text.h BehindHeld) and its key (WorldLabel's).
// Off: a 2D line.
struct WorldAt { bool on,back; worldtext::Standee plate; float at[3],w,h,mpp,fd,phase,depth; int align; std::uint64_t key; };
// A line of text to draw once the quads are down: where, its size (measured; a world line's on the screen), its scale and
// colour, and whether it stands in the world.
constexpr int kLineChars=128;   // a line's text, its terminator counted (TextWritten logs a longer one cut)
struct Line { wchar_t text[kLineChars]; float x,y,w,h,scale; const float* rgba; WorldAt world; };
constexpr int kMaxLines=256;   // the map's panels, card, support bar (a name and a price a row), the test range's place panel and tooltip on top of its grid labels
// The frame's lines (HudDraw's lines[kMaxLines], `*at` taken so far) as a panel takes them: Room before a panel lays its
// lines out, Push for one line. Full: the panel (or the line) is left out and the overflow logged, once a run (which
// panel, how many it wanted), never dropped in silence. `who`: the panel (__func__).
struct LineSink {
    Line* lines;
    int* at;
    int Free() const noexcept { return kMaxLines-*at; }
    bool Room(int n,const char* who) const noexcept;
    Line* Push(const char* who) const noexcept;   // the next line (its text empty, a 2D one); nullptr: full
};
// A line's text written by _vsnwprintf_s(..., _TRUNCATE): `written` its result (-1: cut at the line's 127 characters),
// the cut logged once a run with the text's start.
void TextWritten(int written,const wchar_t* text) noexcept;
void Format(Line& l,const wchar_t* format,...) noexcept;
void Append(Line& l,const wchar_t* format,...) noexcept;

// The text renderer for one draw (the rescue message's sequence, 0x808410).
struct Text {
    void* ctx;
    unsigned char* mgr;
    unsigned char* renderer;      // kRendererSize bytes, 16-aligned (the caller's stack)
    unsigned char* font;          // kFontSize bytes, 16-aligned
    bool made;                    // the renderer made (on the first line measured: none when nothing is shown)
    bool begun;                   // between a Begin and its End: a fault in between still owes the End
    float s=1.0f;                 // the HUD's scale (hudscale.h): every line's font scale is times it
    const float* vp=nullptr;      // the camera's viewProj and the viewport: the world lines' (WorldLabel, DrawWorld)
    float width=0.0f,height=0.0f;
};
void Draw(Text& t,const Line& l) noexcept;
bool MakeText(Text& t) noexcept;
void MeasureAll(Text& t,Line* lines,int n) noexcept;
// A measured line into [left, right] (text_fit.h's order, by the game's own measure): `shrink` smaller as far as
// textfit::kMinScale of its scale (the font's quantized sizes remeasured), then cut with "..." at the longest start
// that fits (a binary search, a measure a step; never a surrogate pair split, no space before the dots). A HUD line is
// one row: a text that must wrap is laid by its caller as two lines (MapScreen's band). Kept inside, its centre height.
void FitLine(Text* text,Line& row,float left,float right,bool shrink=true) noexcept;
void DrawAll(Text& t,const Line* lines,int n) noexcept;
void FreeText(Text& t) noexcept;
bool Project(const float* vp,const float* p,float width,float height,float* sx,float* sy,float* depth) noexcept;
const float* HpColour(float share) noexcept;
int Readouts(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int at,
             const Snapshot& snap,int* shown,ULONGLONG now) noexcept;
float Panel(void* drawer,void* ctx,Text* text,const CarrierPanel& p,int index,int count,float width,float top,float s,
            Line* lines,int* at) noexcept;
void KeyName(int vk,wchar_t* out,int size) noexcept;
void SeatButtonName(int mask,wchar_t* out,int size) noexcept;
void SeatChordName(int mask,wchar_t* out,int size) noexcept;
void AircraftControls(Line& line,bool keys,int choices,int storeButton,int targetButton,bool target,bool flares) noexcept;
void PadBitsName(int mask,wchar_t* key,int size) noexcept;
void PayloadControls(Line& line,const PayloadReadout& p) noexcept;
void JetControls(Line& line,const PlayerJetReadout& j) noexcept;
void ControlRow(Text* text,float width,float y,float s,Line& line) noexcept;
void AimSquare(void* drawer,void* ctx,const float* vp,float width,float height,float s,const float* at) noexcept;
void AimMarks(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetReadout& j) noexcept;
bool ImpactCross(void* drawer,void* ctx,const float* vp,float width,float height,float s,const float* at,float* sx,float* sy) noexcept;
void ImpactMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetReadout& j) noexcept;
void LauncherMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const LauncherReadout& l,
                   Line* lines,int* at) noexcept;
void HighCamHint(Text* text,float width,float height,float s,bool on,bool keys,Line* lines,int* at) noexcept;
void SeatLine(Text* text,float width,float height,const SeatPrompt& p,Line* lines,int* at) noexcept;
void TurretMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const TurretCamReadout& r,bool square) noexcept;
void GunnerMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const GunnerReadout& g,
                 Line* lines,int* at) noexcept;
void ThermalControlHint(Text* text,float width,float height,float s,const thermal::Status& status,Line* lines,int* at) noexcept;
void VehicleContacts(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,
                     const VehicleSensorReadout& sensor,const thermal::Status& thermalStatus,Line* lines,int* at) noexcept;
bool LockAt(void* drawer,void* ctx,const float* vp,float width,float height,float s,int lock,const float* p,float progress,float* sx,
            float* sy) noexcept;
void LockMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetReadout& j) noexcept;
void StoresText(wchar_t* text,std::size_t size,const PlayerJetReadout& j,bool names=true) noexcept;
void FuelText(wchar_t* out,std::size_t size,const FuelReading& f) noexcept;
void Cockpit(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetReadout& j,Line* lines,
             int* at) noexcept;

// --- The fighter HUD (the user, 2026-10-05: "the HUD and the parts a jet should have; locked on, it should show the
// direction"; ini PlayerJetGunSight / PlayerJetFlightHud / PlayerJetThreatHud): for the aircraft the player flies,
// whichever it is (the PlayerJetSymbols playerjet.cpp gathers). Drawn with the quad of the bars (as thin quads at any
// angle and as triangles) and the text of the rest, so it is there in exclusive full screen too. The horizon, the pitch
// ladder, the flight path marker and the boresight are directions (sight::ToScreen with w 0: where they vanish), so
// they lie on the view as the world does wherever the camera stands; the pipper and the lead mark are points. ---
alignas(16) const float kHud[4]={0.35f,1.0f,0.5f,0.95f};
alignas(16) const float kHudDim[4]={0.35f,1.0f,0.5f,0.45f};
constexpr float kDeg=0.0174532925f,kTurn=6.2831853f;
void Seg(void* drawer,void* ctx,float x0,float y0,float x1,float y1,float t,const float* rgba) noexcept;
void Tri(void* drawer,void* ctx,float bx,float by,float tx,float ty,float half,const float* rgba) noexcept;
void Arc(void* drawer,void* ctx,float cx,float cy,float r,float from,float span,float t,int sides,const float* rgba) noexcept;
constexpr float kBoxOff=280.0f,kBoxW=120.0f,kBoxH=34.0f,kBoxRow=30.0f;
// Central instruments end above the lower dock; the remaining 6% separates its
// measured status rows from the projected pitch geometry.
constexpr float kFlightRegionBottom=0.58f,kLoadoutRegionShare=0.36f;
void Label(Text* text,Line* lines,int* at,float x,float y,int align,float scale,const float* rgba,const wchar_t* format,...) noexcept;
// The depth (m) a world label is its 2D size at: the player's view's kViewRef, the map's camera distance (MapScreen).
inline float worldRef=worldtext::kViewRef;
inline unsigned long long worldFrame=0;   // HudDraw's frames (the facings' least-recent order)
inline bool worldMapView=false;           // this frame is the map's (MapScreen)
constexpr std::uint64_t kGroundKey=0;
enum WorldKind : unsigned { kWorldQMark=1,kWorldWarning,kWorldEntrance,kWorldCommandHint,kWorldPing,kWorldMapMarker,kWorldMapTarget,
                            kWorldMapHover,kWorldMapUnit };
std::uint64_t WorldKey(WorldKind kind,std::uint64_t of) noexcept;
void ScreenShifted(const float* vp,float width,float height,const float* anchor,float dx,float dy,float* out) noexcept;
void WorldLabel(Text* text,Line* lines,int* at,const float* vp,float width,float height,const float* anchor,std::uint64_t key,
                float dx,float dy,int align,float scale,const float* rgba,const wchar_t* format,...) noexcept;
void EntranceMark(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,
                  const BoardingEntrance& e,Line* lines,int* at) noexcept;
void CommandHintMark(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const CommandHint& h,
                     Line* lines,int* at) noexcept;

// --- The loadout strip (the user, 2026-10-06: "切换挂载应该有图片显示，而非仅文字"): every store a cell with its picture
// (StoreGlyph: a silhouette from quads, hud_cue.h; the HUD has no texture of the game's to draw), its name and rounds
// under it, the picked one on a panel in a cyan frame; on a switch the picked store large over it for kSwitchMs
// (LoadoutBanner). For the plugin's jets and rotor craft (PlayerJetReadout) and a stock heli (StockHudReadout). ---
constexpr unsigned long long kSwitchMs=1500;   // a switch's banner (the store's, EDF6AutoTurret's auto-aim's)
struct LoadCell { hudcue::StoreIcon icon; wchar_t text[64]; const float* rgba; bool picked; float load=-1.0f; };   // load: hud_cue.h CellLoad

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
struct LoadoutDock { float scale,y,footer,top,bannerBottom; };
LoadoutDock LoadoutDockOf(Text* text,float width,float height,float s,const LoadCell* cells,int n,int footers) noexcept;
float LoadoutStrip(void* drawer,void* ctx,Text* text,float width,float y,float s,const LoadCell* cells,int n,Line* lines,int* at) noexcept;
void LoadoutBanner(void* drawer,void* ctx,Text* text,float width,float bottom,float s,const LoadCell& c,Line* lines,int* at) noexcept;
int JetCells(const PlayerJetReadout& j,LoadCell* cells) noexcept;
void Ladder(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const PlayerJetSymbols& y,
            Line* lines,int* at) noexcept;

// The heading tape at the top: a tick every 5 degrees within kTapeHalf of the heading (sight::HeadingOf: a compass's,
// 0 along the world's +Z), the tens numbered, a caret under the middle and the heading under it.
constexpr float kTapeHalf=30.0f,kTapePx=6.0f;   // degrees either side; pixels a degree (at 1080 lines)
void Boresight(void* drawer,void* ctx,const float* vp,float width,float height,float s,const float* dir) noexcept;
void FcsLead(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockArm& a,const float* rgba,
             Line* lines,int* at) noexcept;
void GunSight(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetSymbols& y) noexcept;
alignas(16) const float kLit[4]={1.0f,0.16f,0.1f,0.95f};
alignas(16) const float kLed[4]={1.0f,0.42f,0.12f,1.0f};
alignas(16) const float kLedDim[4]={1.0f,0.42f,0.12f,0.55f};
void Render(void* drawer,void* ctx,Text* text,float s,const reticle::Sketch& k,Line* lines,int* at) noexcept;
float FocalAt(const float* vp,const float* bore,float width,float height,float cx,float cy) noexcept;
float CentreFocal(const float* vp,float width,float height) noexcept;
void LadderMarks(const float* vp,float width,float height,const gunsight::Ladder& l,reticle::Aim* a) noexcept;
void SeekerGate(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const float* bore,const wchar_t* line,
                Line* lines,int* at) noexcept;

constexpr float kRocketMark=11.0f;   // px at 1080 lines
void RocketDiamond(void* drawer,void* ctx,float x,float y,float s,const float* rgba) noexcept;
void PhysicalPaths(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,
                   const roundaim::Impact* paths,int count,bool label,Line* lines,int* at) noexcept;
void HeliGunSight(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const HeliSightReadout& h,
                  Line* lines,int* at) noexcept;
void FlightAim(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const float* p,Line* lines,
               int* at) noexcept;

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
void RwrScope(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetSymbols& y,ULONGLONG launchAt,
              ULONGLONG now,float side,Line* lines,int* at,float bottom=-1.0f) noexcept;
void ThreatMarks(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetSymbols& y) noexcept;
const Tx kWarnText[kWarnCount]={Tx::warnPullUp,Tx::warnMissile,Tx::warnStall,Tx::warnGear,Tx::warnTerrain,Tx::warnSinkRate,Tx::warnLock,
                                Tx::warnGearSpeed,Tx::warnWeightOnWheels,Tx::warnLowFuel,Tx::warnArea};
void Annunciator(void* drawer,void* ctx,Text* text,float width,float height,float s,const Warnings& w,Line* lines,int* at) noexcept;
void FighterHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const PlayerJetReadout& j,
                ULONGLONG launchAt,Line* lines,int* at) noexcept;
void CockpitStrip(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetReadout& j,bool switched,Line* lines,
                  int* at) noexcept;
void HeliHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const HeliFlight& f,
             const PlayerJetSymbols& y,ULONGLONG launchAt,Line* lines,int* at) noexcept;
void HeliStrip(void* drawer,void* ctx,Text* text,float width,float height,float s,const HeliFlight& f,const FuelReading& fuel,
               const wchar_t* stores,const LoadCell* cells,int n,int picked,bool switched,Line* lines,int* at,const Line* controls=nullptr) noexcept;
void GearPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,bool annunciated,Line* lines,int* at,ULONGLONG now) noexcept;
void HeliPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,const HeliCue& c,Line* lines,int* at) noexcept;
void DrillPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,const DrillCue& c,Line* lines,int* at) noexcept;
void SazabiHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const SazabiCue& c,
               Line* lines,int* at) noexcept;
void DreadnoughtHud(void* drawer,void* ctx,Text* text,float width,float height,float s,const DreadnoughtCue& c,Line* lines,
                    int* at) noexcept;
void CarrierBars(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const CarrierPanel& p,
                 Line* lines,int* at,ULONGLONG now) noexcept;
void Binding(bool keys,int key,int button,wchar_t* out,int size) noexcept;
int WorldPolyline(void* drawer,void* ctx,const float* vp,float width,float height,const float (*pts)[3],int n,float t,
                  const float* rgba) noexcept;
void WorldRing(void* drawer,void* ctx,const float* vp,float width,float height,const float* c,float radius,float t,
               const float* rgba) noexcept;
void Clip(const float* vp,const float* p,float* c) noexcept;
void BargaAimMarks(void* drawer,void* ctx,const float* vp,float width,float height,float s,const bargaaim::Readout& r) noexcept;
void TurretAimMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,
                    const edf::aimlink::TurretReadoutV1& r,Line* lines,int* at,float controlsBottom=-1.0f) noexcept;
void AutoAimBanner(void* drawer,void* ctx,Text* text,float width,float height,float s,bool on,Line* lines,int* at) noexcept;
constexpr float kIndicatorR=34.0f;   // px (1080 lines): the hull / turret indicator's ring
// The optic the player looks through this frame (HudDraw sets it before the vehicle's marks: ScopeShade's field).
struct ScopeNow { bool on; scope::Field field; float zoom; int zero; };
inline ScopeNow scopeNow{};
// Whose sight field HudDraw drew last, and when (scope_hold.h): the stock crosshair's draw (infantryscope.cpp) reads it
// from its own pass, so it is published under a lock.
inline scopehold::Drawn scopeDrawn{};
inline SRWLOCK scopeDrawnLock=SRWLOCK_INIT;
void PublishScopeDrawn(const void* soldier) noexcept;

// What the stock vehicles' HUD takes from the other readouts: the Nix's legs and torso (its ring), the drill tank's drill
// (a line in the block instead of DrillPanel), the EMC's charged beam (a line and its bar: emc.cpp), and whether
struct StockExtras { const NixTorso* nix; const DrillCue* drill; const EmcCue* emc; const ProteusReadout* proteus; bool high=false;
                     const armorhud::Readout* armor=nullptr; };   // the hidden stock armor gauge's numbers (ArmorRows)
void ProteusMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const ProteusReadout& p,Line* lines,
                  int* at) noexcept;
void StockMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockHudReadout& r,
                const StockExtras& x,Line* lines,int* at) noexcept;
void HullTurret(void* drawer,void* ctx,float cx,float cy,float s,float up,float hull,float gun,const float* stops=nullptr) noexcept;
float HeadingOfYaw(float yaw) noexcept;

// The block of ArmorRows apart (see above): where the stock gauge was, the screen's top left, the vehicle's durability
// over the player's armor.
constexpr float kArmorPanelX=48.0f,kArmorPanelY=96.0f;   // px at 1080 lines: its left and top
void ArmorPanel(void* drawer,void* ctx,Text* text,float s,const armorhud::Readout& r,Line* lines,int* at) noexcept;
int StockCells(const StockHudReadout& r,LoadCell* cells,int* picked=nullptr) noexcept;
hudcue::SeatArms SeatArmList(const Snapshot& snap) noexcept;
int SeatArmPick(const hudcue::SeatArms& a) noexcept;
void StockDockHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,
                  const StockHudReadout& r,const StockExtras& x,const PayloadReadout* payload,bool switched,Line* lines,int* at) noexcept;
}  // namespace hudi
void ControlKeyName(int vk,wchar_t* out,int size) noexcept;

// The camera of the HUD pass GaugeHook is about to draw (its player's: split screen), for HudDraw's per-player marks.
static const void* passCamera=nullptr;
inline std::uintptr_t passPlayerRefKey=0,passPlayerSoldierKey=0;
namespace hudi {
bool Invert4(const float* m,float* out) noexcept;
}  // namespace hudi
bool CameraRay(float* eye,float* dir) noexcept;
namespace hudi {
alignas(16) const float kMapBand[4]={0.0f,0.0f,0.0f,0.45f};
void HideLinesUnder(Line* lines,int prior,float x0,float y0,float x1,float y1) noexcept;

// The NPC commands (mapcmd.cpp, README 地图 → 指挥 NPC): the mouse pointer (where G sends the selection; with a pad a
// crosshair at the screen's centre) and the box being dragged from it, each commandable unit ringed (white brackets:
// selected), a guard order's line from the unit to its point (its slot of the formation) and a ring there, FOLLOW under a
// unit following the player, and a band over the keys: how many are selected, the keys, the last command's word.
alignas(16) const float kMapOrder[4]={0.3f,0.9f,1.0f,1.0f};
alignas(16) const float kMapOrderDim[4]={0.3f,0.9f,1.0f,0.55f};

// The command card (map_buttons.h; the user, 2026-10-09: "可以参考各大rts游戏"): an icon and a short word for each order
// the selection takes, the squads' tools with squads selected, the sweep and its switch; lit: the armed point order,
// the sweep going, health boxes for the hurt. The key and the full word in the button's tooltip. The rectangles drawn go
// to mapcmd.cpp (MapCommandButtons), which takes a click on one for the button. With a pad: none (its keys). The rows
// it took (Flow's), for what is laid out over it.
alignas(16) const float kBtnFill[4]={0.03f,0.05f,0.06f,0.78f};
alignas(16) const float kBtnLit[4]={0.10f,0.42f,0.50f,0.90f};
const wchar_t* EnemyAbilityState(const enemy_control::Ability& ability,wchar_t* out,std::size_t size) noexcept;
const wchar_t* EnemyHealthText(const enemy_control::Snapshot& snapshot,wchar_t* out,std::size_t size) noexcept;
const wchar_t* EnemyKeyLabel(unsigned index,bool pad,wchar_t* out,std::size_t size) noexcept;
bool MapScreen(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept;
void QMarkHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept;
void SupportWarnHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept;
void FormationBanner(Text* text,float width,float height,float s,Line* lines,int* at) noexcept;
void SweepBanner(Text* text,float width,float height,float s,Line* lines,int* at) noexcept;
void RescueCueBanner(Text* text,float width,float height,float s,Line* lines,int* at) noexcept;
void StrikeBanner(Text* text,float width,float height,float s,Line* lines,int* at) noexcept;
const Tx* DebugSpawnCategoryWords() noexcept;
}  // namespace hudi
void ScopeShade(void* drawer,void* ctx,Text* text,float width,float height,float s,float zoom,sightzoom::Mask mask,Line* lines,int* at) noexcept;
void InfantryScope(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept;
}  // namespace crew
