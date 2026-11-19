#include "anim/skeleton/Blend.h"

#include <cmath>

#include "anim/core/Assert.h"

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define ANIM_BLEND_SSE2 1
#else
#define ANIM_BLEND_SSE2 0
#endif

namespace anim {
namespace {

#if ANIM_BLEND_SSE2

static_assert(alignof(Transform) >= 16, "Transform must be SIMD aligned");
static_assert(sizeof(Transform) == 48, "Transform must be three float4s");

inline __m128 loadTranslation(const Transform& transform) {
    return _mm_load_ps(reinterpret_cast<const float*>(&transform.translation));
}

inline __m128 loadRotation(const Transform& transform) {
    return _mm_load_ps(reinterpret_cast<const float*>(&transform.rotation));
}

inline __m128 loadScale(const Transform& transform) {
    return _mm_load_ps(reinterpret_cast<const float*>(&transform.scale));
}

inline void storeTranslation(Transform& transform, __m128 value) {
    _mm_store_ps(reinterpret_cast<float*>(&transform.translation), value);
}

inline void storeRotation(Transform& transform, __m128 value) {
    _mm_store_ps(reinterpret_cast<float*>(&transform.rotation), value);
}

inline void storeScale(Transform& transform, __m128 value) {
    _mm_store_ps(reinterpret_cast<float*>(&transform.scale), value);
}

inline __m128 dot4(__m128 a, __m128 b) {
    const __m128 product = _mm_mul_ps(a, b);
    const __m128 pairs =
        _mm_add_ps(product, _mm_shuffle_ps(product, product, _MM_SHUFFLE(1, 0, 3, 2)));
    return _mm_add_ps(pairs, _mm_shuffle_ps(pairs, pairs, _MM_SHUFFLE(2, 3, 0, 1)));
}

inline __m128 lengthSquared4(__m128 value) { return dot4(value, value); }

inline __m128 normalize4(__m128 value) {
    const __m128 lengthSquared = lengthSquared4(value);
    if (!(_mm_cvtss_f32(lengthSquared) > kEpsilon)) {
        return _mm_set_ps(0.0f, 0.0f, 0.0f, 1.0f);
    }
    return _mm_mul_ps(value, _mm_div_ps(_mm_set1_ps(1.0f), _mm_sqrt_ps(lengthSquared)));
}

inline __m128 negate4(__m128 value) {
    return _mm_xor_ps(value, _mm_set1_ps(-0.0f));
}

#endif

void clearPose(Pose& pose) {
#if ANIM_BLEND_SSE2
    const __m128 zero = _mm_setzero_ps();
    for (Transform& transform : pose.local) {
        storeTranslation(transform, zero);
        storeRotation(transform, zero);
        storeScale(transform, zero);
    }
#else
    for (Transform& transform : pose.local) {
        transform.translation = Vec3(0.0f);
        transform.rotation = Quat(0.0f, 0.0f, 0.0f, 0.0f);
        transform.scale = Vec3(0.0f);
    }
#endif
}

}  // namespace

float totalWeight(Span<const float> weights) {
    float total = 0.0f;
    for (float weight : weights) total += weight;
    return total;
}

void normalizeWeights(Span<float> weights) {
    float total = totalWeight(weights);
    // 对齐 ManualMixerState.NormalizeWeights：total == 1 时直接返回。
    if (total == 1.0f) return;
    if (!(total > 0.0f)) return;

    const float inverse = 1.0f / total;
    for (float& weight : weights) weight *= inverse;
}

void lerpPose(const Pose& a, const Pose& b, float t, Pose& dst) {
    ANIM_CHECK_MSG(a.local.size() == b.local.size(), "pose size mismatch");

    dst.local.resize(a.local.size());
#if ANIM_BLEND_SSE2
    const __m128 weight = _mm_set1_ps(t);
    for (std::size_t i = 0; i < a.local.size(); ++i) {
        const Transform& from = a.local[i];
        const Transform& to = b.local[i];
        Transform& result = dst.local[i];

        storeTranslation(result, _mm_add_ps(loadTranslation(from),
                                            _mm_mul_ps(_mm_sub_ps(loadTranslation(to),
                                                                  loadTranslation(from)),
                                                       weight)));
        storeScale(result, _mm_add_ps(loadScale(from),
                                      _mm_mul_ps(_mm_sub_ps(loadScale(to), loadScale(from)),
                                                 weight)));
        result.rotation = safeNormalize(
            glm::slerp(safeNormalize(from.rotation), safeNormalize(to.rotation), t));
    }
#else
    for (std::size_t i = 0; i < a.local.size(); ++i) {
        const Transform& from = a.local[i];
        const Transform& to = b.local[i];
        Transform& result = dst.local[i];

        result.translation = from.translation + (to.translation - from.translation) * t;
        result.scale = from.scale + (to.scale - from.scale) * t;
        result.rotation = safeNormalize(
            glm::slerp(safeNormalize(from.rotation), safeNormalize(to.rotation), t));
    }
#endif
}

void resetAccumulator(const Skeleton& skeleton, Pose& accumulator) {
    accumulator.local.resize(skeleton.bones.size());
    clearPose(accumulator);
}

void accumulatePose(Pose& acc, const Pose& src, float weight, const Pose* reference) {
    ANIM_CHECK_MSG(acc.local.size() == src.local.size(), "pose size mismatch");
    ANIM_CHECK_MSG(std::isfinite(weight), "weight must be finite");
    ANIM_CHECK_MSG(weight >= 0.0f, "weight must be non-negative");
    if (weight == 0.0f) return;

#if ANIM_BLEND_SSE2
    const __m128 scale = _mm_set1_ps(weight);
    for (std::size_t i = 0; i < acc.local.size(); ++i) {
        const Transform& source = src.local[i];
        Transform& target = acc.local[i];

        storeTranslation(target, _mm_add_ps(loadTranslation(target),
                                            _mm_mul_ps(loadTranslation(source), scale)));
        storeScale(target, _mm_add_ps(loadScale(target), _mm_mul_ps(loadScale(source), scale)));

        __m128 rotation = normalize4(loadRotation(source));
        if (reference != nullptr) {
            const __m128 ref = normalize4(loadRotation(reference->local[i]));
            if (_mm_cvtss_f32(dot4(rotation, ref)) < 0.0f) rotation = negate4(rotation);
        }
        storeRotation(target, _mm_add_ps(loadRotation(target), _mm_mul_ps(rotation, scale)));
    }
#else
    for (std::size_t i = 0; i < acc.local.size(); ++i) {
        const Transform& source = src.local[i];
        Transform& target = acc.local[i];

        target.translation += source.translation * weight;
        target.scale += source.scale * weight;

        Quat rotation = safeNormalize(source.rotation);
        if (reference != nullptr) {
            const Quat ref = safeNormalize(reference->local[i].rotation);
            if (glm::dot(rotation, ref) < 0.0f) rotation = -rotation;
        }
        target.rotation = target.rotation + rotation * weight;
    }
#endif
}

void normalizePose(Pose& accumulator) {
#if ANIM_BLEND_SSE2
    for (Transform& transform : accumulator.local) {
        storeRotation(transform, normalize4(loadRotation(transform)));
    }
#else
    for (Transform& transform : accumulator.local) {
        transform.rotation = safeNormalize(transform.rotation);
    }
#endif
}

void blendPoses(Span<const Pose*> poses, Span<const float> weights, Pose& out) {
    ANIM_CHECK_MSG(poses.size() == weights.size(), "poses/weights size mismatch");
    if (poses.empty()) {
        out.clear();
        return;
    }

    // 权重最大的姿态作为半球参考，避免累加时 q 与 -q 互相抵消。
    std::size_t referenceIndex = 0;
    for (std::size_t i = 1; i < poses.size(); ++i) {
        if (weights[i] > weights[referenceIndex]) referenceIndex = i;
    }

    ANIM_CHECK_MSG(poses[referenceIndex] != nullptr, "reference pose is null");
    const Pose* reference = poses[referenceIndex];

    std::size_t activeCount = 0;
    const Pose* single = nullptr;
    float singleWeight = 0.0f;
    for (std::size_t i = 0; i < poses.size(); ++i) {
        if (poses[i] == nullptr || weights[i] == 0.0f) continue;
        ANIM_CHECK_MSG(std::isfinite(weights[i]), "weight must be finite");
        ANIM_CHECK_MSG(weights[i] >= 0.0f, "weight must be non-negative");
        ++activeCount;
        single = poses[i];
        singleWeight = weights[i];
    }
    if (activeCount == 1 && singleWeight == 1.0f) {
        out = *single;
        normalizePose(out);
        return;
    }

    out.local.resize(reference->local.size());
    clearPose(out);
    for (std::size_t i = 0; i < poses.size(); ++i) {
        if (poses[i] == nullptr || weights[i] == 0.0f) continue;
        accumulatePose(out, *poses[i], weights[i], reference);
    }
    normalizePose(out);
}

}  // namespace anim
