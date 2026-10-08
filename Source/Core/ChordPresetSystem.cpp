#include "ChordPresetSystem.h"

#include <array>

namespace chordengine::core
{
namespace
{
#include "ChordPresetMaps.inc"

constexpr std::array<ChordPresetDefinition, ChordPresetSystem::presetCount> presets {{
    { ChordPresetId::basic, "Basic", "close", triggerMaps[0].data() },
    { ChordPresetId::pop, "Pop", "close", triggerMaps[1].data() },
    { ChordPresetId::piano, "Piano", "close", triggerMaps[2].data() },
    { ChordPresetId::emotional, "Emotional", "close", triggerMaps[3].data() },
    { ChordPresetId::dreamy, "Dreamy", "close", triggerMaps[4].data() },
    { ChordPresetId::cinematic, "Cinematic", "spread", triggerMaps[5].data() },
    { ChordPresetId::rAndB, "R&B", "close", triggerMaps[6].data() },
    { ChordPresetId::neoSoul, "Neo Soul", "close", triggerMaps[7].data() },
    { ChordPresetId::loFi, "Lo-Fi", "close", triggerMaps[8].data() },
    { ChordPresetId::house, "House", "close", triggerMaps[9].data() },
    { ChordPresetId::deep, "Deep", "close", triggerMaps[10].data() },
    { ChordPresetId::ambient, "Ambient", "open", triggerMaps[11].data() }
}};
}

const ChordPresetDefinition* ChordPresetSystem::get(ChordPresetId id) noexcept
{
    const auto index = static_cast<std::size_t>(id);
    return index < presets.size() ? &presets[index] : nullptr;
}

const ChordPresetDefinition* ChordPresetSystem::findByIndex(int index) noexcept
{
    return index >= 0 && index < static_cast<int>(presets.size())
        ? &presets[static_cast<std::size_t>(index)] : nullptr;
}

const ChordPresetDefinition* ChordPresetSystem::findByName(std::string_view name) noexcept
{
    for (const auto& preset : presets)
        if (preset.name == name)
            return &preset;
    return nullptr;
}

const ChordTemplateDefinition* ChordPresetSystem::chordTemplate(std::size_t index) noexcept
{
    return index < templates.size() ? &templates[index] : nullptr;
}

std::optional<std::uint8_t> ChordPresetSystem::triggerCode(ChordPresetId preset, int midiNote) noexcept
{
    const auto* definition = get(preset);
    if (definition == nullptr || midiNote < triggerLow || midiNote > triggerHigh)
        return std::nullopt;
    const auto mapIndex = static_cast<std::size_t>(midiNote - triggerLow);
    return definition->triggerMap[mapIndex];
}

std::optional<ResolvedChord> ChordPresetSystem::resolve(ChordPresetId preset, int midiNote,
                                                         ScaleId scaleId, int keyIndex) noexcept
{
    const auto code = triggerCode(preset, midiNote);
    const auto* scale = ScaleSystem::find(scaleId);
    const auto* presetDefinition = get(preset);
    if (!code || scale == nullptr || !ScaleSystem::keyOffset(keyIndex))
        return std::nullopt;

    const int degree = static_cast<int>(*code % 7) + 1;
    const auto templateIndex = static_cast<std::size_t>(*code / 7);
    const auto* chord = chordTemplate(templateIndex);
    if (chord == nullptr || chord->toneCount == 0)
        return std::nullopt;

    ResolvedChord result;
    result.scaleDegree = static_cast<std::uint8_t>(degree);
    result.rootMidiNote = 48 + keyIndex
        + scale->semitoneOffsetsFromRoot[static_cast<std::size_t>(degree - 1)];

    const auto resolveInterval = [scale, degree](const ChordToneTemplate& tone) noexcept
    {
        if (tone.intervalOverride != 0)
            return static_cast<int>(tone.intervalOverride);
        const int step = degree - 1 + 2 * static_cast<int>(tone.stack);
        return static_cast<int>(scale->semitoneOffsetsFromRoot[static_cast<std::size_t>(step % 7)])
            + 12 * (step / 7)
            - static_cast<int>(scale->semitoneOffsetsFromRoot[static_cast<std::size_t>(degree - 1)]);
    };

    int third = 0;
    for (std::size_t index = 0; index < chord->toneCount; ++index)
    {
        const auto& tone = chord->tones[index];
        if (tone.stack >= 4)
            continue;
        const int interval = resolveInterval(tone);
        if (result.intervalCount >= result.intervals.size())
            return std::nullopt;
        result.intervals[result.intervalCount++] = static_cast<std::uint8_t>(interval);
        if (interval == 4)
            third = 4;
        else if (interval == 3 && third == 0)
            third = 3;
    }

    for (std::size_t index = 0; index < chord->toneCount; ++index)
    {
        const auto& tone = chord->tones[index];
        if (tone.stack < 4)
            continue;
        const int interval = resolveInterval(tone);
        const int pitchClass = interval % 12;
        const bool keep = pitchClass == 2 || pitchClass == 9
            || (pitchClass == 5 && third == 3)
            || (pitchClass == 6 && third == 4);
        if (keep && result.intervalCount < result.intervals.size())
            result.intervals[result.intervalCount++] = static_cast<std::uint8_t>(interval);
    }

    if (result.intervalCount < 3 || presetDefinition == nullptr)
        return std::nullopt;
    return result;
}
} // namespace chordengine::core
