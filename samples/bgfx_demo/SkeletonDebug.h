#pragma once

#include <cstdint>
#include <vector>

#include "anim/skeleton/Palette.h"

namespace demo {

struct DebugVertex {
    float position[3];
    uint8_t color[4];
};

uint32_t packDebugColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

void appendSkeletonLines(std::vector<DebugVertex>& vertices,
                         const anim::Skeleton& skeleton,
                         const std::vector<anim::Mat4>& world, uint32_t color);

}  // namespace demo
