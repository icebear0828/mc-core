include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(mc_tests
    test_animator.cpp
    test_voxel_world.cpp
    test_combat.cpp
    test_ballistics.cpp
    test_wukong_adapter.cpp
    test_sekiro_adapter.cpp
    test_consumables.cpp
    test_elytra.cpp
    test_hud.cpp
    test_hud_layout.cpp
    test_session.cpp
    test_rig.cpp
    test_item_model.cpp
    test_sekiro_live.cpp
    test_sekiro_enemies.cpp
    test_sekiro_host_write.cpp
    test_sekiro_input_filter.cpp
    test_sekiro_damage_probe.cpp
    test_sekiro_steve.cpp
    test_sekiro_model.cpp
    test_eldenring_live.cpp
    test_eldenring_world.cpp
    test_eldenring_state.cpp
    test_eldenring_damage.cpp
    test_eldenring_steve.cpp
    test_eldenring_melee.cpp
    test_eldenring_los.cpp
    test_eldenring_survival.cpp
    test_eldenring_audio.cpp
)

target_compile_definitions(mc_tests PRIVATE MC_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/source")

target_link_libraries(mc_tests
    PRIVATE
        mc_core
        wukong_adapter
        sekiro_adapter
        eldenring_live
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(mc_tests)
