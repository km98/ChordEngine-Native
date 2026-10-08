// Plugin state persistence tests.
//
// These drive real processor and editor instances through getStateInformation /
// setStateInformation. They cover round trips, the malformed-payload policy,
// MIDI output after a restore, instance independence and GUI re-synchronisation.
// No DAW is involved.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Core/ChordEngineCore.h"
#include "State/PluginState.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace juce
{
void juce_VerifyPlugin();
void juce_VerifyPlugin() {}
}

namespace
{
using Config = chordengine::core::CoreConfiguration;
using Preset = chordengine::core::ChordPresetId;
using Scale = chordengine::core::ScaleId;
using Target = chordengine::core::TransposeTarget;
using Mode = chordengine::core::VelocityMode;
using NoteEvent = chordengine::midi::NoteEvent;

int casesPassed = 0;

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

void casePassed(const char* name)
{
    ++casesPassed;
    std::cout << "PASS: " << name << '\n';
}

bool sameConfiguration(const Config& a, const Config& b) noexcept
{
    return a.keyIndex == b.keyIndex && a.scale == b.scale && a.preset == b.preset
        && a.velocityMode == b.velocityMode && a.fixedVelocity == b.fixedVelocity
        && a.transpose.target == b.transpose.target
        && a.transpose.octaveSteps == b.transpose.octaveSteps;
}

juce::String describe(const Config& c)
{
    return "key=" + juce::String(c.keyIndex)
        + " scale=" + juce::String(static_cast<int>(c.scale))
        + " preset=" + juce::String(static_cast<int>(c.preset))
        + " target=" + juce::String(static_cast<int>(c.transpose.target))
        + " octave=" + juce::String(c.transpose.octaveSteps)
        + " mode=" + juce::String(static_cast<int>(c.velocityMode))
        + " fixed=" + juce::String(static_cast<int>(c.fixedVelocity));
}

void requireSame(const Config& actual, const Config& expected, const juce::String& context)
{
    require(sameConfiguration(actual, expected),
            context + ": expected " + describe(expected) + " but got " + describe(actual));
}

// Non-default values, applied only through the public setters.
void applyNonDefault(ChordEngineAudioProcessor& processor)
{
    require(processor.setKeyIndex(7), "setup: key");
    require(processor.setScale(Scale::dorian), "setup: scale");
    require(processor.setChordPreset(Preset::house), "setup: preset");
    require(processor.setTransposeTarget(Target::highest), "setup: target");
    require(processor.setTransposeOctave(-1), "setup: octave");
    require(processor.setVelocityMode(Mode::fixed), "setup: velocity mode");
    require(processor.setFixedVelocity(33), "setup: fixed velocity");
}

Config nonDefaultExpected()
{
    Config expected;
    expected.keyIndex = 7;
    expected.scale = Scale::dorian;
    expected.preset = Preset::house;
    expected.transpose.target = Target::highest;
    expected.transpose.octaveSteps = -1;
    expected.velocityMode = Mode::fixed;
    expected.fixedVelocity = 33;
    return expected;
}

std::string serializeState(ChordEngineAudioProcessor& processor)
{
    juce::MemoryBlock block;
    processor.getStateInformation(block);
    return std::string(static_cast<const char*>(block.getData()), block.getSize());
}

void restoreState(ChordEngineAudioProcessor& processor, const std::string& payload)
{
    processor.setStateInformation(payload.data(), static_cast<int>(payload.size()));
}

// A valid version-1 payload with one attribute replaced.
std::string payloadWith(const char* name, const char* value)
{
    return std::string("<CHORDENGINE_STATE version=\"1\" ") + name + "=\"" + value + "\"/>";
}

juce::File temporaryStorageDirectory(const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("ChordEngine-StateTests")
                   .getChildFile(name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

// ------------------------------------------------------------- A / B / C / D

void testDefaultRoundTrip()
{
    ChordEngineAudioProcessor source;
    requireSame(source.configurationSnapshot(), Config {}, "fresh processor defaults");
    const auto payload = serializeState(source);
    require(juce::String(payload.c_str()).contains("CHORDENGINE_STATE"),
            "default: payload carries the state root");

    ChordEngineAudioProcessor restored;
    applyNonDefault(restored);
    restoreState(restored, payload);
    requireSame(restored.configurationSnapshot(), Config {}, "default round trip");
    casePassed("A default state serializes, restores and matches every field");
}

void testNonDefaultRoundTrip()
{
    ChordEngineAudioProcessor source;
    applyNonDefault(source);
    const auto payload = serializeState(source);

    // Only persistent musical fields may be written.
    const juce::String text(payload.c_str());
    for (const char* forbidden : { "license", "token", "update", "recorder", "sustain", "panic", "session" })
        require(!text.containsIgnoreCase(forbidden),
                juce::String("payload must not contain ") + forbidden);

    ChordEngineAudioProcessor restored;
    restoreState(restored, payload);
    requireSame(restored.configurationSnapshot(), nonDefaultExpected(), "non-default round trip");
    casePassed("B non-default state round trips into a fresh processor");
}

void testFixedVelocityRoundTrip()
{
    for (const int velocity : { 1, 64, 100, 127 })
    {
        ChordEngineAudioProcessor source;
        require(source.setVelocityMode(Mode::fixed), "fixed: mode");
        require(source.setFixedVelocity(velocity), "fixed: value");
        const auto payload = serializeState(source);

        ChordEngineAudioProcessor restored;
        restoreState(restored, payload);
        const auto snapshot = restored.configurationSnapshot();
        require(snapshot.velocityMode == Mode::fixed, "fixed: mode survives");
        require(snapshot.fixedVelocity == velocity,
                "fixed: velocity " + juce::String(velocity) + " must survive save/restore");
    }
    casePassed("C fixed velocity 1, 64, 100 and 127 survive save/restore");
}

void testTransposeRoundTrip()
{
    for (const auto target : { Target::whole, Target::lowest, Target::highest })
        for (int octave = -2; octave <= 2; ++octave)
        {
            ChordEngineAudioProcessor source;
            require(source.setTransposeTarget(target), "transpose: target");
            require(source.setTransposeOctave(octave), "transpose: octave");
            const auto payload = serializeState(source);

            ChordEngineAudioProcessor restored;
            restoreState(restored, payload);
            const auto snapshot = restored.configurationSnapshot();
            require(snapshot.transpose.target == target && snapshot.transpose.octaveSteps == octave,
                    "transpose: " + juce::String(static_cast<int>(target)) + "/"
                        + juce::String(octave) + " must survive save/restore");
        }
    casePassed("D transpose targets WHOLE/LOWEST/HIGHEST and octaves -2..+2 survive save/restore");
}

// ------------------------------------------------------------------- E

void testMalformedPayloads()
{
    std::vector<std::pair<juce::String, std::string>> bad;
    bad.push_back({ "empty payload", std::string() });
    bad.push_back({ "binary garbage", std::string("\x01\x02\xff not xml", 13) });
    bad.push_back({ "truncated XML", "<CHORDENGINE_STATE version=\"1\" keyIndex=\"3\"" });
    bad.push_back({ "wrong root element", "<OTHER version=\"1\" keyIndex=\"3\"/>" });
    bad.push_back({ "missing version", "<CHORDENGINE_STATE keyIndex=\"3\"/>" });
    bad.push_back({ "future version 2", "<CHORDENGINE_STATE version=\"2\" keyIndex=\"3\"/>" });
    bad.push_back({ "version 0", "<CHORDENGINE_STATE version=\"0\"/>" });
    bad.push_back({ "non-numeric version", "<CHORDENGINE_STATE version=\"x\"/>" });
    bad.push_back({ "oversized payload", "<CHORDENGINE_STATE version=\"1\" pad=\""
                                             + std::string(5000, 'a') + "\"/>" });

    const std::pair<const char*, const char*> invalidValues[] {
        { "keyIndex", "12" }, { "keyIndex", "-1" }, { "keyIndex", "abc" },
        { "keyIndex", " 3" }, { "keyIndex", "3x" }, { "keyIndex", "99999" },
        { "scale", "bogus" }, { "scale", "Minor" },
        { "chordPreset", "Basic" }, { "chordPreset", "unknown" },
        { "transposeTarget", "middle" },
        { "transposeOctave", "3" }, { "transposeOctave", "-3" },
        { "velocityMode", "Fixed" }, { "velocityMode", "soft" },
        { "fixedVelocity", "0" }, { "fixedVelocity", "128" },
        { "fixedVelocity", "300" }, { "fixedVelocity", "1x" }
    };
    for (const auto& [name, value] : invalidValues)
        bad.push_back({ juce::String(name) + "=" + value, payloadWith(name, value) });

    for (const auto& [label, payload] : bad)
    {
        ChordEngineAudioProcessor processor;
        applyNonDefault(processor);
        restoreState(processor, payload);
        requireSame(processor.configurationSnapshot(), nonDefaultExpected(),
                    "malformed (" + label + ") must keep the current valid state");
    }

    // Null and empty buffers are handled without crashing or changing state.
    ChordEngineAudioProcessor processor;
    applyNonDefault(processor);
    processor.setStateInformation(nullptr, 0);
    processor.setStateInformation(nullptr, 10);
    requireSame(processor.configurationSnapshot(), nonDefaultExpected(), "null payload");

    casePassed("E malformed, invalid, oversized and incompatible payloads keep the valid state");
}

void testIncompleteAndFuturePayloads()
{
    // Incomplete: absent known fields take their defaults, the present one applies.
    {
        ChordEngineAudioProcessor processor;
        applyNonDefault(processor);
        restoreState(processor, "<CHORDENGINE_STATE version=\"1\" keyIndex=\"5\"/>");
        Config expected;
        expected.keyIndex = 5;
        requireSame(processor.configurationSnapshot(), expected, "incomplete payload");
    }

    // Future properties: unknown attributes and child elements are ignored.
    {
        const auto payload = std::string(
            "<CHORDENGINE_STATE version=\"1\" keyIndex=\"2\" scale=\"lydian\" chordPreset=\"pop\" "
            "transposeTarget=\"lowest\" transposeOctave=\"1\" velocityMode=\"maximum\" "
            "fixedVelocity=\"50\" futureFeature=\"yes\">"
            "<FutureBlock enabled=\"1\"/></CHORDENGINE_STATE>");
        ChordEngineAudioProcessor processor;
        restoreState(processor, payload);
        Config expected;
        expected.keyIndex = 2;
        expected.scale = Scale::lydian;
        expected.preset = Preset::pop;
        expected.transpose.target = Target::lowest;
        expected.transpose.octaveSteps = 1;
        expected.velocityMode = Mode::maximum;
        expected.fixedVelocity = 50;
        requireSame(processor.configurationSnapshot(), expected, "future properties");
    }
    casePassed("E incomplete payloads take defaults and unknown future properties are ignored");
}

// ------------------------------------------------------------------- F

struct NoteOut
{
    int note = 0;
    int velocity = 0;
    bool operator==(const NoteOut& other) const noexcept
    {
        return note == other.note && velocity == other.velocity;
    }
};

std::vector<NoteOut> renderTrigger(ChordEngineAudioProcessor& processor, int note, int velocity)
{
    processor.prepareToPlay(48000.0, 64);
    juce::AudioBuffer<float> audio(2, 64);
    audio.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(velocity)), 0);
    processor.processBlock(audio, midi);

    std::vector<NoteOut> output;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
            output.push_back({ message.getNoteNumber(), message.getVelocity() });
    }
    return output;
}

