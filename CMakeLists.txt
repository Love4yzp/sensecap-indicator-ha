# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET esp32s3)
execute_process(
    COMMAND git describe --tags --always --dirty --match "v[0-9]*"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE GIT_DESCRIBE_RESULT
    OUTPUT_VARIABLE GIT_DESCRIBE_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(GIT_DESCRIBE_RESULT EQUAL 0 AND NOT GIT_DESCRIBE_VERSION STREQUAL "")
    set(PROJECT_VER "${GIT_DESCRIBE_VERSION}")
else()
    set(PROJECT_VER "0.0.0-dev")
    message(STATUS "Git version unavailable; using PROJECT_VER=${PROJECT_VER}")
endif()

set(EXTRA_COMPONENT_DIRS components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-fdiagnostics-color=always -w)

project(indicator_ha)
