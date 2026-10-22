#pragma once

#include <algorithm>
#include <cstdint>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
 #include <xmmintrin.h>
#endif

namespace chordengine::audio
{
// A view of the host's output channels for one block (no ownership).
struct AudioBlock
{
    float* const* channels = nullptr;
    int numChannels = 0;
    int numSamples = 0;

    void clear() const noexcept
    {
        for (int channel = 0; channel < numChannels; ++channel)
            if (channels[channel] != nullptr)
                std::fill(channels[channel], channels[channel] + numSamples, 0.0f);
    }

    void addFrom(int channel, int startSample, const float* source, int length) const noexcept
    {
        float* destination = channels[channel] + startSample;
        for (int index = 0; index < length; ++index)
            destination[index] += source[index];
    }
};

// Linear ramp to a target over a fixed number of samples: the same arithmetic
// as the smoothing the 0.5 synth used, so the level ramp is unchanged.
class LinearSmoothed
{
public:
    void reset(double sampleRate, double rampSeconds) noexcept
    {
        stepsToTarget_ = static_cast<int>(rampSeconds * sampleRate); // floor for positive values
        setCurrentAndTargetValue(target_);
    }

    void setCurrentAndTargetValue(float value) noexcept
    {
        target_ = current_ = value;
        countdown_ = 0;
    }

    void setTargetValue(float value) noexcept
    {
        if (value == target_)
            return;
        if (stepsToTarget_ <= 0)
        {
            setCurrentAndTargetValue(value);
            return;
        }
        target_ = value;
        countdown_ = stepsToTarget_;
        step_ = (target_ - current_) / static_cast<float>(countdown_);
    }

    float getNextValue() noexcept
    {
        if (countdown_ <= 0)
            return target_;
        --countdown_;
        if (countdown_ > 0)
            current_ += step_;
        else
            current_ = target_;
        return current_;
    }

    float getTargetValue() const noexcept { return target_; }

private:
    float current_ = 0.0f;
    float target_ = 0.0f;
    float step_ = 0.0f;
    int countdown_ = 0;
    int stepsToTarget_ = 0;
};

// Flushes denormals to zero for the lifetime of the object (audio thread).
class ScopedNoDenormals
{
public:
    ScopedNoDenormals() noexcept
    {
#if defined(__aarch64__) || defined(_M_ARM64)
        std::uint64_t fpcr = 0;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        saved_ = fpcr;
        fpcr |= (1ull << 24); // FZ
        asm volatile("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
        saved_ = _mm_getcsr();
        _mm_setcsr(static_cast<unsigned>(saved_) | 0x8040u); // FTZ | DAZ
#endif
    }

    ~ScopedNoDenormals() noexcept
    {
#if defined(__aarch64__) || defined(_M_ARM64)
        const std::uint64_t fpcr = saved_;
        asm volatile("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
        _mm_setcsr(static_cast<unsigned>(saved_));
#endif
    }

    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
    std::uint64_t saved_ = 0;
};
} // namespace chordengine::audio
