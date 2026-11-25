#pragma once
// The plugin's own UI drawing on the game's D3D11 context (shared by the world map, src/worldmap.cpp, and the 整备 screen,
// src/armory.cpp; docs/worldmap-select-re.md §1, §6). Render-thread only (a UI screen's Draw, vtable slot 2).
//  - Begin swaps the native pipeline out for a private one (D3D11.1 SwapDeviceContextState, as the scope's lens pass
//    scope_pip_gpu.cpp does): xgl caches its state in the context object (ctx+0x80..), so every native binding comes
//    back whole at End. The colour target is the one the native UI draw has bound (never a target of ours), the
//    viewport the screen's (ctx+0x398): the design layout 1920 x 1080 stretched over it, as the stock UI is.
//  - One shader pair compiled at run time (D3DCompile from the system's d3dcompiler_47.dll): a quad from four corners
//    in a constant block, each corner its own colour (a gradient; one colour: solid), solid or textured (linear filter,
//    clamped at the texture's edges, or repeated: Pattern).
//  - Textures are the caller's (MakeImage: an immutable BGRA texture of this device), drawn by Image / Pattern.
#include <cstdint>
#include <memory>

struct ID3D11DeviceContext;
struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace crew::uigpu {

// The screen's viewport in render target pixels (ctx+0x398: {int x, y, w, h; float minZ, maxZ}).
struct Viewport { float x,y,w,h; };

// What a screen draws with, between Begin and End. Coordinates are design units (1920 x 1080, the layout's: the stock
// UI's own space); colours 0xAARRGGBB, straight alpha, blended over.
class Painter {
public:
    // A quad by its corners: top-left, top-right, bottom-left, bottom-right (any shape: a rotated line is one).
    virtual void Quad(const float (&xy)[4][2],std::uint32_t argb) noexcept=0;
    // The same with a colour per corner (in the same order), interpolated across the quad.
    virtual void QuadColours(const float (&xy)[4][2],const std::uint32_t (&argb)[4]) noexcept=0;
    virtual void Rect(float x0,float y0,float x1,float y1,std::uint32_t argb) noexcept=0;
    // A rectangle shading from `from` to `to`: top to bottom (vertical) or left to right.
    virtual void Gradient(float x0,float y0,float x1,float y1,std::uint32_t from,std::uint32_t to,bool vertical) noexcept=0;
    // A line `width` design units thick, square ends.
    virtual void Line(float x0,float y0,float x1,float y1,float width,std::uint32_t argb) noexcept=0;
    // `texture`'s (u0, v0)..(u1, v1) over the rectangle, its colour times `tint` (0xFFFFFFFF: as it is). The texture
    // must be the device's (Device()).
    virtual void Image(ID3D11ShaderResourceView* texture,float x0,float y0,float x1,float y1,float u0,float v0,float u1,
                       float v1,std::uint32_t tint=0xFFFFFFFFu) noexcept=0;
    // The same cut to the rectangle (cx0, cy0)..(cx1, cy1): the part outside it not drawn, the texture coordinates of
    // the rest kept where they were (a label too long for its cell). Nothing when they do not overlap.
    virtual void ImageClipped(ID3D11ShaderResourceView* texture,float x0,float y0,float x1,float y1,float u0,float v0,
                              float u1,float v1,std::uint32_t tint,float cx0,float cy0,float cx1,float cy1) noexcept=0;
    // `texture` repeated over the rectangle, one copy every `periodX` x `periodY` design units from the design origin
    // (a background pattern: the same tiling wherever the rectangle is).
    virtual void Pattern(ID3D11ShaderResourceView* texture,float x0,float y0,float x1,float y1,float periodX,float periodY,
                         std::uint32_t tint=0xFFFFFFFFu) noexcept=0;
    // The device (the caller's own textures: MakeImage). Valid between Begin and End.
    virtual ID3D11Device* Device() const noexcept=0;
    // The bound target stores sRGB (a texture made for it reads its texels as sRGB: MakeImage's `srgb`).
    virtual bool Srgb() const noexcept=0;
    // Drawing limited to the rectangle (design units; within the viewport) until Unclip (Begin: the whole viewport).
    virtual void Clip(float x0,float y0,float x1,float y1) noexcept=0;
    virtual void Unclip() noexcept=0;
    // Target pixels a design unit across (a label rasterized at that size stays sharp).
    virtual float Scale() const noexcept=0;
protected:
    ~Painter()=default;
};

// `w` x `h` BGRA pixels (0xAARRGGBB words) as an immutable texture of `device`, viewed as sRGB or not; one reference
// the caller's (Release it). nullptr when the device refused.
ID3D11ShaderResourceView* MakeImage(ID3D11Device* device,const std::uint32_t* bgra,int w,int h,bool srgb) noexcept;

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
    // Every device object released (the screen's object gone; the next Begin makes them again).
    void Reset() noexcept;

    void Quad(const float (&xy)[4][2],std::uint32_t argb) noexcept override;
    void QuadColours(const float (&xy)[4][2],const std::uint32_t (&argb)[4]) noexcept override;
    void Rect(float x0,float y0,float x1,float y1,std::uint32_t argb) noexcept override;
    void Gradient(float x0,float y0,float x1,float y1,std::uint32_t from,std::uint32_t to,bool vertical) noexcept override;
    void Line(float x0,float y0,float x1,float y1,float width,std::uint32_t argb) noexcept override;
    void Image(ID3D11ShaderResourceView* texture,float x0,float y0,float x1,float y1,float u0,float v0,float u1,float v1,
               std::uint32_t tint=0xFFFFFFFFu) noexcept override;
    void ImageClipped(ID3D11ShaderResourceView* texture,float x0,float y0,float x1,float y1,float u0,float v0,float u1,
                      float v1,std::uint32_t tint,float cx0,float cy0,float cx1,float cy1) noexcept override;
    void Pattern(ID3D11ShaderResourceView* texture,float x0,float y0,float x1,float y1,float periodX,float periodY,
                 std::uint32_t tint=0xFFFFFFFFu) noexcept override;
    ID3D11Device* Device() const noexcept override;
    bool Srgb() const noexcept override;
    void Clip(float x0,float y0,float x1,float y1) noexcept override;
    void Unclip() noexcept override;
    float Scale() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace crew::uigpu
