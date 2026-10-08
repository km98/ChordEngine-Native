#include "ChordEngineCore.h"

#include <algorithm>
#include <limits>

namespace chordengine::core
{
namespace
{
std::size_t channelIndex(std::uint8_t channel) noexcept
{
    return static_cast<std::size_t>(channel - 1);
}

std::size_t triggerIndex(std::uint8_t note) noexcept
{
    return static_cast<std::size_t>(note);
}

void normalizeWholeChord(std::array<std::int64_t, 6>& pitches, std::size_t count) noexcept
{
    for (std::size_t attempt = 0; attempt < 12; ++attempt)
    {
        bool shifted = false;
        for (std::size_t index = 0; index < count; ++index)
        {
            if (pitches[index] < 0 || pitches[index] > 127)
            {
                const auto shift = pitches[index] > 127 ? -12 : 12;
                for (std::size_t note = 0; note < count; ++note)
                    pitches[note] += shift;
                shifted = true;
                break;
            }
        }
        if (!shifted)
            break;
    }
}
}

bool ChordEngineCore::validInput(const chordengine::midi::NoteEvent& input) noexcept
{
    return input.channel >= 1 && input.channel <= 16
        && input.note <= 127 && input.velocity <= 127
        && input.value <= 127 && input.sampleOffset >= 0;
}

bool ChordEngineCore::append(CoreResult& result,
                             const chordengine::midi::NoteEvent& event) noexcept
{
    if (result.eventCount >= result.events.size())
    {
        result.unresolved = true;
        return false;
    }
    result.events[result.eventCount++] = event;
    return true;
}

bool ChordEngineCore::setConfiguration(const CoreConfiguration& configuration) noexcept
{
    if (ChordPresetSystem::get(configuration.preset) == nullptr
        || ScaleSystem::find(configuration.scale) == nullptr
        || !ScaleSystem::keyOffset(configuration.keyIndex)
        || configuration.fixedVelocity < 1 || configuration.fixedVelocity > 127
        || configuration.transpose.octaveSteps < TransposeEngine::minimumOctaveSteps
        || configuration.transpose.octaveSteps > TransposeEngine::maximumOctaveSteps)
        return false;
    configuration_ = configuration;
    return true;
}

bool ChordEngineCore::buildNotes(int triggerNote, NoteSet& notes) const noexcept
{
    const auto resolved = ChordPresetSystem::resolve(configuration_.preset, triggerNote,
        configuration_.scale, configuration_.keyIndex);
    const auto* preset = ChordPresetSystem::get(configuration_.preset);
    if (!resolved || preset == nullptr)
        return false;

    std::array<std::int64_t, 6> pitches{};
    const auto count = static_cast<std::size_t>(resolved->intervalCount);
    for (std::size_t index = 0; index < count; ++index)
        pitches[index] = static_cast<std::int64_t>(resolved->rootMidiNote)
            + resolved->intervals[index];

    if (preset->voicingStyle == "open")
    {
        for (std::size_t index = 1; index < count; index += 2)
            pitches[index] += 12;
    }
    else if (preset->voicingStyle == "spread")
    {
        constexpr std::array<std::size_t, 4> indices {{ 1, 2, 3, 4 }};
        constexpr std::array<int, 4> shifts {{ 12, 24, 12, 24 }};
        for (std::size_t index = 0; index < indices.size(); ++index)
            if (count > indices[index])
                pitches[indices[index]] += shifts[index];
    }

    std::sort(pitches.begin(), pitches.begin() + static_cast<std::ptrdiff_t>(count));
    if (configuration_.transpose.target == TransposeTarget::whole)
    {
        if (!TransposeEngine::applyToNotes(configuration_.transpose, pitches, count))
            return false;
        normalizeWholeChord(pitches, count);
    }
    else
    {
        normalizeWholeChord(pitches, count);
        if (!TransposeEngine::applyToNotes(configuration_.transpose, pitches, count))
            return false;
    }

    notes = {};
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto clamped = std::clamp<std::int64_t>(pitches[index], 0, 127);
        const auto pitch = static_cast<std::uint8_t>(clamped);
        if (std::find(notes.notes.begin(), notes.notes.begin()
                          + static_cast<std::ptrdiff_t>(notes.size), pitch)
            == notes.notes.begin() + static_cast<std::ptrdiff_t>(notes.size))
            notes.notes[notes.size++] = pitch;
    }
    return notes.size > 0;
}

std::optional<NoteSet> ChordEngineCore::generateChordForTrigger(int triggerNote) const noexcept
{
    NoteSet notes;
    if (!buildNotes(triggerNote, notes))
        return std::nullopt;
    return notes;
}

CoreResult ChordEngineCore::process(const chordengine::midi::NoteEvent& input) noexcept
{
    CoreResult result;
    process(input, result);
    return result;
}

void ChordEngineCore::addPitchOwner(std::size_t channel, std::uint8_t pitch) noexcept
{
    auto& owners = pitchOwners_[channel][pitch];
    if (owners < std::numeric_limits<std::uint16_t>::max())
        ++owners;
}

void ChordEngineCore::removePitchOwner(std::size_t channel, std::uint8_t pitch,
                                       int sampleOffset, CoreResult& result) noexcept
{
    auto& owners = pitchOwners_[channel][pitch];
    if (owners == 0)
        return;
    --owners;
    if (owners != 0)
        return;

    chordengine::midi::NoteEvent event;
    event.kind = chordengine::midi::NoteEvent::Kind::noteOff;
    event.channel = static_cast<std::uint8_t>(channel + 1);
    event.note = pitch;
    event.sampleOffset = sampleOffset;
    append(result, event);
}

