#include "SkeletonWriter.h"

#include <sstream>

#include "JsonWriter.h"

namespace anim::pack {

bool writeSkeletonFile(const ImportedAsset& asset, const std::string& path,
                       std::string& error) {
    if (asset.bones.empty()) {
        error = "cannot write an empty skeleton";
        return false;
    }

    std::ostringstream out;
    out << "{\n  \"version\": 1,\n  \"bones\": [\n";
    for (std::size_t i = 0; i < asset.bones.size(); ++i) {
        const ImportedBone& bone = asset.bones[i];
        out << "    {\n";
        out << "      \"name\": \"" << jsonEscape(bone.name) << "\",\n";
        out << "      \"parent\": " << bone.parent << ",\n";
        out << "      \"translation\": [" << jsonNumber(bone.restLocal.translation.x)
            << ", " << jsonNumber(bone.restLocal.translation.y) << ", "
            << jsonNumber(bone.restLocal.translation.z) << "],\n";
        out << "      \"rotation\": [" << jsonNumber(bone.restLocal.rotation.x) << ", "
            << jsonNumber(bone.restLocal.rotation.y) << ", "
            << jsonNumber(bone.restLocal.rotation.z) << ", "
            << jsonNumber(bone.restLocal.rotation.w) << "],\n";
        out << "      \"scale\": [" << jsonNumber(bone.restLocal.scale.x) << ", "
            << jsonNumber(bone.restLocal.scale.y) << ", "
            << jsonNumber(bone.restLocal.scale.z) << "],\n";
        out << "      \"inverseBind\": [";
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (column != 0 || row != 0) out << ", ";
                out << jsonNumber(bone.inverseBind[column][row]);
            }
        }
        out << "]\n";
        out << "    }";
        if (i + 1 < asset.bones.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";

    return writeTextFile(path, out.str(), error);
}

}  // namespace anim::pack
