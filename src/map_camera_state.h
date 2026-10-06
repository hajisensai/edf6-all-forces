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
}  // namespace mapcam
