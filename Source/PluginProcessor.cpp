#include "PluginProcessor.h"
#include "Licensing/MusicProdAuthService.h"
#include "MIDI/MidiFileWriter.h"
#include "Platform/Platform.h"

#include <algorithm>
#include <cmath>
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

ChordEngineAudioProcessor::ChordEngineAudioProcessor(bool withAudioOutput)
    : hasAudioOutput_(withAudioOutput)
{
    blockOutput_.reserve(midiEventCapacity);
    configurationBits_.store(encodeConfiguration(core_.configuration()), std::memory_order_relaxed);

    auto storageDir = defaultLicensingStorageDir();
   #if CHORDENGINE_ISOLATED_LICENSING
    // Test builds: every processor gets its own empty folder, so no test reads
    // the developer's account or shares a trial budget with another test.
    storageDir = storageDir / ("processor-" + chordengine::platform::randomHex(8));
    isolatedLicensingDir_ = storageDir;
   #endif
    licensing_ = std::make_unique<chordengine::licensing::LicensingRuntime>(
        std::make_unique<chordengine::licensing::MusicProdAuthService>(),
        nullptr,
        storageDir / "auth.json");
    licensing_->setGateSink([this](bool open) { setLicensingGateOpen(open); });
    licensing_->setPlaybackActivitySource([this] { return consumePlaybackActivity(); });
   #if !CHORDENGINE_ISOLATED_LICENSING
    licensing_->start();
   #endif
}

ChordEngineAudioProcessor::~ChordEngineAudioProcessor()
{
    licensing_.reset();
    if (!isolatedLicensingDir_.empty())
    {
        std::error_code error;
        std::filesystem::remove_all(isolatedLicensingDir_, error);
    }
}

std::filesystem::path ChordEngineAudioProcessor::defaultLicensingStorageDir()
{
   #if CHORDENGINE_ISOLATED_LICENSING
    return chordengine::platform::temporaryDirectory() / "ChordEngineTestLicensing";
   #else
    // The account token and the trial budget live next to the other
    // Music-Prod plugin state, in the same AppData location VYRE uses.
    return chordengine::platform::userApplicationDataDirectory() / "Music-Prod" / "ChordEngine";
   #endif
}

chordengine::state::PluginState ChordEngineAudioProcessor::stateSnapshot() const
{
    chordengine::state::PluginState state;
    state.configuration = configurationSnapshot();
    state.packPreset = packPreset();
    state.internalAudioEnabled = internalAudioEnabled();
    state.internalSound = internalSound();
    state.recordQuantize = recordQuantize();
    state.followHostRecord = followHostRecord();
    return state;
}

void ChordEngineAudioProcessor::applyState(const chordengine::state::PluginState& state)
{
    // One validated, atomic publish of the whole configuration. processBlock
    // applies it to the Core at its next block boundary; the editor reads it
    // back from here, so the GUI never becomes the source of truth.
    setConfiguration(state.configuration);
    if (state.packPreset.has_value())
        setPackPreset(*state.packPreset);
    else
        clearPackPreset();
    if (state.internalAudioEnabled.has_value())
        setInternalAudioEnabled(*state.internalAudioEnabled);
    if (state.internalSound.has_value())
        setInternalSound(*state.internalSound);
    if (state.recordQuantize.has_value())
        setRecordQuantize(*state.recordQuantize);
    if (state.followHostRecord.has_value())
        setFollowHostRecord(*state.followHostRecord);
}

