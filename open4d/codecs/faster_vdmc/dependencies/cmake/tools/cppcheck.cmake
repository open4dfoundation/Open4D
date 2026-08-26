# additional target to perform cppcheck run, requires cppcheck

find_program(CPPCHECK_PATH NAMES cppcheck )

if( EXISTS  "${CPPCHECK_PATH}" )
  file(GLOB_RECURSE ALL_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp  
                                     ${CMAKE_CURRENT_SOURCE_DIR}/source/*.hpp)
  add_custom_target(
    cppcheck
    COMMAND ${CPPCHECK_PATH}
            --enable=warning,performance,portability,information,missingInclude
            --std=c++14
            --template="[{severity}][{id}] {message} {callstack} \(On {file}:{line}\)"
            --verbose
            --quiet
            ${ALL_SOURCE_FILES} 
  )
else()
  message("cppcheck executable not found")
endif()

