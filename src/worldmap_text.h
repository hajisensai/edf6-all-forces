#pragma once
// The world map's words and marks (src/worldmap.cpp, the select's draw; render thread only, under its gpuLock).
//  - Labels: a string rasterized by GDI (the system's CJK UI font for the game's language: Microsoft YaHei UI, Microsoft
//    JhengHei UI, Yu Gothic UI, Segoe UI; grey-scale antialiasing) into a white texture whose alpha is the coverage,
//    drawn with Painter::Image times a colour. Rasterized at the target's pixel size (Painter::Scale), kept by string
//    and size while drawn (at most kLabels; the least recently drawn goes), so a line is made once, not every frame.
//    Why not the game's own text: its renderer (src/glyphs.cpp, docs/hud-re.md) queues glyph quads into the HUD's and
//    the layout's own draw lists with their own state; nothing of it can be called between our Begin and End, and its
//    glyph cache is not safe from another thread (glyphs.cpp locks it for that).
//  - Sprites: an antialiased disc and ring (the pins, the current mission's ring), made once per device.
#include "hudtext.h"
#include "worldmap_gpu.h"

#include <memory>

namespace crew::worldmaptext {

inline constexpr int kLabels=192;

struct Label {
    ID3D11ShaderResourceView* view=nullptr;   // null: not made (no device, GDI or D3D failed)
    float w=0.f,h=0.f;                        // design units
};

class Labels {
public:
    Labels() noexcept;
    ~Labels() noexcept;
    Labels(const Labels&)=delete;
    Labels& operator=(const Labels&)=delete;

    // `text` `size` design units tall (the font's em), bold or not, in `lang`'s font, for `paint`'s device and scale;
    // marked drawn in `frame`.
    Label Get(worldmapgpu::Painter& paint,const wchar_t* text,float size,bool bold,hudtext::Lang lang,unsigned frame) noexcept;
    // White, alpha the shape, 64 x 64: a disc filling it, a ring 6 / 64 thick at its edge.
    ID3D11ShaderResourceView* Disc(worldmapgpu::Painter& paint) noexcept;
    ID3D11ShaderResourceView* Ring(worldmapgpu::Painter& paint) noexcept;
    // Every texture released (the select gone; with the renderer's Reset).
    void Reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace crew::worldmaptext
