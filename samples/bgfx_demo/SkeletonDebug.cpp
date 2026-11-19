#include "SkeletonDebug.h"

namespace demo {

uint32_t packDebugColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

void appendSkeletonLines(std::vector<DebugVertex>& vertices,
                         const anim::Skeleton& skeleton,
                         const std::vector<anim::Mat4>& world, uint32_t color) {
    if (world.size() < static_cast<std::size_t>(skeleton.boneCount())) return;
    const uint8_t r = static_cast<uint8_t>(color & 0xff);
    const uint8_t g = static_cast<uint8_t>((color >> 8) & 0xff);
    const uint8_t b = static_cast<uint8_t>((color >> 16) & 0xff);
    const uint8_t a = static_cast<uint8_t>((color >> 24) & 0xff);

    for (int i = 0; i < skeleton.boneCount(); ++i) {
        const int parent = skeleton.bone(i).parent;
        if (parent < 0) continue;

        const anim::Vec3 from(
            world[static_cast<std::size_t>(parent)][3]);
        const anim::Vec3 to(world[static_cast<std::size_t>(i)][3]);
        for (const anim::Vec3& point : {from, to}) {
            DebugVertex vertex{};
            vertex.position[0] = point.x;
            vertex.position[1] = point.y;
            vertex.position[2] = point.z;
            vertex.color[0] = r;
            vertex.color[1] = g;
            vertex.color[2] = b;
            vertex.color[3] = a;
            vertices.push_back(vertex);
        }
    }
}

}  // namespace demo
