// Native PluginProcessor integration tests.
//
// These exercise ChordEngineAudioProcessor::processBlock directly with MIDI
// buffers - no DAW is involved. They are integration/conformance tests
// against the already-validated native core and the source-derived fixture;
// they are NOT host-parity tests and do not observe any DAW behavior.

#include "MidiTestSupport.h"
#include "Core/ChordEngineCore.h"
#include "MIDI/MidiNoteTracker.h"
#include "Platform/Text.h"

#include <array>
#include <map>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int casesPassed = 0;
int vectorMatches = 0;
int vectorTotal = 0;

using chordengine::test::fail;
using chordengine::test::require;
using chordengine::test::str;
using chordengine::test::MidiBuffer;
using chordengine::test::MidiMessage;
using AudioBuffer = chordengine::test::AudioBuffer;

// The host transport each processor sees (the tests' play heads).
std::map<const ChordEngineAudioProcessor*, chordengine::test::PlayHead> playHeads;

void attachPlayHead(ChordEngineAudioProcessor& processor, chordengine::test::PlayHead playHead)
{
    playHeads[&processor] = std::move(playHead);
}

void detachPlayHead(ChordEngineAudioProcessor& processor)
{
    playHeads.erase(&processor);
}

void runProcessor(ChordEngineAudioProcessor& processor, AudioBuffer& audio, MidiBuffer& midi)
{
    const auto it = playHeads.find(&processor);
    chordengine::test::processBlock(processor, audio, midi, it != playHeads.end() ? &it->second : nullptr);
}

void casePassed()
{
    ++casesPassed;
}

struct Captured
{
    MidiMessage message;
    int position = 0;
};

std::vector<Captured> capture(const MidiBuffer& buffer)
{
    std::vector<Captured> result;
    for (const auto metadata : buffer)
        result.push_back({ metadata.getMessage(), metadata.samplePosition });
    return result;
}

bool sameBytes(const MidiMessage& a, const MidiMessage& b)
{
    if (a.getRawDataSize() != b.getRawDataSize())
        return false;
    return std::equal(a.getRawData(), a.getRawData() + a.getRawDataSize(), b.getRawData());
}

// Runs one block and asserts the processor keeps audio completely silent.
// A host transport whose record state the test flips between blocks.
struct TestPlayHead
{
    bool recording = false;
    // FL Studio style: the record switch is on but the transport is stopped.
    bool recordSwitchOnly = false;
    HostTransport position() const
    {
        HostTransport info;
        info.bpm = 120.0;
        info.isPlaying = recording;
        info.isRecording = recording || recordSwitchOnly;
        return info;
    }
};

void runBlock(ChordEngineAudioProcessor& processor, AudioBuffer& audio,
              MidiBuffer& midi, const std::string& context)
{
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            audio.setSample(channel, sample, 0.25f);

    runProcessor(processor, audio, midi);

    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            require(audio.getSample(channel, sample) == 0.0f,
                    context + ": processor must keep audio silent");
}

MidiMessage generatedMessage(const chordengine::midi::NoteEvent& event)
{
    if (event.kind == chordengine::midi::NoteEvent::Kind::noteOn)
        return MidiMessage::noteOn(event.channel, event.note,
                                         static_cast<std::uint8_t>(event.velocity));
    return MidiMessage::noteOff(event.channel, event.note);
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
                 const std::string& context)
{
    require(events.size() == pitches.size(),
            context + ": expected " + str(static_cast<int>(pitches.size()))
                + " events, got " + str(static_cast<int>(events.size())));
    for (std::size_t index = 0; index < pitches.size(); ++index)
    {
        const auto& event = events[index];
        require(shouldBeNoteOn ? event.message.isNoteOn() : event.message.isNoteOff(),
                context + ": event " + str(static_cast<int>(index)) + " wrong kind");
        require(event.message.getNoteNumber() == pitches[index],
                context + ": event " + str(static_cast<int>(index)) + " pitch mismatch");
        require(event.message.getChannel() == channel,
                context + ": event " + str(static_cast<int>(index)) + " channel mismatch");
        require(event.position == position,
                context + ": event " + str(static_cast<int>(index))
                    + " sample position mismatch");
    }
}

// --- JSON helpers for the source-derived fixture -----------------------------

using chordengine::platform::Json;
using chordengine::test::field;

int integer(const Json& object, const std::string& key)
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
}

