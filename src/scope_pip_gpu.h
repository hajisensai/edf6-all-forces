#pragma once
// The picture-in-picture scope's GPU pass (scope_pip.h; docs/scope-pip-re.md): at the end of a world view (thermal.cpp
// EndHook, the Commander's world end 0x11E3AE2 -> 0x11B63A0, before the native post stage and the HUD) the lens round
// the target's centre is redrawn from a copy of the same frame, its neighbourhood scaled up by the magnification. Only
// the lens's pixels are written (scissor + discard); every other pixel, the depth and every native binding stay as
// they were (D3D11.1 SwapDeviceContextState round the pass, as the thermal composite does).
//
// Render-thread only. Never reads pixels back to the CPU, never hooks COM.
#include <memory>

struct ID3D11DeviceContext;

namespace crew::scopepip {

class Magnifier final {
public:
    Magnifier() noexcept;
    ~Magnifier() noexcept;
    Magnifier(const Magnifier&) = delete;
    Magnifier& operator=(const Magnifier&) = delete;
    Magnifier(Magnifier&&) = delete;
    Magnifier& operator=(Magnifier&&) = delete;

    // With the world's colour target bound as RTV0 (no other RTV) and one full-size viewport: the lens (scope_pip.h
    // LensFor on the target's size) drawn at `magnification`. False, nothing changed, when the surface is not one this
    // supports (multisampled, an array or mip chain, a format it cannot sample, a partial viewport), the magnification
    // is under kLeast, or the device could not give what it needs.
    bool Composite(ID3D11DeviceContext* context, float magnification) noexcept;

    // Release every device resource (the scope closed for a while, a device lost or resized).
    void Reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace crew::scopepip
