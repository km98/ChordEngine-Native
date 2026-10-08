#include "Core/ChordEngineCore.h"
#include "Core/ScaleSystem.h"
#include "MIDI/MidiNoteTracker.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

int integer(const juce::var& object, const juce::Identifier& key)
{
    const auto value = field(object, key);
    require(value.isInt() || value.isInt64(), "Expected integer field " + key.toString());
    return static_cast<int>(value);
}

std::string text(const juce::var& object, const juce::Identifier& key)
{
    return field(object, key).toString().toStdString();
}

std::vector<int> integers(const juce::var& value, const juce::String& name)
{
    const auto* array = value.getArray();
    require(array != nullptr, "Expected integer array: " + name);
    std::vector<int> result;
    for (const auto& item : *array)
    {
        require(item.isInt() || item.isInt64(), "Expected integer in " + name);
        result.push_back(static_cast<int>(item));
    }
    return result;
}

chordengine::core::TransposeTarget transposeTarget(std::string_view name)
{
    using chordengine::core::TransposeTarget;
    if (name == "WHOLE") return TransposeTarget::whole;
    if (name == "LOWEST") return TransposeTarget::lowest;
    if (name == "HIGHEST") return TransposeTarget::highest;
    fail("Unknown transpose target in fixture: " + juce::String(name.data()));
}

chordengine::midi::NoteEvent inputEvent(chordengine::midi::NoteEvent::Kind kind,
                                        const juce::var& input)
{
    chordengine::midi::NoteEvent event;
    event.kind = kind;
    event.channel = static_cast<std::uint8_t>(integer(input, "channel"));
    event.note = static_cast<std::uint8_t>(integer(input, "note"));
    event.velocity = static_cast<std::uint8_t>(integer(input, "velocity"));
    return event;
}

void checkPitches(const chordengine::core::CoreResult& result, const juce::var& expected,
                  std::uint8_t channel, chordengine::midi::NoteEvent::Kind kind,
                  std::uint8_t velocity, int sampleOffset, const juce::String& id)
{
    const auto pitches = integers(expected, id);
    require(result.eventCount == pitches.size(), id + ": event count mismatch: expected "
            + juce::String(static_cast<int>(pitches.size())) + ", actual "
            + juce::String(static_cast<int>(result.eventCount)));
    for (std::size_t index = 0; index < pitches.size(); ++index)
    {
        const auto& event = result.events[index];
        require(event.kind == kind && event.note == pitches[index],
                id + ": pitch/order mismatch at event " + juce::String(static_cast<int>(index)));
        require(event.channel == channel, id + ": channel mismatch at event "
                + juce::String(static_cast<int>(index)));
        require(event.velocity == velocity, id + ": velocity mismatch at event "
                + juce::String(static_cast<int>(index)));
        require(event.sampleOffset == sampleOffset, id + ": sample offset mismatch at event "
                + juce::String(static_cast<int>(index)));
    }
}

bool configure(chordengine::core::ChordEngineCore& core, const juce::var& item)
{
    using namespace chordengine::core;
    const auto input = field(item, "input");
    const auto transpose = field(item, "transpose");
    const auto presetIndex = integer(input, "presetIndex");
    const auto scaleIndex = integer(input, "scaleIndex");
    if (presetIndex < 0 || presetIndex >= static_cast<int>(ChordPresetSystem::presetCount)
        || scaleIndex < 0 || scaleIndex >= static_cast<int>(ScaleSystem::scaleCount))
        return false;

    CoreConfiguration config;
    config.preset = static_cast<ChordPresetId>(presetIndex);
    config.scale = static_cast<ScaleId>(scaleIndex);
    config.keyIndex = integer(input, "keyIndex");
    config.transpose.target = transposeTarget(text(transpose, "target"));
    config.transpose.octaveSteps = integer(transpose, "octaveSteps");
    const auto mode = text(item, "velocityMode");
    if (mode == "dynamic") config.velocityMode = VelocityMode::dynamic;
    else if (mode == "maximum") config.velocityMode = VelocityMode::maximum;
    else if (mode == "fixed") config.velocityMode = VelocityMode::fixed;
    else return false;
    config.fixedVelocity = static_cast<std::uint8_t>(integer(item, "fixedVelocity"));
    return core.setConfiguration(config);
}
}

int main()
{
    const juce::File fixtureFile(CHORDENGINE_FL_GOLDEN_FILE);
    require(fixtureFile.existsAsFile(), "FLReferenceVectors.json exists");
    const auto fixture = juce::JSON::parse(fixtureFile);
    require(fixture.isObject(), "FLReferenceVectors.json parses as an object");
    require(field(fixture, "hostObserved") == juce::var(false),
            "fixture remains classified as source-derived, not host-observed");
    const auto casesVar = field(fixture, "cases");
    const auto* cases = casesVar.getArray();
    require(cases != nullptr && cases->size() == 38, "38 source-derived cases are present");

    std::size_t matched = 0;
    std::size_t unresolved = 0;
    const auto assumptions = field(fixture, "vectorScenarioAssumption").toString();
    const bool isolatedVoiceVectors = assumptions.contains("single isolated trigger")
        && assumptions.contains("sustain is off");
    if (!isolatedVoiceVectors)
        unresolved = static_cast<std::size_t>(cases->size());
    for (const auto& item : *cases)
    {
        const auto id = text(item, "id");
        const auto idText = juce::String(id);
        if (!isolatedVoiceVectors)
        {
            std::cout << "UNRESOLVED " << id << ": fixture voice/sustain assumptions are not explicit\\n";
            continue;
        }
        const auto input = field(item, "input");
        const auto expected = field(item, "expected");
        chordengine::core::ChordEngineCore core;
        require(configure(core, item), idText + ": configuration rejected");

        const auto on = core.process(inputEvent(chordengine::midi::NoteEvent::Kind::noteOn, input));
        require(on.supported && on.suppressInput, idText + ": valid MIDI note-on not suppressed");
        require(!on.unresolved, idText + ": note-on unexpectedly unresolved");
        const auto expectedNotes = field(expected, "notes");
        const auto expectedPitchList = integers(expectedNotes, idText + " expected notes");
        const auto expectedVelocity = field(expected, "velocity");
        const auto velocity = expectedVelocity.isVoid()
            ? std::uint8_t { 0 } : static_cast<std::uint8_t>(static_cast<int>(expectedVelocity));
        if (expectedPitchList.empty())
            require(expectedVelocity.isVoid(), idText + ": empty chord should not define velocity");
        checkPitches(on, expectedNotes,
            static_cast<std::uint8_t>(integer(expected, "outputChannel")),
            chordengine::midi::NoteEvent::Kind::noteOn, velocity,
            0, idText + " note-ons");

        const auto off = core.process(inputEvent(chordengine::midi::NoteEvent::Kind::noteOff, input));
        require(off.supported && off.suppressInput && !off.unresolved,
                idText + ": corresponding note-off was not handled deterministically");
        checkPitches(off, field(expected, "noteOffs"),
            static_cast<std::uint8_t>(integer(expected, "outputChannel")),
            chordengine::midi::NoteEvent::Kind::noteOff, 0, 0, idText + " note-offs");
        ++matched;
    }

    std::cout << "ChordEngineFLReferenceVectorTests: " << matched << "/" << cases->size()
              << " source-derived native-core vectors matched; hostObserved=false; unresolved="
              << unresolved << '\n';
    return EXIT_SUCCESS;
}
