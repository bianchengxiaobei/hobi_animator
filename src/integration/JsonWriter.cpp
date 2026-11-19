#include "anim/integration/JsonWriter.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace anim::json {

std::string escape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        const unsigned char byte = static_cast<unsigned char>(c);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (byte < 0x20) {
                    char buffer[7]{};
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", byte);
                    out += buffer;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

std::string number(float value) {
    if (!std::isfinite(value)) return "0";
    if (value == 0.0f) value = 0.0f;
    char buffer[40]{};
    std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    return buffer;
}

std::string number(double value) {
    if (!std::isfinite(value)) return "0";
    if (value == 0.0) value = 0.0;
    char buffer[40]{};
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return buffer;
}

bool writeFile(const std::string& path, const std::string& text, std::string& error) {
    const std::filesystem::path target(path);
    const std::filesystem::path parent = target.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = "cannot create directory '" + parent.string() + "': " + ec.message();
            return false;
        }
    }

    std::ofstream file(target, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "cannot open '" + path + "' for writing";
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        error = "cannot write '" + path + "'";
        return false;
    }
    return true;
}

}  // namespace anim::json
