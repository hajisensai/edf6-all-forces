// The Sazabi's arms' constants and state (src/sazabi_arms.inc runs them; sazabi.cpp's Mech owns an Arms).
#pragma once
#include "crew.h"
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
// the funnels
constexpr int kFunnelCount=6;
constexpr float kFunnelSec=15.0f,kFunnelCool=20.0f,kFunnelRange=320.0f,kFunnelOrbit=28.0f,kFunnelOver=12.0f;
constexpr float kFunnelSpring=3.0f,kFunnelDamp=2.6f,kFunnelSpeed=90.0f,kFunnelShotSec=1.6f,kFunnelNear=15.0f;
constexpr float kFunnelHome=10.0f,kFunnelHomeHigh=28.0f,kFunnelLaunchGap=0.12f,kFunnelDock=1.5f,kFunnelBackMost=6.0f;
constexpr int kMostTargets=32;
// the thrusters: the backpack's two throats (the model's thruster glow, tools/prep_sazabi.py's model folder), from the
// backpack's joint in its frame, and the way each points
constexpr float kNozzleAt[2][3]={{0.54f,-4.63f,-4.84f},{-0.54f,-4.63f,-4.84f}};
constexpr float kNozzleDir[2][3]={{0.31f,0.22f,-0.92f},{-0.31f,0.22f,-0.92f}};

enum class Special : int { missiles, funnels, cannon, count };
enum class FunnelPhase : int { docked, launching, out, back };
struct Funnel {
    FunnelPhase phase=FunnelPhase::docked;
    float at[3]{},vel[3]{},dir[3]{0.0f,0.0f,1.0f};
    const void* target=nullptr;
    float aim[3]{};
    float shotIn=0.0f,wait=0.0f,backFor=0.0f;
};
struct Arms {
    Special special=Special::missiles;
    bool switchHeld=false,meleeHeld=false,secondaryHeld=false,queued=false,struck=false,whooshed=false;
    float swing=-1.0f,guard=0.0f,charge=0.0f,brace=0.0f,cannonCool=0.0f,megaLeft=0.0f,funnelCool=0.0f,funnelLeft=0.0f;
    float megaDamage=0.0f,megaYaw[kMegaBeams]{};
    RoundObj glow{},mega[kMegaBeams]{};
    Funnel funnels[kFunnelCount];
    std::int32_t rifleAmmo=-1,missileAmmo=-1,missiles=0;
    float rifleSince=1e3f,missileSince=1e3f,lockProgress=0.0f;
    bool hasAim=false,hasLock=false,aimHit=false,centred=false;
    float aimRange=0.0f;
    float aim[3]{},lock[3]{};
    bool stance[2]{true,true};   // each foot on the ground last frame (its footstep when it comes down)
};
}  // namespace crew::szarms
