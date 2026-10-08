#pragma once

#include "ChordPresetSystem.h"
#include "TransposeEngine.h"
#include "../MIDI/MidiNoteTracker.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace chordengine::core
{
enum class VelocityMode : std::uint8_t { dynamic, maximum, fixed };

struct CoreConfiguration
{
    ChordPresetId preset = ChordPresetId::dreamy;
    ScaleId scale = ScaleId::minor;
    int keyIndex = 0;
    TransposeConfiguration transpose{};
    VelocityMode velocityMode = VelocityMode::dynamic;
    std::uint8_t fixedVelocity = 100;
};

struct CoreResult
{
    // At most 16*128*6 releases can follow a panic, plus small event overhead.
    static constexpr std::size_t maximumEvents = 16 * 128 * 6 + 16;
    std::array<chordengine::midi::NoteEvent, maximumEvents> events{};
    std::uint16_t eventCount = 0;
    bool supported = false;
    bool suppressInput = false;
    bool unresolved = false;
};

class ChordEngineCore
{
public:
    ChordEngineCore() noexcept = default;

    void process(const chordengine::midi::NoteEvent& input, CoreResult& result) noexcept;
    CoreResult process(const chordengine::midi::NoteEvent& input) noexcept;
    std::optional<NoteSet> generateChordForTrigger(int triggerNote) const noexcept;
    const CoreConfiguration& configuration() const noexcept { return configuration_; }
    bool setConfiguration(const CoreConfiguration& configuration) noexcept;
    void reset() noexcept;
    std::uint32_t activeVoiceCount() const noexcept;
    bool voiceSustained(std::uint8_t channel, std::uint8_t triggerNote) const noexcept;
    bool sustainHeld() const noexcept { return sustainHeld_; }

private:
    struct Voice
    {
        std::array<std::uint8_t, 6> pitches{};
        std::uint8_t pitchCount = 0;
        bool active = false;
        bool sustained = false;
    };

    static bool validInput(const chordengine::midi::NoteEvent& input) noexcept;
    static bool append(CoreResult& result, const chordengine::midi::NoteEvent& event) noexcept;
    bool buildNotes(int triggerNote, NoteSet& notes) const noexcept;
    void releaseVoice(std::size_t channel, std::size_t trigger, int sampleOffset,
                      CoreResult& result) noexcept;
    void releaseSustained(int sampleOffset, CoreResult& result) noexcept;
    void panic(int sampleOffset, CoreResult& result) noexcept;
    void addPitchOwner(std::size_t channel, std::uint8_t pitch) noexcept;
    void removePitchOwner(std::size_t channel, std::uint8_t pitch, int sampleOffset,
                          CoreResult& result) noexcept;

    CoreConfiguration configuration_{};
    std::array<std::array<Voice, 128>, 16> voices_{};
    std::array<std::array<std::uint16_t, 128>, 16> pitchOwners_{};
    bool sustainHeld_ = false;
};
} // namespace chordengine::core
