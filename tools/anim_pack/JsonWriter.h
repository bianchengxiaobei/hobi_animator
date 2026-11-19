#pragma once

#include <string>
#include <string_view>

namespace anim::pack {

std::string jsonEscape(std::string_view value);
std::string jsonNumber(float value);
bool writeTextFile(const std::string& path, const std::string& text, std::string& error);

}  // namespace anim::pack
