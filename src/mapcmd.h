// mapcmd.cpp: commanding the plugin's NPC units from the map view (README 地图 → 指挥 NPC; the user, 2026-10-06:
// "在地图上可以指挥npc", "操作 需要一个框选吧"). The map (map.cpp) calls MapCommandFrame each frame it is open; hud.cpp
// hands it the map's view (MapCommandView) and draws its readout.
// The orders themselves are the AI modules' state (heli.cpp, jet.cpp, ground.cpp): each takes a command at the one
// place it picks what it works round (its anchor / post / leader), reusing its own flight, targeting and fight.
// Included by crew.h.
#pragma once
#include <Windows.h>
#include "mapcmd_logic.h"

namespace crew {
using mapcmd::Command;
using mapcmd::Order;
// A new air order first reaches its patrol/hover area before it acquires another target.
// Once there, ordinary fighting continues until it leaves the wider engagement area.
// The two radii avoid alternating between a run and return at the patrol-ring boundary.
inline bool AirCommandTransit(const Command& cmd,bool& moving,const float* pos,const float* anchor,
                              float arrive,float leash) noexcept {
    if(cmd.order==Order::none){moving=false;return false;}
    const float x=pos[0]-anchor[0],z=pos[2]-anchor[2],d=x*x+z*z;
    if(d>leash*leash)moving=true;
    if(d<=arrive*arrive)moving=false;
    return moving;
}
// A unit an AI module takes map commands for: the vehicle, a short name for the map, the command it stands under,
// whether it flies (its icon is on it; a ground unit's is up its pin).
// `v` is an identity only outside the owning module; positions are copied while the object is verified live.
struct CommandUnit { const void* v; const char* name; Command now; bool air; float pos[3]{}; bool locked=false; const char* status=nullptr; };
bool CommandVehicleLive(const ObjRef& ref) noexcept;
bool ReadCommandUnit(const ObjRef& ref,const char* name,const Command& cmd,bool air,CommandUnit* out) noexcept;
// Each module's units that take a command now (live, flown or driven by the plugin's NPC, not withdrawing), at most
// `most`; how many. Game thread.
int HeliCommandUnits(CommandUnit* out,int most) noexcept;
int JetCommandUnits(CommandUnit* out,int most) noexcept;
int GroundCommandUnits(CommandUnit* out,int most) noexcept;
// ...and a command given to one of them: false when `v` is not one of its units now. Order::none releases it (back to
// what it did before any command). Game thread.
bool HeliCommand(const void* v,const Command& c) noexcept;
bool JetCommand(const void* v,const Command& c) noexcept;
bool GroundCommand(const void* v,const Command& c) noexcept;
// npcai.cpp: the NPC soldiers' squads (one unit a squad, its leader's address; docs/npc-ai-design.md §5, §6), every one
// the frame saw (a script's too, locked), and an order to one.
int SquadCommandUnits(CommandUnit* out,int most) noexcept;
bool SquadCommand(const void* leader,const Command& c) noexcept;
// npcpost.cpp: the NPC tanks keeping a post (guard moves it; release puts it back on the spawn point).
int TankCommandUnits(CommandUnit* out,int most) noexcept;
bool TankCommand(const void* v,const Command& c) noexcept;
// The panel's row of a squad (hud.cpp MapCommands): its members alive, the seconds left of its dismissal's cooldown.
struct SquadRow { const void* leader; char name[24]; char status[16]; int alive; int cooldown; Command now; bool locked;
    ObjRef identity{}; // game-thread snapshot; the draw passes this unchanged, never recaptures from leader
};
int SquadRows(SquadRow* out,int most) noexcept;
bool HeliSharesPost() noexcept;   // heli.cpp: guard helis on one post share its orbit (HeliGuardRadius > 0)

// The map's input a frame (map.cpp Frame, the map open): the game window in front, a pad read (its buttons), the last
// input a pad's (the pointer hidden, commands at the screen's centre), the game's mouse delta this frame (mouse units),
// the camera (its eye looking at the focus).
struct MapCmdInput {
    bool front,pad,usingPad,mouse;
    WORD buttons;
    float dx,dy;
    float eye[3],look[3];
};
// True when the map should centre on `centre` (a unit just selected by Tab / pad X). Updates in.usingPad to the
// source of this frame's command or pointer input so the map keeps the same mode next frame. Game thread.
bool MapCommandFrame(MapCmdInput& in,float* centre) noexcept;
void ResetMapCommands() noexcept;   // map.cpp ResetMap: a new mission (the selection dropped)
// The map's view as the HUD draws it (hud.cpp MapScreen, draw thread): the selection's box and clicks and the pointer's
// ground point are found on it.
void MapCommandView(const float* viewProj,float width,float height) noexcept;
// hud.cpp: the command buttons as drawn this frame (map_buttons.h; `id` each one's mapbtn::Id), for the clicks.
void MapCommandButtons(const float* rects,const int* ids,int n) noexcept;
// Draw-thread hitboxes: four floats per rectangle (x0,y0,x1,y1), published every draw.
// Pass n=0 when a panel is absent. These calls copy snapshots and never read game objects.
void MapCommandSquadButtons(const float* rects,const ObjRef* identities,int n) noexcept; // up to 16 rows
void MapCommandPayloadButtons(const float* rects,std::uint64_t token,int seat,const int* entries,int n) noexcept; // up to 16
void MapCommandUiPanels(const float* rects,int n) noexcept; // up to 16 complete background rectangles
// Game thread, after MapCommandFrame and before map camera steering. A press begun on UI stays
// captured until its own release, even outside that panel (left, Ctrl-left, and right mouse).
bool MapCommandPointerCaptured() noexcept;
// The left drag is the box's, not the map's pan (Ctrl held when it began): map.cpp Steer leaves the ground alone.
bool MapCommandBoxing() noexcept;
// map.cpp Close: discard hover, pending presses and the rendered view immediately, preserving selected units.
void SuspendMapCommands() noexcept;
// The mark key (NpcMarkKey) pressed with the pointer on an enemy marks it instead of what the map does with that key (Q: the
// camera's turn left): map.cpp Steer asks every frame before it reads its keys (`front`: the game window in front). True
// while that press lasts; the enemy is the one under the pointer when it began (MapCommandFrame marks that one). Game
// thread.
bool MapCommandEats(bool front) noexcept;
// npcai.cpp, the mark key on foot with no enemy near the screen's centre: the units selected on the map guard `at` (as G
// on the map, in a formation round it). How many took it; -1 none selected, -2 online (InSession). Game thread.
int MapCommandGuardAt(const float* at) noexcept;

// What the draw shows (hud.cpp MapScreen): the commandable units, the selection, the pointer and its box, the point,
// the last word.
constexpr int kCmdUnits=96;
// A unit's mark: `name` its kind as the plugin names it (a jet's role, a heli's type, CRAWLER: hud.cpp shows it in the
// HUD's language, hudtext.h Word), `owner` whose unit it is (the HUD names a heli's and a jet's so).
constexpr std::uint8_t kCmdOwnerHeli=0,kCmdOwnerJet=1,kCmdOwnerGround=2;
struct CmdMark { float pos[3]; Command now; bool selected,air,locked; std::uint8_t owner; char name[24]; };
struct MapCommandReadout {
    bool allowed;              // command framework enabled; each target still validates authority and execution
    bool all;                  // every unit selected (more than one)
    int selected;              // how many are
    bool pointOk;
    float point[3];            // where G sends them (the pointer's ground point; the screen centre's with a pad)
    bool pointer;              // the mouse pointer shown at (px, py)
    float px,py;
    bool boxing;               // a box being dragged from (bx, by) to the pointer
    float bx,by;
    int count;
    CmdMark unit[kCmdUnits];
    wchar_t note[80];          // the last command's result or refusal
    bool noteFresh;
    int squads;                // the squad panel (number keys 1-9 pick a row)
    SquadRow squad[16];
    bool squadSelected[16];
    bool sweepOn,healthOn;     // the box sweep going; health boxes for hurt soldiers (npcai.cpp)
    bool guardArmed;           // the guard button clicked: the next click on the ground is its point
    int march;                 // the march's formation (formation.h Shape)
    bool hover;                // an enemy under the pointer (the screen centre with a pad): Q marks it, H focuses on it
    float hoverAt[3];          // its lock point
    bool supportArmed;
    wchar_t supportName[64];
    wchar_t supportStatus[128];
};
bool PlayerMapCommands(MapCommandReadout* out) noexcept;
}  // namespace crew
