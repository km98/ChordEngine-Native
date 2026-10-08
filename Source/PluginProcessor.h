#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "Core/ChordEngineCore.h"
#include "MIDI/MidiNoteTracker.h"
#include "State/PluginState.h"

class ChordEngineAudioProcessor final : public juce::AudioProcessor
{
public:
    ChordEngineAudioProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    chordengine::core::CoreConfiguration configurationSnapshot() const noexcept;
    chordengine::state::PluginState stateSnapshot() const noexcept
    {
        return { configurationSnapshot() };
    }
    bool setConfiguration(const chordengine::core::CoreConfiguration& configuration) noexcept;
    bool setKeyIndex(int keyIndex) noexcept;
    bool setScale(chordengine::core::ScaleId scale) noexcept;
    bool setChordPreset(chordengine::core::ChordPresetId preset) noexcept;
    bool setTransposeTarget(chordengine::core::TransposeTarget target) noexcept;
    bool setTransposeOctave(int octaveSteps) noexcept;
    bool setVelocityMode(chordengine::core::VelocityMode mode) noexcept;
    bool setFixedVelocity(int velocity) noexcept;
    bool armRecorder() noexcept;
    bool cancelRecorder() noexcept;
    bool stopRecorder() noexcept;
    std::uint8_t recorderState() const noexcept { return recorderState_.load(std::memory_order_acquire); }
    std::uint8_t recorderTakeCount() const noexcept { return recorderTakeCount_.load(std::memory_order_acquire); }
    int recorderRecordedEventCount() const noexcept { return recorderRecordedEventCount_.load(std::memory_order_acquire); }
    bool isRecorderOverflowed() const noexcept { return recorderOverflowed_.load(std::memory_order_acquire); }
    static constexpr const char* helpFeedbackUrl = "https://music-prod.com/plugin/chordengine-feedback";

    // ------------------------------------------------------------------
    // Licensing gate. The audio path READS one lock-free flag; the message
    // thread (LicensingBoundary) is the only writer. When the gate is closed
    // an unlicensed trial has expired and the reference generates no chord at
    // all - no MIDI, no visual state (reference onNoteOn: "an expired,
    // unlicensed trial generates NO chord at all").
    // ------------------------------------------------------------------
    void setLicensingGateOpen(bool open) noexcept
    {
        licensingGateOpen_.store(open, std::memory_order_release);
    }
    bool licensingGateOpen() const noexcept
    {
        return licensingGateOpen_.load(std::memory_order_acquire);
    }
    // Set by the audio thread when a chord is actually generated; the trial
    // usage clock may only start after the first real chord (reference).
    bool consumePlaybackActivity() noexcept
    {
        return playbackActivityPending_.exchange(false, std::memory_order_acq_rel);
    }

    // ------------------------------------------------------------------
    // On-screen keyboard injection (piano keys). The editor NEVER resolves a
    // chord itself: it pushes a raw note event through this lock-free FIFO and
    // processBlock feeds it into the exact same Core path an external MIDI
    // keyboard uses (noteOn -> core_.process -> generated chord events).
    // Single-producer (message thread) / single-consumer (audio thread).
    // ------------------------------------------------------------------
    void pushUiNoteOn(int channel, int note, int velocity) noexcept;
    void pushUiNoteOff(int channel, int note) noexcept;

    bool isRecorderRecording() const noexcept { return recorderState_.load(std::memory_order_acquire) == 2; }
    bool exportRecorderTake(std::uint8_t newestFirstIndex, juce::File& destination) const;
    // Read-only take summary for the UI take rows (never used by the audio path).
    int recorderTakeNoteCount(std::uint8_t newestFirstIndex) const noexcept;

