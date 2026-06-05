cmake_minimum_required(VERSION 3.27)
project(cppreference-mcp VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Sanitizer support (from email_backup)
option(ENABLE_SANITIZERS "Enable sanitizers for Debug builds" OFF)

if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Enabling sanitizers for Debug build")
    set(SANITIZER_FLAGS "-fsanitize=address,undefined -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SANITIZER_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SANITIZER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${SANITIZER_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${SANITIZER_FLAGS}")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")
endif()

# Output directory: out/<Debug|Release>/
set(BUILD_TYPE "${CMAKE_BUILD_TYPE}")
if(CMAKE_CONFIGURATION_TYPES)
    if("${CMAKE_CFG_INTDIR}" STREQUAL ".")
        set(BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    else()
        set(BUILD_TYPE "${CMAKE_CFG_INTDIR}")
    endif()
else()
    if("${BUILD_TYPE}" STREQUAL "")
        set(BUILD_TYPE "Debug")
    endif()
endif()
set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/../out/${BUILD_TYPE}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Homebrew paths for macOS ARM64
list(PREPEND CMAKE_PREFIX_PATH /opt/homebrew)
set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl)

# FetchContent for dependencies
include(FetchContent)

FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
        cpp-httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.16.0
)
FetchContent_MakeAvailable(cpp-httplib)

# Fetch thread_pool from GitHub
FetchContent_Declare(
    thread_pool
    GIT_REPOSITORY https://github.com/AbstractSoft/thread_pool.git
    GIT_TAG main
)
FetchContent_MakeAvailable(thread_pool)

# Fetch configuration from GitHub (header-only, fetch sources only)
FetchContent_Declare(
    configuration
    GIT_REPOSITORY https://github.com/AbstractSoft/configuration.git
    GIT_TAG main
)
cmake_policy(PUSH)
cmake_policy(SET CMP0169 OLD)
FetchContent_GetProperties(configuration)
if(NOT configuration_POPULATED)
    FetchContent_Populate(configuration)
    add_library(configuration_lib INTERFACE)
    target_include_directories(configuration_lib INTERFACE
        ${configuration_SOURCE_DIR}/include
    )
    target_link_libraries(configuration_lib INTERFACE nlohmann_json::nlohmann_json)
    add_library(configuration::configuration ALIAS configuration_lib)
endif()
cmake_policy(POP)

# SQLite3
find_package(SQLite3 REQUIRED)

# html_md library (HTML-to-markdown converter, depends on gumbo-parser)
include(FetchContent)

FetchContent_Declare(
        gumbo
        GIT_REPOSITORY https://codeberg.org/gumbo-parser/gumbo-parser.git
        GIT_TAG 0.13.2
        GIT_SHALLOW TRUE
)

FetchContent_Declare(
        html_md
        GIT_REPOSITORY https://github.com/AbstractSoft/html_md.git
        GIT_TAG main
        GIT_SHALLOW TRUE
)

FetchContent_GetProperties(gumbo)
if(NOT gumbo_POPULATED)
    FetchContent_Populate(gumbo)
    file(GLOB _gumbo_c_files "${gumbo_SOURCE_DIR}/src/*.c")
    set(_gumbo_cmake_content "cmake_minimum_required(VERSION 3.15)\nproject(gumbo C)\nset(CMAKE_C_STANDARD 99)\nset(CMAKE_C_STANDARD_REQUIRED ON)\nset(_gumbo_srcs\n")
    foreach(_src IN LISTS _gumbo_c_files)
        string(APPEND _gumbo_cmake_content "    \"${_src}\"\n")
    endforeach()
    string(APPEND _gumbo_cmake_content ")\nadd_library(gumbo STATIC \${_gumbo_srcs})\ntarget_include_directories(gumbo PUBLIC\n    \"${gumbo_SOURCE_DIR}/src\"\n)\n")
    file(WRITE "${gumbo_BINARY_DIR}/CMakeLists.txt" "${_gumbo_cmake_content}")
    add_subdirectory("${gumbo_BINARY_DIR}" "${gumbo_BINARY_DIR}/_build")
endif()

FetchContent_GetProperties(html_md)
if(NOT html_md_POPULATED)
    FetchContent_Populate(html_md)
endif()

add_library(html_md STATIC "${html_md_SOURCE_DIR}/src/converter.cpp")
target_include_directories(html_md PUBLIC "${html_md_SOURCE_DIR}/include")
target_link_libraries(html_md PRIVATE gumbo)

add_executable(cppreference-mcp
        src/main.cpp
        src/client/client.cpp
        src/server/server.cpp
        src/config/config.cpp
        src/db/sqlite_db.hpp
        src/db/sqlite_db.cpp
        src/cache/cache.cpp
        src/parser/html_to_md.cpp
)

target_compile_definitions(cppreference-mcp PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(cppreference-mcp PRIVATE
        nlohmann_json::nlohmann_json
        httplib::httplib
        ${SQLITE3_LIBRARIES}
        thread_pool::thread_pool
        configuration::configuration
        html_md
)

target_include_directories(cppreference-mcp PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${cpp-httplib_SOURCE_DIR}
        ${nlohmann_json_SOURCE_DIR}/single_include
        ${configuration_SOURCE_DIR}/include
        ${thread_pool_SOURCE_DIR}/include
        ${html_md_SOURCE_DIR}/include
)
if(NOT SQLITE3_LIBRARIES)
    target_link_libraries(cppreference-mcp PRIVATE sqlite3)
endif()

# Copy config.json to output directory alongside the binary
add_custom_command(TARGET cppreference-mcp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.json"
        "$<TARGET_FILE_DIR:cppreference-mcp>/config.json"
)

# Clean up FetchContent artifacts leaked into out/
add_custom_command(TARGET cppreference-mcp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:cppreference-mcp>/thread_pool.hpp"
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:cppreference-mcp>/libthread_pool.a"
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:cppreference-mcp>/libgumbo.a"
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:cppreference-mcp>/libhtml_md.a"
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:cppreference-mcp>/converter.hpp"
)

# C++23 features
target_compile_features(cppreference-mcp PRIVATE cxx_std_23)
