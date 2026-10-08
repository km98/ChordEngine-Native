#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <thread>

namespace
{
constexpr std::uint64_t keyMask = 0xfull;
constexpr std::uint64_t scaleMask = 0xfull << 4;
constexpr std::uint64_t presetMask = 0xfull << 8;
constexpr std::uint64_t targetMask = 0x3ull << 12;
constexpr std::uint64_t octaveMask = 0x7ull << 14;
constexpr std::uint64_t modeMask = 0x3ull << 17;
constexpr std::uint64_t velocityMask = 0x7full << 19;

// Pack the GUI-selected musical values into one lock-free snapshot. Control
// setters update individual fields atomically; processBlock applies one coherent
// snapshot at its next block boundary.
std::uint64_t encodeConfiguration(const chordengine::core::CoreConfiguration& configuration) noexcept
{
    return static_cast<std::uint64_t>(configuration.keyIndex)
        | (static_cast<std::uint64_t>(configuration.scale) << 4)
        | (static_cast<std::uint64_t>(configuration.preset) << 8)
        | (static_cast<std::uint64_t>(configuration.transpose.target) << 12)
        | (static_cast<std::uint64_t>(configuration.transpose.octaveSteps + 2) << 14)
        | (static_cast<std::uint64_t>(configuration.velocityMode) << 17)
        | (static_cast<std::uint64_t>(configuration.fixedVelocity) << 19);
}

chordengine::core::CoreConfiguration decodeConfiguration(std::uint64_t bits) noexcept
{
    using namespace chordengine::core;
    CoreConfiguration configuration;
    configuration.keyIndex = static_cast<int>(bits & keyMask);
    configuration.scale = static_cast<ScaleId>((bits & scaleMask) >> 4);
    configuration.preset = static_cast<ChordPresetId>((bits & presetMask) >> 8);
    configuration.transpose.target = static_cast<TransposeTarget>((bits & targetMask) >> 12);
    configuration.transpose.octaveSteps = static_cast<int>((bits & octaveMask) >> 14) - 2;
    configuration.velocityMode = static_cast<VelocityMode>((bits & modeMask) >> 17);
    configuration.fixedVelocity = static_cast<std::uint8_t>((bits & velocityMask) >> 19);
    return configuration;
}
}

ChordEngineAudioProcessor::ChordEngineAudioProcessor()
    : juce::AudioProcessor(BusesProperties())
{
    configurationBits_.store(encodeConfiguration(core_.configuration()), std::memory_order_relaxed);
}

chordengine::core::CoreConfiguration ChordEngineAudioProcessor::configurationSnapshot() const noexcept
{
    return decodeConfiguration(configurationBits_.load(std::memory_order_acquire));
}

void ChordEngineAudioProcessor::updateConfigurationField(std::uint64_t mask,
                                                           std::uint64_t value) noexcept
{
    auto current = configurationBits_.load(std::memory_order_relaxed);
    auto updated = (current & ~mask) | (value & mask);
    while (!configurationBits_.compare_exchange_weak(current, updated,
               std::memory_order_release, std::memory_order_relaxed))
        updated = (current & ~mask) | (value & mask);
}

bool ChordEngineAudioProcessor::setConfiguration(
    const chordengine::core::CoreConfiguration& configuration) noexcept
{
    using namespace chordengine::core;
    if (ChordPresetSystem::get(configuration.preset) == nullptr
        || ScaleSystem::find(configuration.scale) == nullptr
        || !ScaleSystem::keyOffset(configuration.keyIndex)
        || configuration.fixedVelocity < 1 || configuration.fixedVelocity > 127
        || configuration.transpose.octaveSteps < TransposeEngine::minimumOctaveSteps
        || configuration.transpose.octaveSteps > TransposeEngine::maximumOctaveSteps
        || (configuration.transpose.target != TransposeTarget::whole
            && configuration.transpose.target != TransposeTarget::lowest
            && configuration.transpose.target != TransposeTarget::highest)
        || (configuration.velocityMode != VelocityMode::dynamic
            && configuration.velocityMode != VelocityMode::maximum
            && configuration.velocityMode != VelocityMode::fixed))
        return false;
    configurationBits_.store(encodeConfiguration(configuration), std::memory_order_release);
    return true;
}

