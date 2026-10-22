#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "Audio/AudioBlock.h"
#include "Audio/InternalSynth.h"
#include "Core/ChordEngineCore.h"
#include "Licensing/LicensingRuntime.h"
#include "MIDI/MidiEvents.h"
#include "MIDI/MidiNoteTracker.h"
#include "State/PluginState.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

// Builds with an audio output (the VST3 and the ChordEngine Instrument AU) can
// play the generated chords through the internal synth. The MIDI FX AU has no
// audio bus at all: Logic Pro takes no audio from a MIDI FX slot.
#ifndef CHORDENGINE_AUDIO_OUTPUT
 #define CHORDENGINE_AUDIO_OUTPUT 0
#endif
// The instrument variant starts with internal audio on; the others off.
#ifndef CHORDENGINE_INTERNAL_AUDIO_DEFAULT_ON
 #define CHORDENGINE_INTERNAL_AUDIO_DEFAULT_ON 0
#endif

// What the host reports about its transport for the current block. Every
// field is optional because hosts differ in what they provide; the adapters
// (AU, VST3) fill it before each processBlock().
struct HostTransport
{
    bool isPlaying = false;
    bool isRecording = false;
    std::optional<double> bpm;
    std::optional<double> ppqPosition;          // at the block's first sample
    std::optional<double> ppqPositionOfLastBarStart;
    std::optional<int> timeSignatureNumerator;
    std::optional<int> timeSignatureDenominator;
};

// =====================================================================
// The ChordEngine processor: the MIDI engine, the recorder, the internal
// synth, the host state and the licensing runtime, in plain C++. The format
// adapters (Source/AU, Source/VST3) host it; the AppKit editor displays it.
// =====================================================================
class ChordEngineAudioProcessor final
{
public:
    explicit ChordEngineAudioProcessor(bool withAudioOutput = CHORDENGINE_AUDIO_OUTPUT != 0);
    ~ChordEngineAudioProcessor();
    ChordEngineAudioProcessor(const ChordEngineAudioProcessor&) = delete;
    ChordEngineAudioProcessor& operator=(const ChordEngineAudioProcessor&) = delete;

    // Allocates (not on the audio thread).
    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    // Output channel counts this build accepts on its one output bus (none for
    // the MIDI FX; mono or stereo for the builds with internal audio).
    bool supportsOutputChannels(int channels) const noexcept
    {
        return hasAudioOutput_ ? (channels == 1 || channels == 2) : channels == 0;
    }

    // One block. `midi` holds the host's input events (sorted by offset) and
    // is replaced by the output events, as the 0.5 processor did. `audio`
    // may have no channels (the MIDI FX). Real-time safe after prepareToPlay.
    void processBlock(const chordengine::audio::AudioBlock& audio, chordengine::midi::MidiEventList& midi,
                      const HostTransport& transport = {});

    static constexpr std::size_t midiEventCapacity = 4096;

    bool acceptsMidi() const noexcept { return true; }
    bool producesMidi() const noexcept { return true; }
    bool isMidiEffect() const noexcept { return !hasAudioOutput_; }
    double getTailLengthSeconds() const noexcept { return hasAudioOutput_ ? 3.0 : 0.0; }

    // The host state: a UTF-8 XML document (see State/PluginState.h), the
    // same bytes 0.5.x wrote.
    std::string getStateInformation() const;
    void setStateInformation(const void* data, std::size_t sizeInBytes);

    chordengine::core::CoreConfiguration configurationSnapshot() const noexcept;
    chordengine::state::PluginState stateSnapshot() const;
    void applyState(const chordengine::state::PluginState& state);

    // ------------------------------------------------------------------
    // Pack presets. A preset from an installed pack replaces the stock preset
    // for chord generation until it is cleared (selecting a stock preset in
    // the editor clears it). Message thread only; the audio thread picks the
    // change up at its next block, lock-free.
    // ------------------------------------------------------------------
    using PackPreset = chordengine::state::PackPresetState;
    bool setPackPreset(const PackPreset& preset);
    void clearPackPreset();
    std::optional<PackPreset> packPreset() const;

