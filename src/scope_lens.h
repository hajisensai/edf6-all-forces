// The picture-in-picture scope's lens view (scope_pip.h; docs/scope-pip-re.md §4-§5): the engine's own fourth viewport
// (kLensViewport = 3) renders the world again through a TraceCamera that follows the player's camera (the engine's
// stereo_camera debug command makes the same camera), at the lens's narrow field and at the lens's own resolution. The
// lens's pixels are a real render at the scope's power, not a magnification of the frame: their clarity does not drop
// with the power (the maintainer, 2026-11-10, of the magnified centre: "画质不能接受").
//
// Game thread, inside the render recording 0x11978D0 (System::update at 0x1198FA4):
//  - viewport 0's projection (0x1197B50 -> 0x118DFF0): while the scoped soldier's camera (Want) left the frame
//    unmagnified, the lens camera is made viewport 3's current camera for this recording ("armed");
//  - viewport 3's projection: the lens camera takes viewport 0's camera's world matrix, near / far and +0x20 exactly
//    (not the TraceCamera's own 0.2 m eye offset) and the lens's field (scope_pip.h LensViewFov);
//  - while armed, viewport 0's tail (post stage and its composite to the screen: 0x1198742 / 0x1198753 / 0x1198777 /
//    0x119879F / 0x11987BF) is held and recorded after viewport 3's world, its G-buffer set (0x119863C) recorded again
//    before it; viewport 3's tail is never recorded (the lens view never reaches the screen), its first call replaced by
//    one command of the plugin's own (InfantryScopeLensCommand: the lens drawn into viewport 0's world colour);
//  - the loop's submit (0x119889B -> 0x111B410): the held tail recorded, viewport 3's current camera taken off again
//    (no HUD stage, no other code ever sees a fourth player's camera).
// Refused (logged once, until the next mission): a local split screen (viewport 1 or 2 has a camera), viewport 3 used by
// something else, the camera never joining the viewport. Then InfantryScope=2 shows mode 1's picture.
// Addresses: RVAs into EDF.dll TimeDateStamp 0x678CCB46, checked byte by byte at install (tests/scope_lens_native_audit.py).
#pragma once

struct ID3D11DeviceContext;

namespace crew {
namespace scopelens {
// At load: every site and callee checked, the recording's calls redirected. False (logged): no lens view.
bool Install() noexcept;
bool Installed() noexcept;
// Game thread (the camera step): `camera` (a CharacterGhostCamera) is the scoped soldier's, its step left the frame
// unmagnified, the scope's vertical FOV `scopeFov`. nullptr: no lens wanted. Stale after kFreshMs.
void Want(const void* camera,float scopeFov) noexcept;
// Whether the lens view is refused this mission (any thread).
bool Refused() noexcept;
// The lens view's side (S x S) the last armed recording used (render thread: the lens view's world end checks it). 0: none.
int Side() noexcept;
// Mission start (game thread): the lens camera (the engine destroyed it with the mission's cameras) let go, a refusal
// forgotten.
void Reset() noexcept;
}  // namespace scopelens

// Render thread, the lens command (recorded by scope_lens.cpp after the lens view's world, before viewport 0's post
// stage): infantryscope.cpp composites the lens.
void InfantryScopeLensCommand(ID3D11DeviceContext* context) noexcept;
}  // namespace crew