bool ChordEngineAudioProcessor::setKeyIndex(int keyIndex) noexcept
{
    if (!chordengine::core::ScaleSystem::keyOffset(keyIndex))
        return false;
    updateConfigurationField(keyMask, static_cast<std::uint64_t>(keyIndex));
    return true;
}

bool ChordEngineAudioProcessor::setScale(chordengine::core::ScaleId scale) noexcept
{
    if (chordengine::core::ScaleSystem::find(scale) == nullptr)
        return false;
    updateConfigurationField(scaleMask, static_cast<std::uint64_t>(scale) << 4);
    return true;
}

bool ChordEngineAudioProcessor::setChordPreset(chordengine::core::ChordPresetId preset) noexcept
{
    if (chordengine::core::ChordPresetSystem::get(preset) == nullptr)
        return false;
    updateConfigurationField(presetMask, static_cast<std::uint64_t>(preset) << 8);
    return true;
}

bool ChordEngineAudioProcessor::setTransposeTarget(chordengine::core::TransposeTarget target) noexcept
{
    if (target != chordengine::core::TransposeTarget::whole
        && target != chordengine::core::TransposeTarget::lowest
        && target != chordengine::core::TransposeTarget::highest)
        return false;
    updateConfigurationField(targetMask, static_cast<std::uint64_t>(target) << 12);
    return true;
}

bool ChordEngineAudioProcessor::setTransposeOctave(int octaveSteps) noexcept
{
    if (octaveSteps < chordengine::core::TransposeEngine::minimumOctaveSteps
        || octaveSteps > chordengine::core::TransposeEngine::maximumOctaveSteps)
        return false;
    updateConfigurationField(octaveMask, static_cast<std::uint64_t>(octaveSteps + 2) << 14);
    return true;
}

bool ChordEngineAudioProcessor::setVelocityMode(chordengine::core::VelocityMode mode) noexcept
{
    if (mode != chordengine::core::VelocityMode::dynamic
        && mode != chordengine::core::VelocityMode::maximum
        && mode != chordengine::core::VelocityMode::fixed)
        return false;
    updateConfigurationField(modeMask, static_cast<std::uint64_t>(mode) << 17);
    return true;
}

bool ChordEngineAudioProcessor::setFixedVelocity(int velocity) noexcept
{
    if (velocity < 1 || velocity > 127)
        return false;
    updateConfigurationField(velocityMask, static_cast<std::uint64_t>(velocity) << 19);
    return true;
}

bool ChordEngineAudioProcessor::armRecorder() noexcept
{
    std::uint8_t expected = 0;
    if (!recorderState_.compare_exchange_strong(expected, 3, std::memory_order_acq_rel))
        return false;
    acquireRecorderCapture();
    resetRecorderCapture();
    recorderState_.store(1, std::memory_order_release);
    releaseRecorderCapture();
    return true;
}

bool ChordEngineAudioProcessor::cancelRecorder() noexcept
{
    std::uint8_t expected = 1;
    if (!recorderState_.compare_exchange_strong(expected, 3, std::memory_order_acq_rel))
        return false;
    acquireRecorderCapture();
    resetRecorderCapture();
    recorderState_.store(0, std::memory_order_release);
    releaseRecorderCapture();
    return true;
}

bool ChordEngineAudioProcessor::stopRecorder() noexcept
{
    std::uint8_t expected = 2;
    if (!recorderState_.compare_exchange_strong(expected, 3, std::memory_order_acq_rel))
        return false;
    acquireRecorderCapture();
    const bool stored = finalizeRecorderTake();
    recorderState_.store(0, std::memory_order_release);
    releaseRecorderCapture();
    return stored;
}

