#pragma once

#include "UpdateTypes.h"

namespace chordengine::updates
{
// The normalised public fields of the LAST SUCCESSFUL answer plus the moment
// it was received. Only these fields are ever written to disk; nothing about
// the plugin's account, session or usage is involved, and no opaque identifiers
// are copied out of the response.
struct CachedUpdate
{
    bool valid = false;
    juce::String product;
    juce::String platform;
    juce::String currentVersion;
    juce::String latestVersion;   // empty when the server reported none
    juce::String decision;
    juce::String serverStatus;
    juce::String reason;
    bool mustUpdate = false;
    std::int64_t checkedAtMs = 0;
};

// Reads and writes `update-cache.json` in the Music-Prod application-data
// folder ChordEngine already uses for its other state. Every method is a plain
// file operation and never touches the network or the message thread.
class UpdateCache
{
public:
    explicit UpdateCache(juce::File file);

    CachedUpdate load() const;
    bool save(const CachedUpdate& entry) const;

    const juce::File& file() const noexcept { return file_; }
    static constexpr const char* fileName = "update-cache.json";

private:
    juce::File file_;
};
} // namespace chordengine::updates
