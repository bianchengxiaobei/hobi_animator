#pragma once

#include <string>
#include <string_view>

namespace anim::json {

std::string escape(std::string_view value);
std::string number(float value);
std::string number(double value);
bool writeFile(const std::string& path, const std::string& text, std::string& error);

}  // namespace anim::json
