#pragma once

#include "AudioBlock.h"
#include "MIDI/MidiEvents.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace chordengine::audio
{
// A small preview instrument so generated chords can be heard without routing
// MIDI to another instrument. Three fixed sounds, 16 preallocated voices.
//
// Threading: setSound/setLevel/setBassLowestNoteOnly are lock-free and may be
// called from any thread; they are latched at the start of each render.
// prepare() is the only call that allocates and must not run concurrently with
// rendering. Every other call belongs to the audio thread and is free of
// allocation, locks and I/O.
//
// Note events are queued with a sample offset relative to the start of the
// NEXT render() call, so noteOn(..., 37) followed by render(buffer, 0, 512)
// starts the note on sample 37 of that render.
class InternalSynth
{
public:
    enum class Sound : std::uint8_t { synth, piano, bass };

    static constexpr int maxVoices = 16;

    InternalSynth() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;              // silence immediately

    void setSound(Sound sound) noexcept;
    Sound getSound() const noexcept { return sound_.load(std::memory_order_relaxed); }
    void setLevel(float linearGain) noexcept; // 0..1, smoothed
    float getLevel() const noexcept { return level_.load(std::memory_order_relaxed); }

    // Bass only: voice just the lowest held note (monophonic, legato when the
    // lowest key is released, retriggered when the new lowest note is struck).
    // Default on.
    void setBassLowestNoteOnly(bool enabled) noexcept;
    bool getBassLowestNoteOnly() const noexcept { return bassLowestOnly_.load(std::memory_order_relaxed); }

    // channel is one-based (1..16) like the rest of ChordEngine; out-of-range
    // channels are clamped. velocity01 <= 0 is treated as a note-off.
    void noteOn(int channel, int note, float velocity01, int sampleOffset) noexcept;
    void noteOff(int channel, int note, int sampleOffset) noexcept;
    void allNotesOff() noexcept;        // graceful release, immediate

    // Renders numSamples and ADDS the mono result into every channel of
    // buffer from startSample on.
    void render(const AudioBlock& buffer, int startSample, int numSamples) noexcept;

    // Queues the block's note on/off (velocity 0 = off), CC 120 (all sound
    // off: fast fade) and CC 123 (all notes off: release) sample-accurately,
    // then renders the whole buffer additively.
    void process(const AudioBlock& buffer, const midi::MidiEventList& midi) noexcept;

    bool isSilent() const noexcept;
    int activeVoiceCount() const noexcept;

private:
    enum class EventKind : std::uint8_t { noteOn, noteOff, allNotesOff, allSoundOff };
    struct Event
    {
        EventKind kind = EventKind::noteOn;
        std::uint8_t channel = 0; // zero-based internally
        std::uint8_t note = 0;
        float velocity = 0.0f;
        int offset = 0;
    };

    struct Envelope
    {
        enum class Stage : std::uint8_t { off, attack, decay, sustain, release };
        Stage stage = Stage::off;
        float value = 0.0f;
        float attackStep = 1.0f;
        float decayCoeff = 0.0f;
        float sustain = 1.0f;
        float releaseCoeff = 0.0f;

        float next() noexcept;
    };

    struct Svf // TPT state-variable low-pass
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        void clear() noexcept { ic1 = ic2 = 0.0f; }
        float lowpass(float input, float g, float k) noexcept;
    };

    static constexpr int pianoPartials = 10;

    struct Voice
    {
        enum class State : std::uint8_t { idle, held, released, stealing };
        State state = State::idle;
        Sound sound = Sound::synth;
        bool mono = false;
        std::uint8_t channel = 0;
        std::uint8_t note = 0;
        float velocity = 0.0f;
        std::uint64_t age = 0;

        float frequency = 440.0f;
        float targetFrequency = 440.0f;
        std::array<float, 3> phase{};
        Envelope amp;
        float filterEnv = 0.0f;
        float filterEnvCoeff = 0.0f;
        float cutoffBase = 1000.0f;
        float cutoffDepth = 0.0f;
        Svf filterA, filterB;
        float gain = 0.0f;

        // Piano: rotating phasors with a fast and a slow decay per partial.
        std::array<float, pianoPartials> re{}, im{}, rotC{}, rotS{};
        std::array<float, pianoPartials> fastAmp{}, slowAmp{}, fastCoeff{}, slowCoeff{};
        int activePartials = 0;
        float hammer = 0.0f, hammerCoeff = 0.0f, hammerLp = 0.0f, hammerLp2 = 0.0f, hammerLpCoeff = 0.0f;
        std::uint32_t noise = 1;
        int renormCounter = 0;

        // Quick fade used when the voice is stolen (or CC 120); a pending
        // note, if any, starts when it reaches zero.
        float stealGain = 1.0f;
        float stealStep = 0.0f;
        bool hasPending = false;
        Event pending;
        Sound pendingSound = Sound::synth;
        bool pendingMono = false;
    };

    void enqueue(const Event& event) noexcept;
    void applyEvent(const Event& event) noexcept;
    void handleNoteOn(int channel, int note, float velocity) noexcept;
    void handleNoteOff(int channel, int note) noexcept;
    void releaseAll() noexcept;
    void fadeAll() noexcept;
    void resolveMono() noexcept;
    int lowestHeldNote() const noexcept;

    int allocateVoice() noexcept;
    int triggerVoice(int channel, int note, float velocity, Sound sound, bool mono) noexcept;
    void releaseVoice(Voice& voice) noexcept;
    void startVoice(Voice& voice, int channel, int note, float velocity, Sound sound, bool mono) noexcept;
    void renderVoice(Voice& voice, float* output, int numSamples) noexcept;
    float renderSynth(Voice& voice) noexcept;
    float renderPiano(Voice& voice) noexcept;
    float renderBass(Voice& voice) noexcept;
    float coeffForTime(float seconds) const noexcept;

    static float polyBlep(float t, float dt) noexcept;
    // Output stage: fixed headroom gain, then a zero-latency peak limiter.
    float limit(float x) noexcept;

    std::atomic<Sound> sound_{ Sound::synth };
    std::atomic<float> level_{ 0.8f };
    std::atomic<bool> bassLowestOnly_{ true };

    double sampleRate_ = 0.0;
    float sampleRateF_ = 44100.0f;
    std::vector<float> scratch_;
    LinearSmoothed levelSmoothed_;
    float limiterGain_ = 1.0f;
    float limiterRelease_ = 0.0f;

    Sound blockSound_ = Sound::synth;
    bool blockBassMono_ = true;

    std::array<Voice, maxVoices> voices_{};
    std::uint64_t ageCounter_ = 0;

    static constexpr int maxEvents = 1024;
    std::array<Event, maxEvents> events_{};
    int eventCount_ = 0;

    // Held keys across all channels, for the bass lowest-note mode.
    std::array<std::array<bool, 128>, 16> held_{};
    std::array<std::uint8_t, 128> heldCount_{};
    std::array<float, 128> heldVelocity_{};
    std::array<bool, 128> struckThisBatch_{};
    bool monoDirty_ = false;
    int monoVoice_ = -1;
};
} // namespace chordengine::audio
