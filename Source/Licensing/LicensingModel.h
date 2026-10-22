#pragma once

#include "LicensingTypes.h"

#include <functional>

namespace chordengine::licensing
{
// =====================================================================
// The licensing state machine. Pure logic: no network, no GUI, no file
// system, and no clock of its own (every time value is passed in). This
// is the native port of the reference `ceLic` block plus ceTrialAdvance /
// ceAuth* / ceApplyEntitlement, so the GUI and automated tests drive the
// exact same transitions.
//
// Every network action is requested through the hooks below and performed
// by LicensingBoundary, never here and never on the audio thread.
//
// THE ACCESS DECISION
//   `snapshot().licensed` is derived from the server's product-scoped
//   `plugin_entitled` field, never from the product-agnostic `subscribed`
//   membership flag. A licence key grants it on its own, with no
//   subscription. A missing, malformed or stale answer removes access; it
//   can never grant it.
// =====================================================================
class LicensingModel
{
public:
    LicensingModel();

    // ---- trial usage clock (reference ceTrialAdvance) ----------------
    // Advances the budget only while the trial is genuinely running:
    // never for a licensed user, never after expiry, and never before the
    // first generated chord. Returns true when this call expired the trial.
    bool advanceTrial(double deltaMs) noexcept;

    // The first real chord starts the clock (reference onNoteOn).
    void notePlaybackStarted() noexcept;

    // The persisted budget (see LicensingBoundary): a reload or a reopened
    // editor continues the trial instead of restarting it. The clock itself
    // still starts only at the first chord of the session.
    void restoreTrial(double remainingMs, bool expired) noexcept;
    bool trialRunning() const noexcept
    {
        return !snapshot_.licensed && snapshot_.trialStarted && !snapshot_.trialExpired;
    }

    // ---- account state machine (reference ceAuthTick / ceAuthButtonClicked)
    void tick();                       // 10 Hz driver: prime, poll, re-verify
    // Re-verify now (playback prepared, editor opened). No-op without a
    // token or while a link is pending.
    void requestVerificationNow();
    void signInOrCancel();             // the ONE account button
    void signOut();                    // infoSignOutButton
    void cancelLink();
    void beginLink();
    void onAuthStartReply(int status, const platform::Json& response);
    void onAuthPollReply(int status, const platform::Json& response);
    void onEntitlementsReply(int status, const platform::Json& response, std::int64_t nowMs);
    // A 401 means the token was revoked server-side: drop it AND the
    // remembered entitlement in the same transition.
    void handle401(std::int64_t nowMs);
    void applyCachedEntitlement(std::int64_t nowMs) noexcept;
    // The approval URL is handed to the browser exactly once.
    void clearVerificationUrl() noexcept { snapshot_.verificationUrl.clear(); }

    // ---- lifetime-licence activation (reference INFO-page key field) --
    // The customer's typed key is forwarded; the KEPT state comes from the
    // server's `entitlements` answer, never from this reply.
    void activateLicense(const std::string& licenseKey);
    void onActivateLicenseReply(int status, const platform::Json& response, std::int64_t nowMs);

    // ---- token handling (the value never leaves this class) ----------
    void loadToken(const std::string& token, const std::string& user,
                   const CachedEntitlement& cached, std::int64_t verifiedAtMs);
    const std::string& token() const noexcept { return token_; }
    const CachedEntitlement& cached() const noexcept { return snapshot_.cached; }
    std::int64_t verifiedAtMs() const noexcept { return snapshot_.verifiedAtMs; }
    const std::string& user() const noexcept { return snapshot_.user; }
    bool consumePersistenceDirty() noexcept;

    // ---- update check (reference ceCheckForUpdates + a service seam) --
    void checkForUpdates();
    void onUpdateResult(bool available, const std::string& latestVersion,
                        const std::string& status);
    void onUpdateFailed(const std::string& status);

    // ---- view -------------------------------------------------------
    const Snapshot& snapshot() const noexcept { return snapshot_; }
    bool expiredGateActive() const noexcept { return isExpiredGateActive(snapshot_); }

    // ---- action hooks (wired by LicensingBoundary; message thread) ----
    std::function<void()> requestLinkStart;
    std::function<void(const std::string& deviceCode)> requestLinkPoll;
    std::function<void(const std::string& token)> requestEntitlements;
    std::function<void(const std::string& token)> requestLogout;
    std::function<void(const std::string& licenseKey)> requestLicenseActivation;
    std::function<void()> requestUpdateCheck;

private:
    void resetEntitlement() noexcept;
    void setToken(const std::string& token, const std::string& user);

    Snapshot snapshot_ {};
    std::string token_;
    std::string deviceCode_;
    bool persistenceDirty_ = false;
    int pollTickCounter_ = 0;
    int verifyTickCounter_ = 0;
    bool primed_ = false;
};
} // namespace chordengine::licensing
