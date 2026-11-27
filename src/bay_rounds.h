// The rounds the plugin fires through a bay (jet_bay.cpp): impacts, the drill's charge, the EMC's and the
// barrages' rounds, the Dreadnought's.
#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace crew {
// An impact `by` a vehicle (a crash, jet.cpp / playerjet.cpp; a ground vehicle's ram, vehicleram.cpp) at `at`: `damage`
// to the enemies of its side within about `radius` metres (the charge nearest that size: vehicleram.h NearestCharge; a
// charge of the vehicle's own, as the blast drones' is: its team, its kills, friends untouched). False when it could not
// be dealt (no charge preloaded this mission).
bool ImpactDamage(const unsigned char* by,const float* at,float damage,float radius) noexcept;
// jet_bay.cpp: a bite of the drill tank's drill: its charge fired by `by` straight from `from` at `at` with `damage`
// (its side's enemies, its kills, the map's buildings and rocks). False when not fired (not preloaded this mission).
bool DrillCharge(const unsigned char* by,const float* from,const float* at,float damage) noexcept;

// jet_bay.cpp: the EMC's rounds (emc.cpp; pylib/vcobjects.py EMC_*, tools/make_emc.py), DemoIndirectFire objects owned
// by the EMC (its team: its side's enemies hurt, its kills, friends spared): the beam, the charge's glow (sight), the
// break charge fired at each building on the beam's line, the blast at its end. Ready: preloaded this mission.
// The Sazabi's beams are made and fired the same way (sazabi.cpp; pylib/vcobjects.py SAZABI_ROUND_FILES): the mega
// particle cannon's beam, its charge's glow at the chest, a funnel's burst. The Proteus's shield is raised the same way
// (proteus_shield.inc; tools/make_proteus.py): one BarrierBullet01, the stock Air Raider's electromagnetic barrier.
enum class EmcRound { beam, sight, breakCharge, blast, szMega, szCharge, szFunnel, proteusShield,
                      ultSight, ultBolt, ultSpark, ultBeam, ultCore, ultBlast, ultGather, barrage, barrageBurst, barrageMissile,
                      andryushaBlast, andryushaFire };
struct RoundObj { unsigned char* obj; const void* ctrl; };   // an object and its weak-this control block (none: obj null)
bool EmcRoundReady(EmcRound kind) noexcept;
RoundObj EmcFire(EmcRound kind,const unsigned char* by,const float* from,const float* at,float damage) noexcept;
bool RoundSteer(const RoundObj& r,const float* from,const float* at) noexcept;   // its next rounds' start and aim; false: gone
bool RoundSize(const RoundObj& r,float size) noexcept;       // its next rounds' thickness (AmmoSize)
bool RoundBlast(const RoundObj& r,float radius) noexcept;    // its next rounds' blast radius (AmmoExplosion)
void RoundDrop(RoundObj& r) noexcept;                        // deleted while it is there; forgotten
bool EmcIfcOk() noexcept;                                    // RoundSize / RoundBlast's IFC fields are where they write

// jet_bay.cpp: a long-range barrage's round (barrage.cpp; EmcRound::barrage): fired by `by` from `from` toward `aim` with
// `damage`, its AmmoSize `size`, blast `blast` m and AmmoHitSizeAdjust `mark` (the plugin's flight tells it by that); a
// cluster's flash where its bomblets leave (EmcRound::barrageBurst, no damage). False: not fired (not preloaded, the IFC's
// fields not where they are written).
bool BarrageFire(const unsigned char* by,const float* from,const float* aim,float damage,float size,float blast,float mark,bool missile) noexcept;
void BarrageBurst(const unsigned char* by,const float* at) noexcept;
// jet_bay.cpp: one of the Andryusha's bomblets (andryusha.cpp; docs/andryusha-re.md): two rounds of `by` on one arc from
// `from` onto `aim` (the IFC's own ballistic solve, both at one speed and gravity): its blast (EmcRound::andryushaBlast,
// GrenadeBullet01, `blast` damage) and its incendiary (EmcRound::andryushaFire, NapalmBullet01: the ground fire where it
// lands, `fire` damage). False: neither fired (not preloaded this mission).
bool AndryushaBomblet(const unsigned char* by,const float* from,const float* aim,float blast,float fire) noexcept;
// `real`: the gunner's own round (its damage); false: a remote gunner's, seen here (no damage: theirs is theirs).
bool DreadnoughtRound(unsigned char* owner,int round,const float* from,const float* at,bool real) noexcept;
}  // namespace crew
