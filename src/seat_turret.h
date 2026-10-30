// A vehicle's own turrets (StoreSpec::turret, docs/independent-seat-re.md, docs/multi-weapon-keys.md): a gunner seat's
// weapon on its own yaw / pitch bones, which the plugin turns. Its seat's rider trains it: the local player toward the
// screen's centre, an NPC gunner this machine runs toward its target (SeatTurretsTrain); nobody in it, an auto turret on
// the machine that runs the vehicle trains itself on the nearest enemy and fires (payload.cpp UnmannedTurretsFire), with
// the vehicle's auto turrets on. Every other copy turns it as its trainer's machine says (tag 15 'STUR':
// seat_turret_net.h). The rounds follow the bones: the native shot copies its holder bone's world (0x630250).
#pragma once

namespace crew {
// Game thread, each frame of a vehicle (crew.cpp, after PayloadFrame): its turrets trained one step and sent.
void SeatTurretsFrame(unsigned char* vehicle) noexcept;
// The V603's final world build (vulcan_rotor.cpp Build, before 0x1100B90): its turrets' bones posed.
void SeatTurretsPose(unsigned char* vehicle) noexcept;
// The target of the NPC gunner of seat `seat` this frame (npcai.cpp CrewOwnWeapons; nullptr none).
void SeatTurretsTrain(unsigned char* vehicle,unsigned seat,const void* target) noexcept;
bool InstallSeatTurrets() noexcept;
void ResetSeatTurrets() noexcept;
}  // namespace crew
