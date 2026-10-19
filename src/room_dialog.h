// The game's own dialog box (HUiDialogBox, APP:/UI/LYT_HUIDIALOGBOX.SGO) opened by the plugin with its own buttons
// (docs/room-native-dialog-re.md): the window, its focus, mouse / keyboard / pad navigation, confirm / cancel keys and
// sounds are the game's; the plugin hands it a title and a list of (text, 8-byte id) and learns which one was picked.
// Built exactly as the soldier screen builds its outfit popup (0x8AB310): HUiDialogBox::InitParam (ctor 0x8C320),
// its items vector, 0x119C600(system, out weak, layout name, param), then the param destroyed as the game does.
// UI thread only (a screen's OnUpdate).
#pragma once
#include <cstdint>

namespace crew::nativedialog {

struct Button { const wchar_t* text; std::uint64_t id; };
// The dialog: a weak reference to the game's object (it ends when the game closes and frees it).
struct Handle { void* object=nullptr; void* control=nullptr; std::uint32_t serial=0; };

// At load (room_ui.cpp InstallRoomUi): every address checked; false (logged) leaves the menu without dialogs.
bool InstallNativeDialog() noexcept;
bool NativeDialogReady() noexcept;
// Opens one; `cursor` the button focused first. False when the game refused (nothing open then).
bool OpenDialog(const wchar_t* title,const Button* buttons,int count,int cursor,Handle* out) noexcept;
// The game still holds it (open or still closing).
bool DialogAlive(const Handle& h) noexcept;
// The button picked in this dialog (its id), once: false while none was.
bool TakePicked(const Handle& h,std::uint64_t* id) noexcept;
// Closes it now when it is still open (the screen went, the menu was turned off); the reference dropped either way.
void CloseDialog(Handle& h) noexcept;
// The reference dropped (the dialog gone or not ours any more).
void ForgetDialog(Handle& h) noexcept;

}  // namespace crew::nativedialog
