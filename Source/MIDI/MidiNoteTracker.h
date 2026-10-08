#pragma once

#include <array>
#include <cstdint>

namespace chordengine::midi
{
struct NoteEvent
{
    enum class Kind : std::uint8_t { noteOn, noteOff, controller, pitchBend, aftertouch, programChange, allNotesOff, other };
    Kind kind = Kind::other;
    std::uint8_t channel = 1; // MIDI channels are one-based here.
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    std::uint8_t controller = 0;
    std::uint8_t value = 0;
    std::uint16_t data = 0;
    int sampleOffset = 0;
};

// A predictable 16 x 128 tracker. No dynamic allocation; it is not yet a
// ChordEngine implementation and currently records incoming events only.
class MidiNoteTracker
{
public:
    void observe(const NoteEvent& event) noexcept;
    void clearChannel(std::uint8_t channel) noexcept;
    void reset() noexcept;
    bool isDown(std::uint8_t channel, std::uint8_t note) const noexcept;
    bool isSustained(std::uint8_t channel, std::uint8_t note) const noexcept;
    bool sustainPedalDown(std::uint8_t channel) const noexcept;
    std::uint32_t activeCount() const noexcept { return activeCount_; }

private:
    static bool valid(std::uint8_t channel, std::uint8_t note) noexcept;
    std::array<std::array<bool, 128>, 16> down_{};
    std::array<std::array<bool, 128>, 16> sustained_{};
    std::array<bool, 16> sustainPedal_{};
    std::uint32_t activeCount_ = 0;
};
} // namespace chordengine::midi
