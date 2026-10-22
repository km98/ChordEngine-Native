// Internal preview synth tests.
//
// Drives chordengine::audio::InternalSynth directly (no processor, no host)
// for every sound at 44.1 kHz and 96 kHz: silence, onset accuracy, headroom,
// release to silence, voice stealing, all-notes-off / reset, additive
// rendering, the MidiBuffer driver and the fundamental pitch.

#include "Audio/InternalSynth.h"

#include "AudioTestSupport.h"
#include "MIDI/MidiEvents.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using chordengine::audio::InternalSynth;
using Sound = InternalSynth::Sound;

int casesPassed = 0;
int casesFailed = 0;

void check(bool condition, const std::string& name)
{
    if (condition)
    {
        ++casesPassed;
        std::cout << "PASS: " << name << '\n';
    }
    else
    {
        ++casesFailed;
        std::cout << "FAIL: " << name << '\n';
    }
}

const char* soundName(Sound sound)
{
    switch (sound)
    {
        case Sound::synth: return "synth";
        case Sound::piano: return "piano";
        case Sound::bass: return "bass";
    }
    return "?";
}

constexpr int blockSize = 512;

struct Stats
{
    float peak = 0.0f;
    bool finite = true;
    long firstNonZero = -1;
};

// Renders whole blocks into a stereo buffer and accumulates statistics of the
// left channel; optionally appends the left channel to `capture`.
struct Rig
{
    InternalSynth synth;
    double sampleRate = 44100.0;
    chordengine::test::AudioBuffer buffer{ 2, blockSize };
    long rendered = 0;

    Rig(Sound sound, double rate, bool bassLowestOnly = true) : sampleRate(rate)
    {
        synth.prepare(rate, blockSize);
        synth.setSound(sound);
        synth.setLevel(1.0f);
        synth.setBassLowestNoteOnly(bassLowestOnly);
    }

    Stats run(double seconds, std::vector<float>* capture = nullptr)
    {
        Stats stats;
        const auto total = static_cast<long>(seconds * sampleRate);
        for (long done = 0; done < total; done += blockSize)
        {
            buffer.clear();
            synth.render(buffer, 0, blockSize);
            accumulate(stats, done);
            if (capture != nullptr)
                capture->insert(capture->end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + blockSize);
        }
        return stats;
    }

    void accumulate(Stats& stats, long base)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const float* data = buffer.getReadPointer(channel);
            for (int i = 0; i < blockSize; ++i)
            {
                if (!std::isfinite(data[i]))
                    stats.finite = false;
                stats.peak = std::max(stats.peak, std::abs(data[i]));
                if (channel == 0 && stats.firstNonZero < 0 && data[i] != 0.0f)
                    stats.firstNonZero = base + i;
            }
        }
        rendered += blockSize;
    }

    // Renders until isSilent(); returns the seconds it took or -1 on timeout.
    double secondsUntilSilent(double limitSeconds, Stats& stats)
    {
        const auto limit = static_cast<long>(limitSeconds * sampleRate);
        for (long done = 0; done <= limit; done += blockSize)
        {
            if (synth.isSilent())
                return static_cast<double>(done) / sampleRate;
            buffer.clear();
            synth.render(buffer, 0, blockSize);
            accumulate(stats, done);
        }
        return -1.0;
    }
};

void chordOn(InternalSynth& synth, std::initializer_list<int> notes, float velocity, int offset = 0)
{
    for (int note : notes)
        synth.noteOn(1, note, velocity, offset);
}

void chordOff(InternalSynth& synth, std::initializer_list<int> notes, int offset = 0)
{
    for (int note : notes)
        synth.noteOff(1, note, offset);
}

const std::initializer_list<int> sixNoteChord{ 48, 52, 55, 59, 62, 67 };

