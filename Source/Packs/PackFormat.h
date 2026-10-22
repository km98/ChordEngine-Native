#pragma once

#include "Core/ChordPresetSystem.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// =====================================================================
// ChordEngine Preset Pack format ("chordengine-pack", formatVersion 1).
//
// A pack is one UTF-8 JSON document holding named presets in EXACTLY the
// shape the built-in presets already use (Core/ChordPresetSystem): a
// voicing style and a 73-entry trigger map for MIDI notes 24..96, where
// each code is `templateIndex * 7 + scaleDegree` (template 0..9, degree
// 0..6, so 0..69). A pack carries data only - no script, no samples.
//
// The format is documented in Docs/PRESET_PACKS.md. The validator below is
// strict on everything the engine reads and ignores unknown keys, so a later
// writer can add fields without breaking this reader.
// =====================================================================
namespace chordengine::packs
{
inline constexpr const char* formatName = "chordengine-pack";
inline constexpr int formatVersion = 1;

// The stock pack: the 12 built-in presets. Always present, never on disk and
// never gated beyond product access itself.
inline constexpr const char* stockSlug = "chordengine-stock";
inline constexpr const char* stockName = "ChordEngine Stock Pack";
inline constexpr const char* stockVersion = "1.0.0";
inline constexpr const char* defaultAuthor = "Music-Prod";

inline constexpr std::size_t triggerCount = core::ChordPresetSystem::triggerCount; // 73
inline constexpr int maxTriggerCode = static_cast<int>(core::ChordPresetSystem::templateCount) * 7 - 1; // 69
inline constexpr std::size_t maxPackBytes = 256u * 1024u;
inline constexpr int minPresets = 1;
inline constexpr int maxPresets = 64;
inline constexpr int maxPresetNameLength = 40;
inline constexpr int maxPackNameLength = 64;
inline constexpr int maxAuthorLength = 64;
inline constexpr int maxDescriptionLength = 512;

enum class VoicingStyle : std::uint8_t
{
    close,
    spread,
    open
};

// "close" | "spread" | "open" - the same spelling ChordPresetDefinition uses.
const char* toString(VoicingStyle style) noexcept;
std::optional<VoicingStyle> voicingFromString(const juce::String& text) noexcept;

struct PackPreset
{
    juce::String name;
    VoicingStyle voicing = VoicingStyle::close;
    std::array<std::uint8_t, triggerCount> triggerMap {};

    bool operator==(const PackPreset& other) const noexcept;
    bool operator!=(const PackPreset& other) const noexcept { return !(*this == other); }
};

struct PackDefinition
{
    juce::String slug;
    juce::String name;
    juce::String version;
    juce::String author = defaultAuthor;
    juce::String description;
    std::vector<PackPreset> presets;
    bool isStock = false; // derived from the slug, never read from JSON

    bool operator==(const PackDefinition& other) const noexcept;
    bool operator!=(const PackDefinition& other) const noexcept { return !(*this == other); }
};

// ^[a-z0-9][a-z0-9-]{1,62}$
bool isValidSlug(const juce::String& slug) noexcept;

// "MAJOR.MINOR.PATCH", each a decimal integer without sign.
bool isValidVersion(const juce::String& version) noexcept;

// <0, 0 or >0 like strcmp. Invalid versions sort before every valid one.
int compareVersions(const juce::String& a, const juce::String& b) noexcept;

// Parses and validates one pack document. On failure returns nullopt and sets
// `error` to a short, developer-facing reason ("presets[2].triggerMap[5]: out
// of range"); on success `error` is cleared.
std::optional<PackDefinition> parse(const juce::String& json, juce::String& error);
std::optional<PackDefinition> parse(const juce::MemoryBlock& utf8, juce::String& error);

// Deterministic serialisation (stable key order, one trigger map per line),
// so a pack written twice is byte-identical and its SHA-256 is reproducible.
juce::String toJson(const PackDefinition& pack);

// The stock pack built from ChordPresetSystem itself, so it is by
// construction exactly the built-in maps.
PackDefinition makeStockPack();

// Lowercase hex SHA-256 of raw bytes (the digest the store publishes).
juce::String sha256Hex(const juce::MemoryBlock& bytes);
bool isValidSha256Hex(const juce::String& digest) noexcept;
} // namespace chordengine::packs
