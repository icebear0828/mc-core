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

    add_library(eldenring_adapter SHARED src/loader.cpp src/overlay_d3d12.cpp eldenring_dinput8.def)
    set_target_properties(eldenring_adapter PROPERTIES
        OUTPUT_NAME dinput8
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/eldenring"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/eldenring"
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON)
    target_link_libraries(eldenring_adapter PRIVATE eldenring_live minhook imgui_dx12 psapi d3d12 dxgi)
    if(MSVC)
        target_compile_options(eldenring_adapter PRIVATE /W4 /WX /permissive- /utf-8)
    endif()
endif()
