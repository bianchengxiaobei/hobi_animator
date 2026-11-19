#pragma once

#include <string>

#include "anim/clip/Clip.h"

namespace anim::converter {

bool writeAbFile(const Clip& clip, const std::string& path, std::string& error);

}  // namespace anim::converter
