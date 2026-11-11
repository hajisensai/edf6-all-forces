#pragma once
// The world map mission select (test site #46; docs/plan-2026-11-10-worldmap.md, docs/worldmap-select-re.md): the
// stock mission select (ui::HUiHQMissionSelect, the same object the offline HQ, the online room, the room search and
// the room creation open) keeps everything it does; its list is hidden and a satellite map of the whole Earth (the
// JPEG tile pyramid Mods/Plugins/EDF6VehicleCrew.worldmap.bin) is drawn under its remaining windows. src/worldmap.cpp.
//
// The base map, pan and zoom, the opening move; one pin a place, the routes from the current mission, the chosen
// place's panel (right, over the window skins' empty frame), choosing by left / right, pins and panel lines
// (docs/plan-2026-11-10-worldmap.md §5). Further overlays can draw and read input through the two hooks below.
#include "worldmap_gpu.h"
#include "worldmap_logic.h"

namespace crew {

// At load: the select's OnUpdate, Draw and destructor slots chained (REFUSED in the log, nothing patched, when the code
// is not as read). The tiles file is opened at the first select shown; without it the select stays stock.
bool InstallWorldMap() noexcept;

namespace worldmapui {

// One frame of the map as drawn: the view (worldmap_logic.h), the design screen (1920 x 1080, `px` the viewport's real
// width), the viewport in render target pixels, the pointer in design units (pointerShown: the game in front and
// the pointer over the screen), the select object (its fields: docs/worldmap-select-re.md §3).
struct Frame {
    worldmap::View view;
    worldmap::Screen screen{1920.f,1080.f};
    worldmapgpu::Viewport viewport{};
    worldmap::Pixel pointer{-1.f,-1.f};
    bool pointerShown=false;
    unsigned char* select=nullptr;
};

// Render thread, in the select's Draw: after the base map, before the stock draw (so the select's own windows and
// any dialog stay over what is drawn here). `paint` draws in design units.
using DrawHook=void(*)(worldmapgpu::Painter& paint,const Frame& frame,void* user);
// UI thread, in the select's OnUpdate, after the map's own pan / zoom of this frame and before the stock OnUpdate;
// `input`: the select owns the input this frame (no dialog, no transition over it). True: the hook used this frame's
// input (a click on a pin): the stock OnUpdate then runs without it (its ctx's ui::Input* null, §5.2).
using UpdateHook=bool(*)(const Frame& frame,bool input,float dt,void* user);

// Set once (at load, before any select opens); null: none.
void SetDrawHook(DrawHook hook,void* user) noexcept;
void SetUpdateHook(UpdateHook hook,void* user) noexcept;

}  // namespace worldmapui
}  // namespace crew
