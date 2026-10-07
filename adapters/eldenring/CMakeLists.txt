# Elden Ring port. Only the platform-independent live-memory layer exists so far (no loader, no renderer).
add_library(eldenring_live INTERFACE)
target_include_directories(eldenring_live INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
