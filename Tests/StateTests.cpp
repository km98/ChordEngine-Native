// Plugin state persistence tests.
//
// These drive real processor instances through getStateInformation /
// setStateInformation. They cover round trips, the malformed-payload policy,
// MIDI output after a restore, instance independence and - the native 0.6
// build's compatibility promise - the exact state bytes the shipped 0.5.1
// AU and VST3 wrote (Tests/Fixtures/State051). The editor's re-sync after a
// restore is covered by the editor tests. No DAW is involved.

#include "ProcessorTestSupport.h"
#include "Core/ChordEngineCore.h"
#include "State/PluginState.h"
#include "State/XmlLite.h"

#include <cstring>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Config = chordengine::core::CoreConfiguration;
using Preset = chordengine::core::ChordPresetId;
using Scale = chordengine::core::ScaleId;
using Target = chordengine::core::TransposeTarget;
using Mode = chordengine::core::VelocityMode;
using NoteEvent = chordengine::midi::NoteEvent;

using chordengine::test::require;
using chordengine::test::str;

int casesPassed = 0;

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

std::string describe(const Config& c)
{
    return "key=" + str(c.keyIndex)
        + " scale=" + str(static_cast<int>(c.scale))
        + " preset=" + str(static_cast<int>(c.preset))
        + " target=" + str(static_cast<int>(c.transpose.target))
        + " octave=" + str(c.transpose.octaveSteps)
        + " mode=" + str(static_cast<int>(c.velocityMode))
        + " fixed=" + str(static_cast<int>(c.fixedVelocity));
}

void requireSame(const Config& actual, const Config& expected, const std::string& context)
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
    return processor.getStateInformation();
}

void restoreState(ChordEngineAudioProcessor& processor, const std::string& payload)
{
    processor.setStateInformation(payload.data(), payload.size());
}

// A valid version-1 payload with one attribute replaced.
std::string payloadWith(const char* name, const char* value)
{
    return std::string("<CHORDENGINE_STATE version=\"1\" ") + name + "=\"" + value + "\"/>";
}

// ------------------------------------------------------------- A / B / C / D

void testDefaultRoundTrip()
{
    ChordEngineAudioProcessor source;
    requireSame(source.configurationSnapshot(), Config {}, "fresh processor defaults");
    const auto payload = serializeState(source);
    require(payload.find("CHORDENGINE_STATE") != std::string::npos,
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
    std::string text = payload;
    for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* forbidden : { "license", "token", "update", "recorder", "sustain", "panic", "session" })
        require(text.find(forbidden) == std::string::npos,
                std::string("payload must not contain ") + forbidden);

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
                "fixed: velocity " + str(velocity) + " must survive save/restore");
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
                    "transpose: " + str(static_cast<int>(target)) + "/"
                        + str(octave) + " must survive save/restore");
        }
    casePassed("D transpose targets WHOLE/LOWEST/HIGHEST and octaves -2..+2 survive save/restore");
}

// ------------------------------------------------------------------- E

