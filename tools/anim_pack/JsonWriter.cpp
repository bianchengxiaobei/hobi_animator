#include "JsonWriter.h"

#include "anim/integration/JsonWriter.h"

namespace anim::pack {

std::string jsonEscape(std::string_view value) { return anim::json::escape(value); }

std::string jsonNumber(float value) { return anim::json::number(value); }

bool writeTextFile(const std::string& path, const std::string& text, std::string& error) {
    return anim::json::writeFile(path, text, error);
}

}  // namespace anim::pack