// Strongest frequency between 40 Hz and 2 kHz: Hann window, Goertzel scan in
// 1 Hz steps, then a 0.05 Hz refinement around the winner.
double dominantFrequency(const std::vector<float>& samples, double sampleRate)
{
    const auto n = samples.size();
    std::vector<double> windowed(n);
    for (std::size_t i = 0; i < n; ++i)
        windowed[i] = samples[i] * (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(n - 1)));

    auto power = [&](double frequency)
    {
        const double coeff = 2.0 * std::cos(2.0 * 3.14159265358979323846 * frequency / sampleRate);
        double s1 = 0.0, s2 = 0.0;
        for (double x : windowed)
        {
            const double s0 = x + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return s1 * s1 + s2 * s2 - coeff * s1 * s2;
    };

    double best = 0.0, bestPower = -1.0;
    for (double f = 40.0; f <= 2000.0; f += 1.0)
        if (const double p = power(f); p > bestPower)
        {
            bestPower = p;
            best = f;
        }
    const double centre = best;
    for (double f = centre - 1.0; f <= centre + 1.0; f += 0.05)
        if (const double p = power(f); p > bestPower)
        {
            bestPower = p;
            best = f;
        }
    return best;
}

double settleTimeFor(Sound sound)
{
    return sound == Sound::piano ? 3.0 : 1.0;
}

