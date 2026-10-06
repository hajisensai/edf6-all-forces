cmake_minimum_required(VERSION 3.20)
project(EDF6VehicleCrew VERSION 0.7.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(EDF6VehicleCrew SHARED src/plugin.cpp src/crew.cpp src/heli.cpp src/ground.cpp src/jet.cpp src/jet_flight.cpp src/jet_combat.cpp src/jet_carrier.cpp src/jet_bay.cpp src/jet_spawn.cpp src/jet_hooks.cpp src/airstrike.cpp src/loadout.cpp src/subcarrier.cpp src/carrierlaser.cpp src/overlay.cpp src/hud.cpp src/launcher.cpp src/katyusha.cpp src/highcam.cpp src/helisight.cpp src/jetsound.cpp src/jetaudio.cpp src/playerjet.cpp src/missile.cpp src/stores.cpp src/bigworld.cpp src/jetprops.cpp src/booster.cpp src/shield.cpp src/view.cpp src/mission.cpp src/body506.cpp src/physics.cpp src/drill.cpp src/gear.cpp src/netprobe.cpp src/glyphs.cpp src/turretaim.cpp src/rounds.cpp src/vhud.cpp src/stockgauge.cpp src/warn.cpp src/payload.cpp src/seatswitch.cpp src/sidecar.cpp src/turretcam.cpp src/vehsound.cpp)
# The Nix's torso twist (its own line: the source list above is where every feature branch adds).
target_sources(EDF6VehicleCrew PRIVATE src/nix.cpp)
# The ground vehicles' ram (its own line, as above).
target_sources(EDF6VehicleCrew PRIVATE src/vehicleram.cpp)
# The gun stabilizer (its own line, as the Nix's).
target_sources(EDF6VehicleCrew PRIVATE src/stab.cpp)
# The map view (src/map.cpp: the overhead camera, the input hold, the marks it gathers).
target_sources(EDF6VehicleCrew PRIVATE src/map.cpp)
# The EMC's charged beam (src/emc.cpp, src/emc_plan.h).
target_sources(EDF6VehicleCrew PRIVATE src/emc.cpp)
# The Proteus rework (its own line too).
target_sources(EDF6VehicleCrew PRIVATE src/proteus.cpp)
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

# The boardable aircraft's flight numbers checked offline (tools/pjet_kinds.cpp); not part of the plugin build:
# cmake --build build --target pjet_kinds && build\pjet_kinds.exe
add_executable(pjet_kinds EXCLUDE_FROM_ALL tools/pjet_kinds.cpp)
target_include_directories(pjet_kinds PRIVATE src common third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_compile_options(pjet_kinds PRIVATE /W4 /WX /utf-8 /permissive-)

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

# The Nix's torso hold (src/nix_twist.h) against a stand-in of the stock update's order, offline (tools/nix_twist_check.cpp).
# Not part of the plugin build: cmake --build build --target nix_twist_check && build\nix_twist_check.exe
add_executable(nix_twist_check EXCLUDE_FROM_ALL tools/nix_twist_check.cpp)
target_compile_options(nix_twist_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(nix_twist_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

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

# The gun stabilizer (src/stab.h) on a hull driven over bumps and through turns against the stock aim axis step, the
# gun's world pointing error with and without it, offline (tools/stab_check.cpp). Not part of the plugin build:
# cmake --build build --target stab_check && build\stab_check.exe
add_executable(stab_check EXCLUDE_FROM_ALL tools/stab_check.cpp)
target_compile_options(stab_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(stab_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The Proteus rework's rules (src/proteus_logic.h: the stances and their stagger, the directional shield's heat, the barrier,
# the salvo's cooldown, the step height) offline (tools/proteus_check.cpp). Not part of the plugin build:
# cmake --build build --target proteus_check && build\proteus_check.exe
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

# The map view's camera and grid math (src/map_cam.h: the eye, the drag that grabs the ground, the zoom's limits, the
# grid's reach), offline (tools/map_cam_check.cpp). Not part of the plugin build:
# cmake --build build --target map_cam_check && build\map_cam_check.exe
add_executable(map_cam_check EXCLUDE_FROM_ALL tools/map_cam_check.cpp)
target_compile_options(map_cam_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(map_cam_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
