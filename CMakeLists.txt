cmake_minimum_required(VERSION 3.20)
project(EDF6VehicleCrew VERSION 0.8.0 LANGUAGES CXX)
set(EDF6_GAME_DIR "" CACHE PATH "Optional EDF6 game directory for read-only generated-asset checks")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(EDF6VehicleCrew SHARED src/plugin.cpp src/crew.cpp src/heli.cpp src/ground.cpp src/jet.cpp src/jet_flight.cpp src/jet_combat.cpp src/jet_carrier.cpp src/jet_bay.cpp src/jet_spawn.cpp src/jet_hooks.cpp src/airstrike.cpp src/loadout.cpp src/subcarrier.cpp src/carrierlaser.cpp src/overlay.cpp src/hud.cpp src/launcher.cpp src/katyusha.cpp src/highcam.cpp src/helisight.cpp src/jetsound.cpp src/jetaudio.cpp src/playerjet.cpp src/missile.cpp src/stores.cpp src/bigworld.cpp src/legacymap.cpp src/edf41sound.cpp src/edf41ai.cpp src/edf41classes.cpp src/edf41mother.cpp src/edf41fortress.cpp src/edf41dragon.cpp src/fieldshare.cpp src/jetprops.cpp src/booster.cpp src/shield.cpp src/view.cpp src/mission.cpp src/mission_watch.cpp src/crash_dump.cpp src/body506.cpp src/physics.cpp src/drill.cpp src/gear.cpp src/netprobe.cpp src/glyphs.cpp src/edf5campaign.cpp src/edf5online.cpp src/turretaim.cpp src/rounds.cpp src/vhud.cpp src/stockgauge.cpp src/warn.cpp src/payload.cpp src/seatswitch.cpp src/sidecar.cpp src/turretcam.cpp src/vehsound.cpp src/sightzoom.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/edf41robo.cpp)
# The Nix's torso twist (its own line: the source list above is where every feature branch adds).
target_sources(EDF6VehicleCrew PRIVATE src/nix.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/edf41mother401.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/edf41deiroi.cpp)
# Every class in every seat (ini AllClassesRide; its own line, as above).
target_sources(EDF6VehicleCrew PRIVATE src/allclass.cpp)
# CRI File System's loader pool sized for the plugin's content (its own line, as above).
target_sources(EDF6VehicleCrew PRIVATE src/fsbudget.cpp)
# The ground vehicles' ram (its own line, as above).
target_sources(EDF6VehicleCrew PRIVATE src/vehicleram.cpp)
# The gravity operations (src/gravity.cpp, the pure part src/gravity_logic.h checked by tools/gravity_check.cpp; its own line).
target_sources(EDF6VehicleCrew PRIVATE src/gravity.cpp)
# The gravity gun (src/gravitygun.cpp: tools/calls.py EDF6VC_CALL_GRAVITY_GUN, the Laser Guide Kit that grabs and throws).
target_sources(EDF6VehicleCrew PRIVATE src/gravitygun.cpp)
# Their online sharing (src/gravity_net.cpp, the float guns' too: the wire src/gravity_net_protocol.cpp, tests/gravity_net_test.cpp).
target_sources(EDF6VehicleCrew PRIVATE src/gravity_net.cpp src/gravity_net_protocol.cpp)
# The plugin's online message channels (src/plugin_net.cpp: the registry every plugin-to-plugin message goes through, the
# host's roster of the peers' features; the pure part src/plugin_net_logic.h and src/wire.h, tests/plugin_net_test.cpp).
target_sources(EDF6VehicleCrew PRIVATE src/plugin_net.cpp)
# The float guns (src/floatgun.cpp: EDF5's last mission's dropped colonist guns and vehicle guns that float round the player).
target_sources(EDF6VehicleCrew PRIVATE src/floatgun.cpp)
# The gun stabilizer (its own line, as the Nix's).
target_sources(EDF6VehicleCrew PRIVATE src/stab.cpp)
# A remote gunner's recoil on the vehicle's authority (src/gunnerrecoil.cpp, docs/online-re.md §10).
target_sources(EDF6VehicleCrew PRIVATE src/gunnerrecoil.cpp)
# The map view (src/map.cpp: the overhead camera, the input hold, the marks it gathers).
target_sources(EDF6VehicleCrew PRIVATE src/map.cpp)
# A soldier's zoomed scope as a real optic (src/infantryscope.cpp: the cue, the stock crosshair's draw held back).
target_sources(EDF6VehicleCrew PRIVATE src/infantryscope.cpp)
# First person on foot (src/firstperson.cpp: the camera at the head, the arms and weapon drawn as a viewmodel; the
# arithmetic in src/fp_view.h, checked offline by tools/fp_view_check.cpp).
target_sources(EDF6VehicleCrew PRIVATE src/firstperson.cpp src/fp_vehicle.cpp src/fp_skin_gpu.cpp src/vehicle_look.cpp)
# The damage statistics (src/damagestats.cpp: the damage hook and the round tags; the page in src/hud_stats.inc).
target_sources(EDF6VehicleCrew PRIVATE src/damagestats.cpp)
# The EDF4.1 port's kill counters in the battle record (src/killcount.cpp; docs/kill-counter-re.md).
target_sources(EDF6VehicleCrew PRIVATE src/killcount.cpp)
# Fire that cannot hurt its target (src/wastedfire.cpp, src/wasted_fire.h): fed by the damage hook, asked by the AI.
target_sources(EDF6VehicleCrew PRIVATE src/wastedfire.cpp)
# The map's NPC commands (src/mapcmd.cpp, src/mapcmd_logic.h).
target_sources(EDF6VehicleCrew PRIVATE src/mapcmd.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/support_dispatch.cpp)
# The squads' transports (src/transport.cpp, rules in src/transport_logic.h; tools/transport_check.cpp).
target_sources(EDF6VehicleCrew PRIVATE src/transport.cpp)
# The container airdrops (src/airdrop.cpp, the stock container's data and the carrier's decisions in src/airdrop_logic.h;
# tools/airdrop_check.cpp).
target_sources(EDF6VehicleCrew PRIVATE src/airdrop.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/support_policy.cpp)
# Out-of-mission support configuration: callable units and their crews' stock weapons (src/support_config.h).
target_sources(EDF6VehicleCrew PRIVATE src/support_config.cpp)
# The EMC's charged beam (src/emc.cpp, src/emc_plan.h).
target_sources(EDF6VehicleCrew PRIVATE src/emc.cpp)
# The ultimate strike (src/ultimate.cpp, src/ultimate_plan.h).
target_sources(EDF6VehicleCrew PRIVATE src/ultimate.cpp)
# The Proteus rework (its own line too).
target_sources(EDF6VehicleCrew PRIVATE src/proteus.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/proteus_net.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/boarding.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/primer.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/sazabi.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/dreadnought.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/sazabi_sound.cpp)
# What a blocked jet ran into, logged (src/impact.cpp).
target_sources(EDF6VehicleCrew PRIVATE src/impact.cpp)
# The map's real play area: the walls where its ground ends (src/playarea.cpp, src/playarea.h).
target_sources(EDF6VehicleCrew PRIVATE src/playarea.cpp)
# The stock split missiles (MissileBullet02) split short of a big target's surface.
target_sources(EDF6VehicleCrew PRIVATE src/splitmissile.cpp)
# Support units (SupportUnitBullet01) stick to allies instead of passing through them.
target_sources(EDF6VehicleCrew PRIVATE src/supportunit.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/route_guide.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/edf41natives.cpp)
# The DLC's EDF4.1 small dragons: the hit reaction on 4.1's ragdoll bodies (src/edf41smalldragon.cpp).
target_sources(EDF6VehicleCrew PRIVATE src/edf41smalldragon.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/edf41insects.cpp)
# Which machine runs the plugin's work on an object online (src/online_authority.h, docs/online-re.md section 3).
target_sources(EDF6VehicleCrew PRIVATE src/online_authority.cpp)
# The EDF4.1 classes online (src/edf41net.h, docs/edf41-classes.md 联机): child identities, the host's decisions as events.
target_sources(EDF6VehicleCrew PRIVATE src/edf41net.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/drill_net.cpp src/seat_net.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/sazabi_net.cpp)
# Every stock homing round (MissileBullet01/02, HomingLaserBullet01) steers by proportional navigation (src/pn.h).
target_sources(EDF6VehicleCrew PRIVATE src/guidance.cpp)
# Every lock-on weapon's lock: the order it locks in (the player's nearest the view), the target cycle, reading it.
target_sources(EDF6VehicleCrew PRIVATE src/lockon.cpp)
# The Tempest cruise missiles flown from their nose (TV guidance; the view and the hold are the map's).
target_sources(EDF6VehicleCrew PRIVATE src/tvguide.cpp)
# The friendly NPC soldiers' own AI (src/npcai.cpp, decisions in src/npc_logic.h; docs/npc-ai-design.md).
target_sources(EDF6VehicleCrew PRIVATE src/npcai.cpp src/npcpost.cpp src/npc_backpack.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/ground_navigation.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/mission_crew_support.cpp src/mission_crew_team.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/support_soldier.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/npc_gunner_aim.cpp src/real_driver_native.cpp src/mission_crew.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/command_unit.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/support_spawn.cpp)
# The generated variant files (coloured soldiers, loaded vehicles) a mission preloads (src/support_variants.h).
target_sources(EDF6VehicleCrew PRIVATE src/support_variants.cpp)
# The debug spawn tool (ini DebugSpawn, off by default; src/debug_spawn.h, docs/debug-spawn.md).
target_sources(EDF6VehicleCrew PRIVATE src/debug_spawn.cpp)
# The in-game loadout menu's saving: the ini writer, the background variant generator (src/loadout_store.h).
target_sources(EDF6VehicleCrew PRIVATE src/loadout_store.cpp src/loadout_file.cpp src/room_preview.cpp src/room_sync.cpp src/room_ui.cpp src/room_dialog.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/outfit41.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/coop_attr.cpp src/host_settings.cpp)
# More than four players in an EDF5 / EDF4.1 script mission online: EDF6Coop's BvmPlayerTableHooks and every member's
# word (src/edf5online_room.h; the creation and the table: src/edf5online.cpp).
target_sources(EDF6VehicleCrew PRIVATE src/edf5online_room.cpp)
# edf6common: the memory / patch / image / seat code both plugins link (common/)
add_subdirectory(common)
target_link_libraries(EDF6VehicleCrew PRIVATE edf6common)
configure_file(src/version.h.in "${CMAKE_BINARY_DIR}/generated/version.h" @ONLY)
# generated/build_id.h: the commit, at every build (cmake/build_id.cmake), for both plugins' load line.
add_custom_target(edf6_build_id
    COMMAND "${CMAKE_COMMAND}" "-DSRC=${CMAKE_SOURCE_DIR}" "-DOUT=${CMAKE_BINARY_DIR}/generated/build_id.h"
            -P "${CMAKE_SOURCE_DIR}/cmake/build_id.cmake"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/generated/build_id.h"
    COMMENT "Recording the build's commit" VERBATIM)
