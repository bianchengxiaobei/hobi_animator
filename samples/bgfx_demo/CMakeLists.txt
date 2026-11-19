cmake_minimum_required(VERSION 3.20)

# bgfx demo（P7）。核心库不依赖 bgfx/assimp；只有这个目标引用它们。
#
# 配置示例：
#   cmake -S . -B build -DANIM_BUILD_SAMPLES=ON \
#         -DANIM_BGFX_ROOT=E:/Learn/bgfx -DANIM_ASSIMP_ROOT=E:/Learn/assimp

if(NOT WIN32)
    message(FATAL_ERROR "bgfx_demo currently only wires the Win32 window backend")
endif()

set(ANIM_BGFX_ROOT "" CACHE PATH "bgfx source root (contains bgfx/, bx/, bimg/)")
set(ANIM_ASSIMP_ROOT "" CACHE PATH "assimp source/build root")
set(ANIM_SHADERC "" CACHE FILEPATH "shaderc executable")
set(ANIM_BGFX_LIB_DIR "" CACHE PATH "directory containing bgfxDebug.lib/bxDebug.lib")
set(ANIM_ASSIMP_LIB "" CACHE FILEPATH "assimp import library")
set(ANIM_ASSIMP_DLL "" CACHE FILEPATH "assimp runtime DLL")

if(NOT EXISTS "${ANIM_BGFX_ROOT}/bgfx/include/bgfx/bgfx.h")
    message(FATAL_ERROR "ANIM_BGFX_ROOT='${ANIM_BGFX_ROOT}' does not contain bgfx/include/bgfx/bgfx.h")
endif()
if(NOT EXISTS "${ANIM_ASSIMP_ROOT}/include/assimp/scene.h")
    message(FATAL_ERROR "ANIM_ASSIMP_ROOT='${ANIM_ASSIMP_ROOT}' does not contain include/assimp/scene.h")
endif()

if(NOT ANIM_SHADERC)
    set(ANIM_SHADERC "${ANIM_BGFX_ROOT}/bgfx/.build/win64_vs2022/bin/shadercDebug.exe")
endif()
if(NOT ANIM_BGFX_LIB_DIR)
    set(ANIM_BGFX_LIB_DIR "${ANIM_BGFX_ROOT}/bgfx/.build/win64_vs2022/bin")
endif()
if(NOT ANIM_ASSIMP_LIB)
    set(ANIM_ASSIMP_LIB "${ANIM_ASSIMP_ROOT}/lib/MinSizeRel/assimp-vc143-mt.lib")
endif()
if(NOT ANIM_ASSIMP_DLL)
    set(ANIM_ASSIMP_DLL "${ANIM_ASSIMP_ROOT}/bin/MinSizeRel/assimp-vc143-mt.dll")
endif()

if(NOT EXISTS "${ANIM_SHADERC}")
    message(FATAL_ERROR "shaderc not found: ${ANIM_SHADERC}")
endif()

set(SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(BGFX_SRC_DIR "${ANIM_BGFX_ROOT}/bgfx/src")

add_custom_command(
    OUTPUT "${SHADER_DIR}/vs_skin.bin" "${SHADER_DIR}/fs_skin.bin"
           "${SHADER_DIR}/vs_debug.bin" "${SHADER_DIR}/fs_debug.bin"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_DIR}"
    COMMAND "${ANIM_SHADERC}" --platform windows -p s_5_0 -O 3 --type vertex
            --varyingdef "${SHADER_SRC_DIR}/varying.def.sc" -i "${BGFX_SRC_DIR}"
            -o "${SHADER_DIR}/vs_skin.bin" -f "${SHADER_SRC_DIR}/vs_skin.sc"
    COMMAND "${ANIM_SHADERC}" --platform windows -p s_5_0 -O 3 --type fragment
            --varyingdef "${SHADER_SRC_DIR}/varying.def.sc" -i "${BGFX_SRC_DIR}"
            -o "${SHADER_DIR}/fs_skin.bin" -f "${SHADER_SRC_DIR}/fs_skin.sc"
    COMMAND "${ANIM_SHADERC}" --platform windows -p s_5_0 -O 3 --type vertex
            --varyingdef "${SHADER_SRC_DIR}/varying_debug.def.sc" -i "${BGFX_SRC_DIR}"
            -o "${SHADER_DIR}/vs_debug.bin" -f "${SHADER_SRC_DIR}/vs_debug.sc"
    COMMAND "${ANIM_SHADERC}" --platform windows -p s_5_0 -O 3 --type fragment
            --varyingdef "${SHADER_SRC_DIR}/varying_debug.def.sc" -i "${BGFX_SRC_DIR}"
            -o "${SHADER_DIR}/fs_debug.bin" -f "${SHADER_SRC_DIR}/fs_debug.sc"
    DEPENDS "${SHADER_SRC_DIR}/vs_skin.sc" "${SHADER_SRC_DIR}/fs_skin.sc"
            "${SHADER_SRC_DIR}/varying.def.sc"
            "${SHADER_SRC_DIR}/vs_debug.sc" "${SHADER_SRC_DIR}/fs_debug.sc"
            "${SHADER_SRC_DIR}/varying_debug.def.sc"
    COMMENT "Compiling bgfx skinning and debug shaders")

