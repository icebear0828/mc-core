cmake_minimum_required(VERSION 3.24)
project(mc_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Feature Options
#
# The three modes are independent build axes:
#   ENABLE_MC_MODE         - Minecraft gameplay available at runtime
#   ENABLE_CS2_VIEWMODEL   - Counter-Strike 2 arsenal + viewmodel compiled in
#   ENABLE_OW_VIEWMODEL    - Overwatch Doomfist viewmodel compiled in
#
# A deployment picks any subset; see tools/deploy_modes.py.
option(ENABLE_MC_MODE "Enable the Minecraft gameplay mode at runtime" ON)
if(ENABLE_MC_MODE)
    add_compile_definitions(ENABLE_MC_MODE=1)
else()
    add_compile_definitions(ENABLE_MC_MODE=0)
endif()

option(ENABLE_CS2_VIEWMODEL "Enable Counter-Strike 2 viewmodel and knife inspection subsystem" ON)
if(ENABLE_CS2_VIEWMODEL)
    add_compile_definitions(ENABLE_CS2_VIEWMODEL=1)
else()
    add_compile_definitions(ENABLE_CS2_VIEWMODEL=0)
endif()

# Orthogonal to the CS2 subsystem: the Overwatch (Doomfist) gauntlet has its own
# renderer, descriptor heap, config file and assets.
option(ENABLE_OW_VIEWMODEL "Enable the Overwatch Doomfist first-person viewmodel" ON)
if(ENABLE_OW_VIEWMODEL)
    add_compile_definitions(ENABLE_OW_VIEWMODEL=1)
else()
    add_compile_definitions(ENABLE_OW_VIEWMODEL=0)
endif()

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
    include/mc/control_policy.hpp
    include/mc/death.hpp
    include/mc/sleep.hpp
    include/mc/voxel_world.hpp
    include/mc/ballistics.hpp
    include/mc/combat.hpp
    include/mc/consumables.hpp
    include/mc/elytra.hpp
    include/mc/hud.hpp
    include/mc/hud_layout.hpp
    include/mc/session.hpp
    include/mc/rig.hpp
    include/mc/item_model.hpp
    include/mc/entity_model.hpp
    include/mc/tnt.hpp
    include/mc/hunger.hpp
    include/mc/xp.hpp
    include/mc/heart_anim.hpp
    include/mc/mining.hpp
    include/mc/loot_table.hpp
    include/mc/recipe.hpp
    include/mc/ground_mining.hpp
    include/mc/shield.hpp
    include/mc/contracts/physics_adapter.hpp
    include/mc/contracts/render_adapter.hpp
    include/mc/contracts/combat_adapter.hpp
    include/mc/contracts/input_adapter.hpp
    include/mc/contracts/hud_renderer.hpp
    include/mc/contracts/mod_interface.hpp
    include/mc/contracts/mod_manager.hpp
    src/animator.cpp
    src/voxel_world.cpp
    src/ballistics.cpp
    src/combat.cpp
    src/consumables.cpp
    src/elytra.cpp
    src/hud.cpp
    src/hud_layout.cpp
    src/session.cpp
    src/rig.cpp
    src/item_model.cpp
    src/entity_model.cpp
    src/tnt.cpp
    src/recipe.cpp
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
    add_subdirectory(adapters/eldenring)
endif()

# Testing
option(BUILD_TESTING "Build the testing tree." ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