void ChordEngineAudioProcessor::resetRecorderForPlaybackBoundary() noexcept
{
    auto state = recorderState_.load(std::memory_order_acquire);
    while (state != 3
           && !recorderState_.compare_exchange_weak(state, 3, std::memory_order_acq_rel))
    {
    }
    acquireRecorderCapture();
    resetRecorderCapture();
    recorderState_.store(0, std::memory_order_release);
    releaseRecorderCapture();
}

void ChordEngineAudioProcessor::acquireRecorderCapture() noexcept
{
    while (recorderCaptureAccess_.test_and_set(std::memory_order_acquire))
        std::this_thread::yield();
}

void ChordEngineAudioProcessor::releaseRecorderCapture() noexcept
{
    recorderCaptureAccess_.clear(std::memory_order_release);
}

void ChordEngineAudioProcessor::resetRecorderCapture() noexcept
{
    recorderEventCount_ = 0;
    recorderRecordedEventCount_.store(0, std::memory_order_release);
    recorderOpenNoteCount_ = 0;
    recorderStartSample_ = 0;
    recorderLastTick_ = 0;
    recorderOverflow_ = false;
    recorderOverflowed_.store(false, std::memory_order_release);
    for (auto& note : recorderOpenNotes_)
        note.active = false;
}

bool ChordEngineAudioProcessor::finalizeRecorderTake() noexcept
{
    const auto stopTick = recorderLastTick_ + 12;
    for (auto& note : recorderOpenNotes_)
    {
        if (!note.active)
            continue;
        if (recorderEventCount_ >= recorderEventBuffer_.size())
        {
            recorderOverflow_ = true;
            recorderOverflowed_.store(true, std::memory_order_release);
            break;
        }
        RecorderMidiEvent releaseEvent;
        releaseEvent.tick = juce::jmax(note.startTick + 12, stopTick);
        releaseEvent.channel = static_cast<std::uint8_t>(note.channel);
        releaseEvent.note = static_cast<std::uint8_t>(note.note);
        recorderEventBuffer_[recorderEventCount_++] = releaseEvent;
        recorderRecordedEventCount_.store(static_cast<int>(recorderEventCount_), std::memory_order_release);
        note.active = false;
    }
    recorderOpenNoteCount_ = 0;
    if (recorderOverflow_ || recorderEventCount_ == 0)
    {
        resetRecorderCapture();
        return false;
    }
    while (recorderTakeAccess_.test_and_set(std::memory_order_acquire))
        std::this_thread::yield();
    const auto previousTakeCount = recorderTakeCountInternal_.load(std::memory_order_relaxed);
    for (std::size_t takeIndex = std::min<std::size_t>(previousTakeCount, 2); takeIndex > 0; --takeIndex)
        recorderTakes_[takeIndex] = recorderTakes_[takeIndex - 1];
    for (std::size_t index = 0; index < recorderEventCount_; ++index)
        recorderTakes_[0].events[index] = recorderEventBuffer_[index];
    recorderTakes_[0].eventCount = recorderEventCount_;
    recorderTakes_[0].tempoBpm = recorderTempoBpm_;
    recorderTakes_[0].noteCount = static_cast<int>(recorderEventCount_ / 2);
    const auto takeCount = std::min<std::size_t>(3, previousTakeCount + 1);
    recorderTakeCountInternal_.store(takeCount, std::memory_order_relaxed);
    recorderTakeCount_.store(static_cast<std::uint8_t>(takeCount), std::memory_order_release);
    recorderTakeAccess_.clear(std::memory_order_release);
    resetRecorderCapture();
    return true;
}

