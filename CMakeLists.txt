cmake_minimum_required(VERSION 3.20)
project(EDF6VehicleCrew VERSION 0.8.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(EDF6VehicleCrew SHARED src/plugin.cpp src/crew.cpp src/heli.cpp src/ground.cpp src/jet.cpp src/jet_flight.cpp src/jet_combat.cpp src/jet_carrier.cpp src/jet_bay.cpp src/jet_spawn.cpp src/jet_hooks.cpp src/primer.cpp src/airstrike.cpp src/loadout.cpp src/subcarrier.cpp src/carrierlaser.cpp src/overlay.cpp src/hud.cpp src/jetsound.cpp src/jetaudio.cpp src/playerjet.cpp src/missile.cpp src/stores.cpp src/bigworld.cpp src/jetprops.cpp src/booster.cpp src/shield.cpp src/view.cpp src/mission.cpp src/body506.cpp src/physics.cpp)
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

# The Primer creatures' pose code run offline (tools/primer_pose_view.py, tools/primer_chain_view.py render it):
# not part of the plugin.
add_executable(primer_pose_sim tools/primer_pose_sim.cpp)
set_target_properties(primer_pose_sim PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The Primer creatures' flight and fight run without the game (tools/primer_flight_sim.cpp): the plugin's own
# primer.cpp and the jets' flight code against a stand-in world; tools/primer_trace_view.py draws its trace.
add_executable(primer_flight_sim tools/primer_flight_sim.cpp src/primer.cpp src/jet_flight.cpp src/body506.cpp)
target_link_libraries(primer_flight_sim PRIVATE edf6common)
set_target_properties(primer_flight_sim PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
