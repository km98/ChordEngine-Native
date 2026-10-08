// Development-only helper: renders the native editor to PNG at the supported
// editor sizes (plus one live-trigger readout and the INFO page) so the
// reference-parity layout can be reviewed without a DAW host. This target is
// not part of the plugin, is never installed, signed, notarized or shipped.

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
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialise;

    ChordEngineAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (editor == nullptr)
    {
        std::cerr << "editor creation failed" << std::endl;
        return 1;
    }

    const auto directory = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                               .getParentDirectory().getChildFile("GuiSnapshots");
    directory.createDirectory();

    bool ok = writeSnapshot(*editor, 800, 650, directory, "");
    ok = writeSnapshot(*editor, 640, 520, directory, "") && ok;
    ok = writeSnapshot(*editor, 1280, 1040, directory, "") && ok;

    // Feed one trigger note through the real process path so the chord readout
    // and piano highlights can be inspected in their live state.
    processor.prepareToPlay(44100.0, 512);
    juce::AudioBuffer<float> audio(0, 512);
    juce::MidiBuffer trigger;
    trigger.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
    processor.processBlock(audio, trigger);
    ok = writeSnapshot(*editor, 800, 650, directory, "-live") && ok;

    if (auto* infoButton = dynamic_cast<juce::Button*>(editor->findChildWithID("info-button")))
    {
        infoButton->triggerClick();
        ok = writeSnapshot(*editor, 800, 650, directory, "-info") && ok;
    }

    juce::MidiBuffer release;
    release.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    processor.processBlock(audio, release);

    return ok ? 0 : 1;
}
