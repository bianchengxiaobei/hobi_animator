#include "FbxMetaParser.h"

#include <fstream>
#include <sstream>

namespace anim::converter {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

}  // namespace

std::string FbxHumanMap::humanNameFor(std::string_view boneName) const {
    for (const auto& entry : entries) {
        if (entry.first == boneName) return entry.second;
    }
    return {};
}

bool parseFbxMeta(std::string_view yaml, FbxHumanMap& out, std::string& error) {
    out = FbxHumanMap{};

    bool inHumanDescription = false;
    bool inHumanList = false;
    std::string currentBone;

    std::istringstream stream{std::string(yaml)};
    std::string lineText;
    while (std::getline(stream, lineText)) {
        const std::string_view line(lineText);
        const std::string_view content = trim(line);
        if (content.empty()) continue;

        if (content == "humanDescription:") {
            inHumanDescription = true;
            continue;
        }
        if (!inHumanDescription) continue;

        if (content == "human:") {
            inHumanList = true;
            continue;
        }

        if (inHumanList) {
            if (content.rfind("- boneName:", 0) == 0) {
                currentBone = std::string(trim(content.substr(11)));
                continue;
            }
            if (content.rfind("humanName:", 0) == 0) {
                const std::string humanName(trim(content.substr(10)));
                if (!currentBone.empty() && !humanName.empty()) {
                    out.entries.emplace_back(currentBone, humanName);
                }
                currentBone.clear();
                continue;
            }
            if (content == "skeleton:" || content == "muscleClipSize:") {
                inHumanList = false;
                continue;
            }
        }

        if (content == "skeleton:") inHumanDescription = false;
    }

    if (out.entries.empty()) {
        error = "no humanDescription.human entries found";
        return false;
    }
    return true;
}

bool parseFbxMetaFile(const std::string& path, FbxHumanMap& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return parseFbxMeta(stream.str(), out, error);
}

}  // namespace anim::converter
