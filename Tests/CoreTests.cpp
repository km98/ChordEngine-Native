#include "Core/ChordEngineCore.h"
#include "Core/ScaleSystem.h"
#include "MIDI/MidiNoteTracker.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace juce
{
void juce_VerifyPlugin();
void juce_VerifyPlugin() {}
}

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

chordengine::midi::NoteEvent note(chordengine::midi::NoteEvent::Kind kind,
                                  std::uint8_t channel, std::uint8_t pitch,
                                  std::uint8_t velocity = 0, int sampleOffset = 0)
{
    chordengine::midi::NoteEvent event;
    event.kind = kind;
    event.channel = channel;
    event.note = pitch;
    event.velocity = velocity;
    event.sampleOffset = sampleOffset;
    return event;
}

chordengine::midi::NoteEvent controller(std::uint8_t channel, std::uint8_t number,
                                         std::uint8_t value, int sampleOffset = 0)
{
    auto event = note(chordengine::midi::NoteEvent::Kind::controller, channel, 0, 0, sampleOffset);
    event.controller = number;
    event.value = value;
    return event;
}

void expectPitches(const chordengine::core::CoreResult& result,
                   const std::vector<int>& pitches,
                   chordengine::midi::NoteEvent::Kind kind,
                   std::uint8_t channel, std::uint8_t velocity, int sampleOffset,
                   const char* message)
{
    require(result.eventCount == pitches.size(), message);
    for (std::size_t i = 0; i < pitches.size(); ++i)
    {
        const auto& event = result.events[i];
        require(event.kind == kind && event.note == pitches[i]
                    && event.channel == channel && event.velocity == velocity
                    && event.sampleOffset == sampleOffset,
                message);
    }
}
}

