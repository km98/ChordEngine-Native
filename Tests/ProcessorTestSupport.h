#pragma once

// Helpers for driving ChordEngineAudioProcessor block by block in tests.

#include "PluginProcessor.h"
#include "AudioTestSupport.h"
#include "TestSupport.h"

#include <array>
#include <vector>

namespace chordengine::test
{
using chordengine::midi::MidiEvent;
using chordengine::midi::MidiEventList;

inline MidiEventList events(std::initializer_list<MidiEvent> list)
{
    MidiEventList result(ChordEngineAudioProcessor::midiEventCapacity);
    for (const auto& event : list)
        result.add(event);
    return result;
}

inline MidiEvent noteOn(int channel, int note, int velocity, int offset = 0)
{
    return MidiEvent::noteOn(offset, channel, note, velocity);
}

inline MidiEvent noteOff(int channel, int note, int offset = 0)
{
    return MidiEvent::noteOff(offset, channel, note);
}

inline MidiEvent controller(int channel, int number, int value, int offset = 0)
{
    return MidiEvent::controller(offset, channel, number, value);
}
} // namespace chordengine::test
