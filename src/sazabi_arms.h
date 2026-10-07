// The Sazabi's arms' constants and state (src/sazabi_arms.inc runs them; sazabi.cpp's Mech owns an Arms).
#pragma once
#include "crew.h"
#include "sazabi_funnels.h"
#include <cstddef>
#include <cstdint>

namespace crew::szarms {
constexpr std::size_t kFireGun=0x2020,kFireMissile=0x2021;   // the 506's fire bytes (heli.cpp)
constexpr std::size_t kWeaponTrigger=0x139;                    // a weapon's trigger latch (stores.cpp TriggerStore)
constexpr std::uint64_t kHolderRifle=0,kHolderMissile=1;
constexpr float kRifleInterval=0.5f,kMissileInterval=2.5f;     // s: their SGOs' FireInterval (30, 150 frames)
constexpr float kAimRange=800.0f;                              // m: the reticle's farthest
// the tomahawk
constexpr float kSwingSec=0.8f,kAxeAhead=11.0f,kAxeReach=17.0f,kAxeHigh=16.0f;
constexpr int kAxeMostHits=6;
// the cannon
constexpr int kMegaBeams=5;
constexpr float kMegaFan[kMegaBeams]={-18.0f,-9.0f,0.0f,9.0f,18.0f};   // deg either side of the chest's aim
constexpr float kChargeSec=2.0f,kChargeLeast=0.25f,kMegaSec=1.5f,kCannonCool=6.0f,kMegaRange=700.0f;
constexpr float kMegaSize=8.0f,kGlowThin=0.6f,kGlowThick=4.0f;
// the funnels: their flight is sazabi_funnels.h's; the enemies they go for are within kFunnelRange of the mech, kept
// in kFunnelSlots slots (an enemy keeps its slot while it lives: a funnel keeps its target)
constexpr int kFunnelCount=sazabi::funnels::kCount,kFunnelSlots=4;
constexpr float kFunnelRange=320.0f;
constexpr int kMostTargets=32;
enum class Special : int { missiles, funnels, cannon, count };
using Funnel=sazabi::funnels::Funnel;
using FunnelPhase=sazabi::funnels::Phase;
struct Arms {
    Special special=Special::missiles;
    bool switchHeld=false,meleeHeld=false,secondaryHeld=false,queued=false,struck=false,whooshed=false;
    float swing=-1.0f,guard=0.0f,charge=0.0f,brace=0.0f,cannonCool=0.0f,megaLeft=0.0f;
    float megaDamage=0.0f,megaYaw[kMegaBeams]{};
    RoundObj glow{},mega[kMegaBeams]{};
    Funnel funnels[kFunnelCount];
    const void* funnelSlot[kFunnelSlots]{};   // the enemies the funnels go for (FlyFunnels)
    bool funnelsOut=false;                    // some were out last frame (their homecoming logged)
    std::int32_t rifleAmmo=-1,missileAmmo=-1,missiles=0;
    float rifleSince=1e3f,missileSince=1e3f,lockProgress=0.0f;
    bool hasAim=false,hasLock=false,aimHit=false,centred=false;
    float aimRange=0.0f;
    float aim[3]{},lock[3]{};
    bool stance[2]{true,true};   // each foot on the ground last frame (its footstep when it comes down)
};
}  // namespace crew::szarms
