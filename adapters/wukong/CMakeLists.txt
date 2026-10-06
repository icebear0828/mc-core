cmake_minimum_required(VERSION 3.24)
project(wukong_mc_adapter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Warnings
if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# mc-core dependency path
set(MC_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../" CACHE PATH "Path to mc-core root")

if(NOT TARGET mc_core)
    set(BUILD_ADAPTERS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory("${MC_CORE_DIR}" mc_core_build EXCLUDE_FROM_ALL)
endif()

add_library(wukong_adapter SHARED
    src/wukong_adapter.cpp
    src/plugin_entry.cpp
)

target_include_directories(wukong_adapter
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        "${MC_CORE_DIR}/include"
)

target_link_libraries(wukong_adapter
    PUBLIC
        mc_core
)

if(WIN32)
    set_target_properties(wukong_adapter PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
    )
endif()
