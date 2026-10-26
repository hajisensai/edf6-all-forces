# The offline checks, tests and tools (every EXECUTABLE a directory builds) link the CRT as a DLL; the plugins and the
# installer's helper DLL keep the static CRT (CMAKE_MSVC_RUNTIME_LIBRARY), which the game's folder needs.
# Why: cmake/versioninfo.cmake's version resource was not enough. Kaspersky's cloud verdict VHO:Trojan.Win32.Agent.gen
# still took build\hudtext_check.exe (2026-10-26): an unsigned, never-seen console exe whose import table is the static
# CRT's KERNEL32 handful (VirtualProtect / LoadLibraryExW / GetProcAddress / IsDebuggerPresent) is a loader's profile
# to a reputation model. With the CRT as a DLL the import table is VCRUNTIME140 / api-ms-win-crt-*, every MSVC console
# program's. The tests run on machines with Visual Studio (local builds, CI), which have the runtime.
#
# A target that tests the static CRT itself (tests/crash_dump_test.cpp: abort in the exe's own CRT) opts out with
#   set_target_properties(<target> PROPERTIES EDF6_STATIC_CRT ON)
# An executable linking edf6common gets edf6common_dyn (common/CMakeLists.txt): one CRT per link.
#
# include() it, then edf6_tool_crt_at_end(): applied at the end of the directory (cmake_language DEFER), so the
# executables a branch adds below are covered too.
include_guard(GLOBAL)
define_property(TARGET PROPERTY EDF6_STATIC_CRT
  BRIEF_DOCS "Keep the static CRT on this executable"
  FULL_DOCS "cmake/toolcrt.cmake: executables link the CRT as a DLL unless this is set.")

function(edf6_tool_crt_apply)
  get_directory_property(targets BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(type ${target} TYPE)
    if(NOT type STREQUAL "EXECUTABLE")
      continue()
    endif()
    get_target_property(keep ${target} EDF6_STATIC_CRT)
    if(keep)
      continue()
    endif()
    set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    get_target_property(libs ${target} LINK_LIBRARIES)
    if(libs)
      list(TRANSFORM libs REPLACE "^edf6common$" "edf6common_dyn")
      set_target_properties(${target} PROPERTIES LINK_LIBRARIES "${libs}")
    endif()
  endforeach()
endfunction()

macro(edf6_tool_crt_at_end)
  cmake_language(DEFER CALL edf6_tool_crt_apply)
endmacro()
