#include "anim/core/Transform.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "anim/core/Assert.h"

namespace anim {

Mat4 Transform::toMatrix() const {
    Mat4 m = glm::mat4_cast(rotation);
    m[0] *= scale.x;
    m[1] *= scale.y;
    m[2] *= scale.z;
    m[3] = Vec4(translation, 1.0f);
    return m;
}

Transform Transform::fromMatrix(const Mat4& matrix) {
    Transform result;

    result.translation = Vec3(matrix[3]);

    Mat3 basis(matrix);
    Vec3 c0 = basis[0];
    Vec3 c1 = basis[1];
    Vec3 c2 = basis[2];

    float sx = glm::length(c0);
    float sy = glm::length(c1);
    float sz = glm::length(c2);

    // 负缩放通常表现为某一列为负；先用行列式判断再翻转 X。
    if (glm::determinant(basis) < 0.0f) sx = -sx;

    result.scale = Vec3(sx, sy, sz);

    const float invX = (std::abs(sx) > kEpsilon) ? 1.0f / sx : 0.0f;
    const float invY = (std::abs(sy) > kEpsilon) ? 1.0f / sy : 0.0f;
    const float invZ = (std::abs(sz) > kEpsilon) ? 1.0f / sz : 0.0f;

    Mat3 rotationBasis;
    rotationBasis[0] = c0 * invX;
    rotationBasis[1] = c1 * invY;
    rotationBasis[2] = c2 * invZ;

    result.rotation = safeNormalize(glm::quat_cast(rotationBasis));
    return result;
}

Transform Transform::compose(const Transform& parent, const Transform& local) {
    Transform result;
    result.translation = parent.translation + parent.rotation * (parent.scale * local.translation);
    result.rotation = safeNormalize(parent.rotation * local.rotation);
    result.scale = parent.scale * local.scale;
    return result;
}

const Transform& Transform::identity() noexcept {
    static const Transform kIdentity{};
    return kIdentity;
}

}  // namespace anim
