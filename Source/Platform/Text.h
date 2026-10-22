#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

// Small std::string helpers (ASCII case rules; UTF-8 aware where the old
// code counted characters).
namespace chordengine::text
{
inline bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

inline std::string trim(std::string_view value)
{
    std::size_t start = 0;
    std::size_t end = value.size();
    while (start < end && isSpace(value[start]))
        ++start;
    while (end > start && isSpace(value[end - 1]))
        --end;
    return std::string(value.substr(start, end - start));
}

inline std::string toLower(std::string_view value)
{
    std::string out(value);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return out;
}

inline std::string toUpper(std::string_view value)
{
    std::string out(value);
    for (auto& c : out)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return out;
}

inline bool containsOnly(std::string_view value, std::string_view allowed) noexcept
{
    for (const auto c : value)
        if (allowed.find(c) == std::string_view::npos)
            return false;
    return true;
}

inline std::string retainCharacters(std::string_view value, std::string_view allowed)
{
    std::string out;
    for (const auto c : value)
        if (allowed.find(c) != std::string_view::npos)
            out += c;
    return out;
}

inline bool startsWith(std::string_view value, std::string_view prefix) noexcept
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

inline bool endsWith(std::string_view value, std::string_view suffix) noexcept
{
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool contains(std::string_view value, std::string_view part) noexcept
{
    return value.find(part) != std::string_view::npos;
}

inline std::string replaceAll(std::string value, std::string_view from, std::string_view to)
{
    if (from.empty())
        return value;
    std::size_t position = 0;
    while ((position = value.find(from, position)) != std::string::npos)
    {
        value.replace(position, from.size(), to);
        position += to.size();
    }
    return value;
}

inline std::string padLeft(std::string value, std::size_t width, char fill)
{
    if (value.size() < width)
        value.insert(0, width - value.size(), fill);
    return value;
}

// Number of UTF-8 code points (what the 0.5 string type's length counted).
inline std::size_t utf8Length(std::string_view value) noexcept
{
    std::size_t count = 0;
    for (const auto c : value)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
            ++count;
    return count;
}

// Decodes UTF-8 into code points; invalid bytes become U+FFFD.
inline std::vector<std::uint32_t> codePoints(std::string_view value)
{
    std::vector<std::uint32_t> out;
    for (std::size_t index = 0; index < value.size();)
    {
        const auto c = static_cast<unsigned char>(value[index]);
        int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (extra < 0 || index + static_cast<std::size_t>(extra) >= value.size())
        {
            out.push_back(0xFFFD);
            ++index;
            continue;
        }
        std::uint32_t code = extra == 0 ? c : extra == 1 ? (c & 0x1F) : extra == 2 ? (c & 0x0F) : (c & 0x07);
        bool valid = true;
        for (int k = 1; k <= extra; ++k)
        {
            const auto next = static_cast<unsigned char>(value[index + static_cast<std::size_t>(k)]);
            if ((next & 0xC0) != 0x80)
            {
                valid = false;
                break;
            }
            code = (code << 6) | (next & 0x3F);
        }
        if (!valid)
        {
            out.push_back(0xFFFD);
            ++index;
            continue;
        }
        out.push_back(code);
        index += static_cast<std::size_t>(extra) + 1;
    }
    return out;
}

inline std::string hex2(unsigned value)
{
    char buffer[4];
    std::snprintf(buffer, sizeof(buffer), "%02x", value & 0xFFu);
    return buffer;
}

inline int hexDigit(char c) noexcept
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline std::vector<std::string> split(std::string_view value, char separator)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t index = 0; index <= value.size(); ++index)
        if (index == value.size() || value[index] == separator)
        {
            out.emplace_back(value.substr(start, index - start));
            start = index + 1;
        }
    return out;
}
} // namespace chordengine::text
