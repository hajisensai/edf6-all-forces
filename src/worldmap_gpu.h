#pragma once
// The world map mission select's D3D11 side (docs/worldmap-select-re.md §1, §6; src/worldmap.cpp draws through it from
// the select's Draw, slot 2 of vtable 0x18052E8, before the stock draw). Render-thread only.
//  - Begin swaps the native pipeline out for a private one (D3D11.1 SwapDeviceContextState, as the scope's lens pass
//    scope_pip_gpu.cpp does): xgl caches its state in the context object (ctx+0x80..), so every native binding comes
//    back whole at End. The colour target is the one the native UI draw has bound (never a target of ours), the
//    viewport the select's (ctx+0x398): the design layout 1920 x 1080 stretched over it, as the stock UI is.
//  - One shader pair compiled at run time (D3DCompile from the system's d3dcompiler_47.dll): a quad from four corners
//    in a constant block, solid or textured (linear filter, clamped at the texture's edges: neighbouring tiles are
//    separate textures whose edge texels are neighbouring pixels of the source, so clamping leaves no seam).
//  - The base map's tiles: one immutable B8G8R8A8 texture each (no mip chain: worldmap_logic.h LevelFor draws the
//    coarsest level whose pixels are no larger than the screen's, so a tile is shown between 1x and 2x minified,
//    where a bilinear sample needs no mips; a chain would add a third to the memory and a GenerateMips per upload),
//    at most kCacheTiles of them (1350 x 1350 x 4 = 7.3 MB each), evicted by worldmap_logic.h Victim.
#include <cstdint>
#include <memory>

struct ID3D11DeviceContext;
struct ID3D11Device;
struct ID3D11ShaderResourceView;
namespace worldmap { struct TilePixels; }

namespace crew::worldmapgpu {

inline constexpr int kCacheTiles=48;

// The select's viewport in render target pixels (ctx+0x398: {int x, y, w, h; float minZ, maxZ}).
struct Viewport { float x,y,w,h; };

// What the map's draw and a stage-2 overlay draw with, between Begin and End. Coordinates are design units
// (1920 x 1080, the layout's: the stock UI's own space); colours 0xAARRGGBB, straight alpha, blended over.
class Painter {
public:
    // A quad by its corners: top-left, top-right, bottom-left, bottom-right (any shape: a rotated line is one).
    virtual void Quad(const float (&xy)[4][2],std::uint32_t argb) noexcept=0;
    virtual void Rect(float x0,float y0,float x1,float y1,std::uint32_t argb) noexcept=0;
    // A line `width` design units thick, square ends.
    virtual void Line(float x0,float y0,float x1,float y1,float width,std::uint32_t argb) noexcept=0;
    // `texture`'s (u0, v0)..(u1, v1) over the rectangle, its colour times `tint` (0xFFFFFFFF: as it is). The texture
    // must be the device's (Device()).
    virtual void Image(ID3D11ShaderResourceView* texture,float x0,float y0,float x1,float y1,float u0,float v0,float u1,
                       float v1,std::uint32_t tint=0xFFFFFFFFu) noexcept=0;
    // The device (stage 2's own textures: markers, labels). Valid between Begin and End.
    virtual ID3D11Device* Device() const noexcept=0;
    // Drawing limited to the rectangle (design units; within the viewport) until Unclip (Begin: the whole viewport).
    virtual void Clip(float x0,float y0,float x1,float y1) noexcept=0;
    virtual void Unclip() noexcept=0;
    // Target pixels a design unit across (a label rasterized at that size stays sharp).
    virtual float Scale() const noexcept=0;
protected:
    ~Painter()=default;
};

class Renderer final : public Painter {
public:
    Renderer() noexcept;
    ~Renderer() noexcept;
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;

    enum class Begun { ok, skip, broken };
    // The private pipeline on `context` (an immediate context; its device's resources made once), drawing into the
    // bound colour target within `viewport`. ok: drawing until End. skip: nothing changed this frame (no colour target
    // bound, an empty viewport). broken: this device cannot (no D3D11.1, the compiler or a shader failed): Why().
    Begun Begin(ID3D11DeviceContext* context,const Viewport& viewport) noexcept;
    // The native pipeline back, exactly as Begin found it.
    void End() noexcept;
    const char* Why() const noexcept;
    // The bound target's size at the last Begin (the pointer's client-to-design mapping).
    int TargetWidth() const noexcept;
    int TargetHeight() const noexcept;

    // The base map's tiles (table indices, worldmap_logic.h TileIndex).
    bool Has(int index) const noexcept;
    // `tile` (w x h BGRA) as a texture, made while begun. False: not made (not begun, a failed device call, or the
    // cache full of tiles that may not go: Victim -1).
    bool Upload(const worldmap::TilePixels& tile,int level,unsigned frame) noexcept;
    // The cached tile's (u0, v0)..(u1, v1) over the rectangle, marked drawn in `frame`.
    void DrawTile(int index,float x0,float y0,float x1,float y1,float u0,float v0,float u1,float v1,unsigned frame) noexcept;
    int Cached() const noexcept;

    // Every device object released (the select's object gone; the next Begin makes them again).
    void Reset() noexcept;

    void Quad(const float (&xy)[4][2],std::uint32_t argb) noexcept override;
    void Rect(float x0,float y0,float x1,float y1,std::uint32_t argb) noexcept override;
    void Line(float x0,float y0,float x1,float y1,float width,std::uint32_t argb) noexcept override;
    void Image(ID3D11ShaderResourceView* texture,float x0,float y0,float x1,float y1,float u0,float v0,float u1,float v1,
               std::uint32_t tint=0xFFFFFFFFu) noexcept override;
    ID3D11Device* Device() const noexcept override;
    void Clip(float x0,float y0,float x1,float y1) noexcept override;
    void Unclip() noexcept override;
    float Scale() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace crew::worldmapgpu
