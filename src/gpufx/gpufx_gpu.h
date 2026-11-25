// GPU particle effects (src/gpufx): the Direct3D 11 half. Render thread only.
// Draws particle batches into the world's HDR colour target at the world's end (thermal.cpp EndHook, before the post
// stage), soft against a private copy of the scene depth. The whole native pipeline is swapped out and back with a
// D3D11.1 context state (SwapDeviceContextState), so no native binding, shader constant or state object changes.
#pragma once
#include "gpufx_sim.h"
#include <cstdint>
#include <cstring>
#include <memory>

struct ID3D11Buffer;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace crew::gpufx {

// How a batch's texels and colours become light. Ours (smoke) is premultiplied (ONE, INV_SRC_ALPHA, the target's alpha
// untouched), so lit smoke and glow sort and draw together. The stock kinds keep the stock blend's effect exactly,
// the target's alpha included (docs/gpufx-re.md, forward PS 0x19670A0): blend 1 SRC_ALPHA/INV_SRC_ALPHA with alpha
// ONE/ZERO (drawn premultiplied, the same result), blend 2 ONE/ONE on colour and alpha.
enum class Kind : std::uint32_t {
    smoke=0,          // the procedural flipbook: rg normal, b thickness, a density; lit by sun + ambient
    nativeAlpha=1,    // a stock texture with blend 1: rgb = texel * particle colour, a = sat(texel.a * colour.a) * fade
    nativeAdditive=2, // a stock texture with blend 2: the same rgb and a, added
    // The stock lit particle sets (docs/gpufx-re.md, PS 0x196BA60 / VS 0x19848A0): the same rgb and a, the rgb times
    // the stock light sum (four directional lights, the first shadowed, the ambient, the clustered point lights).
    nativeLitAlpha=3,
    nativeLitAdditive=4
};

// The stock lighting a lit batch needs, borrowed from a stock lit draw of this view (gpufx.cpp LitCapture): the
// system constants (xgl_system, 608 bytes), the shadow constants (xgl_additional_light_setting, 400 bytes), the
// shadow map array, the point lights (64-byte records) and the three cluster masks (x, y, depth; 260-byte records).
struct StockLights {
    ID3D11Buffer* system=nullptr;
    ID3D11Buffer* shadow=nullptr;
    ID3D11ShaderResourceView* shadowMap=nullptr;
    ID3D11ShaderResourceView* points=nullptr;
    ID3D11ShaderResourceView* masks[3]{};
    bool Complete() const noexcept { return system && shadow && shadowMap && points && masks[0] && masks[1] && masks[2]; }
};

struct Batch {
    ID3D11ShaderResourceView* texture=nullptr;   // null: the procedural smoke flipbook
    Kind kind=Kind::smoke;
    int gridX=kFlipGrid,gridY=kFlipGrid,frames=kFlipFrames;
    // Corner distance multiplier: 1 = the quad's half width is the instance size (ours); the stock VS places each
    // corner at `size` from the centre (half width size/sqrt 2): 0.7071.
    float cornerScale=1.0f;
    float uInset=0.0f;  // the stock atlas trims 0.0005 off each side of a tile's u range (its VS adds it to frc(u))
    bool sorted=true;   // back to front by this view's depth before the upload
    // The lit kinds: 1 the stock set 0xA40 (the instance's normal, packed into `emissive`: PackNormal), 2 the set 0xC08
    // (an up normal, texels under 0.001 alpha clipped); the normal's weights (core +0x114 normal, +0x118 the corner's
    // direction, +0x11C towards the camera).
    int litSet=0;
    float litWeights[3]{};
    const Instance* instances=nullptr;
    int count=0;
};

struct ViewParams {
    float view[16];    // world -> view, row vectors (Commander view+0x20)
    float proj[16];    // view -> clip, row vectors (Commander view+0x60)
    float sunWorld[3]{0.35f,0.8f,0.45f};   // towards the sun (normalised by the renderer)
    float sunColor[3]{1.6f,1.5f,1.35f};
    float ambient[3]{0.55f,0.6f,0.7f};
    float exposure=1.0f;   // multiplies lit smoke: the scene's measured light scale (Stats::sceneLuma, gpufx.cpp)
    // The stock particle VS's distance fade (xgl_system cb0[35].z/.w): alpha *= sat(1 - (distance * mul + add)).
    float fadeMul=0.0f,fadeAdd=0.0f;
    StockLights lights;   // lit batches are skipped unless complete
};

// A unit normal into a float's bits (10 bits a component, below the NaN patterns) and back (the HLSL decodes it).
inline float PackNormal(const float n[3]) noexcept {
    std::uint32_t u=0;
    for(int i=0;i<3;++i) {
        float v=n[i]<-1.0f ? -1.0f : n[i]>1.0f ? 1.0f : n[i];
        u|=static_cast<std::uint32_t>((v*0.5f+0.5f)*1023.0f+0.5f)<<(10*i);
    }
    float f;static_assert(sizeof(f)==sizeof(u));
    std::memcpy(&f,&u,sizeof(f));
    return f;
}

struct Stats {
    // The surface met (for the probe log): bound colour target and the depth used.
    unsigned width=0,height=0,colorFormat=0,depthFormat=0,samples=0;
    float viewport[4]{};   // the bound viewport (x, y, width, height): split screen draws into its own rectangle
    int drawn=0,batches=0;
    float gpuMs=-1.0f;     // the last resolved GPU time of a Draw (timestamp queries, a few frames late; -1: none yet)
    // The scene's luminance before our draw (the median of five 16x16 blocks of the colour target, read back a few
    // frames late without a stall; -1: none yet) and the brightest block's.
    float sceneLuma=-1.0f,sceneLumaMax=-1.0f;
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
