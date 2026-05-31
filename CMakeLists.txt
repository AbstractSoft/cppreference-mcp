cmake_minimum_required(VERSION 3.27)
project(cppreference-mcp VERSION 1.0.0 LANGUAGES CXX)

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

# SQLite3
find_package(SQLite3 REQUIRED)

add_executable(cppreference-mcp
        src/main.cpp
        src/client/client.cpp
        src/server/server.cpp
        src/config/config.cpp
        src/db/sqlite_db.hpp
        src/db/sqlite_db.cpp
        src/cache/cache.cpp
        src/thread_pool/thread_pool.cpp
        src/parser/parser.cpp
)

target_compile_definitions(cppreference-mcp PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(cppreference-mcp PRIVATE
        nlohmann_json::nlohmann_json
        httplib::httplib
        ${SQLITE3_LIBRARIES}
)
if(NOT SQLITE3_LIBRARIES)
    target_link_libraries(cppreference-mcp PRIVATE sqlite3)
endif()

target_include_directories(cppreference-mcp PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${cpp-httplib_SOURCE_DIR}
        ${nlohmann_json_SOURCE_DIR}/single_include
)

# Copy config.json to output directory alongside the binary
add_custom_command(TARGET cppreference-mcp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.json"
        "$<TARGET_FILE_DIR:cppreference-mcp>/config.json"
)

# C++23 features
target_compile_features(cppreference-mcp PRIVATE cxx_std_23)
