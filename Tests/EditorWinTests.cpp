// The Windows editor (child HWND, Direct2D / DirectWrite), driven the way the
// macOS EditorTests drive the AppKit one, plus what only Windows has: real
// mouse messages through the window procedure, the native licence key EDIT
// control, DPI scaling, offscreen rendering and the window's lifecycle.
//
//   ChordEngineEditorWinTests                    run the checks (CTest)
//   ChordEngineEditorWinTests --snapshots <dir>  also write PNGs of the
//                                                 states the macOS snapshot
//                                                 tool renders (CI artifact)
//
// Runs on the isolated processor build: no account file, no network.
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>

#include "Licensing/LicensingTypes.h"
#include "Packs/PackFormat.h"
#include "Packs/PackLibrary.h"
#include "Packs/PackStoreClient.h"
#include "Platform/Bytes.h"
#include "Platform/Platform.h"
#include "PluginProcessor.h"
#include "UI/EditorController.h"
#include "UI/EditorHost.h"
#include "UI/EditorStyleWin.h"
#include "UI/EditorWin.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
namespace ui = chordengine::ui;
namespace st = chordengine::ui::style;
using chordengine::midi::MidiEvent;
using chordengine::midi::MidiEventList;

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
        std::fflush(stdout);
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

std::string readText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void pump(int ms)
{
    chordengine::platform::runMainLoopFor(ms);
}

// A hidden top-level window standing in for the host's plug-in frame.
HWND createParent(int width, int height)
{
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSW windowClass {};
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"ChordEngineTestParent";
        RegisterClassW(&windowClass);
        registered = true;
    }
    return CreateWindowExW(0, L"ChordEngineTestParent", L"Test host", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0,
                           width + 40, height + 60, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

SIZE clientSize(HWND window)
{
    RECT rect {};
    GetClientRect(window, &rect);
    return { rect.right - rect.left, rect.bottom - rect.top };
}

// A click through the window procedure at an editor point (points).
void clickAt(ui::WinEditor* editor, ui::Point point)
{
    auto* window = static_cast<HWND>(ui::winEditorWindow(editor));
    const float scale = ui::winEditorScale(editor);
    const LPARAM position = MAKELPARAM(static_cast<int>(std::lround(point.x * scale)),
                                       static_cast<int>(std::lround(point.y * scale)));
    SendMessageW(window, WM_MOUSEMOVE, 0, position);
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, position);
    SendMessageW(window, WM_LBUTTONUP, 0, position);
}

ui::Point centreOf(ui::EditorController& controller, const std::string& identifier)
{
    const auto bounds = controller.controlBounds(identifier);
    return { bounds.midX(), bounds.midY() };
}

HWND keyField(ui::WinEditor* editor)
{
    return FindWindowExW(static_cast<HWND>(ui::winEditorWindow(editor)), nullptr, L"EDIT", nullptr);
}

