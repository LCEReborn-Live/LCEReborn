# Writes BuildInfo.hpp with the commit, exact release tag, and UTC build time. It runs on every build and rewrites
# the header only when a value changed, so an unchanged tree doesn't recompile.
execute_process(COMMAND git rev-parse --short=8 HEAD
                WORKING_DIRECTORY "${SOURCE_DIR}"
                OUTPUT_VARIABLE COMMIT
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE COMMIT_RESULT
                ERROR_QUIET)
if(NOT COMMIT_RESULT EQUAL 0 OR COMMIT STREQUAL "")
  set(COMMIT "unknown")
  set(DIRTY 0)
else()
  execute_process(COMMAND git status --porcelain --untracked-files=no
                  WORKING_DIRECTORY "${SOURCE_DIR}"
                  OUTPUT_VARIABLE CHANGES
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
  if(CHANGES STREQUAL "")
    set(DIRTY 0)
  else()
    set(DIRTY 1)
  endif()
endif()
string(TIMESTAMP DATE "%Y-%m-%d" UTC)
string(TIMESTAMP TIMESTAMP_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)
execute_process(COMMAND git describe --tags --exact-match HEAD
                WORKING_DIRECTORY "${SOURCE_DIR}"
                OUTPUT_VARIABLE TAG
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE TAG_RESULT
                ERROR_QUIET)
if(NOT TAG_RESULT EQUAL 0)
  set(TAG "")
endif()

set(CONTENT "#define LCE_BUILD_COMMIT \"${COMMIT}\"\n#define LCE_BUILD_TAG \"${TAG}\"\n#define LCE_BUILD_DIRTY ${DIRTY}\n#define LCE_BUILD_DATE \"${DATE}\"\n#define LCE_BUILD_TIMESTAMP \"${TIMESTAMP_UTC}\"\n")
if(EXISTS "${OUTPUT}")
  file(READ "${OUTPUT}" EXISTING)
endif()
if(NOT EXISTING STREQUAL CONTENT)
  file(WRITE "${OUTPUT}" "${CONTENT}")
endif()
