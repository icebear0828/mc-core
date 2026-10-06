cmake_minimum_required(VERSION 3.24)
project(mc_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Compiler Warnings
if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Core static library
add_library(mc_core STATIC
    include/mc/types.hpp
    include/mc/animator.hpp
    include/mc/voxel_world.hpp
    include/mc/ballistics.hpp
    include/mc/combat.hpp
    include/mc/consumables.hpp
    include/mc/elytra.hpp
    include/mc/contracts/physics_adapter.hpp
    include/mc/contracts/render_adapter.hpp
    include/mc/contracts/combat_adapter.hpp
    include/mc/contracts/input_adapter.hpp
    src/animator.cpp
    src/voxel_world.cpp
    src/ballistics.cpp
    src/combat.cpp
    src/consumables.cpp
    src/elytra.cpp
)

target_include_directories(mc_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Adapters
option(BUILD_ADAPTERS "Build game adapters." ON)
if(BUILD_ADAPTERS AND CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_subdirectory(adapters/wukong)
    add_subdirectory(adapters/sekiro)
endif()

# Testing
option(BUILD_TESTING "Build the testing tree." ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

