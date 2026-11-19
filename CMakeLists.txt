cmake_minimum_required(VERSION 3.20)

project(hobi_animator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
    # bgfx's prebuilt Debug libraries use the static debug CRT (/MTd). Keep the
    # whole project on the same runtime so allocations never cross CRT heaps.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

option(ANIM_BUILD_TESTS "Build unit tests" ON)

# ---------------------------------------------------------------------------
# glm: header-only. Prefer an explicit ANIM_GLM_DIR, then a local third_party
# copy, then find_package, then FetchContent.
# ---------------------------------------------------------------------------
set(ANIM_GLM_DIR "" CACHE PATH "Path to a glm source tree containing glm/glm.hpp")

if(NOT ANIM_GLM_DIR AND DEFINED ENV{GLM_DIR})
    set(ANIM_GLM_DIR "$ENV{GLM_DIR}")
endif()
if(NOT ANIM_GLM_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm/glm/glm.hpp")
    set(ANIM_GLM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm")
endif()
if(NOT ANIM_GLM_DIR AND EXISTS "E:/Learn/glm/glm/glm.hpp")
    set(ANIM_GLM_DIR "E:/Learn/glm")
endif()

if(ANIM_GLM_DIR)
    if(NOT EXISTS "${ANIM_GLM_DIR}/glm/glm.hpp")
        message(FATAL_ERROR "ANIM_GLM_DIR='${ANIM_GLM_DIR}' does not contain glm/glm.hpp")
    endif()
    add_library(anim_glm INTERFACE)
    add_library(anim::glm ALIAS anim_glm)
    target_include_directories(anim_glm INTERFACE "${ANIM_GLM_DIR}")
    target_compile_definitions(anim_glm INTERFACE GLM_FORCE_CTOR_INIT GLM_ENABLE_EXPERIMENTAL)
    message(STATUS "Using glm from ${ANIM_GLM_DIR}")
else()
    find_package(glm CONFIG QUIET)
    if(TARGET glm::glm)
        add_library(anim_glm INTERFACE)
        add_library(anim::glm ALIAS anim_glm)
        target_link_libraries(anim_glm INTERFACE glm::glm)
        target_compile_definitions(anim_glm INTERFACE GLM_FORCE_CTOR_INIT GLM_ENABLE_EXPERIMENTAL)
        message(STATUS "Using glm from find_package")
    else()
        include(FetchContent)
        FetchContent_Declare(glm
            GIT_REPOSITORY https://github.com/g-truc/glm.git
            GIT_TAG 1.0.1
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(glm)
        add_library(anim_glm INTERFACE)
        add_library(anim::glm ALIAS anim_glm)
        target_link_libraries(anim_glm INTERFACE glm::glm)
        target_compile_definitions(anim_glm INTERFACE GLM_FORCE_CTOR_INIT GLM_ENABLE_EXPERIMENTAL)
        message(STATUS "Using glm from FetchContent")
    endif()
endif()

# ---------------------------------------------------------------------------
# Core library (L1 + L2). Rendering/ECC/threading are NOT dependencies here.
# ---------------------------------------------------------------------------
add_library(anim STATIC
    src/core/Transform.cpp
    src/core/Serialization.cpp
    src/clip/Curve.cpp
    src/clip/SampleClip.cpp
    src/clip/ClipIO.cpp
    src/avatar/HumanBone.cpp
    src/avatar/MuscleTable.cpp
    src/avatar/HumanoidAvatar.cpp
    src/avatar/HumanoidCalibration.cpp
    src/avatar/MuscleSpace.cpp
    src/avatar/HumanoidIK.cpp
    src/skeleton/Skeleton.cpp
    src/skeleton/Pose.cpp
    src/skeleton/Blend.cpp
    src/skeleton/Palette.cpp
    src/skeleton/RootMotion.cpp
    src/graph/PlayableNode.cpp
    src/graph/PlayableGraph.cpp
    src/graph/Nodes.cpp
    src/jobs/JobUtils.cpp
    src/jobs/TwoBoneIK.cpp
    src/jobs/Damping.cpp
    src/jobs/SimpleLean.cpp
    src/jobs/ThreadPoolJobScheduler.cpp
    src/integration/Json.cpp
    src/integration/JsonWriter.cpp
    src/integration/AnimationManifest.cpp
    src/integration/AnimationClipLibrary.cpp
    src/integration/AnimationTransitions.cpp
    src/integration/Skinning.cpp
    src/hobi/EventSequence.cpp
    src/hobi/EventDispatcher.cpp
    src/hobi/HobiNode.cpp
    src/hobi/HobiState.cpp
    src/hobi/ClipState.cpp
    src/hobi/RetargetClipState.cpp
    src/hobi/HobiLayer.cpp
    src/hobi/LayerList.cpp
    src/hobi/StateDictionary.cpp
    src/hobi/Transition.cpp
    src/hobi/ClipTransitionSequence.cpp
    src/hobi/HobiPlayable.cpp
    src/hobi/HobiAnimator.cpp
    src/hobi/SoloAnimation.cpp
    src/hobi/NamedHobiAnimator.cpp
    src/hobi/MixerStates.cpp
    src/hobi/MixerTransitions.cpp
    src/hobi/CustomFade.cpp
    src/hobi/TimeSynchronizationGroup.cpp
    src/hobi/ExitEvent.cpp
    src/hobi/DontAllowFade.cpp
    src/hobi/DirectionalAnimationSet.cpp
    src/hobi/DirectionalClipTransition.cpp
)
add_library(anim::anim ALIAS anim)

target_include_directories(anim PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(anim PUBLIC anim::glm)

option(ANIM_BUILD_DEBUG_TOOLS "Build runtime debug snapshot/recorder" ON)
if(ANIM_BUILD_DEBUG_TOOLS)
    add_library(anim_debug STATIC
        src/debug/Snapshot.cpp
        src/debug/Recorder.cpp
    )
    add_library(anim::debug ALIAS anim_debug)
    target_link_libraries(anim_debug PUBLIC anim::anim)
endif()

# ---------------------------------------------------------------------------
# Offline converter (P6). The core is a library so tests can link it directly.
# ---------------------------------------------------------------------------
option(ANIM_BUILD_TOOLS "Build offline tools" ON)

add_library(anim_converter_core STATIC
    tools/anim_converter/YamlAnimParser.cpp
    tools/anim_converter/SkeletonLoader.cpp
    tools/anim_converter/AvatarLoader.cpp
    tools/anim_converter/AvatarBuilder.cpp
    tools/anim_converter/FbxMetaParser.cpp
    tools/anim_converter/AnimConverter.cpp
    tools/anim_converter/AbWriter.cpp
)
target_include_directories(anim_converter_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/anim_converter")
target_link_libraries(anim_converter_core PUBLIC anim::anim)

add_subdirectory(tools/anim_validate)

if(ANIM_BUILD_TOOLS)
    add_executable(anim_converter tools/anim_converter/main.cpp)
    target_link_libraries(anim_converter PRIVATE anim_converter_core)

    add_executable(anim_calibrate tools/anim_calibrate/main.cpp)
    target_link_libraries(anim_calibrate PRIVATE anim_converter_core)

    add_executable(anim_bench tools/anim_bench/main.cpp)
    target_link_libraries(anim_bench PRIVATE anim::anim)
endif()

option(ANIM_BUILD_ANIM_PACK "Build the offline glTF animation packer" OFF)
if(ANIM_BUILD_ANIM_PACK)
    add_subdirectory(tools/anim_pack)
endif()

# ---------------------------------------------------------------------------
# Optional EnTT ECS adapter (headers only; core stays ECS-agnostic).
# ---------------------------------------------------------------------------
option(ANIM_ENABLE_ENTT "Build the optional EnTT ECS adapter" OFF)
if(ANIM_ENABLE_ENTT)
    set(ANIM_ENTT_DIR "" CACHE PATH "Path to an EnTT source tree (containing src/entt/entt.hpp)")
    if(NOT ANIM_ENTT_DIR AND EXISTS "E:/Learn/entt/src/entt/entt.hpp")
        set(ANIM_ENTT_DIR "E:/Learn/entt")
    endif()
    find_path(ANIM_ENTT_INCLUDE_DIR entt/entt.hpp
        HINTS "${ANIM_ENTT_DIR}/src" "${ANIM_ENTT_DIR}/single_include")
    if(NOT ANIM_ENTT_INCLUDE_DIR)
        message(FATAL_ERROR "ANIM_ENABLE_ENTT=ON but entt/entt.hpp was not found; set ANIM_ENTT_DIR")
    endif()

    add_library(anim_entt INTERFACE)
    add_library(anim::entt ALIAS anim_entt)
    target_link_libraries(anim_entt INTERFACE anim::anim)
    target_include_directories(anim_entt INTERFACE "${ANIM_ENTT_INCLUDE_DIR}")
    target_compile_definitions(anim_entt INTERFACE ANIM_ENABLE_ENTT=1)
    message(STATUS "Using EnTT from ${ANIM_ENTT_INCLUDE_DIR}")
endif()

# ---------------------------------------------------------------------------
# Samples (P7). Engine/renderer dependencies are isolated here.
# ---------------------------------------------------------------------------
option(ANIM_BUILD_SAMPLES "Build engine integration samples (bgfx demo)" OFF)
if(ANIM_BUILD_SAMPLES)
    add_subdirectory(samples/bgfx_demo)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(ANIM_BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)
endif()
