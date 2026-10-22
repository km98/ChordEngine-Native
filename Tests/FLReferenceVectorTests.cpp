#include "Core/ChordEngineCore.h"
#include "Core/ScaleSystem.h"
#include "MIDI/MidiNoteTracker.h"

#include "TestSupport.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace chordengine::test;
using chordengine::platform::Json;

namespace
{
int integerField(const Json& object, const std::string& key)
{
    return chordengine::test::integer(field(object, key), key);
}

std::string text(const Json& object, const std::string& key)
{
    return field(object, key).asString();
}

std::vector<int> integers(const Json& value, const std::string& name)
{
    require(value.isArray(), "Expected integer array: " + name);
    std::vector<int> result;
    for (const auto& item : value.items())
    {
        require(item.isInteger(), "Expected integer in " + name);
        result.push_back(static_cast<int>(item.asInt()));
    }
    return result;
}

chordengine::core::TransposeTarget transposeTarget(std::string_view name)
{
    using chordengine::core::TransposeTarget;
    if (name == "WHOLE") return TransposeTarget::whole;
    if (name == "LOWEST") return TransposeTarget::lowest;
    if (name == "HIGHEST") return TransposeTarget::highest;
    fail("Unknown transpose target in fixture: " + std::string(name));
}

chordengine::midi::NoteEvent inputEvent(chordengine::midi::NoteEvent::Kind kind,
                                        const Json& input)
{
    chordengine::midi::NoteEvent event;
    event.kind = kind;
    event.channel = static_cast<std::uint8_t>(integerField(input, "channel"));
    event.note = static_cast<std::uint8_t>(integerField(input, "note"));
    event.velocity = static_cast<std::uint8_t>(integerField(input, "velocity"));
    return event;
}

void checkPitches(const chordengine::core::CoreResult& result, const Json& expected,
                  std::uint8_t channel, chordengine::midi::NoteEvent::Kind kind,
                  std::uint8_t velocity, int sampleOffset, const std::string& id)
{
    const auto pitches = integers(expected, id);
    require(result.eventCount == pitches.size(), id + ": event count mismatch: expected "
            + str(static_cast<int>(pitches.size())) + ", actual "
            + str(static_cast<int>(result.eventCount)));
    for (std::size_t index = 0; index < pitches.size(); ++index)
    {
        const auto& event = result.events[index];
        require(event.kind == kind && event.note == pitches[index],
                id + ": pitch/order mismatch at event " + str(static_cast<int>(index)));
        require(event.channel == channel, id + ": channel mismatch at event "
                + str(static_cast<int>(index)));
        require(event.velocity == velocity, id + ": velocity mismatch at event "
                + str(static_cast<int>(index)));
        require(event.sampleOffset == sampleOffset, id + ": sample offset mismatch at event "
                + str(static_cast<int>(index)));
    }
}

bool configure(chordengine::core::ChordEngineCore& core, const Json& item)
{
    using namespace chordengine::core;
    const auto& input = field(item, "input");
    const auto& transpose = field(item, "transpose");
    const auto presetIndex = integerField(input, "presetIndex");
    const auto scaleIndex = integerField(input, "scaleIndex");
    if (presetIndex < 0 || presetIndex >= static_cast<int>(ChordPresetSystem::presetCount)
        || scaleIndex < 0 || scaleIndex >= static_cast<int>(ScaleSystem::scaleCount))
        return false;

    CoreConfiguration config;
    config.preset = static_cast<ChordPresetId>(presetIndex);
    config.scale = static_cast<ScaleId>(scaleIndex);
    config.keyIndex = integerField(input, "keyIndex");
    config.transpose.target = transposeTarget(text(transpose, "target"));
    config.transpose.octaveSteps = integerField(transpose, "octaveSteps");
    const auto mode = text(item, "velocityMode");
    if (mode == "dynamic") config.velocityMode = VelocityMode::dynamic;
    else if (mode == "maximum") config.velocityMode = VelocityMode::maximum;
    else if (mode == "fixed") config.velocityMode = VelocityMode::fixed;
    else return false;
    config.fixedVelocity = static_cast<std::uint8_t>(integerField(item, "fixedVelocity"));
    return core.setConfiguration(config);
}
}

int main()
{
    const auto fixture = parseJson(readText(CHORDENGINE_FL_GOLDEN_FILE));
    require(fixture.isObject(), "FLReferenceVectors.json parses as an object");
    require(field(fixture, "hostObserved").isBool() && !field(fixture, "hostObserved").asBool(),
            "fixture remains classified as source-derived, not host-observed");
    const auto& casesVar = field(fixture, "cases");
    const auto* cases = casesVar.isArray() ? &casesVar.items() : nullptr;
    require(cases != nullptr && cases->size() == 38, "38 source-derived cases are present");

    std::size_t matched = 0;
    std::size_t unresolved = 0;
    const auto assumptions = field(fixture, "vectorScenarioAssumption").asString();
    const bool isolatedVoiceVectors = assumptions.find("single isolated trigger") != std::string::npos
        && assumptions.find("sustain is off") != std::string::npos;
    if (!isolatedVoiceVectors)
        unresolved = static_cast<std::size_t>(cases->size());
    for (const auto& item : *cases)
    {
        const auto id = text(item, "id");
        const auto& idText = id;
        if (!isolatedVoiceVectors)
        {
            std::cout << "UNRESOLVED " << id << ": fixture voice/sustain assumptions are not explicit\\n";
            continue;
        }
        const auto& input = field(item, "input");
        const auto& expected = field(item, "expected");
        chordengine::core::ChordEngineCore core;
        require(configure(core, item), idText + ": configuration rejected");

        const auto on = core.process(inputEvent(chordengine::midi::NoteEvent::Kind::noteOn, input));
        require(on.supported && on.suppressInput, idText + ": valid MIDI note-on not suppressed");
        require(!on.unresolved, idText + ": note-on unexpectedly unresolved");
        const auto& expectedNotes = field(expected, "notes");
        const auto expectedPitchList = integers(expectedNotes, idText + " expected notes");
        const auto& expectedVelocity = field(expected, "velocity");
        const auto velocity = expectedVelocity.isNull()
            ? std::uint8_t { 0 } : static_cast<std::uint8_t>(expectedVelocity.asInt());
        if (expectedPitchList.empty())
            require(expectedVelocity.isNull(), idText + ": empty chord should not define velocity");
        checkPitches(on, expectedNotes,
            static_cast<std::uint8_t>(integerField(expected, "outputChannel")),
            chordengine::midi::NoteEvent::Kind::noteOn, velocity,
            0, idText + " note-ons");

        const auto off = core.process(inputEvent(chordengine::midi::NoteEvent::Kind::noteOff, input));
        require(off.supported && off.suppressInput && !off.unresolved,
                idText + ": corresponding note-off was not handled deterministically");
        checkPitches(off, field(expected, "noteOffs"),
            static_cast<std::uint8_t>(integerField(expected, "outputChannel")),
            chordengine::midi::NoteEvent::Kind::noteOff, 0, 0, idText + " note-offs");
        ++matched;
    }

    std::cout << "ChordEngineFLReferenceVectorTests: " << matched << "/" << cases->size()
              << " source-derived native-core vectors matched; hostObserved=false; unresolved="
              << unresolved << '\n';
    return EXIT_SUCCESS;
}