std::uint32_t pixelAt(const std::vector<std::uint8_t>& bgra, int width, int x, int y)
{
    const auto* p = &bgra[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
    return 0xff000000u | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[0];
}

bool near(std::uint32_t a, std::uint32_t b, int tolerance = 6)
{
    for (int shift = 0; shift <= 16; shift += 8)
        if (std::abs(static_cast<int>((a >> shift) & 0xff) - static_cast<int>((b >> shift) & 0xff)) > tolerance)
            return false;
    return true;
}

// ---------------------------------------------------------------- snapshots
fs::path snapshotDir;

void snapshot(ui::WinEditor* editor, int width, int height, const std::string& suffix)
{
    if (snapshotDir.empty())
        return;
    ui::setWinEditorPixelSize(editor, width, height);
    auto* controller = ui::winEditorController(editor);
    controller->timerCallback();
    pump(150);
    controller->timerCallback();
    const auto name = "ChordEngine-Windows-" + std::to_string(width) + "x" + std::to_string(height) + suffix + ".png";
    const bool ok = ui::writeWinEditorPng(editor, snapshotDir / name);
    check(ok, "snapshot " + name);
    std::printf("wrote %s\n", (snapshotDir / name).string().c_str());
}

class SnapshotStoreTransport final : public chordengine::packs::PackStoreTransport
{
public:
    void get(const std::string&, std::size_t, Callback callback) override
    {
        const auto testPack = readText(fs::path(CHORDENGINE_SOURCE_DIR) / "Resources/Packs/chordengine-test-pack.json");
        const auto sha = chordengine::packs::sha256Hex(chordengine::platform::bytesOf(testPack));
        const std::string json = std::string(R"({"version":1,"generated_at":"2026-10-18T00:00:00Z","packs":[)")
            + R"({"slug":"chordengine-stock","name":"ChordEngine Stock Pack","description":"The twelve built-in presets","version":"1.0.0","preset_count":12,"is_stock":true,"size_bytes":4522,"sha256":")"
            + std::string(64, '0') + R"(","sort_order":0},)"
            + R"({"slug":"chordengine-test-pack","name":"ChordEngine Test Pack","description":"Four presets that prove the pack system","version":"1.0.0","preset_count":4,"is_stock":false,"size_bytes":)"
            + std::to_string(testPack.size()) + R"(,"sha256":")" + sha + R"(","sort_order":1},)"
            + R"({"slug":"neo-soul-keys","name":"Neo Soul Keys","description":"Extended voicings for keys","version":"1.0.0","preset_count":16,"is_stock":false,"size_bytes":5000,"sha256":")"
            + std::string(64, '1') + R"(","sort_order":2}]})";
        chordengine::packs::StoreReply reply;
        reply.status = 200;
        reply.transportOk = true;
        reply.body = chordengine::platform::bytesOf(json);
        chordengine::platform::callOnMainThread([deliver = std::move(callback), reply] { deliver(reply); });
    }
    void postJson(const std::string&, const std::string&, std::size_t, Callback callback) override
    {
        chordengine::platform::callOnMainThread([deliver = std::move(callback)] { deliver({}); });
    }
};

ui::WinEditor* openEditor(TestHost& host, HWND parent, const fs::path& storage, int width = 800, int height = 650,
                          float scale = 1.0f, std::unique_ptr<chordengine::packs::PackStoreTransport> store = nullptr)
{
    ui::EditorOptions options;
    options.storageDirectory = storage;
    options.packTransport = std::move(store);
    return ui::createWinEditor(host, parent, width, height, scale, std::move(options));
}
} // namespace

