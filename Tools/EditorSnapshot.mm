// =====================================================================
// EditorSnapshot - renders the AppKit editor offscreen to PNG, for the same
// states and sizes the 0.5.1 snapshot tool rendered, and composes
// before | after comparisons with the 0.5.1 images.
//
// Development tool only: never installed, signed or shipped. The editor gets
// a throwaway state folder (wiped on every run) and offline transports, so a
// render never reads the user's account and never touches the network.
//
// Usage: ChordEngineEditorSnapshot <out dir> [<0.5.1 dir> <compare dir>]
// =====================================================================
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "Packs/PackLibrary.h"
#include "Packs/PackStoreClient.h"
#include "Platform/Platform.h"
#include "PluginProcessor.h"
#include "UI/EditorHost.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
namespace fs = std::filesystem;
using chordengine::midi::MidiEvent;
using chordengine::midi::MidiEventList;

class SnapshotHost final : public chordengine::ui::EditorHost
{
public:
    explicit SnapshotHost(ChordEngineAudioProcessor& processor) : processor_(processor) {}
    ChordEngineAudioProcessor& processor() override { return processor_; }
    bool requestEditorSize(int, int) override { return false; }

private:
    ChordEngineAudioProcessor& processor_;
};

void runBlock(ChordEngineAudioProcessor& processor, std::initializer_list<MidiEvent> events)
{
    MidiEventList midi(ChordEngineAudioProcessor::midiEventCapacity);
    for (const auto& event : events)
        midi.add(event);
    processor.processBlock({ nullptr, 0, 512 }, midi);
}

std::string readText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool writePng(CGImageRef image, const fs::path& path)
{
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageDestinationRef destination = CGImageDestinationCreateWithURL((__bridge CFURLRef) url,
                                                                        (__bridge CFStringRef) UTTypePNG.identifier, 1, nullptr);
    if (destination == nullptr)
        return false;
    CGImageDestinationAddImage(destination, image, nullptr);
    const bool ok = CGImageDestinationFinalize(destination);
    CFRelease(destination);
    return ok;
}

CGImageRef readPng(const fs::path& path)
{
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef) url, nullptr);
    if (source == nullptr)
        return nullptr;
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    return image;
}

