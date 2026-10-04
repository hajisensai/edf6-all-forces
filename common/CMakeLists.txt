# edf6common: what EDF6VehicleCrew and EDF6AutoTurret share (memory and patch primitives, the EDF.dll
# image check, the seat / rider test and the layout facts they rest on), linked statically into both DLLs.
add_library(edf6common STATIC memory.cpp patch.cpp host.cpp seat.cpp)
target_include_directories(edf6common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(edf6common PRIVATE /W4 /WX /utf-8 /permissive-)
