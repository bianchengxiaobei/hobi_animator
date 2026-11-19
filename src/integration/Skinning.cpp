#include "anim/integration/Skinning.h"

namespace anim {

void transposePalette(const std::vector<Mat4>& source, std::vector<Mat4>& destination) {
    destination.resize(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        destination[i] = transpose(source[i]);
    }
}

}  // namespace anim
