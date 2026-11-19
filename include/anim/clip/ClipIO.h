#pragma once

#include <memory>
#include <string>
#include <vector>

#include "anim/clip/Clip.h"
#include "anim/core/Serialization.h"
#include "anim/core/Span.h"

namespace anim {

bool writeClip(const Clip& clip, Writer& writer, std::string* error = nullptr);
std::unique_ptr<Clip> readClip(Reader& reader, std::string* error = nullptr);

std::vector<uint8_t> encodeClip(const Clip& clip, std::string* error = nullptr);
std::unique_ptr<Clip> decodeClip(Span<const uint8_t> data, std::string* error = nullptr);
std::unique_ptr<Clip> decodeClip(const std::vector<uint8_t>& data, std::string* error = nullptr);

}  // namespace anim