void testSound(Sound sound, double rate)
{
    const std::string tag = std::string(soundName(sound)) + " @" + std::to_string(static_cast<int>(rate)) + ": ";
    const double settle = settleTimeFor(sound);

    {
        Rig rig(sound, rate);
        const auto stats = rig.run(0.1);
        check(stats.peak == 0.0f && rig.synth.isSilent(), tag + "silent before any note");
    }

    {
        Rig rig(sound, rate);
        rig.synth.noteOn(1, 60, 0.8f, 0);
        const auto stats = rig.run(0.2);
        check(stats.peak > 1.0e-3f && stats.finite, tag + "note-on produces audio (peak " + std::to_string(stats.peak) + ")");
    }

    {
        Rig rig(sound, rate, false);
        chordOn(rig.synth, sixNoteChord, 1.0f);
        auto stats = rig.run(1.5);
        check(stats.peak <= 0.7080f && stats.peak > 0.05f,
              tag + "6-note chord vel 127 peak " + std::to_string(stats.peak) + " <= -3 dBFS");
        chordOff(rig.synth, sixNoteChord);
        const double took = rig.secondsUntilSilent(settle, stats);
        check(took >= 0.0, tag + "note-off reaches silence in " + std::to_string(took) + " s (limit "
                               + std::to_string(settle) + " s)");
        check(stats.finite, tag + "no NaN/inf");
        const auto tail = rig.run(0.05);
        check(tail.peak == 0.0f, tag + "output exactly zero once silent");
    }

    {
        Rig rig(sound, rate);
        rig.synth.noteOn(1, 64, 0.7f, 123);
        const auto stats = rig.run(0.1);
        check(std::abs(stats.firstNonZero - 123) <= 1,
              tag + "sample-accurate onset at 123 (got " + std::to_string(stats.firstNonZero) + ")");
    }

    {
        Rig rig(sound, rate);
        rig.synth.noteOn(1, 64, 0.7f, blockSize + 77); // lands in the second render
        const auto stats = rig.run(0.1);
        check(std::abs(stats.firstNonZero - (blockSize + 77)) <= 1,
              tag + "onset carried into next block (got " + std::to_string(stats.firstNonZero) + ")");
    }

    {
        Rig rig(sound, rate);
        chordengine::midi::MidiEventList midi(64);
        midi.add(chordengine::midi::MidiEvent::noteOn(200, 1, 60, 100));
        rig.buffer.clear();
        rig.synth.process(rig.buffer, midi);
        Stats stats;
        rig.accumulate(stats, 0);
        check(std::abs(stats.firstNonZero - 200) <= 1,
              tag + "process() onset at 200 (got " + std::to_string(stats.firstNonZero) + ")");

        midi.clear();
        midi.add(chordengine::midi::MidiEvent::noteOn(10, 1, 60, 0)); // vel 0 = off
        rig.buffer.clear();
        rig.synth.process(rig.buffer, midi);
        double took = rig.secondsUntilSilent(settle, stats);
        check(took >= 0.0, tag + "process() note-on vel 0 releases");

        midi.clear();
        midi.add(chordengine::midi::MidiEvent::noteOn(0, 1, 60, 100));
        midi.add(chordengine::midi::MidiEvent::noteOn(0, 1, 64, 100));
        midi.add(chordengine::midi::MidiEvent::controller(300, 1, 120, 0));
        rig.buffer.clear();
        rig.synth.process(rig.buffer, midi);
        took = rig.secondsUntilSilent(0.05, stats);
        check(took >= 0.0, tag + "process() CC 120 fades out within 50 ms");

        midi.clear();
        midi.add(chordengine::midi::MidiEvent::noteOn(0, 1, 60, 100));
        midi.add(chordengine::midi::MidiEvent::controller(300, 1, 123, 0));
        rig.buffer.clear();
        rig.synth.process(rig.buffer, midi);
        took = rig.secondsUntilSilent(settle, stats);
        check(took >= 0.0 && stats.finite, tag + "process() CC 123 releases");
    }

    {
        Rig rig(sound, rate, false);
        for (int i = 0; i < 24; ++i)
            rig.synth.noteOn(1 + i % 3, 36 + i * 2, 1.0f, i * 7);
        auto stats = rig.run(0.5);
        for (int i = 0; i < 24; ++i)
            rig.synth.noteOn(1, 40 + i, 0.9f, i * 11); // re-strike while full
        const auto more = rig.run(0.5);
        check(stats.finite && more.finite && stats.peak <= 0.7080f && more.peak <= 0.7080f
                  && rig.synth.activeVoiceCount() <= InternalSynth::maxVoices,
              tag + "24-note voice stealing stays bounded (peak "
                  + std::to_string(std::max(stats.peak, more.peak)) + ")");
        rig.synth.allNotesOff();
        const double took = rig.secondsUntilSilent(settle, stats);
        check(took >= 0.0, tag + "allNotesOff silences in " + std::to_string(took) + " s");
    }

    {
        Rig rig(sound, rate);
        chordOn(rig.synth, sixNoteChord, 0.9f);
        rig.run(0.3);
        rig.synth.reset();
        const bool silentNow = rig.synth.isSilent();
        const auto after = rig.run(0.05);
        check(silentNow && after.peak == 0.0f, tag + "reset() silences immediately");
    }

    {
        // Two identical instruments: one renders onto a DC-filled buffer, the
        // other onto zeros. The difference must be exactly the DC.
        Rig onDc(sound, rate), onZero(sound, rate);
        for (auto* rig : { &onDc, &onZero })
            chordOn(rig->synth, { 57, 60, 64 }, 0.8f, 5);
        float worst = 0.0f;
        for (int block = 0; block < 20; ++block)
        {
            onDc.buffer.clear();
            for (int channel = 0; channel < 2; ++channel)
                std::fill(onDc.buffer.getWritePointer(channel), onDc.buffer.getWritePointer(channel) + blockSize, 0.25f);
            onZero.buffer.clear();
            onDc.synth.render(onDc.buffer, 0, blockSize);
            onZero.synth.render(onZero.buffer, 0, blockSize);
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < blockSize; ++i)
                    worst = std::max(worst, std::abs(onDc.buffer.getSample(channel, i) - 0.25f
                                                     - onZero.buffer.getSample(channel, i)));
        }
        check(worst < 1.0e-6f, tag + "render adds into existing buffer content");
    }

    {
        // render() with a start offset touches only its own range.
        Rig rig(sound, rate);
        rig.synth.noteOn(1, 60, 0.8f, 0);
        rig.buffer.clear();
        rig.synth.render(rig.buffer, 100, 200);
        bool outsideClean = true;
        for (int i = 0; i < blockSize; ++i)
            if ((i < 100 || i >= 300) && rig.buffer.getSample(0, i) != 0.0f)
                outsideClean = false;
        check(outsideClean && rig.buffer.getSample(1, 101) != 0.0f, tag + "render honours startSample/numSamples");
    }

    if (sound != Sound::piano)
    {
        Rig rig(sound, rate);
        rig.synth.noteOn(1, 69, 0.8f, 0);
        std::vector<float> capture;
        rig.run(0.2);
        rig.run(0.4, &capture);
        const double f = dominantFrequency(capture, rate);
        check(std::abs(f - 440.0) <= 440.0 * 0.02, tag + "A4 pitch estimate " + std::to_string(f) + " Hz");
    }
    else
    {
        // The piano's partials are slightly stretched; its fundamental is
        // still the strongest component in the 40 Hz..2 kHz range.
        Rig rig(sound, rate);
        rig.synth.noteOn(1, 57, 0.5f, 0);
        std::vector<float> capture;
        rig.run(0.05);
        rig.run(0.4, &capture);
        const double f = dominantFrequency(capture, rate);
        check(std::abs(f - 220.0) <= 220.0 * 0.02, tag + "A3 pitch estimate " + std::to_string(f) + " Hz");
    }
}

