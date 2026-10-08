// Native PluginProcessor integration tests.
//
// These exercise ChordEngineAudioProcessor::processBlock directly with JUCE
// MIDI buffers - no DAW is involved. They are integration/conformance tests
// against the already-validated native core and the source-derived fixture;
// they are NOT host-parity tests and do not observe any DAW behavior.

#include "PluginProcessor.h"
#include "Core/ChordEngineCore.h"
#include "MIDI/MidiNoteTracker.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace juce
{
void juce_VerifyPlugin();
void juce_VerifyPlugin() {}
}

namespace
{
int casesPassed = 0;
int vectorMatches = 0;
int vectorTotal = 0;

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

void casePassed()
{
    ++casesPassed;
}

struct Captured
{
    juce::MidiMessage message;
    int position = 0;
};

std::vector<Captured> capture(const juce::MidiBuffer& buffer)
{
    std::vector<Captured> result;
    for (const auto metadata : buffer)
        result.push_back({ metadata.getMessage(), metadata.samplePosition });
    return result;
}

bool sameBytes(const juce::MidiMessage& a, const juce::MidiMessage& b)
{
    if (a.getRawDataSize() != b.getRawDataSize())
        return false;
    return std::equal(a.getRawData(), a.getRawData() + a.getRawDataSize(), b.getRawData());
}

// Runs one block and asserts the processor keeps audio completely silent.
void runBlock(ChordEngineAudioProcessor& processor, juce::AudioBuffer<float>& audio,
              juce::MidiBuffer& midi, const juce::String& context)
{
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            audio.setSample(channel, sample, 0.25f);

    processor.processBlock(audio, midi);

    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            require(audio.getSample(channel, sample) == 0.0f,
                    context + ": processor must keep audio silent");
}

juce::MidiMessage generatedMessage(const chordengine::midi::NoteEvent& event)
{
    if (event.kind == chordengine::midi::NoteEvent::Kind::noteOn)
        return juce::MidiMessage::noteOn(event.channel, event.note,
                                         static_cast<juce::uint8>(event.velocity));
    return juce::MidiMessage::noteOff(event.channel, event.note);
}

chordengine::midi::NoteEvent note(chordengine::midi::NoteEvent::Kind kind,
                                  std::uint8_t channel, std::uint8_t pitch,
                                  std::uint8_t velocity, int sampleOffset)
{
    chordengine::midi::NoteEvent event;
    event.kind = kind;
    event.channel = channel;
    event.note = pitch;
    event.velocity = velocity;
    event.sampleOffset = sampleOffset;
    return event;
}

void expectNotes(const std::vector<Captured>& events, const std::vector<int>& pitches,
                 bool shouldBeNoteOn, std::uint8_t channel, int position,
                 const juce::String& context)
{
    require(events.size() == pitches.size(),
            context + ": expected " + juce::String(static_cast<int>(pitches.size()))
                + " events, got " + juce::String(static_cast<int>(events.size())));
    for (std::size_t index = 0; index < pitches.size(); ++index)
    {
        const auto& event = events[index];
        require(shouldBeNoteOn ? event.message.isNoteOn() : event.message.isNoteOff(),
                context + ": event " + juce::String(static_cast<int>(index)) + " wrong kind");
        require(event.message.getNoteNumber() == pitches[index],
                context + ": event " + juce::String(static_cast<int>(index)) + " pitch mismatch");
        require(event.message.getChannel() == channel,
                context + ": event " + juce::String(static_cast<int>(index)) + " channel mismatch");
        require(event.position == position,
                context + ": event " + juce::String(static_cast<int>(index))
                    + " sample position mismatch");
    }
}

// --- JSON helpers for the source-derived fixture -----------------------------

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
}

