// The aircraft's HUD drawn without the game, to look at its layout: src/hud.cpp included whole, its draw run on a
// stand-in "EDF.dll" image whose quad and text functions (the RVAs hud.cpp calls) jump to recorders here, for a few
// scenes of the warnings (warn.h) on a jet (one low on fuel: the fuel readout that replaces the stock FUEL gauge, LOW
// FUEL lit), a rotor craft and a stock heli, and the stock vehicles' HUD (a tank, the drill tank, a Nix, the Proteus
// walking behind its front shield and deployed with its field, barrier and a mark; at 16:9 and 21:9) under threat. Each
// scene's quads (as triangles) and text
// lines go to DIR/<scene>.txt; tools/hud_view.py turns them into PNGs. The text's size is a stand-in (the game's
// glyphs are not here: kGlyphH px a unit of font scale, kGlyphW of that a character), so read the layout, not the
// lettering.
//
// The stock HUD's layout is also checked: the RWR scope's box and the hull / turret block's (StockBlock, with its drill
// line, and with the Proteus's lines and bars) must not overlap at 16:9 or 21:9, nor leave the screen; exit code 1 when
// they do.
//
// The HUD's scale (src/hudscale.h) is checked too: hudscale::Of on the typical screens and split viewports, and the
// stock HUD drawn whole (HudDraw) at 1280x720 .. 3840x2160 and in split viewports, its text and its block / RWR scope
// against the 1080-line design times the scale, in size and in place; exit code 1 when one is off.
//
// Every scene is drawn in each of the HUD's languages (src/hudtext.h): English into DIR, the others into DIR\<language>
// (zh-CN, zh-TW, ja). The stand-in text is as wide as the game's fonts make it (the game's font chain, docs/hud-re.md
// §11: a CJK character a full em; Latin half an em in English and Japanese, whose first font is the monospaced New
// Cezanne, and the Chinese fonts' own advances in Chinese). In each language every scene's text must stay on the
// screen, and no two of its lines may overlap that do not in English; the layout checks above run in each language too.
// Each scene's glyphs (character and font scale: the game's glyph cache keys, §2.1) are counted.
//
//   hud_view [--out DIR] [--lang en|zh-CN|zh-TW|ja]      (default %TEMP%\edf6_hud_view, every language), then:
//   python tools/hud_view.py DIR
#include "../src/hud.cpp"
#include "../src/map_cam.h"
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace crew {
unsigned char* image=nullptr;
PlayerFix player{};
namespace {
Config config{};
// The scene: what the stubs hand the HUD.
bool hasTurret=false;
edf::aimlink::TurretReadoutV1 sceneTurret{};
bool hasJet=false,hasHeli=false,hasWarn=false,hasStock=false,hasDrill=false,hasNix=false,hasMap=false,hasEmc=false,hasProteus=false;
bool paused=false;   // the game's pause flag (crew.cpp GamePaused)
// When the scene's readouts were taken (the game thread's tick): before HudDraw takes its own `now`. The gear's read the
// tick in the middle of the draw, a 16 ms tick later at times: `now - tick` wrapped and the gear panel was not drawn.
ULONGLONG sceneTick=0;
bool mapEasing=false;   // the map's camera easing back to the player: its view, no readout (map.cpp Camera)
