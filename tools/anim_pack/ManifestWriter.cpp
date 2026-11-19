#include "ManifestWriter.h"

#include <cmath>
#include <sstream>

#include "JsonWriter.h"

namespace anim::pack {

bool writeManifestFile(const AnimationSet& manifest, const std::string& path,
                       std::string& error) {
    if (manifest.version != 1) {
        error = "animation manifest version must be 1";
        return false;
    }
    if (manifest.skeletonFile.empty()) {
        error = "animation manifest skeleton must not be empty";
        return false;
    }
    if (manifest.coordinate != "gltf" && manifest.coordinate != "mirrored") {
        error = "animation manifest coordinate must be 'gltf' or 'mirrored'";
        return false;
    }

    std::ostringstream out;
    out << "{\n  \"version\": 1,\n";
    out << "  \"skeleton\": \"" << jsonEscape(manifest.skeletonFile) << "\",\n";
    out << "  \"coordinate\": \"" << jsonEscape(manifest.coordinate) << "\",\n";
    out << "  \"clips\": [\n";
    for (std::size_t i = 0; i < manifest.clips.size(); ++i) {
        const ClipAssetInfo& clip = manifest.clips[i];
        out << "    {\n";
        out << "      \"name\": \"" << jsonEscape(clip.name) << "\",\n";
        out << "      \"action\": \"" << jsonEscape(clip.action) << "\",\n";
        out << "      \"file\": \"" << jsonEscape(clip.file) << "\",\n";
        out << "      \"loop\": " << (clip.looping ? "true" : "false") << ",\n";
        out << "      \"fade\": " << jsonNumber(clip.fadeDuration) << ",\n";
        out << "      \"speed\": " << jsonNumber(clip.speed) << ",\n";
        if (std::isfinite(clip.normalizedStartTime)) {
            out << "      \"normalizedStartTime\": "
                << jsonNumber(clip.normalizedStartTime) << ",\n";
        }
        if (!clip.rootBone.empty()) {
            out << "      \"rootBone\": \"" << jsonEscape(clip.rootBone) << "\",\n";
        }

        out << "      \"events\": [";
        if (!clip.events.empty()) out << "\n";
        for (std::size_t eventIndex = 0; eventIndex < clip.events.size(); ++eventIndex) {
            const ClipEventInfo& event = clip.events[eventIndex];
            out << "        {\"normalizedTime\": " << jsonNumber(event.normalizedTime)
                << ", \"name\": \"" << jsonEscape(event.name) << "\"}";
            if (eventIndex + 1 < clip.events.size()) out << ",";
            out << "\n";
        }
        if (!clip.events.empty()) out << "      ";
        out << "]\n";
        out << "    }";
        if (i + 1 < manifest.clips.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";

    return writeTextFile(path, out.str(), error);
}

}  // namespace anim::pack
