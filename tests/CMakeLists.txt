add_executable(anim_tests
    test_hash.cpp
    test_serialization.cpp
    test_json_writer.cpp
    test_validation.cpp
    test_pool.cpp
    test_transform.cpp
    test_curve.cpp
    test_clip.cpp
    test_skeleton.cpp
    test_blend.cpp
    test_palette.cpp
    test_rootmotion.cpp
    test_sample_clip.cpp
    test_humanoid.cpp
    test_fsm.cpp
    test_jobs.cpp
    test_integration.cpp
    test_converter.cpp
    test_animation_manifest.cpp
    test_animation_runtime.cpp
    test_playable_graph.cpp
    test_playable_nodes.cpp
    test_events.cpp
    test_event_observer.cpp
    test_fade.cpp
    test_layer.cpp
    test_hobi.cpp
    test_solo_animation.cpp
    test_named_hobi.cpp
    test_clip_collection.cpp
    test_mixers.cpp
    test_transition_sequence.cpp
    test_utilities.cpp
    test_directional.cpp
    test_state_selector.cpp
    test_bgfx_input.cpp
    "${CMAKE_SOURCE_DIR}/samples/bgfx_demo/Input.cpp"
)

target_include_directories(anim_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/samples/bgfx_demo")

target_compile_definitions(anim_tests PRIVATE
    ANIM_ASSETS_DIR="${CMAKE_SOURCE_DIR}/../Assets"
    ANIM_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(anim_tests PRIVATE anim::anim anim_converter_core anim_validate_core GTest::gtest_main)

if(ANIM_BUILD_ANIM_PACK)
    target_sources(anim_tests PRIVATE test_anim_pack.cpp)
    target_link_libraries(anim_tests PRIVATE anim_pack_core)
endif()

if(ANIM_BUILD_DEBUG_TOOLS)
    target_sources(anim_tests PRIVATE test_debug_snapshot.cpp test_debug_recorder.cpp)
    target_link_libraries(anim_tests PRIVATE anim::debug)
endif()

if(ANIM_ENABLE_ENTT)
    target_sources(anim_tests PRIVATE test_entt.cpp)
    target_link_libraries(anim_tests PRIVATE anim::entt)
endif()

include(GoogleTest)
gtest_discover_tests(anim_tests)
