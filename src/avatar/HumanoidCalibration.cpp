#include "anim/avatar/HumanoidCalibration.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "anim/core/Assert.h"

namespace anim {
namespace {

constexpr float kAxisEpsilon = 1e-5f;

struct AxisFit {
    bool hasAxis = false;
    Vec3 axisSum{0.0f};
    int positiveCount = 0;
    int negativeCount = 0;
    double positiveLimitSum = 0.0;
    double negativeLimitSum = 0.0;
    MuscleLimit limit{};
};

Vec3 canonicalAxis(MuscleAxis axis) noexcept {
    switch (axis) {
        case MuscleAxis::X: return Vec3(1.0f, 0.0f, 0.0f);
        case MuscleAxis::Y: return Vec3(0.0f, 1.0f, 0.0f);
        case MuscleAxis::Z: return Vec3(0.0f, 0.0f, 1.0f);
    }
    return Vec3(1.0f, 0.0f, 0.0f);
}

Quat rotationBetween(const Vec3& from, const Vec3& to) {
    const Vec3 a = glm::normalize(from);
    const Vec3 b = glm::normalize(to);
    const float dot = std::clamp(glm::dot(a, b), -1.0f, 1.0f);
    if (dot > 1.0f - kAxisEpsilon) return kIdentityQuat;
    if (dot < -1.0f + kAxisEpsilon) {
        Vec3 axis = glm::cross(a, Vec3(1.0f, 0.0f, 0.0f));
        if (glm::dot(axis, axis) < kAxisEpsilon) {
            axis = glm::cross(a, Vec3(0.0f, 1.0f, 0.0f));
        }
        return glm::angleAxis(kPi, glm::normalize(axis));
    }
    const Vec3 axis = glm::normalize(glm::cross(a, b));
    return glm::angleAxis(std::acos(dot), axis);
}

Quat basisToQuat(const std::array<bool, 3>& has, const std::array<Vec3, 3>& raw,
                 std::string& error) {
    int count = 0;
    int only = -1;
    for (int i = 0; i < 3; ++i) {
        if (has[static_cast<std::size_t>(i)]) {
            ++count;
            only = i;
        }
    }
    if (count == 0) return kIdentityQuat;

    std::array<Vec3, 3> axis = raw;
    for (int i = 0; i < 3; ++i) {
        if (!has[static_cast<std::size_t>(i)]) continue;
        const float length = glm::length(axis[static_cast<std::size_t>(i)]);
        if (!(length > kAxisEpsilon)) {
            error = "degenerate muscle axis";
            return kIdentityQuat;
        }
        axis[static_cast<std::size_t>(i)] /= length;
    }

    if (count == 1) {
        return rotationBetween(canonicalAxis(static_cast<MuscleAxis>(only)),
                               axis[static_cast<std::size_t>(only)]);
    }

    Vec3 x(1.0f, 0.0f, 0.0f);
    Vec3 y(0.0f, 1.0f, 0.0f);
    Vec3 z(0.0f, 0.0f, 1.0f);

    if (has[0] && has[1]) {
        x = axis[0];
        y = axis[1] - x * glm::dot(x, axis[1]);
        if (glm::length(y) < kAxisEpsilon) {
            error = "degenerate X/Y muscle axes";
            return kIdentityQuat;
        }
        y = glm::normalize(y);
        z = glm::cross(x, y);
        if (count == 3 && glm::dot(z, axis[2]) < 0.0f) {
            error = "muscle axes are left-handed (X/Y/Z)";
            return kIdentityQuat;
        }
    } else if (has[1] && has[2]) {
        y = axis[1];
        z = axis[2] - y * glm::dot(y, axis[2]);
        if (glm::length(z) < kAxisEpsilon) {
            error = "degenerate Y/Z muscle axes";
            return kIdentityQuat;
        }
        z = glm::normalize(z);
        x = glm::cross(y, z);
        if (count == 3 && glm::dot(x, axis[0]) < 0.0f) {
            error = "muscle axes are left-handed (Y/Z/X)";
            return kIdentityQuat;
        }
    } else {
        z = axis[2];
        x = axis[0] - z * glm::dot(z, axis[0]);
        if (glm::length(x) < kAxisEpsilon) {
            error = "degenerate Z/X muscle axes";
            return kIdentityQuat;
        }
        x = glm::normalize(x);
        y = glm::cross(z, x);
        if (count == 3 && glm::dot(y, axis[1]) < 0.0f) {
            error = "muscle axes are left-handed (Z/X/Y)";
            return kIdentityQuat;
        }
    }

    return glm::normalize(glm::quat_cast(Mat3(x, y, z)));
}

}  // namespace

bool calibrateHumanoid(const HumanoidCalibrationData& data,
                       HumanoidCalibrationReport& report, std::string& error,
                       float toleranceDegrees) {
    report = HumanoidCalibrationReport{};
    report.preRot.fill(kIdentityQuat);
    report.postRot.fill(kIdentityQuat);
    for (int i = 0; i < kMuscleCount; ++i) {
        report.limits[static_cast<std::size_t>(i)] =
            data.hasDefaultLimit[static_cast<std::size_t>(i)]
                ? data.defaultLimits[static_cast<std::size_t>(i)]
                : muscleEntry(i).defaultLimit;
    }

    std::array<std::array<AxisFit, 3>, kHumanBoneCount> fits{};
    for (int i = 0; i < kMuscleCount; ++i) {
        fits[static_cast<std::size_t>(humanBoneIndex(muscleEntry(i).bone))]
            [static_cast<std::size_t>(muscleEntry(i).axis)]
                .limit = report.limits[static_cast<std::size_t>(i)];
    }

    if (data.samples.empty()) {
        error = "calibration data contains no muscle samples";
        return false;
    }

    for (const MuscleCalibrationSample& sample : data.samples) {
        if (sample.muscleId < 0 || sample.muscleId >= kMuscleCount) {
            error = "calibration sample has invalid muscle id";
            return false;
        }
        if (!std::isfinite(sample.value) || sample.value == 0.0f) {
            continue;
        }

        const MuscleEntry& entry = muscleEntry(sample.muscleId);
        const int bone = humanBoneIndex(entry.bone);
        if (!data.hasRest[static_cast<std::size_t>(bone)]) {
            std::ostringstream stream;
            stream << "missing rest rotation for bone " << humanBoneName(entry.bone);
            error = stream.str();
            return false;
        }

        const Quat rest = safeNormalize(data.restRotation[static_cast<std::size_t>(bone)]);
        const Quat local = safeNormalize(sample.localRotation);
        Quat delta = safeNormalize(glm::inverse(rest) * local);
        if (delta.w < 0.0f) delta = -delta;

        const Vec3 imaginary(delta.x, delta.y, delta.z);
        const float sinHalf = glm::length(imaginary);
        if (sinHalf < kAxisEpsilon) {
            std::ostringstream stream;
            stream << "sample for " << entry.name << " has no rotation";
            error = stream.str();
            return false;
        }

        const float angle = degrees(2.0f * std::atan2(sinHalf, delta.w));
        const Vec3 axis = imaginary / sinHalf;

        AxisFit& fit = fits[static_cast<std::size_t>(bone)]
                            [static_cast<std::size_t>(entry.axis)];
        fit.hasAxis = true;
        if (sample.value > 0.0f) {
            fit.axisSum += axis;
            ++fit.positiveCount;
            fit.positiveLimitSum += static_cast<double>(angle) / sample.value;
        } else {
            // 负值四元数的最短表示轴会反向；翻回来得到 pre*canonical。
            fit.axisSum -= axis;
            ++fit.negativeCount;
            fit.negativeLimitSum += static_cast<double>(angle) / (-sample.value);
        }
        report.calibratedMuscles[static_cast<std::size_t>(sample.muscleId)] = true;
    }

    for (int bone = 0; bone < kHumanBoneCount; ++bone) {
        auto& boneFits = fits[static_cast<std::size_t>(bone)];
        std::array<bool, 3> has{};
        std::array<Vec3, 3> axes{};
        bool any = false;
        for (int axis = 0; axis < 3; ++axis) {
            AxisFit& fit = boneFits[static_cast<std::size_t>(axis)];
            if (!fit.hasAxis) continue;
            const float length = glm::length(fit.axisSum);
            if (!(length > kAxisEpsilon)) {
                error = "calibration samples disagree on muscle axis";
                return false;
            }
            fit.axisSum /= length;
            has[static_cast<std::size_t>(axis)] = true;
            axes[static_cast<std::size_t>(axis)] = fit.axisSum;
            any = true;

            if (fit.positiveCount > 0) {
                fit.limit.maxDegrees =
                    static_cast<float>(fit.positiveLimitSum / fit.positiveCount);
            }
            if (fit.negativeCount > 0) {
                fit.limit.minDegrees =
                    -static_cast<float>(fit.negativeLimitSum / fit.negativeCount);
            }
            if (fit.limit.minDegrees > fit.limit.maxDegrees) {
                std::ostringstream stream;
                stream << "invalid calibrated limits for bone " << bone;
                error = stream.str();
                return false;
            }
        }
        if (!any) continue;

        std::string basisError;
        const Quat pre = basisToQuat(has, axes, basisError);
        if (!basisError.empty()) {
            error = basisError;
            return false;
        }
        report.preRot[static_cast<std::size_t>(bone)] = pre;
        report.postRot[static_cast<std::size_t>(bone)] =
            glm::normalize(glm::conjugate(pre));
        report.calibratedBones[static_cast<std::size_t>(bone)] = true;

        for (int axis = 0; axis < 3; ++axis) {
            AxisFit& fit = boneFits[static_cast<std::size_t>(axis)];
            if (fit.hasAxis) {
                for (int id = 0; id < kMuscleCount; ++id) {
                    const MuscleEntry& entry = muscleEntry(id);
                    if (humanBoneIndex(entry.bone) == bone &&
                        static_cast<int>(entry.axis) == axis) {
                        report.limits[static_cast<std::size_t>(id)] = fit.limit;
                    }
                }
            }
        }
    }

    bool anyMuscle = false;
    for (bool value : report.calibratedMuscles) anyMuscle = anyMuscle || value;
    if (!anyMuscle) {
        error = "calibration data produced no usable muscle";
        return false;
    }

    float maxError = 0.0f;
    for (const MuscleCalibrationSample& sample : data.samples) {
        if (sample.muscleId < 0 || sample.muscleId >= kMuscleCount ||
            !report.calibratedMuscles[static_cast<std::size_t>(sample.muscleId)]) {
            continue;
        }
        const MuscleEntry& entry = muscleEntry(sample.muscleId);
        const int bone = humanBoneIndex(entry.bone);
        const MuscleLimit limit = report.limits[static_cast<std::size_t>(sample.muscleId)];
        const float angle = sample.value >= 0.0f ? sample.value * limit.maxDegrees
                                                 : sample.value * (-limit.minDegrees);
        const Quat predicted = safeNormalize(
            safeNormalize(data.restRotation[static_cast<std::size_t>(bone)]) *
            report.preRot[static_cast<std::size_t>(bone)] *
            glm::angleAxis(radians(angle), canonicalAxis(entry.axis)) *
            report.postRot[static_cast<std::size_t>(bone)]);
        const Quat relative =
            safeNormalize(glm::conjugate(predicted) * safeNormalize(sample.localRotation));
        const float sinHalf = glm::length(Vec3(relative.x, relative.y, relative.z));
        const float errorDegrees =
            degrees(2.0f * std::atan2(sinHalf, std::abs(relative.w)));
        maxError = std::max(maxError, errorDegrees);
        if (errorDegrees > toleranceDegrees) {
            std::ostringstream stream;
            stream << "muscle " << entry.name << " residual " << errorDegrees
                   << " deg exceeds tolerance " << toleranceDegrees << " deg";
            error = stream.str();
            return false;
        }
    }
    report.maxErrorDegrees = maxError;
    return true;
}

void applyHumanoidCalibration(HumanoidAvatar& avatar,
                              const HumanoidCalibrationReport& report) {
    for (int bone = 0; bone < kHumanBoneCount; ++bone) {
        if (!report.calibratedBones[static_cast<std::size_t>(bone)]) continue;
        avatar.preRot[static_cast<std::size_t>(bone)] =
            report.preRot[static_cast<std::size_t>(bone)];
        avatar.postRot[static_cast<std::size_t>(bone)] =
            report.postRot[static_cast<std::size_t>(bone)];
    }
    for (int muscle = 0; muscle < kMuscleCount; ++muscle) {
        if (!report.calibratedMuscles[static_cast<std::size_t>(muscle)]) continue;
        avatar.limits[static_cast<std::size_t>(muscle)] =
            report.limits[static_cast<std::size_t>(muscle)];
    }
}

}  // namespace anim
