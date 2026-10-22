#include "Json.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace chordengine::platform
{
namespace
{
const Json& nullValue()
{
    static const Json value;
    return value;
}

const Json::Array& emptyArray()
{
    static const Json::Array value;
    return value;
}

const Json::Object& emptyObject()
{
    static const Json::Object value;
    return value;
}

void appendUtf8(std::string& out, std::uint32_t code)
{
    if (code < 0x80)
        out += static_cast<char>(code);
    else if (code < 0x800)
    {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    else if (code < 0x10000)
    {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

class Parser
{
public:
    explicit Parser(const std::string& text) : text_(text) {}

    std::optional<Json> run()
    {
        skipSpace();
        auto value = parseValue(0);
        if (!value.has_value())
            return std::nullopt;
        skipSpace();
        if (position_ != text_.size())
            return std::nullopt;
        return value;
    }

private:
    void skipSpace()
    {
        while (position_ < text_.size())
        {
            const char c = text_[position_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                return;
            ++position_;
        }
    }

    bool literal(const char* word)
    {
        std::size_t index = 0;
        for (; word[index] != '\0'; ++index)
            if (position_ + index >= text_.size() || text_[position_ + index] != word[index])
                return false;
        position_ += index;
        return true;
    }

    std::optional<Json> parseValue(int depth)
    {
        if (depth > Json::maxDepth || position_ >= text_.size())
            return std::nullopt;
        const char c = text_[position_];
        if (c == '{')
            return parseObject(depth);
        if (c == '[')
            return parseArray(depth);
        if (c == '"')
        {
            auto text = parseString();
            if (!text.has_value())
                return std::nullopt;
            return Json(std::move(*text));
        }
        if (c == 't')
            return literal("true") ? std::optional<Json>(Json(true)) : std::nullopt;
        if (c == 'f')
            return literal("false") ? std::optional<Json>(Json(false)) : std::nullopt;
        if (c == 'n')
            return literal("null") ? std::optional<Json>(Json()) : std::nullopt;
        if (c == '-' || (c >= '0' && c <= '9'))
            return parseNumber();
        return std::nullopt;
    }

    std::optional<Json> parseNumber()
    {
        const auto start = position_;
        bool real = false;
        if (text_[position_] == '-')
            ++position_;
        if (position_ >= text_.size())
            return std::nullopt;
        if (text_[position_] == '0')
            ++position_;
        else if (text_[position_] >= '1' && text_[position_] <= '9')
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9')
                ++position_;
        else
            return std::nullopt;
        if (position_ < text_.size() && text_[position_] == '.')
        {
            real = true;
            ++position_;
            const auto digits = position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9')
                ++position_;
            if (position_ == digits)
                return std::nullopt;
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E'))
        {
            real = true;
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-'))
                ++position_;
            const auto digits = position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9')
                ++position_;
            if (position_ == digits)
                return std::nullopt;
        }
        const std::string token = text_.substr(start, position_ - start);
        if (!real)
        {
            errno = 0;
            char* end = nullptr;
            const long long value = std::strtoll(token.c_str(), &end, 10);
            if (errno == 0 && end != nullptr && *end == '\0')
                return Json(static_cast<std::int64_t>(value));
        }
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0' || !std::isfinite(value))
            return std::nullopt;
        return Json(value);
    }

    std::optional<std::uint32_t> parseHex4()
    {
        if (position_ + 4 > text_.size())
            return std::nullopt;
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index)
        {
            const char c = text_[position_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return std::nullopt;
        }
        return value;
    }

    std::optional<std::string> parseString()
    {
        ++position_; // opening quote
        std::string out;
        while (position_ < text_.size())
        {
            const auto c = static_cast<unsigned char>(text_[position_++]);
            if (c == '"')
                return out;
            if (c < 0x20)
                return std::nullopt;
            if (c != '\\')
            {
                out += static_cast<char>(c);
                continue;
            }
            if (position_ >= text_.size())
                return std::nullopt;
            const char escape = text_[position_++];
            switch (escape)
            {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    auto code = parseHex4();
                    if (!code.has_value())
                        return std::nullopt;
                    if (*code >= 0xD800 && *code <= 0xDBFF)
                    {
                        if (position_ + 2 > text_.size() || text_[position_] != '\\' || text_[position_ + 1] != 'u')
                            return std::nullopt;
                        position_ += 2;
                        const auto low = parseHex4();
                        if (!low.has_value() || *low < 0xDC00 || *low > 0xDFFF)
                            return std::nullopt;
                        *code = 0x10000 + ((*code - 0xD800) << 10) + (*low - 0xDC00);
                    }
                    else if (*code >= 0xDC00 && *code <= 0xDFFF)
                        return std::nullopt;
                    appendUtf8(out, *code);
                    break;
                }
                default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<Json> parseArray(int depth)
    {
        ++position_;
        Json::Array items;
        skipSpace();
        if (position_ < text_.size() && text_[position_] == ']')
        {
            ++position_;
            return Json(std::move(items));
        }
        while (true)
        {
            skipSpace();
            auto value = parseValue(depth + 1);
            if (!value.has_value())
                return std::nullopt;
            items.push_back(std::move(*value));
            skipSpace();
            if (position_ >= text_.size())
                return std::nullopt;
            if (text_[position_] == ',')
            {
                ++position_;
                continue;
            }
            if (text_[position_] == ']')
            {
                ++position_;
                return Json(std::move(items));
            }
            return std::nullopt;
        }
    }

    std::optional<Json> parseObject(int depth)
    {
        ++position_;
        Json result = Json::object();
        skipSpace();
        if (position_ < text_.size() && text_[position_] == '}')
        {
            ++position_;
            return result;
        }
        while (true)
        {
            skipSpace();
            if (position_ >= text_.size() || text_[position_] != '"')
                return std::nullopt;
            auto key = parseString();
            if (!key.has_value())
                return std::nullopt;
            skipSpace();
            if (position_ >= text_.size() || text_[position_] != ':')
                return std::nullopt;
            ++position_;
            skipSpace();
            auto value = parseValue(depth + 1);
            if (!value.has_value())
                return std::nullopt;
            result.set(*key, std::move(*value));
            skipSpace();
            if (position_ >= text_.size())
                return std::nullopt;
            if (text_[position_] == ',')
            {
                ++position_;
                continue;
            }
            if (text_[position_] == '}')
            {
                ++position_;
                return result;
            }
            return std::nullopt;
        }
    }

    const std::string& text_;
    std::size_t position_ = 0;
};

void dumpTo(std::string& out, const Json& value)
{
    switch (value.type())
    {
        case Json::Type::null: out += "null"; break;
        case Json::Type::boolean: out += value.asBool() ? "true" : "false"; break;
        case Json::Type::integer: out += std::to_string(value.asInt()); break;
        case Json::Type::real:
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.17g", value.asDouble());
            out += buffer;
            break;
        }
        case Json::Type::string: appendJsonString(out, value.stringRef()); break;
        case Json::Type::array:
        {
            out += '[';
            bool first = true;
            for (const auto& item : value.items())
            {
                if (!first) out += ',';
                first = false;
                dumpTo(out, item);
            }
            out += ']';
            break;
        }
        case Json::Type::object:
        {
            out += '{';
            bool first = true;
            for (const auto& [key, item] : value.members())
            {
                if (!first) out += ',';
                first = false;
                appendJsonString(out, key);
                out += ':';
                dumpTo(out, item);
            }
            out += '}';
            break;
        }
    }
}
} // namespace

Json::Json(Array value) : type_(Type::array), array_(std::make_shared<Array>(std::move(value))) {}
Json::Json(Object value) : type_(Type::object), object_(std::make_shared<Object>(std::move(value))) {}

std::int64_t Json::asInt() const noexcept
{
    if (type_ == Type::integer)
        return int_;
    if (type_ == Type::real && std::isfinite(real_)
        && real_ >= static_cast<double>(std::numeric_limits<std::int64_t>::min())
        && real_ <= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        return static_cast<std::int64_t>(real_);
    if (type_ == Type::boolean)
        return bool_ ? 1 : 0;
    return 0;
}

double Json::asDouble() const noexcept
{
    if (type_ == Type::real)
        return real_;
    if (type_ == Type::integer)
        return static_cast<double>(int_);
    if (type_ == Type::boolean)
        return bool_ ? 1.0 : 0.0;
    return 0.0;
}

std::string Json::asString() const
{
    switch (type_)
    {
        case Type::string: return string_;
        case Type::boolean: return bool_ ? "true" : "false";
        case Type::integer: return std::to_string(int_);
        case Type::real:
        {
            std::string out;
            dumpTo(out, *this);
            return out;
        }
        default: return {};
    }
}

const Json::Array& Json::items() const noexcept
{
    return type_ == Type::array && array_ ? *array_ : emptyArray();
}

const Json::Object& Json::members() const noexcept
{
    return type_ == Type::object && object_ ? *object_ : emptyObject();
}

bool Json::has(const std::string& key) const noexcept
{
    for (const auto& member : members())
        if (member.first == key)
            return true;
    return false;
}

const Json& Json::operator[](const std::string& key) const noexcept
{
    for (const auto& member : members())
        if (member.first == key)
            return member.second;
    return nullValue();
}

Json& Json::set(const std::string& key, Json value)
{
    if (type_ != Type::object || !object_)
    {
        *this = Json::object();
    }
    else if (object_.use_count() > 1)
        object_ = std::make_shared<Object>(*object_);
    for (auto& member : *object_)
        if (member.first == key)
        {
            member.second = std::move(value);
            return *this;
        }
    object_->emplace_back(key, std::move(value));
    return *this;
}

void Json::remove(const std::string& key)
{
    if (type_ != Type::object || !object_)
        return;
    if (object_.use_count() > 1)
        object_ = std::make_shared<Object>(*object_);
    for (auto it = object_->begin(); it != object_->end(); ++it)
        if (it->first == key)
        {
            object_->erase(it);
            return;
        }
}

Json& Json::member(const std::string& key)
{
    if (type_ != Type::object || !object_)
        *this = Json::object();
    else if (object_.use_count() > 1)
        object_ = std::make_shared<Object>(*object_);
    for (auto& entry : *object_)
        if (entry.first == key)
            return entry.second;
    object_->emplace_back(key, Json());
    return object_->back().second;
}

Json::Array& Json::mutableItems()
{
    if (type_ != Type::array || !array_)
        *this = Json::array();
    else if (array_.use_count() > 1)
        array_ = std::make_shared<Array>(*array_);
    return *array_;
}

void Json::push(Json value)
{
    if (type_ != Type::array || !array_)
        *this = Json::array();
    else if (array_.use_count() > 1)
        array_ = std::make_shared<Array>(*array_);
    array_->push_back(std::move(value));
}

std::size_t Json::size() const noexcept
{
    if (type_ == Type::array)
        return items().size();
    if (type_ == Type::object)
        return members().size();
    return 0;
}

std::string Json::dump() const
{
    std::string out;
    dumpTo(out, *this);
    return out;
}

std::optional<Json> Json::parse(const std::string& text)
{
    return Parser(text).run();
}

void appendJsonString(std::string& out, const std::string& text)
{
    out += '"';
    for (const auto raw : text)
    {
        const auto c = static_cast<unsigned char>(raw);
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                }
                else
                    out += static_cast<char>(c);
        }
    }
    out += '"';
}
} // namespace chordengine::platform
