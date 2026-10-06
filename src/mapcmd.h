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
// A unit an AI module takes map commands for: the vehicle, a short name for the map, the command it stands under,
// whether it flies (its icon is on it; a ground unit's is up its pin).
// `locked`: listed (shown, selectable) but takes no order now (a squad a mission script drives: §4.3), `status` what it
// is doing in a word or two (the panel's column; nullptr: none).
struct CommandUnit { const void* v; const char* name; Command now; bool air; bool locked; const char* status; };
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
struct SquadRow { const void* leader; char name[24]; char status[16]; int alive; int cooldown; Command now; bool locked; };
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
// True when the map should centre on `centre` (a unit just selected by Tab / pad X). Game thread.
bool MapCommandFrame(const MapCmdInput& in,float* centre) noexcept;
void ResetMapCommands() noexcept;   // map.cpp ResetMap: a new mission (the selection dropped)
// The map's view as the HUD draws it (hud.cpp MapScreen, draw thread): the selection's box and clicks and the pointer's
// ground point are found on it.
void MapCommandView(const float* viewProj,float width,float height) noexcept;
// The left drag is the box's, not the map's pan (Ctrl held when it began): map.cpp Steer leaves the ground alone.
bool MapCommandBoxing() noexcept;

// What the draw shows (hud.cpp MapScreen): the commandable units, the selection, the pointer and its box, the point,
// the last word.
constexpr int kCmdUnits=96;
struct CmdMark { float pos[3]; Command now; bool selected,air,locked; char name[24]; };
struct MapCommandReadout {
    bool allowed;              // commands work (offline: InSession false)
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
};
bool PlayerMapCommands(MapCommandReadout* out) noexcept;
}  // namespace crew
