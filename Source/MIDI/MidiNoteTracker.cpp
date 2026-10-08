#include "MidiNoteTracker.h"

namespace chordengine::midi
{
bool MidiNoteTracker::valid(std::uint8_t channel, std::uint8_t note) noexcept
{
    return channel >= 1 && channel <= 16 && note <= 127;
}

void MidiNoteTracker::observe(const NoteEvent& event) noexcept
{
    if (event.channel < 1 || event.channel > 16)
        return;

    const auto channel = static_cast<std::size_t>(event.channel - 1);
    if (event.kind == NoteEvent::Kind::controller && event.controller == 64)
    {
        const bool wasDown = sustainPedal_[channel];
        const bool isDown = event.value >= 64;
        sustainPedal_[channel] = isDown;
        if (wasDown && !isDown)
        {
            for (std::size_t note = 0; note < 128; ++note)
            {
                if (sustained_[channel][note])
                {
                    sustained_[channel][note] = false;
                    if (activeCount_ > 0)
                        --activeCount_;
                }
            }
        }
        return;
    }

    if (event.kind == NoteEvent::Kind::allNotesOff)
    {
        clearChannel(event.channel);
        return;
    }

    if (!valid(event.channel, event.note))
        return;

    const auto note = static_cast<std::size_t>(event.note);
    if (event.kind == NoteEvent::Kind::noteOn && event.velocity > 0)
    {
        if (!down_[channel][note] && !sustained_[channel][note])
            ++activeCount_;
        down_[channel][note] = true;
        sustained_[channel][note] = false;
    }
    else if (event.kind == NoteEvent::Kind::noteOff ||
             (event.kind == NoteEvent::Kind::noteOn && event.velocity == 0))
    {
        if (down_[channel][note])
        {
            down_[channel][note] = false;
            if (sustainPedal_[channel])
                sustained_[channel][note] = true;
            else if (activeCount_ > 0)
                --activeCount_;
        }
    }
}

void MidiNoteTracker::clearChannel(std::uint8_t channel) noexcept
{
    if (channel < 1 || channel > 16)
        return;
    const auto index = static_cast<std::size_t>(channel - 1);
    for (std::size_t note = 0; note < 128; ++note)
        if (down_[index][note] || sustained_[index][note])
            --activeCount_;
    down_[index] = {};
    sustained_[index] = {};
    sustainPedal_[index] = false;
}

void MidiNoteTracker::reset() noexcept
{
    down_ = {};
    sustained_ = {};
    sustainPedal_ = {};
    activeCount_ = 0;
}

bool MidiNoteTracker::isDown(std::uint8_t channel, std::uint8_t note) const noexcept
{
    return valid(channel, note) && down_[channel - 1][note];
}

bool MidiNoteTracker::isSustained(std::uint8_t channel, std::uint8_t note) const noexcept
{
    return valid(channel, note) && sustained_[channel - 1][note];
}

bool MidiNoteTracker::sustainPedalDown(std::uint8_t channel) const noexcept
{
    return channel >= 1 && channel <= 16 && sustainPedal_[channel - 1];
}
} // namespace chordengine::midi
