add_library(anim_validate_core STATIC
    Report.cpp
    Validator.cpp
)

target_include_directories(anim_validate_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(anim_validate_core PUBLIC anim_converter_core)

if(ANIM_BUILD_TOOLS)
    add_executable(anim_validate main.cpp)
    target_link_libraries(anim_validate PRIVATE anim_validate_core)
endif()
