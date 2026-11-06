# Shared host runtime seam (issue #177, P5).
#
# Portable: no game headers and no MinHook dependency of its own - the adapter
# injects its functions through Dinput8Runtime::Bindings.
#
# Both translation units are always compiled: dinput8_runtime.cpp is the Windows
# loader, and reframework_runtime.cpp has to stay in the source list so the
# static library still has a source on hosts without a dinput8 loader. The
# Resident Evil 4 integration inside reframework_runtime.cpp is behind
# MC_ENABLE_RE4 (default OFF): with the option off that file compiles to a stub
# whose initialize() refuses, with it on the REFramework host is built.
add_library(mc_host_runtime STATIC
    reframework_runtime.cpp
)

target_include_directories(mc_host_runtime PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../include"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_compile_features(mc_host_runtime PUBLIC cxx_std_20)

if(MC_ENABLE_RE4)
    target_compile_definitions(mc_host_runtime PUBLIC MC_ENABLE_RE4=1)
    message(STATUS "mc_host_runtime: Resident Evil 4 (REFramework) integration enabled")
else()
    target_compile_definitions(mc_host_runtime PUBLIC MC_ENABLE_RE4=0)
endif()

if(WIN32)
    target_sources(mc_host_runtime PRIVATE dinput8_runtime.cpp)
endif()
