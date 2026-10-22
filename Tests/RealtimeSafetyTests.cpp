// Real-time safety: the audio callback (ChordEngineAudioProcessor::processBlock)
// makes no heap allocation, no free and takes no lock, across every feature the
// audio thread touches.
//
// The counting is done by RtProbe.dylib (Tests/RtProbe.c), injected with
// DYLD_INSERT_LIBRARIES by CTest. A positive control proves the probe is
// loaded and counts, so a missing probe can never pass as "zero".

#include "MidiTestSupport.h"
#include "State/PluginState.h"

#include <dlfcn.h>
#include <pthread.h>

#include <cstdlib>
#include <iostream>

namespace
{
using chordengine::test::require;
using chordengine::midi::MidiEvent;
using chordengine::midi::MidiEventList;

using ArmFn = void (*)(int);
using CountFn = long (*)();
using ResetFn = void (*)();

struct Probe
{
    ArmFn arm = nullptr;
    CountFn allocations = nullptr;
    CountFn frees = nullptr;
    CountFn locks = nullptr;
    ResetFn reset = nullptr;
};

Probe loadProbe()
{
    Probe probe;
    probe.arm = reinterpret_cast<ArmFn>(dlsym(RTLD_DEFAULT, "ceRtProbeArm"));
    probe.allocations = reinterpret_cast<CountFn>(dlsym(RTLD_DEFAULT, "ceRtProbeAllocations"));
    probe.frees = reinterpret_cast<CountFn>(dlsym(RTLD_DEFAULT, "ceRtProbeFrees"));
    probe.locks = reinterpret_cast<CountFn>(dlsym(RTLD_DEFAULT, "ceRtProbeLocks"));
    probe.reset = reinterpret_cast<ResetFn>(dlsym(RTLD_DEFAULT, "ceRtProbeReset"));
    require(probe.arm && probe.allocations && probe.frees && probe.locks && probe.reset,
            "the RT probe is loaded (run through CTest, which injects it)");
    return probe;
}

volatile void* sink = nullptr;
// Through volatile function pointers, so the optimiser cannot remove the
// control's allocation (it may elide a plain malloc/free pair).
void* (*volatile allocateMemory)(std::size_t) = std::malloc;
void (*volatile freeMemory)(void*) = std::free;
} // namespace

int main()
{
    const auto probe = loadProbe();

    // ---- positive control ------------------------------------------------------
    {
        probe.reset();
        probe.arm(1);
        void* memory = allocateMemory(64);
        sink = memory;
        freeMemory(memory);
        pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock(&mutex);
        pthread_mutex_unlock(&mutex);
        probe.arm(0);
        require(probe.allocations() >= 1 && probe.frees() >= 1 && probe.locks() >= 1,
                "positive control: the probe counts an allocation, a free and a lock");
        probe.reset();
        void* unarmed = allocateMemory(64);
        sink = unarmed;
        freeMemory(unarmed);
        require(probe.allocations() == 0, "positive control: nothing is counted while disarmed");
    }

    // ---- the processor --------------------------------------------------------------
    ChordEngineAudioProcessor processor(true); // the build with internal audio
    constexpr int blockSize = 256;
    processor.prepareToPlay(48000.0, blockSize);
    chordengine::test::AudioBuffer audio(2, blockSize);
    MidiEventList midi(ChordEngineAudioProcessor::midiEventCapacity);

    chordengine::state::PackPresetState pack;
    pack.packSlug = "rt-pack";
    pack.packName = "RT Pack";
    pack.presetName = "RT";
    for (std::size_t index = 0; index < pack.preset.triggerMap.size(); ++index)
        pack.preset.triggerMap[index] = static_cast<std::uint8_t>((index * 5) % 70);
    pack.preset.voicingStyle = "spread";

    processor.setInternalAudioEnabled(true);
    processor.setFollowHostRecord(true);

    long blocks = 0;
    for (int round = 0; round < 256; ++round)
    {
        // Message-thread work between blocks (not measured): every setting the
        // editor can change, so the audio thread has to pick each change up.
        processor.setInternalSound(static_cast<ChordEngineAudioProcessor::InternalSound>(round % 3));
        processor.setKeyIndex(round % 12);
        processor.setChordPreset(static_cast<chordengine::core::ChordPresetId>(round % 12));
        processor.setVelocityMode(static_cast<chordengine::core::VelocityMode>(round % 3));
        processor.setTransposeOctave((round % 5) - 2);
        if (round % 32 == 8)
            processor.setPackPreset(pack);
        if (round % 32 == 24)
            processor.clearPackPreset();
        if (round % 16 == 3)
            processor.armRecorder();
        if (round % 16 == 12)
            processor.stopRecorder();
        processor.pushUiNoteOn(1, 64, 90);
        processor.pushUiNoteOff(1, 64);

        HostTransport transport;
        transport.bpm = 120.0;
        transport.isPlaying = (round / 40) % 2 == 1;
        transport.isRecording = (round / 40) % 4 == 1; // FOLLOW DAW RECORD arms and stores
        transport.ppqPosition = round * 0.25;
        transport.ppqPositionOfLastBarStart = (round / 16) * 4.0;
        transport.timeSignatureNumerator = 4;
        transport.timeSignatureDenominator = 4;

        // The audio thread: build the block's input and process it.
        probe.arm(1);
        midi.clear();
        midi.add(MidiEvent::noteOn(round % 200, 1, 48 + round % 30, 100));
        midi.add(MidiEvent::noteOn(round % 200 + 3, 2, 60, 80));
        if (round % 4 == 1)
            midi.add(MidiEvent::controller(10, 1, 64, 127)); // sustain down
        if (round % 4 == 3)
            midi.add(MidiEvent::controller(20, 1, 64, 0)); // sustain up
        if (round % 8 == 7)
            midi.add(MidiEvent::controller(30, 1, 123, 0)); // all notes off
        if (round % 16 == 15)
            midi.add(MidiEvent::controller(31, 1, 120, 0)); // all sound off
        midi.add(MidiEvent::make(40, 0xE0, 0, 64));          // pitch bend
        midi.add(MidiEvent::make(41, 0xA0, 60, 30));         // aftertouch
        midi.add(MidiEvent::make(42, 0xC0, 5, 0, 2));        // program change
        midi.add(MidiEvent::make(43, 0xF2, 1, 0));           // passed through
        midi.add(MidiEvent::noteOff(blockSize - 1, 1, 48 + round % 30));
        processor.processBlock(audio.block(), midi, transport);
        probe.arm(0);
        ++blocks;
    }

    std::cout << "realtime: " << blocks << " blocks; allocations " << probe.allocations() << ", frees "
              << probe.frees() << ", locks " << probe.locks() << '\n';
    require(probe.allocations() == 0, "realtime: processBlock never allocates");
    require(probe.frees() == 0, "realtime: processBlock never frees");
    require(probe.locks() == 0, "realtime: processBlock never takes a lock");
    require(processor.recorderTakeCount() > 0, "realtime: the recorder captured takes during the run");

    std::cout << "ChordEngineRealtimeSafetyTests: positive control + 256 blocks with every audio-thread feature, "
                 "0 allocations, 0 frees, 0 locks\n";
    return EXIT_SUCCESS;
}