bool ChordEngineAudioProcessor::exportRecorderTake(std::uint8_t newestFirstIndex,
                                                   juce::File& destination) const
{
    while (recorderTakeAccess_.test_and_set(std::memory_order_acquire))
        std::this_thread::yield();
    if (newestFirstIndex >= recorderTakeCountInternal_.load(std::memory_order_relaxed)
        || destination == juce::File{})
    {
        recorderTakeAccess_.clear(std::memory_order_release);
        return false;
    }
    const auto& take = recorderTakes_[newestFirstIndex];
    juce::MidiMessageSequence sequence;
    sequence.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(60000000.0 / take.tempoBpm)), 0.0);
    sequence.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
    for (std::size_t index = 0; index < take.eventCount; ++index)
    {
        const auto& event = take.events[index];
        const auto message = event.noteOn
            ? juce::MidiMessage::noteOn(event.channel, event.note, static_cast<juce::uint8>(event.velocity))
            : juce::MidiMessage::noteOff(event.channel, event.note);
        sequence.addEvent(juce::MidiMessage(message, static_cast<double>(event.tick)));
    }
    sequence.updateMatchedPairs();
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(960);
    midiFile.addTrack(sequence);
    juce::FileOutputStream output(destination);
    const bool success = output.openedOk() && midiFile.writeTo(output);
    output.flush();
    recorderTakeAccess_.clear(std::memory_order_release);
    return success && !output.getStatus().failed();
}

int ChordEngineAudioProcessor::recorderTakeNoteCount(std::uint8_t newestFirstIndex) const noexcept
{
    while (recorderTakeAccess_.test_and_set(std::memory_order_acquire))
        std::this_thread::yield();
    const int noteCount = newestFirstIndex < recorderTakeCountInternal_.load(std::memory_order_relaxed)
        ? recorderTakes_[newestFirstIndex].noteCount : 0;
    recorderTakeAccess_.clear(std::memory_order_release);
    return noteCount;
}

void ChordEngineAudioProcessor::recordGeneratedEvent(const chordengine::midi::NoteEvent& event) noexcept
{
    if (recorderCaptureAccess_.test_and_set(std::memory_order_acquire))
        return;
    auto state = recorderState_.load(std::memory_order_acquire);
    if (state != 1 && state != 2)
    {
        recorderCaptureAccess_.clear(std::memory_order_release);
        return;
    }
    if (event.kind == chordengine::midi::NoteEvent::Kind::noteOn && state == 1)
    {
        std::uint8_t expected = 1;
        if (!recorderState_.compare_exchange_strong(expected, 2, std::memory_order_acq_rel))
        {
            recorderCaptureAccess_.clear(std::memory_order_release);
            return;
        }
        recorderStartSample_ = static_cast<std::int64_t>(processedSampleCount_.load(std::memory_order_relaxed))
            + event.sampleOffset;
        if (const auto* playHead = getPlayHead())
            if (const auto position = playHead->getPosition())
                recorderTempoBpm_ = position->getBpm().orFallback(120.0);
        if (recorderTempoBpm_ <= 0.0)
            recorderTempoBpm_ = 120.0;
        recorderEventCount_ = 0;
        recorderLastTick_ = 0;
        recorderOpenNoteCount_ = 0;
        recorderOverflow_ = false;
        recorderOverflowed_.store(false, std::memory_order_release);
        recorderRecordedEventCount_.store(0, std::memory_order_release);
        for (auto& note : recorderOpenNotes_)
            note.active = false;
        recorderState_.store(2, std::memory_order_release);
        state = 2;
    }
    if (state != 2 || recorderOverflow_)
    {
        recorderCaptureAccess_.clear(std::memory_order_release);
        return;
    }
    if (recorderEventCount_ >= recorderEventBuffer_.size())
    {
        recorderOverflow_ = true;
        recorderOverflowed_.store(true, std::memory_order_release);
        recorderCaptureAccess_.clear(std::memory_order_release);
        return;
    }

    const auto absoluteSample = static_cast<std::int64_t>(processedSampleCount_.load(std::memory_order_relaxed))
        + event.sampleOffset;
    const auto sampleDelta = juce::jmax<std::int64_t>(0, absoluteSample - recorderStartSample_);
    const auto computedTicks = static_cast<std::int64_t>(static_cast<double>(sampleDelta)
        * recorderTempoBpm_ * 960.0 / (60.0 * juce::jmax(1.0, recorderSampleRate_)));
    const auto ticks = juce::jmax(recorderLastTick_, computedTicks);
    recorderLastTick_ = ticks;
    const bool noteOn = event.kind == chordengine::midi::NoteEvent::Kind::noteOn;
    RecorderMidiEvent capturedEvent;
    capturedEvent.tick = ticks;
    capturedEvent.channel = event.channel;
    capturedEvent.note = event.note;
    capturedEvent.velocity = static_cast<std::uint8_t>(noteOn ? event.velocity : 0);
    capturedEvent.noteOn = noteOn;
    recorderEventBuffer_[recorderEventCount_++] = capturedEvent;
    recorderRecordedEventCount_.store(static_cast<int>(recorderEventCount_), std::memory_order_release);

    if (noteOn)
    {
        for (auto& open : recorderOpenNotes_)
            if (open.active && open.channel == event.channel && open.note == event.note)
            {
                recorderEventBuffer_[open.startEventIndex].noteOn = false;
                open.active = false;
                if (recorderOpenNoteCount_ > 0)
                    --recorderOpenNoteCount_;
            }
        for (auto& open : recorderOpenNotes_)
            if (!open.active)
            {
                open = { true, event.channel, event.note, event.velocity, ticks, recorderEventCount_ - 1 };
                ++recorderOpenNoteCount_;
                break;
            }
    }
    else
        for (auto& open : recorderOpenNotes_)
            if (open.active && open.channel == event.channel && open.note == event.note)
            {
                open.active = false;
                if (recorderOpenNoteCount_ > 0)
                    --recorderOpenNoteCount_;
                break;
            }
    recorderCaptureAccess_.clear(std::memory_order_release);
}

