// GPU particle effects (src/gpufx): the Direct3D 11 half. Render thread only.
// Draws particle batches into the world's HDR colour target at the world's end (thermal.cpp EndHook, before the post
// stage), soft against a private copy of the scene depth. The whole native pipeline is swapped out and back with a
// D3D11.1 context state (SwapDeviceContextState), so no native binding, shader constant or state object changes.
#pragma once
#include "gpufx_sim.h"
#include <cstdint>
#include <memory>

struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace crew::gpufx {

// How a batch's texels and colours become light. All three share one premultiplied blend (ONE, INV_SRC_ALPHA, the
// target's alpha untouched), so lit smoke and glow sort and draw together.
enum class Kind : std::uint32_t {
    smoke=0,          // the procedural flipbook: rg normal, b thickness, a density; lit by sun + ambient
    nativeAlpha=1,    // a stock ParticleRender texture with blend 1: colour = texel * particle colour, alpha blended
    nativeAdditive=2  // a stock texture with blend 2 (ONE/ONE): texel * particle colour added
};

struct Batch {
    ID3D11ShaderResourceView* texture=nullptr;   // null: the procedural smoke flipbook
    Kind kind=Kind::smoke;
    int gridX=kFlipGrid,gridY=kFlipGrid,frames=kFlipFrames;
    // Corner distance multiplier: 1 = the quad's half width is the instance size (ours); the stock VS places each
    // corner at `size` from the centre (half width size/sqrt 2): 0.7071.
    float cornerScale=1.0f;
    bool sorted=true;   // back to front by this view's depth before the upload
    const Instance* instances=nullptr;
    int count=0;
};

struct ViewParams {
    float view[16];    // world -> view, row vectors (Commander view+0x20)
    float proj[16];    // view -> clip, row vectors (Commander view+0x60)
    float sunWorld[3]{0.35f,0.8f,0.45f};   // towards the sun (normalised by the renderer)
    float sunColor[3]{1.6f,1.5f,1.35f};
    float ambient[3]{0.55f,0.6f,0.7f};
    float exposure=1.0f;   // multiplies lit smoke (the HDR target's scale; stock smoke is drawn near 2,2,2)
};

struct Stats {
    // The surface met (for the probe log): bound colour target and the depth used.
    unsigned width=0,height=0,colorFormat=0,depthFormat=0,samples=0;
    float viewport[4]{};   // the bound viewport (x, y, width, height): split screen draws into its own rectangle
    int drawn=0,batches=0;
    float gpuMs=-1.0f;     // the last resolved GPU time of a Draw (timestamp queries, a few frames late; -1: none yet)
    bool reversed=false,lookNegZ=true;
    const char* refused=nullptr;   // why nothing was drawn (null when drawn)
};

class Renderer final {
public:
    Renderer() noexcept;
    ~Renderer() noexcept;
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;

    // The smoke flipbook's pixels (kFlipSide^2 RGBA8, GenerateSmokeFlipbook); copied into a texture with mips on the
    // next Draw. Until given, smoke batches are skipped.
    void SetFlipbook(const std::uint8_t* rgba) noexcept;

    // Call with the world's colour target bound as the only RTV and the view's viewport set (thermal.cpp EndHook). `depth`: the scene depth texture
    // this view rendered with and its DSV format (D16/D24S8/D32/D32S8); same size as the colour target, one sample.
    // False (nothing drawn, nothing changed) on any unsupported surface; stats->refused says why.
    bool Draw(ID3D11DeviceContext* context,ID3D11Texture2D* depth,unsigned depthViewFormat,const ViewParams& view,
              const Batch* batches,int count,Stats* stats) noexcept;

    // Release every device resource (a new mission, a device change).
    void Reset() noexcept;

    static constexpr int kMaxInstances=16384;
    static constexpr int kMaxBatches=256;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace crew::gpufx
