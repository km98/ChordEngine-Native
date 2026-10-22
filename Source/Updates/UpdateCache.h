#pragma once

#include "UpdateTypes.h"

#include <filesystem>

namespace chordengine::updates
{
// The normalised public fields of the LAST SUCCESSFUL answer plus the moment
// it was received. Only these fields are ever written to disk; nothing about
// the plugin's account, session or usage is involved, and no opaque identifiers
// are copied out of the response.
struct CachedUpdate
{
    bool valid = false;
    std::string product;
    std::string platform;
    std::string currentVersion;
    std::string latestVersion;   // empty when the server reported none
    std::string decision;
    std::string serverStatus;
    std::string reason;
    bool mustUpdate = false;
    std::int64_t checkedAtMs = 0;
};

// Reads and writes `update-cache.json` in the Music-Prod application-data
// folder ChordEngine already uses for its other state. Every method is a plain
// file operation and never touches the network or the message thread.
class UpdateCache
{
public:
    explicit UpdateCache(std::filesystem::path file);

    CachedUpdate load() const;
    bool save(const CachedUpdate& entry) const;

    const std::filesystem::path& file() const noexcept { return file_; }
    static constexpr const char* fileName = "update-cache.json";

private:
    std::filesystem::path file_;
};
} // namespace chordengine::updates