chordengine::core::NoteSet ChordEngineAudioProcessor::previewChordForTrigger(int midiNote) const noexcept
{
    chordengine::core::ChordEngineCore preview;
    const auto configuration = configurationSnapshot();
    if (!preview.setConfiguration(configuration))
        return {};
    return preview.generateChordForTrigger(midiNote).value_or(chordengine::core::NoteSet{});
}

void ChordEngineAudioProcessor::syncStateFromAudioThread() noexcept
{
    const auto bits = configurationBits_.load(std::memory_order_acquire);
    if (bits != appliedConfigurationBits_ && core_.setConfiguration(decodeConfiguration(bits)))
        appliedConfigurationBits_ = bits;
}

void ChordEngineAudioProcessor::updateNoteState(const chordengine::midi::NoteEvent& event) noexcept
{
    using Kind = chordengine::midi::NoteEvent::Kind;
    if (event.channel < 1 || event.channel > 16)
        return;
    const auto channel = static_cast<std::size_t>(event.channel - 1);
    if (event.kind == Kind::noteOn && event.velocity > 0)
    {
        if (event.note < chordengine::core::ChordPresetSystem::triggerLow
            || event.note > chordengine::core::ChordPresetSystem::triggerHigh)
            return;
        if (!triggerNoteActive_[channel][event.note].exchange(true, std::memory_order_relaxed))
            activeTriggerCount_.fetch_add(1, std::memory_order_relaxed);
        triggerNoteDown_[channel][event.note].store(true, std::memory_order_relaxed);
        triggerNoteSustained_[channel][event.note].store(false, std::memory_order_relaxed);
        return;
    }
    if (event.kind == Kind::noteOff || (event.kind == Kind::noteOn && event.velocity == 0))
    {
        if (event.note > 127 || !triggerNoteActive_[channel][event.note].load(std::memory_order_relaxed))
            return;
        triggerNoteDown_[channel][event.note].store(false, std::memory_order_relaxed);
        const bool sustained = core_.voiceSustained(event.channel, event.note);
        triggerNoteSustained_[channel][event.note].store(sustained, std::memory_order_relaxed);
        if (!sustained && triggerNoteActive_[channel][event.note].exchange(false, std::memory_order_relaxed))
            activeTriggerCount_.fetch_sub(1, std::memory_order_relaxed);
        return;
    }
    if (event.kind == Kind::controller && event.controller == 64)
    {
        const bool wasDown = sustainPedalDown_[channel].load(std::memory_order_relaxed);
        const bool isDown = event.value >= 64;
        sustainPedalDown_[channel].store(isDown, std::memory_order_relaxed);
        if (wasDown && !isDown)
            clearSustainedTriggerStates();
        return;
    }
    if ((event.kind == Kind::controller && (event.controller == 120 || event.controller == 123))
        || event.kind == Kind::allNotesOff)
    {
        for (std::size_t ch = 0; ch < 16; ++ch)
        {
            for (std::size_t note = 0; note < 128; ++note)
            {
                if (triggerNoteActive_[ch][note].exchange(false, std::memory_order_relaxed))
                    activeTriggerCount_.fetch_sub(1, std::memory_order_relaxed);
                triggerNoteDown_[ch][note].store(false, std::memory_order_relaxed);
                triggerNoteSustained_[ch][note].store(false, std::memory_order_relaxed);
            }
            sustainPedalDown_[ch].store(false, std::memory_order_relaxed);
        }
    }
}

