# A VERSIONINFO resource (company, product, description, version) in every executable and DLL a directory builds.
# Without one, an unsigned, rarely seen PE whose only import is KERNEL32 (the static CRT's VirtualProtect /
# LoadLibraryExW / GetProcAddress / IsDebuggerPresent) is what reputation scanners score as a dropper: Kaspersky's
# cloud verdict VHO:Trojan.Win32.Agent.gen took build\edf41mother_control_fixture.exe (2026-10-21) and
# build\hudtext_check.exe (2026-10-12) mid-ctest, one rebuild's hash, the next build of the same code clean.
#
# include() it, then edf6_version_info_at_end(): at the end of the directory (cmake_language DEFER) every
# EXECUTABLE / SHARED / MODULE target defined in it gets the resource, those a branch adds below included. The .rc is
# compiled by rc.exe on its own and linked as an object: the targets' C++ options (/W4 /WX /utf-8 ...) never reach it.
include_guard(GLOBAL)
enable_language(RC)

function(edf6_version_info_apply)
  get_directory_property(targets BUILDSYSTEM_TARGETS)
  string(REPLACE "." "," version_commas "${PROJECT_VERSION}.0")
  foreach(target IN LISTS targets)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "EXECUTABLE")
      set(file_type "VFT_APP")
      set(suffix ".exe")
    elseif(type STREQUAL "SHARED_LIBRARY" OR type STREQUAL "MODULE_LIBRARY")
      set(file_type "VFT_DLL")
      set(suffix ".dll")
    else()
      continue()
    endif()
    get_target_property(name ${target} OUTPUT_NAME)
    if(NOT name)
      set(name ${target})
    endif()
    set(description "${PROJECT_NAME} ${name}")
    if(name STREQUAL PROJECT_NAME)
      set(description "${name}")
    endif()
    set(rc "${CMAKE_CURRENT_BINARY_DIR}/versioninfo/${target}.rc")
    set(res "${CMAKE_CURRENT_BINARY_DIR}/versioninfo/${target}.res")
    file(CONFIGURE OUTPUT "${rc}" @ONLY CONTENT [[#include <winver.h>
VS_VERSION_INFO VERSIONINFO
FILEVERSION @version_commas@
PRODUCTVERSION @version_commas@
FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
FILEFLAGS 0
FILEOS VOS_NT_WINDOWS32
FILETYPE @file_type@
FILESUBTYPE VFT2_UNKNOWN
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904B0"
    BEGIN
      VALUE "CompanyName", "hajisensai"
      VALUE "FileDescription", "@description@"
      VALUE "FileVersion", "@PROJECT_VERSION@"
      VALUE "InternalName", "@name@"
      VALUE "LegalCopyright", "hajisensai, github.com/hajisensai/edf6-all-forces"
      VALUE "OriginalFilename", "@name@@suffix@"
      VALUE "ProductName", "EDF6 All Forces (@PROJECT_NAME@)"
      VALUE "ProductVersion", "@PROJECT_VERSION@"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
]])
    add_custom_command(OUTPUT "${res}"
      COMMAND "${CMAKE_RC_COMPILER}" /nologo /fo "${res}" "${rc}"
      DEPENDS "${rc}"
      COMMENT "Version resource for ${name}${suffix}" VERBATIM)
    set_source_files_properties("${res}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    target_sources(${target} PRIVATE "${res}")
  endforeach()
endfunction()

macro(edf6_version_info_at_end)
  cmake_language(DEFER CALL edf6_version_info_apply)
endmacro()