void testMalformedPayloads()
{
    std::vector<std::pair<std::string, std::string>> bad;
    bad.push_back({ "empty payload", std::string() });
    bad.push_back({ "binary garbage", std::string("\x01\x02\xff not xml") });
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
        bad.push_back({ std::string(name) + "=" + value, payloadWith(name, value) });

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
    chordengine::test::AudioBuffer audio(2, 64);
    auto midi = chordengine::test::events({ chordengine::test::noteOn(1, note, velocity) });
    processor.processBlock(audio.block(), midi);

    std::vector<NoteOut> output;
    for (const auto& message : midi)
        if (message.isNoteOn())
            output.push_back({ message.noteNumber(), message.velocity() });
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

// ------------------------------------------------------------------- H

// Strips the private-data trailer the 0.5.1 VST3 appended (the VST3 adapter
// does the same; this keeps the processor-level check self-contained).
std::string withoutVst3Trailer(std::string bytes)
{
    static const std::string id = "JUCEPrivateData";
    require(bytes.size() > id.size() + 16 && bytes.compare(bytes.size() - id.size(), id.size(), id) == 0,
            "0.5.1 VST3 state ends with its private-data marker");
    std::uint64_t privateSize = 0;
    std::memcpy(&privateSize, bytes.data() + bytes.size() - id.size() - 8, 8);
    bytes.resize(static_cast<std::size_t>(bytes.size() - (privateSize + id.size() + 16)));
    return bytes;
}

std::string fixture(const char* name)
{
    return chordengine::test::readText(std::string(CHORDENGINE_FIXTURE_DIR) + "/State051/" + name);
}

void requireCustom051(ChordEngineAudioProcessor& processor, const std::string& context)
{
    Config expected;
    expected.keyIndex = 3;
    expected.scale = Scale::dorian;
    expected.preset = Preset::neoSoul;
    expected.transpose.target = Target::lowest;
    expected.transpose.octaveSteps = -1;
    expected.velocityMode = Mode::fixed;
    expected.fixedVelocity = 77;
    requireSame(processor.configurationSnapshot(), expected, context + ": configuration");
    const auto pack = processor.packPreset();
    require(pack.has_value(), context + ": pack preset restored");
    require(pack->packSlug == "chordengine-test-pack" && pack->packName == "Test & Pack"
                && pack->presetName == "Caf\xc3\xa9 \xe2\x99\xaf" && pack->preset.voicingStyle == "open",
            context + ": pack identity and voicing (with character references) restored");
    for (std::size_t index = 0; index < pack->preset.triggerMap.size(); ++index)
        require(pack->preset.triggerMap[index] == (index * 11) % 70, context + ": pack trigger map restored");
    require(processor.internalAudioEnabled(), context + ": internal audio on");
    require(processor.internalSound() == ChordEngineAudioProcessor::InternalSound::bass, context + ": bass sound");
    require(processor.recordQuantize() == chordengine::midi::TakeQuantize::thirdBeat, context + ": quantize 1/3 beat");
    require(processor.followHostRecord(), context + ": FOLLOW DAW RECORD on");
}

void testState051Fixtures()
{
    // The exact bytes 0.5.1 wrote into a Logic project (AU) and into a VST3
    // project, restored by this build.
    for (const auto* name : { "au-midifx-custom.state", "au-instrument-custom.state" })
    {
        ChordEngineAudioProcessor processor;
        const auto bytes = fixture(name);
        restoreState(processor, bytes);
        requireCustom051(processor, name);

        // Written back by this build and restored again: nothing is lost.
        ChordEngineAudioProcessor again;
        restoreState(again, serializeState(processor));
        requireCustom051(again, std::string(name) + " re-saved");
    }
    {
        ChordEngineAudioProcessor processor;
        restoreState(processor, withoutVst3Trailer(fixture("vst3-custom.state")));
        requireCustom051(processor, "vst3-custom.state");

        ChordEngineAudioProcessor defaults;
        applyNonDefault(defaults);
        restoreState(defaults, withoutVst3Trailer(fixture("vst3-default.state")));
        requireSame(defaults.configurationSnapshot(), Config {}, "vst3-default.state restores the defaults");
        require(!defaults.packPreset().has_value(), "vst3-default.state: no pack preset");
    }

    // This build writes the same document 0.5.1 wrote: same root, version and
    // attribute names, so 0.5.1 can read a project saved by this build.
    {
        ChordEngineAudioProcessor processor;
        restoreState(processor, fixture("au-midifx-custom.state"));
        const auto written = chordengine::state::parseXmlRoot(serializeState(processor));
        const auto original = chordengine::state::parseXmlRoot(fixture("au-midifx-custom.state"));
        require(written.has_value() && original.has_value(), "both documents parse");
        require(written->tag == original->tag, "same root element");
        require(written->attributes == original->attributes, "same attributes, values and order as 0.5.1");
    }
    casePassed("H the 0.5.1 AU and VST3 state bytes restore every field, and this build writes the same document");
}
} // namespace

int main()
{
    testDefaultRoundTrip();
    testNonDefaultRoundTrip();
    testFixedVelocityRoundTrip();
    testTransposeRoundTrip();
    testMalformedPayloads();
    testIncompleteAndFuturePayloads();
    testMidiAfterRestore();
    testInstanceIndependence();
    testState051Fixtures();

    std::cout << "ChordEngine state persistence: " << casesPassed << " cases passed\n";
    return EXIT_SUCCESS;
}
