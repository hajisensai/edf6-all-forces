#pragma once
#include <cstdint>
namespace crew {
// Wire namespace is outside every vanilla/Coop SEARCH_TYPE range. The low byte
// preserves the room kind and Coop capacity family; protocol changes get a new prefix.
// The room protocol: every machine in a room runs the same wire protocols (PvP, support, room sync). A change any of
// them cannot read takes the next number: an older build then neither finds nor joins the room (its own namespace
// check), and this one refuses the older build's rooms. No compatibility with older builds (CLAUDE.md).
//   1: up to 2026-10-25;  2: the named PvP factions (pvp_protocol.h v3, team_native.h's 16 teams);
//   3 (2026-10-26): every support from the map's bar for pt (the support catalog with the strikes, support_call.h; the
//      barrages' BRGS channel; the support call weapons retired from the weapon table);
//   4 (2026-10-27): the support pt shared by a team, kept by the host (support_points_net.h: the SPTS channel).
//   5 (2026-10-30): the X-Morph infantry kind (SupportWeapon 14; an older build rejects the composed loadout), the
//      support channel's re-handshake keeping its transactions (support_protocol.cpp), the gunner seats' STUR channel.
//   6 (2026-10-31): every channel names a player by its mission slot (net_object_ref.h), not its NetId (a stock
//      mission numbers those per machine; the support wire keeps v2 for the version notice, its plans' requester changed
//      with PR #300): map commands v8, PVEH v2, BRGS v2, ULTS v2, QMRK v2, GRVT/FGUN v3, enemy control v5; the EDF4.1 /
//      EDF5 BVM seeds each player's NetId from its mission slot (edf5online.cpp, PR #299).
//   7 (2026-10-31): every object on the wire is its reference (net_object_ref.h): a player by its slot with the tag at
//      bytes 20..23 (the targets too: a teammate grabbed, a player marked or attacked), anything else by its NetId; an
//      ultimate's / barrage's caller sent fallen too: QMRK v3, GRVT/FGUN v4, ULTS v3, BRGS v3, map commands v9, PVEH v3,
//      enemy control v6 (its projectiles' targets and the Deiroi / monster codecs v2); the support wire keeps v2.
//   8 (2026-11-08): a support soldier carries the requesting player's weapon one tier below its own and a player's attack
//      coefficient (docs/feature-npc-weapon-tier.md): a request carries the requester's weapons, a unit its own
//      (support_protocol.cpp, Unit::arms; the support wire keeps v2), every peer builds the soldier with it;
//      and the support deployment's strike jet, fighter, interceptor and multirole fly bodies of their own in the
//      model pack's Dreadnought models (jet_internal.h Body::sup*, marks 7061-7064; every machine makes its own copy of a
//      support aircraft from the plan, so an older build would make the stock-bomber one under another mark).
//   9 (2026-11-10), several changes in one step. A mission vehicle's crew (the host's player's weapons), a sea rescue's crew (the swimmer's) and the
//      X-Morph infantry (the Ranger's assault rifles) carry player weapons too (docs/feature-npc-weapon-tier.md 2.4): a
//      crew plan's units carry arms an 8 would build as stock soldiers, an X-Morph arms word an 8 refuses (the support
//      wire keeps v2).
//      And the support vehicles' and aircraft's weapons deal a request's damage at the online story's weapon level
//      cap for the room's difficulty and mission (support_firepower.h, docs/feature-npc-weapon-tier.md §4.1), not the
//      friend attack coefficient: every machine makes its own copy, so an older build's copies would hit by another rule.
//      And the map's DISMOUNT ALL carries its choice (all but the driver, some classes' riders: map commands
//      v10, mapcmd_logic.h DismountParamValid), and the squads following a player ride along with that player
//      (src/ride_along.h; the host runs the guests' squads): an older build in the room would refuse the order or leave
//      a guest's squad on foot.
//      And the support's gunship and paratroop plane fly the model pack's C-130s (jet_internal.h Body::c130Gunship,
//      mark 7065; Body::transportPlane, now mark 7066): an older build would make the bomber401 ones under other marks.
//  10 (2026-11-10): the support's air carrier and blast / doll drone carriers fly the model pack's Helicarrier (jet_internal.h
//      Body::supCarrier / supBlastCarrier / supDollCarrier, marks 7067-7069): an older build would make the V508 ones under
//      the requested twins' marks 7005 / 7009 / 7010.
//      And the Andryusha's fuse switch goes to the vehicle's copies (andryusha_net.h 'AFUZ' on tag 15): every
//      machine writes the rider's fuse into its copy of the launcher, so its carriers burst and split alike. An older
//      build ignores the block (seat_net.h) and keeps its copy's impact fuse: its bomblets would fall elsewhere.
//  11 (2026-11-21): underground an infantry call comes in a drill transport (support_spawn.h kSupportDrillTransport,
//      resource 0x20100; docs/insertion-re.md §5): an older build's Validate refuses an infantry plan that has a hull, so
//      the transaction would fail with no reason anyone sees.
//  12 (2026-11-21): every member's own support loadout applies to the supports that member calls (member_config.h): every
//      member publishes it in the room (EDF6X_VCCFG, room_sync_logic.h; the host's file list EDF6X_VCVARIANTS and the
//      pack marker EDF6X_VCPACKS are gone), every machine generates every member's files, and the host plans a member's
//      request with that member's loadout (support_dispatch.cpp PlanForRequester). An older build would neither publish
//      its loadout (its calls would come with the defaults) nor generate the others' files (stock where a plan wants them).
//  13 (2026-11-21): commander mode adds a catalog and host settings field. Older builds cannot negotiate it.
//  14 (2026-11-21): custom mechs add a vehicle body to PVEH v4. Every member must use the same channel version.
//  15 (2026-11-21): host settings v3 synchronizes scope spread and support wire v3 carries both Fencer hands.
//  16 (2026-11-24): every member needs EDF6Coop 2.8.0 (batch poll, recoverable overflow) and sends enemy-control and
//      gravity states by logic frame; a 15 build would sit in the room on an EDF6Coop whose overflow ends its networking.
//  17 (2026-11-24): net-6, the per-feature compatibility layer removed: no extension bits in the support handshake
//      (kExtVariants alone, runtime state), no plugin_net roster / PeerHas, no command protocol negotiation (v10 only),
//      no "older build" notices. Every channel sends to every peer: a 16 would be sent what it cannot read.
//      Same number, net-9: enemy-control protocol 8 / pose wire 2 (each pose sample carries the host's frame and
//      measured motion; the guest interpolates it per logic frame).
//      Same number, net-5: every member needs EDF6Coop 2.9.0 (extension ABI 2, coop_attr.h kRoomCoop*); the per-frame
//      states (enemy-control pose, gravity hold / flight, ultimate keep) go as state messages (plugin_net.h SendState).
//  18 (2026-11-24): host settings v4 carries the preload set (host_settings_logic.h n= / p=): every member preloads the
//      host's room set of the plugin's content instead of all of it, so an older build (which preloads everything but
//      never says what it needs) would leave the host's set without its calls' bodies on the other machines.
// EDF6Coop reads both values from All Forces (EDF6AF_RoomPrefix / EDF6AF_RoomProfile) rather than keeping its own.
constexpr std::int64_t kAllForcesRoomProtocol=19;  // BRGS v5 impact timing and SP v4 attack-run geometry
constexpr std::int64_t kAllForcesRoomPrefix=0x41460000+(kAllForcesRoomProtocol<<8);
constexpr std::int64_t kAllForcesRoomInvalid=-1;
constexpr bool AllForcesWireType(std::int64_t value) noexcept {
    return value>=kAllForcesRoomPrefix && value<kAllForcesRoomPrefix+0x100;
}
constexpr std::int64_t EncodeAllForcesRoom(std::int64_t value) noexcept {
    return value>=0 && value<0x100 ? kAllForcesRoomPrefix+value : value;
}
constexpr std::int64_t DecodeAllForcesRoom(std::int64_t value) noexcept {
    return AllForcesWireType(value) ? value-kAllForcesRoomPrefix : kAllForcesRoomInvalid;
}
// The room protocol of another build's All Forces room (its SEARCH_TYPE 0x4146PP00+kind, PP not this one's): PP, or 0
// for anything else (a vanilla room, this protocol's own). What the join refusal tells the player (mod_room.cpp).
constexpr std::int64_t OtherAllForcesProtocol(std::int64_t value) noexcept {
    const std::int64_t protocol=(value-0x41460000)>>8;
    return value>=0x41460000 && value<0x41470000 && protocol>0 && protocol!=kAllForcesRoomProtocol ? protocol : 0;
}
static_assert(OtherAllForcesProtocol(0x41460000+(15<<8)+0x93)==15 && OtherAllForcesProtocol(kAllForcesRoomPrefix+0x93)==0 &&
              OtherAllForcesProtocol(0x93)==0,"another protocol's room told apart");
bool InstallModRoom() noexcept;
// The mission this machine's room last published (the game's DLC and MISSION_NO attributes, as the host publishes them
// whenever it picks one): false until it published both. Any thread.
bool PublishedRoomMission(std::int64_t* content,std::int64_t* mission) noexcept;
}
