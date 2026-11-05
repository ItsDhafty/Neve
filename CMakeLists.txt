cmake_minimum_required(VERSION 3.26)

option(NV_DISABLE_ALT "Disable alternate screen buffer" OFF)
set(NV_TERM_W 0 CACHE STRING "Manual terminal width")
set(NV_TERM_H 0 CACHE STRING "Manual terminal height")

project(neve VERSION 0.0.1)

message("CMake Config: ${CMAKE_BUILD_TYPE}")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED true)

if (${CMAKE_GENERATOR} MATCHES ".*(Make|Ninja).*")
    set(CMAKE_EXPORT_COMPILE_COMMANDS on)
endif()

add_compile_definitions(_FILE_OFFSET_BITS=64 _CRT_SECURE_NO_WARNINGS)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(NV_DEBUG)
endif()

if (NV_DISABLE_ALT)
    add_compile_definitions(NV_DISABLE_ALT)
endif()
if (NOT NV_TERM_W STREQUAL "0")
    add_compile_definitions(NV_TERM_W=${NV_TERM_W})
endif()
if (NOT NV_TERM_H STREQUAL "0")
    add_compile_definitions(NV_TERM_H=${NV_TERM_H})
endif()

include_directories(neve include)

file(REAL_PATH "src/unitybuild.c" UnityBuildFile)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    file(READ ${UnityBuildFile} FileContent)
    string(REPLACE "#include \"" "src/" FileContent ${FileContent})
    string(REPLACE "\"\n" ";" FileContent ${FileContent})

    set(NeveSources "")

    foreach(src IN LISTS FileContent)
        file(REAL_PATH "${src}" ExpandedSource)
        list(APPEND NeveSources ${ExpandedSource})
    endforeach()
else()
    set(NeveSources ${UnityBuildFile})
endif()

add_executable(neve ${NeveSources})

if(MSVC)
    target_compile_options(neve PRIVATE /W4)
else()
    target_compile_options(neve PRIVATE -Wall -Wextra)
endif()

if(NOT WIN32)
    target_link_libraries(neve PRIVATE m)
endif()

enable_testing()

add_subdirectory(tests)

# Copy compile_commands.json only with supported generators
if (${CMAKE_GENERATOR} MATCHES ".*(Make|Ninja).*")
    add_custom_command(
        TARGET neve POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_SOURCE_DIR}/compile_commands.json
    )
endif()