add_custom_target(anim_bgfx_shaders DEPENDS
    "${SHADER_DIR}/vs_skin.bin" "${SHADER_DIR}/fs_skin.bin"
    "${SHADER_DIR}/vs_debug.bin" "${SHADER_DIR}/fs_debug.bin")

add_executable(bgfx_demo
    main.cpp
    FbxLoader.cpp
    Input.cpp
    SkeletonDebug.cpp
)

add_executable(humanoid_retarget_demo
    retarget_main.cpp
)

target_include_directories(bgfx_demo PRIVATE
    "${ANIM_BGFX_ROOT}/bgfx/include"
    "${ANIM_BGFX_ROOT}/bx/include"
    "${ANIM_BGFX_ROOT}/bimg/include"
    "${ANIM_ASSIMP_ROOT}/include"
    "${CMAKE_SOURCE_DIR}/tools/anim_converter"
)

target_include_directories(humanoid_retarget_demo PRIVATE
    "${ANIM_BGFX_ROOT}/bgfx/include"
    "${ANIM_BGFX_ROOT}/bx/include"
    "${ANIM_BGFX_ROOT}/bimg/include"
    "${CMAKE_SOURCE_DIR}/tools/anim_converter"
)

target_compile_definitions(bgfx_demo PRIVATE
    ANIM_ASSETS_DIR="${CMAKE_SOURCE_DIR}/../Assets"
    ANIM_RETARGET_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/golden"
    $<$<CONFIG:Debug>:BX_CONFIG_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:BX_CONFIG_DEBUG=0>)

target_compile_definitions(humanoid_retarget_demo PRIVATE
    ANIM_RETARGET_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/golden"
    $<$<CONFIG:Debug>:BX_CONFIG_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:BX_CONFIG_DEBUG=0>)

if(MSVC)
    target_compile_options(bgfx_demo PRIVATE /Zc:preprocessor)
    target_compile_options(humanoid_retarget_demo PRIVATE /Zc:preprocessor)
endif()

if(ANIM_BUILD_DEBUG_TOOLS)
    target_sources(bgfx_demo PRIVATE DebugOverlay.cpp)
    target_link_libraries(bgfx_demo PRIVATE anim::debug)
    target_compile_definitions(bgfx_demo PRIVATE ANIM_DEBUG_TOOLS=1)
else()
    target_compile_definitions(bgfx_demo PRIVATE ANIM_DEBUG_TOOLS=0)
endif()

target_link_libraries(bgfx_demo PRIVATE
    anim::anim
    anim_converter_core
    "${ANIM_BGFX_LIB_DIR}/$<IF:$<CONFIG:Debug>,bgfxDebug,bgfxRelease>.lib"
    "${ANIM_BGFX_LIB_DIR}/$<IF:$<CONFIG:Debug>,bxDebug,bxRelease>.lib"
    "${ANIM_BGFX_LIB_DIR}/$<IF:$<CONFIG:Debug>,bimgDebug,bimgRelease>.lib"
    "${ANIM_ASSIMP_LIB}"
    gdi32 psapi user32
)

target_link_libraries(humanoid_retarget_demo PRIVATE
    anim::anim
    anim_converter_core
    "${ANIM_BGFX_LIB_DIR}/$<IF:$<CONFIG:Debug>,bgfxDebug,bgfxRelease>.lib"
    "${ANIM_BGFX_LIB_DIR}/$<IF:$<CONFIG:Debug>,bxDebug,bxRelease>.lib"
    "${ANIM_BGFX_LIB_DIR}/$<IF:$<CONFIG:Debug>,bimgDebug,bimgRelease>.lib"
    gdi32 psapi user32
)

add_dependencies(bgfx_demo anim_bgfx_shaders)
add_dependencies(humanoid_retarget_demo anim_bgfx_shaders)

add_custom_command(TARGET bgfx_demo POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ANIM_ASSIMP_DLL}"
            "$<TARGET_FILE_DIR:bgfx_demo>"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:bgfx_demo>/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SHADER_DIR}/vs_skin.bin"
            "${SHADER_DIR}/fs_skin.bin" "${SHADER_DIR}/vs_debug.bin"
            "${SHADER_DIR}/fs_debug.bin" "$<TARGET_FILE_DIR:bgfx_demo>/shaders"
    COMMENT "Copying assimp DLL and shaders next to bgfx_demo")

add_custom_command(TARGET humanoid_retarget_demo POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:humanoid_retarget_demo>/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SHADER_DIR}/vs_debug.bin"
            "${SHADER_DIR}/fs_debug.bin" "$<TARGET_FILE_DIR:humanoid_retarget_demo>/shaders"
    COMMENT "Copying debug shaders next to humanoid_retarget_demo")