    // Read-only snapshots used for UI visualization and integration tests.
    bool isOutputNoteActive(std::uint8_t channel, std::uint8_t note) const noexcept
    {
        return channel >= 1 && channel <= 16 && note <= 127
            && outputNoteOwners_[channel - 1][note].load(std::memory_order_relaxed) != 0;
    }
    bool isTriggerNoteActive(std::uint8_t channel, std::uint8_t note) const noexcept
    {
        return channel >= 1 && channel <= 16 && note <= 127
            && triggerNoteActive_[channel - 1][note].load(std::memory_order_relaxed);
    }
    bool isTriggerNoteDown(std::uint8_t channel, std::uint8_t note) const noexcept
    {
        return channel >= 1 && channel <= 16 && note <= 127
            && triggerNoteDown_[channel - 1][note].load(std::memory_order_relaxed);
    }
    bool triggerNoteSustained(std::uint8_t channel, std::uint8_t note) const noexcept
    {
        return channel >= 1 && channel <= 16 && note <= 127
            && triggerNoteSustained_[channel - 1][note].load(std::memory_order_relaxed);
    }
    // Existing input-tracker snapshot retained for processor integration tests.
    bool trackedNoteSustained(std::uint8_t channel, std::uint8_t note) const noexcept
    {
        return tracker_.isSustained(channel, note);
    }
    bool isAnyTriggerNoteDown(std::uint8_t note) const noexcept
    {
        if (note > 127)
            return false;
        for (const auto& channel : triggerNoteDown_)
            if (channel[note].load(std::memory_order_relaxed))
                return true;
        return false;
    }
    bool isAnyTriggerNoteActive(std::uint8_t note) const noexcept
    {
        if (note > 127)
            return false;
        for (const auto& channel : triggerNoteActive_)
            if (channel[note].load(std::memory_order_relaxed))
                return true;
        return false;
    }
    bool isAnyOutputNoteActive(std::uint8_t note) const noexcept
    {
        if (note > 127)
            return false;
        for (const auto& channel : outputNoteOwners_)
            if (channel[note].load(std::memory_order_relaxed) > 0)
                return true;
        return false;
    }
    std::uint32_t activeTriggerCount() const noexcept { return activeTriggerCount_.load(std::memory_order_relaxed); }
    std::uint32_t activeOutputNoteCount() const noexcept { return activeOutputNoteCount_.load(std::memory_order_relaxed); }
    chordengine::core::NoteSet previewChordForTrigger(int midiNote) const noexcept;
    // The on-screen keyboard's note range (C2..C7) and the input channel and
    // trigger velocity it uses, so the GUI and its tests share one definition.
    static constexpr int uiPianoChannel = 1;
    static constexpr int uiPianoVelocity = 100;
    static constexpr int pianoFirstNote = 36;
    static constexpr int pianoLastNote = 96;
    static constexpr int defaultEditorWidth = 800;
    static constexpr int defaultEditorHeight = 650;
    static constexpr int minimumEditorWidth = 640;
    static constexpr int minimumEditorHeight = 520;
    static constexpr int maximumEditorWidth = 1280;
    static constexpr int maximumEditorHeight = 1040;

private:
    void syncStateFromAudioThread() noexcept;
    void updateNoteState(const chordengine::midi::NoteEvent& event) noexcept;
    void updateOutputNoteState(const chordengine::midi::NoteEvent& event) noexcept;
    void clearSustainedTriggerStates() noexcept;
    void clearOutputNoteStates() noexcept;
    void updateConfigurationField(std::uint64_t mask, std::uint64_t value) noexcept;
    struct RecorderMidiEvent
    {
        std::int64_t tick = 0;
        std::uint8_t channel = 1;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        bool noteOn = false;
    };
    struct RecorderNote
    {
        bool active = false;
        int channel = 1;
        int note = 0;
        int velocity = 0;
        std::int64_t startTick = 0;
        std::size_t startEventIndex = 0;
    };
    struct RecorderTake
    {
        std::array<RecorderMidiEvent, 4096> events {};
        std::size_t eventCount = 0;
        double tempoBpm = 120.0;
        int noteCount = 0;
    };
    static constexpr std::size_t recorderEventCapacity = 4096;
    void recordGeneratedEvent(const chordengine::midi::NoteEvent& event) noexcept;
    void resetRecorderCapture() noexcept;
    bool finalizeRecorderTake() noexcept;
    void resetRecorderForPlaybackBoundary() noexcept;
    void acquireRecorderCapture() noexcept;
    void releaseRecorderCapture() noexcept;

