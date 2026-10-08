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

    // ---- account state machine (reference ceAuthTick / ceAuthButtonClicked)
    void tick();                       // 10 Hz driver: prime, poll, re-verify
    void signInOrCancel();             // the ONE account button
    void signOut();                    // infoSignOutButton
    void cancelLink();
    void beginLink();
    void onAuthStartReply(int status, const juce::var& response);
    void onAuthPollReply(int status, const juce::var& response);
    void onEntitlementsReply(int status, const juce::var& response, std::int64_t nowMs);
    // A 401 means the token was revoked server-side: drop it AND the
    // remembered entitlement in the same transition.
    void handle401(std::int64_t nowMs);
    void applyCachedEntitlement(std::int64_t nowMs) noexcept;
    // The approval URL is handed to the browser exactly once.
    void clearVerificationUrl() noexcept { snapshot_.verificationUrl.clear(); }

    // ---- token handling (the value never leaves this class) ----------
    void loadToken(const juce::String& token, const juce::String& user,
                   bool subscribed, std::int64_t verifiedAtMs);
    const juce::String& token() const noexcept { return token_; }
    bool plusCached() const noexcept { return snapshot_.plusCached; }
    std::int64_t verifiedAtMs() const noexcept { return snapshot_.verifiedAtMs; }
    const juce::String& user() const noexcept { return snapshot_.user; }
    bool consumePersistenceDirty() noexcept;

    // ---- update check (reference ceCheckForUpdates + a service seam) --
    void checkForUpdates();
    void onUpdateResult(bool available, const juce::String& latestVersion,
                        const juce::String& status);
    void onUpdateFailed(const juce::String& status);

    // ---- view -------------------------------------------------------
    const Snapshot& snapshot() const noexcept { return snapshot_; }
    bool expiredGateActive() const noexcept { return isExpiredGateActive(snapshot_); }

    // ---- action hooks (wired by LicensingBoundary; message thread) ----
    std::function<void()> requestLinkStart;
    std::function<void(const juce::String& deviceCode)> requestLinkPoll;
    std::function<void(const juce::String& token)> requestEntitlements;
    std::function<void(const juce::String& token)> requestLogout;
    std::function<void()> requestUpdateCheck;

private:
    void resetEntitlement() noexcept;
    void setToken(const juce::String& token, const juce::String& user);

    Snapshot snapshot_ {};
    juce::String token_;
    juce::String deviceCode_;
    bool persistenceDirty_ = false;
    int pollTickCounter_ = 0;
    int verifyTickCounter_ = 0;
    bool primed_ = false;
};
} // namespace chordengine::licensing
