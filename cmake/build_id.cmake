# Writes OUT (generated/build_id.h): the commit both plugins were built from, "+dirty" with uncommitted changes to
# tracked files, "unknown" without git. Run at every build (the edf6_build_id target), the header rewritten only when
# it changes, so the plugins' load line names the build a log came from (a game directory several sessions install
# into: CLAUDE.md, 实机测试).
execute_process(COMMAND git -C "${SRC}" rev-parse --short=10 HEAD OUTPUT_VARIABLE commit RESULT_VARIABLE failed
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(failed OR NOT commit)
  set(commit "unknown")
else()
  execute_process(COMMAND git -C "${SRC}" status --porcelain --untracked-files=no OUTPUT_VARIABLE changes ERROR_QUIET)
  if(changes)
    string(APPEND commit "+dirty")
  endif()
endif()
set(text "// Generated at build time by cmake/build_id.cmake: the commit the plugins were built from.\n#pragma once\n#define EDF6_BUILD_ID \"${commit}\"\n")
if(EXISTS "${OUT}")
  file(READ "${OUT}" previous)
endif()
if(NOT previous STREQUAL text)
  file(WRITE "${OUT}" "${text}")
endif()
