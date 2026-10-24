// The game's own dialog box (HUiDialogBox, APP:/UI/LYT_HUIDIALOGBOX.SGO) opened by the plugin with its own buttons
// (docs/room-native-dialog-re.md): the window, its focus, mouse / keyboard / pad navigation, confirm / cancel keys and
// sounds are the game's; the plugin hands it a title and a list of (text, 8-byte id) and learns which one was picked.
// Built exactly as the soldier screen builds its outfit popup (0x8AB310): HUiDialogBox::InitParam (ctor 0x8C320),
// its items vector (game_dialog.h), 0x119C600(system, out weak, layout name, param), then the param destroyed as the
// game does. UI thread only (a screen's OnUpdate, the dialog's own OnUpdate) except RequestCloseDialogs.
#pragma once
#include <cstdint>

namespace crew::nativedialog {

// `desc`: what the description dialog shows beside the list while this button has the focus (nullptr: nothing); the
// text must stay valid until the dialog is gone (CloseDialog / ForgetDialog).
struct Button { const wchar_t* text; std::uint64_t id; const wchar_t* desc=nullptr; };
// The dialog: a weak reference to the game's object (it ends when the game closes and frees it), and the button picked
// in it (each dialog its own: a pick lands in the Handle of the dialog it was made for, and in no other). The caller
// keeps the Handle where OpenDialog wrote it (its address is how a pick finds it) until CloseDialog / ForgetDialog.
struct Handle {
    void* object=nullptr; void* control=nullptr; std::uint32_t serial=0;
    std::uint64_t picked=0; bool hasPicked=false;
};

// At load (room_ui.cpp InstallRoomUi): every address checked; false (logged) leaves the menu without dialogs.
bool InstallNativeDialog() noexcept;
bool NativeDialogReady() noexcept;
// The description dialog (HUiDescriptionDialog: the list with the focused button's description beside it) is usable:
// OpenDialog opens one when its buttons carry descriptions.
bool DescriptionDialogReady() noexcept;
// The index of the button the focus is on in this description dialog (the game reports it as the focus moves), -1 none
// yet or a plain dialog.
int FocusedButton(const Handle& h) noexcept;
// Opens one; `cursor` the button focused first. Buttons with descriptions (and DescriptionDialogReady): an
// HUiDescriptionDialog from `layout` (room_ui_logic.h kLoadoutBoxLayout, the menu's wide copy of the game's
// LYT_HUIDESCDIALOGBOX; nullptr: that stock file); else the plain dialog box from the game's LYT_HUIDIALOGBOX (`layout`
// ignored). False when the game refused (nothing open then, nothing held).
bool OpenDialog(const wchar_t* title,const Button* buttons,int count,int cursor,Handle* out,const wchar_t* layout) noexcept;
// The game still holds it (open or still closing).
bool DialogAlive(const Handle& h) noexcept;
// The button picked in this dialog (its id), once: false while none was.
bool TakePicked(Handle& h,std::uint64_t* id) noexcept;
// Closes it now when it is still open (the screen went, the menu was turned off); the reference dropped either way.
void CloseDialog(Handle& h) noexcept;
// The reference dropped (the dialog gone or not ours any more).
void ForgetDialog(Handle& h) noexcept;
// Any thread (a mission starting, MissionSync on the script thread): every dialog of ours open now is closed by its
// own next frame (HUiDialogBox::OnUpdate, chained: it runs while the dialog is on screen, whether or not the room or
// the HQ screen under it still updates). Dialogs opened after the request are not touched.
void RequestCloseDialogs() noexcept;

}  // namespace crew::nativedialog
