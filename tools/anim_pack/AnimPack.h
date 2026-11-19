#pragma once

#include <string>
#include <vector>

namespace anim::pack {

struct PackOptions {
    std::string input;
    std::string outDir;
    std::string manifest;
    std::string skeletonOut;
    std::string coordinate;
    std::string stripActionPrefix;
    std::string events;
};

struct PackClipSummary {
    std::string name;
    std::string file;
    int channelCount = 0;
    int keyCount = 0;
    float length = 0.0f;
    int eventCount = 0;
};

struct PackResult {
    int boneCount = 0;
    int animationCount = 0;
    std::vector<PackClipSummary> clips;
};

bool packAnimations(const PackOptions& options, PackResult& result,
                    std::vector<std::string>& warnings, std::string& error);

}  // namespace anim::pack
