#pragma once

#include "ScaleSystem.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace chordengine::core
{
enum class ChordPresetId : std::uint8_t
{
    basic, pop, piano, emotional, dreamy, cinematic,
    rAndB, neoSoul, loFi, house, deep, ambient
};

struct ChordToneTemplate
{
    std::uint8_t stack = 0;
    std::uint8_t intervalOverride = 0;
};

struct ChordTemplateDefinition
{
    std::string_view name;
    std::array<ChordToneTemplate, 6> tones{};
    std::uint8_t toneCount = 0;
};

struct ChordPresetDefinition
{
    ChordPresetId id;
    std::string_view name;
    std::string_view voicingStyle;
    const std::uint8_t* triggerMap = nullptr;
};

struct ResolvedChord
{
    std::array<std::uint8_t, 6> intervals{};
    std::uint8_t intervalCount = 0;
    std::uint8_t scaleDegree = 0;
    int rootMidiNote = 48;
};

class ChordPresetSystem
{
public:
    static constexpr int triggerLow = 24;
    static constexpr int triggerHigh = 96;
    static constexpr std::size_t triggerCount = triggerHigh - triggerLow + 1;
    static constexpr std::size_t presetCount = 12;
    static constexpr std::size_t templateCount = 10;

    static std::size_t count() noexcept { return presetCount; }
    static const ChordPresetDefinition* get(ChordPresetId id) noexcept;
    static const ChordPresetDefinition* findByIndex(int index) noexcept;
    static const ChordPresetDefinition* findByName(std::string_view name) noexcept;
    static const ChordTemplateDefinition* chordTemplate(std::size_t index) noexcept;
    static std::optional<std::uint8_t> triggerCode(ChordPresetId preset, int midiNote) noexcept;
    static std::optional<ResolvedChord> resolve(ChordPresetId preset, int midiNote,
                                                ScaleId scale, int keyIndex) noexcept;
    // The same resolution for a trigger map that is not built in (a preset
    // from an installed pack). `triggerMap` holds triggerCount codes.
    static std::optional<ResolvedChord> resolve(const std::uint8_t* triggerMap, int midiNote,
                                                ScaleId scale, int keyIndex) noexcept;
    // Highest valid trigger code: degree (code % 7) and template (code / 7).
    static constexpr std::uint8_t maximumTriggerCode = 7 * templateCount - 1;
};

// A preset defined outside the binary (an installed pack preset): its own
// trigger map and one of the built-in voicing styles.
struct CustomChordPreset
{
    std::array<std::uint8_t, ChordPresetSystem::triggerCount> triggerMap{};
    std::string_view voicingStyle = "close";

    // True when every code is in range and the voicing is close/spread/open.
    bool valid() const noexcept;
};
} // namespace chordengine::core
