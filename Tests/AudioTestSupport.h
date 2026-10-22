#pragma once

// An owning multi-channel float buffer for tests, convertible to the
// AudioBlock view the synth and the processor take.

#include "Audio/AudioBlock.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <vector>

namespace chordengine::test
{
struct AudioBuffer
{
    AudioBuffer(int channels, int samples)
        : data(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(samples), 0.0f))
    {
        for (std::size_t c = 0; c < data.size() && c < pointers.size(); ++c)
            pointers[c] = data[c].data();
    }

    chordengine::audio::AudioBlock block() { return { pointers.data(), getNumChannels(), getNumSamples() }; }
    operator chordengine::audio::AudioBlock() { return block(); }

    int getNumChannels() const { return static_cast<int>(data.size()); }
    int getNumSamples() const { return data.empty() ? 0 : static_cast<int>(data[0].size()); }
    float getSample(int channel, int index) const { return data[static_cast<std::size_t>(channel)][static_cast<std::size_t>(index)]; }
    void setSample(int channel, int index, float value) { data[static_cast<std::size_t>(channel)][static_cast<std::size_t>(index)] = value; }
    const float* getReadPointer(int channel) const { return data[static_cast<std::size_t>(channel)].data(); }
    float* getWritePointer(int channel) { return data[static_cast<std::size_t>(channel)].data(); }
    void clear() { fill(0.0f); }
    float getMagnitude(int channel, int start, int count) const
    {
        float peak = 0.0f;
        for (int index = start; index < start + count; ++index)
            peak = std::max(peak, std::abs(getSample(channel, index)));
        return peak;
    }
    void fill(float value)
    {
        for (auto& channel : data)
            std::fill(channel.begin(), channel.end(), value);
    }

    std::vector<std::vector<float>> data;
    std::array<float*, 8> pointers {};
};
} // namespace chordengine::test
