# Game-independent D3D12 pieces for drawing the rig into a host's frame: resource helpers, the runtime HLSL
# compiler, the pure pipeline-descriptor builder, the pipeline owner and the skin texture cache. Windows only.
#
# The caller supplies its own HLSL source (see mc::rig::d3d12::Compile), so this layer embeds no shader and
# knows no game. Add it after the including project has defined `mc_core`.
if(NOT WIN32)
    return()
endif()

add_library(mc_d3d12_rig STATIC
    include/d3d12_rig/d3d12_util.hpp
    include/d3d12_rig/d3d12_shader_lib.hpp
    include/d3d12_rig/d3d12_pipeline_builder.hpp
    include/d3d12_rig/steve_pipeline.hpp
    include/d3d12_rig/steve_texture_cache.hpp
    include/d3d12_rig/steve_types.hpp
    src/d3d12_util.cpp
    src/d3d12_shader_lib.cpp
    src/d3d12_pipeline_builder.cpp
    src/steve_pipeline.cpp
    src/steve_texture_cache.cpp
)
target_include_directories(mc_d3d12_rig PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(mc_d3d12_rig PUBLIC mc_core)
target_link_libraries(mc_d3d12_rig PRIVATE d3d12 dxgi d3dcompiler)
