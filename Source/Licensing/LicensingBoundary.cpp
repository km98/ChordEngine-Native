#include "LicensingBoundary.h"

#include <algorithm>

namespace chordengine::licensing
{
namespace
{
// JSON field readers for the auth file (a value written by this class).
bool flagAt(const platform::Json& object, const char* name, bool fallback)
{
    if (!object.isObject() || !object.has(name) || object[name].isNull())
        return fallback;
    const auto& value = object[name];
    if (value.isBool())
        return value.asBool();
    return varNumber(value) != 0.0;
}

int intAt(const platform::Json& object, const char* name, int fallback)
{
    if (!object.isObject() || !object.has(name) || object[name].isNull())
        return fallback;
    return static_cast<int>(varNumber(object[name]));
}

bool isNumber(const platform::Json& value)
{
    return value.isNumber();
}
} // namespace

LicensingBoundary::LicensingBoundary(AuthService& auth, UpdateService& update,
                                     std::filesystem::path authFile)
    : auth_(auth), update_(update), authFile_(std::move(authFile))
{
    model_.requestLinkStart = [this]
    {
        const auto weakThis = lifetime_.weak();
        auto* const owner = this;
        // The product is EXPLICIT: the endpoint must never have to infer it
        // from the display device name, and the device id travels with the
        // pairing so the token carries this installation's identity.
        auth_.start(std::string(deviceName), std::string(productVersion),
                    std::string(productSlug), deviceId_,
                    [weakThis, owner](HttpReply reply)
                    {
                        if (platform::LifetimeToken::alive(weakThis))
                            owner->deliverAuthStartReply(reply);
                    });
    };

    model_.requestLinkPoll = [this](const std::string& deviceCode)
    {
        const auto weakThis = lifetime_.weak();
        auto* const owner = this;
        auth_.poll(deviceCode,
                   [weakThis, owner](HttpReply reply)
                   {
                       if (platform::LifetimeToken::alive(weakThis))
                           owner->deliverAuthPollReply(reply);
                   });
    };

    model_.requestEntitlements = [this](const std::string& token)
    {
        const auto weakThis = lifetime_.weak();
        auto* const owner = this;
        auth_.entitlements(token,
                           [weakThis, owner](HttpReply reply)
                           {
                               if (platform::LifetimeToken::alive(weakThis))
                                   owner->deliverEntitlementsReply(reply);
                           });
    };

    model_.requestLogout = [this](const std::string& token)
    {
        const auto weakThis = lifetime_.weak();
        auto* const owner = this;
        (void) owner;
        (void) weakThis;
        auth_.logout(token, [](HttpReply) {});
    };

    model_.requestLicenseActivation = [this](const std::string& licenseKey)
    {
        const auto weakThis = lifetime_.weak();
        auto* const owner = this;
        // The device id is required by the endpoint; the label and platform
        // are display-only and bounded server-side. The key is never stored.
        auth_.activateLicense(model_.token(), licenseKey, deviceId_, deviceLabel(),
                              platform::operatingSystemName(),
                              std::string(productVersion),
                              [weakThis, owner](HttpReply reply)
                              {
                                  if (platform::LifetimeToken::alive(weakThis))
                                      owner->deliverActivateLicenseReply(reply);
                              });
    };

    model_.requestUpdateCheck = [this]
    {
        // The shipped service performs no I/O (there is no plugin-facing
        // update API), so this stays a message-thread operation.
        const auto result = update_.check();
        model_.onUpdateResult(result.available, result.latestVersion, result.status);
    };
}

LicensingBoundary::~LicensingBoundary()
{
    model_.requestLinkStart = nullptr;
    model_.requestLinkPoll = nullptr;
    model_.requestEntitlements = nullptr;
    model_.requestLogout = nullptr;
    model_.requestLicenseActivation = nullptr;
    model_.requestUpdateCheck = nullptr;
    gateSink_ = nullptr;
}

std::int64_t LicensingBoundary::nowMs() const noexcept
{
    return platform::currentTimeMillis();
}

// The owner-visible name of this computer, beside the deactivate button.
std::string LicensingBoundary::deviceLabel() const
{
    const auto name = text::trim(platform::computerName());
    return name.empty() ? std::string(deviceName) : name;
}

void LicensingBoundary::initialise()
{
    loadAuthFile();
    model_.applyCachedEntitlement(nowMs());
    publishGate();
}

void LicensingBoundary::setGateSink(std::function<void(bool)> sink)
{
    gateSink_ = std::move(sink);
    publishedGate_ = -1;
    publishGate();
}

void LicensingBoundary::publishGate()
{
    // Published on change only, so the sink is not rewritten ten times a
    // second and a gate set by someone else stays until the decision moves.
    const int open = expiredGateActive() ? 0 : 1;
    if (gateSink_ && open != publishedGate_)
    {
        publishedGate_ = open;
        gateSink_(open == 1);
    }
}

void LicensingBoundary::setInfoPageVisible(bool visible) noexcept
{
    (void) visible;
}

void LicensingBoundary::notePlaybackStarted()
{
    model_.notePlaybackStarted();
}

void LicensingBoundary::tick(double deltaMs)
{
    // One step of the reference's onTimer: the usage clock first, then the
    // auth driver (prime / poll / re-verify).
    const bool trialWasRunning = model_.trialRunning();
    const bool expiredNow = model_.advanceTrial(deltaMs);
    model_.tick();

    publishGate();

    bool save = model_.consumePersistenceDirty() || expiredNow;
    if (trialWasRunning && ++trialSaveCounter_ >= trialSaveTicks)
        save = true;
    if (save)
    {
        trialSaveCounter_ = 0;
        saveAuthFile();
    }

    if (pendingUrl_.empty() && !model_.snapshot().verificationUrl.empty())
        pendingUrl_ = model_.snapshot().verificationUrl;
}

void LicensingBoundary::refreshNow()
{
    model_.requestVerificationNow();
}

void LicensingBoundary::flushTrial()
{
    if (model_.snapshot().trialStarted || model_.snapshot().trialExpired)
        saveAuthFile();
}

bool LicensingBoundary::reloadIfChangedOnDisk(std::int64_t now)
{
    if (authFile_.empty() || platform::fileModificationTimeMs(authFile_) == 0)
        return false;
    const auto& snapshot = model_.snapshot();
    if (snapshot.polling || snapshot.activationPending || snapshot.phase == Phase::linking)
        return false;
    if (platform::fileModificationTimeMs(authFile_) <= authFileTimeMs_)
        return false;

    // Keep this session's trial progress: loading merges the stored budget
    // with it (the smaller remaining budget wins).
    const auto remaining = snapshot.trialMs;
    const auto expired = snapshot.trialExpired;
    const auto started = snapshot.trialStarted;
    loadAuthFile();
    if (expired || remaining < model_.snapshot().trialMs)
        model_.restoreTrial(remaining, expired);
    if (started)
        model_.notePlaybackStarted();
    model_.applyCachedEntitlement(now);
    publishGate();
    return true;
}

void LicensingBoundary::signInButtonClicked()
{
    model_.signInOrCancel();
    pendingUrl_ = model_.snapshot().verificationUrl;
    publishGate();
    if (model_.consumePersistenceDirty())
        saveAuthFile();
}

void LicensingBoundary::signOutButtonClicked()
{
    model_.signOut();
    publishGate();
    saveAuthFile();
}

void LicensingBoundary::cancelPendingLink()
{
    model_.cancelLink();
    pendingUrl_.clear();
}

void LicensingBoundary::checkForUpdatesClicked()
{
    model_.checkForUpdates();
}

void LicensingBoundary::activateLicenseKey(const std::string& licenseKey)
{
    model_.activateLicense(licenseKey);
    publishGate();
    if (model_.consumePersistenceDirty())
        saveAuthFile();
}

void LicensingBoundary::deliverAuthStartReply(const HttpReply& reply)
{
    if (!reply.transportOk && reply.status == 0)
    {
        model_.onAuthStartReply(0, {});
        pendingUrl_.clear();
        return;
    }

    model_.onAuthStartReply(reply.status, reply.body);
    pendingUrl_ = model_.snapshot().verificationUrl;
}

void LicensingBoundary::deliverAuthPollReply(const HttpReply& reply)
{
    const auto wasSignedIn = isSignedIn(model_.snapshot());
    if (!reply.transportOk && reply.status == 0)
        return; // transport failure: keep the code and retry on the next tick

    model_.onAuthPollReply(reply.status, reply.body);
    if (!wasSignedIn && isSignedIn(model_.snapshot()))
        saveAuthFile();
    publishGate();
}

void LicensingBoundary::deliverEntitlementsReply(const HttpReply& reply)
{
    if (!reply.transportOk && reply.status == 0)
    {
        model_.applyCachedEntitlement(nowMs());
        publishGate();
        return;
    }

    model_.onEntitlementsReply(reply.status, reply.body, nowMs());
    if (model_.consumePersistenceDirty())
        saveAuthFile();
    publishGate();
}

void LicensingBoundary::deliverActivateLicenseReply(const HttpReply& reply)
{
    if (!reply.transportOk && reply.status == 0)
    {
        model_.onActivateLicenseReply(0, {}, nowMs());
        publishGate();
        return;
    }

    model_.onActivateLicenseReply(reply.status, reply.body, nowMs());
    if (model_.consumePersistenceDirty())
        saveAuthFile();
    publishGate();
}

bool LicensingBoundary::consumeVerificationUrl(std::string& url)
{
    if (pendingUrl_.empty())
    {
        if (model_.snapshot().verificationUrl.empty())
            return false;
        url = model_.snapshot().verificationUrl;
        pendingUrl_ = url;
    }

    url = pendingUrl_;
    pendingUrl_.clear();
    model_.clearVerificationUrl();
    return !url.empty();
}

// ---------------------------------------------------------- persistence
// What is kept is exactly what the contract needs to re-derive access: the
// opaque token, this installation's device id, and the last VERIFIED
// entitlement answer with the moment it arrived. The licence key itself is
// never written: the server binds the key to the account, and the slot is
// the entitlement.
void LicensingBoundary::saveAuthFile()
{
    if (authFile_.empty())
        return;

    // The trial budget is shared by every instance on this computer: never
    // write back more time than another instance already left on disk.
    auto trialRemaining = model_.snapshot().trialMs;
    auto trialExpired = model_.snapshot().trialExpired;
    if (const auto existing = platform::readFile(authFile_, 1u << 20))
    {
        const auto onDisk = platform::Json::parse(*existing);
        if (onDisk.has_value() && onDisk->isObject())
        {
            const auto& storedRemaining = (*onDisk)["trial_remaining_ms"];
            if (isNumber(storedRemaining))
                trialRemaining = std::min(trialRemaining, storedRemaining.asDouble());
            if (flagAt(*onDisk, "trial_expired", false))
                trialExpired = true;
        }
    }
    if (trialExpired || trialRemaining <= 0.0)
    {
        trialRemaining = 0.0;
        trialExpired = true;
    }
    if (trialExpired != model_.snapshot().trialExpired || trialRemaining < model_.snapshot().trialMs)
    {
        model_.restoreTrial(trialRemaining, trialExpired);
        publishGate();
    }

    const auto& cached = model_.cached();

    auto object = platform::Json::object();
    object.set("token", model_.token());
    object.set("display_name", model_.user());
    object.set("device_id", deviceId_);
    object.set("subscribed", cached.subscribed);
    object.set("plugin_entitled", cached.entitled);
    object.set("subscription_entitles", cached.subscriptionEntitles);
    object.set("license_entitles", cached.licenseEntitles);
    object.set("license_account_active", cached.licenseAccountActive);
    object.set("license_device_active", cached.licenseDeviceActive);
    object.set("license_status", cached.licenseStatus);
    object.set("devices_used", cached.devicesUsed);
    object.set("device_limit", cached.deviceLimit);
    object.set("verified_at", static_cast<std::int64_t>(model_.verifiedAtMs()));
    object.set("trial_remaining_ms", static_cast<std::int64_t>(trialRemaining));
    object.set("trial_expired", trialExpired);
    platform::writeFileAtomically(authFile_, object.dump());
    authFileTimeMs_ = platform::fileModificationTimeMs(authFile_);
}

void LicensingBoundary::loadAuthFile()
{
    auto restoreDeviceId = [this](const std::string& stored)
    {
        // The device id is created ONCE and then kept. An id that is missing
        // or not the shape the backend accepts is replaced, so this install
        // can never present something the licence route would refuse.
        if (isDeviceIdFormat(stored))
        {
            deviceId_ = normalizeDeviceId(stored);
            return false;
        }

        deviceId_ = newDeviceId();
        return true;
    };

    const auto stored = authFile_.empty() ? std::nullopt : platform::readFile(authFile_, 1u << 20);
    if (!stored.has_value())
    {
        restoreDeviceId({});
        model_.loadToken({}, {}, {}, 0);
        saveAuthFile();
        return;
    }

    const auto document = platform::Json::parse(*stored);
    if (!document.has_value() || !document->isObject())
    {
        restoreDeviceId({});
        model_.loadToken({}, {}, {}, 0);
        saveAuthFile();
        return;
    }

    const auto& parsed = *document;
    const auto deviceIdWasCreated = restoreDeviceId(varText(parsed["device_id"]));

    CachedEntitlement cached;
    cached.entitled = flagAt(parsed, "plugin_entitled", false);
    cached.subscribed = flagAt(parsed, "subscribed", false);
    cached.subscriptionEntitles = flagAt(parsed, "subscription_entitles", false);
    cached.licenseEntitles = flagAt(parsed, "license_entitles", false);
    cached.licenseAccountActive = flagAt(parsed, "license_account_active", false);
    cached.licenseDeviceActive = flagAt(parsed, "license_device_active", false);
    cached.licenseStatus = varText(parsed["license_status"]);
    if (cached.licenseStatus.empty())
        cached.licenseStatus = licenseStatusNone;
    cached.devicesUsed = intAt(parsed, "devices_used", 0);
    cached.deviceLimit = intAt(parsed, "device_limit", deviceActivationLimit);

    // A cache that claims access but names no route is not trusted, and
    // `licensed` is re-derived by applyCachedEntitlement from the route's own
    // window - never from the stored flag alone.
    if (offlineGraceDays(cached) <= 0.0)
    {
        cached.entitled = false;
        cached.licenseEntitles = false;
        cached.subscriptionEntitles = false;
    }

    const auto token = varText(parsed["token"]);
    const auto user = varText(parsed["display_name"]);
    const auto verifiedAt = static_cast<std::int64_t>(varNumber(parsed["verified_at"]));

    model_.loadToken(token, user, cached, verifiedAt);

    // The remaining trial budget. A file written before it was persisted
    // has none, and keeps the budget this model already holds.
    const auto& trialRemaining = parsed["trial_remaining_ms"];
    if (isNumber(trialRemaining))
        model_.restoreTrial(trialRemaining.asDouble(), flagAt(parsed, "trial_expired", false));
    else if (flagAt(parsed, "trial_expired", false))
        model_.restoreTrial(0.0, true);
    authFileTimeMs_ = platform::fileModificationTimeMs(authFile_);

    if (deviceIdWasCreated)
        saveAuthFile();
}
} // namespace chordengine::licensing