std::vector<std::uint8_t> pixels(CGImageRef image, std::size_t width, std::size_t height)
{
    std::vector<std::uint8_t> data(width * height * 4);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(data.data(), width, height, 8, width * 4, space,
                                                 kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
    CGContextRelease(context);
    CGColorSpaceRelease(space);
    return data;
}

struct Comparison
{
    std::string name;
    double meanAbsoluteDifference = 0.0; // 0..255, per channel
    double differingPixels = 0.0;        // fraction with a channel off by > 24
    bool compared = false;
};

std::vector<Comparison> comparisons;
fs::path outDir, beforeDir, compareDir;

void compare(const std::string& name, CGImageRef after)
{
    if (beforeDir.empty())
        return;
    CGImageRef before = readPng(beforeDir / name);
    if (before == nullptr)
        return;
    const auto width = CGImageGetWidth(after);
    const auto height = CGImageGetHeight(after);
    Comparison result;
    result.name = name;
    if (CGImageGetWidth(before) == width && CGImageGetHeight(before) == height)
    {
        const auto a = pixels(before, width, height);
        const auto b = pixels(after, width, height);
        double sum = 0.0;
        std::size_t differing = 0;
        for (std::size_t index = 0; index < width * height; ++index)
        {
            int worst = 0;
            for (int c = 0; c < 3; ++c)
            {
                const int d = std::abs(static_cast<int>(a[index * 4 + c]) - static_cast<int>(b[index * 4 + c]));
                sum += d;
                worst = std::max(worst, d);
            }
            if (worst > 24)
                ++differing;
        }
        result.meanAbsoluteDifference = sum / (3.0 * width * height);
        result.differingPixels = static_cast<double>(differing) / (width * height);
        result.compared = true;
    }
    comparisons.push_back(result);

    // before | after, with a thin gap.
    const std::size_t gap = 8;
    const std::size_t composedWidth = CGImageGetWidth(before) + gap + width;
    const std::size_t composedHeight = std::max(CGImageGetHeight(before), height);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(nullptr, composedWidth, composedHeight, 8, 0, space,
                                                 kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGContextSetRGBFillColor(context, 1, 0, 1, 1);
    CGContextFillRect(context, CGRectMake(0, 0, composedWidth, composedHeight));
    CGContextDrawImage(context, CGRectMake(0, composedHeight - CGImageGetHeight(before), CGImageGetWidth(before), CGImageGetHeight(before)), before);
    CGContextDrawImage(context, CGRectMake(CGImageGetWidth(before) + gap, composedHeight - height, width, height), after);
    CGImageRef composed = CGBitmapContextCreateImage(context);
    writePng(composed, compareDir / name);
    CGImageRelease(composed);
    CGContextRelease(context);
    CGColorSpaceRelease(space);
    CGImageRelease(before);
}

bool snapshot(NSView* editor, int width, int height, const std::string& suffix)
{
    [editor setFrameSize:NSMakeSize(width, height)];
    chordengine::ui::refreshEditor(editor);
    chordengine::platform::runMainLoopFor(150);
    chordengine::ui::refreshEditor(editor);
    CGImageRef image = chordengine::ui::renderEditorImage(editor);
    if (image == nullptr)
        return false;
    const auto name = "ChordEngine-GUI-" + std::to_string(width) + "x" + std::to_string(height) + suffix + ".png";
    const bool ok = writePng(image, outDir / name);
    compare(name, image);
    CGImageRelease(image);
    std::cout << "wrote " << (outDir / name).string() << std::endl;
    return ok;
}

// A canned store for the PACKS render: the stock pack, the test pack and one
// more pack that is not installed. Downloads are never requested.
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
} // namespace

int main(int argc, const char* argv[])
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        outDir = argc > 1 ? fs::path(argv[1]) : fs::path("EditorSnapshots");
        if (argc > 3)
        {
            beforeDir = argv[2];
            compareDir = argv[3];
            fs::create_directories(compareDir);
        }
        fs::create_directories(outDir);
        const auto stateDirectory = outDir / "SnapshotState";
        fs::remove_all(stateDirectory);
        fs::create_directories(stateDirectory);

        bool ok = true;
        {
            ChordEngineAudioProcessor processor(false);
            processor.prepareToPlay(44100.0, 512);
            SnapshotHost host(processor);
            chordengine::ui::EditorOptions options;
            options.storageDirectory = stateDirectory;
            NSView* editor = chordengine::ui::createEditorViewWithOptions(host, 800, 650, std::move(options));

            ok = snapshot(editor, 800, 650, "") && ok;
            ok = snapshot(editor, 640, 520, "") && ok;
            ok = snapshot(editor, 1280, 1040, "") && ok;

            runBlock(processor, { MidiEvent::noteOn(0, 1, 60, 100) });
            ok = snapshot(editor, 800, 650, "-live") && ok;
            runBlock(processor, { MidiEvent::noteOff(0, 1, 60) });

            processor.armRecorder();
            ok = snapshot(editor, 800, 650, "-armed") && ok;
            runBlock(processor, { MidiEvent::noteOn(0, 1, 55, 100), MidiEvent::noteOff(256, 1, 55) });
            processor.stopRecorder();
            runBlock(processor, {});
            processor.setVelocityMode(chordengine::core::VelocityMode::fixed);
            processor.setRecordQuantize(chordengine::midi::TakeQuantize::quarterStep);
            processor.setFollowHostRecord(true);
            ok = snapshot(editor, 800, 650, "-recorder") && ok;
            ok = snapshot(editor, 640, 520, "-recorder") && ok;
            ok = snapshot(editor, 1280, 1040, "-recorder") && ok;
            processor.setRecordQuantize(chordengine::midi::TakeQuantize::none);
            processor.setFollowHostRecord(false);

            if (chordengine::ui::clickEditorControl(editor, "info-button"))
            {
                ok = snapshot(editor, 800, 650, "-info") && ok;
                ok = snapshot(editor, 640, 520, "-info") && ok;
                ok = snapshot(editor, 1280, 1040, "-info") && ok;
                // The activation result line with real text: signed out, the
                // licensing boundary refuses locally and says what is missing.
                chordengine::ui::setEditorFieldText(editor, "license-key-editor", "PC-7QF2-3MHD-NX4T-9BKR");
                ok = chordengine::ui::clickEditorControl(editor, "license-activate") && ok;
                ok = snapshot(editor, 800, 650, "-info-activation") && ok;
            }
            else
                ok = false;
            chordengine::ui::detachEditorView(editor);
        }

        {
            const auto packState = outDir / "SnapshotPackState";
            fs::remove_all(packState);
            fs::create_directories(packState);
            const auto testPack = chordengine::platform::bytesOf(
                readText(fs::path(CHORDENGINE_SOURCE_DIR) / "Resources/Packs/chordengine-test-pack.json"));
            chordengine::packs::PackLibrary library(packState / "Packs");
            ok = library.install("chordengine-test-pack", testPack, chordengine::packs::sha256Hex(testPack)).empty() && ok;

            ChordEngineAudioProcessor processor(true);
            processor.prepareToPlay(44100.0, 512);
            processor.setInternalAudioEnabled(true);
            processor.setInternalSound(ChordEngineAudioProcessor::InternalSound::piano);
            SnapshotHost host(processor);
            chordengine::ui::EditorOptions options;
            options.storageDirectory = packState;
            options.packTransport = std::make_unique<SnapshotStoreTransport>();
            NSView* editor = chordengine::ui::createEditorViewWithOptions(host, 800, 650, std::move(options));
            ok = snapshot(editor, 800, 650, "-internal-audio") && ok;

            std::vector<float> left(512), right(512);
            float* channels[] = { left.data(), right.data() };
            MidiEventList chord(ChordEngineAudioProcessor::midiEventCapacity);
            chord.add(MidiEvent::noteOn(0, 1, 62, 100));
            processor.processBlock({ channels, 2, 512 }, chord);
            ok = snapshot(editor, 1280, 1040, "-internal-audio-live") && ok;
            MidiEventList chordOff(ChordEngineAudioProcessor::midiEventCapacity);
            chordOff.add(MidiEvent::noteOff(0, 1, 62));
            processor.processBlock({ channels, 2, 512 }, chordOff);

            ok = chordengine::ui::clickEditorControl(editor, "packs-button") && ok;
            chordengine::platform::runMainLoopFor(200);
            ok = snapshot(editor, 800, 650, "-packs") && ok;
            ok = snapshot(editor, 640, 520, "-packs") && ok;
            ok = snapshot(editor, 1280, 1040, "-packs") && ok;
            chordengine::ui::detachEditorView(editor);
            fs::remove_all(packState);
        }
        fs::remove_all(stateDirectory);

        if (!comparisons.empty())
        {
            std::printf("\n%-48s %8s %10s\n", "image", "mean|d|", "differing");
            for (const auto& c : comparisons)
                if (c.compared)
                    std::printf("%-48s %8.2f %9.2f%%\n", c.name.c_str(), c.meanAbsoluteDifference, c.differingPixels * 100.0);
                else
                    std::printf("%-48s %s\n", c.name.c_str(), "size differs");
        }
        return ok ? 0 : 1;
    }
}