void ChordEngineCore::releaseVoice(std::size_t channel, std::size_t trigger,
                                   int sampleOffset, CoreResult& result) noexcept
{
    auto& voice = voices_[channel][trigger];
    if (!voice.active)
        return;
    if (sustainHeld_)
    {
        voice.sustained = true;
        return;
    }

    for (std::size_t index = 0; index < voice.pitchCount; ++index)
        removePitchOwner(channel, voice.pitches[index], sampleOffset, result);
    voice = {};
}

void ChordEngineCore::releaseSustained(int sampleOffset, CoreResult& result) noexcept
{
    for (std::size_t channel = 0; channel < voices_.size(); ++channel)
    {
        for (std::size_t trigger = 0; trigger < voices_[channel].size(); ++trigger)
        {
            auto& voice = voices_[channel][trigger];
            if (!voice.active || !voice.sustained)
                continue;
            for (std::size_t index = 0; index < voice.pitchCount; ++index)
                removePitchOwner(channel, voice.pitches[index], sampleOffset, result);
            voice = {};
        }
    }
}

void ChordEngineCore::panic(int sampleOffset, CoreResult& result) noexcept
{
    for (std::size_t channel = 0; channel < pitchOwners_.size(); ++channel)
    {
        for (std::size_t pitch = 0; pitch < pitchOwners_[channel].size(); ++pitch)
        {
            if (pitchOwners_[channel][pitch] == 0)
                continue;
            chordengine::midi::NoteEvent event;
            event.kind = chordengine::midi::NoteEvent::Kind::noteOff;
            event.channel = static_cast<std::uint8_t>(channel + 1);
            event.note = static_cast<std::uint8_t>(pitch);
            event.sampleOffset = sampleOffset;
            append(result, event);
        }
    }
    voices_ = {};
    pitchOwners_ = {};
    sustainHeld_ = false;
}

void ChordEngineCore::process(const chordengine::midi::NoteEvent& input,
                              CoreResult& result) noexcept
{
    result = {};
    if (!validInput(input))
        return;
    result.supported = true;

    if (input.kind == chordengine::midi::NoteEvent::Kind::noteOn)
    {
        result.suppressInput = true;
        if (input.note < ChordPresetSystem::triggerLow
            || input.note > ChordPresetSystem::triggerHigh)
            return;

        const auto channel = channelIndex(input.channel);
        const auto trigger = triggerIndex(input.note);
        auto& existing = voices_[channel][trigger];
        if (existing.active && sustainHeld_)
        {
            // The embedded registration/release interaction under sustain can
            // orphan ownership. Do not guess a replacement policy.
            result.unresolved = true;
            return;
        }
        if (existing.active)
            releaseVoice(channel, trigger, input.sampleOffset, result);

        NoteSet notes;
        if (!buildNotes(input.note, notes))
            return;
        const auto velocity = configuration_.velocityMode == VelocityMode::maximum
            ? 127 : configuration_.velocityMode == VelocityMode::fixed
                ? configuration_.fixedVelocity : std::clamp<int>(input.velocity, 1, 127);

        auto& voice = voices_[channel][trigger];
        voice.active = true;
        voice.pitchCount = static_cast<std::uint8_t>(notes.size);
        for (std::size_t index = 0; index < notes.size; ++index)
        {
            voice.pitches[index] = notes.notes[index];
            addPitchOwner(channel, notes.notes[index]);
            chordengine::midi::NoteEvent output;
            output.kind = chordengine::midi::NoteEvent::Kind::noteOn;
            output.channel = input.channel;
            output.note = notes.notes[index];
            output.velocity = static_cast<std::uint8_t>(velocity);
            output.sampleOffset = input.sampleOffset;
            append(result, output);
        }
        return;
    }

    if (input.kind == chordengine::midi::NoteEvent::Kind::noteOff)
    {
        result.suppressInput = true;
        if (input.note >= ChordPresetSystem::triggerLow
            && input.note <= ChordPresetSystem::triggerHigh)
            releaseVoice(channelIndex(input.channel), triggerIndex(input.note),
                         input.sampleOffset, result);
        return;
    }

    if (input.kind == chordengine::midi::NoteEvent::Kind::allNotesOff)
    {
        panic(input.sampleOffset, result);
        return;
    }

    if (input.kind == chordengine::midi::NoteEvent::Kind::controller)
    {
        if (input.controller == 64)
        {
            const bool wasHeld = sustainHeld_;
            sustainHeld_ = input.value >= 64;
            if (wasHeld && !sustainHeld_)
                releaseSustained(input.sampleOffset, result);
        }
        else if (input.controller == 120 || input.controller == 123)
        {
            panic(input.sampleOffset, result);
        }
    }
}

std::uint32_t ChordEngineCore::activeVoiceCount() const noexcept
{
    std::uint32_t count = 0;
    for (const auto& channel : voices_)
        for (const auto& voice : channel)
            if (voice.active)
                ++count;
    return count;
}

bool ChordEngineCore::voiceSustained(std::uint8_t channel,
                                     std::uint8_t triggerNote) const noexcept
{
    return channel >= 1 && channel <= 16 && triggerNote <= 127
        && voices_[channel - 1][triggerNote].active
        && voices_[channel - 1][triggerNote].sustained;
}

void ChordEngineCore::reset() noexcept
{
    configuration_ = {};
    voices_ = {};
    pitchOwners_ = {};
    sustainHeld_ = false;
}
} // namespace chordengine::core
