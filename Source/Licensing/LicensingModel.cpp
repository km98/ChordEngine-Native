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

// A nested object of an answer ("license", "subscription", "device"), or an
// empty var when the server did not send it.
juce::var nestedObject(const juce::var& response, const char* name)
{
    if (response.isVoid() || response.isUndefined())
        return {};

    const auto value = response.getProperty(name, juce::var());
    return value.isObject() ? value : juce::var();
}

bool objectHasField(const juce::var& object, const char* name)
{
    if (object.isVoid() || object.isUndefined())
        return false;

    const auto value = object.getProperty(name, juce::var());
    return !value.isVoid() && !value.isUndefined();
}

// A boolean field, for DISPLAY facts. `fallback` is used when the server did
// not send the field at all.
bool propertyBool(const juce::var& object, const char* name, bool fallback)
{
    if (!objectHasField(object, name))
        return fallback;

    const auto value = object.getProperty(name, juce::var());
    if (value.isBool())
        return static_cast<bool>(value);
    if (value.isInt() || value.isInt64() || value.isDouble())
        return static_cast<double>(value) != 0.0;
    if (value.isString())
    {
        const auto text = value.toString().trim().toLowerCase();
        return text == "true" || text == "1";
    }

    return fallback;
}

// Does this reply actually carry a product decision?
//
// A reply counts as an entitlement answer only when `plugin_entitled` is a real
// JSON boolean. Anything else - the field missing, null, a string, a number, an
// array - is NOT a decision this contract defines, so it is treated exactly as
// if the field were absent: it can neither grant nor withdraw access, and it
// never refreshes the verified window. That is deliberately the SAME treatment
// an unparseable body already gets further down, because the two are the same
// thing: a reply that carries no usable answer. Treating a non-boolean as a
// definitive denial instead would let a body the contract cannot produce
// destroy a valid cached entitlement (and persist the loss), while granting
// nothing in return - the grant boundary is already strict, since only a real
// boolean `true` opens the gate.
bool carriesEntitlementDecision(const juce::var& response)
{
    if (response.isVoid() || response.isUndefined() || !response.isObject())
        return false;

    return response.getProperty("plugin_entitled", juce::var()).isBool();
}

// THE GATE, read STRICTLY: only a real JSON boolean `true` grants access. A
// missing field, a string, a number or any other shape is NOT an entitlement,
// so a malformed answer can never be turned into a licence.
bool entitledFlag(const juce::var& response)
{
    if (!carriesEntitlementDecision(response))
        return false;

    return static_cast<bool>(response.getProperty("plugin_entitled", juce::var()));
}

double elapsedDays(std::int64_t fromMs, std::int64_t toMs) noexcept
{
    return static_cast<double>(toMs - fromMs) / (1000.0 * 60.0 * 60.0 * 24.0);
}
} // namespace

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
    snapshot_.cached = CachedEntitlement {};
    snapshot_.verifiedAtMs = 0;
    snapshot_.licensed = false;
}

void LicensingModel::loadToken(const juce::String& token, const juce::String& user,
                               const CachedEntitlement& cached, std::int64_t verifiedAtMs)
{
    token_ = token;
    snapshot_.hasToken = token.isNotEmpty();
    snapshot_.user = user;
    snapshot_.cached = cached;
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
    if (token_.isEmpty() || !snapshot_.cached.entitled || snapshot_.verifiedAtMs <= 0)
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

    // The window belongs to the ROUTE that granted the answer: 14 days for a
    // subscription, 7 for a device-bound licence (the shorter, binding one
    // whenever a licence is what grants access). A cache that granted nothing
    // is honoured for no time at all, so an expired cache can never restore
    // access after a revocation, and a deactivated computer loses the licence
    // route within its own documented window.
    const auto windowDays = offlineGraceDays(snapshot_.cached);
    snapshot_.licensed = windowDays > 0.0 && elapsedDays(snapshot_.verifiedAtMs, nowMs) <= windowDays;
}