add_dependencies(EDF6VehicleCrew edf6_build_id)
target_include_directories(EDF6VehicleCrew PRIVATE third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(EDF6VehicleCrew PRIVATE xaudio2 sapi ole32)
target_compile_options(EDF6VehicleCrew PRIVATE /W4 /WX /utf-8 /permissive-)
target_link_options(EDF6VehicleCrew PRIVATE /DYNAMICBASE /NXCOMPAT /PDBALTPATH:EDF6VehicleCrew.pdb)
# Build output, never tracked: build/Mods/Plugins holds both plugins and their ini files, laid out as they go
# into the game (tools/installer.py and tools/build_release.py take them from there).
set_target_properties(EDF6VehicleCrew PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Mods/Plugins")
configure_file(EDF6VehicleCrew.ini "${CMAKE_BINARY_DIR}/Mods/Plugins/EDF6VehicleCrew.ini" COPYONLY)

# The installer's native helpers (native/*.cpp: the CMPL codec, CRILAYLA, the SK maps' greedy LZSS and collision trees):
# pylib/edf6native.py loads them with ctypes, tools/build_release.py bundles the DLL. Each is a port of a Python
# reference that stays the fallback (tests/test_native_*.py: the same bytes).
# MODULE, not SHARED: nothing links it, and SHARED libraries are the game's plugins (selftest pack_ships_every_plugin).
add_library(edf6cmpl MODULE native/cmpl.cpp native/crilayla.cpp native/lzss_greedy.cpp native/aabb_tree.cpp)
target_compile_options(edf6cmpl PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(edf6cmpl PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pylib")

# EDF6AutoTurret: the flak / Bohr auto-aim and the tank side guns (autoturret/README.md)
add_subdirectory(autoturret)

add_executable(append_log_test EXCLUDE_FROM_ALL tools/append_log_test.cpp)
target_link_libraries(append_log_test PRIVATE edf6common)
target_compile_options(append_log_test PRIVATE /W4 /WX /utf-8 /permissive-)
# edf6common's load helpers: the strict ini number, the DLL file line, a patch putting the page protection back.
add_executable(common_host_check EXCLUDE_FROM_ALL tools/common_host_check.cpp)
target_link_libraries(common_host_check PRIVATE edf6common)
target_compile_options(common_host_check PRIVATE /W4 /WX /utf-8 /permissive-)
# Production sidecar boarding against stand-in objects and a recording warp (no game required).
add_executable(sidecar_board_check EXCLUDE_FROM_ALL tools/sidecar_board_check.cpp)
target_include_directories(sidecar_board_check PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(sidecar_board_check PRIVATE edf6common)
target_compile_options(sidecar_board_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(sidecar_board_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# A rotor craft's acceleration budget flown offline (tools/hover_lift_check.cpp, src/hover_lift.h); not part of the plugin build:
# cmake --build build --target hover_lift_check && build\hover_lift_check.exe
add_executable(hover_lift_check EXCLUDE_FROM_ALL tools/hover_lift_check.cpp)
target_compile_options(hover_lift_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(hover_lift_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# A player aircraft on the ground on its contacts (tools/ground_contact_check.cpp, src/pjet_handling.h GroundContact):
# cmake --build build --target ground_contact_check && build\ground_contact_check.exe
add_executable(ground_contact_check EXCLUDE_FROM_ALL tools/ground_contact_check.cpp)
target_compile_options(ground_contact_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(ground_contact_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The boardable aircraft's flight numbers checked offline (tools/pjet_kinds.cpp); not part of the plugin build:
# cmake --build build --target pjet_kinds && build\pjet_kinds.exe
add_executable(pjet_kinds EXCLUDE_FROM_ALL tools/pjet_kinds.cpp)
target_include_directories(pjet_kinds PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(pjet_kinds PRIVATE /W4 /WX /utf-8 /permissive-)
# The fixed wings' turn handling under the player's flight model (tools/pjet_turn_sim.cpp: src/pjet_handling.h against the
# code before 2026-10-06), and its bounds (--check, in CTest):
# cmake --build build --target pjet_turn_sim && build\pjet_turn_sim.exe --both
add_executable(pjet_turn_sim EXCLUDE_FROM_ALL tools/pjet_turn_sim.cpp)
target_include_directories(pjet_turn_sim PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(pjet_turn_sim PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(pjet_turn_sim PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The stock heli's mouse-aim flight (src/heliaim.h) against a stand-in of its flight law, offline: the signs of the keys and
# the mouse (tools/heli_aim_check.cpp). Not part of the plugin build:
# cmake --build build --target heli_aim_check && build\heli_aim_check.exe
add_executable(heli_aim_check EXCLUDE_FROM_ALL tools/heli_aim_check.cpp)
target_compile_options(heli_aim_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(heli_aim_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The NPC helis' unguided store (the 506's napalm gun and drop pod: src/roundaim.h, heli.cpp kStoreHolder) against
# targets at several ranges, heights and speeds, the miss before and after (tools/heli_fire_check.cpp). Not part of the
# plugin build: cmake --build build --target heli_fire_check && build\heli_fire_check.exe
add_executable(heli_fire_check EXCLUDE_FROM_ALL tools/heli_fire_check.cpp)
target_compile_options(heli_fire_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(heli_fire_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The player's turret aim (autoturret/src/aimmath.h: the lead circle's solve, the lock's pick order) against the game's
# per-frame round step, offline (tools/turret_lead_check.cpp). Not part of the plugin build:
# cmake --build build --target turret_lead_check && build\turret_lead_check.exe
add_executable(turret_lead_check EXCLUDE_FROM_ALL tools/turret_lead_check.cpp)
target_include_directories(turret_lead_check PRIVATE common)
target_link_libraries(turret_lead_check PRIVATE edf6common)
target_compile_options(turret_lead_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(turret_lead_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The game clock through the pause menu (src/game_clock.h), offline (tools/pause_clock_check.cpp).
# Not part of the plugin build: cmake --build build --target pause_clock_check && build\pause_clock_check.exe
add_executable(pause_clock_check EXCLUDE_FROM_ALL tools/pause_clock_check.cpp)
target_compile_options(pause_clock_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(pause_clock_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The Nix's torso hold (src/nix_twist.h) against a stand-in of the stock update's order, offline (tools/nix_twist_check.cpp).
# Not part of the plugin build: cmake --build build --target nix_twist_check && build\nix_twist_check.exe
add_executable(nix_twist_check EXCLUDE_FROM_ALL tools/nix_twist_check.cpp)
target_compile_options(nix_twist_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(nix_twist_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The Sazabi's poses (src/sazabi_pose.h) on its model's joints, offline (tools/sazabi_pose_check.cpp; with --dump,
# tools/sazabi_pose_view.py draws them on the model).
add_executable(sazabi_pose_check EXCLUDE_FROM_ALL tools/sazabi_pose_check.cpp)
target_compile_options(sazabi_pose_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(sazabi_pose_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The Sazabi's flight control (src/sazabi_flight.h) flown offline over flat ground, and its thrusters' flame levels
# (src/sazabi_flames.h): tools/sazabi_flight_check.cpp.
add_executable(sazabi_flight_check EXCLUDE_FROM_ALL tools/sazabi_flight_check.cpp)
target_compile_options(sazabi_flight_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(sazabi_flight_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The Sazabi's funnels' flight (src/sazabi_funnels.h) flown offline round enemies (tools/sazabi_funnel_check.cpp).
add_executable(sazabi_funnel_check EXCLUDE_FROM_ALL tools/sazabi_funnel_check.cpp)
target_compile_options(sazabi_funnel_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(sazabi_funnel_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The Sazabi's aim assist (src/sazabi_assist.h): the pick round the reticle, the camera's pull onto it
# (tools/sazabi_assist_check.cpp).
add_executable(sazabi_assist_check EXCLUDE_FROM_ALL tools/sazabi_assist_check.cpp)
target_compile_options(sazabi_assist_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(sazabi_assist_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_executable(seat_aim_check EXCLUDE_FROM_ALL tools/seat_aim_check.cpp)
target_include_directories(seat_aim_check PRIVATE common)
target_compile_options(seat_aim_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(seat_aim_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The vehicles' round models (src/rounds.h: the arcs, MissileBullet01's motor, the first ground along them) against their
# closed forms and the stock rockets' numbers, offline (tools/rounds_check.cpp). Not part of the plugin build:
# cmake --build build --target rounds_check && build\rounds_check.exe
add_executable(rounds_check EXCLUDE_FROM_ALL tools/rounds_check.cpp)
target_compile_options(rounds_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(rounds_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The damage statistics' book and page (src/damage_stats.h, tools/damage_stats_check.cpp). Not part of the plugin build:
# cmake --build build --target damage_stats_check && build\damage_stats_check.exe
add_executable(damage_stats_check EXCLUDE_FROM_ALL tools/damage_stats_check.cpp)
target_compile_options(damage_stats_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(damage_stats_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The map's command buttons (src/map_buttons.h): their layout and the click's button, offline
# (tools/map_buttons_check.cpp). Not part of the plugin build:
# cmake --build build --target map_buttons_check && build\map_buttons_check.exe
add_executable(map_buttons_check EXCLUDE_FROM_ALL tools/map_buttons_check.cpp)
target_compile_options(map_buttons_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(map_buttons_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# A stock vehicle's seat events (src/seat_events.h): a crew created aboard plays no boarding sound, offline
# (tools/seat_events_check.cpp).
add_executable(seat_events_check EXCLUDE_FROM_ALL tools/seat_events_check.cpp)
target_compile_options(seat_events_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(seat_events_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# A magnified sight's picture (src/scopeview.h): the dark round the field, offline (tools/scopeview_check.cpp).
# Not part of the plugin build: cmake --build build --target scopeview_check && build\scopeview_check.exe
add_executable(scopeview_check EXCLUDE_FROM_ALL tools/scopeview_check.cpp)
target_compile_options(scopeview_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(scopeview_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The squad's box sweep (src/pickup.h): who goes for which box, the health-box rules, offline
# (tools/pickup_check.cpp). Not part of the plugin build:
# cmake --build build --target pickup_check && build\pickup_check.exe
add_executable(pickup_check EXCLUDE_FROM_ALL tools/pickup_check.cpp)
target_compile_options(pickup_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(pickup_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The squads' formations (src/formation.h): the slots, the heading, bounding overwatch, giving a blocked slot up,
# offline (tools/formation_check.cpp). Not part of the plugin build:
# cmake --build build --target formation_check && build\formation_check.exe
add_executable(formation_check EXCLUDE_FROM_ALL tools/formation_check.cpp)
target_compile_options(formation_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(formation_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# A vehicle gun's range ladder (src/gunsight.h) against the round flown as the game flies it (roundaim.h), offline
# (tools/gunsight_check.cpp). Not part of the plugin build:
# cmake --build build --target gunsight_check && build\gunsight_check.exe
add_executable(gunsight_check EXCLUDE_FROM_ALL tools/gunsight_check.cpp)
target_compile_options(gunsight_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(gunsight_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The sights' styles by weapon (src/reticle.h): which style each real vehicle weapon gets and each style's geometry,
# offline (tools/reticle_check.cpp). Not part of the plugin build: cmake --build build --target reticle_check && build\reticle_check.exe
add_executable(reticle_check EXCLUDE_FROM_ALL tools/reticle_check.cpp)
target_compile_options(reticle_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(reticle_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The turret camera's rig and turret command (src/turretcam.h) against the stock aim axis step, offline
# (tools/turret_cam_check.cpp). Not part of the plugin build:
# cmake --build build --target turret_cam_check && build\turret_cam_check.exe
add_executable(turret_cam_check EXCLUDE_FROM_ALL tools/turret_cam_check.cpp)
target_compile_options(turret_cam_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(turret_cam_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The ram's pure part (src/vehicleram.h: the kinetic-energy formula the jets and the ground vehicles share, the parts that
# hit, the contact, the impact charge's pick) and representative rams' damage, offline (tools/vehicle_ram_check.cpp).
# Not part of the plugin build: cmake --build build --target vehicle_ram_check && build\vehicle_ram_check.exe
add_executable(vehicle_ram_check EXCLUDE_FROM_ALL tools/vehicle_ram_check.cpp)
target_compile_options(vehicle_ram_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(vehicle_ram_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The gravity operations' pure part (src/gravity_logic.h: the carry's cap, the throw's speed and energy for the mass, the
# impacts of a flight) and representative throws' damage, offline (tools/gravity_check.cpp).
# Not part of the plugin build: cmake --build build --target gravity_check && build\gravity_check.exe
add_executable(gravity_check EXCLUDE_FROM_ALL tools/gravity_check.cpp)
target_compile_options(gravity_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(gravity_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The Katyusha's rack (src/katyusha_rack.h: which rockets are on their rails) against its launcher stepped as the game
# steps it, offline (tools/katyusha_rack_check.cpp). Not part of the plugin build:
# cmake --build build --target katyusha_rack_check && build\katyusha_rack_check.exe
add_executable(katyusha_rack_check EXCLUDE_FROM_ALL tools/katyusha_rack_check.cpp)
target_compile_options(katyusha_rack_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(katyusha_rack_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The gunship's muzzle (src/gunmuzzle.h) on the circles it flies: no round's line through its airframe, its start where the
# muzzle is when it leaves, against the old origin start and the shell's stock wait, offline (tools/gunship_muzzle_check.cpp).
# Not part of the plugin build: cmake --build build --target gunship_muzzle_check && build\gunship_muzzle_check.exe
add_executable(gunship_muzzle_check EXCLUDE_FROM_ALL tools/gunship_muzzle_check.cpp)
target_compile_options(gunship_muzzle_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(gunship_muzzle_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The bullets' candidate collector's verdict for the plugin's jets (src/ownround.h): a jet's own round never on its own
# airframe (its object or any of its own body ids), wingmen as before, all else stock, offline (tools/own_round_check.cpp).
# Not part of the plugin build: cmake --build build --target own_round_check && build\own_round_check.exe
add_executable(own_round_check EXCLUDE_FROM_ALL tools/own_round_check.cpp)
target_compile_options(own_round_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(own_round_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The exhausts' frame (src/exhaust_pose.h): a jet flown frame by frame in the game's order, its flame's root against its
# nozzle as drawn, before / after carrying the bone record to this frame (tools/exhaust_pose_check.cpp). Not part of the
# plugin build: cmake --build build --target exhaust_pose_check && build\exhaust_pose_check.exe
add_executable(exhaust_pose_check EXCLUDE_FROM_ALL tools/exhaust_pose_check.cpp)
target_compile_options(exhaust_pose_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(exhaust_pose_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The exhausts' nozzles read off the model (src/exhaust_nozzles.h: its nozzle bones, the stock models' src/nozzles_gen.h)
# and placed where the game draws them, a carrier's tilting pods too (tools/exhaust_nozzles_check.cpp). Not part of the
# plugin build: cmake --build build --target exhaust_nozzles_check && build\exhaust_nozzles_check.exe
add_executable(exhaust_nozzles_check EXCLUDE_FROM_ALL tools/exhaust_nozzles_check.cpp)
target_compile_options(exhaust_nozzles_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(exhaust_nozzles_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The gun stabilizer (src/stab.h) on a hull driven over bumps and through turns against the stock aim axis step, the
# gun's world pointing error with and without it, offline (tools/stab_check.cpp). Not part of the plugin build:
# cmake --build build --target stab_check && build\stab_check.exe
add_executable(stab_check EXCLUDE_FROM_ALL tools/stab_check.cpp)
target_compile_options(stab_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(stab_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_executable(stab_native_readback_test EXCLUDE_FROM_ALL tests/stab_native_readback_test.cpp)
target_include_directories(stab_native_readback_test PRIVATE src common)
target_link_libraries(stab_native_readback_test PRIVATE edf6common)
target_compile_options(stab_native_readback_test PRIVATE /W4 /WX /utf-8 /permissive-)

# The Proteus rework's rules (src/proteus_logic.h: the stances and their stagger, the shield's heat / HP / break / refill,
# who operates each stock weapon mount, the step height) offline (tools/proteus_check.cpp). Not part of the plugin build:
# cmake --build build --target proteus_check && build\proteus_check.exe
# The hit prediction's intercept and window logic (tools/hit_predict_check.cpp, src/hit_predict.h).
add_executable(hit_predict_check EXCLUDE_FROM_ALL tools/hit_predict_check.cpp)
target_compile_options(hit_predict_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(hit_predict_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Fire that cannot hurt its target held (tools/wasted_fire_check.cpp, src/wasted_fire.h).
add_executable(wasted_fire_check EXCLUDE_FROM_ALL tools/wasted_fire_check.cpp)
target_compile_options(wasted_fire_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(wasted_fire_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# What the charge drones and the gunship go for and when a charge drone gives up (tools/air_chase_check.cpp,
# src/air_chase.h). Not part of the plugin build.
add_executable(air_chase_check EXCLUDE_FROM_ALL tools/air_chase_check.cpp)
target_compile_options(air_chase_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(air_chase_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The NPC helis' rotor clearance offline (tools/rotor_clear_check.cpp, src/rotor_clear.h). Not part of the plugin build.
add_executable(rotor_clear_check EXCLUDE_FROM_ALL tools/rotor_clear_check.cpp)
target_compile_options(rotor_clear_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(rotor_clear_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The flyers' soft edge's rules offline (tools/airbound_check.cpp, src/airbound.h). Not part of the plugin build.
add_executable(airbound_check EXCLUDE_FROM_ALL tools/airbound_check.cpp)
target_compile_options(airbound_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(airbound_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_executable(proteus_check EXCLUDE_FROM_ALL tools/proteus_check.cpp)
target_compile_options(proteus_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(proteus_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# NPC jets against buildings, offline (tools/jet_obstacle_sim.cpp): the plugin's own jet_flight.cpp, jet_combat.cpp and
# body506.cpp against a stand-in world of flat ground and box buildings; tools/jet_obstacle_view.py draws its trace.
# jet_obstacle_sim_before: the same with Wing's Ahead call and Strike's InSight test cut out of copies of the two files,
# to show what they change. Not part of the plugin build:
# cmake --build build --target jet_obstacle_sim jet_obstacle_sim_before
file(READ src/jet_flight.cpp EDF6_SIM_FLIGHT)
file(READ src/jet_combat.cpp EDF6_SIM_COMBAT)
string(REPLACE "    Ahead(j,pos,ms);\n" "" EDF6_SIM_FLIGHT_BEFORE "${EDF6_SIM_FLIGHT}")
string(REPLACE " && InSight(pos,lead)" "" EDF6_SIM_COMBAT_BEFORE "${EDF6_SIM_COMBAT}")
set(EDF6_SIMS jet_obstacle_sim)
add_executable(jet_obstacle_sim EXCLUDE_FROM_ALL tools/jet_obstacle_sim.cpp src/jet_flight.cpp src/jet_combat.cpp src/body506.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/jet_flight.cpp src/jet_combat.cpp)
# The cut not found (the code moved): no "before" build, never a failed configure of the plugin's own.
if(EDF6_SIM_FLIGHT_BEFORE STREQUAL EDF6_SIM_FLIGHT OR EDF6_SIM_COMBAT_BEFORE STREQUAL EDF6_SIM_COMBAT)
    message(WARNING "jet_obstacle_sim_before skipped: the Ahead call or the InSight test is no longer where the copy cuts it")
else()
    file(WRITE "${CMAKE_BINARY_DIR}/sim/jet_flight_before.cpp" "${EDF6_SIM_FLIGHT_BEFORE}")
    file(WRITE "${CMAKE_BINARY_DIR}/sim/jet_combat_before.cpp" "${EDF6_SIM_COMBAT_BEFORE}")
    add_executable(jet_obstacle_sim_before EXCLUDE_FROM_ALL tools/jet_obstacle_sim.cpp "${CMAKE_BINARY_DIR}/sim/jet_flight_before.cpp"
                   "${CMAKE_BINARY_DIR}/sim/jet_combat_before.cpp" src/body506.cpp)
    list(APPEND EDF6_SIMS jet_obstacle_sim_before)
endif()
foreach(sim ${EDF6_SIMS})
    target_include_directories(${sim} PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(${sim} PRIVATE edf6common)
    target_compile_options(${sim} PRIVATE /W3 /utf-8 /permissive-)
    target_compile_definitions(${sim} PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_target_properties(${sim} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endforeach()

# The cockpit's warnings offline (tools/warn_check.cpp): the ground-proximity cases, the launch detection, the callouts'
# scheduler, and every warning sound written out as a WAV (no sound played). Not part of the plugin build:
# cmake --build build --target warn_check && build\warn_check.exe
add_executable(warn_check EXCLUDE_FROM_ALL tools/warn_check.cpp src/warn.cpp)
target_include_directories(warn_check PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(warn_check PRIVATE edf6common xaudio2 sapi ole32)
target_compile_options(warn_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(warn_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The ground vehicles' sounds offline (tools/vsound_check.cpp): every clip src/vsynth.h makes written out as a WAV, their
# levels and loops measured, src/vehmix.h's rules against cases, a tank's scenario mixed down (no sound played). Not part
# of the plugin build: cmake --build build --target vsound_check && build\vsound_check.exe
add_executable(vsound_check EXCLUDE_FROM_ALL tools/vsound_check.cpp)
target_include_directories(vsound_check PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(vsound_check PRIVATE edf6common xaudio2 sapi ole32)
target_compile_options(vsound_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(vsound_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The EMC's charged beam offline (tools/emc_check.cpp: src/emc_plan.h's trigger cycle, damage budget and the scan along the
# beam against a stand-in line of buildings and a hill). Not part of the plugin build:
# cmake --build build --target emc_check && build\emc_check.exe
add_executable(emc_check EXCLUDE_FROM_ALL tools/emc_check.cpp)
target_compile_options(emc_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(emc_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The ultimate strike's show offline (tools/ultimate_check.cpp: src/ultimate_plan.h's timeline, seeded replay, area and load).
# Not part of the plugin build:
# cmake --build build --target ultimate_check && build\ultimate_check.exe
add_executable(ultimate_check EXCLUDE_FROM_ALL tools/ultimate_check.cpp)
target_compile_options(ultimate_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(ultimate_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Where a sound is heard from the camera offline (tools/sound_place_check.cpp, src/sound_place.h: SoundAt's arithmetic,
# a still sound given no velocity). cmake --build build --target sound_place_check && build\sound_place_check.exe
add_executable(sound_place_check EXCLUDE_FROM_ALL tools/sound_place_check.cpp)
target_compile_options(sound_place_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(sound_place_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The aircraft's HUD drawn offline (tools/hud_view.cpp: src/hud.cpp against a stand-in image whose draw functions record;
# tools/hud_view.py makes the PNGs): the warnings' layout to look at. Not part of the plugin build:
# cmake --build build --target hud_view && build\hud_view.exe && python tools\hud_view.py %TEMP%\edf6_hud_view
add_executable(hud_view EXCLUDE_FROM_ALL tools/hud_view.cpp)
target_include_directories(hud_view PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(hud_view PRIVATE edf6common)
target_compile_options(hud_view PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(hud_view PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The HUD's string table offline (tools/hudtext_check.cpp: src/hudtext.h): every text in every language, the same printf
# arguments as the English, the run-time words, the language picked from the game and the ini. Not part of the plugin build:
# cmake --build build --target hudtext_check && build\hudtext_check.exe
add_executable(hudtext_check EXCLUDE_FROM_ALL tools/hudtext_check.cpp)
target_compile_options(hudtext_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(hudtext_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The map view's camera and grid math (src/map_cam.h: the eye, the drag that grabs the ground, the zoom's limits, the
# grid's reach), offline (tools/map_cam_check.cpp). Not part of the plugin build:
# cmake --build build --target map_cam_check && build\map_cam_check.exe
add_executable(map_cam_check EXCLUDE_FROM_ALL tools/map_cam_check.cpp)
target_compile_options(map_cam_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(map_cam_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The map's NPC commands (src/mapcmd_logic.h: the pointer's ground point, the box / click / cycle selection, the keys,
# the guard formation),
# offline (tools/map_cmd_check.cpp). Not part of the plugin build:
# cmake --build build --target map_cmd_check && build\map_cmd_check.exe
add_executable(map_cmd_check EXCLUDE_FROM_ALL tools/map_cmd_check.cpp)
target_compile_options(map_cmd_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(map_cmd_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The squads' transports' rules (src/transport_logic.h) offline: phases, drop points, the paratroop stick and canopy.
# The transports executed (src/transport.cpp against a world model: trips, an assault, WITHDRAW, the paratroop drop).
add_executable(transport_runtime_test EXCLUDE_FROM_ALL tests/transport_runtime_test.cpp)
target_link_libraries(transport_runtime_test PRIVATE edf6common)
target_include_directories(transport_runtime_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(transport_runtime_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME transport_runtime COMMAND transport_runtime_test)
add_executable(airdrop_check EXCLUDE_FROM_ALL tools/airdrop_check.cpp)
target_compile_options(airdrop_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(airdrop_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_executable(transport_check EXCLUDE_FROM_ALL tools/transport_check.cpp)
target_compile_options(transport_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(transport_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The Depth Crawler's driving on a floor, a wall and a ceiling (src/crawl_logic.h), offline (tools/crawl_check.cpp).
# Not part of the plugin build:
# cmake --build build --target crawl_check && build\crawl_check.exe
add_executable(crawl_check EXCLUDE_FROM_ALL tools/crawl_check.cpp)
target_compile_options(crawl_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(crawl_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# EDF4.1's ObjectAction on its fortress (src/edf41fortress_action.h), offline (tools/edf41_fortress_check.cpp): 4.1's seven
# actions run as 4.1 runs them, none through MovingFortress's own table (M117 crash, docs/feedback-2026-10-18-edf41-m16-crash.md).
add_executable(edf41_fortress_check EXCLUDE_FROM_ALL tools/edf41_fortress_check.cpp)
target_compile_options(edf41_fortress_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(edf41_fortress_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Exercise the production installer and patched vtable slots with the optional Hector AI disabled and enabled.
add_executable(edf41fortress_runtime_test EXCLUDE_FROM_ALL tests/edf41fortress_runtime_test.cpp)
target_link_libraries(edf41fortress_runtime_test PRIVATE edf6common)
target_include_directories(edf41fortress_runtime_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(edf41fortress_runtime_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME edf41fortress_runtime COMMAND edf41fortress_runtime_test)
# EDF4.1 Hector's arm gate and back-off cycle (src/hector_ai.h), offline (tools/hector_ai_check.cpp). Not part of the plugin build:
# cmake --build build --target hector_ai_check && build\hector_ai_check.exe
add_executable(hector_ai_check EXCLUDE_FROM_ALL tools/hector_ai_check.cpp)
target_compile_options(hector_ai_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(hector_ai_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# What the map marks of the friendly side (src/map_marks.h: team 5's walk, the aircraft's icon), offline
# (tools/map_marks_check.cpp). Not part of the plugin build:
# cmake --build build --target map_marks_check && build\map_marks_check.exe
add_executable(map_marks_check EXCLUDE_FROM_ALL tools/map_marks_check.cpp)
target_compile_options(map_marks_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(map_marks_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The HUD's switch cues (src/hud_cue.h: the banner of a store / aim-mode switch, a store's picture), offline
# (tools/hud_cue_check.cpp). Not part of the plugin build:
# cmake --build build --target hud_cue_check && build\hud_cue_check.exe
add_executable(hud_cue_check EXCLUDE_FROM_ALL tools/hud_cue_check.cpp)
target_compile_options(hud_cue_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(hud_cue_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The NPC AI's decisions (src/npc_logic.h: fire lanes, arms, crowding, the combat spot, squads, the mark, the way back to a
# post) offline (tools/npc_ai_check.cpp). Not part of the plugin build:
# cmake --build build --target npc_ai_check && build\npc_ai_check.exe
# The NPCs' backpack weapons (src/npc_backpack.h, src/npc_pack_table.h; docs/npc-backpack-re.md) offline:
# cmake --build build --target npc_backpack_check && build\npc_backpack_check.exe
add_executable(npc_backpack_check EXCLUDE_FROM_ALL tools/npc_backpack_check.cpp)
target_include_directories(npc_backpack_check PRIVATE src)
target_compile_options(npc_backpack_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(npc_backpack_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# Execute the production native slot reader and all readiness/state-machine gates against recording entry points.
add_executable(npc_backpack_native_check EXCLUDE_FROM_ALL tools/npc_backpack_native_check.cpp)
target_include_directories(npc_backpack_native_check PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(npc_backpack_native_check PRIVATE edf6common)
target_compile_options(npc_backpack_native_check PRIVATE /W4 /WX /utf-8 /permissive-)
add_executable(npc_ai_check EXCLUDE_FROM_ALL tools/npc_ai_check.cpp)
target_compile_options(npc_ai_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(npc_ai_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The Primer creatures' pose code run offline (tools/primer_pose_view.py, tools/primer_chain_view.py render it):
# not part of the plugin.
add_executable(primer_pose_sim tools/primer_pose_sim.cpp)
target_compile_options(primer_pose_sim PRIVATE /utf-8)
set_target_properties(primer_pose_sim PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The Primer creatures' flight and fight run without the game (tools/primer_flight_sim.cpp): the plugin's own
# primer.cpp and the jets' flight code against a stand-in world; tools/primer_trace_view.py draws its trace.
add_executable(primer_flight_sim tools/primer_flight_sim.cpp src/primer.cpp src/jet_flight.cpp src/body506.cpp)
target_compile_options(primer_flight_sim PRIVATE /utf-8)
target_link_libraries(primer_flight_sim PRIVATE edf6common)
set_target_properties(primer_flight_sim PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

include(CTest)
if(BUILD_TESTING)
    add_executable(primer_ballistics_test tools/primer_ballistics_test.cpp)
    add_test(NAME primer_ballistics COMMAND primer_ballistics_test)
    add_test(NAME primer_lifecycle COMMAND primer_flight_sim --selftest)
endif()
enable_testing()
add_executable(support_spawn_test tests/support_spawn_test.cpp)
target_include_directories(support_spawn_test PRIVATE common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(support_spawn_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME support_spawn COMMAND support_spawn_test)
# A diagnostic logged once per distinct answer (src/log_once.h: heli.cpp LogSea) offline.
add_executable(log_once_test tests/log_once_test.cpp)
target_compile_options(log_once_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME log_once COMMAND log_once_test)
# The debug spawn tool's catalog, menu and placement (src/debug_spawn.h) offline.
add_executable(debug_spawn_test tests/debug_spawn_test.cpp)
target_compile_options(debug_spawn_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME debug_spawn COMMAND debug_spawn_test)
# The test range's 「摆放」 panel on the M map (src/range_place.h) offline.
add_executable(range_place_test tests/range_place_test.cpp)
target_compile_options(range_place_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME range_place COMMAND range_place_test)
add_executable(sightzoom_test tests/sightzoom_test.cpp)
target_link_libraries(sightzoom_test PRIVATE edf6common)
target_compile_options(sightzoom_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME sightzoom COMMAND sightzoom_test)
add_executable(highcam_input_test EXCLUDE_FROM_ALL tests/highcam_input_test.cpp)
target_include_directories(highcam_input_test PRIVATE src common)
target_link_libraries(highcam_input_test PRIVATE edf6common user32)
target_compile_options(highcam_input_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME highcam_input COMMAND highcam_input_test)
add_executable(turret_cam_runtime_check EXCLUDE_FROM_ALL tools/turret_cam_runtime_check.cpp src/rounds.cpp src/launcher.cpp)
target_include_directories(turret_cam_runtime_check PRIVATE src common)
target_link_libraries(turret_cam_runtime_check PRIVATE edf6common user32)
target_compile_options(turret_cam_runtime_check PRIVATE /Gy /W4 /WX /utf-8 /permissive-)
target_link_options(turret_cam_runtime_check PRIVATE /OPT:REF)
add_test(NAME turret_cam_runtime COMMAND turret_cam_runtime_check)
add_executable(boarding_hit_test tests/boarding_hit_test.cpp)
target_link_libraries(boarding_hit_test PRIVATE edf6common)
target_compile_options(boarding_hit_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME boarding_hit COMMAND boarding_hit_test)

# Exercise the production call picker with local/remote owners, without a running game.
add_executable(call_pick_test tests/call_pick_test.cpp)
target_link_libraries(call_pick_test PRIVATE edf6common)
target_include_directories(call_pick_test PRIVATE third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(call_pick_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME call_pick COMMAND call_pick_test)
add_executable(support_entry_test tests/support_entry_test.cpp)
target_compile_options(support_entry_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME support_entry COMMAND support_entry_test)
# The paratroop plane's passes along one straight line through its drop point (src/ferry_line.h; 2026-10-10).
add_executable(ferry_line_test tests/ferry_line_test.cpp)
target_compile_options(ferry_line_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME ferry_line COMMAND ferry_line_test)
# The support configuration's parse and validation, and the ini it really reads (src/support_config.cpp).
add_executable(support_config_test tests/support_config_test.cpp)
target_compile_options(support_config_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME support_config COMMAND support_config_test)
# The out-of-game loadouts' pure logic: preset / round-mix syntax, the per-tank ratio allocation, generated file names
# (src/support_loadout.h; docs/feature-2026-10-09-loadout-editor.md).
add_executable(support_loadout_test tests/support_loadout_test.cpp)
target_compile_options(support_loadout_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME support_loadout COMMAND support_loadout_test)
# The room / HQ loadout menu's model, navigation, save and snapshot (src/room_menu.h; docs/feature-2026-10-12-room-loadout-menu.md).
add_executable(room_menu_test tests/room_menu_test.cpp)
target_compile_options(room_menu_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME room_menu COMMAND room_menu_test)
# The room / HQ 3D preview's pure logic (src/room_preview_logic.h: menu SGO / class / weapons, colours, the Desc rewrite,
# the camera shift, the planner; docs/room-3d-preview-re.md plan A).
add_executable(room_preview_test tests/room_preview_test.cpp)
target_compile_options(room_preview_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME room_preview COMMAND room_preview_test)
# The EDF4.1 outfit's pure logic (src/outfit41_logic.h: its files, the stored choice, the popup's layout).
add_executable(outfit41_test tests/outfit41_test.cpp)
target_include_directories(outfit41_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(outfit41_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME outfit41 COMMAND outfit41_test)
# The room / HQ loadout screen's pure side: element names, keyboard / pad mapping, the input gate, row looks, texts,
# catalog units, preview and save pairing (src/room_ui_logic.h; src/room_ui.cpp draws with it).
# The menu as the game's own dialogs: pages, buttons, paging, cancel, the edits and saves they make
# (src/room_dialog_logic.h; src/room_dialog.cpp opens them).
add_executable(room_dialog_test tests/room_dialog_test.cpp)
target_compile_options(room_dialog_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME room_dialog COMMAND room_dialog_test)
# What the room menu's dialogs and the HQ outfit popup share (src/game_dialog.h: the items vector's growth, the
# std::function vtable; common/edf/wstr.h: the game's std::wstring layout), over fakes of the game's functions.
add_executable(game_dialog_test tests/game_dialog_test.cpp)
target_include_directories(game_dialog_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/common)
target_compile_options(game_dialog_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME game_dialog COMMAND game_dialog_test)
# The ini text read / rewritten key by key (src/ini_text.h: the room menu's ini and the EDF4.1 outfit's file).
add_executable(ini_text_test tests/ini_text_test.cpp)
target_include_directories(ini_text_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(ini_text_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME ini_text COMMAND ini_text_test)
# EDF6Coop's member attributes as every module reads them (src/coop_attr.h) over a fake table, and the host's settings /
# the room's builds word (src/host_settings_logic.h).
add_executable(coop_attr_test tests/coop_attr_test.cpp)
target_include_directories(coop_attr_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(coop_attr_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME coop_attr COMMAND coop_attr_test)
add_executable(room_ui_test tests/room_ui_test.cpp)
target_compile_options(room_ui_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME room_ui COMMAND room_ui_test)
# The loadout menu's ini writer (BOM / CRLF / comments kept, checked with the plugin's parsers), the atomic replace,
# the generator's status and its hidden process (src/loadout_ini.h, src/loadout_file.cpp).
add_executable(loadout_writer_test tests/loadout_writer_test.cpp src/loadout_file.cpp)
target_compile_options(loadout_writer_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME loadout_writer COMMAND loadout_writer_test)
add_executable(room_coop_capability_test tests/room_coop_capability_test.cpp)
target_compile_options(room_coop_capability_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME room_coop_capability COMMAND room_coop_capability_test)
# The room pre-sync over EDF6Coop's member attributes against a fake Coop table (src/room_sync_logic.h; the design doc's 阶段 6).
add_executable(room_sync_test tests/room_sync_test.cpp)
target_compile_options(room_sync_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME room_sync COMMAND room_sync_test)
# The mission's variant preload set (configuration + the room host's list + the pending list's newest, never a
# directory scan) and the hello's filter (src/support_variants_logic.h; the design doc's 阶段 R).
add_executable(support_variants_test tests/support_variants_test.cpp)
target_compile_options(support_variants_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME support_variants COMMAND support_variants_test)
add_executable(support_policy_test EXCLUDE_FROM_ALL tests/support_policy_test.cpp)
target_link_libraries(support_policy_test PRIVATE edf6common)
target_compile_options(support_policy_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME support_policy COMMAND support_policy_test)
add_executable(support_dispatch_test EXCLUDE_FROM_ALL tests/support_dispatch_test.cpp)
target_link_libraries(support_dispatch_test PRIVATE edf6common)
target_compile_options(support_dispatch_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME support_dispatch COMMAND support_dispatch_test)
add_executable(support_infantry_route_test EXCLUDE_FROM_ALL tests/support_infantry_route_test.cpp src/ground_navigation.cpp)
target_link_libraries(support_infantry_route_test PRIVATE edf6common user32)
target_compile_options(support_infantry_route_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(support_infantry_route_test PRIVATE /OPT:REF)
add_test(NAME support_infantry_route COMMAND support_infantry_route_test)
add_executable(support_vehicle_route_test EXCLUDE_FROM_ALL tests/support_vehicle_route_test.cpp)
target_link_libraries(support_vehicle_route_test PRIVATE edf6common)
target_compile_options(support_vehicle_route_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME support_vehicle_route COMMAND support_vehicle_route_test)
# The remote gunner's recoil on the vehicle's authority (src/gunnerrecoil.cpp) against a stand-in EDF.dll.
add_executable(gunner_recoil_test tests/gunner_recoil_test.cpp)
target_link_libraries(gunner_recoil_test PRIVATE edf6common)
target_include_directories(gunner_recoil_test PRIVATE third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(gunner_recoil_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME gunner_recoil COMMAND gunner_recoil_test)

add_executable(map_hud_check EXCLUDE_FROM_ALL tools/map_hud_check.cpp)
target_compile_options(map_hud_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(map_hud_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The stock armor gauge hidden while riding (src/stock_armor_hud.h) and the seat switch's presses and next seat
# (src/seat_switch_logic.h), checked off the game.
add_executable(armor_hud_check EXCLUDE_FROM_ALL tools/armor_hud_check.cpp)
target_compile_options(armor_hud_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(armor_hud_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The stock red aim lines per seat: an NPC's hidden, real crews too (tools/aim_line_want_check.cpp).
add_executable(aim_line_want_check EXCLUDE_FROM_ALL tools/aim_line_want_check.cpp)
target_compile_options(aim_line_want_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(aim_line_want_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_executable(seat_switch_check EXCLUDE_FROM_ALL tools/seat_switch_check.cpp)
target_compile_options(seat_switch_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(seat_switch_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_executable(grape_turret_check EXCLUDE_FROM_ALL tools/grape_turret_check.cpp)
target_compile_options(grape_turret_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(grape_turret_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The view distance's clip planes (src/view_clip.h: the far pass keeps drawing the far-only scenery), offline.
# cmake --build build --target view_clip_check && build\view_clip_check.exe
add_executable(view_clip_check EXCLUDE_FROM_ALL tools/view_clip_check.cpp)
target_compile_options(view_clip_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(view_clip_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The split missiles' surface distance against a simulated Blood Storm flight (src/split_fuse.h).
add_executable(split_fuse_test tests/split_fuse_test.cpp)
target_compile_options(split_fuse_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME split_fuse COMMAND split_fuse_test)
# The stock route guide's field corrections (src/route_guide.h).
add_executable(route_guide_test tests/route_guide_test.cpp)
target_compile_options(route_guide_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME route_guide COMMAND route_guide_test)
# The online gates (src/online_authority.h): their rules on every case, and a source guard that the plugin asks them.
add_executable(online_authority_test tests/online_authority_test.cpp)
target_compile_options(online_authority_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME online_authority COMMAND online_authority_test)
add_executable(drill_net_test tests/drill_net_test.cpp)
target_compile_options(drill_net_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME drill_net COMMAND drill_net_test)
# The vehicles' tag 15 kinds: the common head, gate and the seat pick (src/seat_net.h).
add_executable(seat_net_test tests/seat_net_test.cpp)
target_compile_options(seat_net_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME seat_net COMMAND seat_net_test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# The installer's support menu (tools/support_config.py) against the plugin's keys and weapons, and its ini edits.
add_test(NAME cockpit_build COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/cockpit_build_test.py")
set_tests_properties(cockpit_build PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 3600)
add_test(NAME cabin_view COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/cabin_view_test.py")
add_test(NAME cockpit_instruments COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/cockpit_instruments_test.py")
add_test(NAME cockpit_retrofit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/cockpit_retrofit_test.py")
add_test(NAME cockpit_shell COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/cockpit_shell_test.py")
add_test(NAME gltf_glb COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/gltf_glb_test.py")
add_test(NAME input_source_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/input_source_guard_test.py")
add_test(NAME support_config_ini COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/support_config_ini_test.py")
# The menu prints Chinese: UTF-8 like the workflow's other Python steps, not the runner's cp1252 console.
set_tests_properties(support_config_ini PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# The loadout editor and generator (tools/support_loadout.py) against src/support_loadout.h; with the game installed, the
# files it makes from Root.cpk (read only).
add_test(NAME support_loadout_ini COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/support_loadout_ini_test.py")
set_tests_properties(support_loadout_ini PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# The installer's headless loadout-generate the in-game menu runs (no prompt, new files only, status file).
add_test(NAME loadout_generate COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/loadout_generate_test.py")
set_tests_properties(loadout_generate PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# The room / HQ 3D preview's menu SGOs (tools/menu_preview.py): ledger install / remove, the installer's wiring; with the
# game installed, the files built from Root.cpk (read only).
add_test(NAME menu_preview COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/menu_preview_test.py")
set_tests_properties(menu_preview PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# The preview's signatures, vtable slots and tables against the game's EDF.dll / Root.cpk (read only; skipped without).
add_test(NAME room_preview_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/room_preview_native_audit.py")
set_tests_properties(room_preview_native PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# The EDF4.1 outfit's signatures, call sites and vtables against the game's EDF.dll, its files against the shipped
# registry and the plugin's paths (read only; the EDF.dll / Root.cpk parts skipped without the game).
add_test(NAME outfit41_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/outfit41_native_audit.py")
set_tests_properties(outfit41_native PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# The slim generator install copies for the menu and the room pre-sync (tools/loadout_generator.py, build_release.py).
add_test(NAME slim_generator COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/slim_generator_test.py")
set_tests_properties(slim_generator PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
add_test(NAME frozen_cockpit_package COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_frozen_cockpit_package.py")
set_tests_properties(frozen_cockpit_package PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
add_test(NAME texture_acceleration COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_texture_acceleration.py")
add_test(NAME model_workers COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_model_workers.py")
add_test(NAME native_cmpl COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_native_cmpl.py")
add_test(NAME native_helpers COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_native_helpers.py")
# tools/edf41_maps.py converting maps in processes of their own writes what one by one writes (skipped without the games)
add_test(NAME edf41_maps_parallel COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_edf41_maps_parallel.py")
set_tests_properties(texture_acceleration model_workers edf41_maps_parallel PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# Exported share bundles must include the current mandatory maps, sounds and menu assets, not only a valid old manifest.
add_test(NAME edf41_install_safety COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_edf41_install_safety.py")
set_tests_properties(edf41_install_safety PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
add_test(NAME optics_model_reference COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_optics_model_reference.py")
set_tests_properties(optics_model_reference PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# The room / HQ menu's layout patch (tools/room_layout.py): stock members unchanged, the names src/room_ui_logic.h uses.
add_test(NAME room_layout COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/room_layout_test.py")
set_tests_properties(room_layout PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# The native dialogs' code checks (src/room_dialog.cpp, tools/room_dialog_sigs.py) against EDF.dll (skipped without the game).
add_test(NAME room_dialog_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/room_dialog_native_audit.py")
set_tests_properties(room_dialog_native PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# The room menu screen's code checks (src/room_ui.cpp) against EDF.dll read as a file (skipped without the game).
add_test(NAME room_ui_sigs COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/room_ui_sig_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(room_ui_sigs PROPERTIES SKIP_RETURN_CODE 77)
# The installer side of the room menu's review fixes: the ledger merged on save, every Mods writer under the generator's
# lock, the room layouts never stopping an install, stale generated variant files removed (a fake game directory).
add_test(NAME installer_lock COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/installer_lock_test.py")
set_tests_properties(installer_lock PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# The menu screen's switches (RoomLoadout / RoomPreSync) and screen identity, from its source (no game needed).
add_test(NAME room_ui_wiring COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/room_ui_wiring_guard.py")
# EDF6Coop's member attributes taken in one place, the host's switches read through host_settings.h (tests/coop_attr_guard.py).
add_test(NAME coop_attr_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/coop_attr_guard.py")
# The room menu's palette (src/hq_palette.inc) against the game's HQ colour-select chips (Root.cpk CUSTOMCOLOR*.SGO, EDF.dll
# read as files; skipped without the game).
add_test(NAME hq_palette COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/hq_palette_audit.py")
set_tests_properties(hq_palette PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT PYTHONIOENCODING=utf-8)
add_test(NAME support_resources COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/support_resource_audit.py")
set_tests_properties(support_resources PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME online_gate_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/online_gate_guard.py")
# The sea rescue comes only through the support deployment (a catalog entry, real pilot seated): no seatless spawner.
add_test(NAME rescue_support_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/rescue_support_guard.py")
# The installer's logs send only crash dumps this install wrote (tools/testhub.py dump_matches).
add_test(NAME testhub_dump_source COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/testhub_dump_source_test.py")
set_tests_properties(rescue_support_guard PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# The rescue's cancel reasons, its takeoff point, and the room's build notice (pure: src/rescue_logic.h, support_entry.h,
# version_notice.h).
add_executable(rescue_logic_test tests/rescue_logic_test.cpp)
target_compile_options(rescue_logic_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME rescue_logic COMMAND rescue_logic_test)
# The debug spawn tool ships off and an upgraded ini keeps it off; its natives and stock SGOs against the installed game
# (skip 77 without it).
add_test(NAME debug_spawn_ini COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/debug_spawn_ini_test.py")
set_tests_properties(debug_spawn_ini PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
add_test(NAME debug_spawn_resources COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/debug_spawn_resource_audit.py")
set_tests_properties(debug_spawn_resources PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT PYTHONIOENCODING=utf-8)
# The recorded sound pack (tools/sound_pack.py) on a stand-in archive: every clip it names is one the plugin reads, as
# the plugin reads it (ReadWav), the loops seamless, the recordings' DC out, never louder than the plugin's own clip.
add_test(NAME sound_pack COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/sound_pack_test.py")
set_tests_properties(sound_pack PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# It never launches a crewed aircraft without its crew (JetLaunch / HeliLaunch): those come as support calls.
add_test(NAME debug_spawn_launch_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/debug_spawn_launch_guard.py")
set_tests_properties(debug_spawn_launch_guard PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# An empty vehicle it makes is CreateVehicle2's (0x633280), never SetLevel's: off the native AI list, so the player in
# it drives and fires; with EDF6_NATIVE_DLL its signatures are compared with the DLL.
add_test(NAME debug_spawn_vehicle_level_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/debug_spawn_vehicle_level_guard.py")
set_tests_properties(debug_spawn_vehicle_level_guard PROPERTIES ENVIRONMENT "PYTHONIOENCODING=utf-8;EDF6_NATIVE_DLL=${EDF6_NATIVE_DLL}")
# The doll drone's doll follows its drone once a frame, whoever flies it (src/jet_carrier.cpp DollsFollow).
add_test(NAME doll_follow_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/doll_follow_guard.py")
set_tests_properties(doll_follow_guard PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# The plugin's launched drones fly with no one aboard; crewed aircraft never launched empty (src/jet_internal.h Unmanned).
add_test(NAME unmanned_launch_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/unmanned_launch_guard.py")
set_tests_properties(unmanned_launch_guard PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
add_test(NAME npc_car_aim_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/npc_car_aim_guard.py")
set_tests_properties(npc_car_aim_guard PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
# Proportional navigation (src/pn.h) against the stock pure pursuit at the same turn limit.
add_executable(pn_test tests/pn_test.cpp)
target_compile_options(pn_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME pn COMMAND pn_test)

# The first person's arithmetic (src/fp_view.h: the viewmodel transform, the collapse, the eye) offline
# (tools/fp_view_check.cpp). cmake --build build --target fp_view_check && buildp_view_check.exe
# The cockpit screens' canvas map (src/cockpit_canvas.h), checked offline.
add_executable(cockpit_canvas_check EXCLUDE_FROM_ALL tools/cockpit_canvas_check.cpp)
target_compile_options(cockpit_canvas_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(cockpit_canvas_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The ported content unlock's offline parts (src/unlock_ported.h): which packs, the mission select's rows, the weapon rows.
add_executable(unlock_ported_check EXCLUDE_FROM_ALL tools/unlock_ported_check.cpp)
target_compile_options(unlock_ported_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(unlock_ported_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_executable(fp_view_check EXCLUDE_FROM_ALL tools/fp_view_check.cpp)
target_compile_options(fp_view_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(fp_view_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Run the feature checks in CI as well as locally; these targets remain optional for plugin-only builds.
set(EDF6_OFFLINE_CHECKS append_log_test common_host_check pjet_kinds heli_aim_check heli_fire_check
    nix_twist_check seat_aim_check rounds_check turret_cam_check turret_lead_check warn_check grape_turret_check vehicle_ram_check gravity_check katyusha_rack_check stab_check vsound_check map_cam_check map_cmd_check crawl_check hector_ai_check edf41_fortress_check map_marks_check hud_cue_check emc_check ultimate_check sound_place_check proteus_check sidecar_board_check map_hud_check pause_clock_check hudtext_check gunship_muzzle_check exhaust_pose_check airbound_check rotor_clear_check hover_lift_check view_clip_check npc_ai_check npc_backpack_check sazabi_pose_check sazabi_flight_check sazabi_funnel_check gunsight_check reticle_check formation_check pickup_check map_buttons_check scopeview_check sazabi_assist_check armor_hud_check seat_switch_check ground_contact_check transport_check seat_events_check airdrop_check aim_line_want_check air_chase_check wasted_fire_check hit_predict_check own_round_check exhaust_nozzles_check damage_stats_check fp_view_check cockpit_canvas_check unlock_ported_check)
add_custom_target(offline_checks DEPENDS ${EDF6_OFFLINE_CHECKS} hud_view jet_obstacle_sim)
add_dependencies(offline_checks transport_runtime_test edf41fortress_runtime_test npc_backpack_native_check)
add_test(NAME npc_backpack_native_check COMMAND npc_backpack_native_check)
add_dependencies(offline_checks stab_native_readback_test)
add_test(NAME stab_native_readback COMMAND stab_native_readback_test "${EDF6_NATIVE_DLL}")
set_tests_properties(stab_native_readback PROPERTIES SKIP_RETURN_CODE 77)
add_dependencies(offline_checks turret_cam_runtime_check)
add_dependencies(offline_checks highcam_input_test)
foreach(check IN LISTS EDF6_OFFLINE_CHECKS)
    add_test(NAME ${check} COMMAND ${check})
endforeach()
add_test(NAME hud_layout COMMAND hud_view --out "${CMAKE_BINARY_DIR}/hud-review")
# The jets' attack runs offline (tools/jet_obstacle_sim.cpp --selftest): Guard's predicted pull-out (src/jet_pullout.h) clears
# the ground in every dive it is given, and a called strike jet's guns and missiles fire as it arrives.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/jet-attack-runs")
add_test(NAME jet_attack_runs COMMAND jet_obstacle_sim --selftest --out "${CMAKE_BINARY_DIR}/jet-attack-runs")
# The NPC flyers' soft edge (src/airbound.h): its rules (airbound_check) and the jets' real flight code flown at the play
# edge (jet_obstacle_sim --edge-suite).
add_test(NAME jet_edge_suite COMMAND jet_obstacle_sim --edge-suite WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
# Ground attack runs on a stock-size map, whose soft box is narrower than the strike's own run (jet_obstacle_sim
# --strike-room-suite; the 2026-10-09 log's multirole jets that never fired).
add_test(NAME jet_strike_room COMMAND jet_obstacle_sim --strike-room-suite --out "${CMAKE_BINARY_DIR}")
# The play area's walls (src/playarea.h: the ground's edge found by map rays, the walls' turn), a jet flown out at them
# offline (tools/play_area_check.cpp). Not part of the plugin build:
# cmake --build build --target play_area_check && build\play_area_check.exe
add_executable(play_area_check EXCLUDE_FROM_ALL tools/play_area_check.cpp)
target_include_directories(play_area_check PRIVATE src)
target_compile_options(play_area_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(play_area_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_dependencies(offline_checks play_area_check)
add_test(NAME play_area_check COMMAND play_area_check)
add_dependencies(offline_checks pjet_turn_sim)
add_test(NAME pjet_turn_check COMMAND pjet_turn_sim --check)
# The hail's approach flown offline: the logged crashes (legacy law) against src/hail_glide.h, and a sweep.
add_executable(hail_glide_check EXCLUDE_FROM_ALL tools/hail_glide_check.cpp)
target_include_directories(hail_glide_check PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(hail_glide_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(hail_glide_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_dependencies(offline_checks hail_glide_check)
add_test(NAME hail_glide COMMAND hail_glide_check)

add_executable(map_command_runtime_check EXCLUDE_FROM_ALL tools/map_command_runtime_check.cpp)
target_link_libraries(map_command_runtime_check PRIVATE edf6common user32)
target_compile_options(map_command_runtime_check PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks map_command_runtime_check)
add_dependencies(offline_checks support_dispatch_test)
add_dependencies(offline_checks support_loadout_test support_config_test support_spawn_test room_menu_test room_preview_test outfit41_test room_ui_test room_dialog_test game_dialog_test ini_text_test coop_attr_test loadout_writer_test room_coop_capability_test room_sync_test)
add_dependencies(offline_checks support_infantry_route_test)
add_dependencies(offline_checks support_vehicle_route_test)
add_dependencies(offline_checks support_policy_test)
add_test(NAME map_command_runtime COMMAND map_command_runtime_check)
add_test(NAME jet_player_rotor_suite COMMAND jet_obstacle_sim --player-rotor-suite)
add_test(NAME jet_ground_settle COMMAND jet_obstacle_sim --ground-settle-suite)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME playerjet_catch COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/playerjet_catch_check.py"
    --compiler "${CMAKE_CXX_COMPILER}")

add_executable(lockon_test EXCLUDE_FROM_ALL tests/lockon_test.cpp)
target_link_libraries(lockon_test PRIVATE edf6common)
target_include_directories(lockon_test PRIVATE src common)
target_compile_options(lockon_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks lockon_test)
add_test(NAME lockon COMMAND lockon_test)

add_executable(tvguide_test EXCLUDE_FROM_ALL tests/tvguide_test.cpp)
target_link_libraries(tvguide_test PRIVATE edf6common)
target_compile_options(tvguide_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks tvguide_test)
add_test(NAME tvguide COMMAND tvguide_test)

add_executable(crew_first_use_test EXCLUDE_FROM_ALL tests/crew_first_use_test.cpp)
target_link_libraries(crew_first_use_test PRIVATE edf6common)
target_compile_options(crew_first_use_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(crew_first_use_test PRIVATE /OPT:REF)
add_dependencies(offline_checks crew_first_use_test)
add_test(NAME crew_first_use COMMAND crew_first_use_test)

foreach(check IN ITEMS proteus_frame_test gunner_aim_object_test stab_interpolation_test heli_player_input_test)
    add_executable(${check} EXCLUDE_FROM_ALL tests/${check}.cpp)
    target_include_directories(${check} PRIVATE src common)
    target_compile_options(${check} PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
    add_dependencies(offline_checks ${check})
    add_test(NAME ${check} COMMAND ${check})
endforeach()
target_link_libraries(proteus_frame_test PRIVATE edf6common user32)
add_executable(proteus_net_test tests/proteus_net_test.cpp)
target_compile_options(proteus_net_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME proteus_net COMMAND proteus_net_test)
add_executable(proteus_net_runtime_test EXCLUDE_FROM_ALL tests/proteus_net_runtime_test.cpp)
target_include_directories(proteus_net_runtime_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(proteus_net_runtime_test PRIVATE edf6common user32)
target_compile_options(proteus_net_runtime_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(proteus_net_runtime_test PRIVATE /OPT:REF)
add_dependencies(offline_checks proteus_net_test proteus_net_runtime_test)
add_test(NAME proteus_net_runtime COMMAND proteus_net_runtime_test)
add_test(NAME proteus_net_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/proteus_net_guard.py")
add_executable(proteus_net_native_test EXCLUDE_FROM_ALL tests/proteus_net_native_test.cpp src/drill_net.cpp src/sazabi_net.cpp)
target_include_directories(proteus_net_native_test PRIVATE src common)
target_link_libraries(proteus_net_native_test PRIVATE edf6common user32)
target_compile_options(proteus_net_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
target_link_libraries(gunner_aim_object_test PRIVATE edf6common)
target_link_libraries(heli_player_input_test PRIVATE edf6common user32)
add_executable(jetsound_check EXCLUDE_FROM_ALL tools/jetsound_check.cpp)
target_include_directories(jetsound_check PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(jetsound_check PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks jetsound_check)
add_test(NAME jetsound_check COMMAND jetsound_check)
add_test(NAME vehicle_cas COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_cas_pose.py")
add_test(NAME heli_request_setup COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_heli_setup.py")
# The Begaruta family (Proteus too) chains its per-frame slot 4; with the game present, against EDF.dll.
add_test(NAME begaruta_family_profile COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/begaruta_family_native_profile_test.py")
add_test(NAME proteus_visual_profile COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/proteus_visual_native_profile.py")
set_tests_properties(proteus_visual_profile PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME proteus_assets COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/test_proteus_assets.py")
add_test(NAME proteus_text COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/proteus_text_test.py")
add_test(NAME proteus_install COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/proteus_install_test.py")
add_test(NAME proteus_range COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/proteus_range_test.py")
add_test(NAME vehicle_shapes COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/test_vehicle_model_shapes.py")
add_test(NAME aircraft_collision COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/aircraft_collision_check.py")

foreach(check IN ITEMS heli_command_test jet_command_test bullet_pass_test rocket_guide_prediction_test rocket_impact_readout_test npc_post_test map_command_identity_test net_session_lifecycle_test)
    add_executable(${check} EXCLUDE_FROM_ALL tests/${check}.cpp)
    target_include_directories(${check} PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(${check} PRIVATE edf6common user32)
    target_compile_options(${check} PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
    target_link_options(${check} PRIVATE /OPT:REF)
    add_dependencies(offline_checks ${check})
    add_test(NAME ${check} COMMAND ${check})
endforeach()
target_sources(jet_command_test PRIVATE src/jet_flight.cpp)
target_sources(map_command_runtime_check PRIVATE src/command_unit.cpp)
target_sources(map_command_identity_test PRIVATE src/command_unit.cpp)
target_sources(npc_post_test PRIVATE src/command_unit.cpp)
add_executable(npc_core_check EXCLUDE_FROM_ALL tools/npc_core_check.cpp src/ground_navigation.cpp)
target_include_directories(npc_core_check PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(npc_core_check PRIVATE edf6common user32)
target_compile_options(npc_core_check PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(npc_core_check PRIVATE /OPT:REF)
add_dependencies(offline_checks npc_core_check)
add_test(NAME npc_core_check COMMAND npc_core_check)
set(EDF6_NATIVE_DLL "" CACHE FILEPATH "Optional supported EDF.dll for isolated native checks")
add_test(NAME net_session_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/net_session_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(net_session_native PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "PYTHONIOENCODING=utf-8;PYTHONUTF8=1")
add_test(NAME item_pickup_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/item_pickup_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(item_pickup_native PROPERTIES SKIP_RETURN_CODE 77)
# The stock join mark (hidden while the map recruits) and the free-squad cover a recruited squad keeps (src/npcai.cpp
# InstallFreeSquadCover): the patched bytes and the original visit run on stand-ins against the real EDF.dll.
add_test(NAME npc_cover_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/npc_cover_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(npc_cover_native PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "PYTHONIOENCODING=utf-8;PYTHONUTF8=1")
# The Proteus shield's round carries its model into the IFC (src/ifc_model.h; test hub report #6, the B-key crash):
# the copy against fake visitors, its wiring in jet_bay.cpp / make_proteus.py, and the contract on the real EDF.dll.
add_executable(ifc_model_test EXCLUDE_FROM_ALL tests/ifc_model_test.cpp)
target_compile_options(ifc_model_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks ifc_model_test)
add_test(NAME ifc_model COMMAND ifc_model_test)
add_test(NAME ifc_model_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/ifc_model_guard.py")
add_test(NAME ifc_model_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/ifc_model_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(ifc_model_native PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME playerjet_recovery COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/playerjet_recovery_check.py"
    --build-dir "${CMAKE_BINARY_DIR}" --negative-controls)
# Production launch heat and cooling, including the overheat latch before the launch frame's cooling.
add_executable(drill_heat_test EXCLUDE_FROM_ALL tests/drill_heat_test.cpp)
target_link_libraries(drill_heat_test PRIVATE edf6common user32)
target_include_directories(drill_heat_test PRIVATE src common)
target_compile_options(drill_heat_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(drill_heat_test PRIVATE /OPT:REF)
add_dependencies(offline_checks drill_heat_test)
add_test(NAME drill_heat COMMAND drill_heat_test)
add_executable(drill_sync_test EXCLUDE_FROM_ALL tests/drill_sync_test.cpp)
target_link_libraries(drill_sync_test PRIVATE edf6common user32)
target_include_directories(drill_sync_test PRIVATE src common)
target_compile_options(drill_sync_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(drill_sync_test PRIVATE /OPT:REF)
add_dependencies(offline_checks drill_net_test drill_sync_test seat_net_test)
add_test(NAME drill_sync COMMAND drill_sync_test)
add_executable(drill_launch_test EXCLUDE_FROM_ALL tests/drill_launch_test.cpp)
target_link_libraries(drill_launch_test PRIVATE edf6common user32)
target_include_directories(drill_launch_test PRIVATE src common)
target_compile_options(drill_launch_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(drill_launch_test PRIVATE /OPT:REF)
add_dependencies(offline_checks drill_launch_test)
add_test(NAME drill_launch COMMAND drill_launch_test)
add_executable(drill_hud_binding_test EXCLUDE_FROM_ALL tests/drill_hud_binding_test.cpp)
target_include_directories(drill_hud_binding_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(drill_hud_binding_test PRIVATE edf6common)
target_compile_options(drill_hud_binding_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks drill_hud_binding_test)
add_test(NAME drill_hud_binding COMMAND drill_hud_binding_test)
add_test(NAME drill_model_rotation COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/drill_model_rotation_test.py")
# The EDF4.1 classes' network layer (src/edf41net.h): the wire (always), the production code on the real EDF.dll (by hand:
# edf41net_native_test <EDF.dll>, as drill_net_native_test).
add_executable(edf41net_test tests/edf41net_test.cpp)
target_compile_options(edf41net_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME edf41net COMMAND edf41net_test)
# UfoRobo's death is final (src/edf41robo_body.h: after Die only S10 -> S11).
add_executable(edf41robo_body_test tests/edf41robo_body_test.cpp)
target_compile_options(edf41robo_body_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME edf41robo_body COMMAND edf41robo_body_test)
# UfoRobo's robot AI as 4.1's (src/edf41robo_ai.h): A5 holds the AI through the attack, the robot walks between attacks.
add_executable(edf41robo_ai_test tests/edf41robo_ai_test.cpp)
target_compile_options(edf41robo_ai_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME edf41robo_ai COMMAND edf41robo_ai_test)
# EDF6 DragonSmall's get-up rule in its knock-down state (src/edf41dragon_wake.h), logged for 4.1's giant dragon.
add_executable(edf41dragon_wake_test tests/edf41dragon_wake_test.cpp)
target_compile_options(edf41dragon_wake_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME edf41dragon_wake COMMAND edf41dragon_wake_test)
# UfoMother301's rest poses (src/edf41mother_pose.h: the hull closed, the cannons folded until the script enables them).
add_executable(edf41mother_pose_test tests/edf41mother_pose_test.cpp)
target_compile_options(edf41mother_pose_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME edf41mother_pose COMMAND edf41mother_pose_test)
# A ported part's hit bodies on a layer the area damage's query meets (src/edf41_hitlayer.h: rockets / missiles reach the parts).
add_executable(edf41_hitlayer_test tests/edf41_hitlayer_test.cpp)
target_compile_options(edf41_hitlayer_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME edf41_hitlayer COMMAND edf41_hitlayer_test)
add_executable(edf41net_native_test EXCLUDE_FROM_ALL tests/edf41net_native_test.cpp)
target_link_libraries(edf41net_native_test PRIVATE edf6common user32)
target_include_directories(edf41net_native_test PRIVATE src common)
target_compile_options(edf41net_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_executable(drill_net_native_test EXCLUDE_FROM_ALL tests/drill_net_native_test.cpp)
target_link_libraries(drill_net_native_test PRIVATE edf6common user32)
target_include_directories(drill_net_native_test PRIVATE src common)
target_compile_options(drill_net_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_executable(online_authority_seat_test EXCLUDE_FROM_ALL tests/online_authority_seat_test.cpp)
target_link_libraries(online_authority_seat_test PRIVATE edf6common user32)
target_include_directories(online_authority_seat_test PRIVATE src common)
target_compile_options(online_authority_seat_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(online_authority_seat_test PRIVATE /OPT:REF)
add_dependencies(offline_checks online_authority_seat_test)
add_test(NAME online_authority_seat COMMAND online_authority_seat_test)

add_executable(sazabi_runtime_test EXCLUDE_FROM_ALL tests/sazabi_runtime_test.cpp)
target_link_libraries(sazabi_runtime_test PRIVATE edf6common user32)
target_include_directories(sazabi_runtime_test PRIVATE src common)
target_compile_options(sazabi_runtime_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(sazabi_runtime_test PRIVATE /OPT:REF)
add_dependencies(offline_checks sazabi_runtime_test)
add_test(NAME sazabi_runtime COMMAND sazabi_runtime_test)
add_executable(sazabi_net_test tests/sazabi_net_test.cpp)
target_compile_options(sazabi_net_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME sazabi_net COMMAND sazabi_net_test)
add_executable(sazabi_net_runtime_test EXCLUDE_FROM_ALL tests/sazabi_net_runtime_test.cpp)
target_include_directories(sazabi_net_runtime_test PRIVATE src common)
target_link_libraries(sazabi_net_runtime_test PRIVATE edf6common user32)
target_compile_options(sazabi_net_runtime_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(sazabi_net_runtime_test PRIVATE /OPT:REF)
add_dependencies(offline_checks sazabi_net_test sazabi_net_runtime_test)
add_test(NAME sazabi_net_runtime COMMAND sazabi_net_runtime_test)
# The Sazabi's trigger requests (src/sazabi_arms.h Ask): a tap fires once, a release no extra round.
add_executable(sazabi_ask_test EXCLUDE_FROM_ALL tests/sazabi_ask_test.cpp)
target_include_directories(sazabi_ask_test PRIVATE src common third_party/EDFModLoader)
target_compile_options(sazabi_ask_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks sazabi_ask_test)
add_test(NAME sazabi_ask COMMAND sazabi_ask_test)
add_test(NAME sazabi_net_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/sazabi_net_guard.py")
add_executable(sazabi_net_native_test EXCLUDE_FROM_ALL tests/sazabi_net_native_test.cpp src/drill_net.cpp)
target_include_directories(sazabi_net_native_test PRIVATE src common)
target_link_libraries(sazabi_net_native_test PRIVATE edf6common user32)
target_compile_options(sazabi_net_native_test PRIVATE /W4 /WX /utf-8 /permissive-)

add_executable(npc_gunner_aim_test EXCLUDE_FROM_ALL tests/npc_gunner_aim_test.cpp)
target_include_directories(npc_gunner_aim_test PRIVATE src common)
target_link_libraries(npc_gunner_aim_test PRIVATE edf6common)
target_compile_options(npc_gunner_aim_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks npc_gunner_aim_test)
add_test(NAME npc_gunner_aim COMMAND npc_gunner_aim_test)
set(EDF6_NATIVE_DLL "" CACHE FILEPATH "Optional supported EDF.dll for isolated native gunner checks")
add_test(NAME sightzoom_native COMMAND sightzoom_test "${EDF6_NATIVE_DLL}")
set_tests_properties(sightzoom_native PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME npc_gunner_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/npc_gunner_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(npc_gunner_native PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME npc_mech_aim_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/npc_mech_aim_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(npc_mech_aim_native PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT PYTHONIOENCODING=utf-8)
# The NPCs' backpack weapons: the source guard always, the code read against an installed EDF.dll when one is given.
add_test(NAME npc_backpack_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/npc_backpack_guard.py")
add_test(NAME npc_backpack_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/npc_backpack_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(npc_backpack_native PROPERTIES SKIP_RETURN_CODE 77)
add_executable(airframe_constructor_test EXCLUDE_FROM_ALL tests/airframe_constructor_test.cpp)
target_link_libraries(airframe_constructor_test PRIVATE edf6common)
target_compile_options(airframe_constructor_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(airframe_constructor_test PRIVATE /OPT:REF)
add_dependencies(offline_checks airframe_constructor_test)
add_test(NAME airframe_constructor COMMAND airframe_constructor_test)
add_test(NAME airframe_constructor_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/airframe_constructor_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(airframe_constructor_native PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME aircraft_locator_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/aircraft_locator_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(aircraft_locator_native PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME medic_native_heal COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/medic_native_heal_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(medic_native_heal PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME heli_yaw_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/heli_yaw_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(heli_yaw_native PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME katyusha_native_rack COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/katyusha_native_rack_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(katyusha_native_rack PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME supportunit_allies_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/supportunit_allies_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(supportunit_allies_native PROPERTIES SKIP_RETURN_CODE 77)

add_executable(edf5_campaign_native_test EXCLUDE_FROM_ALL tests/edf5_campaign_native_test.cpp)
target_include_directories(edf5_campaign_native_test PRIVATE src common)
target_link_libraries(edf5_campaign_native_test PRIVATE edf6common)
target_compile_options(edf5_campaign_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks edf5_campaign_native_test)
add_test(NAME edf5_campaign_native COMMAND edf5_campaign_native_test "${EDF6_NATIVE_DLL}")
add_test(NAME edf5_campaign_partial_install COMMAND edf5_campaign_native_test "${EDF6_NATIVE_DLL}" 3)
add_test(NAME edf5_campaign_chained COMMAND edf5_campaign_native_test "${EDF6_NATIVE_DLL}" chain)
add_test(NAME edf5_campaign_foreign_site COMMAND edf5_campaign_native_test "${EDF6_NATIVE_DLL}" foreign)
add_test(NAME ported_missions_unlock_native COMMAND edf5_campaign_native_test "${EDF6_NATIVE_DLL}" unlock)
# The route guide hook on a private EDF.dll mapping: every constructor call redirected, each caller's guide corrected.
# The hit prediction against the game's own hit-acceptance overrides on a private EDF.dll mapping
# (tests/hit_accept_native_test.cpp; src/wastedfire.cpp, src/hit_predict.h).
add_executable(hit_accept_native_test EXCLUDE_FROM_ALL tests/hit_accept_native_test.cpp)
target_include_directories(hit_accept_native_test PRIVATE src common)
target_link_libraries(hit_accept_native_test PRIVATE edf6common)
target_compile_options(hit_accept_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks hit_accept_native_test)
add_test(NAME hit_accept_native COMMAND hit_accept_native_test "${EDF6_NATIVE_DLL}")
set_tests_properties(hit_accept_native PROPERTIES SKIP_RETURN_CODE 77)
add_executable(route_guide_native_test EXCLUDE_FROM_ALL tests/route_guide_native_test.cpp)
target_include_directories(route_guide_native_test PRIVATE src common)
target_link_libraries(route_guide_native_test PRIVATE edf6common)
target_compile_options(route_guide_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks route_guide_native_test)
add_test(NAME route_guide_native COMMAND route_guide_native_test "${EDF6_NATIVE_DLL}")
add_test(NAME route_guide_foreign_site COMMAND route_guide_native_test "${EDF6_NATIVE_DLL}" foreign)
add_test(NAME route_guide_partial_install COMMAND route_guide_native_test "${EDF6_NATIVE_DLL}" partial)
set_tests_properties(route_guide_native route_guide_foreign_site route_guide_partial_install PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(edf5_campaign_native edf5_campaign_partial_install edf5_campaign_chained edf5_campaign_foreign_site ported_missions_unlock_native
                     PROPERTIES SKIP_RETURN_CODE 77)
# The mission packs' save marker through EDF.dll's own reset / load-apply / checksum / seen writers (private mapping).
add_executable(mission_pack_save_native_test EXCLUDE_FROM_ALL tests/mission_pack_save_native_test.cpp)
target_compile_options(mission_pack_save_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks mission_pack_save_native_test)
add_test(NAME mission_pack_save_native COMMAND mission_pack_save_native_test "${EDF6_NATIVE_DLL}")
set_tests_properties(mission_pack_save_native PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME edf5_campaign_lifecycle COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_edf5_campaign_lifecycle.py")

add_executable(npc_gunners_online_test EXCLUDE_FROM_ALL tests/npc_gunners_online_test.cpp src/ground_navigation.cpp)
target_link_libraries(npc_gunners_online_test PRIVATE edf6common user32)
target_include_directories(npc_gunners_online_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(npc_gunners_online_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(npc_gunners_online_test PRIVATE /OPT:REF)
add_dependencies(offline_checks npc_gunners_online_test)
add_test(NAME npc_gunners_online COMMAND npc_gunners_online_test)
# EDF6Autopilot: a test-only plugin for background measurement runs (tests/autopilot/autopilot.cpp, driven by
# tests/autopilot/drive.py); a MODULE into build/tools, never shipped:
# cmake --build build --target EDF6Autopilot
add_library(EDF6Autopilot MODULE EXCLUDE_FROM_ALL tests/autopilot/autopilot.cpp tests/autopilot/airdrop_probe.cpp
            tests/autopilot/wdenergy_probe.cpp tests/autopilot/hector_probe.cpp)
target_include_directories(EDF6Autopilot PRIVATE third_party/EDFModLoader)
target_link_libraries(EDF6Autopilot PRIVATE edf6common psapi)
target_include_directories(EDF6Autopilot PRIVATE common)
target_compile_options(EDF6Autopilot PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(EDF6Autopilot PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

add_executable(fieldshare_test tests/fieldshare_test.cpp)
target_include_directories(fieldshare_test PRIVATE src common)
target_link_libraries(fieldshare_test PRIVATE edf6common)
target_compile_options(fieldshare_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME fieldshare COMMAND fieldshare_test)
add_test(NAME autopilot_driver COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/autopilot/test_drive.py")
# One real-game test at a time per game folder, first come first served (pylib/gamelease.py, tools/gamequeue.py).
add_test(NAME game_lease COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/game_lease_test.py")

add_executable(ground_navigation_test tests/ground_navigation_test.cpp)
target_compile_options(ground_navigation_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME ground_navigation COMMAND ground_navigation_test)
add_executable(payload_runtime_test EXCLUDE_FROM_ALL tests/payload_runtime_test.cpp)
target_include_directories(payload_runtime_test PRIVATE src common)
target_link_libraries(payload_runtime_test PRIVATE edf6common user32)
target_compile_options(payload_runtime_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(payload_runtime_test PRIVATE /OPT:REF)
add_dependencies(offline_checks payload_runtime_test)
add_test(NAME payload_runtime COMMAND payload_runtime_test)
add_executable(payload_sight_test EXCLUDE_FROM_ALL tests/payload_sight_test.cpp)
target_include_directories(payload_sight_test PRIVATE src common)
target_link_libraries(payload_sight_test PRIVATE edf6common user32)
target_compile_options(payload_sight_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(payload_sight_test PRIVATE /OPT:REF)
add_dependencies(offline_checks payload_sight_test)
add_test(NAME payload_sight COMMAND payload_sight_test)

add_executable(real_turret_crew_test EXCLUDE_FROM_ALL tests/real_turret_crew_test.cpp)
target_include_directories(real_turret_crew_test PRIVATE common)
target_link_libraries(real_turret_crew_test PRIVATE edf6common)
target_compile_options(real_turret_crew_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(real_turret_crew_test PRIVATE /OPT:REF)
add_dependencies(offline_checks real_turret_crew_test)
add_test(NAME real_turret_crew COMMAND real_turret_crew_test)
target_sources(EDF6VehicleCrew PRIVATE src/mod_room.cpp)
add_executable(mod_room_test tests/mod_room_test.cpp)
target_include_directories(mod_room_test PRIVATE common)
target_link_libraries(mod_room_test PRIVATE edf6common)
target_compile_options(mod_room_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME mod_room_isolation COMMAND mod_room_test "${EDF6_NATIVE_DLL}")
add_executable(support_soldier_test EXCLUDE_FROM_ALL tests/support_soldier_test.cpp)
target_include_directories(support_soldier_test PRIVATE src common)
target_link_libraries(support_soldier_test PRIVATE edf6common)
target_compile_options(support_soldier_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks support_soldier_test)
add_test(NAME support_soldier COMMAND support_soldier_test)
add_executable(support_soldier_create_native_test EXCLUDE_FROM_ALL tests/support_soldier_create_native_test.cpp)
target_include_directories(support_soldier_create_native_test PRIVATE src common)
target_link_libraries(support_soldier_create_native_test PRIVATE edf6common)
target_compile_options(support_soldier_create_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks support_soldier_create_native_test)
add_test(NAME support_soldier_create_native COMMAND support_soldier_create_native_test "${EDF6_NATIVE_DLL}")
set_tests_properties(support_soldier_create_native PROPERTIES SKIP_RETURN_CODE 77)
add_executable(create_jet_native_test EXCLUDE_FROM_ALL tests/create_jet_native_test.cpp)
target_include_directories(create_jet_native_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(create_jet_native_test PRIVATE edf6common)
target_compile_options(create_jet_native_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(create_jet_native_test PRIVATE /OPT:REF)
add_dependencies(offline_checks create_jet_native_test)
add_test(NAME create_jet_native COMMAND create_jet_native_test "${EDF6_NATIVE_DLL}")
set_tests_properties(create_jet_native PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME support_soldier_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/support_soldier_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(support_soldier_native PROPERTIES SKIP_RETURN_CODE 77)
# Reliable host-authoritative map support protocol (also compiled in production).
target_sources(EDF6VehicleCrew PRIVATE src/support_protocol.cpp src/support_net.cpp)
add_executable(support_protocol_test tests/support_protocol_test.cpp src/support_protocol.cpp)
target_compile_options(support_protocol_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME support_protocol COMMAND support_protocol_test)
add_executable(support_net_runtime_test tests/support_net_runtime_test.cpp src/support_protocol.cpp src/plugin_net.cpp)
target_include_directories(support_net_runtime_test PRIVATE src common third_party/EDFModLoader)
target_compile_options(support_net_runtime_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME support_net_runtime COMMAND support_net_runtime_test)
add_executable(real_npc_crew_test EXCLUDE_FROM_ALL tests/real_npc_crew_test.cpp src/ground_navigation.cpp)
target_link_libraries(real_npc_crew_test PRIVATE edf6common user32)
target_include_directories(real_npc_crew_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(real_npc_crew_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(real_npc_crew_test PRIVATE /OPT:REF)
add_dependencies(offline_checks real_npc_crew_test)
add_test(NAME real_npc_crew COMMAND real_npc_crew_test)

add_executable(real_driver_native_test EXCLUDE_FROM_ALL tests/real_driver_native_test.cpp)
target_link_libraries(real_driver_native_test PRIVATE edf6common)
target_include_directories(real_driver_native_test PRIVATE src common)
target_compile_options(real_driver_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks real_driver_native_test)
add_test(NAME real_driver_native_fixture COMMAND real_driver_native_test)
if(EXISTS "${EDF6_NATIVE_DLL}")
    add_test(NAME real_driver_native_dll COMMAND real_driver_native_test "${EDF6_NATIVE_DLL}")
endif()

add_test(NAME npc_board_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/npc_board_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(npc_board_native PROPERTIES SKIP_RETURN_CODE 77)

add_executable(mission_crew_test EXCLUDE_FROM_ALL tests/mission_crew_test.cpp)
target_link_libraries(mission_crew_test PRIVATE edf6common)
target_include_directories(mission_crew_test PRIVATE src common)
target_compile_options(mission_crew_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks mission_crew_test)
add_test(NAME mission_crew COMMAND mission_crew_test)
add_executable(mission_crew_support_test EXCLUDE_FROM_ALL tests/mission_crew_support_test.cpp)
target_link_libraries(mission_crew_support_test PRIVATE edf6common)
target_include_directories(mission_crew_support_test PRIVATE src common)
target_compile_options(mission_crew_support_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks mission_crew_support_test)
add_test(NAME mission_crew_support COMMAND mission_crew_support_test)
# Every class in every seat (src/allclass.cpp, ini AllClassesRide; docs/all-classes-ride-re.md): the real hooks on a
# private image, the source guard, the native facts on EDF.dll and the Root.cpk census behind them.
add_executable(allclass_test EXCLUDE_FROM_ALL tests/allclass_test.cpp)
target_link_libraries(allclass_test PRIVATE edf6common)
target_include_directories(allclass_test PRIVATE src common)
target_compile_options(allclass_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks allclass_test)
add_test(NAME allclass COMMAND allclass_test)
add_test(NAME allclass_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/allclass_guard.py")
set_tests_properties(allclass_guard PROPERTIES ENVIRONMENT PYTHONIOENCODING=utf-8)
add_test(NAME allclass_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/allclass_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(allclass_native PROPERTIES SKIP_RETURN_CODE 77)
# CRI File System's loader pool (src/fsbudget.cpp; docs/cri-fs-loaders-re.md): the init sites, the config field and the
# crash a run-out pool led to, on a real EDF.dll.
add_test(NAME fsbudget_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/fsbudget_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(fsbudget_native PROPERTIES SKIP_RETURN_CODE 77)
# The battle record's rows and the counter store (src/killcount_logic.h; docs/kill-counter-re.md): the row fill's
# decoding and the tree lookup on made-up bytes, then every site src/killcount.cpp patches on a real EDF.dll.
add_executable(killcount_test EXCLUDE_FROM_ALL tests/killcount_test.cpp)
target_include_directories(killcount_test PRIVATE src)
target_compile_options(killcount_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks killcount_test)
add_test(NAME killcount COMMAND killcount_test)
add_test(NAME killcount_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/killcount_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(killcount_native PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME allclass_census COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/allclass_census_check.py" "${EDF6_GAME_DIR}")
set_tests_properties(allclass_census PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT PYTHONIOENCODING=utf-8)

add_test(NAME mission_crew_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/mission_crew_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(mission_crew_native PROPERTIES SKIP_RETURN_CODE 77)
add_executable(mission_crew_team_test EXCLUDE_FROM_ALL tests/mission_crew_team_test.cpp)
target_link_libraries(mission_crew_team_test PRIVATE edf6common)
target_include_directories(mission_crew_team_test PRIVATE src common)
target_compile_options(mission_crew_team_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks mission_crew_team_test)
add_test(NAME mission_crew_team_fixture COMMAND mission_crew_team_test)
if(EXISTS "${EDF6_NATIVE_DLL}")
    add_test(NAME mission_crew_team_dll COMMAND mission_crew_team_test "${EDF6_NATIVE_DLL}")
endif()

add_test(NAME npc_reseat_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/npc_reseat_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(npc_reseat_native PROPERTIES SKIP_RETURN_CODE 77)

# Integration checks for observed native crashes, real assets and mission participation.
target_sources(EDF6VehicleCrew PRIVATE src/mission_participants.cpp src/mission_participant_gate.cpp)
add_test(NAME stock_store_recoil COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_stock_store_recoil.py")
add_test(NAME stock_store_loadouts COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_stock_store_loadouts.py")
add_test(NAME stock_store_recoil_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/stock_store_recoil_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(stock_store_recoil_native PROPERTIES SKIP_RETURN_CODE 77)
# The coax's shot no longer runs the main gun's firing motion (src/payload.cpp InstallCoaxMotion): the native facts it
# relies on, on the real EDF.dll (docs/feedback-2026-10-12-coax-recoil.md).
add_test(NAME coax_fire_motion_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/coax_fire_motion_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(coax_fire_motion_native PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME katyusha_mount COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/katyusha_mount_check.py" "${EDF6_GAME_DIR}")
set_tests_properties(katyusha_mount PROPERTIES SKIP_RETURN_CODE 77)
foreach(check IN ITEMS mission_participants_test mission_participant_gate_test)
    add_executable(${check} EXCLUDE_FROM_ALL tests/${check}.cpp)
    target_include_directories(${check} PRIVATE src common)
    target_link_libraries(${check} PRIVATE edf6common)
    target_compile_options(${check} PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
    target_link_options(${check} PRIVATE /OPT:REF)
    add_dependencies(offline_checks ${check})
    add_test(NAME ${check} COMMAND ${check})
endforeach()
add_test(NAME mission_participants_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/mission_participants_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(mission_participants_native PROPERTIES SKIP_RETURN_CODE 77)

add_executable(npc_driver_payload_test EXCLUDE_FROM_ALL tests/npc_driver_payload_test.cpp src/ground_navigation.cpp)
target_include_directories(npc_driver_payload_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(npc_driver_payload_test PRIVATE edf6common user32)
target_compile_options(npc_driver_payload_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(npc_driver_payload_test PRIVATE /OPT:REF)
add_dependencies(offline_checks npc_driver_payload_test)
add_test(NAME npc_driver_payload COMMAND npc_driver_payload_test)
add_test(NAME npc_driver_payload_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/npc_driver_payload_native.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(npc_driver_payload_native PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME npc_vehicle_ai_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/npc_vehicle_ai_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(npc_vehicle_ai_native_audit PROPERTIES SKIP_RETURN_CODE 77)
add_executable(ground_navigation_runtime_test tests/ground_navigation_runtime_test.cpp src/ground_navigation.cpp)
target_include_directories(ground_navigation_runtime_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(ground_navigation_runtime_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME ground_navigation_runtime COMMAND ground_navigation_runtime_test)

add_executable(payload_click_test EXCLUDE_FROM_ALL tests/payload_click_test.cpp)
target_include_directories(payload_click_test PRIVATE src common)
target_link_libraries(payload_click_test PRIVATE edf6common user32)
target_compile_options(payload_click_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(payload_click_test PRIVATE /OPT:REF)
add_dependencies(offline_checks payload_click_test)
add_test(NAME payload_click COMMAND payload_click_test)

add_executable(aircraft_payload_click_test EXCLUDE_FROM_ALL tests/aircraft_payload_click_test.cpp)
target_include_directories(aircraft_payload_click_test PRIVATE src common)
target_link_libraries(aircraft_payload_click_test PRIVATE edf6common user32)
target_compile_options(aircraft_payload_click_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(aircraft_payload_click_test PRIVATE /OPT:REF)
add_dependencies(offline_checks aircraft_payload_click_test)
add_test(NAME aircraft_payload_click COMMAND aircraft_payload_click_test)

add_executable(playerjet_payload_click_test EXCLUDE_FROM_ALL tests/playerjet_payload_click_test.cpp)
target_include_directories(playerjet_payload_click_test PRIVATE src common)
target_link_libraries(playerjet_payload_click_test PRIVATE edf6common user32)
target_compile_options(playerjet_payload_click_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(playerjet_payload_click_test PRIVATE /OPT:REF)
add_dependencies(offline_checks playerjet_payload_click_test)
add_test(NAME playerjet_payload_click COMMAND playerjet_payload_click_test)
add_executable(fixed_weapon_sight_test EXCLUDE_FROM_ALL tests/fixed_weapon_sight_test.cpp src/rounds.cpp)
target_include_directories(fixed_weapon_sight_test PRIVATE src common)
target_link_libraries(fixed_weapon_sight_test PRIVATE edf6common user32)
target_compile_options(fixed_weapon_sight_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(fixed_weapon_sight_test PRIVATE /OPT:REF)
add_dependencies(offline_checks fixed_weapon_sight_test)
add_test(NAME fixed_weapon_sight COMMAND fixed_weapon_sight_test)

add_executable(heli_physical_sight_test EXCLUDE_FROM_ALL tests/heli_physical_sight_test.cpp src/launcher.cpp src/rounds.cpp)
target_include_directories(heli_physical_sight_test PRIVATE src common)
target_link_libraries(heli_physical_sight_test PRIVATE edf6common user32)
target_compile_options(heli_physical_sight_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(heli_physical_sight_test PRIVATE /OPT:REF)
add_dependencies(offline_checks heli_physical_sight_test)
add_test(NAME heli_physical_sight COMMAND heli_physical_sight_test)
add_test(NAME weapon_mount_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/weapon_mount_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(weapon_mount_native PROPERTIES SKIP_RETURN_CODE 77)
# Authenticated map command RPC shares the support extension consumer.
target_sources(EDF6VehicleCrew PRIVATE src/command_net.cpp src/command_protocol.cpp src/command_identity.cpp)
add_executable(command_protocol_test tests/command_protocol_test.cpp src/command_protocol.cpp)
target_compile_options(command_protocol_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME command_protocol COMMAND command_protocol_test)
add_executable(command_identity_test tests/command_identity_test.cpp src/support_soldier.cpp)
target_link_libraries(command_identity_test PRIVATE edf6common)
target_include_directories(command_identity_test PRIVATE src common third_party/EDFModLoader)
target_compile_options(command_identity_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME command_identity COMMAND command_identity_test "${EDF6_NATIVE_DLL}")

add_executable(payload_snapshot_test EXCLUDE_FROM_ALL tests/payload_snapshot_test.cpp)
target_include_directories(payload_snapshot_test PRIVATE src common)
target_link_libraries(payload_snapshot_test PRIVATE edf6common user32)
target_compile_options(payload_snapshot_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(payload_snapshot_test PRIVATE /OPT:REF)
add_dependencies(offline_checks payload_snapshot_test)
add_test(NAME payload_snapshot COMMAND payload_snapshot_test)
add_executable(command_native_dispatch_test tests/command_native_dispatch_test.cpp tests/command_native_id_reader.cpp
    src/command_net.cpp src/command_identity.cpp src/command_protocol.cpp src/ground_navigation.cpp)
target_link_libraries(command_native_dispatch_test PRIVATE edf6common user32)
target_include_directories(command_native_dispatch_test PRIVATE src common third_party/EDFModLoader)
target_compile_options(command_native_dispatch_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(command_native_dispatch_test PRIVATE /OPT:REF)
add_test(NAME command_native_dispatch COMMAND command_native_dispatch_test)

add_test(NAME weapon_style_data COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/weapon_style_data_check.py")
set_tests_properties(weapon_style_data PROPERTIES SKIP_RETURN_CODE 77)
add_executable(launcher_owner_test EXCLUDE_FROM_ALL tests/launcher_owner_test.cpp)
target_include_directories(launcher_owner_test PRIVATE src common)
target_link_libraries(launcher_owner_test PRIVATE edf6common)
target_compile_options(launcher_owner_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks launcher_owner_test)
add_test(NAME launcher_owner COMMAND launcher_owner_test)

add_executable(camera_payload_lifecycle_test EXCLUDE_FROM_ALL tests/camera_payload_lifecycle_test.cpp src/payload.cpp src/stores.cpp src/rounds.cpp src/launcher.cpp)
target_include_directories(camera_payload_lifecycle_test PRIVATE src common)
target_link_libraries(camera_payload_lifecycle_test PRIVATE edf6common user32)
target_compile_options(camera_payload_lifecycle_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(camera_payload_lifecycle_test PRIVATE /OPT:REF)
add_dependencies(offline_checks camera_payload_lifecycle_test)
add_test(NAME camera_payload_lifecycle COMMAND camera_payload_lifecycle_test)
# The Proteus's borrowed stock weapon mounts and native shield barrier; native mode maps EDF.dll without DllMain.
add_executable(proteus_weapon_test EXCLUDE_FROM_ALL tests/proteus_weapon_test.cpp)
target_include_directories(proteus_weapon_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(proteus_weapon_test PRIVATE edf6common user32)
target_compile_options(proteus_weapon_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(proteus_weapon_test PRIVATE /OPT:REF)
add_dependencies(offline_checks proteus_weapon_test)
add_test(NAME proteus_weapon COMMAND proteus_weapon_test)
add_test(NAME proteus_weapon_native COMMAND proteus_weapon_test "${EDF6_NATIVE_DLL}")
set_tests_properties(proteus_weapon_native PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME vehicle_optics_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/vehicle_optics_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(vehicle_optics_native PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME sight_data_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/sight_data_audit.py" --game "${EDF6_GAME_DIR}")
set_tests_properties(sight_data_audit PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME optic_install_integration COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/optic_install_integration_test.py")

add_test(NAME highcam_collision_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/highcam_collision_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(highcam_collision_native PROPERTIES SKIP_RETURN_CODE 77)

add_executable(autoturret_binding_test EXCLUDE_FROM_ALL tests/autoturret_binding_test.cpp src/turretaim.cpp)
target_include_directories(autoturret_binding_test PRIVATE src common)
target_link_libraries(autoturret_binding_test PRIVATE edf6common user32)
target_compile_options(autoturret_binding_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(autoturret_binding_test PRIVATE /OPT:REF)
add_dependencies(offline_checks autoturret_binding_test)
add_test(NAME autoturret_binding COMMAND autoturret_binding_test)
# The one player turret aim (aimlink.h V4) on production designate.cpp: keys, lock rule, lead, flak and tank alike.
add_executable(player_aim_test EXCLUDE_FROM_ALL tests/player_aim_test.cpp)
target_include_directories(player_aim_test PRIVATE common)
target_link_libraries(player_aim_test PRIVATE edf6common user32)
target_compile_options(player_aim_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(player_aim_test PRIVATE /OPT:REF)
add_dependencies(offline_checks player_aim_test)
add_test(NAME player_aim COMMAND player_aim_test)
# The stock spot (Q) from a vehicle along the drawn camera (src/spot_ray.h): production hook, pure math, native contract.
add_executable(spot_ray_test EXCLUDE_FROM_ALL tests/spot_ray_test.cpp)
target_include_directories(spot_ray_test PRIVATE src common)
target_link_libraries(spot_ray_test PRIVATE edf6common user32)
target_compile_options(spot_ray_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(spot_ray_test PRIVATE /OPT:REF)
add_dependencies(offline_checks spot_ray_test)
add_test(NAME spot_ray COMMAND spot_ray_test)
add_test(NAME spot_ray_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/spot_ray_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(spot_ray_native PROPERTIES SKIP_RETURN_CODE 77)
# The custom Q mark that replaces the stock spot (src/qmark.cpp, its wire src/qmark_protocol.cpp, its ray src/qmark_ray.h):
# production qmark.cpp on stand-ins (marks shared and shown, the cues asked for), the protocol, the aim's ray.
target_sources(EDF6VehicleCrew PRIVATE src/qmark.cpp src/qmark_protocol.cpp src/player_name.cpp)
add_executable(qmark_test EXCLUDE_FROM_ALL tests/qmark_test.cpp src/qmark_protocol.cpp)
target_include_directories(qmark_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(qmark_test PRIVATE edf6common user32)
target_compile_options(qmark_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(qmark_test PRIVATE /OPT:REF)
add_dependencies(offline_checks qmark_test)
add_test(NAME qmark COMMAND qmark_test)
# The gravity operations' and the float guns' wire (src/gravity_net_protocol.cpp): round trips, refusals, the grab inbox.
add_executable(gravity_net_test EXCLUDE_FROM_ALL tests/gravity_net_test.cpp src/gravity_net_protocol.cpp)
target_compile_options(gravity_net_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks gravity_net_test)
add_test(NAME gravity_net COMMAND gravity_net_test)
# The plugin's online channels (src/plugin_net_logic.h, src/wire.h): registration, routing by magic, the version and size
# drops, the feature bits (an older peer left out), the joins, the roster's wire.
add_executable(plugin_net_test EXCLUDE_FROM_ALL tests/plugin_net_test.cpp)
target_compile_options(plugin_net_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks plugin_net_test)
add_test(NAME plugin_net COMMAND plugin_net_test)
# The ultimate strike online and its shows (src/ultimate_net_protocol.h's wire, src/ultimate_show.h's slots, dark sky
# holders and seeks; tests/ultimate_net_test.cpp).
add_executable(ultimate_net_test EXCLUDE_FROM_ALL tests/ultimate_net_test.cpp)
target_compile_options(ultimate_net_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks ultimate_net_test)
add_test(NAME ultimate_net COMMAND ultimate_net_test)
# The marker's name from the game's own name tag source (src/player_name.cpp), read from EDF.dll without running it.
add_test(NAME player_name_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/player_name_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(player_name_native PROPERTIES SKIP_RETURN_CODE 77)
# The mission start's phase watchdog (src/mission_watch.cpp): phases logged, a stuck one reported with a dump.
add_executable(mission_watch_test tests/mission_watch_test.cpp src/crash_dump.cpp)
target_include_directories(mission_watch_test PRIVATE common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(mission_watch_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME mission_watch COMMAND mission_watch_test)
# The plugin's crash dump (src/crash_dump.cpp): a child process per crash kind (access violation, C++ throw, stack
# overflow, abort / terminate in ucrtbase and in the static CRT, an invalid CRT parameter, a filter set later, two
# threads at once), each dump's exception stream checked; handled exceptions write none.
add_executable(crash_dump_test tests/crash_dump_test.cpp)
target_compile_options(crash_dump_test PRIVATE /W4 /WX /utf-8 /permissive-)
target_link_libraries(crash_dump_test PRIVATE dbghelp)
add_test(NAME crash_dump COMMAND crash_dump_test)
add_dependencies(offline_checks crash_dump_test)
# More than four in an EDF5 / EDF4.1 script mission online: EDF6Coop's version and hook shape, the room word, and the
# spawn layout both plugins share to the bit (src/edf5online_room.h, src/spawn_layout.h). No game.
add_executable(edf5_online_room_test EXCLUDE_FROM_ALL tests/edf5_online_room_test.cpp)
target_compile_options(edf5_online_room_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks edf5_online_room_test)
add_test(NAME edf5_online_room COMMAND edf5_online_room_test)
# The EDF5 scripts' player natives online (src/edf5online.cpp) on a private EDF.dll mapping.
add_executable(edf5_online_native_test EXCLUDE_FROM_ALL tests/edf5_online_native_test.cpp)
target_include_directories(edf5_online_native_test PRIVATE src common)
target_link_libraries(edf5_online_native_test PRIVATE edf6common)
target_compile_options(edf5_online_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks edf5_online_native_test)
add_test(NAME edf5_online_native COMMAND edf5_online_native_test "${EDF6_NATIVE_DLL}")
add_test(NAME edf5_online_foreign_build COMMAND edf5_online_native_test "${EDF6_NATIVE_DLL}" foreign)
add_test(NAME edf5_online_rollback COMMAND edf5_online_native_test "${EDF6_NATIVE_DLL}" rollback)
set_tests_properties(edf5_online_native edf5_online_foreign_build edf5_online_rollback PROPERTIES SKIP_RETURN_CODE 77)
add_executable(fp_ride_lifecycle_test EXCLUDE_FROM_ALL tests/fp_ride_lifecycle_test.cpp)
target_sources(fp_ride_lifecycle_test PRIVATE src/fp_skin_gpu.cpp common/weapon.cpp)
target_include_directories(fp_ride_lifecycle_test PRIVATE src common)
target_link_libraries(fp_ride_lifecycle_test PRIVATE user32)
target_compile_options(fp_ride_lifecycle_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(fp_ride_lifecycle_test PRIVATE /OPT:REF)
add_dependencies(offline_checks fp_ride_lifecycle_test)
add_test(NAME fp_ride_lifecycle COMMAND fp_ride_lifecycle_test)
add_executable(fp_skin_test EXCLUDE_FROM_ALL tests/fp_skin_test.cpp src/fp_skin_gpu.cpp)
target_include_directories(fp_skin_test PRIVATE src common)
target_link_libraries(fp_skin_test PRIVATE edf6common d3d11)
target_compile_options(fp_skin_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks fp_skin_test)
add_test(NAME fp_skin COMMAND fp_skin_test)
set_tests_properties(fp_skin PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME fp_render_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/fp_render_native_audit.py" --dll "${EDF6_NATIVE_DLL}")
set_tests_properties(fp_render_native PROPERTIES SKIP_RETURN_CODE 77)

add_executable(vehicle_look_test EXCLUDE_FROM_ALL tests/vehicle_look_test.cpp)
target_include_directories(vehicle_look_test PRIVATE src common)
target_link_libraries(vehicle_look_test PRIVATE edf6common user32)
target_compile_options(vehicle_look_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks vehicle_look_test)
add_test(NAME vehicle_look COMMAND vehicle_look_test)

add_test(NAME sedan_build COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/sedan_build_test.py")
set_tests_properties(sedan_build PROPERTIES SKIP_RETURN_CODE 77)

# The two built plugins loaded for real into one process, in either order (tests/plugin_pair_load_test.cpp): each
# exports its list (aimlink.h and the rest) from its own DLL, aimlink::Resolve finds the peer's names, and with no
# EDF.dll both EML6_Load refuse and log their load line (with the build id) and REFUSED. Copies in the test's own
# scratch directory are loaded (EML6_Load writes its .log beside the DLL, never into Mods/Plugins).
add_executable(plugin_pair_load_test EXCLUDE_FROM_ALL tests/plugin_pair_load_test.cpp)
target_include_directories(plugin_pair_load_test PRIVATE common third_party/EDFModLoader)
target_compile_options(plugin_pair_load_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(plugin_pair_load_test EDF6VehicleCrew EDF6AutoTurret)
add_dependencies(offline_checks plugin_pair_load_test)
foreach(order crew-first turret-first)
  add_test(NAME plugin_pair_load_${order} COMMAND plugin_pair_load_test "$<TARGET_FILE:EDF6VehicleCrew>" "$<TARGET_FILE:EDF6AutoTurret>"
           "${CMAKE_CURRENT_BINARY_DIR}/test_scratch/plugin_pair_load_${order}" ${order})
endforeach()
# EDF6VehicleCrew's EDF.dll fingerprints (src/profile_sigs.h) on a private EDF.dll mapping (tests/profile_native_test.cpp):
# every row matches, every byte changed is refused by its table's check; the built plugin refuses each kSignatures row.
add_executable(profile_native_test EXCLUDE_FROM_ALL tests/profile_native_test.cpp)
target_include_directories(profile_native_test PRIVATE src common third_party/EDFModLoader)
target_link_libraries(profile_native_test PRIVATE edf6common)
target_compile_options(profile_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(profile_native_test EDF6VehicleCrew)
add_dependencies(offline_checks profile_native_test)
add_test(NAME profile_native COMMAND profile_native_test "${EDF6_NATIVE_DLL}")
add_test(NAME profile_native_plugin_refusal COMMAND profile_native_test "${EDF6_NATIVE_DLL}" plugin "$<TARGET_FILE:EDF6VehicleCrew>"
         "${CMAKE_CURRENT_BINARY_DIR}/test_scratch/profile_native_plugin_refusal")
set_tests_properties(profile_native profile_native_plugin_refusal PROPERTIES SKIP_RETURN_CODE 77)
# Real runtime instructions: BVM/AS difficulty, fixed multiplier and BVM reset contract.
add_executable(mission_difficulty_native_test EXCLUDE_FROM_ALL tests/mission_difficulty_native_test.cpp)
target_compile_options(mission_difficulty_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks mission_difficulty_native_test)
add_test(NAME mission_difficulty_native COMMAND mission_difficulty_native_test "${EDF6_NATIVE_DLL}")
set_tests_properties(mission_difficulty_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
add_executable(vehicle_sensor_test EXCLUDE_FROM_ALL tests/vehicle_sensor_test.cpp)
target_include_directories(vehicle_sensor_test PRIVATE src)
target_compile_options(vehicle_sensor_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks vehicle_sensor_test)
add_test(NAME vehicle_sensor COMMAND vehicle_sensor_test)
add_executable(vehicle_sensor_runtime_test EXCLUDE_FROM_ALL tests/vehicle_sensor_runtime_test.cpp)
target_include_directories(vehicle_sensor_runtime_test PRIVATE src common)
target_link_libraries(vehicle_sensor_runtime_test PRIVATE edf6common user32)
target_compile_options(vehicle_sensor_runtime_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(vehicle_sensor_runtime_test PRIVATE /OPT:REF)
add_dependencies(offline_checks vehicle_sensor_runtime_test)
add_test(NAME vehicle_sensor_runtime COMMAND vehicle_sensor_runtime_test)
add_test(NAME outfit41_color COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/outfit41_color_test.py")
add_test(NAME as_registration COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_as_registration.py")
add_test(NAME mission_script_contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_mission_script.py")
add_test(NAME bvm41_dataflow COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_bvm41_dataflow.py")
target_sources(EDF6VehicleCrew PRIVATE src/sight_contacts.cpp src/pvp.cpp src/pvp_native.cpp)
add_executable(debug_spawn_net_test EXCLUDE_FROM_ALL tests/debug_spawn_net_test.cpp src/support_protocol.cpp)
target_include_directories(debug_spawn_net_test PRIVATE src)
target_compile_options(debug_spawn_net_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks debug_spawn_net_test)
add_test(NAME debug_spawn_net COMMAND debug_spawn_net_test)
add_executable(pvp_protocol_check EXCLUDE_FROM_ALL tests/pvp_protocol_check.cpp)
target_include_directories(pvp_protocol_check PRIVATE src)
target_compile_options(pvp_protocol_check PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks pvp_protocol_check)
add_test(NAME pvp_protocol COMMAND pvp_protocol_check)
add_test(NAME tracked_combat COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_tracked_combat.py")
add_test(NAME edf41_material_install COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_edf41_material_install.py")
add_test(NAME edf41_materials COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_edf41materials.py")
add_executable(decal_depth_native_test EXCLUDE_FROM_ALL tests/decal_depth_native_test.cpp)
target_link_libraries(decal_depth_native_test PRIVATE d3d11)
target_compile_options(decal_depth_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks decal_depth_native_test)
add_test(NAME decal_depth_native COMMAND decal_depth_native_test "${EDF6_NATIVE_DLL}")
set_tests_properties(decal_depth_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
target_sources(EDF6VehicleCrew PRIVATE src/thermal.cpp src/thermal_models.cpp src/thermal_gpu.cpp)
target_link_libraries(EDF6VehicleCrew PRIVATE d3d11 d3dcompiler)
add_executable(debug_spawn_files_check EXCLUDE_FROM_ALL tests/debug_spawn_files_test.cpp)
target_compile_options(debug_spawn_files_check PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks debug_spawn_files_check)
add_test(NAME debug_spawn_files COMMAND debug_spawn_files_check)
add_executable(edf41_sound_channels_test EXCLUDE_FROM_ALL tests/edf41_sound_channels_test.cpp)
target_compile_options(edf41_sound_channels_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks edf41_sound_channels_test)
add_test(NAME edf41_sound_channels COMMAND edf41_sound_channels_test)
add_executable(edf41_sound_bank_test EXCLUDE_FROM_ALL tests/edf41_sound_bank_test.cpp)
target_include_directories(edf41_sound_bank_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(edf41_sound_bank_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(edf41_sound_bank_test PRIVATE /OPT:REF)
add_dependencies(offline_checks edf41_sound_bank_test)
add_test(NAME edf41_sound_bank COMMAND edf41_sound_bank_test)
add_test(NAME edf41_sound_lifecycle COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/edf41_sound_lifecycle_test.py")
add_test(NAME tracked_install COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_tracked_install.py")
add_test(NAME tracked_range COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_tracked_range.py")
add_executable(thermal_gpu_test EXCLUDE_FROM_ALL tests/thermal_gpu_test.cpp src/thermal_gpu.cpp)
target_include_directories(thermal_gpu_test PRIVATE src)
target_link_libraries(thermal_gpu_test PRIVATE d3d11 d3dcompiler)
target_compile_options(thermal_gpu_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks thermal_gpu_test)
add_test(NAME thermal_gpu COMMAND thermal_gpu_test)
set_tests_properties(thermal_gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
add_test(NAME thermal_models_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/thermal_model_native_audit.py" --dll "${EDF6_NATIVE_DLL}")
set_tests_properties(thermal_models_native PROPERTIES SKIP_RETURN_CODE 77)
add_executable(map_pvp_input_test EXCLUDE_FROM_ALL tests/map_pvp_input_test.cpp)
target_include_directories(map_pvp_input_test PRIVATE src common)
target_compile_options(map_pvp_input_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks map_pvp_input_test)
add_test(NAME map_pvp_input COMMAND map_pvp_input_test)
add_executable(pvp_native_fixture EXCLUDE_FROM_ALL tests/pvp_native_fixture.cpp)
target_include_directories(pvp_native_fixture PRIVATE src common third_party/EDFModLoader)
target_compile_options(pvp_native_fixture PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks pvp_native_fixture)
add_test(NAME pvp_native_fixture COMMAND pvp_native_fixture)
add_executable(thermal_models_test EXCLUDE_FROM_ALL tests/thermal_models_test.cpp src/thermal_models.cpp)
target_include_directories(thermal_models_test PRIVATE src common)
target_compile_options(thermal_models_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks thermal_models_test)
add_test(NAME thermal_models COMMAND thermal_models_test)
add_test(NAME thermal_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/thermal_native_audit.py" --dll "${EDF6_NATIVE_DLL}")
set_tests_properties(thermal_native PROPERTIES SKIP_RETURN_CODE 77)
add_executable(pvp_hud_layout_test EXCLUDE_FROM_ALL tests/pvp_hud_layout_test.cpp)
target_include_directories(pvp_hud_layout_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(pvp_hud_layout_test PRIVATE edf6common)
target_compile_options(pvp_hud_layout_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks pvp_hud_layout_test)
add_test(NAME pvp_hud_layout COMMAND pvp_hud_layout_test)
add_executable(view_depth_lifecycle_test EXCLUDE_FROM_ALL tests/view_depth_lifecycle_test.cpp)
target_include_directories(view_depth_lifecycle_test PRIVATE src common)
target_compile_options(view_depth_lifecycle_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks view_depth_lifecycle_test)
add_test(NAME view_depth_lifecycle COMMAND view_depth_lifecycle_test)
add_executable(thermal_runtime_test EXCLUDE_FROM_ALL tests/thermal_runtime_test.cpp)
target_include_directories(thermal_runtime_test PRIVATE src common)
target_link_libraries(thermal_runtime_test PRIVATE edf6common user32)
target_compile_options(thermal_runtime_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks thermal_runtime_test)
add_test(NAME thermal_runtime COMMAND thermal_runtime_test)
target_sources(EDF6VehicleCrew PRIVATE src/pvp_support.cpp)
add_executable(pvp_support_fixture EXCLUDE_FROM_ALL tests/pvp_support_fixture.cpp)
target_include_directories(pvp_support_fixture PRIVATE src common third_party/EDFModLoader)
target_compile_options(pvp_support_fixture PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks pvp_support_fixture)
add_test(NAME pvp_support_fixture COMMAND pvp_support_fixture)
add_test(NAME edf41_vehicle_calls COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_edf41_vehicle_calls.py")
add_test(NAME edf41_vehicle_sounds COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_edf41_vehicle_sounds.py")
add_test(NAME edf41_tank_models COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_edf41tank.py")
add_test(NAME edf41_tank_recoil_binding COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_edf41tank_recoil_binding.py")
add_test(NAME edf41_heli_models COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_edf41heli.py")
add_test(NAME edf41_mech_models COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_edf41mech.py")
add_test(NAME pvp_native_contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/pvp_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(pvp_native_contract PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME edf41_vehicle_spawn COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_edf41_vehicle_spawn.py")
add_test(NAME edf41_vehicle_spawn_data COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_edf41_vehicle_spawn_data.py")
target_sources(EDF6VehicleCrew PRIVATE src/edf41bike.cpp)
add_executable(edf41bike_check EXCLUDE_FROM_ALL tests/edf41bike_test.cpp)
target_compile_options(edf41bike_check PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks edf41bike_check)
add_test(NAME edf41bike COMMAND edf41bike_check)
add_test(NAME edf41_bike_models COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_edf41bike.py")
add_test(NAME edf41bike_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/edf41bike_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(edf41bike_native PROPERTIES SKIP_RETURN_CODE 77)
add_executable(edf41bike_runtime_test EXCLUDE_FROM_ALL tests/edf41bike_runtime_test.cpp)
target_include_directories(edf41bike_runtime_test PRIVATE src common)
target_compile_options(edf41bike_runtime_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks edf41bike_runtime_test)
add_test(NAME edf41bike_runtime COMMAND edf41bike_runtime_test)
add_executable(payload_aim_api_test EXCLUDE_FROM_ALL tests/payload_aim_api_test.cpp)
target_include_directories(payload_aim_api_test PRIVATE src common third_party/EDFModLoader)
target_link_libraries(payload_aim_api_test PRIVATE edf6common user32)
target_compile_options(payload_aim_api_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(payload_aim_api_test PRIVATE /OPT:REF)
add_dependencies(offline_checks payload_aim_api_test)
add_test(NAME payload_aim_api COMMAND payload_aim_api_test)
add_executable(autoturret_payload_aim_test EXCLUDE_FROM_ALL tests/autoturret_payload_aim_test.cpp)
target_include_directories(autoturret_payload_aim_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(autoturret_payload_aim_test PRIVATE edf6common user32)
target_compile_options(autoturret_payload_aim_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(autoturret_payload_aim_test PRIVATE /OPT:REF)
add_dependencies(offline_checks autoturret_payload_aim_test)
add_test(NAME autoturret_payload_aim COMMAND autoturret_payload_aim_test)
add_test(NAME edf41_caption_contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/edf41_caption_contract_test.py")

add_executable(enemy_ui_logic_test EXCLUDE_FROM_ALL tests/enemy_ui_logic_test.cpp)
target_include_directories(enemy_ui_logic_test PRIVATE src common third_party/EDFModLoader)
target_compile_options(enemy_ui_logic_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks enemy_ui_logic_test)
add_test(NAME enemy_ui_logic COMMAND enemy_ui_logic_test)
add_test(NAME room_native_menu_contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/room_native_menu_contract_test.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(room_native_menu_contract PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME outfit41_palette_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/outfit41_palette_native_test.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(outfit41_palette_native PROPERTIES SKIP_RETURN_CODE 77)

add_executable(outfit41_color_warp_test EXCLUDE_FROM_ALL tests/outfit41_color_warp_test.cpp)
target_compile_options(outfit41_color_warp_test PRIVATE /W4 /WX /utf-8 /permissive-)
target_link_libraries(outfit41_color_warp_test PRIVATE d3d11 d3dcompiler)
add_dependencies(offline_checks outfit41_color_warp_test)
add_test(NAME outfit41_color_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/outfit41_color_native_test.py" --edf6 "${EDF6_GAME_DIR}" --probe "$<TARGET_FILE:outfit41_color_warp_test>")
set_tests_properties(outfit41_color_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
add_executable(enemy_control_protocol_test EXCLUDE_FROM_ALL tests/enemy_control_protocol_test.cpp)
target_compile_options(enemy_control_protocol_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks enemy_control_protocol_test)
add_test(NAME enemy_control_protocol COMMAND enemy_control_protocol_test)
target_sources(EDF6VehicleCrew PRIVATE src/vulcan_rotor.cpp)
add_executable(vulcan_rotor_test EXCLUDE_FROM_ALL tests/vulcan_rotor_test.cpp)
target_include_directories(vulcan_rotor_test PRIVATE src common third_party/EDFModLoader)
target_compile_options(vulcan_rotor_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks vulcan_rotor_test)
add_test(NAME vulcan_rotor COMMAND vulcan_rotor_test)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control.cpp src/enemy_control_logic.cpp src/enemy_control_native.cpp src/enemy_control_catalog.cpp src/enemy_control_net.cpp src/sub_support_net.cpp)
add_executable(terrain_sampler_native_test EXCLUDE_FROM_ALL tests/terrain_sampler_native_test.cpp)
target_compile_options(terrain_sampler_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
target_link_libraries(terrain_sampler_native_test PRIVATE d3d11 d3dcompiler)
add_dependencies(offline_checks terrain_sampler_native_test)
add_test(NAME sk_terrain_materials COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_sk_terrain_materials.py")
add_test(NAME terrain_shader_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/terrain_shader_native_audit.py" --probe "$<TARGET_FILE:terrain_sampler_native_test>")
set_tests_properties(terrain_shader_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)

add_executable(camera_frame_native_test EXCLUDE_FROM_ALL tests/camera_frame_native_test.cpp)
target_compile_options(camera_frame_native_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks camera_frame_native_test)
add_test(NAME camera_frame_native COMMAND camera_frame_native_test "${EDF6_NATIVE_DLL}")
set_tests_properties(camera_frame_native PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME edf41_npc_visual_contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/edf41_npc_visual_contract_test.py")
add_test(NAME edf41_npc_climb COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/edf41_npc_climb_test.py")
add_test(NAME npc_climb_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/npc_climb_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(npc_climb_native PROPERTIES SKIP_RETURN_CODE 77)
add_executable(room_native_entry_runtime_test EXCLUDE_FROM_ALL tests/room_native_entry_runtime_test.cpp)
target_include_directories(room_native_entry_runtime_test PRIVATE src common third_party/EDFModLoader)
target_link_libraries(room_native_entry_runtime_test PRIVATE edf6common user32)
target_compile_options(room_native_entry_runtime_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(room_native_entry_runtime_test PRIVATE /OPT:REF)
add_dependencies(offline_checks room_native_entry_runtime_test)
add_test(NAME room_native_entry_runtime COMMAND room_native_entry_runtime_test)

add_executable(npc_action_look_test EXCLUDE_FROM_ALL tests/npc_action_look_test.cpp src/ground_navigation.cpp)
target_include_directories(npc_action_look_test PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(npc_action_look_test PRIVATE edf6common user32)
target_compile_options(npc_action_look_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(npc_action_look_test PRIVATE /OPT:REF)
add_dependencies(offline_checks npc_action_look_test)
add_test(NAME npc_action_look COMMAND npc_action_look_test)
add_test(NAME npc_action_look_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/npc_action_look_native.py")

add_test(NAME tracked_collision COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_tracked_collision.py")
# The Havok tagfile graph and its writer (pylib/hkfile.py), the SHKT / SGO binding checks (pylib/shkt_check.py): the
# portable parts always, the stock files' round trips and mutations with the game.
add_test(NAME hkfile COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_hkfile.py")
add_test(NAME shkt_check COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_shkt_check.py")
add_test(NAME hkshape COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_hkshape.py")
add_test(NAME vehicle_check COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_vehicle_check.py")
add_test(NAME model_pieces COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_model_pieces.py")
add_executable(vulcan_rotor_runtime_test EXCLUDE_FROM_ALL tests/vulcan_rotor_runtime_test.cpp)
target_include_directories(vulcan_rotor_runtime_test PRIVATE src common third_party/EDFModLoader)
target_compile_options(vulcan_rotor_runtime_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks vulcan_rotor_runtime_test)
add_test(NAME vulcan_rotor_runtime COMMAND vulcan_rotor_runtime_test)
add_test(NAME vulcan_rotor_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/vulcan_rotor_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(vulcan_rotor_native PROPERTIES SKIP_RETURN_CODE 77)

# Production-source fixtures use function sections so unrelated game entry points
# are discarded; the exercised runtime paths still execute the real implementation.
function(edf6_feedback_fixture target test_name source)
    add_executable(${target} EXCLUDE_FROM_ALL ${source})
    target_include_directories(${target} PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(${target} PRIVATE edf6common user32)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
    target_link_options(${target} PRIVATE /OPT:REF)
    add_dependencies(offline_checks ${target})
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()
edf6_feedback_fixture(sub_support_net_test sub_support_net tests/sub_support_net_test.cpp)
target_sources(sub_support_net_test PRIVATE src/sub_support_net.cpp)
edf6_feedback_fixture(sub_support_dispatch_test sub_support_dispatch tests/sub_support_dispatch_test.cpp)
edf6_feedback_fixture(submarine_support_fixture submarine_support tests/submarine_support_fixture.cpp)
edf6_feedback_fixture(carrier_laser_team_fixture carrier_laser_team tests/carrier_laser_team_fixture.cpp)
edf6_feedback_fixture(pvp_inherit_support_test pvp_inherit_support tests/pvp_inherit_support_test.cpp)
edf6_feedback_fixture(pvp_native_inherit_test pvp_native_inherit tests/pvp_native_inherit_test.cpp)
edf6_feedback_fixture(support_drone_runtime_test support_drone_runtime tests/support_drone_runtime_test.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_legacy.cpp)

add_test(NAME tracked_collision_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/tracked_collision_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(tracked_collision_native PROPERTIES SKIP_RETURN_CODE 77)
add_executable(enemy_control_net_input_test EXCLUDE_FROM_ALL tests/enemy_control_net_input_test.cpp)
target_include_directories(enemy_control_net_input_test PRIVATE src)
target_compile_options(enemy_control_net_input_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks enemy_control_net_input_test)
add_test(NAME enemy_control_net_input COMMAND enemy_control_net_input_test)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_native_look.cpp)
edf6_feedback_fixture(enemy_control_runtime_test enemy_control_runtime tests/enemy_control_runtime_test.cpp)
target_sources(enemy_control_runtime_test PRIVATE src/enemy_control_logic.cpp)
edf6_feedback_fixture(enemy_control_catalog_test enemy_control_catalog tests/enemy_control_catalog_test.cpp)
edf6_feedback_fixture(enemy_control_native_fixture enemy_control_native_fixture tests/enemy_control_native_fixture.cpp)
edf6_feedback_fixture(enemy_control_native_look_test enemy_control_native_look tests/enemy_control_native_look_test.cpp)
edf6_feedback_fixture(enemy_control_net_fixture enemy_control_net_runtime tests/enemy_control_net_fixture.cpp)
edf6_feedback_fixture(enemy_control_legacy_robo_test enemy_control_legacy_robo tests/enemy_control_legacy_robo_test.cpp)
target_sources(enemy_control_legacy_robo_test PRIVATE src/enemy_control_logic.cpp)
add_test(NAME enemy_control_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_native_audit.py" "${EDF6_NATIVE_DLL}")
add_test(NAME enemy_control_native_look_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_native_look_audit.py" "${EDF6_NATIVE_DLL}")
add_test(NAME enemy_control_source_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_source_native_audit.py" --dll "${EDF6_NATIVE_DLL}")
add_test(NAME enemy_control_catalog_native COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_catalog_native_audit.py" --dll "${EDF6_NATIVE_DLL}" --game "${EDF6_GAME_DIR}")
set_tests_properties(enemy_control_native_audit enemy_control_native_look_audit enemy_control_source_native enemy_control_catalog_native PROPERTIES SKIP_RETURN_CODE 77)

target_sources(EDF6VehicleCrew PRIVATE src/carrier_drone_net.cpp src/support_source.cpp src/enemy_control_native_move.cpp src/enemy_control_native_net.cpp)
edf6_feedback_fixture(carrier_drone_net_test carrier_drone_net tests/carrier_drone_net_test.cpp)
target_sources(carrier_drone_net_test PRIVATE src/carrier_drone_net.cpp)
edf6_feedback_fixture(support_source_test support_source tests/support_source_test.cpp)
edf6_feedback_fixture(edf41dragon_control_fixture edf41dragon_control tests/edf41dragon_control_fixture.cpp)
edf6_feedback_fixture(edf41mother_control_fixture edf41mother_control tests/edf41mother_control_fixture.cpp)
edf6_feedback_fixture(edf41mother401_control_fixture edf41mother401_control tests/edf41mother_control_fixture.cpp)
target_compile_definitions(edf41mother401_control_fixture PRIVATE MOTHER401_FIXTURE)

edf6_feedback_fixture(carrier_support_dispatch_test carrier_support_dispatch tests/carrier_support_dispatch_test.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/pvp_controlled.cpp)
edf6_feedback_fixture(enemy_control_native_move_test enemy_control_native_move tests/enemy_control_native_move_test.cpp)
edf6_feedback_fixture(enemy_control_native_look_extra_test enemy_control_native_look_extra tests/enemy_control_native_look_extra_test.cpp)
edf6_feedback_fixture(enemy_control_native_net_fixture enemy_control_native_net_fixture tests/enemy_control_native_net_fixture.cpp)
add_test(NAME enemy_control_native_net_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_native_net_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_native_net_audit PROPERTIES SKIP_RETURN_CODE 77)
edf6_feedback_fixture(enemy_control_pose_protocol_test enemy_control_pose_protocol tests/enemy_control_pose_protocol_test.cpp)

edf6_feedback_fixture(enemy_control_pose_codec_test enemy_control_pose_codec tests/enemy_control_pose_codec_test.cpp)

edf6_feedback_fixture(edf41hector_control_fixture edf41hector_control tests/edf41hector_control_fixture.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_deiroi.cpp src/enemy_control_humanoid.cpp src/enemy_control_native_look_extra.cpp src/enemy_control_native_pose.cpp src/enemy_control_weapon_aim.cpp)
target_sources(enemy_control_native_look_test PRIVATE src/enemy_control_native_look_extra.cpp)
edf6_feedback_fixture(enemy_control_deiroi_test enemy_control_deiroi tests/enemy_control_deiroi_test.cpp)
edf6_feedback_fixture(enemy_control_humanoid_test enemy_control_humanoid tests/enemy_control_humanoid_test.cpp)
add_test(NAME enemy_control_native_look_extra_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_native_look_extra_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_native_look_extra_audit PROPERTIES SKIP_RETURN_CODE 77)
edf6_feedback_fixture(enemy_hud_layout_test enemy_hud_layout tests/enemy_hud_layout_test.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_radar.cpp src/enemy_control_target.cpp src/enemy_control_action_native.cpp src/enemy_control_pvp_native.cpp src/enemy_control_pvp_spawn_native.cpp src/pvp_controlled_spawn.cpp)
edf6_feedback_fixture(enemy_control_radar_native_fixture enemy_control_radar_native tests/enemy_control_radar_native_fixture.cpp)
add_test(NAME enemy_control_radar_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_radar_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_radar_audit PROPERTIES SKIP_RETURN_CODE 77)
edf6_feedback_fixture(enemy_control_action_protocol_test enemy_control_action_protocol tests/enemy_control_action_protocol_test.cpp)
edf6_feedback_fixture(enemy_control_action_native_test enemy_control_action_native tests/enemy_control_action_native_test.cpp)
add_test(NAME enemy_control_action_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_action_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_action_native_audit PROPERTIES SKIP_RETURN_CODE 77)
edf6_feedback_fixture(enemy_control_native_pose_test enemy_control_native_pose tests/enemy_control_native_pose_test.cpp)
add_test(NAME enemy_control_native_pose_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_native_pose_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_native_pose_audit PROPERTIES SKIP_RETURN_CODE 77)
edf6_feedback_fixture(pvp_controlled_fixture pvp_controlled tests/pvp_controlled_fixture.cpp)
edf6_feedback_fixture(enemy_control_pvp_tree_test enemy_control_pvp_tree tests/enemy_control_pvp_tree_test.cpp)
edf6_feedback_fixture(enemy_control_pvp_native_test enemy_control_pvp_native tests/enemy_control_pvp_native_test.cpp)

edf6_feedback_fixture(enemy_control_target_fixture enemy_control_target tests/enemy_control_target_fixture.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_deiroi_net.cpp src/enemy_control_pvp_boss.cpp src/enemy_control_pvp_transport.cpp src/enemy_control_timeship.cpp src/enemy_control_timeship_pods.cpp src/enemy_control_walking_guns.cpp src/enemy_control_walking.cpp)
edf6_feedback_fixture(enemy_control_deiroi_net_test enemy_control_deiroi_net tests/enemy_control_deiroi_net_test.cpp)
edf6_feedback_fixture(enemy_control_deiroi_net_codec_test enemy_control_deiroi_net_codec tests/enemy_control_deiroi_net_codec_test.cpp)
edf6_feedback_fixture(enemy_control_pvp_boss_test enemy_control_pvp_boss tests/enemy_control_pvp_boss_test.cpp)
edf6_feedback_fixture(enemy_control_pvp_transport_test enemy_control_pvp_transport tests/enemy_control_pvp_transport_test.cpp)
edf6_feedback_fixture(enemy_control_timeship_test enemy_control_timeship tests/enemy_control_timeship_test.cpp)
edf6_feedback_fixture(enemy_control_timeship_pods_test enemy_control_timeship_pods tests/enemy_control_timeship_pods_test.cpp)
edf6_feedback_fixture(enemy_control_walking_test enemy_control_walking tests/enemy_control_walking_test.cpp)
edf6_feedback_fixture(enemy_control_walking_guns_test enemy_control_walking_guns tests/enemy_control_walking_guns_test.cpp)
edf6_feedback_fixture(enemy_control_weapon_aim_test enemy_control_weapon_aim tests/enemy_control_weapon_aim_test.cpp)
add_test(NAME enemy_control_deiroi_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_deiroi_native_audit.py" --dll "${EDF6_NATIVE_DLL}" --game "${EDF6_GAME_DIR}")
set_tests_properties(enemy_control_deiroi_native_audit PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME enemy_control_timeship_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_timeship_native_audit.py" --dll "${EDF6_NATIVE_DLL}" --game "${EDF6_GAME_DIR}")
set_tests_properties(enemy_control_timeship_native_audit PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME enemy_control_walking_guns_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_walking_guns_native_audit.py" --dll "${EDF6_NATIVE_DLL}" --game "${EDF6_GAME_DIR}")
set_tests_properties(enemy_control_walking_guns_native_audit PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME enemy_control_walking_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_walking_native_audit.py" --dll "${EDF6_NATIVE_DLL}" --game "${EDF6_GAME_DIR}")
set_tests_properties(enemy_control_walking_native_audit PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME enemy_control_deiroi_net_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_deiroi_net_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_deiroi_net_native_audit PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME enemy_control_native_move_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_native_move_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_native_move_audit PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME enemy_control_timeship_pods_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_timeship_pods_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_timeship_pods_audit PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME enemy_control_weapon_aim_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_weapon_aim_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_weapon_aim_native_audit PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME enemy_control_target_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_target_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_target_native_audit PROPERTIES SKIP_RETURN_CODE 77)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_command_actions.cpp)
edf6_feedback_fixture(enemy_control_command_actions_test enemy_control_command_actions tests/enemy_control_command_actions_test.cpp)
add_test(NAME enemy_control_command_actions_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_command_actions_native_audit.py" --dll "${EDF6_NATIVE_DLL}" --game "${EDF6_GAME_DIR}")
set_tests_properties(enemy_control_command_actions_native_audit PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME enemy_control_command_boss_carrier_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_command_boss_carrier_native_audit.py" --dll "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_command_boss_carrier_native_audit PROPERTIES SKIP_RETURN_CODE 77)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_pvp_berserker.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_timeship_pods_net.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_timeship_pods_script.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_timeship_script.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_walking_net.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_gun_factory.cpp)

edf6_feedback_fixture(enemy_control_pvp_berserker_test enemy_control_pvp_berserker tests/enemy_control_pvp_berserker_test.cpp)

edf6_feedback_fixture(enemy_control_pvp_boss_core_test enemy_control_pvp_boss_core tests/enemy_control_pvp_boss_core_test.cpp)

edf6_feedback_fixture(pvp_controlled_spawn_native_test pvp_controlled_spawn_native tests/pvp_controlled_spawn_native_test.cpp)

edf6_feedback_fixture(enemy_control_timeship_pods_net_test enemy_control_timeship_pods_net tests/enemy_control_timeship_pods_net_test.cpp)

edf6_feedback_fixture(enemy_control_timeship_script_test enemy_control_timeship_script tests/enemy_control_timeship_script_test.cpp)

edf6_feedback_fixture(carrier_drone_transaction_test carrier_drone_transaction tests/carrier_drone_transaction_test.cpp)

edf6_feedback_fixture(enemy_control_pvp_transport_core_test enemy_control_pvp_transport_core tests/enemy_control_pvp_transport_test.cpp)
target_compile_definitions(enemy_control_pvp_transport_core_test PRIVATE EDF_TRANSPORT_REAL_CORE=1)

add_test(NAME enemy_control_pvp_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_pvp_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_pvp_native_audit PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME enemy_control_timeship_pods_net_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_timeship_pods_net_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_timeship_pods_net_audit PROPERTIES SKIP_RETURN_CODE 77)

edf6_feedback_fixture(enemy_control_gun_factory_test enemy_control_gun_factory tests/enemy_control_gun_factory_test.cpp)

add_test(NAME enemy_control_gun_factory_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_gun_factory_native_audit.py" --dll "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_gun_factory_native_audit PROPERTIES SKIP_RETURN_CODE 77)

target_sources(carrier_drone_transaction_test PRIVATE src/jet_carrier.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_boss_gun_net.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_merman_net.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_monster_net.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_projectile_native.cpp)

edf6_feedback_fixture(enemy_control_boss_gun_net_test enemy_control_boss_gun_net tests/enemy_control_boss_gun_net_test.cpp)

edf6_feedback_fixture(enemy_control_boss_gun_projectile_test enemy_control_boss_gun_projectile tests/enemy_control_boss_gun_projectile_test.cpp)

edf6_feedback_fixture(enemy_control_merman_net_test enemy_control_merman_net tests/enemy_control_merman_net_test.cpp)

edf6_feedback_fixture(enemy_control_monster_definition_test enemy_control_monster_definition tests/enemy_control_monster_definition_test.cpp)

edf6_feedback_fixture(enemy_control_monster_net_codec_test enemy_control_monster_net_codec tests/enemy_control_monster_net_codec_test.cpp)

edf6_feedback_fixture(enemy_control_monster_net_test enemy_control_monster_net tests/enemy_control_monster_net_test.cpp)

edf6_feedback_fixture(enemy_control_projectile_native_test enemy_control_projectile_native tests/enemy_control_projectile_native_test.cpp)

add_test(NAME enemy_control_boss_gun_net_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_boss_gun_net_native_audit.py" --dll "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_boss_gun_net_native_audit PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME enemy_control_merman_net_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_merman_net_native_audit.py" --dll "${EDF6_NATIVE_DLL}")
set_tests_properties(enemy_control_merman_net_native_audit PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME enemy_control_monster_net_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_monster_net_native_audit.py" --dll "${EDF6_NATIVE_DLL}" --game "${EDF6_GAME_DIR}")
set_tests_properties(enemy_control_monster_net_native_audit PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME enemy_control_projectile_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_projectile_native_audit.py" --dll "${EDF6_NATIVE_DLL}" --game "${EDF6_GAME_DIR}")
set_tests_properties(enemy_control_projectile_native_audit PROPERTIES SKIP_RETURN_CODE 77)

edf6_feedback_fixture(enemy_control_pvp_berserker_core_test enemy_control_pvp_berserker_core tests/enemy_control_pvp_berserker_core_test.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_walking_parts.cpp)

edf6_feedback_fixture(enemy_control_walking_net_test enemy_control_walking_net tests/enemy_control_walking_net_test.cpp)

edf6_feedback_fixture(enemy_control_walking_parts_test enemy_control_walking_parts tests/enemy_control_walking_parts_test.cpp)

add_test(NAME enemy_control_walking_parts_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_walking_parts_native_audit.py" --dll "${EDF6_NATIVE_DLL}" --game "${EDF6_GAME_DIR}")
set_tests_properties(enemy_control_walking_parts_native_audit PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME online_gate_bridge_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/online_gate_bridge_guard_test.py")

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_deiroi_entity_aim.cpp)

edf6_feedback_fixture(enemy_control_deiroi_entity_aim_test enemy_control_deiroi_entity_aim tests/enemy_control_deiroi_entity_aim_test.cpp)

add_test(NAME enemy_control_deiroi_entity_aim_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_deiroi_entity_aim_native_audit.py" --dll "${EDF6_NATIVE_DLL}" --game "${EDF6_GAME_DIR}")
set_tests_properties(enemy_control_deiroi_entity_aim_native_audit PROPERTIES SKIP_RETURN_CODE 77)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_capacity.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/enemy_control_unit_guide.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/pvp_controlled_ported_spawn.cpp)

target_sources(EDF6VehicleCrew PRIVATE src/pvp_controlled_npc.cpp)

edf6_feedback_fixture(enemy_control_capacity_test enemy_control_capacity tests/enemy_control_capacity_test.cpp)

edf6_feedback_fixture(enemy_control_unit_guide_test enemy_control_unit_guide tests/enemy_control_unit_guide_test.cpp)

add_test(NAME enemy_control_unit_guide_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_unit_guide_native_audit.py" --dll "${EDF6_NATIVE_DLL}" --game "${EDF6_GAME_DIR}")
set_tests_properties(enemy_control_unit_guide_native_audit PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME enemy_control_walking_net_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/enemy_control_walking_net_native_audit.py" --dll "${EDF6_NATIVE_DLL}" --game "${EDF6_GAME_DIR}")
set_tests_properties(enemy_control_walking_net_native_audit PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME enemy_control_ported_spawn_profiles COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_enemy_control_ported_spawn_profiles.py")

edf6_feedback_fixture(pvp_controlled_npc_test pvp_controlled_npc tests/pvp_controlled_npc_test.cpp)

edf6_feedback_fixture(pvp_controlled_ported_spawn_test pvp_controlled_ported_spawn tests/pvp_controlled_ported_spawn_test.cpp)

edf6_feedback_fixture(pvp_native_live_emitters_test pvp_native_live_emitters tests/pvp_native_live_emitters_test.cpp)

add_test(NAME pvp_controlled_npc_native_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/pvp_controlled_npc_native_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(pvp_controlled_npc_native_audit PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME pvp_native_live_emitters_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/pvp_native_live_emitters_audit.py" "${EDF6_NATIVE_DLL}")
set_tests_properties(pvp_native_live_emitters_audit PROPERTIES SKIP_RETURN_CODE 77)

# Source-derived producer payloads are regenerated by the real production fixtures.
# Every replay uses this build directory, so a clean checkout needs no tmp snapshot.
add_executable(enemy_control_provider_trace_test EXCLUDE_FROM_ALL tests/enemy_control_provider_trace_test.cpp)
target_include_directories(enemy_control_provider_trace_test PRIVATE src)
target_compile_options(enemy_control_provider_trace_test PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(enemy_control_provider_trace_test PRIVATE /OPT:REF)
add_dependencies(offline_checks enemy_control_provider_trace_test)
add_dependencies(enemy_control_provider_trace_test enemy_control_boss_gun_projectile_test enemy_control_monster_net_test enemy_control_merman_net_test enemy_control_walking_net_test enemy_control_deiroi_net_test)
set(EDF6_PROVIDER_TRACES "${CMAKE_CURRENT_BINARY_DIR}/provider-traces")
file(MAKE_DIRECTORY "${EDF6_PROVIDER_TRACES}")
add_test(NAME enemy_control_export_boss COMMAND $<TARGET_FILE:enemy_control_boss_gun_projectile_test> --dump-trace "${EDF6_PROVIDER_TRACES}/boss.jsonl")
add_test(NAME enemy_control_export_monster COMMAND $<TARGET_FILE:enemy_control_monster_net_test> --dump-trace "${EDF6_PROVIDER_TRACES}/monster.jsonl")
add_test(NAME enemy_control_export_merman COMMAND $<TARGET_FILE:enemy_control_merman_net_test> --dump-trace "${EDF6_PROVIDER_TRACES}/merman.jsonl")
add_test(NAME enemy_control_export_walking COMMAND $<TARGET_FILE:enemy_control_walking_net_test> --dump-trace "${EDF6_PROVIDER_TRACES}/walking.jsonl" --trace-assets "${CMAKE_SOURCE_DIR}/tests/data/walking-net-trace-assets.tsv")
set(EDF6_PROVIDER_EXPORT_TESTS enemy_control_export_boss enemy_control_export_monster enemy_control_export_merman enemy_control_export_walking)
foreach(rate 30 60 144)
    add_test(NAME enemy_control_export_deiroi_${rate} COMMAND $<TARGET_FILE:enemy_control_deiroi_net_test> --dump-trace "${EDF6_PROVIDER_TRACES}/deiroi-${rate}.trace" ${rate} "${CMAKE_SOURCE_DIR}/tests/data/deiroi-xl-native-trace.json")
    list(APPEND EDF6_PROVIDER_EXPORT_TESTS enemy_control_export_deiroi_${rate})
endforeach()
set_tests_properties(${EDF6_PROVIDER_EXPORT_TESTS} PROPERTIES FIXTURES_SETUP ec_provider_sources)
add_test(NAME enemy_control_trace_pack COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/enemy_control_trace_pack.py"
    "${EDF6_PROVIDER_TRACES}/boss.jsonl" "${EDF6_PROVIDER_TRACES}/monster.jsonl" "${EDF6_PROVIDER_TRACES}/merman.jsonl" "${EDF6_PROVIDER_TRACES}/walking.jsonl"
    "${EDF6_PROVIDER_TRACES}/deiroi-30.trace" "${EDF6_PROVIDER_TRACES}/deiroi-60.trace" "${EDF6_PROVIDER_TRACES}/deiroi-144.trace" -o "${EDF6_PROVIDER_TRACES}/actual.dat")
set_tests_properties(enemy_control_trace_pack PROPERTIES FIXTURES_REQUIRED ec_provider_sources FIXTURES_SETUP ec_provider_packed)
foreach(rtt 250 600)
    foreach(background 0 32 64)
        add_test(NAME enemy_control_provider_trace_rtt${rtt}_bg${background} COMMAND $<TARGET_FILE:enemy_control_provider_trace_test> --rtt-ms ${rtt} --background-kib ${background} "${EDF6_PROVIDER_TRACES}/actual.dat")
        set_tests_properties(enemy_control_provider_trace_rtt${rtt}_bg${background} PROPERTIES FIXTURES_REQUIRED ec_provider_packed TIMEOUT 300)
    endforeach()
endforeach()
