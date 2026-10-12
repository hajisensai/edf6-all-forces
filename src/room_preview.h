// The room / HQ menu's 3D preview of one support soldier (docs/room-3d-preview-re.md, plan A: the game's own
// UiHQ_3DModelView shows the soldier instead of the player while the menu asks; the save is never written).
// Contract between the menu screen (src/room_ui.cpp, stage 4) and the preview (src/room_preview.cpp, stage 7).
// Every call comes from the game's UI thread (the HUiRoom / HUiHQMain OnUpdate wrappers).
#pragma once
#include "support_loadout.h"

namespace crew::roompreview {

struct PreviewSpec {
    SupportWeapon kind=SupportWeapon{};   // the soldier's class / weapon (support_loadout.h)
    bool leader=false;
    SupportLook look{};                  // kStockColour = the template's colour
};

// Installs the hooks the preview needs; false (and logged) when the code does not match: the menu then runs without it.
bool InstallPreviewHooks() noexcept;
// Shows `spec` in place of the player's soldier (again with a new spec: rebuilt or recoloured as needed).
void ShowPreview(const PreviewSpec& spec) noexcept;
// Puts the player's own soldier back. Idempotent.
void HidePreview() noexcept;
// Once per UI frame while the room / HQ screen lives (camera offset, pending rebuilds, safe teardown).
void TickPreview(bool menuOpen) noexcept;

}  // namespace crew::roompreview
