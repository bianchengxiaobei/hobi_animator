#include "AbWriter.h"

#include <fstream>
#include <vector>

#include "anim/clip/ClipIO.h"

namespace anim::converter {

bool writeAbFile(const Clip& clip, const std::string& path, std::string& error) {
    const std::vector<uint8_t> data = encodeClip(clip, &error);
    if (data.empty()) return false;

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(file);
}

}  // namespace anim::converter
