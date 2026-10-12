// Stage 7 replaces this stub (src/room_preview.h): until then the menu runs without a 3D preview.
#include "room_preview.h"

namespace crew::roompreview {
bool InstallPreviewHooks() noexcept { return false; }
void ShowPreview(const PreviewSpec&) noexcept {}
void HidePreview() noexcept {}
void TickPreview(bool) noexcept {}
}  // namespace crew::roompreview
