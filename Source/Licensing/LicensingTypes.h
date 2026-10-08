#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>

// =====================================================================
// ChordEngine licensing / account / update vocabulary.
//
// EVERY constant and every user-facing string below is taken verbatim
// from the approved reference interface script
//   ChordEngine-FL/Build/layout-info-card-authoritative-pristine-20261002.js
// (read-only) - see the "LICENSING - Music-Prod plugin entitlement"
// block at line 3807 onwards and the INFO-page component definitions at
// lines 3320-3470. Nothing here is invented: no endpoint, no identifier,
// no policy value, no wording.
//
// The native product deliberately reuses the EXISTING Music-Prod plugin
// auth function that VYRE already ships. The plugin sends no API key and
// embeds no secret (the reference documents verify_jwt = false).
// =====================================================================
namespace chordengine::licensing
{
// From the reference: CE_AUTH_ORIGIN / CE_AUTH_FUNCTION.
inline constexpr const char* authOrigin = "https://wfpeajmdojcjqyrsnxbk.supabase.co/";
inline constexpr const char* authFunction = "functions/v1/vyre-plugin-auth";

// From the reference: the trial / cache / timer constants.
inline constexpr double trialTotalMs = 1800000.0; // CE_TRIAL_TOTAL_MS = 30:00
inline constexpr double plusCacheDays = 30.0;     // CE_PLUS_CACHE_DAYS
inline constexpr int authPollTicks = 8;           // CE_AUTH_POLL_TICKS (~0.8 s)
inline constexpr int authVerifyTicks = 9000;      // CE_AUTH_VERIFY_TICKS (15 min)
inline constexpr double tickMs = 100.0;           // Synth.startTimer(0.1)

// From the reference: the identity the plugin reports to the auth function.
inline constexpr const char* deviceName = "ChordEngine";

// From the reference: ceOpenStudio() -> Engine.openWebsite("musicprodstudio://open").
// The scheme is the one registered in the shipped app's CFBundleURLTypes.
inline constexpr const char* studioUrl = "musicprodstudio://open";

// From the reference: onControl(helpFeedbackButton) -> Engine.openWebsite(...).
inline constexpr const char* helpFeedbackUrl = "https://music-prod.com/plugin/chordengine-feedback";

// From the reference: CE_PRODUCT_VERSION (single source of the version string).
inline constexpr const char* productVersion = "0.4.0";

// From the reference: ceCheckForUpdates(). There is NO plugin-facing update
// API for ChordEngine, so the reference reports honestly and directs the user
// to Music-Prod Studio, which owns installation and updates.
inline constexpr const char* updateStatusUnavailable = "Open Music-Prod Studio to check for updates";

// Reference palette values used by the INFO page.
inline constexpr std::uint32_t colourPlusActive = 0xff7fd48a;
inline constexpr std::uint32_t colourPlusInactive = 0xffe0b050;
inline constexpr std::uint32_t colourTrialExpired = 0xffe05a5a;
inline constexpr std::uint32_t colourTrialActive = 0xffe8ebef;
inline constexpr std::uint32_t colourUpdateWarning = 0xffe0b050;

// Reference state machine (A/B/C) plus the transient linking state.
enum class Phase : std::uint8_t
{
    signedOut, // "signedOut"
    linking,   // "linking" - a device-code link is in flight
    signedIn   // "signedIn"
};

// Update-check surface. idle -> checking -> checked | failed.
enum class UpdateState : std::uint8_t
{
    idle,
    checking,
    checked,
    failed
};

// One coherent, copyable licensing view. The GUI renders only this.
struct Snapshot
{
    Phase phase = Phase::signedOut;
    bool hasToken = false;          // the opaque bearer token is never exposed
    juce::String user;              // server display name ("" = unknown)
    bool plusCached = false;        // last VERIFIED server answer for `subscribed`
    std::int64_t verifiedAtMs = 0;  // ms of that answer (0 = never verified)
    bool licensed = false;          // the gate the audio path reads
    bool polling = false;           // a device-code link is in flight
    juce::String userCode;          // public pairing code from the server
    juce::String verificationUrl;   // server-returned approval URL to open
    juce::String message;           // last action result, shown on the INFO page
    double trialMs = trialTotalMs;  // remaining trial budget
    bool trialStarted = false;      // the clock runs only after the first chord
    bool trialExpired = false;
    UpdateState updateState = UpdateState::idle;
    juce::String latestVersion;     // "" renders as "-" (reference behaviour)
    juce::String updateStatus;      // "" renders as "-"
};

// The single definition of "signed in": a token the server has not rejected.
inline bool isSignedIn(const Snapshot& snapshot) noexcept
{
    return snapshot.phase == Phase::signedIn && snapshot.hasToken;
}

// The expiry gate: an unlicensed trial that has actually run out. The
// reference gates chord generation AND shows the ACCESS EXPIRED modal on
// exactly this condition.
inline bool isExpiredGateActive(const Snapshot& snapshot) noexcept
{
    return !snapshot.licensed && snapshot.trialExpired;
}

// MM:SS display - the reference's ceFormatTrial(), including its
// Math.round(Math.ceil(ms / 1000)) rounding and its two-digit padding.
juce::String formatTrial(double msRemaining);

// The reference's ceUiText(): never let undefined/null/NaN reach a label.
juce::String uiText(const juce::var& value, const juce::String& fallback);

// ---- derived INFO-page lines (reference ceLicenseLine / refreshers) ----

juce::String trialStatusText(const Snapshot& snapshot);
std::uint32_t trialStatusColour(const Snapshot& snapshot) noexcept;

juce::String licenseLineText(const Snapshot& snapshot);
juce::String userLineText(const Snapshot& snapshot);
juce::String plusLineText(const Snapshot& snapshot);
std::uint32_t plusLineColour(const Snapshot& snapshot) noexcept;

juce::String signButtonText(const Snapshot& snapshot);
juce::String latestVersionText(const Snapshot& snapshot);
juce::String updateStatusText(const Snapshot& snapshot);
std::uint32_t updateStatusColour(const Snapshot& snapshot) noexcept;

// Reference expiry-modal body (ceRefreshTrialUI).
juce::String expiryModalBody(const Snapshot& snapshot);
inline constexpr const char* expiryModalTitle = "ACCESS EXPIRED";
} // namespace chordengine::licensing
