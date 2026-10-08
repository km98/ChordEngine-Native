#include "Core/ChordPresetSystem.h"
#include "Core/ScaleSystem.h"

#include <juce_core/juce_core.h>

#include <cstdlib>
#include <iostream>

namespace
{
[[noreturn]] void fail(const juce::String& message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const juce::String& message)
{
    if (!condition)
        fail(message);
}

juce::var field(const juce::var& object, const juce::Identifier& key)
{
    auto* dynamicObject = object.getDynamicObject();
    if (dynamicObject == nullptr)
        fail("Expected JSON object while reading " + key.toString());
    const auto* value = dynamicObject->getProperties().getVarPointer(key);
    if (value == nullptr)
        fail("Missing JSON field: " + key.toString());
    return *value;
}

juce::var at(const juce::var& value, int index, const juce::String& name)
{
    const auto* array = value.getArray();
    require(array != nullptr && index >= 0 && index < array->size(),
            "Expected array entry: " + name);
    return (*array)[index];
}

int integer(const juce::var& value)
{
    require(value.isInt() || value.isInt64(), "Expected integer fixture value");
    return static_cast<int>(value);
}
}

int main()
{
    const juce::File fixtureFile(CHORDENGINE_FL_GOLDEN_FILE);
    require(fixtureFile.existsAsFile(), "FLReferenceVectors.json exists");
    const auto fixture = juce::JSON::parse(fixtureFile);
    require(fixture.isObject(), "FLReferenceVectors.json parses");
    require(field(fixture, "hostObserved") == juce::var(false),
            "source-derived fixtures remain explicitly not host-observed");
    const auto tables = field(fixture, "sourceTables");
    const auto scaleNames = field(tables, "scaleNames");
    const auto scaleDegrees = field(tables, "scaleDegrees");
    const auto presetNames = field(tables, "presetNames");
    const auto presetVoicings = field(tables, "presetVoicings");
    const auto triggerMaps = field(tables, "triggerMapByPreset");
    const auto* scaleArray = scaleNames.getArray();
    const auto* presetArray = presetNames.getArray();
    require(scaleArray != nullptr && scaleArray->size() == 8,
            "fixture contains exactly eight scales");
    require(presetArray != nullptr && presetArray->size() == 12,
            "fixture contains exactly twelve presets");

    for (int scale = 0; scale < scaleArray->size(); ++scale)
    {
        const auto* definition = chordengine::core::ScaleSystem::findByIndex(scale);
        require(definition != nullptr && definition->name == (*scaleArray)[scale].toString().toStdString(),
                "scale name matches source table");
        for (int degree = 0; degree < 7; ++degree)
            require(definition->semitoneOffsetsFromRoot[static_cast<std::size_t>(degree)]
                        == integer(at(at(scaleDegrees, scale, "scaleDegrees"), degree, "scale degree")),
                    "scale interval matches source table");
    }

    for (int presetIndex = 0; presetIndex < presetArray->size(); ++presetIndex)
    {
        const auto* preset = chordengine::core::ChordPresetSystem::findByIndex(presetIndex);
        require(preset != nullptr
                    && preset->name == (*presetArray)[presetIndex].toString().toStdString(),
                "preset name matches source table");
        require(preset->voicingStyle
                    == at(presetVoicings, presetIndex, "presetVoicings").toString().toStdString(),
                "preset voicing matches source table");
        const auto expectedMap = at(triggerMaps, presetIndex, "triggerMapByPreset");
        for (int trigger = chordengine::core::ChordPresetSystem::triggerLow;
             trigger <= chordengine::core::ChordPresetSystem::triggerHigh; ++trigger)
        {
            const auto actual = chordengine::core::ChordPresetSystem::triggerCode(
                static_cast<chordengine::core::ChordPresetId>(presetIndex), trigger);
            require(actual && *actual == integer(at(expectedMap, trigger
                        - chordengine::core::ChordPresetSystem::triggerLow, "trigger map")),
                    "preset trigger map entry matches source table");
        }
    }

    std::cout << "CoreReferenceTests: verified all 8 scales and all 12 preset maps from source fixtures; hostObserved=false\n";
    return EXIT_SUCCESS;
}
