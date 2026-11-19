#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace anim::converter {

struct FbxHumanMap {
    std::vector<std::pair<std::string, std::string>> entries;

    std::string humanNameFor(std::string_view boneName) const;
};

bool parseFbxMeta(std::string_view yaml, FbxHumanMap& out, std::string& error);
bool parseFbxMetaFile(const std::string& path, FbxHumanMap& out, std::string& error);

}  // namespace anim::converter
