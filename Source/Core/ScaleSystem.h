#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace chordengine::core
{
enum class ScaleId : std::uint8_t
{
    major, minor, dorian, phrygian, lydian, mixolydian, locrian, harmonicMinor
};

struct ScaleDefinition
{
    ScaleId id;
    std::string_view name;
    std::array<std::uint8_t, 7> semitoneOffsetsFromRoot;
};

class ScaleSystem
{
public:
    static constexpr std::size_t scaleCount = 8;
    static constexpr std::size_t keyCount = 12;

    static const ScaleDefinition* find(ScaleId id) noexcept;
    static const ScaleDefinition* findByIndex(int index) noexcept;
    static const ScaleDefinition* findByName(std::string_view name) noexcept;
    static std::optional<std::uint8_t> keyOffset(int keyIndex) noexcept;
    static std::string_view keyName(int keyIndex) noexcept;
    static std::optional<int> degreeOffset(ScaleId id, int keyIndex, int degree) noexcept;
};
} // namespace chordengine::core