void testBassLowestNote(double rate)
{
    const std::string tag = "bass lowest-note @" + std::to_string(static_cast<int>(rate)) + ": ";
    {
        Rig rig(Sound::bass, rate, true);
        chordOn(rig.synth, { 60, 63, 67, 70 }, 0.8f);
        std::vector<float> capture;
        rig.run(0.2);
        const int voices = rig.synth.activeVoiceCount();
        rig.run(0.4, &capture);
        check(voices == 1, tag + "a 4-note chord sounds one voice (got " + std::to_string(voices) + ")");
        const double f = dominantFrequency(capture, rate);
        check(std::abs(f - 261.63) <= 261.63 * 0.02, tag + "plays the lowest note C4 (" + std::to_string(f) + " Hz)");

        rig.synth.noteOff(1, 60, 0); // lowest released: glides to Eb4
        capture.clear();
        rig.run(0.2);
        rig.run(0.4, &capture);
        const double g = dominantFrequency(capture, rate);
        check(std::abs(g - 311.13) <= 311.13 * 0.02 && rig.synth.activeVoiceCount() >= 1,
              tag + "follows the new lowest note Eb4 (" + std::to_string(g) + " Hz)");

        chordOff(rig.synth, { 63, 67, 70 });
        Stats stats;
        check(rig.secondsUntilSilent(1.0, stats) >= 0.0, tag + "releases when every key is up");
    }
    {
        Rig rig(Sound::bass, rate, false);
        chordOn(rig.synth, { 48, 52, 55 }, 0.8f);
        rig.run(0.1);
        check(rig.synth.activeVoiceCount() == 3, tag + "poly when the mode is off");
    }
    {
        // Sound change applies to the next note-on only; the held mono bass
        // still releases correctly after switching away.
        Rig rig(Sound::bass, rate, true);
        rig.synth.noteOn(1, 40, 0.8f, 0);
        rig.run(0.1);
        rig.synth.setSound(Sound::synth);
        chordOn(rig.synth, { 60, 64, 67 }, 0.8f);
        rig.run(0.1);
        const int voices = rig.synth.activeVoiceCount();
        rig.synth.noteOff(1, 40, 0);
        chordOff(rig.synth, { 60, 64, 67 });
        Stats stats;
        check(voices == 4 && rig.secondsUntilSilent(1.0, stats) >= 0.0,
              tag + "switching sound keeps old notes and releases them");
    }
}

void testLevel()
{
    Rig rig(Sound::synth, 44100.0);
    chordOn(rig.synth, { 60, 64, 67 }, 0.8f);
    const auto loud = rig.run(0.3);
    rig.synth.setLevel(0.25f);
    rig.run(0.1); // smoothing
    const auto quiet = rig.run(0.2);
    rig.synth.setLevel(0.0f);
    rig.run(0.1);
    const auto muted = rig.run(0.1);
    check(quiet.peak < loud.peak * 0.4f && muted.peak < 1.0e-6f, "level: setLevel scales and mutes the output");
}
} // namespace

int main()
{
    for (const double rate : { 44100.0, 96000.0 })
    {
        for (const Sound sound : { Sound::synth, Sound::piano, Sound::bass })
            testSound(sound, rate);
        testBassLowestNote(rate);
    }
    testLevel();

    std::cout << "ChordEngine internal synth: " << casesPassed << " passed, " << casesFailed << " failed\n";
    return casesFailed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
