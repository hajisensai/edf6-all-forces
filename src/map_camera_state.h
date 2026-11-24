// Mission ownership crosses from the game thread to the camera hook. The camera's
// cached matrix and blend remain exclusively owned by that hook.
#pragma once
#include <atomic>
#include <cstdint>

namespace mapcam {
struct CameraState {
    std::uint64_t generation=0;
    const void* cam=nullptr;
    bool shown=false,leaving=false,haveStock=false;
    int blend=0;
    float eye[3]{},look[3]{},stock[16]{};
};

class CameraSession {
    std::atomic<std::uint64_t> generation{1},owner{0};
public:
    // Game thread: invalidate ownership without touching camera-thread state.
    void Reset() noexcept { generation.fetch_add(1); }
    // Camera thread, before restoring a cached matrix or rejecting another camera.
    std::uint64_t Begin(CameraState& state) const noexcept {
        const auto now=generation.load();
        if(state.generation!=now){state=CameraState{};state.generation=now;}
        return now;
    }
    bool Current(std::uint64_t frame) const noexcept { return generation.load()==frame; }
    std::uint64_t Generation() const noexcept { return generation.load(); }
    void Publish(std::uint64_t frame,bool owns) noexcept { owner.store(owns ? frame : 0); }
    bool Owns() const noexcept {
        const auto frame=generation.load();
        return owner.load()==frame && generation.load()==frame;
    }
};
// The pause menu over the open map (the camera step sees it; the scene's steps, Frame among them, are skipped under it):
// the stock view goes up under the menu at once, and the map is closed by the first step after the pause, so the menu's
// Esc is the menu's and the player comes back to the game's own view (step_fresh.h: the pause is an explicit state).
class PauseClose {
    std::atomic<bool> seen{false};
public:
    // Camera thread: whether the map's view goes up this camera step; `due` the map open, its player's and fresh.
    bool CameraShows(bool due,bool paused) noexcept {
        if(due && paused){seen.store(true);return false;}
        return due;
    }
    // Game thread (Frame, each step): true once after a pause the camera saw, while the map is open: close it.
    bool CloseNow(bool open) noexcept { return seen.exchange(false) && open; }
    // Game thread (a new mission).
    void Reset() noexcept { seen.store(false); }
};
}  // namespace mapcam
