// The AppKit editor, driven control by control: every user-facing control
// must change the processor (or the page) the way a click would. Runs with
// CFFIXED_USER_HOME pointed at a temporary folder (see Tests/CMakeLists.txt),
// on the isolated processor build: no account file, no network.
#import <AppKit/AppKit.h>

#include "Licensing/LicensingTypes.h"
#include "Platform/Platform.h"
#include "PluginProcessor.h"
#include "UI/EditorHost.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace
{
using chordengine::midi::MidiEvent;
using chordengine::midi::MidiEventList;
namespace ui = chordengine::ui;

int failures = 0;
int passes = 0;

void check(bool ok, const std::string& what)
{
    if (ok)
        ++passes;
    else
    {
        ++failures;
        std::printf("FAIL %s\n", what.c_str());
    }
}

class TestHost final : public ui::EditorHost
{
public:
    explicit TestHost(bool instrument) : processor_(std::make_unique<ChordEngineAudioProcessor>(instrument))
    {
        processor_->prepareToPlay(48000.0, 512);
    }
    ChordEngineAudioProcessor& processor() override { return *processor_; }
    bool requestEditorSize(int, int) override { return false; }
    std::unique_ptr<ChordEngineAudioProcessor> processor_;
};

MidiEventList runBlock(ChordEngineAudioProcessor& processor, std::initializer_list<MidiEvent> events = {})
{
    MidiEventList midi(ChordEngineAudioProcessor::midiEventCapacity);
    for (const auto& event : events)
        midi.add(event);
    processor.processBlock({ nullptr, 0, 512 }, midi);
    return midi;
}

int count(const MidiEventList& events, bool noteOn)
{
    int result = 0;
    for (const auto& event : events)
        result += noteOn ? event.isNoteOn() : event.isNoteOff();
    return result;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

NSView* openEditor(TestHost& host, const std::filesystem::path& storage)
{
    ui::EditorOptions options;
    options.storageDirectory = storage;
    return ui::createEditorViewWithOptions(host, 800, 650, std::move(options));
}
} // namespace

int main()
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        const auto storage = chordengine::platform::temporaryDirectory()
            / ("ChordEngineEditorTests-" + chordengine::platform::randomHex(6));
        std::filesystem::create_directories(storage);

        // ---- the MIDI FX build ------------------------------------------------
        {
            TestHost host(false);
            auto& processor = host.processor();
            NSView* editor = openEditor(host, storage);

            // Piano: a click on C4 is a real note through the engine.
            const CGPoint c4 = ui::editorPianoKeyCentre(editor, 60);
            check(ui::pressEditorPiano(editor, c4) == 60, "piano: the click lands on C4");
            const auto on = runBlock(processor);
            check(count(on, true) == 4, "piano: C4 generates a four-note chord");
            ui::refreshEditor(editor);
            check(ui::editorControlText(editor, "chord-caption") == "PLAYING - TRIGGER C4",
                  "piano: the readout shows the live chord");
            ui::releaseEditorPiano(editor);
            const auto off = runBlock(processor);
            check(count(off, false) == 4, "piano: releasing the key releases the chord");
            const CGPoint cSharp = ui::editorPianoKeyCentre(editor, 61);
            check(ui::pressEditorPiano(editor, cSharp) == 61, "piano: black keys are hit first");
            ui::releaseEditorPiano(editor);
            runBlock(processor);

            // Preset previous / next and the selector.
            const auto preset = processor.configurationSnapshot().preset;
            check(ui::clickEditorControl(editor, "preset-next"), "preset next clickable");
            check(static_cast<int>(processor.configurationSnapshot().preset) == (static_cast<int>(preset) + 1) % 12,
                  "preset next selects the following preset");
            check(ui::clickEditorControl(editor, "preset-previous"), "preset previous clickable");
            check(processor.configurationSnapshot().preset == preset, "preset previous goes back");
            check(ui::selectEditorItem(editor, "chord-selector", 1), "preset selector choice");
            check(static_cast<int>(processor.configurationSnapshot().preset) == 0, "preset selector sets the preset");
            check(ui::editorControlText(editor, "preset-counter") == "01 / 12", "preset counter follows");

            // Key and scale.
            check(ui::selectEditorItem(editor, "key-selector", 3), "key selector choice");
            check(processor.configurationSnapshot().keyIndex == 2, "key selector sets the key");
            check(ui::selectEditorItem(editor, "scale-selector", 1), "scale selector choice");
            check(static_cast<int>(processor.configurationSnapshot().scale) == 0, "scale selector sets the scale");

            // Transpose.
            check(ui::clickEditorControl(editor, "octave-up"), "octave up clickable");
            check(processor.configurationSnapshot().transpose.octaveSteps == 1, "octave up");
            check(ui::editorControlText(editor, "transpose-octave") == "+1 OCT", "octave readout");
            check(ui::clickEditorControl(editor, "octave-down") && ui::clickEditorControl(editor, "octave-down"),
                  "octave down clickable");
            check(processor.configurationSnapshot().transpose.octaveSteps == -1, "octave down");
            check(ui::clickEditorControl(editor, "transpose-lowest"), "transpose lowest clickable");
            check(processor.configurationSnapshot().transpose.target == chordengine::core::TransposeTarget::lowest,
                  "transpose target lowest");
            check(ui::clickEditorControl(editor, "transpose-highest"), "transpose highest clickable");
            check(processor.configurationSnapshot().transpose.target == chordengine::core::TransposeTarget::highest,
                  "transpose target highest");
            check(ui::clickEditorControl(editor, "transpose-whole"), "transpose whole clickable");

            // Velocity.
            check(!ui::setEditorSliderValue(editor, "fixed-velocity-slider", 77),
                  "fixed velocity is inactive outside Fixed mode");
            check(ui::selectEditorItem(editor, "velocity-mode-selector", 3), "velocity mode choice");
            check(processor.configurationSnapshot().velocityMode == chordengine::core::VelocityMode::fixed,
                  "velocity mode Fixed");
            check(ui::setEditorSliderValue(editor, "fixed-velocity-slider", 77), "fixed velocity slider moves");
            check(processor.configurationSnapshot().fixedVelocity == 77, "fixed velocity reaches the processor");
            check(ui::editorControlText(editor, "fixed-velocity-value") == "77", "fixed velocity readout");

            // Internal audio: the MIDI FX shows its status chip, not a switch.
            check(ui::isEditorControlVisible(editor, "internal-audio-status"), "MIDI FX shows the N/A chip");
            check(!ui::isEditorControlVisible(editor, "internal-audio-toggle"), "MIDI FX has no audio switch");

            // Recorder: arm / cancel, quantize, follow.
            check(ui::clickEditorControl(editor, "recorder-action"), "record arm clickable");
            check(processor.recorderState() == 1, "record arm arms");
            check(ui::editorControlText(editor, "recorder-action") == "CANCEL", "armed: the button cancels");
            check(ui::clickEditorControl(editor, "recorder-action") && processor.recorderState() == 0, "cancel disarms");
            check(ui::selectEditorItem(editor, "record-quantize-selector", 6), "quantize choice");
            check(processor.recordQuantize() == chordengine::midi::TakeQuantize::step, "quantize Step");
            check(ui::clickEditorControl(editor, "follow-host-record") && processor.followHostRecord(),
                  "FOLLOW DAW RECORD switches on");
            check(ui::editorControlText(editor, "follow-host-record") == "FOLLOW DAW RECORD: ON", "follow label");
            check(ui::clickEditorControl(editor, "follow-host-record") && !processor.followHostRecord(),
                  "FOLLOW DAW RECORD switches off");

            // A real take, dragged out: the file is the exported take.
            check(processor.armRecorder(), "arm for a take");
            runBlock(processor, { MidiEvent::noteOn(0, 1, 55, 100), MidiEvent::noteOff(256, 1, 55) });
            check(processor.stopRecorder(), "take stored");
            ui::refreshEditor(editor);
            check(ui::isEditorControlVisible(editor, "recorder-take-1"), "the take row appears");
            const auto takeFile = ui::writeEditorTakeFile(editor, 0);
            const auto expected = processor.exportRecorderTakeBytes(0);
            check(!takeFile.empty() && expected.has_value() && readFile(takeFile) == *expected,
                  "the dragged file is the exported take");
            check(takeFile.extension() == ".mid" && takeFile.parent_path().filename() == "Takes",
                  "takes are written to Takes/*.mid");

            // Navigation.
            check(ui::clickEditorControl(editor, "info-button"), "INFO navigation");
            check(ui::isEditorControlVisible(editor, "info-title") && !ui::isEditorControlVisible(editor, "piano-display"),
                  "INFO page shown, CHORD page hidden");
            check(ui::isEditorControlVisible(editor, "sign-in") && ui::editorControlText(editor, "sign-in") == "SIGN IN",
                  "signed out: SIGN IN offered");
            check(ui::setEditorFieldText(editor, "license-key-editor", "PC-7QF2-3MHD-NX4T-9BKR"), "licence key typed");
            check(ui::clickEditorControl(editor, "license-activate"), "ACTIVATE clickable");
            check(ui::editorControlText(editor, "info-auth-hint").find("Sign in") != std::string::npos,
                  "ACTIVATE reaches the licensing boundary (signed out: refused locally)");
            check(ui::editorControlText(editor, "license-key-editor").empty(), "the key is cleared after ACTIVATE");
            check(ui::editorControlText(editor, "info-trial-status").find("remaining") != std::string::npos,
                  "the trial line is the boundary's");
            check(ui::clickEditorControl(editor, "packs-button") && ui::isEditorControlVisible(editor, "packs-title"),
                  "PACKS navigation");
            check(ui::clickEditorControl(editor, "chord-button") && ui::isEditorControlVisible(editor, "piano-display"),
                  "CHORD navigation");

            // Scaling 640 .. 1280, aspect kept.
            for (const int width : { 640, 800, 1280 })
            {
                ui::resizeEditorTo(editor, width);
                const CGRect piano = ui::editorControlBounds(editor, "piano-display");
                check(std::lround(editor.frame.size.width) == width
                          && std::lround(editor.frame.size.height) == std::lround(width * 650.0 / 800.0),
                      "resize to " + std::to_string(width));
                check(std::abs(piano.size.width - 720.0 * width / 800.0) <= 1.0, "piano scales at " + std::to_string(width));
            }
            ui::resizeEditorTo(editor, 3000);
            check(std::lround(editor.frame.size.width) == 1280, "resize clamps at 1280");
            ui::resizeEditorTo(editor, 100);
            check(std::lround(editor.frame.size.width) == 640, "resize clamps at 640");

            // The expiry gate: a used-up trial closes the gate and owns the page.
            ui::resizeEditorTo(editor, 800);
            runBlock(processor, { MidiEvent::noteOn(0, 1, 60, 100) });
            auto& runtime = processor.licensing();
            const auto now = chordengine::platform::currentTimeMillis();
            for (double elapsed = 0.0; elapsed <= chordengine::licensing::trialTotalMs + 1000.0; elapsed += 1000.0)
                runtime.tick(1000.0, now);
            ui::refreshEditor(editor);
            check(ui::isEditorControlVisible(editor, "trial-gate-title") && !ui::isEditorControlVisible(editor, "piano-display"),
                  "an expired trial shows the gate instead of the page");
            check(ui::editorControlText(editor, "trial-gate-title") == "ACCESS EXPIRED", "gate title");

            ui::detachEditorView(editor);
        }

        // ---- the instrument build ---------------------------------------------
        {
            TestHost host(true);
            auto& processor = host.processor();
            NSView* editor = openEditor(host, storage);
            check(ui::isEditorControlVisible(editor, "internal-audio-toggle")
                      && !ui::isEditorControlVisible(editor, "internal-audio-status"),
                  "instrument: the audio switch is shown");
            const bool before = processor.internalAudioEnabled();
            check(ui::clickEditorControl(editor, "internal-audio-toggle") && processor.internalAudioEnabled() != before,
                  "instrument: the switch toggles internal audio");
            check(ui::selectEditorItem(editor, "internal-sound-selector", 3)
                      && processor.internalSound() == ChordEngineAudioProcessor::InternalSound::bass,
                  "instrument: the sound selector sets Bass");
            ui::detachEditorView(editor);
        }

        // ---- detach safety: the host goes away while the view lives -------------
        {
            auto host = std::make_unique<TestHost>(false);
            NSView* editor = openEditor(*host, storage);
            ui::detachAllEditorViews(host.get());
            host.reset();
            // Every control is now inert; none may reach the deleted processor.
            ui::clickEditorControl(editor, "recorder-action");
            ui::clickEditorControl(editor, "preset-next");
            ui::selectEditorItem(editor, "key-selector", 5);
            ui::pressEditorPiano(editor, ui::editorPianoKeyCentre(editor, 60));
            ui::refreshEditor(editor);
            ui::resizeEditorTo(editor, 1000);
            CGImageRef image = ui::renderEditorImage(editor);
            check(image != nullptr, "a detached view still draws");
            if (image != nullptr)
                CGImageRelease(image);
        }

        std::error_code error;
        std::filesystem::remove_all(storage, error);
        std::printf("%s: %d passed, %d failed\n", failures == 0 ? "EDITOR TESTS PASSED" : "EDITOR TESTS FAILED", passes,
                    failures);
        return failures == 0 ? 0 : 1;
    }
}
