#pragma once

#include "PackFormat.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace chordengine::packs
{
// ---------------------------------------------------------------------
// The installed Preset Packs.
//
// Downloaded packs live as <packsDirectory>/<slug>.chordpack (the pack JSON,
// byte-identical to what the store published). The stock pack is VIRTUAL:
// it is built from ChordPresetSystem, is always list()[0] and is never read
// from or written to disk, so no file operation can ever remove or replace
// the built-in presets.
//
// Not thread-safe: own and call it from one thread (the message thread).
// ---------------------------------------------------------------------
class PackLibrary
{
public:
    static constexpr const char* fileExtension = ".chordpack";

    explicit PackLibrary(juce::File packsDirectory);

    // <Application Support>/Music-Prod/ChordEngine/Packs - beside auth.json,
    // the same root the licensing state uses.
    static juce::File defaultPacksDirectory();

    const juce::File& directory() const noexcept { return directory_; }
    juce::File fileFor(const juce::String& slug) const;

    // Reloads every *.chordpack in the directory. A file that fails to parse,
    // or whose slug does not match its file name, is skipped (and reported in
    // scanErrors()) - one bad file never hides the others. Stale temporary
    // files from an interrupted install are deleted.
    void scan();
    const juce::StringArray& scanErrors() const noexcept { return scanErrors_; }

    // Verifies `bytes` against `expectedSha256` (lowercase hex), parses and
    // validates them, requires the pack's slug to equal `slug`, then writes
    // the file atomically (temp file + rename over the old one) and updates
    // the in-memory list. Returns "" on success, otherwise the reason; on
    // failure nothing on disk or in memory changes.
    juce::String install(const juce::String& slug, const juce::MemoryBlock& bytes,
                         const juce::String& expectedSha256);

    // Deletes an installed pack. Returns "" on success. The stock pack cannot
    // be removed.
    juce::String remove(const juce::String& slug);

    // Stock first, then installed packs ordered by name.
    const std::vector<PackDefinition>& list() const noexcept { return packs_; }
    const PackDefinition* find(const juce::String& slug) const noexcept;

    // The packs whose presets may be offered right now: the stock pack always,
    // downloaded packs only while the account is an active Music-Prod+ member
    // (`subscribed` of the cached, verified entitlement). This is a client
    // convenience; the store's download gate is the real one.
    std::vector<const PackDefinition*> usablePacks(bool memberActive) const;
    static bool isUsable(const PackDefinition& pack, bool memberActive) noexcept;

private:
    void sortAndPinStock();

    juce::File directory_;
    std::vector<PackDefinition> packs_;
    juce::StringArray scanErrors_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PackLibrary)
};
} // namespace chordengine::packs