int main()
{
    using namespace chordengine::core;
    using chordengine::midi::NoteEvent;

    constexpr std::array<std::array<int, 7>, 8> expectedScales {{
        {{ 0, 2, 4, 5, 7, 9, 11 }}, {{ 0, 2, 3, 5, 7, 8, 10 }},
        {{ 0, 2, 3, 5, 7, 9, 10 }}, {{ 0, 1, 3, 5, 7, 8, 10 }},
        {{ 0, 2, 4, 6, 7, 9, 11 }}, {{ 0, 2, 4, 5, 7, 9, 10 }},
        {{ 0, 1, 3, 5, 6, 8, 10 }}, {{ 0, 2, 3, 5, 7, 8, 11 }}
    }};
    require(ScaleSystem::scaleCount == expectedScales.size(), "exactly eight scales exist");
    for (std::size_t scale = 0; scale < expectedScales.size(); ++scale)
    {
        const auto* definition = ScaleSystem::findByIndex(static_cast<int>(scale));
        require(definition != nullptr, "every fixture scale resolves by index");
        for (std::size_t degree = 0; degree < 7; ++degree)
            require(definition->semitoneOffsetsFromRoot[degree] == expectedScales[scale][degree],
                    "scale table matches fixture interval");
    }
    require(ScaleSystem::keyName(0) == "C" && ScaleSystem::keyName(11) == "B"
                && !ScaleSystem::keyOffset(-1) && !ScaleSystem::keyOffset(12),
            "12 key indexes have defined bounds and names");

    require(ChordPresetSystem::count() == 12, "exactly twelve presets are registered");
    constexpr std::array<std::string_view, 12> presetNames {{
        "Basic", "Pop", "Piano", "Emotional", "Dreamy", "Cinematic",
        "R&B", "Neo Soul", "Lo-Fi", "House", "Deep", "Ambient"
    }};
    for (std::size_t preset = 0; preset < presetNames.size(); ++preset)
    {
        const auto* definition = ChordPresetSystem::findByIndex(static_cast<int>(preset));
        require(definition != nullptr && definition->name == presetNames[preset],
                "preset identity matches embedded source order");
        require(ChordPresetSystem::triggerCode(static_cast<ChordPresetId>(preset), 23) == std::nullopt
                    && ChordPresetSystem::triggerCode(static_cast<ChordPresetId>(preset), 97) == std::nullopt,
                "each map rejects trigger notes outside 24-96");
        for (int trigger = 24; trigger <= 96; ++trigger)
            require(ChordPresetSystem::triggerCode(static_cast<ChordPresetId>(preset), trigger).has_value(),
                    "each source preset map covers every accepted trigger");
    }
    require(ChordPresetSystem::findByIndex(-1) == nullptr
                && ChordPresetSystem::findByIndex(12) == nullptr,
            "unknown preset indexes are rejected");

    ChordEngineCore core;
    require(core.configuration().preset == ChordPresetId::dreamy
                && core.configuration().scale == ScaleId::minor
                && core.configuration().keyIndex == 0
                && core.configuration().velocityMode == VelocityMode::dynamic
                && core.configuration().fixedVelocity == 100,
            "core defaults match embedded release source, including Dynamic mode");
    require(core.setConfiguration({}), "source defaults can be explicitly restored");
    CoreConfiguration invalidConfig = core.configuration();
    invalidConfig.fixedVelocity = 0;
    require(!core.setConfiguration(invalidConfig), "fixed velocity below one is rejected");
    invalidConfig = core.configuration();
    invalidConfig.keyIndex = 12;
    require(!core.setConfiguration(invalidConfig), "unknown key index is rejected");
    invalidConfig = core.configuration();
    invalidConfig.preset = static_cast<ChordPresetId>(255);
    require(!core.setConfiguration(invalidConfig), "unknown preset ID is rejected");
    require(ChordPresetSystem::resolve(ChordPresetId::dreamy, 60, ScaleId::minor, 0).has_value(),
            "source preset resolves trigger to scale-degree chord");

    CoreConfiguration config = core.configuration();
    config.keyIndex = 2;
    require(core.setConfiguration(config), "valid key change is accepted");
    const auto dChord = core.generateChordForTrigger(60);
    require(dChord.has_value(), "key-shifted chord is generated");
    config.keyIndex = 0;
    require(core.setConfiguration(config), "C key reset is accepted");

    CoreResult output;
    auto input = note(NoteEvent::Kind::noteOn, 9, 60, 96, 11);
    core.process(input, output);
    expectPitches(output, { 51, 55, 58, 62 }, NoteEvent::Kind::noteOn, 9, 96, 11,
                  "default chord retains input channel, timing and velocity");
    require(output.suppressInput && core.activeVoiceCount() == 1,
            "valid trigger suppresses raw note and stores voice");

    auto second = note(NoteEvent::Kind::noteOn, 2, 64, 80, 17);
    const auto secondOutput = core.process(second);
    require(secondOutput.eventCount > 0 && secondOutput.events[0].channel == 2
                && core.activeVoiceCount() == 2,
            "different trigger notes can coexist on separate channels");
    const auto oldVoice = core.process(note(NoteEvent::Kind::noteOff, 9, 60, 0, 23));
    expectPitches(oldVoice, { 51, 55, 58, 62 }, NoteEvent::Kind::noteOff, 9, 0, 23,
                  "note-off releases the pitches stored for the original channel/trigger");
    require(core.activeVoiceCount() == 1, "matching input note-off removes only its voice");

    auto invalidLow = core.process(note(NoteEvent::Kind::noteOn, 1, 23, 100, 3));
    auto invalidHigh = core.process(note(NoteEvent::Kind::noteOn, 1, 97, 100, 4));
    require(invalidLow.supported && invalidLow.suppressInput && invalidLow.eventCount == 0
                && invalidHigh.supported && invalidHigh.suppressInput && invalidHigh.eventCount == 0,
            "out-of-range raw triggers are suppressed and produce no chord");
    const auto outsideMidi = core.process(note(NoteEvent::Kind::noteOn, 1, 127, 100));
    require(outsideMidi.supported && outsideMidi.suppressInput && outsideMidi.eventCount == 0,
            "MIDI-valid pitches outside the source trigger window never generate chords");
    const auto badChannel = core.process(note(NoteEvent::Kind::noteOn, 17, 60, 100));
    require(!badChannel.supported && badChannel.eventCount == 0,
            "invalid MIDI channel is rejected without output");

    for (int mode = 0; mode < 3; ++mode)
    {
        config = core.configuration();
        config.velocityMode = static_cast<VelocityMode>(mode);
        config.fixedVelocity = 37;
        require(core.setConfiguration(config), "velocity configuration accepted");
        const auto velocityResult = core.process(note(NoteEvent::Kind::noteOn, 1, 60, 1));
        const auto expectedVelocity = mode == static_cast<int>(VelocityMode::dynamic) ? 1
            : mode == static_cast<int>(VelocityMode::maximum) ? 127 : 37;
        require(velocityResult.eventCount > 0
                    && velocityResult.events[0].velocity == expectedVelocity,
                "dynamic, maximum and fixed velocity match source semantics");
        core.process(note(NoteEvent::Kind::noteOff, 1, 60));
    }
    config = core.configuration();
    config.fixedVelocity = 100;
    config.velocityMode = VelocityMode::dynamic;
    require(core.setConfiguration(config), "default mode restored");

    constexpr std::array<TransposeTarget, 3> targets {{
        TransposeTarget::whole, TransposeTarget::lowest, TransposeTarget::highest
    }};
    for (const auto target : targets)
        for (int steps = -2; steps <= 2; ++steps)
        {
            config = core.configuration();
            config.transpose = { target, steps };
            require(core.setConfiguration(config), "all documented transpose configurations accepted");
            const auto chord = core.generateChordForTrigger(60);
            require(chord && chord->size >= 3, "transposed representative chord generated");
        }
    config = core.configuration();
    config.transpose = { TransposeTarget::whole, 0 };
    require(core.setConfiguration(config), "transpose reset accepted");
    config.transpose.octaveSteps = 3;
    require(!core.setConfiguration(config), "out-of-range transpose setting rejected");
    config = core.configuration();

    core.reset();
    auto pedalDown = core.process(controller(3, 64, 64, 0));
    require(pedalDown.eventCount == 0 && core.sustainHeld(), "CC64 >=64 enables global sustain");
    core.process(note(NoteEvent::Kind::noteOn, 3, 60, 90));
    auto sustainOff = core.process(note(NoteEvent::Kind::noteOff, 3, 60, 0, 19));
    require(sustainOff.eventCount == 0 && core.voiceSustained(3, 60),
            "note-off under sustain retains the generated voice");
    auto pedalUp = core.process(controller(12, 64, 63, 27));
    require(!core.sustainHeld() && core.activeVoiceCount() == 0,
            "global CC64 below threshold releases sustained voice across channels");
    expectPitches(pedalUp, { 51, 55, 58, 62 }, NoteEvent::Kind::noteOff, 3, 0, 27,
                  "pedal release outputs stored pitches on original channel");

    core.process(note(NoteEvent::Kind::noteOn, 4, 60, 90));
    const auto panic120 = core.process(controller(4, 120, 0, 31));
    expectPitches(panic120, { 51, 55, 58, 62 }, NoteEvent::Kind::noteOff, 4, 0, 31,
                  "CC120 panic releases tracked generated notes");
    require(core.activeVoiceCount() == 0 && !core.sustainHeld(), "CC120 clears core voice state");
    core.process(note(NoteEvent::Kind::noteOn, 4, 60, 90));
    const auto panic123 = core.process(controller(4, 123, 0, 33));
    require(panic123.eventCount == 4 && core.activeVoiceCount() == 0,
            "CC123 panic releases chord ownership and clears state");
    core.process(note(NoteEvent::Kind::noteOn, 4, 60, 90));
    auto allNotesOff = note(NoteEvent::Kind::allNotesOff, 4, 0);
    allNotesOff.sampleOffset = 35;
    const auto allNotesOffResult = core.process(allNotesOff);
    require(allNotesOffResult.eventCount == 4 && core.activeVoiceCount() == 0,
            "explicit all-notes-off event uses panic release state");

    core.reset();
    core.process(note(NoteEvent::Kind::noteOn, 5, 60, 90));
    core.process(note(NoteEvent::Kind::noteOn, 5, 64, 90));
    const auto repeatedA = core.process(note(NoteEvent::Kind::noteOff, 5, 60, 0, 43));
    const auto repeatedB = core.process(note(NoteEvent::Kind::noteOff, 5, 64, 0, 44));
    require(repeatedA.eventCount > 0 && repeatedB.eventCount > 0,
            "independent simultaneous voices release deterministically");
    core.reset();
    core.process(controller(1, 64, 127));
    core.process(note(NoteEvent::Kind::noteOn, 1, 60, 90));
    const auto ambiguousRetrigger = core.process(note(NoteEvent::Kind::noteOn, 1, 60, 90));
    require(ambiguousRetrigger.unresolved && ambiguousRetrigger.eventCount == 0,
            "retrigger-under-sustain source ambiguity remains explicitly unresolved");

    core.reset();
    const auto deterministicA = core.process(note(NoteEvent::Kind::noteOn, 7, 60, 99, 9));
    core.reset();
    const auto deterministicB = core.process(note(NoteEvent::Kind::noteOn, 7, 60, 99, 9));
    require(deterministicA.eventCount == deterministicB.eventCount,
            "repeated processing from reset is deterministic");
    for (std::size_t index = 0; index < deterministicA.eventCount; ++index)
        require(deterministicA.events[index].note == deterministicB.events[index].note
                    && deterministicA.events[index].velocity == deterministicB.events[index].velocity
                    && deterministicA.events[index].sampleOffset == deterministicB.events[index].sampleOffset,
                "repeated output event contents are deterministic");

    chordengine::midi::MidiNoteTracker tracker;
    tracker.observe(note(NoteEvent::Kind::noteOn, 1, 60, 100));
    tracker.observe(note(NoteEvent::Kind::noteOn, 1, 60, 90));
    require(tracker.isDown(1, 60) && tracker.activeCount() == 1,
            "tracker records channel/pitch state without duplicate inflation");
    tracker.observe(controller(1, 64, 127));
    tracker.observe(note(NoteEvent::Kind::noteOff, 1, 60));
    require(!tracker.isDown(1, 60) && tracker.isSustained(1, 60),
            "tracker observes sustain state");
    tracker.observe(controller(1, 64, 0));
    require(!tracker.isSustained(1, 60) && tracker.activeCount() == 0,
            "tracker clears sustained state on pedal-up");

    ChordEngineAudioProcessor processor;
    juce::AudioBuffer<float> audio(2, 64);
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            audio.setSample(channel, sample, 0.5f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 3);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 17);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 45);
    processor.processBlock(audio, midi);
    require(midi.getNumEvents() == 9,
            "processor replaces valid input with four chord notes and four releases, forwarding the controller");
    std::size_t index = 0;
    for (const auto metadata : midi)
    {
        if (index < 4)
            require(metadata.getMessage().isNoteOn() && metadata.samplePosition == 3,
                    "processor emits ordered chord notes at original input timestamp");
        if (index == 4)
            require(metadata.getMessage().isController() && metadata.samplePosition == 17,
                    "processor forwards unhandled controller in its original sample timeline");
        if (index >= 5)
            require(metadata.getMessage().isNoteOff() && metadata.samplePosition == 45,
                    "processor releases generated pitches at matching trigger note-off time");
        ++index;
    }
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            require(audio.getSample(channel, sample) == 0.0f, "processor keeps audio silent");

    juce::MidiBuffer sustainEvents;
    sustainEvents.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
    sustainEvents.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(90)), 4);
    sustainEvents.addEvent(juce::MidiMessage::noteOff(1, 64), 28);
    processor.processBlock(audio, sustainEvents);
    require(processor.trackedNoteSustained(1, 64), "processor tracker records input note sustain");

    // GUI encoding hygiene: JUCE's String(const char*) decodes as ASCII, so any
    // non-ASCII byte in the editor sources renders as mojibake in the plugin.
    // Keep the editor plain ASCII and free of superseded product copy.
    {
        const juce::File editorSource(juce::String(CHORDENGINE_SOURCE_DIR) + "/Source/PluginEditor.cpp");
        require(editorSource.existsAsFile(), "editor source is readable for the encoding check");
        const auto text = editorSource.loadFileAsString();
        int nonAsciiCharacters = 0;
        for (const auto character : text)
            if (static_cast<juce::juce_wchar>(character) > 127)
                ++nonAsciiCharacters;
        require(nonAsciiCharacters == 0, "editor source contains no non-ASCII bytes");
        const char* const supersededCopy[] { "Chord preview", "MIDI-only", "NATIVE MIDI FX",
                                             "Placeholder only", "SIGN IN / CANCEL" };
        for (const auto* term : supersededCopy)
            require(!text.contains(term), "editor source no longer contains superseded copy");
    }

    std::cout << "ChordEngineCoreTests: all expanded checks passed\n";
    return EXIT_SUCCESS;
}
