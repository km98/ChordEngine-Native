#pragma once

#include "../Core/ChordEngineCore.h"

#include <juce_core/juce_core.h>

#include <cstddef>
#include <optional>

namespace chordengine::state
{
// Persistent user-controlled musical settings only: key, scale, chord preset,
// transpose target and octave, velocity mode and fixed velocity. Licensing,
// update, recorder, sustain/panic and held-note state are deliberately NOT part
// of this snapshot; they are never written into the host state.
struct PluginState
{
    chordengine::core::CoreConfiguration configuration{};
};

// Format version 1. Bump only with a migration path for older payloads.
inline constexpr int currentFormatVersion = 1;

// Upper bound for a host payload; anything larger is rejected unparsed.
inline constexpr std::size_t maximumSerializedBytes = 4096;

// Serializes the state as a UTF-8 XML document (root CHORDENGINE_STATE).
juce::String serialize(const PluginState& state);

// Parses a host payload. Returns std::nullopt for anything malformed,
// incomplete-in-a-way-that-cannot-be-defaulted, invalid, oversized or of an
// incompatible version, so the caller keeps its current valid configuration.
// Absent known properties take their default value; unknown properties are ignored.
std::optional<PluginState> deserialize(const void* data, std::size_t sizeInBytes);
} // namespace chordengine::state