void ChordEngineAudioProcessor::clearSustainedTriggerStates() noexcept
{
    for (std::size_t channel = 0; channel < 16; ++channel)
        for (std::size_t note = 0; note < 128; ++note)
            if (triggerNoteSustained_[channel][note].exchange(false, std::memory_order_relaxed))
            {
                triggerNoteDown_[channel][note].store(false, std::memory_order_relaxed);
                if (triggerNoteActive_[channel][note].exchange(false, std::memory_order_relaxed))
                    activeTriggerCount_.fetch_sub(1, std::memory_order_relaxed);
            }
}

void ChordEngineAudioProcessor::updateOutputNoteState(const chordengine::midi::NoteEvent& event) noexcept
{
    if (event.channel < 1 || event.channel > 16 || event.note > 127)
        return;
    auto& active = outputNoteOwners_[event.channel - 1][event.note];
    if (event.kind == chordengine::midi::NoteEvent::Kind::noteOn)
    {
        auto owners = active.load(std::memory_order_relaxed);
        while (owners < std::numeric_limits<std::uint16_t>::max()
               && !active.compare_exchange_weak(owners, static_cast<std::uint16_t>(owners + 1),
                                                std::memory_order_relaxed))
        {
        }
        if (owners == 0)
            activeOutputNoteCount_.fetch_add(1, std::memory_order_relaxed);
    }
    else if (event.kind == chordengine::midi::NoteEvent::Kind::noteOff)
    {
        auto owners = active.load(std::memory_order_relaxed);
        while (owners > 0 && !active.compare_exchange_weak(owners, static_cast<std::uint16_t>(owners - 1),
                                                           std::memory_order_relaxed))
        {
        }
        if (owners == 1)
            activeOutputNoteCount_.fetch_sub(1, std::memory_order_relaxed);
    }
}

void ChordEngineAudioProcessor::clearOutputNoteStates() noexcept
{
    for (auto& channel : outputNoteOwners_)
        for (auto& active : channel)
            active.store(false, std::memory_order_relaxed);
    activeOutputNoteCount_.store(0, std::memory_order_relaxed);
}

void ChordEngineAudioProcessor::prepareToPlay(double sampleRate, int)
{
    resetRecorderForPlaybackBoundary();
    recorderSampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    processedSampleCount_.store(0, std::memory_order_relaxed);
    tracker_.reset();
    core_.reset();
    uiMidiFifo_.reset();
    clearOutputNoteStates();
    for (std::size_t channel = 0; channel < 16; ++channel)
    {
        sustainPedalDown_[channel].store(false, std::memory_order_relaxed);
        for (std::size_t note = 0; note < 128; ++note)
        {
            triggerNoteActive_[channel][note].store(false, std::memory_order_relaxed);
            triggerNoteDown_[channel][note].store(false, std::memory_order_relaxed);
            triggerNoteSustained_[channel][note].store(false, std::memory_order_relaxed);
        }
    }
    activeTriggerCount_.store(0, std::memory_order_relaxed);
    appliedConfigurationBits_ = 0;
    syncStateFromAudioThread();
}