void LicensingModel::handle401(std::int64_t nowMs)
{
    juce::ignoreUnused(nowMs);
    const bool hadUser = token_.isNotEmpty();

    token_.clear();
    deviceCode_.clear();
    snapshot_.hasToken = false;
    snapshot_.polling = false;
    snapshot_.activationPending = false;
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
    if (status == 200 && carriesEntitlementDecision(response))
    {
        snapshot_.phase = Phase::signedIn;

        // THE decision, and only this field decides it. `subscribed` is the
        // user's raw membership and stays display-only: the server keeps it
        // product-agnostic on purpose, so it is never the product gate.
        const bool entitled = entitledFlag(response);

        CachedEntitlement cached;
        cached.subscribed = propertyBool(response, "subscribed", false);

        const auto subscription = nestedObject(response, "subscription");
        const auto license = nestedObject(response, "license");

        // The nested blocks say WHICH route answered. An older function that
        // sends only the flat fields would report no licence half, and the
        // answer is then attributed to the subscription the account reports -
        // the only route that function had. `subscribed` implies coverage for
        // a subscription-covered product, which is how the flat shape was
        // meant.
        cached.licenseStatus = objectHasField(license, "status")
            ? license.getProperty("status", juce::var()).toString()
            : juce::String(licenseStatusNone);
        // These two are FACTS about the licence, independent of the gate: they
        // are what lets the UI say "your licence is fine, this computer is
        // simply not activated" instead of pretending the key is bad. Only
        // `licenseEntitles` - holding a slot - can grant access.
        cached.licenseAccountActive = propertyBool(license, "account_active", false);
        cached.licenseDeviceActive = propertyBool(license, "device_active", false);
        cached.licenseEntitles = entitled && cached.licenseDeviceActive;
        cached.subscriptionEntitles = entitled
            && propertyBool(subscription, "covers_product", cached.subscribed)
            && propertyBool(subscription, "active", cached.subscribed);
        cached.devicesUsed = static_cast<int>(propertyNumber(license, "devices_used"));
        cached.deviceLimit = objectHasField(license, "device_limit")
            ? static_cast<int>(propertyNumber(license, "device_limit"))
            : deviceActivationLimit;
        cached.entitled = entitled;

        snapshot_.cached = cached;
        snapshot_.verifiedAtMs = nowMs;
        snapshot_.licensed = entitled;
        persistenceDirty_ = true;

        snapshot_.message = entitled
            ? (cached.subscriptionEntitles
                   ? "Music-Prod+ active - full access."
                   : "ChordEngine licence active on this computer - full access.")
            : (cached.licenseAccountActive
                   ? "Your ChordEngine licence is active, but this computer is not activated ("
                         + deviceUsageText(snapshot_) + "). Deactivate a computer in your "
                         + "Music-Prod account, then activate this one."
                   : "Signed in. Music-Prod+ or a ChordEngine licence key unlocks full access.");
        return;
    }

    if (status == 401)
    {
        handle401(nowMs);
        return;
    }

    if (status == 200 && response.isObject())
    {
        // A 200 that does not carry the gate is not an entitlement answer. It
        // can neither grant access nor refresh the cache: the last VERIFIED
        // answer keeps its own bounded window and a malformed reply never
        // extends it, so nothing is granted by accident. The ONE thing it may
        // update is the raw membership the account area shows, because
        // `subscribed` is a known field that grants nothing on its own - and
        // it is not persisted, so a restart still reverts to the verified
        // answer.
        snapshot_.cached.subscribed =
            propertyBool(response, "subscribed", snapshot_.cached.subscribed);
        snapshot_.message = "Music-Prod answered without the licence decision. Your access is "
                            "unchanged until it expires.";
        applyCachedEntitlement(nowMs);
        return;
    }

    // 5xx / transport failure: keep the last verified answer (the existing
    // product rule); the route's own bounded offline window is what ends
    // that grace.
    applyCachedEntitlement(nowMs);
}

// ------------------------------------------------- licence activation
void LicensingModel::activateLicense(const juce::String& licenseKey)
{
    const auto key = licenseKey.trim();
    if (key.isEmpty())
    {
        snapshot_.message = "Enter your ChordEngine licence key first.";
        return;
    }

    // The endpoint takes the identity from the token, so there is nothing to
    // activate until the device is paired. Saying so is honest; inventing a
    // device would end the three-computer rule.
    if (token_.isEmpty())
    {
        snapshot_.message = "Sign in to Music-Prod first, then activate your ChordEngine licence key.";
        return;
    }

    snapshot_.activationPending = true;
    snapshot_.message = "Activating your ChordEngine licence...";
    if (requestLicenseActivation)
        requestLicenseActivation(key);
}