    // On-screen keyboard transport (see pushUiNoteOn).
    struct UiMidiEvent
    {
        std::uint8_t channel = 1;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        bool noteOn = false;
    };
    static constexpr int uiMidiQueueCapacity = 64;
    void drainUiMidi(juce::MidiBuffer& destination) noexcept;
    juce::AbstractFifo uiMidiFifo_ { uiMidiQueueCapacity };
    std::array<UiMidiEvent, uiMidiQueueCapacity> uiMidiEvents_ {};
    chordengine::core::ChordEngineCore core_;
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "The MIDI-thread configuration snapshot must be lock-free");
    static_assert(std::atomic<bool>::is_always_lock_free,
                  "The MIDI-thread UI note snapshots must be lock-free");
    static_assert(std::atomic<std::uint16_t>::is_always_lock_free,
                  "The MIDI-thread output-note visualization must be lock-free");
    // Packed fields: key[0:3], scale[4:7], preset[8:11], target[12:13],
    // octave+2[14:16], velocity mode[17:18], fixed velocity[19:25].
    std::atomic<std::uint64_t> configurationBits_ { 0 };
    std::uint64_t appliedConfigurationBits_ = 0;
    std::array<std::array<std::atomic<std::uint16_t>, 128>, 16> outputNoteOwners_ {};
    std::array<std::array<std::atomic<bool>, 128>, 16> triggerNoteActive_ {};
    std::array<std::array<std::atomic<bool>, 128>, 16> triggerNoteDown_ {};
    std::array<std::array<std::atomic<bool>, 128>, 16> triggerNoteSustained_ {};
    std::array<std::atomic<bool>, 16> sustainPedalDown_ {};
    std::atomic<std::uint32_t> activeTriggerCount_ { 0 };
    std::atomic<std::uint32_t> activeOutputNoteCount_ { 0 };
    std::atomic<bool> licensingGateOpen_ { true };
    std::atomic<bool> playbackActivityPending_ { false };
    std::atomic<std::uint8_t> recorderState_ { 0 }; // idle, armed, recording, transitioning
    std::atomic_flag recorderCaptureAccess_ = ATOMIC_FLAG_INIT;
    std::atomic<std::uint8_t> recorderTakeCount_ { 0 };
    std::atomic<int> recorderRecordedEventCount_ { 0 };
    std::atomic<bool> recorderOverflowed_ { false };
    std::array<RecorderMidiEvent, recorderEventCapacity> recorderEventBuffer_ {};
    std::size_t recorderEventCount_ = 0;
    std::int64_t recorderLastTick_ = 0;
    std::array<RecorderNote, 256> recorderOpenNotes_ {};
    std::size_t recorderOpenNoteCount_ = 0;
    std::array<RecorderTake, 3> recorderTakes_ {};
    std::atomic<std::size_t> recorderTakeCountInternal_ { 0 };
    std::int64_t recorderStartSample_ = 0;
    std::atomic<std::uint64_t> processedSampleCount_ { 0 };
    double recorderSampleRate_ = 44100.0;
    double recorderTempoBpm_ = 120.0;
    bool recorderOverflow_ = false;
    mutable std::atomic_flag recorderTakeAccess_ = ATOMIC_FLAG_INIT;
    chordengine::midi::MidiNoteTracker tracker_;
    chordengine::core::CoreResult coreResult_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordEngineAudioProcessor)
};