int main()
{
    using namespace chordengine::core;
    juce::AudioBuffer<float> audio(2, 64);

    // The native development default follows the scoped Dynamic/100 decision
    // recorded in Docs/DEFAULT_BEHAVIOR_DISCREPANCY.md.
    {
        ChordEngineAudioProcessor probe;
        const auto config = probe.configurationSnapshot();
        require(config.velocityMode == VelocityMode::dynamic && config.fixedVelocity == 100,
                "current native default is Dynamic/100 (see Docs/DEFAULT_BEHAVIOR_DISCREPANCY.md)");
        require(config.preset == ChordPresetId::dreamy && config.scale == ScaleId::minor
                    && config.keyIndex == 0 && config.transpose.target == TransposeTarget::whole
                    && config.transpose.octaveSteps == 0,
                "current native default is C / Minor / Dreamy / WHOLE 0");
    }

    // 1. Valid trigger note produces the generated chord.
    {
        ChordEngineAudioProcessor processor;
        juce::MidiBuffer block;
        block.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(96)), 3);
        runBlock(processor, audio, block, "valid trigger");
        const auto events = capture(block);
        expectNotes(events, { 51, 55, 58, 62 }, true, 1, 3, "valid trigger");
        require(processor.isAnyTriggerNoteActive(60) && processor.isAnyOutputNoteActive(51)
                    && processor.activeTriggerCount() == 1 && processor.activeOutputNoteCount() == 4,
                "piano UI snapshot: trigger and generated note highlights become active");
        require(std::none_of(events.begin(), events.end(),
                             [](const Captured& event) { return event.message.getNoteNumber() == 60; }),
                "valid trigger: raw trigger pitch must not be present in output");
        casePassed();
    }

    // 2. Out-of-range trigger notes are suppressed and generate nothing.
    {
        ChordEngineAudioProcessor processor;
        juce::MidiBuffer block;
        block.addEvent(juce::MidiMessage::noteOn(1, 23, static_cast<juce::uint8>(100)), 5);
        block.addEvent(juce::MidiMessage::noteOn(1, 97, static_cast<juce::uint8>(100)), 6);
        runBlock(processor, audio, block, "invalid range");
        require(capture(block).empty(), "invalid range: no events may be produced");
        casePassed();
    }

    // 3. Raw trigger note-on is suppressed even though MIDI output is produced.
    {
        ChordEngineAudioProcessor processor;
        juce::MidiBuffer block;
        block.addEvent(juce::MidiMessage::noteOn(4, 66, static_cast<juce::uint8>(80)), 0);
        runBlock(processor, audio, block, "note-on suppression");
        const auto events = capture(block);
        require(events.size() == 4, "note-on suppression: chord replaces trigger");
        for (const auto& event : events)
            require(event.message.isNoteOn() && event.message.getNoteNumber() != 66,
                    "note-on suppression: trigger pitch is never forwarded");
        casePassed();
    }

    // 4. Matching note-off releases the stored generated pitches.
    {
        ChordEngineAudioProcessor processor;
        juce::AudioBuffer<float> localAudio(2, 64);
        juce::MidiBuffer on;
        on.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(96)), 3);
        runBlock(processor, localAudio, on, "note-off setup");
        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::noteOff(1, 60), 45);
        runBlock(processor, localAudio, off, "note-off");
        const auto events = capture(off);
        expectNotes(events, { 51, 55, 58, 62 }, false, 1, 45, "note-off");
        require(!processor.isAnyTriggerNoteActive(60) && !processor.isAnyOutputNoteActive(51)
                    && processor.activeTriggerCount() == 0 && processor.activeOutputNoteCount() == 0,
                "piano UI snapshot: note releases clear trigger and generated highlights");
        casePassed();
    }

    // 5. Processor output matches an independent ChordEngineCore run.
    {
        ChordEngineAudioProcessor processor;
        juce::MidiBuffer block;
        block.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(96)), 12);
        runBlock(processor, audio, block, "core comparison");
        const auto events = capture(block);

        ChordEngineCore core;
        const auto result = core.process(note(chordengine::midi::NoteEvent::Kind::noteOn,
                                              1, 60, 96, 12));
        require(events.size() == result.eventCount, "core comparison: event count mismatch");
        for (std::size_t index = 0; index < events.size(); ++index)
        {
            const auto expected = generatedMessage(result.events[index]);
            require(sameBytes(events[index].message, expected)
                        && events[index].position == result.events[index].sampleOffset,
                    "core comparison: event " + juce::String(static_cast<int>(index))
                        + " differs from core output");
        }
        casePassed();
    }

    // 6. Generated MIDI keeps the input MIDI channel.
    {
        for (std::uint8_t channel : { std::uint8_t { 9 }, std::uint8_t { 16 } })
        {
            ChordEngineAudioProcessor processor;
            juce::MidiBuffer block;
            block.addEvent(juce::MidiMessage::noteOn(channel, 60, static_cast<juce::uint8>(70)), 2);
            runBlock(processor, audio, block, "channel preservation");
            const auto events = capture(block);
            require(events.size() == 4, "channel preservation: chord size");
            for (const auto& event : events)
                require(event.message.getChannel() == channel,
                        "channel preservation: every generated event keeps the input channel");
        }
        casePassed();
    }

    // 7. Sample positions are preserved for generated and forwarded events.
    {
        ChordEngineAudioProcessor processor;
        juce::MidiBuffer block;
        block.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 3);
        block.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 17);
        block.addEvent(juce::MidiMessage::noteOff(1, 60), 45);
        runBlock(processor, audio, block, "sample positions");
        const auto events = capture(block);
        require(events.size() == 9, "sample positions: expected 4 ons + controller + 4 offs");
        for (std::size_t index = 0; index < 4; ++index)
            require(events[index].position == 3, "sample positions: generated note-on offset");
        require(events[4].message.isController() && events[4].position == 17,
                "sample positions: forwarded controller offset");
        for (std::size_t index = 5; index < 9; ++index)
            require(events[index].position == 45, "sample positions: generated note-off offset");
        casePassed();
    }

    // 8. Unrelated MIDI is forwarded unchanged (handled-but-untransformed and
    //    unrecognized system messages alike).
    {
        ChordEngineAudioProcessor processor;
        juce::MidiBuffer block;
        const juce::MidiMessage cc = juce::MidiMessage::controllerEvent(3, 1, 127);
        const juce::MidiMessage bend = juce::MidiMessage::pitchWheel(2, 9000);
        const juce::MidiMessage program = juce::MidiMessage::programChange(5, 12);
        const juce::MidiMessage pressure = juce::MidiMessage::aftertouchChange(6, 64, 40);
        const juce::MidiMessage system = juce::MidiMessage::songPositionPointer(9);
        block.addEvent(cc, 17);
        block.addEvent(bend, 5);
        block.addEvent(program, 9);
        block.addEvent(pressure, 21);
        block.addEvent(system, 12);
        runBlock(processor, audio, block, "unrelated MIDI");
        const auto events = capture(block);
        require(events.size() == 5, "unrelated MIDI: every input event is forwarded");
        const std::pair<juce::MidiMessage, int> expected[] = {
            { bend, 5 }, { program, 9 }, { system, 12 }, { cc, 17 }, { pressure, 21 }
        };
        for (std::size_t index = 0; index < events.size(); ++index)
        {
            require(events[index].position == expected[index].second,
                    "unrelated MIDI: forwarded sample position changed");
            require(sameBytes(events[index].message, expected[index].first),
                    "unrelated MIDI: forwarded bytes changed");
        }
        casePassed();
    }

    // 9. Sustain: CC64 holds generated voices until pedal-up.
    {
        ChordEngineAudioProcessor processor;
        juce::AudioBuffer<float> localAudio(2, 64);
        juce::MidiBuffer down;
        down.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
        down.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(90)), 4);
        runBlock(processor, localAudio, down, "sustain down");
        juce::MidiBuffer release;
        release.addEvent(juce::MidiMessage::noteOff(1, 60), 28);
        runBlock(processor, localAudio, release, "sustain note-off");
        require(capture(release).empty(), "sustain: note-off under pedal emits no releases");
        require(processor.trackedNoteSustained(1, 60), "sustain: input state shows held trigger");
        require(processor.triggerNoteSustained(1, 60) && processor.isTriggerNoteActive(1, 60)
                    && !processor.isTriggerNoteDown(1, 60),
                "sustain: UI snapshot distinguishes a sustained trigger from a physically held key");
        juce::MidiBuffer up;
        up.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 6);
        runBlock(processor, localAudio, up, "sustain up");
        const auto events = capture(up);
        require(events.size() == 5, "sustain up: pedal event itself is forwarded");
        require(events.front().message.isController()
                    && events.front().message.getControllerNumber() == 64
                    && events.front().position == 6,
                "sustain up: CC64 forwarded at its position");
        std::vector<Captured> noteEvents(events.begin() + 1, events.end());
        expectNotes(noteEvents, { 51, 55, 58, 62 }, false, 1, 6, "sustain up");
        require(!processor.triggerNoteSustained(1, 60) && !processor.isTriggerNoteActive(1, 60),
                "sustain up: UI snapshot clears the sustained trigger");
        casePassed();
    }

    // 10. CC120 panic releases tracked generated notes and forwards the CC.
    {
        ChordEngineAudioProcessor processor;
        juce::AudioBuffer<float> localAudio(2, 64);
        juce::MidiBuffer on;
        on.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(90)), 4);
        runBlock(processor, localAudio, on, "cc120 setup");
        juce::MidiBuffer panic;
        panic.addEvent(juce::MidiMessage::allSoundOff(1), 8);
        runBlock(processor, localAudio, panic, "cc120");
        const auto events = capture(panic);
        require(events.size() == 5, "cc120: forwarded CC plus four released notes");
        require(events.front().message.isController()
                    && events.front().message.getControllerNumber() == 120
                    && events.front().position == 8,
                "cc120: original controller is forwarded at its position");
        std::vector<Captured> noteEvents(events.begin() + 1, events.end());
        expectNotes(noteEvents, { 51, 55, 58, 62 }, false, 1, 8, "cc120");
        casePassed();
    }

    // 11. CC123 panic releases tracked generated notes and forwards the CC.
    {
        ChordEngineAudioProcessor processor;
        juce::AudioBuffer<float> localAudio(2, 64);
        juce::MidiBuffer on;
        on.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(90)), 4);
        runBlock(processor, localAudio, on, "cc123 setup");
        juce::MidiBuffer panic;
        panic.addEvent(juce::MidiMessage::allNotesOff(1), 8);
        runBlock(processor, localAudio, panic, "cc123");
        const auto events = capture(panic);
        require(events.size() == 5, "cc123: forwarded CC plus four released notes");
        require(events.front().message.isController()
                    && events.front().message.getControllerNumber() == 123,
                "cc123: original controller is forwarded");
        std::vector<Captured> noteEvents(events.begin() + 1, events.end());
        expectNotes(noteEvents, { 51, 55, 58, 62 }, false, 1, 8, "cc123");
        casePassed();
    }

    // 12. Multiple simultaneous inputs in one block stay independent and ordered.
    {
        ChordEngineAudioProcessor processor;
        juce::MidiBuffer block;
        block.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(96)), 4);
        block.addEvent(juce::MidiMessage::noteOn(1, 72, static_cast<juce::uint8>(100)), 4);
        block.addEvent(juce::MidiMessage::noteOn(2, 64, static_cast<juce::uint8>(80)), 11);
        runBlock(processor, audio, block, "simultaneous inputs");
        const auto events = capture(block);
        require(events.size() == 12, "simultaneous inputs: three triggers -> twelve note-ons");

        // Expected pitches are derived from an independent core run per input,
        // so this compares the processor against the validated core rather than
        // inventing harmony.
        ChordEngineCore reference;
        const auto first = reference.process(note(chordengine::midi::NoteEvent::Kind::noteOn,
                                                  1, 60, 96, 4));
        ChordEngineCore referenceSecond;
        const auto second = referenceSecond.process(note(chordengine::midi::NoteEvent::Kind::noteOn,
                                                         1, 72, 100, 4));
        ChordEngineCore referenceThird;
        const auto third = referenceThird.process(note(chordengine::midi::NoteEvent::Kind::noteOn,
                                                       2, 64, 80, 11));
        std::vector<chordengine::midi::NoteEvent> expected;
        for (std::size_t i = 0; i < first.eventCount; ++i) expected.push_back(first.events[i]);
        for (std::size_t i = 0; i < second.eventCount; ++i) expected.push_back(second.events[i]);
        for (std::size_t i = 0; i < third.eventCount; ++i) expected.push_back(third.events[i]);
        require(events.size() == expected.size(), "simultaneous inputs: core count mismatch");
        for (std::size_t index = 0; index < events.size(); ++index)
        {
            const auto message = generatedMessage(expected[index]);
            require(sameBytes(events[index].message, message)
                        && events[index].position == expected[index].sampleOffset,
                    "simultaneous inputs: event " + juce::String(static_cast<int>(index))
                        + " differs from core output");
        }
        casePassed();
    }

    // 13. Repeated identical processing produces identical output.
    {
        const auto runSequence = [&audio]() {
            ChordEngineAudioProcessor processor;
            processor.prepareToPlay(48000.0, 64);
            std::vector<Captured> all;
            juce::MidiBuffer first;
            first.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
            first.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(96)), 4);
            first.addEvent(juce::MidiMessage::controllerEvent(1, 1, 64), 9);
            runBlock(processor, audio, first, "determinism block 1");
            auto part = capture(first);
            all.insert(all.end(), part.begin(), part.end());

            juce::MidiBuffer second;
            second.addEvent(juce::MidiMessage::noteOff(1, 60), 6);
            runBlock(processor, audio, second, "determinism block 2");
            part = capture(second);
            all.insert(all.end(), part.begin(), part.end());

            juce::MidiBuffer third;
            third.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 2);
            third.addEvent(juce::MidiMessage::allNotesOff(1), 10);
            runBlock(processor, audio, third, "determinism block 3");
            part = capture(third);
            all.insert(all.end(), part.begin(), part.end());
            return all;
        };

        const auto runA = runSequence();
        const auto runB = runSequence();
        require(!runA.empty(), "determinism: sequence produced events");
        require(runA.size() == runB.size(), "determinism: event count differs between runs");
        for (std::size_t index = 0; index < runA.size(); ++index)
            require(runA[index].position == runB[index].position
                        && sameBytes(runA[index].message, runB[index].message),
                    "determinism: event " + juce::String(static_cast<int>(index))
                        + " differs between repeated runs");
        casePassed();
    }

    // 14. All GUI-exposed controls update the in-memory state and the next
    //     audio-thread block applies that state to deterministic MIDI processing.
    {
        ChordEngineAudioProcessor processor;
        require(processor.setKeyIndex(2), "GUI state: set key");
        require(processor.setScale(ScaleId::major), "GUI state: set scale");
        require(processor.setChordPreset(ChordPresetId::basic), "GUI state: set preset");
        require(processor.setTransposeTarget(TransposeTarget::highest), "GUI state: set target");
        require(processor.setTransposeOctave(2), "GUI state: set octave");
        require(processor.setTransposeOctave(-2), "GUI state: set lower octave boundary");
        require(processor.setTransposeOctave(2), "GUI state: set upper octave boundary");
        require(processor.setVelocityMode(VelocityMode::fixed), "GUI state: set velocity mode");
        require(processor.setFixedVelocity(73), "GUI state: set fixed velocity");
        require(processor.setFixedVelocity(1) && processor.setFixedVelocity(127)
                    && processor.setFixedVelocity(73),
                "GUI state: fixed velocity slider values span 1 through 127");
        const auto configuration = processor.configurationSnapshot();
        require(configuration.keyIndex == 2 && configuration.scale == ScaleId::major
                    && configuration.preset == ChordPresetId::basic
                    && configuration.transpose.target == TransposeTarget::highest
                    && configuration.transpose.octaveSteps == 2
                    && configuration.velocityMode == VelocityMode::fixed
                    && configuration.fixedVelocity == 73,
                "GUI state: every accessor reflects its selected value");
        const auto pluginState = processor.stateSnapshot();
        require(pluginState.configuration.keyIndex == 2
                    && pluginState.configuration.fixedVelocity == 73,
                "GUI state: explicit plugin state snapshot reflects configuration");

        juce::MidiBuffer block;
        block.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(67)), 5);
        runBlock(processor, audio, block, "GUI state processing");
        const auto actual = capture(block);
        ChordEngineCore reference;
        require(reference.setConfiguration(configuration), "GUI state: reference accepts config");
        const auto expectedResult = reference.process(note(
            chordengine::midi::NoteEvent::Kind::noteOn, 1, 60, 67, 5));
        require(actual.size() == expectedResult.eventCount,
                "GUI state processing: output size follows selected configuration");
        for (std::size_t index = 0; index < actual.size(); ++index)
            require(sameBytes(actual[index].message, generatedMessage(expectedResult.events[index]))
                        && actual[index].position == expectedResult.events[index].sampleOffset,
                    "GUI state processing: generated event reflects selected configuration");
        for (const auto& event : actual)
            require(event.message.getVelocity() == 73,
                    "GUI state processing: fixed velocity is applied on the next block");

        require(processor.setVelocityMode(VelocityMode::maximum), "GUI state: maximum velocity mode");
        juce::MidiBuffer maximumBlock;
        maximumBlock.addEvent(juce::MidiMessage::noteOn(1, 61, static_cast<juce::uint8>(67)), 8);
        runBlock(processor, audio, maximumBlock, "GUI maximum velocity");
        for (const auto& event : capture(maximumBlock))
            require(event.message.getVelocity() == 127,
                    "GUI state processing: Maximum mode sets generated velocity to 127");

        require(processor.setVelocityMode(VelocityMode::dynamic), "GUI state: Dynamic velocity mode");
        juce::MidiBuffer dynamicBlock;
        dynamicBlock.addEvent(juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(67)), 9);
        runBlock(processor, audio, dynamicBlock, "GUI dynamic velocity");
        for (const auto& event : capture(dynamicBlock))
            require(event.message.getVelocity() == 67,
                    "GUI state processing: Dynamic mode follows input velocity");

        require(!processor.setKeyIndex(12) && !processor.setTransposeOctave(3)
                    && !processor.setFixedVelocity(0),
                "GUI state: invalid control values are rejected");
        casePassed();
    }

    // 15. Compare processor output with every source-derived vector whose
    //     configuration equals the native default. These are source-derived
    //     calculation fixtures (hostObserved=false), not host captures.
    {
        const juce::File fixtureFile(CHORDENGINE_FL_GOLDEN_FILE);
        require(fixtureFile.existsAsFile(), "FLReferenceVectors.json exists");
        const auto fixture = juce::JSON::parse(fixtureFile);
        require(fixture.isObject(), "FLReferenceVectors.json parses");
        require(field(fixture, "hostObserved") == juce::var(false),
                "fixture remains source-derived, not host-observed");
        const auto defaults = field(fixture, "defaultStateSource");
        require(text(defaults, "velocityMode") == "dynamic" && integer(defaults, "fixedVelocity") == 100,
                "fixture default remains Dynamic/100 (see Docs/DEFAULT_BEHAVIOR_DISCREPANCY.md)");

        const auto casesVar = field(fixture, "cases");
        const auto* cases = casesVar.getArray();
        require(cases != nullptr && cases->size() == 38, "38 source-derived cases present");

        for (const auto& item : *cases)
        {
            const auto input = field(item, "input");
            const auto transpose = field(item, "transpose");
            const bool matchesDefault =
                integer(input, "keyIndex") == 0 && integer(input, "scaleIndex") == 1
                && integer(input, "presetIndex") == 4
                && text(transpose, "target") == "WHOLE"
                && integer(transpose, "octaveSteps") == 0
                && text(item, "velocityMode") == "dynamic";
            if (!matchesDefault)
                continue;
            ++vectorTotal;

            const auto id = juce::String(text(item, "id"));
            const auto channel = static_cast<int>(integer(input, "channel"));
            const auto trigger = static_cast<int>(integer(input, "note"));
            const auto velocity = static_cast<int>(integer(input, "velocity"));
            const auto expected = field(item, "expected");
            const auto expectedNotes = integers(field(expected, "notes"), id + " notes");
            const auto expectedOffs = integers(field(expected, "noteOffs"), id + " note-offs");
            const auto outputChannel = static_cast<int>(integer(expected, "outputChannel"));
            const auto expectedVelocityField = field(expected, "velocity");
            const auto expectedVelocity = expectedVelocityField.isVoid()
                ? 0 : static_cast<int>(expectedVelocityField);

            ChordEngineAudioProcessor processor;
            juce::AudioBuffer<float> localAudio(2, 64);

            juce::MidiBuffer on;
            on.addEvent(juce::MidiMessage::noteOn(channel, trigger,
                                                   static_cast<juce::uint8>(velocity)), 7);
            runBlock(processor, localAudio, on, id + " note-on");
            const auto onEvents = capture(on);
            require(onEvents.size() == expectedNotes.size(), id + ": generated note count");
            for (std::size_t index = 0; index < onEvents.size(); ++index)
            {
                require(onEvents[index].message.isNoteOn()
                            && onEvents[index].message.getNoteNumber() == expectedNotes[index],
                        id + ": generated pitch/order at index "
                            + juce::String(static_cast<int>(index)));
                require(onEvents[index].message.getChannel() == outputChannel,
                        id + ": generated channel");
                require(onEvents[index].message.getVelocity() == expectedVelocity,
                        id + ": generated velocity");
                require(onEvents[index].position == 7, id + ": native sample position preserved");
            }

            juce::MidiBuffer off;
            off.addEvent(juce::MidiMessage::noteOff(channel, trigger), 13);
            runBlock(processor, localAudio, off, id + " note-off");
            const auto offEvents = capture(off);
            require(offEvents.size() == expectedOffs.size(), id + ": release note count");
            for (std::size_t index = 0; index < offEvents.size(); ++index)
            {
                require(offEvents[index].message.isNoteOff()
                            && offEvents[index].message.getNoteNumber() == expectedOffs[index],
                        id + ": release pitch/order at index "
                            + juce::String(static_cast<int>(index)));
                require(offEvents[index].message.getChannel() == outputChannel,
                        id + ": release channel");
                require(offEvents[index].position == 13, id + ": release sample position");
            }
            ++vectorMatches;
        }
        require(vectorMatches >= 5,
                "expected at least five default-configuration fixture cases");
        casePassed();
    }

    // 16. Recorder arms, captures only generated notes, and exports a valid MIDI file.
    {
        ChordEngineAudioProcessor processor;
        processor.prepareToPlay(48000.0, 64);
        juce::AudioBuffer<float> localAudio(2, 64);
        require(processor.armRecorder() && processor.recorderState() == 1,
                "recorder: arm enters waiting state");
        require(!processor.armRecorder(), "recorder: cannot arm twice");
        juce::MidiBuffer attack;
        attack.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(90)), 0);
        runBlock(processor, localAudio, attack, "recorder generated attack");
        require(processor.isRecorderRecording() && processor.recorderRecordedEventCount() == 4,
                "recorder: first generated chord starts capture and excludes raw trigger");
        juce::MidiBuffer release;
        release.addEvent(juce::MidiMessage::noteOff(1, 60), 48);
        runBlock(processor, localAudio, release, "recorder generated release");
        require(processor.recorderRecordedEventCount() == 8,
                "recorder: generated chord releases are captured");
        require(processor.stopRecorder() && processor.recorderState() == 0
                    && processor.recorderTakeCount() == 1,
                "recorder: stop stores the completed MIDI take");
        auto exportFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("ChordEngine-Integration-Recorder.mid");
        exportFile.deleteFile();
        require(processor.exportRecorderTake(0, exportFile), "recorder: stored take exports as MIDI file");
        juce::FileInputStream input(exportFile);
        juce::MidiFile midiFile;
        require(input.openedOk() && midiFile.readFrom(input, false),
                "recorder: exported MIDI file is readable");
        require(midiFile.getNumTracks() == 1 && midiFile.getTrack(0) != nullptr,
                "recorder: export has one MIDI track");
        const auto* exportedTrack = midiFile.getTrack(0);
        require(exportedTrack->getNumEvents() >= 8,
                "recorder: exported MIDI track contains the generated note events");
        int exportedNoteOns = 0;
        int exportedNoteOffs = 0;
        for (int index = 0; index < exportedTrack->getNumEvents(); ++index)
        {
            const auto message = exportedTrack->getEventPointer(index)->message;
            exportedNoteOns += message.isNoteOn() ? 1 : 0;
            exportedNoteOffs += message.isNoteOff() ? 1 : 0;
        }
        require(exportedNoteOns == 4 && exportedNoteOffs == 4,
                "recorder: export preserves four generated notes and paired releases");
        std::vector<std::pair<double, int>> exportedNotes;
        for (int index = 0; index < exportedTrack->getNumEvents(); ++index)
        {
            const auto* exported = exportedTrack->getEventPointer(index);
            if (exported->message.isNoteOn() || exported->message.isNoteOff())
                exportedNotes.emplace_back(exported->message.getTimeStamp(), exported->message.getNoteNumber());
        }
        require(exportedNotes.size() == 8, "recorder: one onset/release pair for each generated note");
        constexpr std::array<int, 4> expectedChord {{ 51, 55, 58, 62 }};
        for (std::size_t index = 0; index < expectedChord.size(); ++index)
        {
            require(exportedNotes[index].second == expectedChord[index]
                        && exportedNotes[index].first == 0.0,
                    "recorder: first chord onsets are at tick zero");
            require(exportedNotes[index + 4].second == expectedChord[index]
                        && exportedNotes[index + 4].first == 4.0,
                    "recorder: explicit generated releases preserve sample-derived MIDI tick timing");
        }
        require(!processor.exportRecorderTake(1, exportFile),
                "recorder: out-of-range take index is rejected");
        exportFile.deleteFile();

        ChordEngineAudioProcessor stoppedWithHeldChord;
        stoppedWithHeldChord.prepareToPlay(48000.0, 64);
        require(stoppedWithHeldChord.armRecorder(), "recorder: open-note test arms");
        juce::MidiBuffer heldAttack;
        heldAttack.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(90)), 0);
        runBlock(stoppedWithHeldChord, localAudio, heldAttack, "recorder stop-open attack");
        require(stoppedWithHeldChord.stopRecorder(), "recorder: stopping an open take stores it");
        require(stoppedWithHeldChord.exportRecorderTake(0, exportFile),
                "recorder: stop-closed take exports");
        juce::FileInputStream stoppedInput(exportFile);
        juce::MidiFile stoppedMidi;
        require(stoppedInput.openedOk() && stoppedMidi.readFrom(stoppedInput, false)
                    && stoppedMidi.getNumTracks() == 1,
                "recorder: stop-closed take is readable MIDI");
        std::vector<std::pair<double, int>> stoppedEvents;
        const auto* stoppedTrack = stoppedMidi.getTrack(0);
        for (int index = 0; index < stoppedTrack->getNumEvents(); ++index)
        {
            const auto* event = stoppedTrack->getEventPointer(index);
            if (event->message.isNoteOn() || event->message.isNoteOff())
                stoppedEvents.emplace_back(event->message.getTimeStamp(), event->message.getNoteNumber());
        }
        require(stoppedEvents.size() == 8, "recorder: stop pairs every still-open chord note");
        for (std::size_t index = 0; index < 4; ++index)
            require(stoppedEvents[index].second == expectedChord[index]
                        && stoppedEvents[index].first == 0.0
                        && stoppedEvents[index + 4].second == expectedChord[index]
                        && stoppedEvents[index + 4].first == 12.0,
                    "recorder: stop emits matched note-offs with a 12-tick minimum");
        exportFile.deleteFile();
        casePassed();
    }

    // 17. Recorder cancels empty takes and keeps a rolling three-take history.
    {
        ChordEngineAudioProcessor processor;
        processor.prepareToPlay(44100.0, 64);
        require(processor.armRecorder() && processor.cancelRecorder()
                    && processor.recorderTakeCount() == 0,
                "recorder: cancel before the first chord discards the empty take");
        juce::AudioBuffer<float> localAudio(2, 64);
        for (int take = 0; take < 4; ++take)
        {
            require(processor.armRecorder(), "recorder: next take arms");
            juce::MidiBuffer attack;
            attack.addEvent(juce::MidiMessage::noteOn(1, 60 + take, static_cast<juce::uint8>(90)), 0);
            runBlock(processor, localAudio, attack, "recorder rolling take attack");
            juce::MidiBuffer release;
            release.addEvent(juce::MidiMessage::noteOff(1, 60 + take), 32);
            runBlock(processor, localAudio, release, "recorder rolling take release");
            require(processor.stopRecorder(), "recorder: each nonempty take finalizes");
        }
        require(processor.recorderTakeCount() == 3,
                "recorder: completed takes are capped at newest three");
        auto exportFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("ChordEngine-Integration-Recorder-Order.mid");
        exportFile.deleteFile();
        for (std::uint8_t takeIndex = 0; takeIndex < 3; ++takeIndex)
        {
            require(processor.exportRecorderTake(takeIndex, exportFile),
                    "recorder: each retained take can be exported");
            juce::FileInputStream input(exportFile);
            juce::MidiFile exported;
            require(input.openedOk() && exported.readFrom(input, false)
                        && exported.getNumTracks() == 1 && exported.getTrack(0) != nullptr,
                    "recorder: retained take is a valid MIDI file");
            const auto* track = exported.getTrack(0);
            int firstPitch = -1;
            for (int event = 0; event < track->getNumEvents(); ++event)
                if (track->getEventPointer(event)->message.isNoteOn())
                {
                    firstPitch = track->getEventPointer(event)->message.getNoteNumber();
                    break;
                }
            require(firstPitch >= 0, "recorder: retained take has a generated note-on");
            constexpr std::array<int, 3> expectedFirstPitches {{ 53, 53, 51 }};
            require(firstPitch == expectedFirstPitches[takeIndex],
                    "recorder: retained takes are ordered newest-first; expected first pitch "
                        + juce::String(expectedFirstPitches[takeIndex]) + ", got " + juce::String(firstPitch));
            exportFile.deleteFile();
        }
        require(!processor.exportRecorderTake(3, exportFile),
                "recorder: index beyond three retained takes is rejected");
        casePassed();
    }

    constexpr int expectedCaseCount = 17;
    require(casesPassed == expectedCaseCount,
            "every integration case must run: expected "
                + juce::String(expectedCaseCount) + ", executed "
                + juce::String(casesPassed));
    require(vectorMatches == vectorTotal && vectorMatches > 0,
            "every eligible source-derived vector must match");
    std::cout << "ChordEngineProcessorIntegrationTests: " << casesPassed << "/" << expectedCaseCount
              << " native integration cases passed; " << vectorMatches << "/" << vectorTotal
              << " source-derived vector comparisons matched (hostObserved=false; not host-parity)\n";
    return EXIT_SUCCESS;
}
