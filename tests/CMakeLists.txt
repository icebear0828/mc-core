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
)

target_link_libraries(mc_tests
    PRIVATE
        mc_core
        wukong_adapter
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(mc_tests)
