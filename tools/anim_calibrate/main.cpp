#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "anim/integration/Json.h"
#include "anim/avatar/HumanBone.h"
#include "anim/avatar/HumanoidCalibration.h"
#include "anim/avatar/MuscleTable.h"
#include "anim/core/Math.h"

namespace {

using anim::HumanBone;
using anim::HumanoidCalibrationData;
using anim::HumanoidCalibrationReport;
using anim::MuscleCalibrationSample;
using anim::Quat;

struct Options {
    std::string golden;
    std::string output;
    std::string name;
    float tolerance = 0.1f;
};

void usage() {
    std::cout << "usage: anim_calibrate --golden HumanoidGolden.json --out avatar.json"
                 " [--name Name] [--tolerance 0.1]\n";
}

bool parseFloat(const anim::json::Value* value, float& out) {
    if (value == nullptr || !value->isNumber()) return false;
    out = static_cast<float>(value->number);
    return true;
}

bool parseQuat(const anim::json::Value* value, Quat& out) {
    if (value == nullptr || !value->isArray() || value->array.size() < 4) return false;
    for (int i = 0; i < 4; ++i) {
        if (!value->array[static_cast<std::size_t>(i)].isNumber()) return false;
    }
    out = anim::safeNormalize(anim::quatFromXYZW(
        static_cast<float>(value->array[0].number),
        static_cast<float>(value->array[1].number),
        static_cast<float>(value->array[2].number),
        static_cast<float>(value->array[3].number)));
    return true;
}

bool loadGolden(const std::string& path, const std::string& overrideName,
                HumanoidCalibrationData& data, std::string& name,
                std::array<bool, anim::kHumanBoneCount>& hasName,
                std::array<std::string, anim::kHumanBoneCount>& boneNames,
                float& hipsHeight, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();

    anim::json::Value root;
    if (!anim::json::parse(stream.str(), root, error)) return false;
    if (!root.isObject()) {
        error = "golden root must be an object";
        return false;
    }

    if (const anim::json::Value* version = root.find("version");
        version != nullptr && version->isNumber() && version->number != 1.0) {
        error = "unsupported golden version";
        return false;
    }

    if (!overrideName.empty()) {
        name = overrideName;
    } else if (const anim::json::Value* value = root.find("name");
               value != nullptr && value->isString()) {
        name = value->text;
    }

    if (const anim::json::Value* value = root.find("hipsHeight"); value != nullptr) {
        if (!parseFloat(value, hipsHeight)) {
            error = "hipsHeight must be a number";
            return false;
        }
    }

    if (const anim::json::Value* limits = root.find("defaultLimits");
        limits != nullptr && limits->isArray()) {
        for (const anim::json::Value& entry : limits->array) {
            if (!entry.isObject()) {
                error = "defaultLimits entry must be an object";
                return false;
            }
            const anim::json::Value* muscleValue = entry.find("muscle");
            const anim::json::Value* minValue = entry.find("min");
            const anim::json::Value* maxValue = entry.find("max");
            if (muscleValue == nullptr || !muscleValue->isNumber()) {
                error = "defaultLimits entry requires numeric 'muscle'";
                return false;
            }
            const int muscle = static_cast<int>(muscleValue->number);
            if (muscle < 0 || muscle >= anim::kMuscleCount) {
                error = "defaultLimits muscle out of range";
                return false;
            }
            anim::MuscleLimit limit;
            if (!parseFloat(minValue, limit.minDegrees) ||
                !parseFloat(maxValue, limit.maxDegrees)) {
                error = "defaultLimits entry requires numeric 'min' and 'max'";
                return false;
            }
            data.hasDefaultLimit[static_cast<std::size_t>(muscle)] = true;
            data.defaultLimits[static_cast<std::size_t>(muscle)] = limit;
        }
    }

    const anim::json::Value* rest = root.find("rest");
    if (rest == nullptr || !rest->isArray()) {
        error = "golden requires a 'rest' array";
        return false;
    }
    for (const anim::json::Value& entry : rest->array) {
        if (!entry.isObject()) {
            error = "rest entry must be an object";
            return false;
        }
        const anim::json::Value* boneValue = entry.find("humanBone");
        const anim::json::Value* rotation = entry.find("rotation");
        if (boneValue == nullptr || !boneValue->isNumber()) {
            error = "rest entry requires numeric 'humanBone'";
            return false;
        }
        const int bone = static_cast<int>(boneValue->number);
        if (bone < 0 || bone >= anim::kHumanBoneCount) {
            error = "rest humanBone out of range";
            return false;
        }
        Quat rotationValue;
        if (!parseQuat(rotation, rotationValue)) {
            error = "rest entry requires a 4-component 'rotation'";
            return false;
        }
        data.hasRest[static_cast<std::size_t>(bone)] = true;
        data.restRotation[static_cast<std::size_t>(bone)] = rotationValue;
        hasName[static_cast<std::size_t>(bone)] = true;
        if (const anim::json::Value* boneName = entry.find("name");
            boneName != nullptr && boneName->isString()) {
            boneNames[static_cast<std::size_t>(bone)] = boneName->text;
        } else {
            boneNames[static_cast<std::size_t>(bone)] =
                anim::humanBoneName(static_cast<HumanBone>(bone));
        }
    }

    const anim::json::Value* samples = root.find("samples");
    if (samples == nullptr || !samples->isArray()) {
        error = "golden requires a 'samples' array";
        return false;
    }
    for (const anim::json::Value& entry : samples->array) {
        if (!entry.isObject()) {
            error = "sample must be an object";
            return false;
        }
        const anim::json::Value* muscleValue = entry.find("muscle");
        const anim::json::Value* value = entry.find("value");
        const anim::json::Value* rotation = entry.find("rotation");
        if (muscleValue == nullptr || !muscleValue->isNumber()) {
            error = "sample requires numeric 'muscle'";
            return false;
        }
        const int muscle = static_cast<int>(muscleValue->number);
        if (muscle < 0 || muscle >= anim::kMuscleCount) {
            error = "sample muscle out of range";
            return false;
        }
        float sampleValue = 0.0f;
        Quat localRotation;
        if (!parseFloat(value, sampleValue) || !parseQuat(rotation, localRotation)) {
            error = "sample requires numeric 'value' and 4-component 'rotation'";
            return false;
        }
        data.samples.push_back(MuscleCalibrationSample{muscle, sampleValue, localRotation});
    }
    return true;
}

void writeNumber(std::ostream& out, float value) {
    out << std::setprecision(10) << value;
}

void writeQuat(std::ostream& out, const Quat& q) {
    out << '[' << std::setprecision(10) << q.x << ',' << q.y << ',' << q.z << ','
        << q.w << ']';
}

void writeString(std::ostream& out, const std::string& value) {
    out << '"';
    for (char c : value) {
        if (c == '\\' || c == '"') out << '\\';
        out << c;
    }
    out << '"';
}

bool writeAvatar(const Options& options, const std::string& name,
                 const std::array<bool, anim::kHumanBoneCount>& hasName,
                 const std::array<std::string, anim::kHumanBoneCount>& boneNames,
                 float hipsHeight, const HumanoidCalibrationReport& report,
                 const std::array<bool, anim::kMuscleCount>& hasDefaultLimit,
                 std::string& error) {
    std::filesystem::path output(options.output);
    if (output.has_parent_path()) {
        std::error_code code;
        std::filesystem::create_directories(output.parent_path(), code);
        if (code) {
            error = "cannot create output directory: " + output.parent_path().string();
            return false;
        }
    }

    std::ofstream out(options.output, std::ios::binary);
    if (!out) {
        error = "cannot open output " + options.output;
        return false;
    }

    out << "{\n  \"name\": ";
    writeString(out, name);
    out << ",\n  \"hipsHeight\": ";
    writeNumber(out, hipsHeight);
    out << ",\n  \"bones\": {";

    bool first = true;
    for (int bone = 0; bone < anim::kHumanBoneCount; ++bone) {
        if (!hasName[static_cast<std::size_t>(bone)]) continue;
        if (!first) out << ',';
        first = false;
        out << "\n    ";
        writeString(out, anim::humanBoneName(static_cast<HumanBone>(bone)));
        out << ": ";
        writeString(out, boneNames[static_cast<std::size_t>(bone)]);
    }
    out << "\n  },\n  \"preRot\": {";

    first = true;
    for (int bone = 0; bone < anim::kHumanBoneCount; ++bone) {
        if (!report.calibratedBones[static_cast<std::size_t>(bone)]) continue;
        if (!first) out << ',';
        first = false;
        out << "\n    ";
        writeString(out, anim::humanBoneName(static_cast<HumanBone>(bone)));
        out << ": ";
        writeQuat(out, report.preRot[static_cast<std::size_t>(bone)]);
    }
    out << "\n  },\n  \"postRot\": {";

    first = true;
    for (int bone = 0; bone < anim::kHumanBoneCount; ++bone) {
        if (!report.calibratedBones[static_cast<std::size_t>(bone)]) continue;
        if (!first) out << ',';
        first = false;
        out << "\n    ";
        writeString(out, anim::humanBoneName(static_cast<HumanBone>(bone)));
        out << ": ";
        writeQuat(out, report.postRot[static_cast<std::size_t>(bone)]);
    }
    out << "\n  },\n  \"limitsOverride\": {";

    first = true;
    for (int muscle = 0; muscle < anim::kMuscleCount; ++muscle) {
        if (!report.calibratedMuscles[static_cast<std::size_t>(muscle)] &&
            !hasDefaultLimit[static_cast<std::size_t>(muscle)]) {
            continue;
        }
        if (!first) out << ',';
        first = false;
        out << "\n    ";
        writeString(out, anim::muscleEntry(muscle).name);
        out << ": [";
        writeNumber(out, report.limits[static_cast<std::size_t>(muscle)].minDegrees);
        out << ", ";
        writeNumber(out, report.limits[static_cast<std::size_t>(muscle)].maxDegrees);
        out << ']';
    }
    out << "\n  },\n  \"calibrationMaxErrorDegrees\": ";
    writeNumber(out, report.maxErrorDegrees);
    out << "\n}\n";
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string& value) {
            if (i + 1 >= argc) return false;
            value = argv[++i];
            return true;
        };
        if (arg == "--golden") {
            if (!next(options.golden)) return 1;
        } else if (arg == "--out") {
            if (!next(options.output)) return 1;
        } else if (arg == "--name") {
            if (!next(options.name)) return 1;
        } else if (arg == "--tolerance") {
            std::string value;
            if (!next(value)) return 1;
            options.tolerance = std::stof(value);
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else {
            usage();
            return 1;
        }
    }
    if (options.golden.empty() || options.output.empty()) {
        usage();
        return 1;
    }

    HumanoidCalibrationData data;
    std::array<bool, anim::kHumanBoneCount> hasName{};
    std::array<std::string, anim::kHumanBoneCount> boneNames;
    float hipsHeight = 0.0f;
    std::string name;
    std::string error;
    if (!loadGolden(options.golden, options.name, data, name, hasName, boneNames,
                    hipsHeight, error)) {
        std::cerr << "anim_calibrate: " << error << '\n';
        return 1;
    }

    HumanoidCalibrationReport report;
    if (!anim::calibrateHumanoid(data, report, error, options.tolerance)) {
        std::cerr << "anim_calibrate: " << error << '\n';
        return 1;
    }
    if (!writeAvatar(options, name, hasName, boneNames, hipsHeight, report,
                     data.hasDefaultLimit, error)) {
        std::cerr << "anim_calibrate: " << error << '\n';
        return 1;
    }
    std::cout << "calibrated " << data.samples.size() << " samples, max residual "
              << report.maxErrorDegrees << " deg\n";
    return 0;
}
