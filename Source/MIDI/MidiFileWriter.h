#pragma once

#include "TakeQuantize.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace chordengine::midi
{
// Standard MIDI File writer for recorder takes: format 1, one track,
// `ticksPerQuarterNote` division, a tempo and a time-signature meta event at
// tick 0, then the take's note events with running status, then end of track.
// The byte layout is the one 0.5.x wrote, so a dragged take is the same file.
inline void appendVariableLength(std::string& out, std::uint32_t value)
{
    std::uint8_t bytes[5];
    int count = 0;
    bytes[count++] = static_cast<std::uint8_t>(value & 0x7F);
    while ((value >>= 7) != 0)
        bytes[count++] = static_cast<std::uint8_t>((value & 0x7F) | 0x80);
    while (count > 0)
        out += static_cast<char>(bytes[--count]);
}

inline void appendBigEndian(std::string& out, std::uint32_t value, int bytes)
{
    for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8)
        out += static_cast<char>((value >> shift) & 0xFF);
}

inline std::string writeTakeMidiFile(const std::vector<TakeEvent>& events, double tempoBpm,
                                     int timeSignatureNumerator, int timeSignatureDenominator,
                                     int ticksPerQuarterNote = static_cast<int>(takeTicksPerQuarterNote))
{
    struct Message
    {
        std::int64_t tick = 0;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<Message> messages;

    const auto microsecondsPerQuarter = static_cast<std::uint32_t>(60000000.0 / (tempoBpm > 0.0 ? tempoBpm : 120.0));
    messages.push_back({ 0, { 0xFF, 0x51, 0x03,
                              static_cast<std::uint8_t>((microsecondsPerQuarter >> 16) & 0xFF),
                              static_cast<std::uint8_t>((microsecondsPerQuarter >> 8) & 0xFF),
                              static_cast<std::uint8_t>(microsecondsPerQuarter & 0xFF) } });
    int power = 0;
    for (int n = 1; n < timeSignatureDenominator; n <<= 1)
        ++power;
    messages.push_back({ 0, { 0xFF, 0x58, 0x04, static_cast<std::uint8_t>(timeSignatureNumerator),
                              static_cast<std::uint8_t>(power), 0x01, 0x60 } });

    for (const auto& event : events)
    {
        const auto channel = static_cast<std::uint8_t>((event.channel - 1) & 0x0F);
        Message message;
        message.tick = event.tick;
        if (event.noteOn)
            message.bytes = { static_cast<std::uint8_t>(0x90 | channel), static_cast<std::uint8_t>(event.note & 0x7F),
                              static_cast<std::uint8_t>(event.velocity & 0x7F) };
        else
            message.bytes = { static_cast<std::uint8_t>(0x80 | channel), static_cast<std::uint8_t>(event.note & 0x7F), 0x00 };
        // Ordered by time; equal times keep their order (inserted after).
        auto position = messages.end();
        while (position != messages.begin() && (position - 1)->tick > message.tick)
            --position;
        messages.insert(position, std::move(message));
    }

    std::string track;
    std::int64_t lastTick = 0;
    std::uint8_t lastStatus = 0;
    for (std::size_t index = 0; index < messages.size(); ++index)
    {
        const auto& message = messages[index];
        const auto delta = std::max<std::int64_t>(0, message.tick - lastTick);
        appendVariableLength(track, static_cast<std::uint32_t>(delta));
        lastTick = message.tick;
        const auto status = message.bytes[0];
        std::size_t first = 0;
        if (status == lastStatus && (status & 0xF0) != 0xF0 && message.bytes.size() > 1 && index > 0)
            first = 1; // running status
        for (std::size_t byte = first; byte < message.bytes.size(); ++byte)
            track += static_cast<char>(message.bytes[byte]);
        lastStatus = status;
    }
    track += std::string("\x00\xFF\x2F\x00", 4);

    std::string file = "MThd";
    appendBigEndian(file, 6, 4);
    appendBigEndian(file, 1, 2); // format 1
    appendBigEndian(file, 1, 2); // one track
    appendBigEndian(file, static_cast<std::uint32_t>(ticksPerQuarterNote), 2);
    file += "MTrk";
    appendBigEndian(file, static_cast<std::uint32_t>(track.size()), 4);
    file += track;
    return file;
}
} // namespace chordengine::midi
