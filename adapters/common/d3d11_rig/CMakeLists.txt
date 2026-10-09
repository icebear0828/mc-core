# Game-independent D3D11 pieces for drawing the Steve rig into a host's frame: the renderer (scene-depth
# occlusion, ambient matching from the frame) and the depth-buffer capture probe. Windows only.
#
# The including project must already provide the `minhook` target (the depth probe hooks the immediate and
# deferred device contexts). See adapters/sekiro/CMakeLists.txt for how it is fetched.
if(NOT WIN32)
    return()
endif()
if(NOT TARGET minhook)
    message(FATAL_ERROR "d3d11_rig needs a `minhook` target; define it before add_subdirectory(adapters/common/d3d11_rig)")
endif()

add_library(mc_d3d11_rig STATIC
    include/d3d11_rig/rig_renderer.hpp
    include/d3d11_rig/depth_capture.hpp
    include/d3d11_rig/cb_probe.hpp
    src/rig_renderer.cpp
    src/depth_capture.cpp
    src/cb_probe.cpp
)
target_include_directories(mc_d3d11_rig PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(mc_d3d11_rig PUBLIC mc_core)
target_link_libraries(mc_d3d11_rig PRIVATE minhook d3d11 dxgi d3dcompiler)
