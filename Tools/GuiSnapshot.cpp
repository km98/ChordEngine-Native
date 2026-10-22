// Development-only helper: renders the native editor to PNG at the supported
// editor sizes, plus the live-trigger readout, a stored recorder take, the
// Fixed velocity state and the INFO page, so the layout can be reviewed
// without a DAW host. This target is not part of the plugin, is never
// installed, signed, notarized or shipped.
//
// Output: GuiSnapshots/ next to this executable. The editor is given its own
// throwaway licensing/update state folder inside that directory (wiped on
// every run), so a render never reads or writes the user's real Music-Prod
// account state and always shows the same fresh signed-out, trial-unused
// state. The update transport is the offline one this target is compiled
// with, so no render contacts the network.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>
#include <memory>

#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
bool writeSnapshot(juce::AudioProcessorEditor& editor, int width, int height,
                   const juce::File& directory, const juce::String& suffix)
{
    editor.setSize(width, height);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(120);

    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), false);
    if (!image.isValid())
        return false;

    const auto file = directory.getChildFile("ChordEngine-GUI-" + juce::String(width)
                                             + "x" + juce::String(height) + suffix + ".png");
    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::PNGImageFormat png;
    if (!png.writeImageToStream(image, *stream))
        return false;
    stream->flush();
    std::cout << "wrote " << file.getFullPathName() << std::endl;
    return true;
}

void runBlock(ChordEngineAudioProcessor& processor, juce::MidiBuffer& midi)
{
    juce::AudioBuffer<float> audio(0, 512);
    processor.processBlock(audio, midi);
}

// A canned store for the PACKS page render: the catalog lists the stock
// pack, the test pack and one more pack that is not installed. Downloads are
// never requested by this tool.
class SnapshotStoreTransport final : public chordengine::packs::PackStoreTransport
{
public:
    void get(const juce::String&, std::size_t, Callback callback) override
    {
        const auto testPack = juce::File(CHORDENGINE_SOURCE_DIR)
                                  .getChildFile("Resources/Packs/chordengine-test-pack.json");
        juce::MemoryBlock bytes;
        testPack.loadFileAsData(bytes);
        const auto json = juce::String(R"({"version":1,"generated_at":"2026-10-18T00:00:00Z","packs":[)")
            + R"({"slug":"chordengine-stock","name":"ChordEngine Stock Pack","description":"The twelve built-in presets","version":"1.0.0","preset_count":12,"is_stock":true,"size_bytes":4522,"sha256":")"
            + juce::String::repeatedString("0", 64) + R"(","sort_order":0},)"
            + R"({"slug":"chordengine-test-pack","name":"ChordEngine Test Pack","description":"Four presets that prove the pack system","version":"1.0.0","preset_count":4,"is_stock":false,"size_bytes":)"
            + juce::String(static_cast<int>(bytes.getSize())) + R"(,"sha256":")"
            + chordengine::packs::sha256Hex(bytes) + R"(","sort_order":1},)"
            + R"({"slug":"neo-soul-keys","name":"Neo Soul Keys","description":"Extended voicings for keys","version":"1.0.0","preset_count":16,"is_stock":false,"size_bytes":5000,"sha256":")"
            + juce::String::repeatedString("1", 64) + R"(","sort_order":2}]})";
        chordengine::packs::StoreReply reply;
        reply.status = 200;
        reply.transportOk = true;
        reply.body.append(json.toRawUTF8(), json.getNumBytesAsUTF8());
        juce::MessageManager::callAsync([deliver = std::move(callback), reply] { deliver(reply); });
    }
    void postJson(const juce::String&, const juce::String&, std::size_t, Callback callback) override
    {
        juce::MessageManager::callAsync([deliver = std::move(callback)] { deliver({}); });
    }
};