// Expected generated note-ons from the Core, given the same configuration.
// The processor also forwards the trigger unless the Core suppresses it; the
// trigger is removed here so only generated notes are compared.
std::vector<NoteOut> referenceChord(const Config& config, int note, int velocity)
{
    chordengine::core::ChordEngineCore core;
    require(core.setConfiguration(config), "reference: configuration accepted");

    NoteEvent input;
    input.kind = NoteEvent::Kind::noteOn;
    input.channel = 1;
    input.note = static_cast<std::uint8_t>(note);
    input.velocity = static_cast<std::uint8_t>(velocity);

    chordengine::core::CoreResult result;
    core.process(input, result);

    std::vector<NoteOut> generated;
    for (std::size_t index = 0; index < result.eventCount; ++index)
        if (result.events[index].kind == NoteEvent::Kind::noteOn)
            generated.push_back({ result.events[index].note, result.events[index].velocity });
    return generated;
}

void testMidiAfterRestore()
{
    constexpr int triggerNote = 60;
    constexpr int triggerVelocity = 100;

    for (const auto target : { Target::whole, Target::lowest, Target::highest })
    {
        ChordEngineAudioProcessor source;
        require(source.setKeyIndex(7), "midi: key");
        require(source.setScale(Scale::dorian), "midi: scale");
        require(source.setChordPreset(Preset::house), "midi: preset");
        require(source.setTransposeTarget(target), "midi: target");
        require(source.setTransposeOctave(1), "midi: octave");
        require(source.setVelocityMode(Mode::fixed), "midi: mode");
        require(source.setFixedVelocity(33), "midi: fixed velocity");
        const auto payload = serializeState(source);

        ChordEngineAudioProcessor restored;
        restoreState(restored, payload);
        const auto config = restored.configurationSnapshot();
        requireSame(config, source.configurationSnapshot(), "midi: restore matches source");

        auto actual = renderTrigger(restored, triggerNote, triggerVelocity);
        const auto expected = referenceChord(config, triggerNote, triggerVelocity);
        require(!expected.empty(), "midi: restored configuration must generate a chord");

        // Drop the forwarded trigger when the Core lets input through.
        bool suppressed = false;
        {
            chordengine::core::ChordEngineCore probe;
            probe.setConfiguration(config);
            NoteEvent input;
            input.kind = NoteEvent::Kind::noteOn;
            input.channel = 1;
            input.note = triggerNote;
            input.velocity = triggerVelocity;
            chordengine::core::CoreResult probeResult;
            probe.process(input, probeResult);
            suppressed = probeResult.suppressInput;
        }
        if (!suppressed)
        {
            const auto trigger = std::find(actual.begin(), actual.end(),
                                           NoteOut { triggerNote, triggerVelocity });
            require(trigger != actual.end(), "midi: forwarded trigger expected");
            actual.erase(trigger);
        }

        require(actual == expected,
                "midi: generated notes after restore must match the restored configuration");

        // The restored configuration must differ from the default, so this test
        // would fail if restore were silently ignored.
        require(!(referenceChord(Config {}, triggerNote, triggerVelocity) == expected),
                "midi: restored chord must differ from the default chord");
    }
    casePassed("F generated MIDI after restore matches the restored configuration");
}

