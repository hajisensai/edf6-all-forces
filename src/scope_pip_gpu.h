#pragma once
// The picture-in-picture scope's GPU pass (scope_pip.h; docs/scope-pip-re.md §5.4). The lens view (viewport 3, a
// TraceCamera at the lens's narrow field, scope_lens.cpp) renders the world into its own S x S colour target; the main
// view (viewport 0) into its own. At each one's world end (thermal.cpp EndHook, the Commander's world end 0x11E3AE2 ->
// 0x11B63A0) the bound colour target is held: the lens view's after the last recording's main view, the main view's in
// this one; then the lens command (recorded by scope_lens.cpp right after the main view's world, before its post stage)
// samples the lens view's own colour texture (the engine creates it with D3D11_BIND_SHADER_RESOURCE: docs/scope-pip-re.md
// §5.4) and draws it into the main view's world colour as the lens's disc. No copy: one shader resource view per
// texture (pointer + description + view format), kept while that texture is the lens view's. Only the lens's pixels are written (scissor + discard); every other pixel, the depth and
// every native binding stay as they were (D3D11.1 SwapDeviceContextState round the pass, as the thermal composite does).
//
// Render-thread only. Never reads pixels back to the CPU, never hooks COM.
#include <memory>

struct ID3D11DeviceContext;

namespace crew::scopepip {

// The lens view's texture the cached view samples, and the native output-merger bindings the composite found (the log's
// one line per new view: the runtime proof the texture is samplable and is not the main view's target).
struct LensSource {
    const void* texture = nullptr;      // the lens view's colour texture
    unsigned width = 0, height = 0, format = 0, viewFormat = 0, bindFlags = 0;
    const void* mainTexture = nullptr;  // the main view's world colour (the composite's render target)
    unsigned mainWidth = 0, mainHeight = 0;
    unsigned nativeTargets = 0;         // render targets bound natively at the composite
    bool lensBoundAsTarget = false;     // the lens texture among them (the composite refuses then)
    unsigned views = 0;                 // shader resource views created so far (one per lens texture)
    long viewError = 0;                 // the device's HRESULT when it refused the last view (0: made or cached)
};

class LensCompositor final {
public:
    LensCompositor() noexcept;
    ~LensCompositor() noexcept;
    LensCompositor(const LensCompositor&) = delete;
    LensCompositor& operator=(const LensCompositor&) = delete;
    LensCompositor(LensCompositor&&) = delete;
    LensCompositor& operator=(LensCompositor&&) = delete;

    // The main view's world end: its colour target (RTV0, no other RTV, one full-size viewport) held for the lens, with
    // the view's projection's vertical scale. False, no main view held, when the surface is not one this supports
    // (multisampled, an array or mip chain, a format it cannot sample, a partial viewport) or the scale is unusable.
    // A held lens view stays (it was rendered after the last main view, for this one).
    bool CaptureMain(ID3D11DeviceContext* context, float yScale) noexcept;
    // The lens view's world end: its colour texture held (the same checks, square: `side` x `side`, and bindable as a
    // shader resource), with its projection's vertical scale. It replaces any lens view held; false: none held.
    bool CaptureLens(ID3D11DeviceContext* context, int side, float yScale) noexcept;
    // Both held on this device: the lens view's picture drawn into the main view's colour as the lens (scope_pip.h
    // LensFor / LensSample) when it magnifies over the frame (LensPower >= kLeast). Both released either way. True when
    // it drew; then *mainHeight, *radius and *lensYScale are the geometry it drew with (the HUD's reticle).
    bool Composite(ID3D11DeviceContext* context, float* mainHeight, float* radius, float* lensYScale) noexcept;

    // Whether a main view / a lens view is held (tests; the lens command's diagnostics).
    bool HoldsMain() const noexcept;
    bool HoldsLens() const noexcept;
    // Drop the held targets (a frame whose lens never came).
    void Release() noexcept;
    // Release every device resource (the scope closed for a while, a device lost or resized, a new mission): the cached
    // view of the lens view's texture too.
    void Reset() noexcept;
    // Whether a shader resource view of a lens view's texture is cached (tests), and the last composite's source.
    bool HoldsLensView() const noexcept;
    LensSource Source() const noexcept;

private:
    // The pass's device resources for `context`'s device: kept while the device is the same and alive.
    bool Ready(ID3D11DeviceContext* context) noexcept;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace crew::scopepip
