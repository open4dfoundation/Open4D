# Generate a version file from a template based on the VCS version
# - Determine the current version string
#    => If git is unavailable, fallback to specified string
# - Test if it is the same as the cached version
# - If same, do nothing, otherwise generate output
if(NOT GIT_EXECUTABLE)
  set(VERSION "${VERSION_FALLBACK}${VERSION_EXTRA}")
else()
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --long --always 
    OUTPUT_VARIABLE VERSION
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    OUTPUT_VARIABLE BRANCH
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  ) 
  execute_process(
    COMMAND ${GIT_EXECUTABLE} status --porcelain
    OUTPUT_VARIABLE STATUS
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(VERSION "${VERSION_FALLBACK}-t${VERSION}-b${BRANCH}${VERSION_EXTRA}" )
  if ( NOT "${STATUS}" STREQUAL "" ) 
    set(VERSION "${VERSION}-uncommited-changes" )
  endif()

  
endif()

if(EXISTS ${VERSION_CACHE_FILE})
  file(READ ${VERSION_CACHE_FILE} VERSION_CACHED)
endif()

file(WRITE ${VERSION_CACHE_FILE} ${VERSION})

if(NOT VERSION_CACHED STREQUAL VERSION OR ${TEMPLATE} IS_NEWER_THAN ${OUTPUT})
  configure_file("${TEMPLATE}" "${OUTPUT}")
endif()

message("V-Mesh Test Model version: ${VERSION}${VERSION_EXTRA}")
