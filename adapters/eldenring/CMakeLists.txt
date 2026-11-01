# Elden Ring port. The platform-independent live-memory layer is header only (eldenring_live); the Windows loader
# (dinput8.dll proxy) is built only on Windows.
add_library(eldenring_live INTERFACE)
target_include_directories(eldenring_live INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(WIN32)
    include(FetchContent)
    if(NOT TARGET minhook)
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
        add_library(minhook STATIC
            ${minhook_SOURCE_DIR}/src/buffer.c
            ${minhook_SOURCE_DIR}/src/hook.c
            ${minhook_SOURCE_DIR}/src/trampoline.c
            ${minhook_SOURCE_DIR}/src/hde/hde64.c
        )
        target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)
        if(MSVC)
            target_compile_options(minhook PRIVATE /W3 /WX-)
        endif()
    endif()

    # Dear ImGui with the DX12 backend (own target: the Sekiro build links the DX11 one).
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
    add_library(imgui_dx12 STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    )
    target_include_directories(imgui_dx12 PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    if(MSVC)
        target_compile_options(imgui_dx12 PRIVATE /W3 /WX-)
    endif()

    # The Steve HLSL lives in shaders/steve.hlsl. Embed it at configure time so the runtime D3DCompile path
    # stays byte-identical and needs no external shader files or fxc/dxc build step.
    set(MC_STEVE_HLSL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/steve.hlsl")
    file(READ "${MC_STEVE_HLSL_FILE}" MC_STEVE_HLSL_SOURCE)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/shaders/steve_shader_source.hpp.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/generated/steve_shader_source.hpp" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MC_STEVE_HLSL_FILE}")

    set(ELDENRING_ADAPTER_SRCS
        src/loader.cpp
        src/core/loader_state.cpp
        src/core/loader_services.cpp
        src/core/player_services.cpp
        src/core/domain_wiring.cpp
        src/hooks/hook_setup.cpp
        src/hooks/camera_runtime.cpp
        src/combat/combat_services.cpp
        src/combat/bullet_runtime.cpp
        src/world/summon_runtime.cpp
        src/world/mining_runtime.cpp
        src/world/block_actions.cpp
        src/world/tnt_runtime.cpp
        src/world/flags_runtime.cpp
        src/movement/movement_runtime.cpp
        src/ui/hud_overlay.cpp
        src/overlay_d3d12.cpp
        src/overlay_hook.cpp
        src/overlay_hud.cpp
        src/overlay_scene.cpp
        src/overlay_textures.cpp
        src/steve_renderer_d3d12.cpp
        src/steve_draw_first_person.cpp
        src/render/d3d12_util.cpp
        src/render/d3d12_shader_lib.cpp
        src/render/d3d12_pipeline_builder.cpp
        src/render/steve_pipeline.cpp
        src/render/steve_mesh_factory.cpp
        src/render/steve_texture_cache.cpp
        src/input_hook.cpp
        src/png_wic.cpp
        src/audio_xaudio2.cpp
        src/core/diagnostics.cpp
        src/core/memory_scanner.cpp
        src/ui/inventory_controller.cpp
        src/ui/hud_bridge.cpp
        src/debug/probes.cpp
        src/movement/player_locomotion.cpp
        src/world/voxel_world_runtime.cpp
        src/combat/combat_service.cpp
        src/combat/projectile_service.cpp
        src/input/hotkey_dispatcher.cpp
        src/hooks/game_hooks.cpp
        eldenring_dinput8.def)

    if(ENABLE_CS2_VIEWMODEL)
        list(APPEND ELDENRING_ADAPTER_SRCS
            src/cs_renderer_d3d12.cpp
            src/cs_weapon_registry.cpp)
    endif()

    if(ENABLE_OW_VIEWMODEL)
        list(APPEND ELDENRING_ADAPTER_SRCS
            src/ow_renderer_d3d12.cpp)
    endif()

    add_library(eldenring_adapter SHARED ${ELDENRING_ADAPTER_SRCS})
    set_target_properties(eldenring_adapter PROPERTIES
        OUTPUT_NAME dinput8
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/eldenring"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/eldenring"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/eldenring"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/eldenring"
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON)
    target_include_directories(eldenring_adapter PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(eldenring_adapter PRIVATE eldenring_live mc_core minhook imgui_dx12 psapi d3d12 dxgi dxguid d3dcompiler windowscodecs ole32 xaudio2)
    if(MSVC)
        target_compile_options(eldenring_adapter PRIVATE /W4 /WX /permissive- /utf-8)
    endif()
endif()
