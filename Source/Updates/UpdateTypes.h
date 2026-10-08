#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>

// =====================================================================
// ChordEngine native update vocabulary.
//
// This module is the plugin-facing client of the EXISTING Music-Prod
// release service:
//
//   GET {origin}functions/v1/music-prod-studio-api/updates
//       ?product=chordengine&current=<build version>&platform=macos|windows
//
// It is deliberately independent of the licensing subsystem: the check is
// anonymous, it reads no account state, and nothing here is persisted
// except the normalised public fields of the last successful answer
// (see UpdateCache). The product slug is owned HERE, not by licensing.
// =====================================================================
namespace chordengine::updates
{
// The product this client speaks for. Owned by the update module.
inline constexpr const char* productSlug = "chordengine";

// The release-service function path on the shared Music-Prod origin.
inline constexpr const char* updateFunctionPath = "functions/v1/music-prod-studio-api/updates";

// Timeouts: one for establishing the connection, one for reading the body.
inline constexpr int connectTimeoutMs = 5000;
inline constexpr int readTimeoutMs = 5000;

// The ONLY retried failure is the release backend being temporarily
// unavailable (HTTP 503 / errorCode releaseBackendUnavailable). One retry.
inline constexpr int maxCheckAttempts = 2;
inline constexpr int retryBackoffMs = 1500;

// A response body larger than this is refused rather than buffered.
inline constexpr std::size_t maxResponseBytes = 256u * 1024u;

// The build version of the translation unit that includes this header, taken
// from the build metadata the plugin targets already generate (the same value
// JucePlugin_VersionString). Development builds report 0.1.0; nothing here
// hardcodes the reference product's 0.4.0.
//
// The update CLIENT never relies on this constant itself: it is told its build
// version when it is constructed (see UpdateService), so the value always comes
// from a translation unit of the plugin that was actually built. A translation
// unit without the build metadata reports 0.0.0 rather than guessing.
#if defined(JucePlugin_VersionString)
inline constexpr const char* currentBuildVersionString = JucePlugin_VersionString;
#else
inline constexpr const char* currentBuildVersionString = "0.0.0";
#endif

// "macos" | "windows" - the platform is ALWAYS sent explicitly (the server
// treats a missing platform as macOS).
juce::String platformName();

// The request URL, with the product slug, the actual build version and the
// explicit platform. `origin` is the shared Music-Prod origin.
juce::String buildUpdateUrl(const juce::String& origin, const juce::String& product,
                            const juce::String& currentVersion, const juce::String& platform);

// Strict semver: MAJOR.MINOR.PATCH with an optional -prerelease/+build tail.
bool isValidSemver(const juce::String& text);

// ---------------------------------------------------------- view model
// Every state the INFO page can show. No state ever fabricates a version.
enum class UpdateStatus : std::uint8_t
{
    idle,               // no check has been requested yet
    checking,           // a check is in flight
    upToDate,           // a published release exists and this build has it
    updateAvailable,    // a newer (or mandatory) published release exists
    noPublishedRelease, // the product exists but has no published release yet
    offlineCached,      // the service was unreachable; the last answer is shown
    error               // the request or the payload was rejected
};

struct UpdateView
{
    UpdateStatus status = UpdateStatus::idle;
    // When the shown answer was restored from the cache, this is the state the
    // cached answer itself reported, so the Latest row stays truthful even
    // while the Status row explains that the service is unreachable.
    UpdateStatus cachedStatus = UpdateStatus::idle;
    juce::String currentVersion; // the build version reported by this client
    juce::String latestVersion;  // empty when the service has no version for us
    juce::String detail;         // server reason / error detail - never a version
    bool cached = false;         // restored from (or kept as) the last answer
    bool mustUpdate = false;     // the service marked the release mandatory
    std::int64_t checkedAtMs = 0;
};

// ---- INFO-page lines. `Current version` always shows the build version;
// `Latest version` never shows "-" once a real answer exists.
juce::String currentVersionLine(const UpdateView& view);
juce::String latestVersionLine(const UpdateView& view);
juce::String statusLine(const UpdateView& view);
std::uint32_t statusColour(const UpdateView& view) noexcept;
} // namespace chordengine::updates
