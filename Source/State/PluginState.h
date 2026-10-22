#pragma once

#include "../Core/ChordEngineCore.h"
#include "../MIDI/TakeQuantize.h"

#include <string>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace chordengine::state
{
// A preset from an installed pack, stored WITH its chord map so a project
// plays the same chords on reload and on another machine whether or not the
// pack is installed there. configuration.preset remains the stock fallback.
struct PackPresetState
{
    std::string packSlug;
    std::string packName;
    std::string presetName;
    chordengine::core::CustomChordPreset preset{};
};

enum class InternalSound : std::uint8_t { synth, piano, bass };

// Persistent user-controlled settings only: key, scale, chord preset (stock or
// pack), transpose target and octave, velocity mode and fixed velocity, and
// the internal-audio switch and sound, and the recorder's export quantize
// grid. Licensing, update, recorder,
// sustain/panic and held-note state are deliberately NOT part of this
// snapshot; they are never written into the host state.
struct PluginState
{
    chordengine::core::CoreConfiguration configuration{};
    std::optional<PackPresetState> packPreset;
    // Absent in payloads written before internal audio existed; the processor
    // then keeps its own default.
    std::optional<bool> internalAudioEnabled;
    std::optional<InternalSound> internalSound;
    // The recorder's export quantize grid. Absent in older payloads (the
    // processor then keeps none: takes export exactly as played).
    std::optional<chordengine::midi::TakeQuantize> recordQuantize;
    // FOLLOW DAW RECORD. Absent in older payloads (the processor keeps off).
    std::optional<bool> followHostRecord;
    // The VST3 host's bypass switch, stored with the state so a reloaded
    // project keeps it (0.5.x kept it in a private trailer). Written only by
    // the VST3 and only when on; ignored by the processor.
    std::optional<bool> hostBypass;
};

// Format version 1. Bump only with a migration path for older payloads.
inline constexpr int currentFormatVersion = 1;

// Upper bound for a host payload; anything larger is rejected unparsed.
inline constexpr std::size_t maximumSerializedBytes = 4096;

// Serializes the state as a UTF-8 XML document (root CHORDENGINE_STATE).
std::string serialize(const PluginState& state);

// Parses a host payload. Returns std::nullopt for anything malformed,
// incomplete-in-a-way-that-cannot-be-defaulted, invalid, oversized or of an
// incompatible version, so the caller keeps its current valid configuration.
// Absent known properties take their default value; unknown properties are ignored.
std::optional<PluginState> deserialize(const void* data, std::size_t sizeInBytes);
} // namespace chordengine::state
