#include "Core/ChordPresetSystem.h"
#include "Core/ScaleSystem.h"

#include "TestSupport.h"

#include <cstdlib>
#include <iostream>

using namespace chordengine::test;

int main()
{
    const auto fixture = parseJson(readText(CHORDENGINE_FL_GOLDEN_FILE));
    require(fixture.isObject(), "FLReferenceVectors.json parses");
    require(field(fixture, "hostObserved").isBool() && !field(fixture, "hostObserved").asBool(),
            "source-derived fixtures remain explicitly not host-observed");
    const auto& tables = field(fixture, "sourceTables");
    const auto& scaleNames = field(tables, "scaleNames");
    const auto& scaleDegrees = field(tables, "scaleDegrees");
    const auto& presetNames = field(tables, "presetNames");
    const auto& presetVoicings = field(tables, "presetVoicings");
    const auto& triggerMaps = field(tables, "triggerMapByPreset");
    const auto* scaleArray = scaleNames.isArray() ? &scaleNames.items() : nullptr;
    const auto* presetArray = presetNames.isArray() ? &presetNames.items() : nullptr;
    require(scaleArray != nullptr && scaleArray->size() == 8,
            "fixture contains exactly eight scales");
    require(presetArray != nullptr && presetArray->size() == 12,
            "fixture contains exactly twelve presets");

    for (int scale = 0; scale < static_cast<int>(scaleArray->size()); ++scale)
    {
        const auto* definition = chordengine::core::ScaleSystem::findByIndex(scale);
        require(definition != nullptr && definition->name == (*scaleArray)[static_cast<std::size_t>(scale)].asString(),
                "scale name matches source table");
        for (int degree = 0; degree < 7; ++degree)
            require(definition->semitoneOffsetsFromRoot[static_cast<std::size_t>(degree)]
                        == integer(at(at(scaleDegrees, static_cast<std::size_t>(scale), "scaleDegrees"),
                                      static_cast<std::size_t>(degree), "scale degree")),
                    "scale interval matches source table");
    }

    for (int presetIndex = 0; presetIndex < static_cast<int>(presetArray->size()); ++presetIndex)
    {
        const auto* preset = chordengine::core::ChordPresetSystem::findByIndex(presetIndex);
        require(preset != nullptr
                    && preset->name == (*presetArray)[static_cast<std::size_t>(presetIndex)].asString(),
                "preset name matches source table");
        require(preset->voicingStyle
                    == at(presetVoicings, static_cast<std::size_t>(presetIndex), "presetVoicings").asString(),
                "preset voicing matches source table");
        const auto& expectedMap = at(triggerMaps, static_cast<std::size_t>(presetIndex), "triggerMapByPreset");
        for (int trigger = chordengine::core::ChordPresetSystem::triggerLow;
             trigger <= chordengine::core::ChordPresetSystem::triggerHigh; ++trigger)
        {
            const auto actual = chordengine::core::ChordPresetSystem::triggerCode(
                static_cast<chordengine::core::ChordPresetId>(presetIndex), trigger);
            require(actual && *actual == integer(at(expectedMap, static_cast<std::size_t>(trigger
                        - chordengine::core::ChordPresetSystem::triggerLow), "trigger map")),
                    "preset trigger map entry matches source table");
        }
    }

    std::cout << "CoreReferenceTests: verified all 8 scales and all 12 preset maps from source fixtures; hostObserved=false\n";
    return EXIT_SUCCESS;
}
