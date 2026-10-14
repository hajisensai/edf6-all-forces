// Where the plugins read the keyboard, the foreground window and the pad: one place, its source settable from outside.
// By default it is user32 (GetAsyncKeyState, GetForegroundWindow) and the plugin's own XInput (map.cpp). A host that runs the game in the background (the
// test autopilot, tests/autopilot) hands each plugin its own stand-ins through the plugin's exported
// <Plugin>_SetInputSourceV1, so the desktop's keys never reach a background game. Handing them over replaced patching
// the plugins' import slots: another module rewrites those slots after the game starts (measured 2026-10-14: the
// EOS overlay's, 2.1 s in), and the desktop's keys then reached the plugin (the user: "桌面键盘漏进后台").
#pragma once
#include <Windows.h>
#include <atomic>

namespace edf {
using AsyncKeyFn=SHORT(WINAPI*)(int);
using ForegroundFn=HWND(WINAPI*)();
using PadStateFn=DWORD(WINAPI*)(DWORD,void*);   // XInputGetState(user, XINPUT_STATE*)
inline std::atomic<AsyncKeyFn> asyncKeySource{nullptr};
inline std::atomic<ForegroundFn> foregroundSource{nullptr};
inline std::atomic<PadStateFn> padStateSource{nullptr};

// GetAsyncKeyState, from the source handed over if any.
inline SHORT AsyncKey(int vk) noexcept {
    const AsyncKeyFn f=asyncKeySource.load(std::memory_order_acquire);
    return f ? f(vk) : GetAsyncKeyState(vk);
}
// GetForegroundWindow, from the source handed over if any.
inline HWND ForegroundWindow() noexcept {
    const ForegroundFn f=foregroundSource.load(std::memory_order_acquire);
    return f ? f() : GetForegroundWindow();
}
// The pad's XInputGetState handed over, null: the plugin's own XInput.
inline PadStateFn PadSource() noexcept { return padStateSource.load(std::memory_order_acquire); }
// Null for any: user32's / the plugin's own XInput again.
inline void SetInputSource(AsyncKeyFn key,ForegroundFn window,PadStateFn pad=nullptr) noexcept {
    asyncKeySource.store(key,std::memory_order_release);
    foregroundSource.store(window,std::memory_order_release);
    padStateSource.store(pad,std::memory_order_release);
}
}  // namespace edf
