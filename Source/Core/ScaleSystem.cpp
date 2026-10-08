#include "ScaleSystem.h"

#include <array>

namespace chordengine::core
{
namespace
{
constexpr std::array<ScaleDefinition, ScaleSystem::scaleCount> scales {{
    { ScaleId::major, "Major", {{ 0, 2, 4, 5, 7, 9, 11 }} },
    { ScaleId::minor, "Minor", {{ 0, 2, 3, 5, 7, 8, 10 }} },
    { ScaleId::dorian, "Dorian", {{ 0, 2, 3, 5, 7, 9, 10 }} },
    { ScaleId::phrygian, "Phrygian", {{ 0, 1, 3, 5, 7, 8, 10 }} },
    { ScaleId::lydian, "Lydian", {{ 0, 2, 4, 6, 7, 9, 11 }} },
    { ScaleId::mixolydian, "Mixolydian", {{ 0, 2, 4, 5, 7, 9, 10 }} },
    { ScaleId::locrian, "Locrian", {{ 0, 1, 3, 5, 6, 8, 10 }} },
    { ScaleId::harmonicMinor, "Harmonic Minor", {{ 0, 2, 3, 5, 7, 8, 11 }} }
}};
constexpr std::array<std::string_view, ScaleSystem::keyCount> keys {{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
}};
}

const ScaleDefinition* ScaleSystem::find(ScaleId id) noexcept
{
    for (const auto& scale : scales)
        if (scale.id == id)
            return &scale;
    return nullptr;
}

const ScaleDefinition* ScaleSystem::findByIndex(int index) noexcept
{
    return index >= 0 && index < static_cast<int>(scales.size())
        ? &scales[static_cast<std::size_t>(index)] : nullptr;
}

const ScaleDefinition* ScaleSystem::findByName(std::string_view name) noexcept
{
    for (const auto& scale : scales)
        if (scale.name == name)
            return &scale;
    return nullptr;
}

std::optional<std::uint8_t> ScaleSystem::keyOffset(int keyIndex) noexcept
{
    if (keyIndex < 0 || keyIndex >= static_cast<int>(keys.size()))
        return std::nullopt;
    return static_cast<std::uint8_t>(keyIndex);
}

std::string_view ScaleSystem::keyName(int keyIndex) noexcept
{
    return keyIndex >= 0 && keyIndex < static_cast<int>(keys.size())
        ? keys[static_cast<std::size_t>(keyIndex)] : std::string_view{};
}

std::optional<int> ScaleSystem::degreeOffset(ScaleId id, int keyIndex, int degree) noexcept
{
    const auto* scale = find(id);
    if (scale == nullptr || !keyOffset(keyIndex) || degree < 0 || degree >= 7)
        return std::nullopt;
    return keyIndex + scale->semitoneOffsetsFromRoot[static_cast<std::size_t>(degree)];
}
} // namespace chordengine::core
