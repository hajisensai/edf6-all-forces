cmake_minimum_required(VERSION 3.20)
project(EDF6VehicleCrew VERSION 0.8.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(EDF6VehicleCrew SHARED src/plugin.cpp src/crew.cpp src/heli.cpp src/ground.cpp src/jet.cpp src/jet_flight.cpp src/jet_combat.cpp src/jet_carrier.cpp src/jet_bay.cpp src/jet_spawn.cpp src/jet_hooks.cpp src/airstrike.cpp src/loadout.cpp src/subcarrier.cpp src/carrierlaser.cpp src/overlay.cpp src/hud.cpp src/launcher.cpp src/katyusha.cpp src/highcam.cpp src/helisight.cpp src/jetsound.cpp src/jetaudio.cpp src/playerjet.cpp src/missile.cpp src/stores.cpp src/bigworld.cpp src/jetprops.cpp src/booster.cpp src/shield.cpp src/view.cpp src/mission.cpp src/body506.cpp src/physics.cpp src/drill.cpp src/gear.cpp src/netprobe.cpp src/glyphs.cpp src/turretaim.cpp src/rounds.cpp src/vhud.cpp src/stockgauge.cpp src/warn.cpp src/payload.cpp src/seatswitch.cpp src/sidecar.cpp src/turretcam.cpp src/vehsound.cpp)
# The Nix's torso twist (its own line: the source list above is where every feature branch adds).
target_sources(EDF6VehicleCrew PRIVATE src/nix.cpp)
# The ground vehicles' ram (its own line, as above).
target_sources(EDF6VehicleCrew PRIVATE src/vehicleram.cpp)
# The gun stabilizer (its own line, as the Nix's).
target_sources(EDF6VehicleCrew PRIVATE src/stab.cpp)
# A remote gunner's recoil on the vehicle's authority (src/gunnerrecoil.cpp, docs/online-re.md §10).
target_sources(EDF6VehicleCrew PRIVATE src/gunnerrecoil.cpp)
# The map view (src/map.cpp: the overhead camera, the input hold, the marks it gathers).
target_sources(EDF6VehicleCrew PRIVATE src/map.cpp)
# The map's NPC commands (src/mapcmd.cpp, src/mapcmd_logic.h).
target_sources(EDF6VehicleCrew PRIVATE src/mapcmd.cpp)
# The EMC's charged beam (src/emc.cpp, src/emc_plan.h).
target_sources(EDF6VehicleCrew PRIVATE src/emc.cpp)
# The Proteus rework (its own line too).
target_sources(EDF6VehicleCrew PRIVATE src/proteus.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/boarding.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/primer.cpp)
# What a blocked jet ran into, logged (src/impact.cpp).
target_sources(EDF6VehicleCrew PRIVATE src/impact.cpp)
# The map's real play area: the walls where its ground ends (src/playarea.cpp, src/playarea.h).
target_sources(EDF6VehicleCrew PRIVATE src/playarea.cpp)
# The stock split missiles (MissileBullet02) split short of a big target's surface.
target_sources(EDF6VehicleCrew PRIVATE src/splitmissile.cpp)
# Which machine runs the plugin's work on an object online (src/online_authority.h, docs/online-re.md section 3).
target_sources(EDF6VehicleCrew PRIVATE src/online_authority.cpp)
# Every stock homing round (MissileBullet01/02, HomingLaserBullet01) steers by proportional navigation (src/pn.h).
target_sources(EDF6VehicleCrew PRIVATE src/guidance.cpp)
# Every lock-on weapon's lock: the order it locks in (the player's nearest the view), the target cycle, reading it.
target_sources(EDF6VehicleCrew PRIVATE src/lockon.cpp)
# The Tempest cruise missiles flown from their nose (TV guidance; the view and the hold are the map's).
target_sources(EDF6VehicleCrew PRIVATE src/tvguide.cpp)
# The friendly NPC soldiers' own AI (src/npcai.cpp, decisions in src/npc_logic.h; docs/npc-ai-design.md).
target_sources(EDF6VehicleCrew PRIVATE src/npcai.cpp src/npcpost.cpp)
target_sources(EDF6VehicleCrew PRIVATE src/command_unit.cpp)
# edf6common: the memory / patch / image / seat code both plugins link (common/)
add_subdirectory(common)
target_link_libraries(EDF6VehicleCrew PRIVATE edf6common)
configure_file(src/version.h.in "${CMAKE_BINARY_DIR}/generated/version.h" @ONLY)
target_include_directories(EDF6VehicleCrew PRIVATE third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(EDF6VehicleCrew PRIVATE xaudio2 sapi ole32)
target_compile_options(EDF6VehicleCrew PRIVATE /W4 /WX /utf-8 /permissive-)
target_link_options(EDF6VehicleCrew PRIVATE /DYNAMICBASE /NXCOMPAT /PDBALTPATH:EDF6VehicleCrew.pdb)
# Build output, never tracked: build/Mods/Plugins holds both plugins and their ini files, laid out as they go
# into the game (tools/installer.py and tools/build_release.py take them from there).
set_target_properties(EDF6VehicleCrew PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Mods/Plugins")
configure_file(EDF6VehicleCrew.ini "${CMAKE_BINARY_DIR}/Mods/Plugins/EDF6VehicleCrew.ini" COPYONLY)

# EDF6AutoTurret: the flak / Bohr auto-aim and the tank side guns (autoturret/README.md)
add_subdirectory(autoturret)

add_executable(append_log_test EXCLUDE_FROM_ALL tools/append_log_test.cpp)
target_link_libraries(append_log_test PRIVATE edf6common)
target_compile_options(append_log_test PRIVATE /W4 /WX /utf-8 /permissive-)
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

# The gunship's muzzle (src/gunmuzzle.h) on the circles it flies: no round's line through its airframe, its start where the
# muzzle is when it leaves, against the old origin start and the shell's stock wait, offline (tools/gunship_muzzle_check.cpp).
# Not part of the plugin build: cmake --build build --target gunship_muzzle_check && build\gunship_muzzle_check.exe
add_executable(gunship_muzzle_check EXCLUDE_FROM_ALL tools/gunship_muzzle_check.cpp)
target_compile_options(gunship_muzzle_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(gunship_muzzle_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The exhausts' frame (src/exhaust_pose.h): a jet flown frame by frame in the game's order, its flame's root against its
# nozzle as drawn, before / after carrying the bone record to this frame (tools/exhaust_pose_check.cpp). Not part of the
# plugin build: cmake --build build --target exhaust_pose_check && build\exhaust_pose_check.exe
add_executable(exhaust_pose_check EXCLUDE_FROM_ALL tools/exhaust_pose_check.cpp)
target_compile_options(exhaust_pose_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(exhaust_pose_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The gun stabilizer (src/stab.h) on a hull driven over bumps and through turns against the stock aim axis step, the
# gun's world pointing error with and without it, offline (tools/stab_check.cpp). Not part of the plugin build:
# cmake --build build --target stab_check && build\stab_check.exe
add_executable(stab_check EXCLUDE_FROM_ALL tools/stab_check.cpp)
target_compile_options(stab_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(stab_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The Proteus rework's rules (src/proteus_logic.h: the stances and their stagger, the directional shield's heat, the barrier,
# the salvo's cooldown, the step height) offline (tools/proteus_check.cpp). Not part of the plugin build:
# cmake --build build --target proteus_check && build\proteus_check.exe
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
# The Depth Crawler's driving on a floor, a wall and a ceiling (src/crawl_logic.h), offline (tools/crawl_check.cpp).
# Not part of the plugin build:
# cmake --build build --target crawl_check && build\crawl_check.exe
add_executable(crawl_check EXCLUDE_FROM_ALL tools/crawl_check.cpp)
target_compile_options(crawl_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(crawl_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
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
# The remote gunner's recoil on the vehicle's authority (src/gunnerrecoil.cpp) against a stand-in EDF.dll.
add_executable(gunner_recoil_test tests/gunner_recoil_test.cpp)
target_link_libraries(gunner_recoil_test PRIVATE edf6common)
target_include_directories(gunner_recoil_test PRIVATE third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(gunner_recoil_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME gunner_recoil COMMAND gunner_recoil_test)

add_executable(map_hud_check EXCLUDE_FROM_ALL tools/map_hud_check.cpp)
target_compile_options(map_hud_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(map_hud_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

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
# The online gates (src/online_authority.h): their rules on every case, and a source guard that the plugin asks them.
add_executable(online_authority_test tests/online_authority_test.cpp)
target_compile_options(online_authority_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME online_authority COMMAND online_authority_test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME online_gate_guard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/online_gate_guard.py")
# Proportional navigation (src/pn.h) against the stock pure pursuit at the same turn limit.
add_executable(pn_test tests/pn_test.cpp)
target_compile_options(pn_test PRIVATE /W4 /WX /utf-8 /permissive-)
add_test(NAME pn COMMAND pn_test)

# Run the feature checks in CI as well as locally; these targets remain optional for plugin-only builds.
set(EDF6_OFFLINE_CHECKS append_log_test pjet_kinds heli_aim_check heli_fire_check
    nix_twist_check seat_aim_check rounds_check turret_cam_check turret_lead_check warn_check grape_turret_check vehicle_ram_check stab_check vsound_check map_cam_check map_cmd_check crawl_check map_marks_check hud_cue_check emc_check proteus_check sidecar_board_check map_hud_check pause_clock_check hudtext_check gunship_muzzle_check exhaust_pose_check airbound_check hover_lift_check view_clip_check npc_ai_check)
add_custom_target(offline_checks DEPENDS ${EDF6_OFFLINE_CHECKS} hud_view jet_obstacle_sim)
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

add_executable(map_command_runtime_check EXCLUDE_FROM_ALL tools/map_command_runtime_check.cpp)
target_link_libraries(map_command_runtime_check PRIVATE edf6common user32)
target_compile_options(map_command_runtime_check PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks map_command_runtime_check)
add_test(NAME map_command_runtime COMMAND map_command_runtime_check)
add_test(NAME jet_player_rotor_suite COMMAND jet_obstacle_sim --player-rotor-suite)
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
target_link_libraries(gunner_aim_object_test PRIVATE edf6common)
target_link_libraries(heli_player_input_test PRIVATE edf6common user32)
add_executable(jetsound_check EXCLUDE_FROM_ALL tools/jetsound_check.cpp)
target_include_directories(jetsound_check PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(jetsound_check PRIVATE /W4 /WX /utf-8 /permissive-)
add_dependencies(offline_checks jetsound_check)
add_test(NAME jetsound_check COMMAND jetsound_check)
add_test(NAME vehicle_cas COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_cas_pose.py")
# The Begaruta family (Proteus too) chains its per-frame slot 4; with the game present, against EDF.dll.
add_test(NAME begaruta_family_profile COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/begaruta_family_native_profile_test.py")
add_test(NAME vehicle_shapes COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/test_vehicle_model_shapes.py")
add_test(NAME aircraft_collision COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/aircraft_collision_check.py")

foreach(check IN ITEMS heli_command_test jet_command_test bullet_pass_test rocket_guide_prediction_test rocket_impact_readout_test npc_post_test map_command_identity_test)
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
add_executable(npc_core_check EXCLUDE_FROM_ALL tools/npc_core_check.cpp)
target_include_directories(npc_core_check PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(npc_core_check PRIVATE edf6common user32)
target_compile_options(npc_core_check PRIVATE /W4 /WX /utf-8 /permissive- /Gy)
target_link_options(npc_core_check PRIVATE /OPT:REF)
add_dependencies(offline_checks npc_core_check)
add_test(NAME npc_core_check COMMAND npc_core_check)
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