bool clickButton(juce::Component& editor, const juce::String& componentId)
{
    if (auto* button = dynamic_cast<juce::Button*>(editor.findChildWithID(componentId)))
    {
        button->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
        return true;
    }
    return false;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialise;

    const auto directory = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                               .getParentDirectory().getChildFile("GuiSnapshots");
    directory.createDirectory();
    const auto stateDirectory = directory.getChildFile("SnapshotState");
    stateDirectory.deleteRecursively();
    stateDirectory.createDirectory();

    ChordEngineAudioProcessor processor;
    processor.prepareToPlay(44100.0, 512);
    auto editor = std::make_unique<ChordEngineAudioProcessorEditor>(processor, stateDirectory);

    bool ok = writeSnapshot(*editor, 800, 650, directory, "");
    ok = writeSnapshot(*editor, 640, 520, directory, "") && ok;
    ok = writeSnapshot(*editor, 1280, 1040, directory, "") && ok;

    // Feed one trigger note through the real process path so the chord readout
    // and piano highlights can be inspected in their live state.
    juce::MidiBuffer trigger;
    trigger.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    runBlock(processor, trigger);
    ok = writeSnapshot(*editor, 800, 650, directory, "-live") && ok;

    juce::MidiBuffer release;
    release.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    runBlock(processor, release);

    // A real recorder take: arm, play one chord, stop. Then Fixed velocity,
    // so the take row and the enabled velocity slider can both be reviewed.
    processor.armRecorder();
    ok = writeSnapshot(*editor, 800, 650, directory, "-armed") && ok;
    juce::MidiBuffer take;
    take.addEvent(juce::MidiMessage::noteOn(1, 55, static_cast<juce::uint8>(100)), 0);
    take.addEvent(juce::MidiMessage::noteOff(1, 55), 256);
    runBlock(processor, take);
    processor.stopRecorder();
    juce::MidiBuffer idle;
    runBlock(processor, idle);
    processor.setVelocityMode(chordengine::core::VelocityMode::fixed);
    processor.setRecordQuantize(chordengine::midi::TakeQuantize::quarterStep);
    processor.setFollowHostRecord(true);
    ok = writeSnapshot(*editor, 800, 650, directory, "-recorder") && ok;
    ok = writeSnapshot(*editor, 640, 520, directory, "-recorder") && ok;
    ok = writeSnapshot(*editor, 1280, 1040, directory, "-recorder") && ok;
    processor.setRecordQuantize(chordengine::midi::TakeQuantize::none);
    processor.setFollowHostRecord(false);

    if (clickButton(*editor, "info-button"))
    {
        ok = writeSnapshot(*editor, 800, 650, directory, "-info") && ok;
        ok = writeSnapshot(*editor, 640, 520, directory, "-info") && ok;
        ok = writeSnapshot(*editor, 1280, 1040, directory, "-info") && ok;

        // The activation result line with real text: signed out, the
        // licensing boundary refuses locally (no request is made) and says
        // what is missing, exactly as the functional tests exercise it.
        if (auto* keyField = dynamic_cast<juce::TextEditor*>(editor->findChildWithID("license-key-editor")))
            keyField->setText("PC-7QF2-3MHD-NX4T-9BKR", juce::dontSendNotification);
        ok = clickButton(*editor, "license-activate") && ok;
        ok = writeSnapshot(*editor, 800, 650, directory, "-info-activation") && ok;
    }
    else
    {
        ok = false;
    }

    editor.reset();

    // The PACKS page against a canned catalog, with the test pack installed
    // (signed out, so its presets are shown as needing Music-Prod+), and the
    // internal-audio controls of a build that has an audio output.
    {
        const auto packState = directory.getChildFile("SnapshotPackState");
        packState.deleteRecursively();
        packState.createDirectory();
        juce::MemoryBlock testPack;
        juce::File(CHORDENGINE_SOURCE_DIR).getChildFile("Resources/Packs/chordengine-test-pack.json")
            .loadFileAsData(testPack);
        chordengine::packs::PackLibrary library(packState.getChildFile("Packs"));
        ok = library.install("chordengine-test-pack", testPack, chordengine::packs::sha256Hex(testPack))
                 .isEmpty() && ok;

        ChordEngineAudioProcessor audioProcessor(true);
        audioProcessor.prepareToPlay(44100.0, 512);
        audioProcessor.setInternalAudioEnabled(true);
        audioProcessor.setInternalSound(ChordEngineAudioProcessor::InternalSound::piano);
        auto packsEditor = std::make_unique<ChordEngineAudioProcessorEditor>(
            audioProcessor, packState, nullptr, std::make_unique<SnapshotStoreTransport>());
        ok = writeSnapshot(*packsEditor, 800, 650, directory, "-internal-audio") && ok;

        // Large renders for the product page: a sounding chord with internal
        // audio on, and the PACKS page.
        juce::MidiBuffer chord;
        chord.addEvent(juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(100)), 0);
        runBlock(audioProcessor, chord);
        ok = writeSnapshot(*packsEditor, 1280, 1040, directory, "-internal-audio-live") && ok;
        juce::MidiBuffer chordOff;
        chordOff.addEvent(juce::MidiMessage::noteOff(1, 62), 0);
        runBlock(audioProcessor, chordOff);

        ok = clickButton(*packsEditor, "packs-button") && ok;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        ok = writeSnapshot(*packsEditor, 800, 650, directory, "-packs") && ok;
        ok = writeSnapshot(*packsEditor, 640, 520, directory, "-packs") && ok;
        ok = writeSnapshot(*packsEditor, 1280, 1040, directory, "-packs") && ok;
        packsEditor.reset();
        packState.deleteRecursively();
    }

    stateDirectory.deleteRecursively();
    return ok ? 0 : 1;
}