bool ChordEngineAudioProcessor::setPackPreset(const PackPreset& preset)
{
    if (!preset.preset.valid())
        return false;
    while (packPresetLock_.test_and_set(std::memory_order_acquire))
        std::this_thread::yield();
    packPresetSelection_ = preset;
    packPresetLock_.clear(std::memory_order_release);
    while (customPresetAccess_.test_and_set(std::memory_order_acquire))
        std::this_thread::yield();
    pendingCustomPreset_ = preset.preset;
    pendingCustomPresetActive_ = true;
    customPresetAccess_.clear(std::memory_order_release);
    customPresetGeneration_.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

void ChordEngineAudioProcessor::clearPackPreset()
{
    while (packPresetLock_.test_and_set(std::memory_order_acquire))
        std::this_thread::yield();
    const bool had = packPresetSelection_.has_value();
    packPresetSelection_.reset();
    packPresetLock_.clear(std::memory_order_release);
    if (!had)
        return;
    while (customPresetAccess_.test_and_set(std::memory_order_acquire))
        std::this_thread::yield();
    pendingCustomPresetActive_ = false;
    customPresetAccess_.clear(std::memory_order_release);
    customPresetGeneration_.fetch_add(1, std::memory_order_acq_rel);
}

std::optional<ChordEngineAudioProcessor::PackPreset> ChordEngineAudioProcessor::packPreset() const
{
    while (packPresetLock_.test_and_set(std::memory_order_acquire))
        std::this_thread::yield();
    auto selection = packPresetSelection_;
    packPresetLock_.clear(std::memory_order_release);
    return selection;
}

void ChordEngineAudioProcessor::applyCustomPresetFromAudioThread() noexcept
{
    const auto generation = customPresetGeneration_.load(std::memory_order_acquire);
    if (generation == appliedCustomPresetGeneration_
        || customPresetAccess_.test_and_set(std::memory_order_acquire))
        return; // nothing new, or the editor is mid-write: next block
    const auto preset = pendingCustomPreset_;
    const bool active = pendingCustomPresetActive_;
    customPresetAccess_.clear(std::memory_order_release);
    core_.setCustomPreset(active ? &preset : nullptr);
    appliedCustomPresetGeneration_ = generation;
}

void ChordEngineAudioProcessor::setInternalAudioEnabled(bool enabled) noexcept
{
    internalAudioEnabled_.store(enabled, std::memory_order_release);
}

void ChordEngineAudioProcessor::setInternalSound(InternalSound sound) noexcept
{
    internalSound_.store(static_cast<std::uint8_t>(sound), std::memory_order_release);
}

void ChordEngineAudioProcessor::setRecordQuantize(RecordQuantize grid) noexcept
{
    if (static_cast<int>(grid) >= 0 && static_cast<int>(grid) < chordengine::midi::takeQuantizeCount)
        recordQuantize_.store(static_cast<std::uint8_t>(grid), std::memory_order_release);
}

void ChordEngineAudioProcessor::setFollowHostRecord(bool enabled) noexcept
{
    followHostRecord_.store(enabled, std::memory_order_release);
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

void ChordEngineAudioProcessor::followHostRecording() noexcept
{
    // With FOLLOW DAW RECORD on, the recorder follows the host's own record
    // button: when the DAW starts recording, an idle recorder arms itself (the
    // take begins at the first generated note, as with RECORD ARM), and when
    // the DAW stops the take is stored. A take the user armed by hand is left
    // to them. Recording counts only while the transport runs, because some
    // hosts (FL Studio) report their record switch while stopped.
    const bool hostRecording = transport_.isRecording && transport_.isPlaying;
    if (!followHostRecord_.load(std::memory_order_acquire))
    {
        // Switching the mode off hands a host-armed take to the user; switching
        // it on during a host recording waits for the next record start.
        hostWasRecording_ = hostRecording;
        hostArmedTake_ = false;
        return;
    }
    if (hostRecording == hostWasRecording_)
        return;
    hostWasRecording_ = hostRecording;
    if (hostRecording)
    {
        hostArmedTake_ = armRecorder();
        return;
    }
    if (!hostArmedTake_)
        return;
    hostArmedTake_ = false;
    if (!stopRecorder())
        cancelRecorder();
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
        releaseEvent.tick = std::max(note.startTick + 12, stopTick);
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
    recorderTakes_[0].anchor = recorderAnchor_;
    recorderTakes_[0].timeSignatureNumerator = recorderTimeSignatureNumerator_;
    recorderTakes_[0].timeSignatureDenominator = recorderTimeSignatureDenominator_;
    recorderTakes_[0].noteCount = static_cast<int>(recorderEventCount_ / 2);
    const auto takeCount = std::min<std::size_t>(3, previousTakeCount + 1);
    recorderTakeCountInternal_.store(takeCount, std::memory_order_relaxed);
    recorderTakeCount_.store(static_cast<std::uint8_t>(takeCount), std::memory_order_release);
    recorderTakeAccess_.clear(std::memory_order_release);
    resetRecorderCapture();
    return true;
}

std::optional<std::string> ChordEngineAudioProcessor::exportRecorderTakeBytes(std::uint8_t newestFirstIndex) const
{
    // Copy the take out under the take lock (the audio thread takes it when
    // the host stops recording), then quantize and write without holding it.
    // The stored take keeps the played timing; quantizing is export-only.
    std::vector<chordengine::midi::TakeEvent> events;
    events.reserve(recorderEventCapacity);
    double tempoBpm = 120.0;
    chordengine::midi::TakeGridAnchor anchor;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;
    while (recorderTakeAccess_.test_and_set(std::memory_order_acquire))
        std::this_thread::yield();
    if (newestFirstIndex >= recorderTakeCountInternal_.load(std::memory_order_relaxed))
    {
        recorderTakeAccess_.clear(std::memory_order_release);
        return std::nullopt;
    }
    const auto& take = recorderTakes_[newestFirstIndex];
    for (std::size_t index = 0; index < take.eventCount; ++index)
    {
        const auto& event = take.events[index];
        events.push_back({ event.tick, event.channel, event.note, event.velocity, event.noteOn });
    }
    tempoBpm = take.tempoBpm;
    anchor = take.anchor;
    timeSignatureNumerator = take.timeSignatureNumerator;
    timeSignatureDenominator = take.timeSignatureDenominator;
    recorderTakeAccess_.clear(std::memory_order_release);

    events = chordengine::midi::quantizeTake(events, recordQuantize(), anchor);
    return chordengine::midi::writeTakeMidiFile(events, tempoBpm, timeSignatureNumerator,
                                                timeSignatureDenominator);
}

bool ChordEngineAudioProcessor::exportRecorderTake(std::uint8_t newestFirstIndex,
                                                   const std::filesystem::path& destination) const
{
    if (destination.empty())
        return false;
    const auto bytes = exportRecorderTakeBytes(newestFirstIndex);
    return bytes.has_value() && chordengine::platform::writeFileAtomically(destination, *bytes);
}

void ChordEngineAudioProcessor::captureRecorderGridAnchor(const HostTransport& position,
                                                          int sampleOffset) noexcept
{
    // Audio thread, at the take's first note. Records where that note falls
    // in its bar on the host grid, so an exported take can be quantized to
    // the host's grid (TakeQuantize.h). Without a running transport the host
    // reports no meaningful position and the take stays anchored to its
    // first note.
    recorderAnchor_ = {};
    recorderTimeSignatureNumerator_ = 4;
    recorderTimeSignatureDenominator_ = 4;
    if (position.timeSignatureNumerator.has_value() && position.timeSignatureDenominator.has_value()
        && *position.timeSignatureNumerator > 0 && *position.timeSignatureDenominator > 0)
    {
        recorderTimeSignatureNumerator_ = *position.timeSignatureNumerator;
        recorderTimeSignatureDenominator_ = *position.timeSignatureDenominator;
    }
    const auto quarterNotesPerBar = 4.0 * recorderTimeSignatureNumerator_ / recorderTimeSignatureDenominator_;
    recorderAnchor_.barTicks = std::max<std::int64_t>(
        1, static_cast<std::int64_t>(std::floor(quarterNotesPerBar
                                                * static_cast<double>(chordengine::midi::takeTicksPerQuarterNote) + 0.5)));

    const auto& ppq = position.ppqPosition;
    const auto bpm = position.bpm.value_or(0.0);
    if (!position.isPlaying || !ppq.has_value() || bpm <= 0.0)
        return;
    // The block's position plus the note's offset into the block.
    const auto notePpq = *ppq + static_cast<double>(sampleOffset) * bpm
                                    / (60.0 * std::max(1.0, recorderSampleRate_));
    double offsetInBar = 0.0;
    if (const auto& barStart = position.ppqPositionOfLastBarStart; barStart.has_value() && *barStart <= notePpq)
        offsetInBar = notePpq - *barStart;
    else
        offsetInBar = notePpq - std::floor(notePpq / quarterNotesPerBar) * quarterNotesPerBar;
    // A bar-start report that lags across a bar line folds back into the bar.
    offsetInBar = std::fmod(std::max(0.0, offsetInBar), quarterNotesPerBar);
    recorderAnchor_.known = true;
    recorderAnchor_.offsetTicks = static_cast<std::int64_t>(
        std::llround(offsetInBar * static_cast<double>(chordengine::midi::takeTicksPerQuarterNote)));
    recorderAnchor_.offsetTicks = std::clamp<std::int64_t>(recorderAnchor_.offsetTicks, 0,
                                                           recorderAnchor_.barTicks - 1);
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
        recorderAnchor_ = {};
        recorderTempoBpm_ = transport_.bpm.value_or(120.0);
        captureRecorderGridAnchor(transport_, event.sampleOffset);
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
    const auto sampleDelta = std::max<std::int64_t>(0, absoluteSample - recorderStartSample_);
    const auto computedTicks = static_cast<std::int64_t>(static_cast<double>(sampleDelta)
        * recorderTempoBpm_ * 960.0 / (60.0 * std::max(1.0, recorderSampleRate_)));
    const auto ticks = std::max(recorderLastTick_, computedTicks);
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
                // A retrigger ends the sounding note here: insert its note-off
                // ahead of the new note-on (which keeps its velocity). Earlier
                // builds rewrote the old note-on instead, erasing that note.
                if (recorderEventCount_ < recorderEventBuffer_.size())
                {
                    recorderEventBuffer_[recorderEventCount_] = recorderEventBuffer_[recorderEventCount_ - 1];
                    RecorderMidiEvent retriggerOff;
                    retriggerOff.tick = ticks;
                    retriggerOff.channel = event.channel;
                    retriggerOff.note = event.note;
                    recorderEventBuffer_[recorderEventCount_ - 1] = retriggerOff;
                    ++recorderEventCount_;
                    recorderRecordedEventCount_.store(static_cast<int>(recorderEventCount_),
                                                      std::memory_order_release);
                }
                else
                {
                    recorderOverflow_ = true;
                    recorderOverflowed_.store(true, std::memory_order_release);
                }
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
    while (customPresetAccess_.test_and_set(std::memory_order_acquire))
        std::this_thread::yield();
    const auto custom = pendingCustomPreset_;
    const bool customActive = pendingCustomPresetActive_;
    customPresetAccess_.clear(std::memory_order_release);
    if (customActive)
        preview.setCustomPreset(&custom);
    return preview.generateChordForTrigger(midiNote).value_or(chordengine::core::NoteSet{});
}

void ChordEngineAudioProcessor::syncStateFromAudioThread() noexcept
{
    const auto bits = configurationBits_.load(std::memory_order_acquire);
    if (bits != appliedConfigurationBits_ && core_.setConfiguration(decodeConfiguration(bits)))
        appliedConfigurationBits_ = bits;
    applyCustomPresetFromAudioThread();
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

void ChordEngineAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    internalSynth_.prepare(sampleRate > 0.0 ? sampleRate : 44100.0, std::max(1, samplesPerBlock));
    blockOutput_.reserve(midiEventCapacity);
    internalAudioWasEnabled_ = false;
    resetRecorderForPlaybackBoundary();
    // A new playback session: re-verify the account in the background, so a
    // membership that ended falls back to the trial without the editor.
    licensing_->requestRecheck();
    hostWasRecording_ = false;
    hostArmedTake_ = false;
    recorderSampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    processedSampleCount_.store(0, std::memory_order_relaxed);
    tracker_.reset();
    core_.reset();
    uiMidiRead_.store(uiMidiWrite_.load(std::memory_order_acquire), std::memory_order_release);
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
void ChordEngineAudioProcessor::pushUiEvent(const UiMidiEvent& event) noexcept
{
    const auto write = uiMidiWrite_.load(std::memory_order_relaxed);
    const auto next = (write + 1) % uiMidiQueueCapacity;
    if (next == uiMidiRead_.load(std::memory_order_acquire))
        return; // full: the key event is dropped, as the 0.5 FIFO did
    uiMidiEvents_[static_cast<std::size_t>(write)] = event;
    uiMidiWrite_.store(next, std::memory_order_release);
}

void ChordEngineAudioProcessor::pushUiNoteOn(int channel, int note, int velocity) noexcept
{
    if (note < 0 || note > 127)
        return;

    pushUiEvent({ static_cast<std::uint8_t>(std::clamp(channel, 1, 16)),
                  static_cast<std::uint8_t>(note),
                  static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)),
                  true });
}

void ChordEngineAudioProcessor::pushUiNoteOff(int channel, int note) noexcept
{
    if (note < 0 || note > 127)
        return;

    pushUiEvent({ static_cast<std::uint8_t>(std::clamp(channel, 1, 16)),
                  static_cast<std::uint8_t>(note), 0, false });
}

void ChordEngineAudioProcessor::drainUiMidi(chordengine::midi::MidiEventList& destination) noexcept
{
    auto read = uiMidiRead_.load(std::memory_order_relaxed);
    const auto write = uiMidiWrite_.load(std::memory_order_acquire);
    while (read != write)
    {
        const auto& event = uiMidiEvents_[static_cast<std::size_t>(read)];
        destination.add(event.noteOn
                            ? chordengine::midi::MidiEvent::noteOn(0, event.channel, event.note, event.velocity)
                            : chordengine::midi::MidiEvent::noteOff(0, event.channel, event.note));
        read = (read + 1) % uiMidiQueueCapacity;
    }
    uiMidiRead_.store(read, std::memory_order_release);
}

void ChordEngineAudioProcessor::processBlock(const chordengine::audio::AudioBlock& audio,
                                             chordengine::midi::MidiEventList& midi,
                                             const HostTransport& transport)
{
    using chordengine::midi::MidiEvent;
    const chordengine::audio::ScopedNoDenormals noDenormals;
    transport_ = transport;
    syncStateFromAudioThread();
    followHostRecording();
    // Merge any on-screen keyboard events into this block BEFORE the Core pass
    // so a piano key is indistinguishable from an external MIDI note.
    drainUiMidi(midi);
    auto& output = blockOutput_;
    output.clear();
    auto& result = coreResult_;
    for (const auto& message : midi)
    {
        chordengine::midi::NoteEvent event;
        event.channel = static_cast<std::uint8_t>(message.channel());
        event.sampleOffset = message.sampleOffset;
        const auto type = message.status() & 0xF0;
        bool recognized = true;
        if (message.isNoteOn())
        {
            event.kind = chordengine::midi::NoteEvent::Kind::noteOn;
            event.note = static_cast<std::uint8_t>(message.noteNumber());
            event.velocity = static_cast<std::uint8_t>(message.velocity());
        }
        else if (message.isNoteOff())
        {
            event.kind = chordengine::midi::NoteEvent::Kind::noteOff;
            event.note = static_cast<std::uint8_t>(message.noteNumber());
        }
        else if (message.isController())
        {
            event.kind = chordengine::midi::NoteEvent::Kind::controller;
            event.controller = static_cast<std::uint8_t>(message.controllerNumber());
            event.value = static_cast<std::uint8_t>(message.controllerValue());
        }
        else if (type == 0xE0 && message.size == 3)
        {
            event.kind = chordengine::midi::NoteEvent::Kind::pitchBend;
            event.data = static_cast<std::uint16_t>(message.bytes[1] | (message.bytes[2] << 7));
        }
        else if (type == 0xA0 && message.size == 3)
        {
            event.kind = chordengine::midi::NoteEvent::Kind::aftertouch;
            event.note = static_cast<std::uint8_t>(message.noteNumber());
            event.value = static_cast<std::uint8_t>(message.bytes[2]);
        }
        else if (type == 0xC0 && message.size >= 2)
        {
            event.kind = chordengine::midi::NoteEvent::Kind::programChange;
            event.data = static_cast<std::uint16_t>(message.bytes[1]);
        }
        else
            recognized = false;
        if (!recognized)
        {
            output.add(message);
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
            output.add(message);
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
                output.add(MidiEvent::noteOn(generated.sampleOffset, generated.channel, generated.note,
                                             generated.velocity));
            }
            else if (generated.kind == chordengine::midi::NoteEvent::Kind::noteOff)
                output.add(MidiEvent::noteOff(generated.sampleOffset, generated.channel, generated.note));
        }
    }
    midi.swap(output);
    processedSampleCount_.fetch_add(static_cast<std::uint64_t>(audio.numSamples), std::memory_order_relaxed);
    audio.clear();
    renderInternalAudio(audio, midi);
}

