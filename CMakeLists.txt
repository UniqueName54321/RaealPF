cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(RaealPF VERSION 1.0.0)

option(RAEALPF_STANDALONE "Build replay tests without the Geode SDK" OFF)
if(RAEALPF_STANDALONE)
    enable_testing()
    add_library(raealpf-core STATIC src/pathfinder/gdr2.cpp)
    target_include_directories(raealpf-core PUBLIC src)
    add_executable(test-gdr2 tests/gdr2.cpp)
    target_link_libraries(test-gdr2 PRIVATE raealpf-core)
    add_test(NAME gdr2 COMMAND test-gdr2)
    return()
endif()

# Add all source files inside src (recursively)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

# Set up the mod binary
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

CPMAddPackage("gh:matcool/gd-imgui-cocos#934958c68b1f050a8976d893584fa71368c1df1f")
target_link_libraries(${PROJECT_NAME} imgui-cocos)

# Set up dependencies, resources, and link Geode.
setup_geode_mod(${PROJECT_NAME} DONT_INSTALL)