void ChordEngineAudioProcessor::releaseResources()
{
    resetRecorderForPlaybackBoundary();
    tracker_.reset();
    core_.reset();
    clearOutputNoteStates();
    for (std::size_t channel = 0; channel < 16; ++channel)
    {
        sustainPedalDown_[channel].store(false, std::memory_order_relaxed);
        for (std::size_t note = 0; note < 128; ++note)
        {
            triggerNoteActive_[channel][note].store(false, std::memory_order_relaxed);
            triggerNoteDown_[channel][note].store(false, std::memory_order_relaxed);
            triggerNoteSustained_[channel][note].store(false, std::memory_order_relaxed);
        }
    }
    activeTriggerCount_.store(0, std::memory_order_relaxed);
    appliedConfigurationBits_ = 0;
    syncStateFromAudioThread();
}

// ------------------------------------------------- on-screen keyboard -----
// The piano keys are a real MIDI input surface. The editor only pushes raw
// note-on/note-off events; every musical decision (chord resolution, velocity
// mode, transpose, recorder capture) is made by the SAME Core pass that an
// external keyboard drives. These three functions are the only GUI-thread
// entry points on the audio path and they are lock-free.
void ChordEngineAudioProcessor::pushUiNoteOn(int channel, int note, int velocity) noexcept
{
    if (note < 0 || note > 127)
        return;

    const auto scope = uiMidiFifo_.write(1);
    const UiMidiEvent event { static_cast<std::uint8_t>(juce::jlimit(1, 16, channel)),
                              static_cast<std::uint8_t>(note),
                              static_cast<std::uint8_t>(juce::jlimit(1, 127, velocity)),
                              true };
    if (scope.blockSize1 > 0)
        uiMidiEvents_[static_cast<std::size_t>(scope.startIndex1)] = event;
    else if (scope.blockSize2 > 0)
        uiMidiEvents_[static_cast<std::size_t>(scope.startIndex2)] = event;
}

void ChordEngineAudioProcessor::pushUiNoteOff(int channel, int note) noexcept
{
    if (note < 0 || note > 127)
        return;

    const auto scope = uiMidiFifo_.write(1);
    const UiMidiEvent event { static_cast<std::uint8_t>(juce::jlimit(1, 16, channel)),
                              static_cast<std::uint8_t>(note), 0, false };
    if (scope.blockSize1 > 0)
        uiMidiEvents_[static_cast<std::size_t>(scope.startIndex1)] = event;
    else if (scope.blockSize2 > 0)
        uiMidiEvents_[static_cast<std::size_t>(scope.startIndex2)] = event;
}

void ChordEngineAudioProcessor::drainUiMidi(juce::MidiBuffer& destination) noexcept
{
    for (;;)
    {
        int index = -1;
        {
            const auto scope = uiMidiFifo_.read(1);
            if (scope.blockSize1 > 0)
                index = scope.startIndex1;
            else if (scope.blockSize2 > 0)
                index = scope.startIndex2;
        }

        if (index < 0)
            break;

        const auto& event = uiMidiEvents_[static_cast<std::size_t>(index)];
        destination.addEvent(event.noteOn
                                 ? juce::MidiMessage::noteOn(event.channel, event.note, event.velocity)
                                 : juce::MidiMessage::noteOff(event.channel, event.note),
                             0);
    }
}

bool ChordEngineAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // ChordEngine is a pure MIDI processor: it accepts MIDI, emits MIDI and
    // owns NO audio bus in either format. That is what makes the AU an `aumi`
    // MIDI FX and the VST3 a MIDI effect, and it is also why the reference's
    // INTERNAL AUDIO switch (a Sine Wave Generator driven through setBypassed)
    // cannot exist here: a MIDI effect has no audio output to route a synth
    // into. The control stays visible for reference parity but is published as
    // an explicitly unavailable status, never as a fake ON/OFF toggle.
    return layouts.inputBuses.isEmpty() && layouts.outputBuses.isEmpty();
}