// ------------------------------------------------------------------- G

void testInstanceIndependence()
{
    ChordEngineAudioProcessor first;
    applyNonDefault(first);
    const auto firstPayload = serializeState(first);

    ChordEngineAudioProcessor second;
    require(second.setKeyIndex(2), "independence: second key");
    require(second.setChordPreset(Preset::pop), "independence: second preset");
    require(second.setVelocityMode(Mode::maximum), "independence: second mode");

    restoreState(second, firstPayload);
    require(second.setKeyIndex(11), "independence: mutate second");
    requireSame(first.configurationSnapshot(), nonDefaultExpected(),
                "independence: mutating the second instance must not change the first");
    require(serializeState(first) == firstPayload,
            "independence: the first instance's serialized state must be unchanged");

    require(first.setTransposeOctave(2), "independence: mutate first");
    require(second.configurationSnapshot().transpose.octaveSteps == -1,
            "independence: the second instance must not follow the first");
    casePassed("G two processors never share mutable state");
}

// ------------------------------------------------------------------- H

template <typename T>
T& childAs(juce::Component& root, const char* componentId)
{
    auto* child = dynamic_cast<T*>(root.findChildWithID(componentId));
    require(child != nullptr, juce::String("editor child missing: ") + componentId);
    return *child;
}

