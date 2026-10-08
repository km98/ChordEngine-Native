#include "LicensingModel.h"

namespace chordengine::licensing
{
namespace
{
juce::String propertyText(const juce::var& response, const char* name)
{
    if (response.isVoid() || response.isUndefined())
        return {};
    return response.getProperty(name, juce::var()).toString();
}

double propertyNumber(const juce::var& response, const char* name)
{
    if (response.isVoid() || response.isUndefined())
        return 0.0;
    return static_cast<double>(response.getProperty(name, juce::var()));
}
}

LicensingModel::LicensingModel()
{
    snapshot_.trialMs = trialTotalMs;
}

// ---------------------------------------------------------------- trial
bool LicensingModel::advanceTrial(double deltaMs) noexcept
{
    if (snapshot_.licensed || snapshot_.trialExpired || !snapshot_.trialStarted)
        return false;

    snapshot_.trialMs -= deltaMs;
    if (snapshot_.trialMs > 0.0)
        return false;

    // The single expiry transition (reference ceTrialExpire).
    snapshot_.trialMs = 0.0;
    snapshot_.trialExpired = true;
    return true;
}

void LicensingModel::notePlaybackStarted() noexcept
{
    if (!snapshot_.licensed)
        snapshot_.trialStarted = true;
}

// ----------------------------------------------------------- account
void LicensingModel::setToken(const juce::String& token, const juce::String& user)
{
    token_ = token;
    snapshot_.hasToken = token.isNotEmpty();
    if (user.isNotEmpty())
        snapshot_.user = user;
    persistenceDirty_ = true;
}

void LicensingModel::resetEntitlement() noexcept
{
    snapshot_.plusCached = false;
    snapshot_.verifiedAtMs = 0;
    snapshot_.licensed = false;
}

void LicensingModel::loadToken(const juce::String& token, const juce::String& user,
                               bool subscribed, std::int64_t verifiedAtMs)
{
    token_ = token;
    snapshot_.hasToken = token.isNotEmpty();
    snapshot_.user = user;
    snapshot_.plusCached = subscribed;
    snapshot_.verifiedAtMs = verifiedAtMs;

    // A persisted token makes the session signed in; the first entitlement
    // check either confirms it or clears it through handle401().
    snapshot_.phase = token.isNotEmpty() ? Phase::signedIn : Phase::signedOut;
    primed_ = false;
    persistenceDirty_ = false;
}

bool LicensingModel::consumePersistenceDirty() noexcept
{
    const auto dirty = persistenceDirty_;
    persistenceDirty_ = false;
    return dirty;
}

void LicensingModel::applyCachedEntitlement(std::int64_t nowMs) noexcept
{
    if (token_.isEmpty() || !snapshot_.plusCached || snapshot_.verifiedAtMs <= 0)
    {
        snapshot_.licensed = false;
        return;
    }

    // A clock that moved backwards invalidates the cache (fail closed):
    // the window can never be extended by moving the clock.
    if (nowMs < snapshot_.verifiedAtMs)
    {
        snapshot_.licensed = false;
        return;
    }

    const auto elapsedDays = static_cast<double>(nowMs - snapshot_.verifiedAtMs)
        / (1000.0 * 60.0 * 60.0 * 24.0);
    snapshot_.licensed = elapsedDays <= plusCacheDays;
}

void LicensingModel::handle401(std::int64_t nowMs)
{
    juce::ignoreUnused(nowMs);
    const bool hadUser = token_.isNotEmpty();

    token_.clear();
    deviceCode_.clear();
    snapshot_.hasToken = false;
    snapshot_.polling = false;
    snapshot_.userCode.clear();
    snapshot_.verificationUrl.clear();
    snapshot_.phase = Phase::signedOut;
    snapshot_.user.clear();
    resetEntitlement();
    persistenceDirty_ = true;

    if (hadUser)
        snapshot_.message = "Your Music-Prod session ended. Click SIGN IN to reconnect.";
}

void LicensingModel::beginLink()
{
    if (snapshot_.polling)
        return;

    snapshot_.phase = Phase::linking;
    snapshot_.message = "Requesting a link code...";
    if (requestLinkStart)
        requestLinkStart();
}

void LicensingModel::cancelLink()
{
    snapshot_.polling = false;
    snapshot_.userCode.clear();
    snapshot_.verificationUrl.clear();
    deviceCode_.clear();
    snapshot_.phase = token_.isNotEmpty() ? Phase::signedIn : Phase::signedOut;
    snapshot_.message = "Link cancelled.";
}

void LicensingModel::signInOrCancel()
{
    if (snapshot_.polling)
    {
        cancelLink();
        return;
    }

    if (isSignedIn(snapshot_))
    {
        signOut();
        return;
    }

    beginLink();
}

void LicensingModel::signOut()
{
    if (token_.isNotEmpty())
    {
        // The reference revokes server-side first, then clears every local
        // trace in one transition.
        if (requestLogout)
        {
            const auto token = token_;
            requestLogout(token);
        }
    }

    handle401(0);
    snapshot_.message = "Signed out of Music-Prod.";
}

void LicensingModel::onAuthStartReply(int status, const juce::var& response)
{
    const auto deviceCode = propertyText(response, "device_code");
    if (status == 200 && deviceCode.isNotEmpty())
    {
        deviceCode_ = deviceCode;
        snapshot_.userCode = propertyText(response, "user_code");
        snapshot_.polling = true;
        pollTickCounter_ = 0;
        snapshot_.phase = Phase::linking;
        snapshot_.message = "Enter code " + uiText(snapshot_.userCode, "(see the Music-Prod page)")
            + " on the Music-Prod page that just opened.";
        snapshot_.verificationUrl = propertyText(response, "verification_url_complete");
        if (snapshot_.verificationUrl.isEmpty())
            snapshot_.verificationUrl = propertyText(response, "verification_url");
        return;
    }

    snapshot_.polling = false;
    snapshot_.phase = Phase::signedOut;
    snapshot_.message = "Could not reach Music-Prod. Check your connection and try again.";
}

void LicensingModel::onAuthPollReply(int status, const juce::var& response)
{
    const auto state = propertyText(response, "status");

    if (status == 200 && state == "approved")
    {
        snapshot_.polling = false;
        snapshot_.userCode.clear();
        snapshot_.verificationUrl.clear();
        deviceCode_.clear();
        snapshot_.phase = Phase::signedIn;
        setToken(propertyText(response, "token"), propertyText(response, "display_name"));
        if (token_.isEmpty())
            snapshot_.phase = Phase::signedOut;
        snapshot_.message = "Signed in to Music-Prod. Verifying your Music-Prod+ status...";
        if (token_.isNotEmpty() && requestEntitlements)
            requestEntitlements(token_);
        return;
    }

    if (state == "denied")
    {
        snapshot_.polling = false;
        snapshot_.userCode.clear();
        deviceCode_.clear();
        snapshot_.phase = Phase::signedOut;
        snapshot_.message = "Link request denied.";
        return;
    }

    if (state == "expired" || state == "invalid")
    {
        snapshot_.polling = false;
        snapshot_.userCode.clear();
        deviceCode_.clear();
        snapshot_.phase = Phase::signedOut;
        snapshot_.message = "The link code expired. Click SIGN IN to get a new one.";
        return;
    }

    if (status >= 400)
    {
        snapshot_.polling = false;
        snapshot_.userCode.clear();
        deviceCode_.clear();
        snapshot_.phase = Phase::signedOut;
        snapshot_.message = "Could not complete the link. Click SIGN IN to try again.";
    }
    // Otherwise (pending, or a transport failure): keep the code and retry
    // on the next tick.
}

void LicensingModel::onEntitlementsReply(int status, const juce::var& response,
                                         std::int64_t nowMs)
{
    if (status == 200 && !response.isVoid() && !response.isUndefined()
        && !response.getProperty("subscribed", juce::var()).isVoid())
    {
        snapshot_.phase = Phase::signedIn;
        const bool subscribed = propertyNumber(response, "subscribed") != 0.0;

        snapshot_.plusCached = subscribed;
        snapshot_.verifiedAtMs = nowMs;
        snapshot_.licensed = subscribed;
        persistenceDirty_ = true;

        snapshot_.message = subscribed
            ? "Music-Prod+ active - full access."
            : "Signed in. Music-Prod+ unlocks unlimited access.";
        return;
    }

    if (status == 401)
    {
        handle401(nowMs);
        return;
    }

    // 5xx / transport failure: keep the last verified answer (the existing
    // product rule); the bounded offline window is what ends that grace.
    applyCachedEntitlement(nowMs);
}

void LicensingModel::tick()
{
    if (!primed_)
    {
        primed_ = true;
        if (token_.isNotEmpty() && requestEntitlements)
            requestEntitlements(token_);
        return;
    }

    if (snapshot_.polling)
    {
        ++pollTickCounter_;
        if (pollTickCounter_ >= authPollTicks && requestLinkPoll && deviceCode_.isNotEmpty())
        {
            pollTickCounter_ = 0;
            requestLinkPoll(deviceCode_);
        }
        return;
    }

    if (token_.isNotEmpty())
    {
        ++verifyTickCounter_;
        if (verifyTickCounter_ >= authVerifyTicks)
        {
            verifyTickCounter_ = 0;
            if (requestEntitlements)
                requestEntitlements(token_);
        }
    }
}

// ------------------------------------------------------------ update
void LicensingModel::checkForUpdates()
{
    snapshot_.latestVersion.clear();
    snapshot_.updateStatus.clear();
    snapshot_.updateState = UpdateState::checking;
    if (requestUpdateCheck)
        requestUpdateCheck();
}

void LicensingModel::onUpdateResult(bool available, const juce::String& latestVersion,
                                    const juce::String& status)
{
    // The reference never fakes a latest version: when the update service
    // has no answer the Latest row keeps its "-" placeholder and the
    // Status row carries the real, honest result text.
    snapshot_.latestVersion = available ? latestVersion : juce::String();
    snapshot_.updateStatus = status;
    snapshot_.updateState = UpdateState::checked;
}

void LicensingModel::onUpdateFailed(const juce::String& status)
{
    snapshot_.latestVersion.clear();
    snapshot_.updateStatus = status;
    snapshot_.updateState = UpdateState::failed;
}
} // namespace chordengine::licensing