void ChordEngineAudioProcessor::processBlock(juce::AudioBuffer<float>& audio,
                                             juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(noDenormals);
    syncStateFromAudioThread();
    // Merge any on-screen keyboard events into this block BEFORE the Core pass
    // so a piano key is indistinguishable from an external MIDI note.
    drainUiMidi(midi);
    juce::MidiBuffer output;
    auto& result = coreResult_;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        chordengine::midi::NoteEvent event;
        event.channel = static_cast<std::uint8_t>(message.getChannel());
        event.sampleOffset = metadata.samplePosition;
        bool recognized = true;
        if (message.isNoteOn())
        {
            event.kind = chordengine::midi::NoteEvent::Kind::noteOn;
            event.note = static_cast<std::uint8_t>(message.getNoteNumber());
            event.velocity = message.getVelocity();
        }
        else if (message.isNoteOff())
        {
            event.kind = chordengine::midi::NoteEvent::Kind::noteOff;
            event.note = static_cast<std::uint8_t>(message.getNoteNumber());
        }
        else if (message.isController())
        {
            event.kind = chordengine::midi::NoteEvent::Kind::controller;
            event.controller = static_cast<std::uint8_t>(message.getControllerNumber());
            event.value = static_cast<std::uint8_t>(message.getControllerValue());
        }
        else if (message.isPitchWheel())
        {
            event.kind = chordengine::midi::NoteEvent::Kind::pitchBend;
            event.data = static_cast<std::uint16_t>(message.getPitchWheelValue());
        }
        else if (message.isAftertouch())
        {
            event.kind = chordengine::midi::NoteEvent::Kind::aftertouch;
            event.note = static_cast<std::uint8_t>(message.getNoteNumber());
            event.value = static_cast<std::uint8_t>(message.getAfterTouchValue());
        }
        else if (message.isProgramChange())
        {
            event.kind = chordengine::midi::NoteEvent::Kind::programChange;
            event.data = static_cast<std::uint16_t>(message.getProgramChangeNumber());
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            event.kind = chordengine::midi::NoteEvent::Kind::controller;
            event.controller = message.isAllNotesOff() ? 123 : 120;
            event.value = static_cast<std::uint8_t>(message.getControllerValue());
        }
        else
            recognized = false;
        if (!recognized)
        {
            output.addEvent(message, metadata.samplePosition);
            continue;
        }
        // Expired unlicensed trial: the reference swallows the raw trigger
        // and generates no chord, no MIDI and no visual state. The gate is a
        // single lock-free read and never performs any work here.
        if (!licensingGateOpen_.load(std::memory_order_relaxed)
            && event.kind == chordengine::midi::NoteEvent::Kind::noteOn)
            continue;
        core_.process(event, result);
        updateNoteState(event);
        tracker_.observe(event);
        if (!result.suppressInput)
            output.addEvent(message, metadata.samplePosition);
        for (std::size_t index = 0; index < result.eventCount; ++index)
        {
            const auto& generated = result.events[index];
            updateOutputNoteState(generated);
            if (generated.kind == chordengine::midi::NoteEvent::Kind::noteOn
                || generated.kind == chordengine::midi::NoteEvent::Kind::noteOff)
                recordGeneratedEvent(generated);
            if (generated.kind == chordengine::midi::NoteEvent::Kind::noteOn)
            {
                playbackActivityPending_.store(true, std::memory_order_relaxed);
                output.addEvent(juce::MidiMessage::noteOn(generated.channel, generated.note,
                    static_cast<juce::uint8>(generated.velocity)), generated.sampleOffset);
            }
            else if (generated.kind == chordengine::midi::NoteEvent::Kind::noteOff)
                output.addEvent(juce::MidiMessage::noteOff(generated.channel, generated.note), generated.sampleOffset);
        }
    }
    midi.swapWith(output);
    processedSampleCount_.fetch_add(static_cast<std::uint64_t>(audio.getNumSamples()), std::memory_order_relaxed);
    audio.clear();
}

juce::AudioProcessorEditor* ChordEngineAudioProcessor::createEditor()
{
    return new ChordEngineAudioProcessorEditor(*this);
}

void ChordEngineAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    destination.reset();
}

void ChordEngineAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::ignoreUnused(data, sizeInBytes);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChordEngineAudioProcessor();
}