    // ------------------------------------------------------------------
    // Internal audio: the generated chords played by a built-in synth, piano
    // or bass. Only builds with an audio output can sound.
    // ------------------------------------------------------------------
    using InternalSound = chordengine::state::InternalSound;
    bool hasAudioOutput() const noexcept { return hasAudioOutput_; }
    void setInternalAudioEnabled(bool enabled) noexcept;
    bool internalAudioEnabled() const noexcept { return internalAudioEnabled_.load(std::memory_order_acquire); }
    void setInternalSound(InternalSound sound) noexcept;
    InternalSound internalSound() const noexcept
    {
        return static_cast<InternalSound>(internalSound_.load(std::memory_order_acquire));
    }
    // The grid a recorded take is quantized to when it is exported (dragged
    // out or written to a file); none exports it exactly as played.
    using RecordQuantize = chordengine::midi::TakeQuantize;
    void setRecordQuantize(RecordQuantize grid) noexcept;
    RecordQuantize recordQuantize() const noexcept
    {
        return static_cast<RecordQuantize>(recordQuantize_.load(std::memory_order_acquire));
    }
    // FOLLOW DAW RECORD: when on, the recorder arms itself when the host
    // starts recording and stores the take when it stops (see
    // followHostRecording). Off by default; saved with the host state.
    void setFollowHostRecord(bool enabled) noexcept;
    bool followHostRecord() const noexcept { return followHostRecord_.load(std::memory_order_acquire); }
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
    // Licensing. The processor owns the licensing runtime, so the account
    // check, the offline window and the trial clock run whether or not the
    // editor is open; the editor only displays it and forwards its buttons.
    // Message thread only.
    chordengine::licensing::LicensingRuntime& licensing() noexcept { return *licensing_; }
    // Where auth.json lives: Music-Prod/ChordEngine in Application Support,
    // or a private temporary folder in the isolated test builds.
    static std::filesystem::path defaultLicensingStorageDir();

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
    bool exportRecorderTake(std::uint8_t newestFirstIndex, const std::filesystem::path& destination) const;
    // The same file as bytes (what exportRecorderTake writes).
    std::optional<std::string> exportRecorderTakeBytes(std::uint8_t newestFirstIndex) const;
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
        chordengine::midi::TakeGridAnchor anchor {};
        int timeSignatureNumerator = 4;
        int timeSignatureDenominator = 4;
    };
    static constexpr std::size_t recorderEventCapacity = 4096;
    void recordGeneratedEvent(const chordengine::midi::NoteEvent& event) noexcept;
    void resetRecorderCapture() noexcept;
    bool finalizeRecorderTake() noexcept;
    void resetRecorderForPlaybackBoundary() noexcept;
    void followHostRecording() noexcept;
    void renderInternalAudio(const chordengine::audio::AudioBlock& audio,
                             const chordengine::midi::MidiEventList& generated) noexcept;
    // Audio thread only: the host transport's record state last block, and
    // whether the current take was armed by it (see followHostRecording).
    bool hostWasRecording_ = false;
    bool hostArmedTake_ = false;
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
    void drainUiMidi(chordengine::midi::MidiEventList& destination) noexcept;
    // Single-producer / single-consumer ring: the message thread writes at
    // uiMidiWrite_, the audio thread reads at uiMidiRead_. One slot stays free.
    std::array<UiMidiEvent, uiMidiQueueCapacity> uiMidiEvents_ {};
    std::atomic<int> uiMidiWrite_ { 0 };
    std::atomic<int> uiMidiRead_ { 0 };
    void pushUiEvent(const UiMidiEvent& event) noexcept;
    // The current block's transport (audio thread).
    HostTransport transport_ {};
    // Preallocated in prepareToPlay: the block's output events.
    chordengine::midi::MidiEventList blockOutput_;
    chordengine::core::ChordEngineCore core_;
    const bool hasAudioOutput_;

    // Pack preset hand-off: the message thread writes pendingCustomPreset_
    // under customPresetAccess_ and bumps the generation; the audio thread
    // copies it only if it can take the flag without waiting.
    mutable std::atomic_flag customPresetAccess_ = ATOMIC_FLAG_INIT;
    chordengine::core::CustomChordPreset pendingCustomPreset_ {};
    bool pendingCustomPresetActive_ = false;
    std::atomic<std::uint32_t> customPresetGeneration_ { 0 };
    std::uint32_t appliedCustomPresetGeneration_ = 0;
    void applyCustomPresetFromAudioThread() noexcept;
    // What the editor and the host state see; message thread / state calls.
    mutable std::atomic_flag packPresetLock_ = ATOMIC_FLAG_INIT;
    std::optional<PackPreset> packPresetSelection_;

    chordengine::audio::InternalSynth internalSynth_;
    std::atomic<bool> internalAudioEnabled_ { CHORDENGINE_INTERNAL_AUDIO_DEFAULT_ON != 0 };
    std::atomic<std::uint8_t> internalSound_ { 0 };
    bool internalAudioWasEnabled_ = false; // audio thread
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
    // Closed until the licensing runtime has published its first decision
    // (in the constructor, before any audio block).
    std::atomic<bool> licensingGateOpen_ { false };
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
    chordengine::midi::TakeGridAnchor recorderAnchor_ {};
    int recorderTimeSignatureNumerator_ = 4;
    int recorderTimeSignatureDenominator_ = 4;
    void captureRecorderGridAnchor(const HostTransport& position, int sampleOffset) noexcept;
    std::atomic<std::uint8_t> recordQuantize_ { 0 };
    std::atomic<bool> followHostRecord_ { false };
    bool recorderOverflow_ = false;
    mutable std::atomic_flag recorderTakeAccess_ = ATOMIC_FLAG_INIT;
    chordengine::midi::MidiNoteTracker tracker_;
    chordengine::core::CoreResult coreResult_;

    // Declared last: destroyed first, while the gate it publishes to exists.
    std::filesystem::path isolatedLicensingDir_;
    std::unique_ptr<chordengine::licensing::LicensingRuntime> licensing_;
};
