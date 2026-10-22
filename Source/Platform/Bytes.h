#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chordengine::platform
{
// Raw bytes (a downloaded file, a stored pack) as distinct from text.
using Bytes = std::vector<std::uint8_t>;

inline Bytes bytesOf(std::string_view text)
{
    return Bytes(text.begin(), text.end());
}

inline std::string textOf(const Bytes& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

// Strict UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF.
inline bool isValidUtf8(const std::uint8_t* data, std::size_t size) noexcept
{
    std::size_t index = 0;
    while (index < size)
    {
        const auto c = data[index];
        if (c < 0x80)
        {
            ++index;
            continue;
        }
        std::size_t extra = 0;
        std::uint32_t code = 0;
        if ((c & 0xE0) == 0xC0) { extra = 1; code = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; code = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; code = c & 0x07u; }
        else return false;
        if (index + extra >= size)
            return false;
        for (std::size_t k = 1; k <= extra; ++k)
        {
            const auto next = data[index + k];
            if ((next & 0xC0) != 0x80)
                return false;
            code = (code << 6) | (next & 0x3Fu);
        }
        if ((extra == 1 && code < 0x80) || (extra == 2 && code < 0x800) || (extra == 3 && code < 0x10000)
            || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
            return false;
        index += extra + 1;
    }
    return true;
}
} // namespace chordengine::platform