void testGuiResyncAfterRestore()
{
    ChordEngineAudioProcessor source;
    applyNonDefault(source);
    const auto payload = serializeState(source);

    ChordEngineAudioProcessor processor;
    ChordEngineAudioProcessorEditor editor(processor, temporaryStorageDirectory("state-gui-sync"));

    // Let the editor settle on the default state first.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
    require(childAs<juce::ComboBox>(editor, "scale-selector").getSelectedId()
                == static_cast<int>(Config {}.scale) + 1,
            "gui: starts on the default scale");

    restoreState(processor, payload);
    // The 20 Hz editor timer must pick the restored values up without any user input.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(250);

    const auto expected = nonDefaultExpected();
    require(childAs<juce::ComboBox>(editor, "key-selector").getSelectedId() == expected.keyIndex + 1,
            "gui: key selector reflects the restored key");
    require(childAs<juce::ComboBox>(editor, "scale-selector").getSelectedId()
                == static_cast<int>(expected.scale) + 1,
            "gui: scale selector reflects the restored scale");
    require(childAs<juce::ComboBox>(editor, "chord-selector").getSelectedId()
                == static_cast<int>(expected.preset) + 1,
            "gui: chord preset selector reflects the restored preset");
    require(childAs<juce::ComboBox>(editor, "velocity-mode-selector").getSelectedId()
                == static_cast<int>(expected.velocityMode) + 1,
            "gui: velocity mode selector reflects the restored mode");
    require(static_cast<int>(childAs<juce::Slider>(editor, "fixed-velocity-slider").getValue())
                == expected.fixedVelocity,
            "gui: fixed velocity slider reflects the restored value");
    require(childAs<juce::Label>(editor, "transpose-octave").getText() == "-1 OCT",
            "gui: octave readout reflects the restored octave");

    // Every persisted field shown by the controls, including the transpose
    // target that has no combo box, must match the restored configuration.
    requireSame(editor.displayedConfiguration(), expected,
                "gui: displayed configuration matches the restored state");

    // The GUI reads the processor and must never have become the source of truth.
    requireSame(processor.configurationSnapshot(), expected, "gui: processor state is authoritative");
    casePassed("H the editor re-syncs every restored control after setStateInformation");
}
} // namespace

int main()
{
    const juce::ScopedJuceInitialiser_GUI guiInitialiser;
    juce::ignoreUnused(guiInitialiser);

    testDefaultRoundTrip();
    testNonDefaultRoundTrip();
    testFixedVelocityRoundTrip();
    testTransposeRoundTrip();
    testMalformedPayloads();
    testIncompleteAndFuturePayloads();
    testMidiAfterRestore();
    testInstanceIndependence();
    testGuiResyncAfterRestore();

    std::cout << "ChordEngine state persistence: " << casesPassed << " cases passed\n";
    return EXIT_SUCCESS;
}
