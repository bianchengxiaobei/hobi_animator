#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace anim::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    bool isNull() const noexcept { return type == Type::Null; }
    bool isBool() const noexcept { return type == Type::Bool; }
    bool isNumber() const noexcept { return type == Type::Number; }
    bool isString() const noexcept { return type == Type::String; }
    bool isArray() const noexcept { return type == Type::Array; }
    bool isObject() const noexcept { return type == Type::Object; }

    const Value* find(std::string_view key) const;
    const Value* at(std::size_t index) const;
    std::size_t size() const noexcept;
};

bool parse(std::string_view text, Value& out, std::string& error);

}  // namespace anim::json
