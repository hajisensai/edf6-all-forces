cmake_minimum_required(VERSION 3.20)
project(EDF6VehicleCrew VERSION 0.7.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(EDF6VehicleCrew SHARED src/plugin.cpp src/crew.cpp src/heli.cpp src/ground.cpp src/jet.cpp src/jet_flight.cpp src/jet_combat.cpp src/jet_carrier.cpp src/jet_bay.cpp src/jet_spawn.cpp src/jet_hooks.cpp src/airstrike.cpp src/loadout.cpp src/subcarrier.cpp src/carrierlaser.cpp src/overlay.cpp src/hud.cpp src/launcher.cpp src/katyusha.cpp src/highcam.cpp src/turretcam.cpp src/helisight.cpp src/jetsound.cpp src/jetaudio.cpp src/playerjet.cpp src/missile.cpp src/stores.cpp src/bigworld.cpp src/jetprops.cpp src/booster.cpp src/shield.cpp src/view.cpp src/mission.cpp src/body506.cpp src/physics.cpp src/drill.cpp src/gear.cpp src/netprobe.cpp src/glyphs.cpp)
# edf6common: the memory / patch / image / seat code both plugins link (common/)
add_subdirectory(common)
target_link_libraries(EDF6VehicleCrew PRIVATE edf6common)
configure_file(src/version.h.in "${CMAKE_BINARY_DIR}/generated/version.h" @ONLY)
target_include_directories(EDF6VehicleCrew PRIVATE third_party/EDFModLoader "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(EDF6VehicleCrew PRIVATE xaudio2)
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

# The turret camera's rig and turret command (src/turretcam.h) against the stock aim axis step, offline
# (tools/turret_cam_check.cpp). Not part of the plugin build:
# cmake --build build --target turret_cam_check && build	urret_cam_check.exe
add_executable(turret_cam_check EXCLUDE_FROM_ALL tools/turret_cam_check.cpp)
target_compile_options(turret_cam_check PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(turret_cam_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

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
