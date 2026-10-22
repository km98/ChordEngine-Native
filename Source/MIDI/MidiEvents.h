#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chordengine::midi
{
// One short MIDI message at a sample offset inside a block. Channel voice and
// channel mode messages only (at most three bytes): ChordEngine reads notes,
// sustain, all-notes-off / all-sound-off and passes other short messages
// through.
struct MidiEvent
{
    int sampleOffset = 0;
    std::uint8_t bytes[3] {};
    std::uint8_t size = 0;

    std::uint8_t status() const noexcept { return bytes[0]; }
    // 1..16, as the old message type reported it.
    int channel() const noexcept { return (bytes[0] & 0x0F) + 1; }
    int noteNumber() const noexcept { return bytes[1]; }
    int velocity() const noexcept { return bytes[2]; }
    float floatVelocity() const noexcept { return static_cast<float>(bytes[2]) * (1.0f / 127.0f); }

    bool isNoteOn() const noexcept { return size == 3 && (bytes[0] & 0xF0) == 0x90 && bytes[2] != 0; }
    // A note-on with velocity 0 is a note-off.
    bool isNoteOff() const noexcept
    {
        return size == 3 && ((bytes[0] & 0xF0) == 0x80 || ((bytes[0] & 0xF0) == 0x90 && bytes[2] == 0));
    }
    bool isController() const noexcept { return size == 3 && (bytes[0] & 0xF0) == 0xB0; }
    int controllerNumber() const noexcept { return bytes[1]; }
    int controllerValue() const noexcept { return bytes[2]; }
    bool isAllNotesOff() const noexcept { return isController() && bytes[1] == 123; }
    bool isAllSoundOff() const noexcept { return isController() && bytes[1] == 120; }
    bool isSustainPedal() const noexcept { return isController() && bytes[1] == 64; }

    static MidiEvent make(int offset, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2, std::uint8_t length = 3) noexcept
    {
        MidiEvent event;
        event.sampleOffset = offset;
        event.bytes[0] = b0;
        event.bytes[1] = b1;
        event.bytes[2] = b2;
        event.size = length;
        return event;
    }
    // channel is 1..16.
    static MidiEvent noteOn(int offset, int channel, int note, int velocity) noexcept
    {
        return make(offset, static_cast<std::uint8_t>(0x90 | ((channel - 1) & 0x0F)),
                    static_cast<std::uint8_t>(note & 0x7F), static_cast<std::uint8_t>(velocity & 0x7F));
    }
    static MidiEvent noteOff(int offset, int channel, int note, int velocity = 0) noexcept
    {
        return make(offset, static_cast<std::uint8_t>(0x80 | ((channel - 1) & 0x0F)),
                    static_cast<std::uint8_t>(note & 0x7F), static_cast<std::uint8_t>(velocity & 0x7F));
    }
    static MidiEvent controller(int offset, int channel, int number, int value) noexcept
    {
        return make(offset, static_cast<std::uint8_t>(0xB0 | ((channel - 1) & 0x0F)),
                    static_cast<std::uint8_t>(number & 0x7F), static_cast<std::uint8_t>(value & 0x7F));
    }

    bool sameBytes(const MidiEvent& other) const noexcept
    {
        return size == other.size && std::equal(bytes, bytes + size, other.bytes);
    }
};

// A fixed-capacity, sample-ordered event list. reserve() allocates (call it
// outside the audio thread); add() never allocates and drops events beyond
// the capacity. Events with equal offsets keep their insertion order.
class MidiEventList
{
public:
    explicit MidiEventList(std::size_t capacity = 0) { reserve(capacity); }

    void reserve(std::size_t capacity) { events_.reserve(capacity); }
    std::size_t capacity() const noexcept { return events_.capacity(); }
    void clear() noexcept { events_.clear(); }
    bool empty() const noexcept { return events_.empty(); }
    std::size_t size() const noexcept { return events_.size(); }
    bool overflowed() const noexcept { return overflowed_; }
    void resetOverflow() noexcept { overflowed_ = false; }

    // Inserts keeping offsets ascending (stable for equal offsets).
    bool add(const MidiEvent& event) noexcept
    {
        if (events_.size() >= events_.capacity())
        {
            overflowed_ = true;
            return false;
        }
        auto position = events_.end();
        while (position != events_.begin() && (position - 1)->sampleOffset > event.sampleOffset)
            --position;
        events_.insert(position, event); // within capacity: no allocation
        return true;
    }

    const MidiEvent* begin() const noexcept { return events_.data(); }
    const MidiEvent* end() const noexcept { return events_.data() + events_.size(); }
    const MidiEvent& operator[](std::size_t index) const noexcept { return events_[index]; }

    void swap(MidiEventList& other) noexcept
    {
        events_.swap(other.events_);
        std::swap(overflowed_, other.overflowed_);
    }

private:
    std::vector<MidiEvent> events_;
    bool overflowed_ = false;
};
} // namespace chordengine::midi