int main(int argc, char** argv)
{
    for (int index = 1; index + 1 < argc; ++index)
        if (std::string(argv[index]) == "--snapshots")
            snapshotDir = argv[index + 1];
    if (!snapshotDir.empty())
        fs::create_directories(snapshotDir);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    chordengine::platform::initialiseMainThread();
    const auto storage = chordengine::platform::temporaryDirectory()
        / ("ChordEngineEditorWinTests-" + chordengine::platform::randomHex(6));
    fs::create_directories(storage);

    // ---- faces and metrics -------------------------------------------------------
    check(st::gdiFaceName(st::Face::regular) == L"Inter", "Inter Regular is loaded from the bundled file");
    check(st::gdiFaceName(st::Face::caption).find(L"Schibsted") == 0, "Schibsted Grotesk is loaded from the bundled file");
    const auto font = st::uiFont(12.0f);
    check(std::abs(st::ascent(font) + st::descent(font) - 12.0f) < 0.01f, "a font's height is its ascent + descent");
    check(st::stringWidth(font, "CHORD ENGINE") > 40.0f && st::stringWidth(font, "CHORD ENGINE") < 120.0f,
          "text is measured");
    check(st::stringWidth(st::uiFont(12.0f, true, 0.1f), "CHORD") > st::stringWidth(st::uiFont(12.0f, true), "CHORD"),
          "tracking widens text");
    check(st::musicProdMarkAspect() > 1.0f, "the Music-Prod mark is found");

    // ---- the MIDI FX build ---------------------------------------------------------
    {
        TestHost host(false);
        auto& processor = host.processor();
        HWND parent = createParent(800, 650);
        auto* editor = openEditor(host, parent, storage);
        check(editor != nullptr, "the editor window is created");
        auto& controller = *ui::winEditorController(editor);
        auto* window = static_cast<HWND>(ui::winEditorWindow(editor));
        check(GetParent(window) == parent, "the editor is a child of the host's window");
        check(clientSize(window).cx == 800 && clientSize(window).cy == 650, "the window has the requested size");

        // Piano: a real click through the window procedure is a real note.
        const auto c4 = controller.pianoKeyCentre(60);
        const float scale = ui::winEditorScale(editor);
        const LPARAM at = MAKELPARAM(static_cast<int>(std::lround(c4.x * scale)), static_cast<int>(std::lround(c4.y * scale)));
        SendMessageW(window, WM_MOUSEMOVE, 0, at);
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, at);
        check(controller.piano().heldNote() == 60, "piano: the click lands on C4");
        check(count(runBlock(processor), true) == 4, "piano: C4 generates a four-note chord");
        controller.timerCallback();
        check(controller.controlText("chord-caption") == "PLAYING - TRIGGER C4", "piano: the readout shows the live chord");
        // Sliding onto D4 releases C4 and plays D4.
        const auto d4 = controller.pianoKeyCentre(62);
        SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON,
                     MAKELPARAM(static_cast<int>(std::lround(d4.x * scale)), static_cast<int>(std::lround(d4.y * scale))));
        check(controller.piano().heldNote() == 62, "piano: sliding to D4 plays D4");
        SendMessageW(window, WM_LBUTTONUP, 0, at);
        check(controller.piano().heldNote() < 0, "piano: releasing the mouse releases the key");
        runBlock(processor);
        runBlock(processor);
        check(controller.pressPiano(controller.pianoKeyCentre(61)) == 61, "piano: black keys are hit first");
        controller.releasePiano();
        runBlock(processor);

        // Buttons and selectors, through mouse messages where a click is all it takes.
        const auto preset = processor.configurationSnapshot().preset;
        clickAt(editor, centreOf(controller, "preset-next"));
        check(static_cast<int>(processor.configurationSnapshot().preset) == (static_cast<int>(preset) + 1) % 12,
              "preset next (clicked) selects the following preset");
        clickAt(editor, centreOf(controller, "preset-previous"));
        check(processor.configurationSnapshot().preset == preset, "preset previous (clicked) goes back");
        check(controller.selectItem("chord-selector", 1), "preset selector choice");
        check(static_cast<int>(processor.configurationSnapshot().preset) == 0, "preset selector sets the preset");
        check(controller.controlText("preset-counter") == "01 / 12", "preset counter follows");
        check(controller.selectItem("key-selector", 3) && processor.configurationSnapshot().keyIndex == 2,
              "key selector sets the key");
        check(controller.selectItem("scale-selector", 1) && static_cast<int>(processor.configurationSnapshot().scale) == 0,
              "scale selector sets the scale");
        clickAt(editor, centreOf(controller, "octave-up"));
        check(processor.configurationSnapshot().transpose.octaveSteps == 1, "octave up (clicked)");
        check(controller.controlText("transpose-octave") == "+1 OCT", "octave readout");
        clickAt(editor, centreOf(controller, "octave-down"));
        clickAt(editor, centreOf(controller, "octave-down"));
        check(processor.configurationSnapshot().transpose.octaveSteps == -1, "octave down (clicked)");
        clickAt(editor, centreOf(controller, "transpose-lowest"));
        check(processor.configurationSnapshot().transpose.target == chordengine::core::TransposeTarget::lowest,
              "transpose target lowest (clicked)");
        clickAt(editor, centreOf(controller, "transpose-whole"));
        check(processor.configurationSnapshot().transpose.target == chordengine::core::TransposeTarget::whole,
              "transpose target whole (clicked)");
        check(!controller.setSliderValue("fixed-velocity-slider", 77), "fixed velocity is inactive outside Fixed mode");
        check(controller.selectItem("velocity-mode-selector", 3)
                  && processor.configurationSnapshot().velocityMode == chordengine::core::VelocityMode::fixed,
              "velocity mode Fixed");
        {
            // Drag the slider to its right end with the mouse.
            const auto bounds = controller.controlBounds("fixed-velocity-slider");
            const LPARAM from = MAKELPARAM(static_cast<int>(bounds.midX() * scale), static_cast<int>(bounds.midY() * scale));
            const LPARAM to = MAKELPARAM(static_cast<int>((bounds.maxX() + 20.0f) * scale), static_cast<int>(bounds.midY() * scale));
            SendMessageW(window, WM_MOUSEMOVE, 0, from);
            SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, from);
            SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, to);
            SendMessageW(window, WM_LBUTTONUP, 0, to);
            check(processor.configurationSnapshot().fixedVelocity == 127, "the fixed velocity slider drags to 127");
            check(controller.controlText("fixed-velocity-value") == "127", "fixed velocity readout");
        }
        check(controller.isControlVisible("internal-audio-status") && !controller.isControlVisible("internal-audio-toggle"),
              "MIDI FX shows the N/A chip, no audio switch");

        // Recorder.
        clickAt(editor, centreOf(controller, "recorder-action"));
        check(processor.recorderState() == 1 && controller.controlText("recorder-action") == "CANCEL",
              "record arm (clicked) arms");
        clickAt(editor, centreOf(controller, "recorder-action"));
        check(processor.recorderState() == 0, "cancel (clicked) disarms");
        check(controller.selectItem("record-quantize-selector", 6)
                  && processor.recordQuantize() == chordengine::midi::TakeQuantize::step,
              "quantize Step");
        clickAt(editor, centreOf(controller, "follow-host-record"));
        check(processor.followHostRecord() && controller.controlText("follow-host-record") == "FOLLOW DAW RECORD: ON",
              "FOLLOW DAW RECORD (clicked) switches on");
        clickAt(editor, centreOf(controller, "follow-host-record"));
        check(!processor.followHostRecord(), "FOLLOW DAW RECORD switches off");
        check(processor.armRecorder(), "arm for a take");
        runBlock(processor, { MidiEvent::noteOn(0, 1, 55, 100), MidiEvent::noteOff(256, 1, 55) });
        check(processor.stopRecorder(), "take stored");
        controller.timerCallback();
        check(controller.isControlVisible("recorder-take-1"), "the take row appears");
        const auto takeFile = controller.writeRecorderTakeFile(0);
        const auto expected = processor.exportRecorderTakeBytes(0);
        check(!takeFile.empty() && expected.has_value() && readText(takeFile) == *expected,
              "the dragged file is the exported take");
        check(takeFile.extension() == ".mid" && takeFile.parent_path().filename() == "Takes",
              "takes are written to Takes/*.mid");

        // INFO: sign-in and the licence key, typed into the native field.
        clickAt(editor, centreOf(controller, "info-button"));
        check(controller.page() == ui::EditorController::Page::info && controller.isControlVisible("info-title")
                  && !controller.isControlVisible("piano-display"),
              "INFO (clicked): page shown, CHORD hidden");
        check(controller.isControlVisible("sign-in") && controller.controlText("sign-in") == "SIGN IN",
              "signed out: SIGN IN offered");
        HWND field = keyField(editor);
        check(field != nullptr && IsWindowVisible(field), "the licence key field is a visible native EDIT control");
        clickAt(editor, { controller.controlBounds("license-key-editor").x + 2.0f, centreOf(controller, "license-key-editor").y });
        check(GetFocus() == field, "a click on the key box focuses the field");
        for (const wchar_t c : std::wstring(L"PC-7QF2-3MHD-NX4T-9BKR"))
            SendMessageW(field, WM_CHAR, c, 0);
        check(controller.controlText("license-key-editor") == "PC-7QF2-3MHD-NX4T-9BKR", "the licence key is typed");
        clickAt(editor, centreOf(controller, "license-activate"));
        check(controller.controlText("info-auth-hint").find("Sign in") != std::string::npos,
              "ACTIVATE (clicked) reaches the licensing boundary (signed out: refused locally)");
        check(controller.controlText("license-key-editor").empty(), "the key is cleared after ACTIVATE");
        check(controller.controlText("info-trial-status").find("remaining") != std::string::npos,
              "the trial line is the boundary's");
        clickAt(editor, centreOf(controller, "packs-button"));
        check(controller.isControlVisible("packs-title") && !IsWindowVisible(field), "PACKS (clicked); the key field hides");
        clickAt(editor, centreOf(controller, "chord-button"));
        check(controller.isControlVisible("piano-display"), "CHORD (clicked)");

        // Sizes: the host resizes the window; the design scales with the aspect kept.
        for (const int width : { 640, 800, 1280 })
        {
            const int height = static_cast<int>(std::lround(width * 650.0 / 800.0));
            ui::setWinEditorPixelSize(editor, width, height);
            const auto piano = controller.controlBounds("piano-display");
            check(std::abs(piano.width - 720.0 * width / 800.0) <= 1.0, "piano scales at " + std::to_string(width));
        }
        // DPI: 150 % keeps the layout in points.
        ui::setWinEditorScale(editor, 1.5f);
        ui::setWinEditorPixelSize(editor, 1200, 975);
        check(std::abs(controller.controlBounds("piano-display").width - 720.0f) <= 1.0f, "150 %: the design stays 800 x 650 points");
        {
            std::vector<std::uint8_t> pixels;
            int width = 0, height = 0;
            check(ui::renderWinEditor(editor, 1.5f, pixels, width, height) && width == 1200 && height == 975,
                  "150 %: renders 1200 x 975 pixels");
        }
        ui::setWinEditorScale(editor, 1.0f);
        ui::setWinEditorPixelSize(editor, 800, 650);

        // Offscreen render: the page, the piano and type are really drawn.
        {
            std::vector<std::uint8_t> pixels;
            int width = 0, height = 0;
            check(ui::renderWinEditor(editor, 1.0f, pixels, width, height) && width == 800 && height == 650, "renders 800 x 650");
            if (width == 800 && height == 650)
            {
                check(near(pixelAt(pixels, width, 5, 5), st::page.argb), "the page colour fills the canvas");
                const auto key = controller.controlBounds("piano-display");
                // Middle of the first white key's lower half.
                const auto whiteKey = pixelAt(pixels, width, static_cast<int>(key.x + 6), static_cast<int>(key.y + key.height * 0.7f));
                check(near(whiteKey, st::whiteKey.argb, 20) || near(whiteKey, st::whiteKeyShade.argb, 20),
                      "a white key is drawn");
                int ink = 0;
                const auto title = controller.controlBounds("chord-name");
                for (int y = static_cast<int>(title.y); y < static_cast<int>(title.maxY()); ++y)
                    for (int x = static_cast<int>(title.x); x < static_cast<int>(title.x + 200); ++x)
                        ink += near(pixelAt(pixels, width, x, y), st::page.argb, 30) ? 0 : 1;
                check(ink > 200, "the chord name is drawn as text (" + std::to_string(ink) + " ink pixels)");
            }
        }

        snapshot(editor, 800, 650, "");
        snapshot(editor, 640, 520, "");
        snapshot(editor, 1280, 1040, "");
        runBlock(processor, { MidiEvent::noteOn(0, 1, 60, 100) });
        snapshot(editor, 800, 650, "-live");
        runBlock(processor, { MidiEvent::noteOff(0, 1, 60) });
        if (!snapshotDir.empty())
        {
            controller.clickControl("info-button");
            snapshot(editor, 800, 650, "-info");
            snapshot(editor, 1280, 1040, "-info");
            controller.setFieldText("license-key-editor", "PC-7QF2-3MHD-NX4T-9BKR");
            snapshot(editor, 800, 650, "-info-key");
            controller.clickControl("chord-button");
        }

        // The expiry gate.
        ui::setWinEditorPixelSize(editor, 800, 650);
        runBlock(processor, { MidiEvent::noteOn(0, 1, 60, 100) });
        auto& runtime = processor.licensing();
        const auto now = chordengine::platform::currentTimeMillis();
        for (double elapsed = 0.0; elapsed <= chordengine::licensing::trialTotalMs + 1000.0; elapsed += 1000.0)
            runtime.tick(1000.0, now);
        controller.timerCallback();
        check(controller.isControlVisible("trial-gate-title") && !controller.isControlVisible("piano-display"),
              "an expired trial shows the gate instead of the page");
        check(controller.controlText("trial-gate-title") == "ACCESS EXPIRED", "gate title");
        snapshot(editor, 800, 650, "-gate");

        // The editor is destroyed before the host window (the usual order).
        ui::destroyWinEditor(editor);
        check(!IsWindow(window), "destroying the editor destroys its window");
        DestroyWindow(parent);
    }

    // ---- the instrument build, packs ------------------------------------------------
    {
        const auto packState = storage / "PackState";
        fs::create_directories(packState);
        const auto testPack = chordengine::platform::bytesOf(
            readText(fs::path(CHORDENGINE_SOURCE_DIR) / "Resources/Packs/chordengine-test-pack.json"));
        chordengine::packs::PackLibrary library(packState / "Packs");
        check(library.install("chordengine-test-pack", testPack, chordengine::packs::sha256Hex(testPack)).empty(),
              "test pack installed");

        TestHost host(true);
        auto& processor = host.processor();
        processor.setInternalAudioEnabled(true);
        processor.setInternalSound(ChordEngineAudioProcessor::InternalSound::piano);
        HWND parent = createParent(800, 650);
        auto* editor = openEditor(host, parent, packState, 800, 650, 1.0f, std::make_unique<SnapshotStoreTransport>());
        auto& controller = *ui::winEditorController(editor);
        controller.timerCallback();
        check(controller.isControlVisible("internal-audio-toggle") && !controller.isControlVisible("internal-audio-status"),
              "the instrument shows the audio switch");
        const bool before = processor.internalAudioEnabled();
        clickAt(editor, centreOf(controller, "internal-audio-toggle"));
        check(processor.internalAudioEnabled() != before, "the internal audio switch (clicked) toggles");
        clickAt(editor, centreOf(controller, "internal-audio-toggle"));
        snapshot(editor, 800, 650, "-internal-audio");

        clickAt(editor, centreOf(controller, "packs-button"));
        pump(300);
        controller.timerCallback();
        check(controller.isControlVisible("packs-list"), "the pack list is shown");
        check(controller.packList() != nullptr && controller.packList()->listings().size() >= 2,
              "the store catalog is listed");
        // The wheel scrolls the list (it is shorter than the viewport here: no movement, no crash).
        auto* window = static_cast<HWND>(ui::winEditorWindow(editor));
        POINT screen { static_cast<LONG>(centreOf(controller, "packs-list").x), static_cast<LONG>(centreOf(controller, "packs-list").y) };
        ClientToScreen(window, &screen);
        SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), MAKELPARAM(screen.x, screen.y));
        snapshot(editor, 800, 650, "-packs");
        snapshot(editor, 640, 520, "-packs");
        snapshot(editor, 1280, 1040, "-packs");

        // The host destroys its window first, then removes the editor.
        DestroyWindow(parent);
        check(!IsWindow(window), "the host's window took the editor window with it");
        controller.timerCallback(); // still safe with the window gone
        ui::destroyWinEditor(editor);
        check(true, "destroying an editor whose window is gone is safe");
    }

    // ---- lifecycle: open / close repeatedly, detach before destroy --------------------
    {
        TestHost host(false);
        HWND parent = createParent(800, 650);
        for (int round = 0; round < 3; ++round)
        {
            auto* editor = openEditor(host, parent, storage);
            check(editor != nullptr, "re-open " + std::to_string(round + 1));
            pump(60);
            ui::destroyWinEditor(editor);
        }
        auto* editor = openEditor(host, parent, storage);
        ui::detachAllWinEditors(&host);
        check(ui::winEditorController(editor) != nullptr && !ui::winEditorController(editor)->attached(),
              "detachAllWinEditors detaches the controller");
        std::vector<std::uint8_t> pixels;
        int width = 0, height = 0;
        check(ui::renderWinEditor(editor, 1.0f, pixels, width, height), "a detached editor still draws");
        clickAt(editor, { 400.0f, 400.0f }); // no host behind it any more: nothing may happen
        ui::destroyWinEditor(editor);
        DestroyWindow(parent);
    }

    std::error_code error;
    fs::remove_all(storage, error);
    std::printf("%s: %d passed, %d failed\n", failures == 0 ? "WINDOWS EDITOR TESTS PASSED" : "WINDOWS EDITOR TESTS FAILED",
                passes, failures);
    return failures == 0 ? 0 : 1;
}