void ChordEngineAudioProcessor::renderInternalAudio(const chordengine::audio::AudioBlock& audio,
                                                    const chordengine::midi::MidiEventList& generated) noexcept
{
    if (!hasAudioOutput_ || audio.numChannels == 0)
        return;
    const bool enabled = internalAudioEnabled_.load(std::memory_order_acquire);
    if (enabled != internalAudioWasEnabled_)
    {
        // Switching off lets sounding notes release instead of cutting them.
        if (!enabled)
            internalSynth_.allNotesOff();
        internalAudioWasEnabled_ = enabled;
    }
    internalSynth_.setSound(static_cast<chordengine::audio::InternalSynth::Sound>(
        internalSound_.load(std::memory_order_acquire)));
    if (enabled)
        internalSynth_.process(audio, generated);
    else if (!internalSynth_.isSilent())
        internalSynth_.render(audio, 0, audio.numSamples);
}

std::string ChordEngineAudioProcessor::getStateInformation() const
{
    // Only the persistent musical configuration is written; see PluginState.h.
    return chordengine::state::serialize(stateSnapshot());
}

void ChordEngineAudioProcessor::setStateInformation(const void* data, std::size_t sizeInBytes)
{
    if (data == nullptr || sizeInBytes == 0)
        return;

    // A malformed or incompatible payload yields no state at all, so the
    // current valid configuration stays in force.
    const auto restored = chordengine::state::deserialize(data, sizeInBytes);
    if (!restored.has_value())
        return;

    applyState(*restored);
}
