#pragma once

#include "Platform/Json.h"
#include "Platform/Text.h"

#include <string>

#include <cstdint>
#include <string_view>

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
//
// THE SHARED PRODUCT RULE (verified in the backend, not assumed)
//   ChordEngine Native and ChordEngine FL are the SAME commercial
//   product, and the backend already defines both routes to it
//   (supabase/functions/_shared/pluginEntitlements.ts):
//       plugin_entitled = (subscription that COVERS chordengine)
//                      || (an active licence key AND a slot on THIS device)
//   `subscribed` is the user's raw Music-Prod+ MEMBERSHIP and stays
//   deliberately PRODUCT-AGNOSTIC, so it is display only and is never
//   the product-access decision. Neither route needs the other.
// =====================================================================
namespace chordengine::licensing
{
// From the reference: CE_AUTH_ORIGIN / CE_AUTH_FUNCTION.
inline constexpr const char* authOrigin = "https://wfpeajmdojcjqyrsnxbk.supabase.co/";
inline constexpr const char* authFunction = "functions/v1/vyre-plugin-auth";

// From the reference: the trial / cache / timer constants.
inline constexpr double trialTotalMs = 1800000.0; // CE_TRIAL_TOTAL_MS = 30:00
// CE_PLUS_CACHE_DAYS from the reference script. It is RETAINED as the
// reference value but is deliberately NOT the offline policy any more: the
// backend documents a per-route window (subscriptionGraceDays /
// licenseGraceDays below), and `subscribed` is not even the gate. See
// offlineGraceDays().
inline constexpr double plusCacheDays = 30.0;     // CE_PLUS_CACHE_DAYS
inline constexpr int authPollTicks = 8;           // CE_AUTH_POLL_TICKS (~0.8 s)
inline constexpr int authVerifyTicks = 9000;      // CE_AUTH_VERIFY_TICKS (15 min)
inline constexpr double tickMs = 100.0;           // Synth.startTimer(0.1)

// From the reference: the identity the plugin reports to the auth function.
inline constexpr const char* deviceName = "ChordEngine";

// The product slug of THIS product, as the backend's allowlist spells it
// (PRODUCT_PAGES / PLUGIN_PRODUCT_SLUGS / the licence tables). It is sent
// explicitly on `start` so the token's product never depends on the
// legacy device_name fallback, and it is INDEPENDENT of any display name:
// renaming "ChordEngine" on screen can never change what is entitled.
inline constexpr const char* productSlug = "chordengine";

// From the reference: ceOpenStudio() -> Engine.openWebsite("musicprodstudio://open").
// The scheme is the one registered in the shipped app's CFBundleURLTypes.
inline constexpr const char* studioUrl = "musicprodstudio://open";

// From the reference: onControl(helpFeedbackButton) -> Engine.openWebsite(...).
inline constexpr const char* helpFeedbackUrl = "https://music-prod.com/plugin/chordengine-feedback";

// The version this build reports to the Music-Prod auth function as
// `plugin_version` - the version of the binary that is actually running.
//
// The reference script's CE_PRODUCT_VERSION was `0.4.0`, the legacy FL
// release. This build does NOT report the reference's version: backend records
// for a 0.5.0 plug-in must say 0.5.0, so the field is defined by CMake from the
// single product version (CHORDENGINE_VERSION) on the licensing target. A
// translation unit compiled without that definition reports 0.0.0 rather than
// guessing. The static_assert below refuses to compile a build in which the
// telemetry version and the compiled plug-in version disagree.
#if defined(CHORDENGINE_PRODUCT_VERSION)
inline constexpr const char* productVersion = CHORDENGINE_PRODUCT_VERSION;
#else
inline constexpr const char* productVersion = "0.0.0";
#endif

#if defined(CHORDENGINE_PLUGIN_VERSION_STRING) && defined(CHORDENGINE_PRODUCT_VERSION)
static_assert(std::string_view(CHORDENGINE_PLUGIN_VERSION_STRING)
                  == std::string_view(CHORDENGINE_PRODUCT_VERSION),
              "the licensing telemetry version must be the compiled plug-in version");
#endif

// From the reference: ceCheckForUpdates(). There is NO plugin-facing update
// API for ChordEngine, so the reference reports honestly and directs the user
// to Music-Prod Studio, which owns installation and updates.
inline constexpr const char* updateStatusUnavailable = "Open Music-Prod Studio to check for updates";

// ---------------------------------------------------------------------
// The offline windows, per route. These are the backend's OWN documented
// policy (docs/plugin-license-keys.md), not something invented here:
//   * Music-Prod+ subscription : kEntitlementGraceSeconds        = 14 days
//   * licence key (device-bound): kLicenseActivationGraceSeconds =  7 days
// The licence window is deliberately the shorter one, because a released
// computer has to be able to lose the entitlement: "deactivating a computer
// takes effect the next time that computer is online - at the latest 7 days
// after it was last online." A cache is never trusted beyond its route's
// window, and a cache that granted nothing is trusted for no time at all.
inline constexpr double subscriptionGraceDays = 14.0; // kEntitlementGraceSeconds
inline constexpr double licenseGraceDays = 7.0;       // kLicenseActivationGraceSeconds

// The commercial policy the backend enforces (DEVICE_ACTIVATION_LIMIT: at
// most three ACTIVE devices per key). Used only to render a slot count
// before the server has answered; the server's own device_limit is what the
// UI shows once it has.
inline constexpr int deviceActivationLimit = 3;

// The licence status vocabulary of the entitlement answer
// (PluginLicenseStatus in _shared/pluginEntitlements.ts).
inline constexpr const char* licenseStatusNone = "none";
inline constexpr const char* licenseStatusActive = "active";
inline constexpr const char* licenseStatusRevoked = "revoked";
inline constexpr const char* licenseStatusExpired = "expired";

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

// ---------------------------------------------------------------------
// One VERIFIED entitlement answer, reduced to the facts the client may
// persist and re-apply offline. It is the whole of what the plugin keeps
// about access: the answer, the moment it arrived and this device's id -
// never an entitlement flag of its own invention.
//
// `subscribed` is the raw Music-Prod+ membership (the account area's
// concern); `entitled` is the product-scoped decision; the two route flags
// say WHICH route granted it, which is what selects the offline window and
// what lets the UI tell a subscriber apart from a licence holder.
// ---------------------------------------------------------------------
struct CachedEntitlement
{
    bool entitled = false;             // `plugin_entitled` - THE gate
    bool subscribed = false;           // `subscribed` - display only
    bool subscriptionEntitles = false; // the subscription covers this product
    bool licenseEntitles = false;      // ...and/or THIS device holds a slot
    bool licenseAccountActive = false; // the account holds an active licence
    bool licenseDeviceActive = false;  // THIS computer holds one of its slots
    std::string licenseStatus = licenseStatusNone; // none|active|revoked|expired
    int devicesUsed = 0;               // licence slots in use
    int deviceLimit = deviceActivationLimit;
};

// One coherent, copyable licensing view. The GUI renders only this.
struct Snapshot
{
    Phase phase = Phase::signedOut;
    bool hasToken = false;          // the opaque bearer token is never exposed
    std::string user;              // server display name ("" = unknown)
    CachedEntitlement cached;       // last VERIFIED server answer
    std::int64_t verifiedAtMs = 0;  // ms of that answer (0 = never verified)
    bool licensed = false;          // the gate the audio path reads
    bool polling = false;           // a device-code link is in flight
    std::string userCode;          // public pairing code from the server
    std::string verificationUrl;   // server-returned approval URL to open
    std::string message;           // last action result, shown on the INFO page
    bool activationPending = false; // a licence-key activation is in flight
    double trialMs = trialTotalMs;  // remaining trial budget
    bool trialStarted = false;      // the clock runs only after the first chord
    bool trialExpired = false;
    UpdateState updateState = UpdateState::idle;
    std::string latestVersion;     // "" renders as "-" (reference behaviour)
    std::string updateStatus;      // "" renders as "-"
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

// ---------------------------------------------------------------------
// The offline window this cached answer is honoured for. The LICENCE route
// is the binding one whenever it is present - it is the route a
// deactivation has to be able to withdraw, and its window is the shorter -
// so a licence answer never gets the subscription's 14 days. An answer that
// granted access but named no route is trusted for no time at all.
// ---------------------------------------------------------------------
inline double offlineGraceDays(const CachedEntitlement& cached) noexcept
{
    if (!cached.entitled)
        return 0.0;
    if (cached.licenseEntitles)
        return licenseGraceDays;
    if (cached.subscriptionEntitles)
        return subscriptionGraceDays;
    return 0.0;
}

inline double offlineGraceDays(const Snapshot& snapshot) noexcept
{
    return offlineGraceDays(snapshot.cached);
}

// ---------------------------------------------------------------------
// The device identity the licence route binds to.
//
// An opaque, per-INSTALLATION random value, generated once and kept beside
// the auth cache. It is deliberately NOT a hardware serial, a MAC address
// or anything derived from the machine: it identifies "this install of
// ChordEngine", which is exactly the thing an owner releases from their
// account, and it collects nothing else about the computer. It grants
// nothing on its own, and the server only ever stores its SHA-256 hash.
// ---------------------------------------------------------------------
inline constexpr int deviceIdMinLength = 16;  // DEVICE_ID_MIN_LENGTH
inline constexpr int deviceIdMaxLength = 128; // DEVICE_ID_MAX_LENGTH

// A fresh 128-bit device id (32 lowercase hex characters).
std::string newDeviceId();

// The backend's format rule (pluginLicenseDevices.ts isDeviceIdFormat):
// 16..128 characters, no whitespace. Normalisation trims and lowercases,
// because case and surrounding whitespace are not identity.
bool isDeviceIdFormat(const std::string& deviceId) noexcept;
std::string normalizeDeviceId(const std::string& deviceId);

// MM:SS display - the reference's ceFormatTrial(), including its
// Math.round(Math.ceil(ms / 1000)) rounding and its two-digit padding.
std::string formatTrial(double msRemaining);

// The reference's ceUiText(): never let undefined/null/NaN reach a label.
std::string uiText(const std::string& value, const std::string& fallback);

// What the 0.5 build's variant type produced as text for a JSON value: text as is, a
// boolean as "1"/"0", a number in decimal, anything else "". The licensing
// contract was written against those conversions, so they are kept exactly.
std::string varText(const platform::Json& value);
// That variant type's numeric conversion: numbers, booleans (1/0) and the leading
// number of a string; anything else 0.
double varNumber(const platform::Json& value);

// ---- derived INFO-page lines (reference ceLicenseLine / refreshers) ----

std::string trialStatusText(const Snapshot& snapshot);
std::uint32_t trialStatusColour(const Snapshot& snapshot) noexcept;

std::string licenseLineText(const Snapshot& snapshot);
std::string userLineText(const Snapshot& snapshot);
std::string plusLineText(const Snapshot& snapshot);
std::uint32_t plusLineColour(const Snapshot& snapshot) noexcept;

// The licence route's slot count ("2 of 3 computers"), or "" when no
// licence route is known. Never a policy value of its own: it renders the
// server's devices_used / device_limit.
std::string deviceUsageText(const Snapshot& snapshot);

// The licence-key control's button label: ACTIVATE, or the in-flight state.
std::string activationButtonText(const Snapshot& snapshot);

std::string signButtonText(const Snapshot& snapshot);
std::string latestVersionText(const Snapshot& snapshot);
std::string updateStatusText(const Snapshot& snapshot);
std::uint32_t updateStatusColour(const Snapshot& snapshot) noexcept;

// Reference expiry-modal body (ceRefreshTrialUI).
std::string expiryModalBody(const Snapshot& snapshot);
inline constexpr const char* expiryModalTitle = "ACCESS EXPIRED";
} // namespace chordengine::licensing