void LicensingModel::onActivateLicenseReply(int status, const juce::var& response,
                                            std::int64_t nowMs)
{
    snapshot_.activationPending = false;

    // Nobody answered, or the answer is not a statement about the key. The
    // licence state is untouched: a transport failure must never be reported
    // as a rejection, and never as an activation.
    if (status == 0 || !response.isObject())
    {
        snapshot_.message = "Could not reach Music-Prod. Your licence was not activated.";
        return;
    }

    if (status == 401)
    {
        handle401(nowMs);
        return;
    }

    // A structural failure the server reports (malformed_key,
    // device_id_required, server_error). It carries `error`, never `status`,
    // because it is not a verdict on the customer's key.
    if (objectHasField(response, "error"))
    {
        const auto detail = response.getProperty("message", juce::var()).toString();
        snapshot_.message = detail.isNotEmpty()
            ? detail
            : juce::String("Your licence could not be activated. Nothing was changed.");
        return;
    }

    const auto verdict = response.getProperty("status", juce::var()).toString();
    const auto detail = response.getProperty("message", juce::var()).toString();
    const auto device = nestedObject(response, "device");

    if (verdict == "device_limit")
    {
        // An ANSWER about the licence, not about the key: every slot is in
        // use. Surfaced exactly as the server worded it, with the real slot
        // count, and without pretending this computer was activated.
        snapshot_.cached.devicesUsed = static_cast<int>(propertyNumber(device, "used"));
        snapshot_.cached.deviceLimit = deviceActivationLimit;
        if (objectHasField(device, "limit"))
            snapshot_.cached.deviceLimit = static_cast<int>(propertyNumber(device, "limit"));
        snapshot_.message = detail.isNotEmpty()
            ? detail
            : juce::String("This licence is already active on the maximum number of computers."
                           " Deactivate one in your Music-Prod account, then try again.");
        persistenceDirty_ = true;
        return;
    }

    if (verdict == "invalid" || verdict == "revoked" || verdict == "expired")
    {
        // The server made a statement about a key, and the licence route is
        // not what grants access. ONLY the licence half of the cache is
        // withdrawn: a valid Music-Prod+ entitlement is an independent route
        // and is deliberately left exactly as it was. The next entitlement
        // check re-derives everything from the server.
        snapshot_.cached.licenseAccountActive = false;
        snapshot_.cached.licenseDeviceActive = false;
        snapshot_.cached.licenseEntitles = false;
        snapshot_.cached.licenseStatus = verdict == "invalid"
            ? juce::String(licenseStatusNone)
            : verdict;
        snapshot_.cached.entitled = snapshot_.cached.subscriptionEntitles;
        snapshot_.licensed = snapshot_.cached.entitled && snapshot_.cached.subscriptionEntitles;
        snapshot_.message = detail.isNotEmpty()
            ? detail
            : juce::String("That ChordEngine licence key was not accepted.");
        persistenceDirty_ = true;
        primed_ = false; // re-derive the answer from the server immediately
        return;
    }

    if (verdict == "ok" && propertyBool(device, "activated", false))
    {
        // The server took a slot for THIS computer. The activation answer is
        // not an entitlement answer, so `licensed` is NOT set from it: the
        // model re-asks the entitlement question and lets the server's own
        // product-scoped answer open the gate.
        snapshot_.cached.licenseAccountActive = true;
        snapshot_.cached.licenseDeviceActive = true;
        snapshot_.cached.licenseEntitles = true;
        snapshot_.cached.licenseStatus = licenseStatusActive;
        snapshot_.cached.devicesUsed = static_cast<int>(propertyNumber(device, "used"));
        if (objectHasField(device, "limit"))
            snapshot_.cached.deviceLimit = static_cast<int>(propertyNumber(device, "limit"));
        snapshot_.message = detail.isNotEmpty()
            ? detail
            : juce::String("Licence activated on this computer.");
        persistenceDirty_ = true;
        primed_ = false; // verify with the server at once
        return;
    }

    // A 200 whose `ok` is false without a verdict we know: report what the
    // server said and change nothing.
    snapshot_.message = detail.isNotEmpty()
        ? detail
        : juce::String("Your licence was not activated. Nothing was changed.");
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
