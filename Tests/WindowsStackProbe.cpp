#include "PluginProcessor.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#if !defined(_WIN32)
#error This diagnostic target is Windows-only.
#endif

namespace
{
struct RecorderMidiEventLayout
{
    std::int64_t tick = 0;
    std::uint8_t channel = 1;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    bool noteOn = false;
};

struct RecorderTakeLayout
{
    std::array<RecorderMidiEventLayout, 4096> events {};
    std::size_t eventCount = 0;
    double tempoBpm = 120.0;
    int noteCount = 0;
};

#if defined(_MSC_VER)
#define CHORDENGINE_NOINLINE __declspec(noinline)
#else
#define CHORDENGINE_NOINLINE __attribute__((noinline))
#endif

CHORDENGINE_NOINLINE int constructOnStack()
{
    std::cout << "STACK_CONSTRUCTION_BEGIN\n" << std::flush;
    ChordEngineAudioProcessor processor;
    const auto config = processor.configurationSnapshot();
    std::cout << "STACK_CONSTRUCTION_PASS key=" << config.keyIndex << '\n' << std::flush;
    return config.keyIndex < 0 ? 1 : 0;
}

CHORDENGINE_NOINLINE int constructOnHeap()
{
    std::cout << "HEAP_CONSTRUCTION_BEGIN\n" << std::flush;
    const auto processor = std::make_unique<ChordEngineAudioProcessor>();
    const auto config = processor->configurationSnapshot();
    std::cout << "HEAP_CONSTRUCTION_PASS key=" << config.keyIndex << '\n' << std::flush;
    return config.keyIndex < 0 ? 1 : 0;
}

void printSizes()
{
    std::cout << "sizeof(NoteEvent)=" << sizeof(chordengine::midi::NoteEvent) << '\n'
              << "sizeof(CoreResult)=" << sizeof(chordengine::core::CoreResult) << '\n'
              << "sizeof(ChordEngineCore)=" << sizeof(chordengine::core::ChordEngineCore) << '\n'
              << "sizeof(RecorderMidiEvent_layout)=" << sizeof(RecorderMidiEventLayout) << '\n'
              << "sizeof(RecorderEventBuffer_estimate)="
              << sizeof(RecorderMidiEventLayout) * 4096 << '\n'
              << "sizeof(RecorderTake_layout)=" << sizeof(RecorderTakeLayout) << '\n'
              << "sizeof(ThreeRecorderTakes_estimate)=" << sizeof(RecorderTakeLayout) * 3 << '\n'
              << "sizeof(ChordEngineAudioProcessor)=" << sizeof(ChordEngineAudioProcessor) << '\n'
              << std::flush;
}
}

int main(int argc, char** argv)
{
    printSizes();
    if (argc != 2)
    {
        std::cerr << "Usage: ChordEngineWindowsStackProbe.exe --stack|--heap\n";
        return 2;
    }
    const std::string mode(argv[1]);
    if (mode == "--stack")
        return constructOnStack();
    if (mode == "--heap")
        return constructOnHeap();
    std::cerr << "Unknown mode: " << mode << '\n';
    return 2;
}
