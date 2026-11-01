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
    test_death.cpp
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
    test_eldenring_buddy.cpp
    test_eldenring_creative.cpp
    test_eldenring_mobs.cpp
    test_eldenring_chrscan.cpp
    test_eldenring_flight.cpp
    test_eldenring_state.cpp
    test_eldenring_damage.cpp
    test_eldenring_steve.cpp
    test_eldenring_melee.cpp
    test_eldenring_los.cpp
    test_eldenring_shadow.cpp
    test_eldenring_bullet.cpp
    test_eldenring_bow.cpp
    test_eldenring_survival.cpp
    test_eldenring_audio.cpp
    test_eldenring_particles.cpp
    test_eldenring_fp.cpp
    test_inventory.cpp
    test_inventory_layout.cpp
    test_eldenring_blocks.cpp
    test_eldenring_blockmesh.cpp
    test_eldenring_blockicon.cpp
    test_eldenring_action_lock.cpp
    test_eldenring_hud_anim.cpp
    test_eldenring_jump.cpp
    test_eldenring_orientation.cpp
    test_eldenring_roll.cpp
    test_entity_model.cpp
    test_eldenring_shield.cpp
    test_eldenring_flags.cpp
    test_eldenring_pixel.cpp
    test_eldenring_depth.cpp
    test_eldenring_d3dstate.cpp
    test_eldenring_overlay_state.cpp
    test_tnt.cpp
    test_eldenring_tnt.cpp
    test_eldenring_mining.cpp
    test_loot_table.cpp
    test_ground_mining.cpp
    test_zone_detector.cpp
    test_eldenring_combat_balance.cpp
    test_eldenring_sprint.cpp
    test_eldenring_creeper.cpp
    test_eldenring_runtime_context.cpp
    test_mc_core_modules.cpp
    test_sleeping_kinematics.cpp
    test_eldenring_grace_sleep.cpp
    test_eldenring_inventory_controller.cpp
    test_eldenring_debug_probes.cpp
    test_eldenring_hud_bridge.cpp
    test_eldenring_locomotion.cpp
    test_eldenring_world_runtime.cpp
    test_eldenring_sneak.cpp
    test_eldenring_combat_service.cpp
    test_eldenring_projectile_service.cpp
    test_eldenring_virtual_summon.cpp
    test_eldenring_hotkey_dispatcher.cpp
    test_eldenring_game_hooks.cpp
    test_mod_plugin_framework.cpp
    test_control_policy.cpp
    test_eldenring_control_policy.cpp
    test_eldenring_mod_integration.cpp
    test_eldenring_steve_mesh.cpp
    test_eldenring_pipeline_desc.cpp
    ${CMAKE_SOURCE_DIR}/adapters/eldenring/src/ui/inventory_controller.cpp
    ${CMAKE_SOURCE_DIR}/adapters/eldenring/src/ui/hud_bridge.cpp
    ${CMAKE_SOURCE_DIR}/adapters/eldenring/src/movement/player_locomotion.cpp
    ${CMAKE_SOURCE_DIR}/adapters/eldenring/src/world/voxel_world_runtime.cpp
    ${CMAKE_SOURCE_DIR}/adapters/eldenring/src/combat/combat_service.cpp
    ${CMAKE_SOURCE_DIR}/adapters/eldenring/src/combat/projectile_service.cpp
    ${CMAKE_SOURCE_DIR}/adapters/eldenring/src/input/hotkey_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/adapters/eldenring/src/hooks/game_hooks.cpp
    ${CMAKE_SOURCE_DIR}/adapters/eldenring/src/render/steve_mesh_factory.cpp
)

if(ENABLE_CS2_VIEWMODEL)
    target_sources(mc_tests PRIVATE
        test_cs_viewmodel.cpp
        test_cs_ballistics.cpp
        test_cs_impact_decals.cpp
        ${CMAKE_SOURCE_DIR}/adapters/eldenring/src/cs_weapon_registry.cpp
    )
endif()

if(ENABLE_OW_VIEWMODEL)
    target_sources(mc_tests PRIVATE
        test_ow_viewmodel.cpp
        test_ow_dash.cpp
        test_eldenring_ow_combat.cpp
    )
endif()



target_compile_definitions(mc_tests PRIVATE MC_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/source" MC_CORE_TESTS)
target_include_directories(mc_tests PRIVATE ${CMAKE_SOURCE_DIR}/adapters/eldenring/src)

target_link_libraries(mc_tests
    PRIVATE
        mc_core
        wukong_adapter
        sekiro_adapter
        eldenring_live
        GTest::gtest_main
)

if(WIN32 AND TARGET minhook)
    target_link_libraries(mc_tests PRIVATE minhook)
endif()

include(GoogleTest)
gtest_discover_tests(mc_tests)
