// qmark.cpp: the custom Q mark that replaces the stock spot (the user, 2026-10-10: "给原版的q删掉吧，改成我们自制的q。同时这个q
// 会在队友那边显示，还有这个q应该在鼠标位置"; "声音也要有吧"). The key and what it marks are npcai.cpp's (NpcMarkFrame:
// the enemy nearest the aim, else a point on the ground, the map's selection sent there); this file casts the aim's ray
// (qmark_ray.h), keeps the point mark, shares this machine's marks with the teammates and shows theirs (qmark_protocol.h,
// over support_net.cpp's transport), publishes every mark for the HUD and plays the cues (jetaudio.cpp clips).
// Included by crew.h.
#pragma once
#include <cstddef>
#include <cstdint>

namespace crew {
// The custom Q on (the plugin on and NpcMarkKey set): on foot and riding, with the custom NPC AI or without (the NPCs'
// priority on the mark is the AI's alone, npcmark::Enabled).
bool QMarkOn() noexcept;
// The mark's ray for the local player `human` (game thread): the eye of the camera drawn for it, through the mouse's aim
// point where the player's mode has one apart from the view (qmark_ray.h), else the screen's centre. False: no camera.
bool QMarkRay(const unsigned char* human,float* eye,float* dir) noexcept;
// This machine's point mark (no enemy aimed at): set at `at` for QMarkPointSec.
void QMarkSetPoint(const float* at) noexcept;
// A cue to play on the next frame: this machine's player marked, a teammate marked, a mark let go.
enum class QMarkCue : int { own, team, off };
void QMarkPlay(QMarkCue cue) noexcept;
// Every frame of a local player (npcai.cpp NpcMarkFrame, game thread; once a frame however many local players): the
// teammates' marks kept (their enemies found here, moving), the HUD's marks published, the cues played.
void QMarkFrame() noexcept;
// A new mission: no marks, ours or theirs.
void ResetQMarks() noexcept;

// The transport (support_net.cpp, game thread): each tick with the room's state (`send` to peer 1..peers, the same
// as the command messages'); a received payload (true: it was a Q mark's, taken or dropped); the session gone.
void UpdateQMarkNetwork(bool ready,std::uint32_t peers,bool (*send)(std::uint32_t,const void*,std::size_t) noexcept,
                        std::uint64_t now) noexcept;
bool ReceiveQMarkNetwork(std::uint32_t peer,const char* authenticatedPuid,const void* bytes,std::size_t size,
                         std::uint64_t now) noexcept;
void ResetQMarkNetwork() noexcept;

// The marks for the HUD (draw thread; hud.cpp QMarkHud, the map's view too): `own` this machine's player's; `enemy` an
// enemy (else a point on the ground); `slot` the marker's mission player slot (-1 not known); `at` where it is.
struct QMarkView { bool own,enemy; int slot; float at[3]; };
constexpr int kQMarkViews=16;
int QMarkViews(QMarkView* out,int max) noexcept;
}  // namespace crew
