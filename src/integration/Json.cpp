#include "anim/integration/Json.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace anim::json {
namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : _text(text) {}

    bool parseDocument(Value& out, std::string& error) {
        _pos = 0;
        if (_text.size() >= 3 &&
            static_cast<unsigned char>(_text[0]) == 0xEF &&
            static_cast<unsigned char>(_text[1]) == 0xBB &&
            static_cast<unsigned char>(_text[2]) == 0xBF) {
            _pos = 3;  // tolerate a UTF-8 BOM written by editors
        }
        skipSpace();
        if (!parseValue(out)) {
            error = _error.empty() ? "invalid JSON" : _error;
            return false;
        }
        skipSpace();
        if (_pos != _text.size()) {
            error = "trailing characters after JSON value";
            return false;
        }
        return true;
    }

private:
    void skipSpace() {
        while (_pos < _text.size() &&
               (_text[_pos] == ' ' || _text[_pos] == '\t' || _text[_pos] == '\n' ||
                _text[_pos] == '\r')) {
            ++_pos;
        }
    }

    bool fail(const char* message) {
        if (_error.empty()) _error = message;
        return false;
    }

    bool consume(char expected) {
        skipSpace();
        if (_pos >= _text.size() || _text[_pos] != expected) return false;
        ++_pos;
        return true;
    }

    bool parseValue(Value& out) {
        skipSpace();
        if (_pos >= _text.size()) return fail("unexpected end of input");

        const char c = _text[_pos];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') {
            out.type = Value::Type::String;
            return parseString(out.text);
        }
        if (c == 't' || c == 'f') return parseBool(out);
        if (c == 'n') return parseNull(out);
        return parseNumber(out);
    }

    bool parseObject(Value& out) {
        out.type = Value::Type::Object;
        if (!consume('{')) return fail("expected '{'");
        skipSpace();
        if (_pos < _text.size() && _text[_pos] == '}') {
            ++_pos;
            return true;
        }

        while (true) {
            std::string key;
            skipSpace();
            if (!parseString(key)) return fail("expected object key");

            if (!consume(':')) return fail("expected ':'");

            Value value;
            if (!parseValue(value)) return false;
            out.object.emplace_back(std::move(key), std::move(value));

            skipSpace();
            if (_pos < _text.size() && _text[_pos] == ',') {
                ++_pos;
                continue;
            }
            if (_pos < _text.size() && _text[_pos] == '}') {
                ++_pos;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool parseArray(Value& out) {
        out.type = Value::Type::Array;
        if (!consume('[')) return fail("expected '['");
        skipSpace();
        if (_pos < _text.size() && _text[_pos] == ']') {
            ++_pos;
            return true;
        }

        while (true) {
            Value value;
            if (!parseValue(value)) return false;
            out.array.push_back(std::move(value));

            skipSpace();
            if (_pos < _text.size() && _text[_pos] == ',') {
                ++_pos;
                continue;
            }
            if (_pos < _text.size() && _text[_pos] == ']') {
                ++_pos;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool parseString(std::string& out) {
        skipSpace();
        if (_pos >= _text.size() || _text[_pos] != '"') return fail("expected string");
        ++_pos;

        while (_pos < _text.size()) {
            const char c = _text[_pos++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }

            if (_pos >= _text.size()) return fail("unterminated escape");
            const char escaped = _text[_pos++];
            switch (escaped) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (_pos + 4 > _text.size()) return fail("bad unicode escape");
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char digit = _text[_pos++];
                        code <<= 4;
                        if (digit >= '0' && digit <= '9')
                            code |= static_cast<unsigned>(digit - '0');
                        else if (digit >= 'a' && digit <= 'f')
                            code |= static_cast<unsigned>(digit - 'a' + 10);
                        else if (digit >= 'A' && digit <= 'F')
                            code |= static_cast<unsigned>(digit - 'A' + 10);
                        else
                            return fail("bad unicode escape");
                    }
                    appendUtf8(out, code);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }

    static void appendUtf8(std::string& out, unsigned code) {
        if (code <= 0x7F) {
            out.push_back(static_cast<char>(code));
        } else if (code <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool parseBool(Value& out) {
        if (_text.substr(_pos, 4) == "true") {
            out.type = Value::Type::Bool;
            out.boolean = true;
            _pos += 4;
            return true;
        }
        if (_text.substr(_pos, 5) == "false") {
            out.type = Value::Type::Bool;
            out.boolean = false;
            _pos += 5;
            return true;
        }
        return fail("invalid boolean");
    }

    bool parseNull(Value& out) {
        if (_text.substr(_pos, 4) != "null") return fail("invalid null");
        out.type = Value::Type::Null;
        _pos += 4;
        return true;
    }

    bool parseNumber(Value& out) {
        const std::size_t start = _pos;
        if (_pos < _text.size() && _text[_pos] == '-') ++_pos;

        bool hasDigits = false;
        while (_pos < _text.size() && std::isdigit(static_cast<unsigned char>(_text[_pos]))) {
            ++_pos;
            hasDigits = true;
        }
        if (_pos < _text.size() && _text[_pos] == '.') {
            ++_pos;
            while (_pos < _text.size() &&
                   std::isdigit(static_cast<unsigned char>(_text[_pos]))) {
                ++_pos;
                hasDigits = true;
            }
        }
        if (_pos < _text.size() && (_text[_pos] == 'e' || _text[_pos] == 'E')) {
            ++_pos;
            if (_pos < _text.size() && (_text[_pos] == '+' || _text[_pos] == '-')) ++_pos;
            while (_pos < _text.size() &&
                   std::isdigit(static_cast<unsigned char>(_text[_pos]))) {
                ++_pos;
            }
        }

        if (!hasDigits) return fail("invalid number");

        out.type = Value::Type::Number;
        const std::string text(_text.substr(start, _pos - start));
        out.number = std::strtod(text.c_str(), nullptr);
        return true;
    }

    std::string_view _text;
    std::size_t _pos = 0;
    std::string _error;
};

}  // namespace

const Value* Value::find(std::string_view key) const {
    for (const auto& entry : object) {
        if (entry.first == key) return &entry.second;
    }
    return nullptr;
}

const Value* Value::at(std::size_t index) const {
    return index < array.size() ? &array[index] : nullptr;
}

std::size_t Value::size() const noexcept {
    return type == Type::Array ? array.size() : object.size();
}

bool parse(std::string_view text, Value& out, std::string& error) {
    Parser parser(text);
    return parser.parseDocument(out, error);
}

}  // namespace anim::json