int main()
{
    using namespace chordengine::core;
    AudioBuffer audio(2, 64);

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
        MidiBuffer block;
        block.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(96)), 3);
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
        MidiBuffer block;
        block.addEvent(MidiMessage::noteOn(1, 23, static_cast<std::uint8_t>(100)), 5);
        block.addEvent(MidiMessage::noteOn(1, 97, static_cast<std::uint8_t>(100)), 6);
        runBlock(processor, audio, block, "invalid range");
        require(capture(block).empty(), "invalid range: no events may be produced");
        casePassed();
    }

    // 3. Raw trigger note-on is suppressed even though MIDI output is produced.
    {
        ChordEngineAudioProcessor processor;
        MidiBuffer block;
        block.addEvent(MidiMessage::noteOn(4, 66, static_cast<std::uint8_t>(80)), 0);
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
        AudioBuffer localAudio(2, 64);
        MidiBuffer on;
        on.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(96)), 3);
        runBlock(processor, localAudio, on, "note-off setup");
        MidiBuffer off;
        off.addEvent(MidiMessage::noteOff(1, 60), 45);
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
        MidiBuffer block;
        block.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(96)), 12);
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
                    "core comparison: event " + str(static_cast<int>(index))
                        + " differs from core output");
        }
        casePassed();
    }

    // 6. Generated MIDI keeps the input MIDI channel.
    {
        for (std::uint8_t channel : { std::uint8_t { 9 }, std::uint8_t { 16 } })
        {
            ChordEngineAudioProcessor processor;
            MidiBuffer block;
            block.addEvent(MidiMessage::noteOn(channel, 60, static_cast<std::uint8_t>(70)), 2);
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
        MidiBuffer block;
        block.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(100)), 3);
        block.addEvent(MidiMessage::controllerEvent(1, 1, 127), 17);
        block.addEvent(MidiMessage::noteOff(1, 60), 45);
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
        MidiBuffer block;
        const MidiMessage cc = MidiMessage::controllerEvent(3, 1, 127);
        const MidiMessage bend = MidiMessage::pitchWheel(2, 9000);
        const MidiMessage program = MidiMessage::programChange(5, 12);
        const MidiMessage pressure = MidiMessage::aftertouchChange(6, 64, 40);
        const MidiMessage system = MidiMessage::songPositionPointer(9);
        block.addEvent(cc, 17);
        block.addEvent(bend, 5);
        block.addEvent(program, 9);
        block.addEvent(pressure, 21);
        block.addEvent(system, 12);
        runBlock(processor, audio, block, "unrelated MIDI");
        const auto events = capture(block);
        require(events.size() == 5, "unrelated MIDI: every input event is forwarded");
        const std::pair<MidiMessage, int> expected[] = {
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
        AudioBuffer localAudio(2, 64);
        MidiBuffer down;
        down.addEvent(MidiMessage::controllerEvent(1, 64, 127), 0);
        down.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(90)), 4);
        runBlock(processor, localAudio, down, "sustain down");
        MidiBuffer release;
        release.addEvent(MidiMessage::noteOff(1, 60), 28);
        runBlock(processor, localAudio, release, "sustain note-off");
        require(capture(release).empty(), "sustain: note-off under pedal emits no releases");
        require(processor.trackedNoteSustained(1, 60), "sustain: input state shows held trigger");
        require(processor.triggerNoteSustained(1, 60) && processor.isTriggerNoteActive(1, 60)
                    && !processor.isTriggerNoteDown(1, 60),
                "sustain: UI snapshot distinguishes a sustained trigger from a physically held key");
        MidiBuffer up;
        up.addEvent(MidiMessage::controllerEvent(1, 64, 0), 6);
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
        AudioBuffer localAudio(2, 64);
        MidiBuffer on;
        on.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(90)), 4);
        runBlock(processor, localAudio, on, "cc120 setup");
        MidiBuffer panic;
        panic.addEvent(MidiMessage::allSoundOff(1), 8);
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
        AudioBuffer localAudio(2, 64);
        MidiBuffer on;
        on.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(90)), 4);
        runBlock(processor, localAudio, on, "cc123 setup");
        MidiBuffer panic;
        panic.addEvent(MidiMessage::allNotesOff(1), 8);
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
        MidiBuffer block;
        block.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(96)), 4);
        block.addEvent(MidiMessage::noteOn(1, 72, static_cast<std::uint8_t>(100)), 4);
        block.addEvent(MidiMessage::noteOn(2, 64, static_cast<std::uint8_t>(80)), 11);
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
                    "simultaneous inputs: event " + str(static_cast<int>(index))
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
            MidiBuffer first;
            first.addEvent(MidiMessage::controllerEvent(1, 64, 127), 0);
            first.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(96)), 4);
            first.addEvent(MidiMessage::controllerEvent(1, 1, 64), 9);
            runBlock(processor, audio, first, "determinism block 1");
            auto part = capture(first);
            all.insert(all.end(), part.begin(), part.end());

            MidiBuffer second;
            second.addEvent(MidiMessage::noteOff(1, 60), 6);
            runBlock(processor, audio, second, "determinism block 2");
            part = capture(second);
            all.insert(all.end(), part.begin(), part.end());

            MidiBuffer third;
            third.addEvent(MidiMessage::controllerEvent(1, 64, 0), 2);
            third.addEvent(MidiMessage::allNotesOff(1), 10);
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
                    "determinism: event " + str(static_cast<int>(index))
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

        MidiBuffer block;
        block.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(67)), 5);
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
        MidiBuffer maximumBlock;
        maximumBlock.addEvent(MidiMessage::noteOn(1, 61, static_cast<std::uint8_t>(67)), 8);
        runBlock(processor, audio, maximumBlock, "GUI maximum velocity");
        for (const auto& event : capture(maximumBlock))
            require(event.message.getVelocity() == 127,
                    "GUI state processing: Maximum mode sets generated velocity to 127");

        require(processor.setVelocityMode(VelocityMode::dynamic), "GUI state: Dynamic velocity mode");
        MidiBuffer dynamicBlock;
        dynamicBlock.addEvent(MidiMessage::noteOn(1, 62, static_cast<std::uint8_t>(67)), 9);
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
        const auto fixture = chordengine::test::parseJson(chordengine::test::readText(CHORDENGINE_FL_GOLDEN_FILE));
        require(fixture.isObject(), "FLReferenceVectors.json parses");
        require(field(fixture, "hostObserved").isBool() && !field(fixture, "hostObserved").asBool(),
                "fixture remains source-derived, not host-observed");
        const auto defaults = field(fixture, "defaultStateSource");
        require(text(defaults, "velocityMode") == "dynamic" && integer(defaults, "fixedVelocity") == 100,
                "fixture default remains Dynamic/100 (see Docs/DEFAULT_BEHAVIOR_DISCREPANCY.md)");

        const auto& casesVar = field(fixture, "cases");
        const auto* cases = casesVar.isArray() ? &casesVar.items() : nullptr;
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

            const auto id = std::string(text(item, "id"));
            const auto channel = static_cast<int>(integer(input, "channel"));
            const auto trigger = static_cast<int>(integer(input, "note"));
            const auto velocity = static_cast<int>(integer(input, "velocity"));
            const auto expected = field(item, "expected");
            const auto expectedNotes = integers(field(expected, "notes"), id + " notes");
            const auto expectedOffs = integers(field(expected, "noteOffs"), id + " note-offs");
            const auto outputChannel = static_cast<int>(integer(expected, "outputChannel"));
            const auto expectedVelocityField = field(expected, "velocity");
            const auto expectedVelocity = expectedVelocityField.isNull()
                ? 0 : static_cast<int>(expectedVelocityField.asInt());

            ChordEngineAudioProcessor processor;
            AudioBuffer localAudio(2, 64);

            MidiBuffer on;
            on.addEvent(MidiMessage::noteOn(channel, trigger,
                                                   static_cast<std::uint8_t>(velocity)), 7);
            runBlock(processor, localAudio, on, id + " note-on");
            const auto onEvents = capture(on);
            require(onEvents.size() == expectedNotes.size(), id + ": generated note count");
            for (std::size_t index = 0; index < onEvents.size(); ++index)
            {
                require(onEvents[index].message.isNoteOn()
                            && onEvents[index].message.getNoteNumber() == expectedNotes[index],
                        id + ": generated pitch/order at index "
                            + str(static_cast<int>(index)));
                require(onEvents[index].message.getChannel() == outputChannel,
                        id + ": generated channel");
                require(onEvents[index].message.getVelocity() == expectedVelocity,
                        id + ": generated velocity");
                require(onEvents[index].position == 7, id + ": native sample position preserved");
            }

            MidiBuffer off;
            off.addEvent(MidiMessage::noteOff(channel, trigger), 13);
            runBlock(processor, localAudio, off, id + " note-off");
            const auto offEvents = capture(off);
            require(offEvents.size() == expectedOffs.size(), id + ": release note count");
            for (std::size_t index = 0; index < offEvents.size(); ++index)
            {
                require(offEvents[index].message.isNoteOff()
                            && offEvents[index].message.getNoteNumber() == expectedOffs[index],
                        id + ": release pitch/order at index "
                            + str(static_cast<int>(index)));
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
        AudioBuffer localAudio(2, 64);
        require(processor.armRecorder() && processor.recorderState() == 1,
                "recorder: arm enters waiting state");
        require(!processor.armRecorder(), "recorder: cannot arm twice");
        MidiBuffer attack;
        attack.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(90)), 0);
        runBlock(processor, localAudio, attack, "recorder generated attack");
        require(processor.isRecorderRecording() && processor.recorderRecordedEventCount() == 4,
                "recorder: first generated chord starts capture and excludes raw trigger");
        MidiBuffer release;
        release.addEvent(MidiMessage::noteOff(1, 60), 48);
        runBlock(processor, localAudio, release, "recorder generated release");
        require(processor.recorderRecordedEventCount() == 8,
                "recorder: generated chord releases are captured");
        require(processor.stopRecorder() && processor.recorderState() == 0
                    && processor.recorderTakeCount() == 1,
                "recorder: stop stores the completed MIDI take");
        auto exportFile = (chordengine::platform::temporaryDirectory() / "ChordEngine-Integration-Recorder.mid");
        std::filesystem::remove(exportFile);
        require(processor.exportRecorderTake(0, exportFile), "recorder: stored take exports as MIDI file");
        chordengine::test::SmfFile midiFile;
        const bool inputRead = midiFile.readFrom(exportFile);
        require(inputRead,
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
        std::filesystem::remove(exportFile);

        ChordEngineAudioProcessor stoppedWithHeldChord;
        stoppedWithHeldChord.prepareToPlay(48000.0, 64);
        require(stoppedWithHeldChord.armRecorder(), "recorder: open-note test arms");
        MidiBuffer heldAttack;
        heldAttack.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(90)), 0);
        runBlock(stoppedWithHeldChord, localAudio, heldAttack, "recorder stop-open attack");
        require(stoppedWithHeldChord.stopRecorder(), "recorder: stopping an open take stores it");
        require(stoppedWithHeldChord.exportRecorderTake(0, exportFile),
                "recorder: stop-closed take exports");
        chordengine::test::SmfFile stoppedMidi;
        const bool stoppedInputRead = stoppedMidi.readFrom(exportFile);
        require(stoppedInputRead
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
        std::filesystem::remove(exportFile);
        casePassed();
    }

    // 17. Recorder cancels empty takes and keeps a rolling three-take history.
    {
        ChordEngineAudioProcessor processor;
        processor.prepareToPlay(44100.0, 64);
        require(processor.armRecorder() && processor.cancelRecorder()
                    && processor.recorderTakeCount() == 0,
                "recorder: cancel before the first chord discards the empty take");
        AudioBuffer localAudio(2, 64);
        for (int take = 0; take < 4; ++take)
        {
            require(processor.armRecorder(), "recorder: next take arms");
            MidiBuffer attack;
            attack.addEvent(MidiMessage::noteOn(1, 60 + take, static_cast<std::uint8_t>(90)), 0);
            runBlock(processor, localAudio, attack, "recorder rolling take attack");
            MidiBuffer release;
            release.addEvent(MidiMessage::noteOff(1, 60 + take), 32);
            runBlock(processor, localAudio, release, "recorder rolling take release");
            require(processor.stopRecorder(), "recorder: each nonempty take finalizes");
        }
        require(processor.recorderTakeCount() == 3,
                "recorder: completed takes are capped at newest three");
        auto exportFile = (chordengine::platform::temporaryDirectory() / "ChordEngine-Integration-Recorder-Order.mid");
        std::filesystem::remove(exportFile);
        for (std::uint8_t takeIndex = 0; takeIndex < 3; ++takeIndex)
        {
            require(processor.exportRecorderTake(takeIndex, exportFile),
                    "recorder: each retained take can be exported");
            chordengine::test::SmfFile exported;
            const bool inputRead = exported.readFrom(exportFile);
            require(inputRead
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
                        + str(expectedFirstPitches[takeIndex]) + ", got " + str(firstPitch));
            std::filesystem::remove(exportFile);
        }
        require(!processor.exportRecorderTake(3, exportFile),
                "recorder: index beyond three retained takes is rejected");
        casePassed();
    }

    // 18. With FOLLOW DAW RECORD on, the recorder follows the host's record
    // button without RECORD ARM. It is off by default.
    {
        ChordEngineAudioProcessor processor;
        TestPlayHead playHead;
        attachPlayHead(processor, [&playHead] { return playHead.position(); });
        processor.prepareToPlay(48000.0, 64);
        AudioBuffer localAudio(2, 64);
        MidiBuffer none;
        require(!processor.followHostRecord(), "host record: follow mode is off by default");
        playHead.recording = true;
        runBlock(processor, localAudio, none, "host records with follow off");
        require(processor.recorderState() == 0, "host record: with follow off the host does not arm");
        processor.setFollowHostRecord(true);
        runBlock(processor, localAudio, none, "follow switched on mid-recording");
        require(processor.recorderState() == 0,
                "host record: switching follow on mid-recording waits for the next record start");
        playHead.recording = false;
        runBlock(processor, localAudio, none, "host idle");
        require(processor.recorderState() == 0, "host record: idle transport leaves the recorder idle");
        playHead.recording = true;
        runBlock(processor, localAudio, none, "host starts recording");
        require(processor.recorderState() == 1, "host record: host recording arms the recorder");
        MidiBuffer attack;
        attack.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(90)), 0);
        runBlock(processor, localAudio, attack, "host record attack");
        MidiBuffer release;
        release.addEvent(MidiMessage::noteOff(1, 60), 16);
        runBlock(processor, localAudio, release, "host record release");
        require(processor.isRecorderRecording() && processor.recorderRecordedEventCount() == 8,
                "host record: generated chord is captured");
        playHead.recording = false;
        runBlock(processor, localAudio, none, "host stops recording");
        require(processor.recorderState() == 0 && processor.recorderTakeCount() == 1,
                "host record: host stop stores the take");
        playHead.recording = true;
        runBlock(processor, localAudio, none, "host records without notes");
        playHead.recording = false;
        runBlock(processor, localAudio, none, "host stops without notes");
        require(processor.recorderState() == 0 && processor.recorderTakeCount() == 1,
                "host record: a pass with no generated notes stores nothing and disarms");
        require(processor.armRecorder(), "host record: manual arm still works while host is idle");
        playHead.recording = true;
        runBlock(processor, localAudio, none, "host records over manual arm");
        playHead.recording = false;
        runBlock(processor, localAudio, none, "host stops over manual arm");
        require(processor.recorderState() == 1,
                "host record: a take armed by hand is not disarmed by the host");
        require(processor.cancelRecorder(), "host record: manual take cancelled");
        playHead.recordSwitchOnly = true;
        runBlock(processor, localAudio, none, "host record switch while stopped");
        require(processor.recorderState() == 0,
                "host record: a record switch on a stopped transport does not arm");
        playHead.recordSwitchOnly = false;
        runBlock(processor, localAudio, none, "host record switch off");
        playHead.recording = true;
        runBlock(processor, localAudio, none, "host records before follow off");
        require(processor.recorderState() == 1, "host record: armed by the host again");
        processor.setFollowHostRecord(false);
        playHead.recording = false;
        runBlock(processor, localAudio, none, "host stops after follow off");
        require(processor.recorderState() == 1,
                "host record: switching follow off hands the armed take to the user");
        require(processor.cancelRecorder(), "host record: handed-over take cancelled");

        processor.setFollowHostRecord(true);
        const auto saved = processor.getStateInformation();
        ChordEngineAudioProcessor restored;
        restored.setStateInformation(saved.data(), saved.size());
        require(restored.followHostRecord(), "host record: follow mode is saved and restored");
        detachPlayHead(processor);
        casePassed();
    }

    // 19. A retriggered held note keeps both notes in the take.
    {
        ChordEngineAudioProcessor processor;
        processor.prepareToPlay(48000.0, 64);
        AudioBuffer localAudio(2, 64);
        require(processor.armRecorder(), "retrigger: arms");
        MidiBuffer first;
        first.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(90)), 0);
        runBlock(processor, localAudio, first, "retrigger first");
        MidiBuffer again;
        again.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(100)), 32);
        runBlock(processor, localAudio, again, "retrigger again");
        MidiBuffer release;
        release.addEvent(MidiMessage::noteOff(1, 60), 0);
        runBlock(processor, localAudio, release, "retrigger release");
        require(processor.stopRecorder(), "retrigger: take stored");
        auto exportFile = (chordengine::platform::temporaryDirectory() / "ChordEngine-Integration-Retrigger.mid");
        std::filesystem::remove(exportFile);
        require(processor.exportRecorderTake(0, exportFile), "retrigger: take exports");
        chordengine::test::SmfFile midiFile;
        const bool inputRead = midiFile.readFrom(exportFile);
        require(inputRead && midiFile.getNumTracks() == 1,
                "retrigger: export readable");
        const auto* track = midiFile.getTrack(0);
        // Pair each note-on with the next unused note-off of its channel/pitch.
        std::vector<bool> used(static_cast<std::size_t>(track->getNumEvents()), false);
        int pairedOns = 0;
        int ons = 0;
        for (int index = 0; index < track->getNumEvents(); ++index)
        {
            const auto& on = track->getEventPointer(index)->message;
            if (!on.isNoteOn())
                continue;
            ++ons;
            for (int later = index + 1; later < track->getNumEvents(); ++later)
            {
                const auto& off = track->getEventPointer(later)->message;
                if (!used[static_cast<std::size_t>(later)] && off.isNoteOff()
                    && off.getChannel() == on.getChannel() && off.getNoteNumber() == on.getNoteNumber())
                {
                    used[static_cast<std::size_t>(later)] = true;
                    ++pairedOns;
                    break;
                }
            }
        }
        require(ons > 0 && ons == pairedOns,
                "retrigger: every captured note-on has a matching note-off");
        require(ons >= 8, "retrigger: both chord strikes survive in the take, got "
                              + str(ons) + " note-ons");
        std::filesystem::remove(exportFile);
        casePassed();
    }

    // 20. Internal audio: silent by default, sounds when switched on, keeps
    //     the MIDI output, releases when switched off. Every sound.
    {
        ChordEngineAudioProcessor processor(true);
        require(processor.hasAudioOutput() && !processor.isMidiEffect()
                    && processor.supportsOutputChannels(2) && processor.supportsOutputChannels(1)
                    && !processor.supportsOutputChannels(4),
                "internal audio: the audio-output build has a stereo output");
        processor.prepareToPlay(48000.0, 256);
        AudioBuffer buffer(2, 256);
        const auto peakOf = [&buffer]
        {
            return std::max(buffer.getMagnitude(0, 0, buffer.getNumSamples()),
                              buffer.getMagnitude(1, 0, buffer.getNumSamples()));
        };
        MidiBuffer attack;
        attack.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(100)), 0);
        runProcessor(processor, buffer, attack);
        require(peakOf() == 0.0f, "internal audio: off by default, a trigger stays silent");
        require(capture(attack).size() == 4, "internal audio: the chord is still emitted as MIDI");
        MidiBuffer releaseFirst;
        releaseFirst.addEvent(MidiMessage::noteOff(1, 60), 0);
        runProcessor(processor, buffer, releaseFirst);

        for (const auto sound : { ChordEngineAudioProcessor::InternalSound::synth,
                                  ChordEngineAudioProcessor::InternalSound::piano,
                                  ChordEngineAudioProcessor::InternalSound::bass })
        {
            processor.setInternalAudioEnabled(true);
            processor.setInternalSound(sound);
            MidiBuffer on;
            on.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(100)), 0);
            runProcessor(processor, buffer, on);
            float heard = peakOf();
            MidiBuffer none;
            for (int block = 0; block < 8; ++block)
            {
                runProcessor(processor, buffer, none);
                heard = std::max(heard, peakOf());
            }
            require(heard > 0.01f && heard < 1.0f,
                    "internal audio: sound " + str(static_cast<int>(sound))
                        + " is audible and below full scale (peak " + str(heard) + ")");
            require(capture(on).size() == 4, "internal audio: MIDI output unchanged while sounding");
            processor.setInternalAudioEnabled(false);
            MidiBuffer off;
            off.addEvent(MidiMessage::noteOff(1, 60), 0);
            runProcessor(processor, buffer, off);
            float tail = 1.0f;
            for (int block = 0; block < 1500 && tail > 0.0f; ++block)
            {
                runProcessor(processor, buffer, none);
                tail = peakOf();
            }
            require(tail == 0.0f, "internal audio: switching off releases to silence");
        }

        MidiBuffer midiOnly;
        midiOnly.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(100)), 0);
        ChordEngineAudioProcessor midiEffect;
        require(!midiEffect.hasAudioOutput() && midiEffect.isMidiEffect()
                    && midiEffect.supportsOutputChannels(0) && !midiEffect.supportsOutputChannels(2),
                "internal audio: the MIDI FX build keeps no audio bus");
        casePassed();
    }

    // 21. Pack presets replace the stock preset, survive the host state with
    //     the internal-audio settings, and clear back to the stock preset.
    {
        ChordEngineAudioProcessor processor(true);
        processor.prepareToPlay(48000.0, 64);
        AudioBuffer silent(2, 64);
        const auto chordFor = [&processor, &silent]
        {
            MidiBuffer on;
            on.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(100)), 0);
            runProcessor(processor, silent, on);
            std::vector<int> pitches;
            for (const auto& event : capture(on))
                if (event.message.isNoteOn())
                    pitches.push_back(event.message.getNoteNumber());
            MidiBuffer off;
            off.addEvent(MidiMessage::noteOff(1, 60), 0);
            runProcessor(processor, silent, off);
            return pitches;
        };
        const auto stock = chordFor();
        require(stock == std::vector<int>({ 51, 55, 58, 62 }), "pack preset: stock Dreamy chord first");

        ChordEngineAudioProcessor::PackPreset pack;
        pack.packSlug = "chordengine-test-pack";
        pack.packName = "ChordEngine Test Pack";
        pack.presetName = "Test Triads";
        pack.preset.triggerMap.fill(0); // degree 1, plain triad, everywhere
        pack.preset.voicingStyle = "close";
        require(processor.setPackPreset(pack), "pack preset: a valid preset is accepted");
        const auto custom = chordFor();
        require(custom.size() == 3 && custom != stock,
                "pack preset: the pack's map drives generation (got "
                    + str(static_cast<int>(custom.size())) + " notes)");
        require(processor.previewChordForTrigger(60).size == 3,
                "pack preset: the editor preview follows the pack preset");

        auto invalid = pack;
        invalid.preset.triggerMap[10] = 200;
        require(!processor.setPackPreset(invalid) && chordFor() == custom,
                "pack preset: an out-of-range map is refused and the current preset kept");

        processor.setInternalAudioEnabled(true);
        processor.setInternalSound(ChordEngineAudioProcessor::InternalSound::piano);
        const auto saved = processor.getStateInformation();

        ChordEngineAudioProcessor restored(true);
        restored.prepareToPlay(48000.0, 64);
        restored.setStateInformation(saved.data(), saved.size());
        const auto restoredPack = restored.packPreset();
        require(restoredPack.has_value() && restoredPack->presetName == "Test Triads"
                    && restoredPack->packSlug == "chordengine-test-pack"
                    && restoredPack->preset.triggerMap == pack.preset.triggerMap,
                "pack preset: the host state restores the pack preset with its map");
        require(restored.internalAudioEnabled()
                    && restored.internalSound() == ChordEngineAudioProcessor::InternalSound::piano,
                "pack preset: the host state restores the internal-audio switch and sound");

        processor.clearPackPreset();
        require(!processor.packPreset().has_value() && chordFor() == stock,
                "pack preset: clearing returns to the stock preset");
        casePassed();
    }

    // 22. Recorder takes quantize on export to the host grid (FL Studio's list).
    {
        using chordengine::midi::TakeEvent;
        using chordengine::midi::TakeGridAnchor;
        using chordengine::midi::TakeQuantize;
        using chordengine::midi::quantizeTake;
        using chordengine::midi::takeQuantizeGridTicks;

        const std::array<std::int64_t, chordengine::midi::takeQuantizeCount> expectedGrid {{
            0, 40, 60, 80, 120, 240, 160, 240, 320, 480, 960, 3840 }};
        for (std::size_t index = 0; index < expectedGrid.size(); ++index)
            require(takeQuantizeGridTicks(static_cast<TakeQuantize>(index), 3840) == expectedGrid[index],
                    "quantize: grid " + str(static_cast<int>(index)) + " has the FL Studio spacing");
        require(takeQuantizeGridTicks(TakeQuantize::bar, 2880) == 2880, "quantize: a bar follows the time signature");

        const std::vector<TakeEvent> played {
            { 0, 1, 60, 100, true }, { 0, 1, 64, 90, true },
            { 430, 1, 60, 0, false }, { 455, 1, 64, 0, false },
            { 517, 1, 67, 80, true }, { 900, 1, 67, 0, false } };
        require(quantizeTake(played, TakeQuantize::none, {}).size() == played.size()
                    && quantizeTake(played, TakeQuantize::none, {})[2].tick == 430,
                "quantize: none exports the take exactly as played");

        const auto toBeat = quantizeTake(played, TakeQuantize::halfBeat, {});
        require(toBeat.size() == 6, "quantize: every note survives");
        require(toBeat[0].noteOn && toBeat[0].tick == 0 && toBeat[1].noteOn && toBeat[1].tick == 0,
                "quantize: without a host anchor the first note stays at tick 0");
        bool movedToGrid = false;
        for (const auto& event : toBeat)
            if (event.noteOn && event.note == 67)
                movedToGrid = event.tick == 480;
        require(movedToGrid, "quantize: a late note snaps to the nearest 1/2-beat line (517 -> 480)");
        for (const auto& event : toBeat)
            if (!event.noteOn && event.note == 67)
                require(event.tick == 900 - 37, "quantize: the note keeps its played length");

        // Host anchor: the first note fell 259 ticks into its bar.
        TakeGridAnchor anchor;
        anchor.known = true;
        anchor.offsetTicks = 259;
        anchor.barTicks = 3840;
        const auto anchored = quantizeTake(played, TakeQuantize::step, anchor);
        require(anchored.front().noteOn && anchored.front().tick == 240,
                "quantize: with a host anchor the take keeps its place in the bar (259 -> 240)");
        const auto anchoredToBar = quantizeTake(played, TakeQuantize::bar, anchor);
        require(anchoredToBar.front().tick == 0, "quantize: Bar snaps the first chord onto the bar line");
        TakeGridAnchor early = anchor;
        early.offsetTicks = 3830; // played just before the next bar line
        require(quantizeTake(played, TakeQuantize::beat, early).front().tick == 0,
                "quantize: a chord played just early lands on the next bar's downbeat");

        // Repeated pitch that collapses onto one grid line keeps one note,
        // and a release never overlaps the next start of the same pitch.
        const std::vector<TakeEvent> repeated {
            { 0, 1, 60, 100, true }, { 50, 1, 60, 0, false },
            { 70, 1, 60, 110, true }, { 400, 1, 60, 0, false },
            { 470, 1, 60, 90, true }, { 700, 1, 60, 0, false } };
        const auto collapsed = quantizeTake(repeated, TakeQuantize::halfBeat, {});
        int ons = 0;
        bool sounding = false;
        for (const auto& event : collapsed)
        {
            require(event.noteOn != sounding, "quantize: a repeated pitch is released before it starts again");
            sounding = event.noteOn;
            ons += event.noteOn ? 1 : 0;
        }
        require(ons == 2 && !sounding, "quantize: two starts on one grid line become one note");

        // End to end: a running transport, the trigger 0.27 beats into bar 2.
        struct GridPlayHead
        {
            double ppq = 4.27;
            HostTransport position() const
            {
                HostTransport info;
                info.bpm = 120.0;
                info.isPlaying = true;
                info.ppqPosition = ppq;
                info.ppqPositionOfLastBarStart = 4.0;
                info.timeSignatureNumerator = 4;
                info.timeSignatureDenominator = 4;
                return info;
            }
        } gridPlayHead;

        ChordEngineAudioProcessor processor;
        processor.prepareToPlay(48000.0, 64);
        attachPlayHead(processor, [&gridPlayHead] { return gridPlayHead.position(); });
        AudioBuffer localAudio(2, 64);
        require(processor.armRecorder(), "quantize: recorder arms");
        MidiBuffer attack;
        attack.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(90)), 0);
        runBlock(processor, localAudio, attack, "quantize attack");
        MidiBuffer release;
        release.addEvent(MidiMessage::noteOff(1, 60), 48);
        runBlock(processor, localAudio, release, "quantize release");
        require(processor.stopRecorder() && processor.recorderTakeCount() == 1, "quantize: take stored");
        detachPlayHead(processor);

        auto exportFile = (chordengine::platform::temporaryDirectory() / "ChordEngine-Integration-Quantize.mid");
        auto firstOnset = [&exportFile]()
        {
            chordengine::test::SmfFile midiFile;
            const bool inputRead = midiFile.readFrom(exportFile);
            if (!inputRead || midiFile.getNumTracks() != 1)
                return -1.0;
            const auto* track = midiFile.getTrack(0);
            for (int index = 0; index < track->getNumEvents(); ++index)
                if (track->getEventPointer(index)->message.isNoteOn())
                    return track->getEventPointer(index)->message.getTimeStamp();
            return -1.0;
        };
        require(processor.recordQuantize() == TakeQuantize::none, "quantize: off by default");
        std::filesystem::remove(exportFile);
        require(processor.exportRecorderTake(0, exportFile) && firstOnset() == 0.0,
                "quantize: with none the export is unchanged (first chord at tick 0)");
        processor.setRecordQuantize(TakeQuantize::step);
        std::filesystem::remove(exportFile);
        require(processor.exportRecorderTake(0, exportFile) && firstOnset() == 240.0,
                "quantize: Step places the chord on the host grid inside its bar (0.27 beat -> 240 ticks)");
        processor.setRecordQuantize(TakeQuantize::beat);
        std::filesystem::remove(exportFile);
        require(processor.exportRecorderTake(0, exportFile) && firstOnset() == 0.0,
                "quantize: Beat moves the chord to the downbeat");
        std::filesystem::remove(exportFile);

        processor.setRecordQuantize(TakeQuantize::thirdBeat);
        const auto saved = processor.getStateInformation();
        ChordEngineAudioProcessor restored;
        restored.setStateInformation(saved.data(), saved.size());
        require(restored.recordQuantize() == TakeQuantize::thirdBeat,
                "quantize: the grid is saved in and restored from the host state");
        casePassed();
    }

    // 23. Licensing runs in the processor: with no editor ever created the
    // gate is decided at construction, the first chord starts the trial and
    // an expired trial stops chord generation; prepareToPlay asks for a
    // background re-check.
    {
        ChordEngineAudioProcessor processor;
        require(processor.licensingGateOpen(), "licensing: a new installation opens the trial without an editor");
        processor.prepareToPlay(48000.0, 64);
        AudioBuffer localAudio(2, 64);
        MidiBuffer chord;
        chord.addEvent(MidiMessage::noteOn(1, 60, static_cast<std::uint8_t>(90)), 0);
        runBlock(processor, localAudio, chord, "licensing trial chord");
        require(!chord.isEmpty(), "licensing: the trial generates chords");
        auto& runtime = processor.licensing();
        const auto now = chordengine::platform::currentTimeMillis();
        runtime.tick(100.0, now);
        require(runtime.boundary().snapshot().trialStarted,
                "licensing: the processor's first chord starts the trial clock");
        for (double elapsed = 0.0; elapsed <= chordengine::licensing::trialTotalMs; elapsed += 1000.0)
            runtime.tick(1000.0, now);
        require(!processor.licensingGateOpen(), "licensing: an expired trial closes the gate without an editor");
        MidiBuffer release;
        release.addEvent(MidiMessage::noteOff(1, 60), 0);
        runBlock(processor, localAudio, release, "licensing release");
        MidiBuffer blocked;
        blocked.addEvent(MidiMessage::noteOn(1, 62, static_cast<std::uint8_t>(90)), 0);
        runBlock(processor, localAudio, blocked, "licensing expired chord");
        bool anyNoteOn = false;
        for (const auto metadata : blocked)
            anyNoteOn = anyNoteOn || metadata.getMessage().isNoteOn();
        require(!anyNoteOn, "licensing: an expired trial generates no chord");
        casePassed();
    }

    constexpr int expectedCaseCount = 23;
    require(casesPassed == expectedCaseCount,
            "every integration case must run: expected "
                + str(expectedCaseCount) + ", executed "
                + str(casesPassed));
    require(vectorMatches == vectorTotal && vectorMatches > 0,
            "every eligible source-derived vector must match");
    std::cout << "ChordEngineProcessorIntegrationTests: " << casesPassed << "/" << expectedCaseCount
              << " native integration cases passed; " << vectorMatches << "/" << vectorTotal
              << " source-derived vector comparisons matched (hostObserved=false; not host-parity)\n";
    return EXIT_SUCCESS;
}
