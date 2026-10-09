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
constexpr float kAxeAhead=11.0f,kAxeReach=17.0f,kAxeHigh=16.0f;
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
// A trigger press as the weapon is asked for it: held, or after a tap until its round leaves (the animator may first
// have to put the tomahawk away and raise the rifle / turn the shield, kAskMost at the longest). A release once a
// round has left ends it: a tap fires one round, a release fires no extra one.
constexpr float kAskMost=1.0f;
struct Ask {
    float left=0.0f;
    bool held=false,shot=false;
    bool Step(bool pressed,float dt) noexcept {
        if(pressed) { if(!held)shot=false; left=kAskMost; }
        else left=held && shot ? 0.0f : (left>dt ? left-dt : 0.0f);
        held=pressed;
        return left>0.0f;
    }
    void Shot() noexcept { shot=true; if(!held)left=0.0f; }
    bool On() const noexcept { return left>0.0f; }
};
struct Arms {
    Special special=Special::missiles;
    bool meleeHeld=false,secondaryHeld=false,funnelHeld=false,cannonHeld=false,guardHeld=false,dashHeld=false;
    bool queued=false,struck=false,whooshed=false;
    int combo=0;                  // the combo's swing now (sazabi_pose.h kCombo: diagonal cut, slash across, overhead chop)
    int blocksHeard=0;            // the shield's blocks already sounded (ArmsStep)
    float blockSince=1.0f;        // s since the last block's clang (a burst of bullets: kBlockSoundGap apart at most)
    Ask rifleAsk,missileAsk;      // the triggers as asked for (ArmsStep), each ended by its round (ArmsFire)
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
    // the aim assist (sazabi_camera.inc Assist): the enemy picked round the reticle and its lock point this frame
    const void* assistObj=nullptr;
    bool hasAssist=false;
    float assist[3]{};
    // the lock-on (sazabi_camera.inc LockInput): the enemy held while it lives, its key's state, the flick toward the
    // next one, how long it has been out of sight
    ObjRef lockOnTarget{}; // owns one weak via AssignLockTarget; clear before overwriting Arms/Mech
    bool lockOn=false,lockKeyHeld=false;
    float flick=0.0f,flickCool=0.0f,lockHidden=0.0f;
    bool stance[2]{true,true};   // each foot on the ground last frame (its footstep when it comes down)
};
}  // namespace crew::szarms
