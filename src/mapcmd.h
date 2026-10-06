// mapcmd.cpp: commanding the plugin's NPC units from the map view (README 地图 → 指挥 NPC; the user, 2026-10-06:
// "在地图上可以指挥npc"). The map (map.cpp) calls MapCommandFrame each frame it is open; hud.cpp draws its readout.
// The orders themselves are the AI modules' state (heli.cpp, jet.cpp, ground.cpp): each takes a command at the one
// place it picks what it works round (its anchor / post / leader), reusing its own flight, targeting and fight.
// Included by crew.h.
#pragma once
#include <Windows.h>
#include "mapcmd_logic.h"

namespace crew {
using mapcmd::Command;
using mapcmd::Order;
// A unit an AI module takes map commands for: the vehicle, a short name for the map, the command it stands under.
struct CommandUnit { const void* v; const char* name; Command now; bool air; };
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

// The map's frame (map.cpp Frame, game thread, the map open): the command keys (`front`: the game window in front;
// `pad`: a pad read, its `buttons`), the screen's centre (the ray from `eye` through `look`). True when the map should
// centre on `centre` (a unit just selected by its key).
bool MapCommandFrame(bool front,bool pad,WORD buttons,const float* eye,const float* look,float* centre) noexcept;
void ResetMapCommands() noexcept;   // map.cpp ResetMap: a new mission (the selection dropped)

// What the draw shows (hud.cpp MapScreen): the commandable units, the selection, the point, the last word.
constexpr int kCmdUnits=96;
struct CmdMark { float pos[3]; Command now; bool selected,air; char name[24]; };
struct MapCommandReadout {
    bool allowed;              // commands work (offline: InSession false)
    bool all;                  // every unit selected
    bool pointOk;
    float point[3];            // the screen centre's ground point
    int count;
    CmdMark unit[kCmdUnits];
    wchar_t note[80];          // the last command's result or refusal (shown kNoteMs)
    bool noteFresh;
};
bool PlayerMapCommands(MapCommandReadout* out) noexcept;
}  // namespace crew
