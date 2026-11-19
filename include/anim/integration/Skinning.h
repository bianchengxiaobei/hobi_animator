#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "anim/core/Math.h"

// 蒙皮调色板的引擎无关视图与上传适配。
//
// 文档：docs/12-引擎集成-bgfx.md §3
// 约束：本头文件不包含任何渲染 API；引擎侧实现 ISkinningUploader 后调用
// uploadSkinningPalette。bgfx 的 u_model / u_bones 期望行主序（row-major），
// 而 glm 默认列主序，因此提供 transposePalette。
namespace anim {

struct SkinningPalette {
    std::span<const Mat4> bones;
    std::size_t boneCount() const noexcept { return bones.size(); }
};

inline SkinningPalette skinningPalette(const std::vector<Mat4>& palette) noexcept {
    return SkinningPalette{std::span<const Mat4>(palette.data(), palette.size())};
}

// 上传目标（引擎侧实现；例如 bgfx::setUniform / 纹理写入）。
class ISkinningUploader {
public:
    virtual ~ISkinningUploader() = default;
    virtual void uploadSkinningPalette(const SkinningPalette& palette) = 0;
};

inline void uploadSkinningPalette(ISkinningUploader& uploader,
                                  const std::vector<Mat4>& palette) {
    uploader.uploadSkinningPalette(skinningPalette(palette));
}

// 列主序（glm）→ 行主序（bgfx/HLSL 的 mul(M, v)）。
void transposePalette(const std::vector<Mat4>& source, std::vector<Mat4>& destination);

}  // namespace anim
