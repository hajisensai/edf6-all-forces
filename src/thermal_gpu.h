#pragma once

#include <cstdint>
#include <memory>

struct ID3D11DeviceContext;

namespace crew::thermal {

enum class Polarity { whiteHot, blackHot };

// Render-thread only. The caller identifies opaque enemy draws; this module never
// changes the native IA/VS/HS/DS/GS transforms, reads pixels on the CPU, or hooks COM.
// Capture and Composite submit extra draws. Their call sites must be outside
// native occlusion/pipeline-statistics query intervals whose results matter;
// D3D11 exposes neither discovery nor suspension of an active query interval.
class Renderer final {
public:
    Renderer() noexcept;
    ~Renderer() noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    // Each call starts a new view and invalidates every older mask, even if the
    // token is reused. No native target needs to be bound yet. Only immediate
    // D3D11.1 contexts at feature level 11.0+ are supported.
    bool BeginView(ID3D11DeviceContext* context, std::uint64_t frameToken) noexcept;

    // Observe the active scene DSV without replaying geometry. The first valid
    // observation creates/clears this view's private mask. Call for ordinary
    // opaque meshes too so a view without enemies still receives grayscale.
    // A missing/unsupported DSV returns false without changing native resources.
    bool ObserveSurface(ID3D11DeviceContext* context) noexcept;

    // Call immediately BEFORE the enemy's native opaque DrawIndexed, while its
    // native transforms, viewport, render targets and DSV are still bound. MRTs
    // are supported. A replay preserves the native depth comparison and writes
    // only the private heat mask; MIN/MAX blending retains its nearest fragment.
    // Requires one full-size viewport, RTV0, no SO targets, and depth testing;
    // conditional stencil tests that also mutate stencil are unsupported.
    // Mixing LESS and GREATER depth conventions within one view is unsupported.
    // Unsupported draws invalidate the view so Composite preserves the image.
    bool CaptureIndexed(ID3D11DeviceContext* context, unsigned count,
                        unsigned first, int base) noexcept;

    // Call after scene rendering and before the HUD, with exactly one color RTV.
    // It may differ from the capture MRT; its dimensions must match scene depth.
    // The original DSV need not remain bound. A view is consumed only once, and
    // a view with no successfully observed surface preserves the image. An
    // observed view with no enemy captures still receives grayscale.
    bool Composite(ID3D11DeviceContext* context, Polarity polarity) noexcept;

    // Release device resources and invalidate the current view (resize/device loss).
    void Reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace crew::thermal
