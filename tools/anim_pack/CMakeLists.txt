add_library(anim_pack_core STATIC
    AnimPack.cpp
    ClipBuilder.cpp
    EventSidecar.cpp
    GltfImporter.cpp
    JsonWriter.cpp
    ManifestWriter.cpp
    SkeletonWriter.cpp
    third_party/cgltf_impl.cpp
)

target_include_directories(anim_pack_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_include_directories(anim_pack_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)
target_link_libraries(anim_pack_core PUBLIC anim::anim)

if(MSVC)
    set_source_files_properties(third_party/cgltf_impl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties(third_party/cgltf_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

add_executable(anim_pack main.cpp)
target_link_libraries(anim_pack PRIVATE anim_pack_core)

add_executable(anim_pack_demo ../../samples/anim_pack_demo/main.cpp)
target_link_libraries(anim_pack_demo PRIVATE anim::anim anim_converter_core)
