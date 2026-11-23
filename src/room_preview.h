// The room / HQ menu's 3D preview of one support soldier or vehicle (docs/room-3d-preview-re.md, plan A: the game's own
// UiHQ_3DModelView shows the soldier instead of the player while the menu asks; the save is never written).
// Contract between the menu screen (src/room_ui.cpp, stage 4) and the preview (src/room_preview.cpp, stage 7).
// Every call comes from the game's UI thread (the HUiRoom / HUiHQMain OnUpdate wrappers).
#pragma once
#include "preview_camera.h"
#include "support_loadout.h"

namespace crew::roompreview {

struct PreviewSpec {
    SupportWeapon kind=SupportWeapon{};   // the soldier's class / weapon (support_loadout.h)
    bool leader=false;
    SupportLook look{};                  // kStockColour = the template's colour
    // Not none: that vehicle body instead of a soldier (its unified model file, Mods/OBJECT/EDF6VC_FLY_*.SGO: the battle
    // keys plus the menu's, docs/unified-model-re.md; kind / leader / look unused). Its pylons are not on the model (the
    // menu renderer hangs only WeaponTable rows); the menu's own rows show them.
    VehicleBody body=VehicleBody::none;
};

// Installs the hooks the preview needs; false (and logged) when the code does not match: the menu then runs without it.
bool InstallPreviewHooks() noexcept;
// Shows `spec` in place of the player's soldier (again with a new spec: rebuilt or recoloured as needed).
void ShowPreview(const PreviewSpec& spec) noexcept;
// Puts the player's own soldier back. Idempotent.
void HidePreview() noexcept;
// Once per UI frame while the room / HQ screen lives (camera offset, pending rebuilds, safe teardown).
void TickPreview(bool menuOpen) noexcept;

// Places the view's model in `rect` (design units, 1920x1080 over the whole screen) looked at by `camera`
// (preview_camera.h; kCameraPresets: 自由 / 正面 / 侧面 / 俯视 / 特写) instead of MENUMODELSETTING's camera_pos, and keeps
// the view drawn while another screen (the armory) covers the HQ: the model is drawn in the 3D pass before every UI
// layer, so that screen must leave `rect` uncovered (docs/room-3d-preview-re.md section 6.2). Again to move / re-aim;
// the screen's aspect is read here. An empty rect clears it.
void SetViewPlacement(const ViewRect& rect,const ViewCamera& camera) noexcept;
// The game's own placement back (and the view's selection / hidden state as they were). Idempotent.
void ClearViewPlacement() noexcept;
// Shows WeaponTable row `row` in weapon slot `slot` (0..3) of the player's own model (OnMessage 4 {player, slot, row}),
// when the view shows class `cls` and no support soldier is previewed; sent from the view's next update.
void ShowPlayerWeapon(int cls,int slot,int row) noexcept;

}  // namespace crew::roompreview
