# edf6common: what EDF6VehicleCrew and EDF6AutoTurret share (memory and patch primitives, the EDF.dll
# image check, the seat / rider test, a weapon's muzzles and the world gravity, and the layout facts they
# rest on), linked statically into both DLLs.
add_library(edf6common STATIC memory.cpp patch.cpp host.cpp seat.cpp weapon.cpp)
target_include_directories(edf6common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(edf6common PRIVATE /W4 /WX /utf-8 /permissive-)
# The same with the CRT as a DLL, for the executables (cmake/toolcrt.cmake swaps it in): one CRT per link.
add_library(edf6common_dyn STATIC EXCLUDE_FROM_ALL memory.cpp patch.cpp host.cpp seat.cpp weapon.cpp)
target_include_directories(edf6common_dyn PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(edf6common_dyn PRIVATE /W4 /WX /utf-8 /permissive-)
set_target_properties(edf6common_dyn PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
