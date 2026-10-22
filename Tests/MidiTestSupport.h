#pragma once

// Test-local MIDI helpers: a message type, a sample-ordered buffer that
// converts to and from the processor's MidiEventList, a host "play head"
// stand-in, and a Standard MIDI File reader for exported takes.

#include "AudioTestSupport.h"
#include "PluginProcessor.h"
#include "TestSupport.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace chordengine::test
{
struct MidiMessage
{
    std::vector<std::uint8_t> bytes;
    double timeStamp = 0.0;

    static MidiMessage make(std::initializer_list<int> data)
    {
        MidiMessage message;
        for (const auto value : data)
            message.bytes.push_back(static_cast<std::uint8_t>(value));
        return message;
    }
    // channel is 1..16.
    static MidiMessage noteOn(int channel, int note, std::uint8_t velocity)
    {
        return make({ 0x90 | ((channel - 1) & 15), note & 127, velocity & 127 });
    }
    static MidiMessage noteOff(int channel, int note) { return make({ 0x80 | ((channel - 1) & 15), note & 127, 0 }); }
    static MidiMessage controllerEvent(int channel, int number, int value)
    {
        return make({ 0xB0 | ((channel - 1) & 15), number & 127, value & 127 });
    }
    static MidiMessage allNotesOff(int channel) { return controllerEvent(channel, 123, 0); }
    static MidiMessage allSoundOff(int channel) { return controllerEvent(channel, 120, 0); }
    static MidiMessage pitchWheel(int channel, int value)
    {
        return make({ 0xE0 | ((channel - 1) & 15), value & 127, (value >> 7) & 127 });
    }
    static MidiMessage aftertouchChange(int channel, int note, int value)
    {
        return make({ 0xA0 | ((channel - 1) & 15), note & 127, value & 127 });
    }
    static MidiMessage programChange(int channel, int program) { return make({ 0xC0 | ((channel - 1) & 15), program & 127 }); }
    static MidiMessage songPositionPointer(int beats) { return make({ 0xF2, beats & 127, (beats >> 7) & 127 }); }

    int status() const { return bytes.empty() ? 0 : bytes[0]; }
    int getChannel() const { return (status() & 0xF0) == 0xF0 ? 0 : (status() & 15) + 1; }
    int getNoteNumber() const { return bytes.size() > 1 ? bytes[1] : 0; }
    std::uint8_t getVelocity() const { return bytes.size() > 2 ? bytes[2] : 0; }
    bool isNoteOn() const { return bytes.size() == 3 && (status() & 0xF0) == 0x90 && bytes[2] != 0; }
    bool isNoteOff() const
    {
        return bytes.size() == 3 && ((status() & 0xF0) == 0x80 || ((status() & 0xF0) == 0x90 && bytes[2] == 0));
    }
    bool isController() const { return bytes.size() == 3 && (status() & 0xF0) == 0xB0; }
    int getControllerNumber() const { return bytes.size() > 1 ? bytes[1] : 0; }
    int getControllerValue() const { return bytes.size() > 2 ? bytes[2] : 0; }
    bool isAllNotesOff() const { return isController() && bytes[1] == 123; }
    bool isAllSoundOff() const { return isController() && bytes[1] == 120; }
    bool isTempoMetaEvent() const { return bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0x51; }
    bool isTimeSignatureMetaEvent() const { return bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0x58; }
    const std::uint8_t* getRawData() const { return bytes.data(); }
    int getRawDataSize() const { return static_cast<int>(bytes.size()); }
    double getTimeStamp() const { return timeStamp; }

    midi::MidiEvent toEvent(int offset) const
    {
        midi::MidiEvent event;
        event.sampleOffset = offset;
        event.size = static_cast<std::uint8_t>(std::min<std::size_t>(bytes.size(), 3));
        for (std::size_t index = 0; index < event.size; ++index)
            event.bytes[index] = bytes[index];
        return event;
    }
    static MidiMessage fromEvent(const midi::MidiEvent& event)
    {
        MidiMessage message;
        message.bytes.assign(event.bytes, event.bytes + event.size);
        return message;
    }
};

// Events in sample order; equal positions keep insertion order.
class MidiBuffer
{
public:
    struct Metadata
    {
        MidiMessage message;
        int samplePosition = 0;
        const MidiMessage& getMessage() const { return message; }
    };

    void addEvent(const MidiMessage& message, int position)
    {
        auto it = events_.end();
        while (it != events_.begin() && (it - 1)->samplePosition > position)
            --it;
        events_.insert(it, { message, position });
    }
    void clear() { events_.clear(); }
    int getNumEvents() const { return static_cast<int>(events_.size()); }
    bool isEmpty() const { return events_.empty(); }
    std::vector<Metadata>::const_iterator begin() const { return events_.begin(); }
    std::vector<Metadata>::const_iterator end() const { return events_.end(); }

    midi::MidiEventList toList() const
    {
        midi::MidiEventList list(ChordEngineAudioProcessor::midiEventCapacity);
        for (const auto& event : events_)
            list.add(event.message.toEvent(event.samplePosition));
        return list;
    }
    void assign(const midi::MidiEventList& list)
    {
        events_.clear();
        for (const auto& event : list)
            events_.push_back({ MidiMessage::fromEvent(event), event.sampleOffset });
    }

private:
    std::vector<Metadata> events_;
};

// A host transport the test sets per block (what a DAW reports).
using PlayHead = std::function<HostTransport()>;

inline void processBlock(ChordEngineAudioProcessor& processor, AudioBuffer& audio, MidiBuffer& midi,
                         const PlayHead* playHead = nullptr)
{
    auto list = midi.toList();
    processor.processBlock(audio.block(), list, playHead != nullptr && *playHead ? (*playHead)() : HostTransport {});
    midi.assign(list);
}

// ---- Standard MIDI File reader (formats 0/1, running status, meta, sysex) ----
struct SmfTrack
{
    struct Event
    {
        MidiMessage message;
    };
    std::vector<Event> events;
    int getNumEvents() const { return static_cast<int>(events.size()); }
    const Event* getEventPointer(int index) const { return &events[static_cast<std::size_t>(index)]; }
};

struct SmfFile
{
    int format = -1;
    int ticksPerQuarterNote = 0;
    std::vector<SmfTrack> tracks;
    int getNumTracks() const { return static_cast<int>(tracks.size()); }
    const SmfTrack* getTrack(int index) const
    {
        return index >= 0 && index < getNumTracks() ? &tracks[static_cast<std::size_t>(index)] : nullptr;
    }

    // Parses `bytes`; returns false on any structural error.
    bool parse(const std::string& bytes)
    {
        std::size_t position = 0;
        auto u32 = [&](std::uint32_t& out) {
            if (position + 4 > bytes.size()) return false;
            out = (std::uint32_t(std::uint8_t(bytes[position])) << 24) | (std::uint32_t(std::uint8_t(bytes[position + 1])) << 16)
                | (std::uint32_t(std::uint8_t(bytes[position + 2])) << 8) | std::uint32_t(std::uint8_t(bytes[position + 3]));
            position += 4;
            return true;
        };
        auto u16 = [&](int& out) {
            if (position + 2 > bytes.size()) return false;
            out = (std::uint8_t(bytes[position]) << 8) | std::uint8_t(bytes[position + 1]);
            position += 2;
            return true;
        };
        if (bytes.compare(0, 4, "MThd") != 0)
            return false;
        position = 4;
        std::uint32_t headerLength = 0;
        int trackCount = 0;
        if (!u32(headerLength) || headerLength < 6 || !u16(format) || !u16(trackCount) || !u16(ticksPerQuarterNote))
            return false;
        position = 8 + headerLength;
        for (int track = 0; track < trackCount; ++track)
        {
            if (bytes.compare(position, 4, "MTrk") != 0)
                return false;
            position += 4;
            std::uint32_t length = 0;
            if (!u32(length) || position + length > bytes.size())
                return false;
            const auto end = position + length;
            SmfTrack parsed;
            double tick = 0.0;
            int running = 0;
            while (position < end)
            {
                std::uint32_t delta = 0;
                for (int k = 0; k < 4; ++k)
                {
                    const auto byte = std::uint8_t(bytes[position++]);
                    delta = (delta << 7) | (byte & 0x7F);
                    if ((byte & 0x80) == 0)
                        break;
                }
                tick += delta;
                int status = std::uint8_t(bytes[position]);
                MidiMessage message;
                if (status == 0xFF)
                {
                    const int type = std::uint8_t(bytes[position + 1]);
                    position += 2;
                    std::uint32_t metaLength = 0;
                    for (int k = 0; k < 4; ++k)
                    {
                        const auto byte = std::uint8_t(bytes[position++]);
                        metaLength = (metaLength << 7) | (byte & 0x7F);
                        if ((byte & 0x80) == 0)
                            break;
                    }
                    message.bytes = { 0xFF, std::uint8_t(type) };
                    message.bytes.insert(message.bytes.end(), bytes.begin() + static_cast<std::ptrdiff_t>(position),
                                         bytes.begin() + static_cast<std::ptrdiff_t>(position + metaLength));
                    position += metaLength;
                }
                else
                {
                    if ((status & 0x80) != 0)
                    {
                        running = status;
                        ++position;
                    }
                    else
                        status = running;
                    const int type = status & 0xF0;
                    const int dataBytes = (type == 0xC0 || type == 0xD0) ? 1 : 2;
                    message.bytes.push_back(std::uint8_t(status));
                    for (int k = 0; k < dataBytes; ++k)
                        message.bytes.push_back(std::uint8_t(bytes[position++]));
                }
                message.timeStamp = tick;
                parsed.events.push_back({ message });
            }
            tracks.push_back(std::move(parsed));
            position = end;
        }
        return true;
    }

    bool readFrom(const std::filesystem::path& file)
    {
        const auto bytes = platform::readFile(file);
        return bytes.has_value() && parse(*bytes);
    }
};
} // namespace chordengine::test
