#include <iostream>
#include <string>

#include "AnimPack.h"

namespace {

int usage() {
    std::cerr
        << "usage:\n"
        << "  anim_pack --input character.glb --out-dir build/anim"
           " --manifest clips.json\n"
        << "            [--skeleton-out character.skeleton.json]"
           " [--coordinate gltf|mirrored]\n"
        << "            [--strip-action-prefix Armature|]"
           " [--events events.json]\n";
    return 2;
}

bool parseArgs(int argc, char** argv, anim::pack::PackOptions& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](std::string& value) {
            if (i + 1 >= argc) return false;
            value = argv[++i];
            return true;
        };

        if (arg == "--input") {
            if (!next(options.input)) return false;
        } else if (arg == "--out-dir") {
            if (!next(options.outDir)) return false;
        } else if (arg == "--manifest") {
            if (!next(options.manifest)) return false;
        } else if (arg == "--skeleton-out") {
            if (!next(options.skeletonOut)) return false;
        } else if (arg == "--coordinate") {
            if (!next(options.coordinate)) return false;
        } else if (arg == "--strip-action-prefix") {
            if (!next(options.stripActionPrefix)) return false;
        } else if (arg == "--events") {
            if (!next(options.events)) return false;
        } else {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    anim::pack::PackOptions options;
    if (!parseArgs(argc, argv, options)) return usage();

    anim::pack::PackResult result;
    std::vector<std::string> warnings;
    std::string error;
    if (!anim::pack::packAnimations(options, result, warnings, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    for (const std::string& warning : warnings) {
        std::cerr << "warning: " << warning << "\n";
    }

    std::cout << "anim_pack: " << result.boneCount << " bones, "
              << result.animationCount << " animations -> " << options.outDir << "\n";
    for (const anim::pack::PackClipSummary& clip : result.clips) {
        std::cout << "  " << clip.name << " -> " << clip.file << " ("
                  << clip.channelCount << " channels, " << clip.keyCount
                  << " keys, " << clip.length << "s, " << clip.eventCount
                  << " events)\n";
    }
    return 0;
}
