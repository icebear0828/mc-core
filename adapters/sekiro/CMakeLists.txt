cmake_minimum_required(VERSION 3.24)
project(sekiro_mc_adapter VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Warnings
if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8)
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

if(WIN32)
    include(FetchContent)
    FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG        v1.3.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(minhook)
    if(NOT minhook_POPULATED)
        FetchContent_Populate(minhook)
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(MINHOOK_HDE_SRC ${minhook_SOURCE_DIR}/src/hde/hde64.c)
    else()
        set(MINHOOK_HDE_SRC ${minhook_SOURCE_DIR}/src/hde/hde32.c)
    endif()

    if(NOT TARGET minhook)
        add_library(minhook STATIC
            ${minhook_SOURCE_DIR}/src/buffer.c
            ${minhook_SOURCE_DIR}/src/hook.c
            ${minhook_SOURCE_DIR}/src/trampoline.c
            ${MINHOOK_HDE_SRC}
        )
        target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)
        if(MSVC)
            target_compile_options(minhook PRIVATE /W3 /WX-)
        endif()
    endif()

    # Dear ImGui for in-game diagnostic HUD and mode feedback
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.90.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(imgui)
    if(NOT imgui_POPULATED)
        FetchContent_Populate(imgui)
    endif()

    if(NOT TARGET imgui)
        add_library(imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
        )
        target_include_directories(imgui PUBLIC
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
        )
        if(MSVC)
            target_compile_options(imgui PRIVATE /W3 /WX-)
        endif()
    endif()

    set(SEKIRO_PLATFORM_SRCS
        src/dinput8_loader.cpp
        src/steve_renderer.cpp
        src/depth_capture.cpp
        dinput8.def
    )
endif()

add_library(sekiro_adapter SHARED
    src/sekiro_adapter.cpp
    src/sekiro_live.cpp
    src/sekiro_model.cpp
    src/sekiro_steve.cpp
    src/plugin_entry.cpp
    ${SEKIRO_PLATFORM_SRCS}
)

target_include_directories(sekiro_adapter
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        "${MC_CORE_DIR}/include"
)

if(WIN32)
    target_include_directories(sekiro_adapter PRIVATE
        ${minhook_SOURCE_DIR}/include
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
endif()

target_link_libraries(sekiro_adapter
    PUBLIC
        mc_core
)

if(WIN32)
    target_link_libraries(sekiro_adapter PRIVATE
        minhook
        imgui
        d3d11
        dxgi
        psapi
        windowscodecs
        d3dcompiler
    )
endif()

if(WIN32)
    set_target_properties(sekiro_adapter PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )

    # Automatically generate dinput8.dll alias for direct Sekiro loading
    add_custom_command(TARGET sekiro_adapter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:sekiro_adapter>
            $<TARGET_FILE_DIR:sekiro_adapter>/dinput8.dll
        COMMENT "Copying sekiro_adapter.dll as dinput8.dll for direct Sekiro root injection"
    )
endif()
