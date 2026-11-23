// 整备 (docs/plan-2026-11-24-ui-rewrite.md, docs/armory-re.md): the full-screen loadout screen over the offline HQ
// (ui::HUiHQMain) and the online room (ui::HUiRoom). armory_logic.h is its state machine, armory_skin.h its look,
// armory_draw.cpp its painter, armory_presets.h its presets; this is the game side:
//  - both screens' Draw (vtable slot 2, stock HUiBase::Draw 0x7FA970) chained: after the screen's own draw, while the
//    screen is open on that screen object, the frame's View (built on the UI thread) painted over the whole viewport;
//  - room_ui.cpp's frame (the UI thread) drives it: it opens it (F2 / L3+R3 / the native 整备 button), calls Frame each
//    frame before the screen's own update and takes the screen's input away (ctx+8 null) while Frame says so;
//  - its own input: the keyboard, the mouse (pointer in design units through the drawn viewport, buttons, the wheel and
//    typed characters by a window subclass) and the first XInput pad.
#pragma once
#include "room_menu.h"
#include "room_preview.h"

namespace crew::armoryui {

// At load (room_ui.cpp InstallRoomUi, RoomLoadout=1): checks the draw contract and chains both Draw slots. False
// (logged REFUSED): the screen cannot be drawn, room_ui never opens it.
bool Install() noexcept;
bool Installed() noexcept;

// --- The UI thread (room_ui.cpp Frame) ---
// Opened on screen object `self` over a copy of `units` (room_ui's menu, loaded from the ini just now through `read`;
// the 支援编成 / 载具 pages edit the copy and save their own lines, src/support_pages.h). `read` reloads them after a
// preset switch; `previewOn`: the 3D preview's hooks are in (room_preview.h InstallPreviewHooks). The keys held now
// count as already down (the key that opened it does nothing inside). `tab` >= 0: opened at that page (armory.cpp
// kPages: 0 武器, 1 支援编成, 2 载具, 3 任务), else at the last one.
void Open(unsigned char* self,bool room,const roommenu::Menu* units,roommenu::IniRead read,void* readContext,bool previewOn,
          int tab=-1) noexcept;
bool IsOpen() noexcept;
// One frame. `hasInput`: the screen would read input this frame (nothing of the game's over it); `toggleDown`: the
// open / close key or pad combination is held (room_ui's KeysDown). Closed when the screen lost its input (a game
// dialog, a sub-screen) or by its own keys. Returns true while the screen's input must be taken away: open, or one of
// its keys still held after it closed (the room / HQ would read the key that closed it).
bool Frame(unsigned char* self,bool hasInput,bool toggleDown) noexcept;
// What the 3D viewport of the page open now shows, where and from which camera (room_preview.h ShowPreview /
// SetViewPlacement); false: nothing (closed, another page, no model for the row, the preview's hooks not in, a popup
// over the weapons page). `*own` true: the player's own model (the weapons page; `*spec` unused, no override shown).
bool PreviewWanted(roompreview::PreviewSpec* spec,roompreview::ViewRect* rect,roompreview::ViewCamera* camera,
                   bool* own) noexcept;
// The 对战设置 row picked (the room's PvP pages, room_ui.cpp's native dialogs): true once per request.
bool TakePvpRequest() noexcept;
// The world map asked (the HQ's 任务 tab chosen, or the 任务 page's world map row): true once per request; room_ui
// presses the screen's own mission select button (closed already).
bool TakeMapRequest() noexcept;
// The world map's title bar (worldmap.cpp): a tab other than the map's clicked; the select is closed the stock way and
// the next frame its screen (the HQ / the room) has its input, room_ui opens 整备 at `tab` (TakeOpenAsk: once, -1 none).
void AskOpen(int tab) noexcept;
int TakeOpenAsk() noexcept;
// Closed at once, nothing committed (a mission starting, the screen forgotten, the plugin turned off).
void Forget() noexcept;

// --- Any thread: the screen object's destructor ran (room_ui.cpp's destructor hooks): never drawn on it again ---
void ScreenGone(unsigned char* self) noexcept;

}  // namespace crew::armoryui
