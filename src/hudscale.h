// The plugin HUD's one size: how big a design pixel (a pixel at 1080 lines) is on the screen being drawn. Every
// size, line width, offset and font scale hud.cpp draws is a design value times this (docs/hud-re.md §0.1).
//
// The game's own HUD is laid out on the screen it renders to, not on the viewport it is drawn in: the follower
// gauge (0x804300) sizes its bar 31·uiW/1920 by 3·uiH/1080, its HUD text scales its font by (uiW/1920, uiH/1080)
// (0x94E24F, 0x95CE60), `ui` being the screen at *(*(EDF+0x2137090)+0x10) +0x20 / +0x24 (int). A split screen's
// viewport is part of that screen and its HUD keeps the full screen's size; the plugin's does the same. A uniform
// scale (the plugin draws circles and text it must not stretch) follows the height, as the plugin always did: on
// 16:9 that is exactly the game's, on a wider screen the game's text is stretched across and ours is not.
//
// Pure (no EDF.dll, no Windows): tools/hud_view.cpp checks it against the viewports below.
#pragma once

namespace crew::hudscale {
constexpr float kDesignW=1920.0f,kDesignH=1080.0f;
// The ini's HudScale (the player's own factor on top) is held to this.
constexpr float kUserMin=0.5f,kUserMax=3.0f;
// A screen side the game could not have: the screen read is not one (unreadable, not set yet), the viewport's used.
constexpr int kMaxSide=16384;

// The height the HUD is laid out on: the game's screen (uiW x uiH) when it holds the viewport (viewW x viewH), else
// the viewport's own (the screen not read, or not a screen the viewport fits in).
inline float BasisHeight(int uiW,int uiH,int viewW,int viewH) noexcept {
    const bool screen=uiW>0 && uiH>0 && uiW<=kMaxSide && uiH<=kMaxSide && viewW<=uiW && viewH<=uiH;
    return static_cast<float>(screen ? uiH : viewH);
}
inline float ClampUser(float user) noexcept {
    if(!(user==user))return 1.0f;   // NaN: none of the player's own
    return user<kUserMin ? kUserMin : user>kUserMax ? kUserMax : user;
}
// The HUD's scale: design pixels to screen pixels, the player's factor included.
inline float Of(int uiW,int uiH,int viewW,int viewH,float user) noexcept {
    return BasisHeight(uiW,uiH,viewW,viewH)/kDesignH*ClampUser(user);
}
// Interactive map panels must fit inside their own viewport, including split-screen.
// This only caps the common requested scale; text and geometry use the same result.
inline float FitMap(float requested,float width,float height) noexcept {
    const float horizontal=width/960.0f,vertical=height/kDesignH;
    const float limit=horizontal<vertical ? horizontal : vertical;
    return requested<limit ? requested : limit;
}
// A font scale for a design font scale (the game's glyphs are rasterized at a fixed size, docs/hud-re.md §2.1:
// the font is scaled as everything else is).
inline float Font(float design,float s) noexcept { return design*s; }
}  // namespace crew::hudscale
