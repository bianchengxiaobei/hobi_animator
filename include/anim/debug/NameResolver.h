#pragma once

#include <string_view>

namespace anim {
struct Clip;
}

namespace anim::debug {

class NameResolver {
public:
    virtual ~NameResolver() = default;
    virtual std::string_view boneName(int boneIndex) const = 0;
    virtual std::string_view clipName(const Clip& clip) const = 0;
};

}
